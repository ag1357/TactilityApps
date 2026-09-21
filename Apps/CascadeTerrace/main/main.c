/* Tactility 0.8.0-dev native external application; no separate game runtime. */
#include "game.h"
#include "semantic.h"
#include "render.h"
#include <app/event.h>
#include <app/paths.h>
#include <app/scheduler.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <lvgl/devices/keyboard.h>
#include <lvgl/lvgl.h>
#include <lvgl_window_manager/window_manager.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <tactility/freertos/task.h>
#include <tactility/memory.h>
#include <time.h>
static Game* g;
static Renderer* r;
static Conversation conversation;
static Reply reply;
static lv_obj_t *canvas, *textarea;
static uint16_t* canvas_pixels;
static Input input;
static int pending, talking, experimental, reply_offset;
static char submitted[128];
static char save_base[256] = "/sdcard/cascade.save";
static FILE* telemetry;
static void emit(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n < 0) return;
    printf("%s", line);
    if (telemetry) {
        fwrite(line, 1, strlen(line), telemetry);
        fflush(telemetry);
    }
}
static uint64_t micros(void) { return (uint64_t)esp_timer_get_time(); }
static void button(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) pending = (int)(intptr_t)lv_event_get_user_data(e);
}
static void touch(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        input.forward = input.strafe = input.turn = 0;
        return;
    }
    if (code != LV_EVENT_PRESSING) return;
    lv_indev_t* d = lv_indev_active();
    if (!d) return;
    lv_point_t p;
    lv_indev_get_point(d, &p);
    int width = lv_obj_get_width(lv_obj_get_parent(canvas));
    if (p.x < width / 2) {
        input.forward = p.y < 200 ? 1000 : -1000;
        input.strafe = p.x < width / 6 ? -600 : p.x > width / 3 ? 600
                                                                : 0;
    } else
        input.turn = p.x < width * 3 / 4 ? 2 : -2;
}
static void submit(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        snprintf(submitted, sizeof(submitted), "%s", lv_textarea_get_text(textarea));
        pending = 10;
        lv_textarea_set_text(textarea, "");
    }
}
static void create(lv_obj_t* root, void* data) {
    (void)data;
    canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(canvas, canvas_pixels, W * 2, H * 2, LV_COLOR_FORMAT_RGB565);
    /* Explicit nearest-neighbor upscale; PPA optimization remains unqualified. */
    lv_obj_center(canvas);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, touch, LV_EVENT_ALL, NULL);
    const char* labels[] = {"Jump", "Talk", "Repair", "Meditate", "Condense", "Buy", "Save", "Wait", "Show", "Pick", "Page"};
    for (int i = 0; i < 11; i++) {
        lv_obj_t* b = lv_button_create(root);
        lv_obj_set_size(b, 42, 28);
        lv_obj_set_pos(b, i * 43, lv_obj_get_height(root) - 30);
        lv_obj_add_event_cb(b, button, LV_EVENT_CLICKED, (void*)(intptr_t)(i >= 9 ? i + 2 : i + 1));
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, labels[i]);
        lv_obj_center(l);
    }
    textarea = lv_textarea_create(root);
    lv_obj_set_size(textarea, LV_PCT(100), 40);
    lv_obj_set_pos(textarea, 0, lv_obj_get_height(root) - 75);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_max_length(textarea, 127);
    lv_textarea_set_placeholder_text(textarea, "CardKB2: type, then Enter");
    /* Add to an explicit hardware-only group: no software keyboard is created. */
    lv_group_t* group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, textarea);
        lv_group_focus_obj(textarea);
    }
    lv_obj_add_event_cb(textarea, submit, LV_EVENT_READY, NULL);
    lv_obj_add_flag(textarea, LV_OBJ_FLAG_HIDDEN);
}
static void destroy(void* data) {
    (void)data;
    canvas = textarea = NULL;
    input = (Input) {0};
}
static void action(int code, const char* text) {
    Operation o = {OP_NONE, PLAYER_ID, 0, IT_CHIT, 1, NULL};
    switch (code) {
        case 1:
            return;
        case 2:
            talking = !talking;
            return;
        case 3:
            o.op = REPAIR;
            o.item = IT_COUPLING;
            break;
        case 4:
            o.op = EXTRACT;
            o.amount = 20;
            break;
        case 5:
            o.op = CONDENSE;
            break;
        case 6:
            o.op = BUY;
            o.item = IT_COUPLING;
            o.target = 3;
            break;
        case 7: {
            uint64_t t = micros();
            int ok = save_game(g, save_base);
            emit("{\"type\":\"save\",\"ok\":%d,\"us\":%llu}\n", ok, (unsigned long long)(micros() - t));
            return;
        }
        case 8:
            o.op = WAIT;
            o.amount = 60;
            break;
        case 9:
            o.op = SHOW;
            o.item = g->state.player.quantity[IT_CABLE] ? IT_CABLE : IT_LOG;
            o.target = KYRA_ID;
            break;
        case 11:
            o.op = PICK_UP;
            o.item = (g->world.variant != 1 && !g->state.player.quantity[IT_CABLE]) ? IT_CABLE : IT_LOG;
            break;
        case 12:
            reply_offset += 192;
            if (reply_offset >= (int)strlen(reply.text)) reply_offset = 0;
            return;
        case 10: {
            Pos n = npc_position(g);
            int64_t dx = n.x - g->state.player_pos.x, dz = n.z - g->state.player_pos.z;
            if (dx * dx + dz * dz > 2200LL * 2200) {
                snprintf(reply.text, sizeof(reply.text), "Come within two metres of Kyra.");
                return;
            }
            g->state.yaw = (int)(atan2((double)dx, (double)dz) * 180 / 3.141592653589793 + 360) % 360;
            uint64_t t = micros();
            reply_offset = 0;
            dialogue(g, &conversation, text, experimental, &reply);
            emit("{\"type\":\"dialogue\",\"us\":%llu,\"facts\":%u,\"abstained\":%u}\n", (unsigned long long)(micros() - t), reply.count, reply.abstained);
            return;
        }
        default:
            return;
    }
    if (!game_apply(g, o)) snprintf(g->notice, sizeof(g->notice), "Requirements unmet or out of reach.");
}
int main(int argc, char** argv) {
    int qualify = argc > 1 && !strcmp(argv[1], "--qualify");
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--experimental-composition")) experimental = 1;
        else if (!strcmp(argv[i], "--patched-cognition")) experimental = 2;
        else if (!strcmp(argv[i], "--general-cognition")) experimental = 3;
        else if (!strcmp(argv[i], "--learned-cognition")) experimental = 4;
    char dir[256], asset[256];
    if (app_paths_get_user_data_directory("ag1357.cascadeterrace", dir, sizeof(dir)) == ERROR_NONE) mkdir(dir, 0755);
    app_paths_get_user_data_path("ag1357.cascadeterrace", "cascade.save", save_base, sizeof(save_base));
    if (app_paths_get_assets_path("ag1357.cascadeterrace", "qualification.flag", asset, sizeof(asset)) == ERROR_NONE) {
        FILE* flag = fopen(asset, "rb");
        if (flag) {
            qualify = 1;
            fclose(flag);
        }
    }
    if (app_paths_get_assets_path("ag1357.cascadeterrace", "cognition.mode", asset, sizeof(asset)) == ERROR_NONE) {
        FILE* mode_file=fopen(asset,"rb");
        if(mode_file){int m=fgetc(mode_file);if(m>='0'&&m<='4')experimental=m-'0';fclose(mode_file);}
    }
    if (app_paths_get_user_data_path("ag1357.cascadeterrace", "qualification.jsonl", asset, sizeof(asset)) == ERROR_NONE) telemetry = fopen(asset, "ab");
    if (app_paths_get_assets_path("ag1357.cascadeterrace", "kyra.mesh", asset, sizeof(asset)) == ERROR_NONE) render_load_assets(asset);
    struct MemoryPolicy external = {MEMORY_CAPABILITY_EXTERNAL, 0, 16};
    g = memory_calloc_with_policy(1, sizeof(Game), &external);
    r = memory_calloc_with_policy(1, sizeof(Renderer), &external);
    canvas_pixels = memory_alloc_with_policy(W * H * 8, &external);
    if (!g || !r || !canvas_pixels) {
        memory_free(g);
        memory_free(r);
        memory_free(canvas_pixels);
        return 2;
    }
    uint64_t t = micros();
    game_new(g, 42, -1);
    uint64_t gen_us = micros() - t;
    int reloaded = load_game(g, save_base);
    emit("{\"type\":\"boot\",\"platform\":\"esp32p4\",\"generator\":%u,\"generation_us\":%llu,\"reload\":%d,\"explicit_psram_bytes\":%zu,\"clock_resolution_us\":%u}\n", GEN_VERSION, (unsigned long long)gen_us, reloaded, sizeof(Game) + sizeof(Renderer) + W * H * 8, 1U);
    memory_print_stats();
    emit("{\"type\":\"heap\",\"internal_free\":%u,\"psram_free\":%u}\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (qualify) {
        emit("{\"type\":\"cognition_config\",\"mode\":%d,\"model_bytes\":101432,\"view_bytes\":%zu,\"result_bytes\":%zu,\"context_bytes\":%zu}\n",experimental,sizeof(CgView),sizeof(CgResult),sizeof(CgContext));
        /* Language probes exercise actual local mode with the live authorized view.
           They carry no expected answers and do not alter the player's game. */
        const char* probes[]={"What is your occupation?","What is the source of that?","What is Oren occupation?","What is their relationship?","What is the weather on Mars?"};
        Conversation probe_context={0}; Reply probe_reply;
        for(int i=0;i<5;i++){
            t=micros();dialogue(g,&probe_context,probes[i],experimental,&probe_reply);
            emit("{\"type\":\"cognition_sample\",\"mode\":%d,\"probe\":%d,\"us\":%llu,\"count\":%u,\"abstained\":%u,\"text_crc32\":%u}\n",experimental,i,(unsigned long long)(micros()-t),probe_reply.count,probe_reply.abstained,crc32(probe_reply.text,strlen(probe_reply.text)));
        }
        for (int i = 0; i < 120; i++) {
            t = micros();
            render(r, g);
            emit("{\"type\":\"render_sample\",\"n\":%d,\"us\":%llu,\"triangles\":%u}\n", i, (unsigned long long)(micros() - t), (unsigned)r->triangles);
        }
        action(7, NULL);
    }
    struct TaskEventGroup events = {0};
    task_event_group_construct(&events);
    struct AppEventSubscription sub = {0};
    if (app_event_subscribe(&sub, &events) != ERROR_NONE) return 2;
    WindowId window = window_manager_create_ext(app_scheduler_current_app_id(), create, destroy, NULL);
    int closing = 0;
    uint64_t previous = micros();
    while (!closing) {
        struct AppEvent event;
        while (app_event_poll(&sub, &event) == ERROR_NONE)
            if (event.type == APP_EVENT_CLOSE) closing = 1;
        lvgl_lock();
        Input in = input;
        input.jump = 0;
        int cmd = pending;
        pending = 0;
        char text[128];
        snprintf(text, sizeof(text), "%s", submitted);
        lvgl_unlock();
        if (cmd) action(cmd, text);
        if (cmd == 1) in.jump = 1;
        uint64_t current = micros();
        uint64_t period_us = current - previous;
        if (!talking) game_tick(g, in, (uint32_t)((current - previous) / 1000));
        previous = current;
        t = micros();
        r->conversation = talking;
        render(r, g);
        uint64_t frame_us = micros() - t;
        draw_panel(r, 0, 0, W, 12, 0x1108);
        char hud[80];
        snprintf(hud, sizeof(hud), "CASCADE | %d CHITS | %d PU", (int)g->state.player.quantity[IT_CHIT], (int)(g->state.player.quantity[19] / 1000));
        draw_text(r, 2, 2, hud, 0xffff, 238);
        if (talking) {
            draw_panel(r, 0, 80, W, 65, 0x1108);
            draw_text(r, 2, 81, reply.text + reply_offset, 0xffff, 238);
        } else {
            draw_panel(r, 0, 130, W, 25, 0x1108);
            draw_text(r, 2, 131, g->notice, 0xffff, 238);
        }
        lvgl_lock();
        if (canvas) {
            for (int y = 0; y < H * 2; y++)
                for (int x = 0; x < W * 2; x++) canvas_pixels[y * W * 2 + x] = r->pixels[(y / 2) * W + x / 2];
            lv_obj_invalidate(canvas);
            if (talking) lv_obj_remove_flag(textarea, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(textarea, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_unlock();
        if (qualify && r->frame % 30 == 0) emit("{\"type\":\"frame\",\"period_us\":%llu,\"work_us\":%llu,\"render_us\":%llu}\n", (unsigned long long)period_us, (unsigned long long)(micros() - current), (unsigned long long)frame_us);
        task_event_group_wait_any(&events, NULL, pdMS_TO_TICKS(20));
    }
    save_game(g, save_base);
    window_manager_remove(window);
    app_event_unsubscribe(&sub);
    task_event_group_destruct(&events);
    memory_free(canvas_pixels);
    memory_free(r);
    memory_free(g);
    if (telemetry) fclose(telemetry);
    return 0;
}
