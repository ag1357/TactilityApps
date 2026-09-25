/* Native Tactility frontend. World authority and save vocabulary stay in core/content. */
#include "game.h"
#include "render.h"
#include "viewport.h"
#include "action_input.h"
#include "interaction.h"
#include "i2c_input.h"
#include "platform_lifecycle.h"
#include <app/event.h>
#include <app/paths.h>
#include <app/scheduler.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl_window_manager/window_manager.h>
#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/concurrent/thread.h>
#include <tactility/memory.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { PLAY, MENU, CONTROLS, CAPTURE, CONFLICT, CONVERSATION };
enum { CMD_NONE, CMD_MENU, CMD_RESUME, CMD_SAVE, CMD_CONTROLS, CMD_QUIT,
       CMD_DEFAULTS, CMD_CANCEL, CMD_REPLACE, CMD_SUBMIT, CMD_PAGE, CMD_INTERACT,
       CMD_BIND=100 };
static Game *g;
static Renderer *r;
static AiInput actions;
static SemaphoreHandle_t input_mutex;
static Thread *sampler,*i2c_worker;
static AiInput binding_snapshot;
static CtI2cInput peripherals;
static struct TaskEventGroup events, sampler_events, i2c_events;
static uint32_t window_bit, sampler_bit, i2c_bit;
static atomic_int mode, command;
static CtLifecycle lifecycle;
static lv_obj_t *root_widget,*canvas,*overlay,*textarea,*hint,*move_feedback,*look_feedback;
static lv_obj_t *focus_buttons[32];
static int focus_count,focus_index,navigation_held,first_person,reply_offset,qualify;
static uint16_t *canvas_pixels;
static size_t canvas_capacity;
static CtViewport viewport;
static unsigned viewport_stride;
static Conversation conversation;
static CtInteraction conversation_target;
static Reply reply;
static AiBinding conflict_binding;
static char ui_notice[192];
static unsigned i2c_epoch;
static char submitted[128],save_path[256],binding_path[256],i2c_path[256],telemetry_path[256];
static FILE *telemetry;
static uint32_t input_samples,input_events,input_disconnects,input_max_us,input_min_us;
static uint64_t input_total_us;
static uint32_t last_ui_capture;
static atomic_int menu_steps;
static AiControl last_source;
static int last_source_value;
static lv_indev_t *owned_keypads[16];
static unsigned owned_keypad_count;
static uint64_t micros(void) { return (uint64_t)esp_timer_get_time(); }
static void emit(const char *fmt,...) {
    if(!qualify)return;
    char line[512];va_list args;va_start(args,fmt);vsnprintf(line,sizeof(line),fmt,args);va_end(args);
    printf("%s",line);
    if(telemetry){fputs(line,telemetry);fclose(telemetry);telemetry=fopen(telemetry_path,"ab");}
}
static void lock_input(void) { xSemaphoreTake(input_mutex,portMAX_DELAY); }
static void unlock_input(void) { xSemaphoreGive(input_mutex); }
static void wake(void) {
    task_event_group_signal(&events,window_bit);
    task_event_group_signal(&sampler_events,sampler_bit);
    task_event_group_signal(&i2c_events,i2c_bit);
}
/* LVGL owns pointer lifetimes. Restore only indevs this window claimed and still live.
 * The current SDK has no exported Device->indev lookup or exclusive input lease;
 * the foreground window claims keypad streams, while other indev types stay active. */
static void keyboards_latched(int take) {
    /* Prune removed indevs before claiming newly hotplugged ones. */
    unsigned count=0;
    for(unsigned i=0;i<owned_keypad_count;i++) {
        lv_indev_t *live=NULL;while((live=lv_indev_get_next(live)))if(live==owned_keypads[i])break;
        if(live)owned_keypads[count++]=live;
    }
    owned_keypad_count=count;
    lv_indev_t *d=NULL;
    while((d=lv_indev_get_next(d))) {
        if(lv_indev_get_type(d)!=LV_INDEV_TYPE_KEYPAD)continue;
        unsigned i=0;while(i<owned_keypad_count&&owned_keypads[i]!=d)i++;
        if(take&&i==owned_keypad_count&&owned_keypad_count<16) {
            owned_keypads[owned_keypad_count++]=d;lv_indev_enable(d,false);
        } else if(!take&&i<owned_keypad_count)lv_indev_enable(d,true);
    }
    if(!take)owned_keypad_count=0;
}
static void clear_input(void) {
    lock_input();ai_clear(&actions,micros());ai_disconnect(&actions,1,micros());unlock_input();
}
/* Window-manager create/destroy callbacks run with the LVGL lock held. Make those
 * callbacks the sole owner of lifecycle input clearing: stale state is cleared
 * before a grant is published and immediately after a revoke is published.
 * Epoch observers must never clear input later, because a sampler may already
 * have accepted a real post-grant keypress by then. */
static void lifecycle_grant_locked(void) {
    clear_input();
    ct_lifecycle_grant(&lifecycle);
}
static void lifecycle_revoke_locked(void) {
    ct_lifecycle_revoke(&lifecycle);
    clear_input();
}
static int i2c_event_admitted(unsigned event_epoch) {
    return atomic_load(&lifecycle.granted) && !atomic_load(&lifecycle.closing) &&
           event_epoch == atomic_load(&lifecycle.epoch);
}
/* Capture reserves existing digital Menu/Back bindings as cancellation controls. */
static int capture_cancel_source(AiControl source,int value) {
    if(atomic_load(&mode)!=CAPTURE||!value||source.kind!=AI_DIGITAL)return 0;
    for(unsigned i=0;i<actions.binding_count;i++) {
        AiBinding *b=&actions.bindings[i];
        if((b->action==AI_MENU||b->action==AI_BACK)&&b->source.backend==source.backend&&
           b->source.control==source.control&&b->source.kind==source.kind&&
           (!b->source.device||b->source.device==source.device))return 1;
    }
    return 0;
}
static void i2c_emit(void *context,uint32_t instance,AiControl source,int value,uint64_t now) {
    (void)context;
    unsigned event_epoch=i2c_epoch;
    if(!i2c_event_admitted(event_epoch))return;
    int m=atomic_load(&mode);
    if(source.backend==AI_BACKEND_CARDKB2) {
        if(source.control==10)source.control=13;
        if(m!=CONVERSATION&&source.control>='A'&&source.control<='Z')source.control=(uint16_t)(source.control+'a'-'A');
    }
    if(source.backend==AI_BACKEND_CARDKB2&&value) {
        unsigned key=source.control;
        if(m==CONVERSATION) {
            lvgl_lock();
            if(!i2c_event_admitted(event_epoch)){lvgl_unlock();return;}
            if(textarea&&atomic_load(&mode)==CONVERSATION) {
                if(key==27)atomic_store(&command,CMD_RESUME);
                else if(key==13){snprintf(submitted,sizeof(submitted),"%s",lv_textarea_get_text(textarea));lv_textarea_set_text(textarea,"");atomic_store(&command,CMD_SUBMIT);}
                else if(key==8||key==127)lv_textarea_delete_char(textarea);
                else if(key>=32&&key<127){char text[2]={(char)key,0};lv_textarea_add_text(textarea,text);}
            }
            lvgl_unlock();wake();return;
        }
        if(m==MENU||m==CONTROLS||m==CONFLICT) {
            if(!i2c_event_admitted(event_epoch))return;
            if(key=='w'||key=='W'){atomic_fetch_sub(&menu_steps,1);wake();return;}
            if(key=='s'||key=='S'){atomic_fetch_add(&menu_steps,1);wake();return;}
        }
        if(key>='A'&&key<='Z')source.control=(uint16_t)(key+'a'-'A');
    }
    lock_input();
    /* The callback may have waited behind a transition's input clear. Recheck
     * admission while holding the same mutex so a delayed old-epoch sample
     * cannot repopulate freshly cleared state. */
    if(!i2c_event_admitted(event_epoch)){unlock_input();return;}
    if(capture_cancel_source(source,value))atomic_store(&command,CMD_CANCEL);
    else ai_device_event(&actions,instance,source,value,now);
    last_source=source;last_source_value=value;input_events++;unlock_input();
}
static void i2c_disconnect(void *context,uint32_t instance,uint64_t now) {
    (void)context;unsigned event_epoch=i2c_epoch;lock_input();
    if(i2c_event_admitted(event_epoch)){ai_disconnect(&actions,instance,now);input_disconnects++;}
    unlock_input();
}
typedef struct { uint32_t id,instance;unsigned seen; } KeyboardSource;
static KeyboardSource keyboard_sources[8];
static uint32_t next_instance=2;
static unsigned scan_serial;
static uint32_t stable_device(const struct Device *d) {
    uint32_t hash=2166136261u;
    for(;d;d=d->parent) {
        const unsigned char *s=(const unsigned char*)(d->name?d->name:"");
        for(;*s;s++)hash=(hash^*s)*16777619u;
        hash=(hash^(uint32_t)d->address)*16777619u;
    }
    return hash?hash:1;
}
static unsigned normalize_key(uint32_t key) {
    if(key>='A'&&key<='Z')return key+('a'-'A');
    switch(key) {
        case CODEPOINT_ARROW_LEFT:return AI_KEY_LEFT;
        case CODEPOINT_ARROW_RIGHT:return AI_KEY_RIGHT;
        case CODEPOINT_ARROW_UP:return AI_KEY_UP;
        case CODEPOINT_ARROW_DOWN:return AI_KEY_DOWN;
        default:return key;
    }
}
/* Called under LVGL then device-ledger lock; driver read is nonblocking and capped.
 * Revoke callback therefore cannot race a keyboard read or restore a stream mid-read. */
static bool keyboard_drain(struct Device *device,void *context) {
    (void)context;
    if(!device_is_ready(device))return true;
    uint32_t id=stable_device(device);KeyboardSource *s=NULL,*empty=NULL;
    for(unsigned i=0;i<8;i++){if(keyboard_sources[i].id==id)s=&keyboard_sources[i];if(!keyboard_sources[i].id)empty=&keyboard_sources[i];}
    if(!s){s=empty;if(!s)return true;*s=(KeyboardSource){id,next_instance++,0};}
    s->seen=scan_serial;
    struct KeyboardKeyData data;
    for(unsigned n=0;n<32;n++) {
        if(keyboard_read_key(device,&data)!=ERROR_NONE||!data.key)break;
        unsigned key=normalize_key(data.key);
        if(atomic_load(&mode)==CAPTURE&&key==AI_KEY_ESCAPE&&data.pressed) {
            atomic_store(&command,CMD_CANCEL);continue;
        }
        if(key>UINT16_MAX)continue;
        lock_input();
        AiControl source={AI_BACKEND_KEYBOARD,(uint16_t)key,id,AI_DIGITAL};
        if(capture_cancel_source(source,data.pressed))atomic_store(&command,CMD_CANCEL);
        else ai_device_event(&actions,s->instance,source,data.pressed,micros());
        last_source=source;last_source_value=data.pressed;input_events++;
        unlock_input();
    }
    return true;
}
static int32_t input_task(void *context) {
    (void)context;
    uint64_t previous=0;unsigned epoch=atomic_load(&lifecycle.epoch);int owned_last=0;
    while(!atomic_load(&lifecycle.closing)) {
        unsigned current_epoch=atomic_load(&lifecycle.epoch);
        int owns=atomic_load(&lifecycle.granted)&&atomic_load(&mode)!=CONVERSATION;
        if(current_epoch!=epoch||owns!=owned_last) {
            /* Grant/revoke/mode owners already cleared stale state while holding
             * LVGL. An observer must only reset its own bookkeeping here; a
             * second clear can erase input sampled after the transition. */
            epoch=current_epoch;previous=0;owned_last=owns;
            /* LVGL may have consumed releases while we did not own this stream. */
            lock_input();for(unsigned i=0;i<8;i++)if(keyboard_sources[i].id){ai_disconnect(&actions,keyboard_sources[i].instance,micros());keyboard_sources[i]=(KeyboardSource){0};}unlock_input();
        }
        if(!atomic_load(&lifecycle.granted)) {
            task_event_group_wait_any(&sampler_events,NULL,portMAX_DELAY);continue;
        }
        uint64_t begin=micros();
        lvgl_lock();
        if(atomic_load(&lifecycle.granted)) {
            int text=atomic_load(&mode)==CONVERSATION;
            keyboards_latched(!text);
            if(!text) {
                ++scan_serial;
                device_for_each_of_type(&KEYBOARD_TYPE,NULL,keyboard_drain);
                lock_input();
                for(unsigned i=0;i<8;i++)if(keyboard_sources[i].id&&keyboard_sources[i].seen!=scan_serial) {
                    ai_disconnect(&actions,keyboard_sources[i].instance,micros());keyboard_sources[i]=(KeyboardSource){0};input_disconnects++;
                }
                unlock_input();
            }
        }
        lvgl_unlock();
        lock_input();
        if(previous) {
            uint32_t dt=(uint32_t)(begin-previous);input_total_us+=dt;
            if(dt>input_max_us)input_max_us=dt;if(!input_min_us||dt<input_min_us)input_min_us=dt;
            input_samples++;
        }
        unlock_input();previous=begin;
        uint64_t work=micros()-begin;
        TickType_t wait=pdMS_TO_TICKS(work>=10000?1:(10000-work+999)/1000);
        task_event_group_wait_any(&sampler_events,NULL,wait?wait:1);
    }
    clear_input();return 0;
}
/* Bus calls have bounded transfer timeouts, but the generic controller may wait
 * for another owner. Keep that dependency off the keyboard acquisition task. */
static int32_t i2c_task(void *context) {
    (void)context;
    while(!atomic_load(&lifecycle.closing)) {
        if(!atomic_load(&lifecycle.granted)) {
            task_event_group_wait_any(&i2c_events,NULL,portMAX_DELAY);continue;
        }
        i2c_epoch=atomic_load(&lifecycle.epoch);
        ct_i2c_input_poll(&peripherals,micros());
        task_event_group_wait_any(&i2c_events,NULL,pdMS_TO_TICKS(10)?pdMS_TO_TICKS(10):1);
    }
    ct_i2c_input_close(&peripherals,micros());return 0;
}
static void ui_command(lv_event_t *e) { atomic_store(&command,(int)(intptr_t)lv_event_get_user_data(e));wake(); }
static void touch_button(lv_event_t *e) {
    unsigned a=(unsigned)(uintptr_t)lv_event_get_user_data(e);lv_event_code_t code=lv_event_get_code(e);
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_RELEASED&&code!=LV_EVENT_PRESS_LOST)return;
    lock_input();ai_device_event(&actions,1,(AiControl){AI_BACKEND_TOUCH,(uint16_t)a,1,AI_DIGITAL},code==LV_EVENT_PRESSED,micros());unlock_input();
}
static int touch_zone=-1;
static void touch(lv_event_t *e) {
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST) {
        lock_input();for(unsigned a=0;a<4;a++)ai_device_event(&actions,1,(AiControl){AI_BACKEND_TOUCH,a,1,AI_ANALOG},0,micros());unlock_input();touch_zone=-1;return;
    }
    if((code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING)||atomic_load(&mode)!=PLAY)return;
    lv_indev_t *d=lv_indev_active();if(!d)return;lv_point_t p;lv_indev_get_point(d,&p);
    int x,y;if(!ct_viewport_inverse(&viewport,p.x,p.y,&x,&y)) {
        lock_input();for(unsigned a=0;a<4;a++)ai_device_event(&actions,1,(AiControl){AI_BACKEND_TOUCH,a,1,AI_ANALOG},0,micros());unlock_input();touch_zone=-1;return;
    }
    if(touch_zone<0)touch_zone=x<W/2?0:1;
    int cx=touch_zone?W*3/4:W/4,cy=H*3/4;
    int vx=(x-cx)*1000/(W/4),vy=(cy-y)*1000/(H/4);
    lock_input();
    ai_device_event(&actions,1,(AiControl){AI_BACKEND_TOUCH,touch_zone?AI_LOOK_X:AI_MOVE_X,1,AI_ANALOG},vx,micros());
    ai_device_event(&actions,1,(AiControl){AI_BACKEND_TOUCH,touch_zone?AI_LOOK_Y:AI_MOVE_Y,1,AI_ANALOG},vy,micros());unlock_input();
}
static lv_obj_t *add_button(lv_obj_t *parent,const char *label,int cmd) {
    lv_obj_t *b=lv_button_create(parent);lv_obj_set_width(b,LV_PCT(100));lv_obj_set_height(b,LV_SIZE_CONTENT);
    lv_obj_add_event_cb(b,ui_command,LV_EVENT_CLICKED,(void*)(intptr_t)cmd);
    lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,label);lv_obj_set_width(l,LV_PCT(100));
    if(focus_count<32)focus_buttons[focus_count++]=b;return b;
}
static void set_focus(int n) {
    if(!focus_count)return;
    if(focus_index<focus_count)lv_obj_remove_state(focus_buttons[focus_index],LV_STATE_FOCUSED);
    focus_index=(n+focus_count)%focus_count;lv_obj_add_state(focus_buttons[focus_index],LV_STATE_FOCUSED);
    lv_obj_scroll_to_view(focus_buttons[focus_index],LV_ANIM_OFF);
}
static void menu_build(void) {
    if(!root_widget)return;
    if(overlay){lv_obj_delete(overlay);overlay=NULL;}
    focus_count=focus_index=0;int m=atomic_load(&mode);
    if(m==PLAY||m==CONVERSATION)return;
    overlay=lv_obj_create(root_widget);lv_obj_set_size(overlay,LV_PCT(100),LV_PCT(100));
    lv_obj_set_flex_flow(overlay,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(overlay,8,0);
    lv_obj_t *title=lv_label_create(overlay);lv_label_set_text(title,m==MENU?"Anaphorum":m==CONTROLS?"Controls":m==CONFLICT?"Binding conflict":"Bind New");
    if(m==MENU){add_button(overlay,"Resume",CMD_RESUME);add_button(overlay,"Save",CMD_SAVE);add_button(overlay,"Controls",CMD_CONTROLS);add_button(overlay,"Quit",CMD_QUIT);}
    if(m==CONTROLS) {
        AiBinding rows[AI_MAX_BINDINGS];unsigned row_count;
        lock_input();row_count=actions.binding_count;memcpy(rows,actions.bindings,row_count*sizeof(*rows));unlock_input();
        for(unsigned a=0;a<AI_ACTION_COUNT;a++) {
            char text[1024];snprintf(text,sizeof(text),"%s",ai_action_name((AiAction)a));
            for(unsigned i=0;i<row_count;i++)if(rows[i].action==a) {
                char b[100];ai_format_binding(&rows[i],b,sizeof(b));
                size_t len=strlen(text);snprintf(text+len,sizeof(text)-len,"\n%s",b);
            }
            lv_obj_t *label=lv_label_create(overlay);lv_obj_set_width(label,LV_PCT(100));lv_label_set_text(label,text);
            if(a<4){add_button(overlay,"Bind New +",CMD_BIND+(int)a*2);add_button(overlay,"Bind New -",CMD_BIND+(int)a*2+1);}
            else add_button(overlay,"Bind New",CMD_BIND+(int)a*2);
        }
        add_button(overlay,"Restore defaults",CMD_DEFAULTS);add_button(overlay,"Back",CMD_MENU);
    }
    if(m==CAPTURE) {
        lv_obj_t *label=lv_label_create(overlay);lv_label_set_text(label,"Release controls, then press a key/button\nor move one analog direction.\nEscape or Cancel leaves bindings unchanged.");lv_obj_set_width(label,LV_PCT(100));
        add_button(overlay,"Cancel",CMD_CANCEL);
    }
    if(m==CONFLICT) {
        char b[160];ai_format_binding(&conflict_binding,b,sizeof(b));lv_obj_t *label=lv_label_create(overlay);lv_label_set_text(label,b);lv_obj_set_width(label,LV_PCT(100));
        add_button(overlay,"Replace conflicting binding",CMD_REPLACE);add_button(overlay,"Cancel",CMD_CANCEL);
    }
    if(ui_notice[0]){lv_obj_t *notice=lv_label_create(overlay);lv_obj_set_width(notice,LV_PCT(100));lv_label_set_text(notice,ui_notice);}
    set_focus(0);
}
static void layout(lv_event_t *event) {
    (void)event;if(!root_widget||!canvas)return;
    lv_area_t a;lv_obj_get_coords(root_widget,&a);
    CtViewport next=ct_viewport_fit(a.x1,a.y1,lv_area_get_width(&a),lv_area_get_height(&a),W,H);
    unsigned stride=((unsigned)next.w*2+LV_DRAW_BUF_STRIDE_ALIGN-1)/LV_DRAW_BUF_STRIDE_ALIGN*LV_DRAW_BUF_STRIDE_ALIGN/2;
    size_t bytes=(size_t)stride*next.h*sizeof(uint16_t);
    if(!bytes||bytes>16*1024*1024){lv_obj_add_flag(canvas,LV_OBJ_FLAG_HIDDEN);return;}
    if(bytes>canvas_capacity) {
        struct MemoryPolicy policy={MEMORY_CAPABILITY_EXTERNAL,0,LV_DRAW_BUF_ALIGN};uint16_t *pixels=memory_alloc_with_policy(bytes,&policy);
        if(!pixels){lv_obj_add_flag(canvas,LV_OBJ_FLAG_HIDDEN);return;}
        memory_free(canvas_pixels);canvas_pixels=pixels;canvas_capacity=bytes;
    }
    viewport=next;viewport_stride=stride;memset(canvas_pixels,0,bytes);
    lv_canvas_set_buffer(canvas,canvas_pixels,viewport.w,viewport.h,LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas,viewport.x-a.x1,viewport.y-a.y1);lv_obj_remove_flag(canvas,LV_OBJ_FLAG_HIDDEN);
}
static void submit(lv_event_t *e) {
    if(lv_event_get_code(e)==LV_EVENT_READY){snprintf(submitted,sizeof(submitted),"%s",lv_textarea_get_text(textarea));lv_textarea_set_text(textarea,"");atomic_store(&command,CMD_SUBMIT);wake();}
    if(lv_event_get_code(e)==LV_EVENT_KEY&&lv_event_get_key(e)==LV_KEY_ESC){atomic_store(&command,CMD_RESUME);wake();}
}
static void create(lv_obj_t *root,void *context) {
    (void)context;root_widget=root;
    lv_obj_set_style_pad_all(root,0,0);lv_obj_set_style_border_width(root,0,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(root,lv_color_black(),0);
    canvas=lv_canvas_create(root);layout(NULL);
    lv_obj_add_event_cb(root,layout,LV_EVENT_SIZE_CHANGED,NULL);lv_obj_add_flag(canvas,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(canvas,touch,LV_EVENT_ALL,NULL);
    lv_obj_t *b=lv_button_create(root);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-4,4);lv_obj_add_event_cb(b,ui_command,LV_EVENT_CLICKED,(void*)CMD_MENU);lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,"Menu");
    b=lv_button_create(canvas);lv_obj_align(b,LV_ALIGN_BOTTOM_RIGHT,-4,-4);lv_obj_add_event_cb(b,touch_button,LV_EVENT_ALL,(void*)AI_JUMP);l=lv_label_create(b);lv_label_set_text(l,"Jump");
    b=lv_button_create(canvas);lv_obj_align(b,LV_ALIGN_BOTTOM_LEFT,4,-4);lv_obj_add_event_cb(b,touch_button,LV_EVENT_ALL,(void*)AI_RUN);l=lv_label_create(b);lv_label_set_text(l,"Run");
    hint=lv_button_create(canvas);lv_obj_align(hint,LV_ALIGN_BOTTOM_MID,0,-44);lv_obj_add_event_cb(hint,ui_command,LV_EVENT_CLICKED,(void*)CMD_INTERACT);l=lv_label_create(hint);lv_label_set_text(l,"Interact");
    move_feedback=lv_label_create(canvas);lv_obj_align(move_feedback,LV_ALIGN_LEFT_MID,4,0);lv_label_set_text(move_feedback,"MOVE");lv_obj_set_style_text_color(move_feedback,lv_color_white(),0);
    look_feedback=lv_label_create(canvas);lv_obj_align(look_feedback,LV_ALIGN_RIGHT_MID,-4,0);lv_label_set_text(look_feedback,"LOOK");lv_obj_set_style_text_color(look_feedback,lv_color_white(),0);
    textarea=lv_textarea_create(root);lv_obj_set_size(textarea,LV_PCT(100),40);lv_obj_align(textarea,LV_ALIGN_BOTTOM_MID,0,-85);lv_textarea_set_one_line(textarea,true);lv_textarea_set_max_length(textarea,127);lv_textarea_set_placeholder_text(textarea,"Type, Enter; Esc or Menu to leave");
    lv_group_t *group=lv_group_get_default();if(group)lv_group_add_obj(group,textarea);
    lv_obj_add_event_cb(textarea,submit,LV_EVENT_ALL,NULL);
    if(atomic_load(&mode)==CONVERSATION){lv_obj_remove_flag(textarea,LV_OBJ_FLAG_HIDDEN);lv_group_focus_obj(textarea);}else lv_obj_add_flag(textarea,LV_OBJ_FLAG_HIDDEN);
    menu_build();lifecycle_grant_locked();wake();
}
static void destroy(void *context) {
    (void)context;lifecycle_revoke_locked();
    keyboards_latched(0);root_widget=canvas=overlay=textarea=hint=move_feedback=look_feedback=NULL;focus_count=0;touch_zone=-1;wake();
}
static void change_mode(int m) {
    lvgl_lock();snprintf(ui_notice,sizeof(ui_notice),"%s",g->notice);
    if(atomic_load(&mode)==CONVERSATION&&m!=CONVERSATION){memset(&conversation_target,0,sizeof(conversation_target));memset(&conversation,0,sizeof(conversation));}
    atomic_store(&mode,m);clear_input();navigation_held=0;touch_zone=-1;atomic_store(&menu_steps,0);
    if(textarea){lv_textarea_set_text(textarea,"");if(m==CONVERSATION){lv_obj_remove_flag(textarea,LV_OBJ_FLAG_HIDDEN);lv_group_focus_obj(textarea);}else lv_obj_add_flag(textarea,LV_OBJ_FLAG_HIDDEN);}
    keyboards_latched(m!=CONVERSATION&&atomic_load(&lifecycle.granted));menu_build();lvgl_unlock();wake();
}
static void interact(void) {
    CtInteraction target;
    if(!ct_interaction_resolve(g,&target)){snprintf(g->notice,sizeof(g->notice),"Nothing within reach.");return;}
    if(target.kind==CT_INTERACT_NPC) {
        if(!ct_interaction_dialogue_supported(&target)){snprintf(g->notice,sizeof(g->notice),"No conversation available.");return;}
        conversation_target=target;memset(&conversation,0,sizeof(conversation));memset(&reply,0,sizeof(reply));reply_offset=0;
        snprintf(reply.text,sizeof(reply.text),"%s",target.label);change_mode(CONVERSATION);
    } else if(!game_apply(g,target.operation))snprintf(g->notice,sizeof(g->notice),"Requirements unmet or out of reach.");
}
static void save_bindings(void) {
    lock_input();binding_snapshot=actions;unlock_input();
    int ok=ai_bindings_save(&binding_snapshot,binding_path);
    snprintf(g->notice,sizeof(g->notice),ok?"Controls saved.":"Controls could not be saved.");
}
static void process_command(int cmd) {
    int m=atomic_load(&mode);
    if(cmd>=CMD_BIND&&cmd<CMD_BIND+AI_ACTION_COUNT*2) {
        unsigned a=(unsigned)(cmd-CMD_BIND)/2;int sign=(cmd-CMD_BIND)%2?-1:1;
        change_mode(CAPTURE);lock_input();ai_capture_begin(&actions,(AiAction)a,sign,micros());unlock_input();return;
    }
    switch(cmd) {
        case CMD_MENU:change_mode(m==MENU?PLAY:MENU);break;
        case CMD_RESUME:memset(&conversation_target,0,sizeof(conversation_target));change_mode(PLAY);break;
        case CMD_CONTROLS:change_mode(CONTROLS);break;
        case CMD_QUIT:ct_lifecycle_close(&lifecycle);wake();break;
        case CMD_SAVE:{int ok=save_game(g,save_path);snprintf(g->notice,sizeof(g->notice),ok?"Saved.":"Save failed.");emit("{\"type\":\"save\",\"ok\":%d}\n",ok);lvgl_lock();snprintf(ui_notice,sizeof(ui_notice),"%s",g->notice);menu_build();lvgl_unlock();break;}
        case CMD_DEFAULTS:lock_input();ai_defaults(&actions,micros());unlock_input();save_bindings();change_mode(CONTROLS);break;
        case CMD_CANCEL:lock_input();ai_capture_cancel(&actions,micros());unlock_input();change_mode(CONTROLS);break;
        case CMD_REPLACE:{lock_input();AiBindResult b=ai_bind(&actions,conflict_binding,1,micros());unlock_input();if(b==AI_BIND_OK)save_bindings();else snprintf(g->notice,sizeof(g->notice),"Binding could not be added (%d).",b);change_mode(CONTROLS);break;}
        case CMD_INTERACT:if(m==PLAY)interact();else if(m==CONVERSATION){reply_offset+=160;if(reply_offset>=(int)strlen(reply.text))reply_offset=0;}break;
        case CMD_SUBMIT: {
            CtInteraction current;
            if(m!=CONVERSATION||!ct_interaction_dialogue_supported(&conversation_target)||
               !ct_interaction_refresh(g,conversation_target.entity_id,&current)){change_mode(PLAY);snprintf(g->notice,sizeof(g->notice),"Conversation is no longer within reach.");break;}
            char text[128];lvgl_lock();snprintf(text,sizeof(text),"%s",submitted);lvgl_unlock();
            conversation_target=current;reply_offset=0;dialogue(g,&conversation,text,0,&reply);break;
        }
        default:break;
    }
}
int main(int argc,char **argv) {
    ct_lifecycle_init(&lifecycle);
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--qualify"))qualify=1;
    char dir[256],asset[256];
    if(app_paths_get_user_data_directory("ag1357.cascadeterrace",dir,sizeof(dir))==ERROR_NONE)mkdir(dir,0755);
    app_paths_get_user_data_path("ag1357.cascadeterrace","cascade.save",save_path,sizeof(save_path));
    app_paths_get_user_data_path("ag1357.cascadeterrace","controls.cfg",binding_path,sizeof(binding_path));
    app_paths_get_user_data_path("ag1357.cascadeterrace","controls-i2c.cfg",i2c_path,sizeof(i2c_path));
    if(app_paths_get_assets_path("ag1357.cascadeterrace","qualification.flag",asset,sizeof(asset))==ERROR_NONE){FILE *f=fopen(asset,"rb");if(f){qualify=1;fclose(f);}}
    if(qualify&&app_paths_get_user_data_path("ag1357.cascadeterrace","qualification.jsonl",telemetry_path,sizeof(telemetry_path))==ERROR_NONE)telemetry=fopen(telemetry_path,"ab");
    if(app_paths_get_assets_path("ag1357.cascadeterrace","kyra.mesh",asset,sizeof(asset))==ERROR_NONE)render_load_assets(asset);
    struct MemoryPolicy policy={MEMORY_CAPABILITY_EXTERNAL,0,16};
    g=memory_calloc_with_policy(1,sizeof(*g),&policy);r=memory_calloc_with_policy(1,sizeof(*r),&policy);input_mutex=xSemaphoreCreateMutex();
    if(!g||!r||!input_mutex){memory_free(g);memory_free(r);if(input_mutex)vSemaphoreDelete(input_mutex);if(telemetry)fclose(telemetry);return 2;}
    game_new(g,42,-1);int reloaded=load_game(g,save_path);ai_init(&actions);int bindings_loaded=ai_bindings_load(&actions,binding_path,micros());
    emit("{\"type\":\"boot\",\"title\":\"Anaphorum\",\"reload\":%d,\"bindings_loaded\":%d,\"input_period_target_us\":10000,\"physical\":\"PENDING\"}\n",reloaded,bindings_loaded);
    task_event_group_construct(&events);task_event_group_construct(&sampler_events);task_event_group_construct(&i2c_events);task_event_group_claim_bit(&events,&window_bit);task_event_group_claim_bit(&sampler_events,&sampler_bit);task_event_group_claim_bit(&i2c_events,&i2c_bit);
    struct AppEventSubscription sub={0};int subscribed=app_event_subscribe(&sub,&events)==ERROR_NONE;
    WindowId window=0;int result=0;
    if(!subscribed){result=2;goto cleanup;}
    window=window_manager_create_ext(app_scheduler_current_app_id(),create,destroy,NULL);
    if(!window){result=2;goto cleanup;}
    sampler=thread_alloc_full("anaphorum-input",6144,input_task,NULL,tskNO_AFFINITY);
    if(!sampler||thread_start(sampler)!=ERROR_NONE){result=2;goto cleanup;}
    int peripheral_count=ct_i2c_input_open(&peripherals,i2c_path,i2c_emit,i2c_disconnect,NULL);
    if(peripheral_count>0) {
        i2c_worker=thread_alloc_full("anaphorum-i2c",4096,i2c_task,NULL,tskNO_AFFINITY);
        if(!i2c_worker||thread_start(i2c_worker)!=ERROR_NONE) {
            if(i2c_worker){thread_free(i2c_worker);i2c_worker=NULL;}
            ct_i2c_input_close(&peripherals,micros());
            snprintf(g->notice,sizeof(g->notice),"Optional I2C input could not start.");
        }
    }
    emit("{\"type\":\"i2c_config\",\"configured\":%d,\"worker\":%d}\n",peripheral_count,i2c_worker!=NULL);
    unsigned jump_pending=0;
    float yaw_remainder=0;
    uint64_t previous=micros(),last_report=previous;
    unsigned seen_epoch=atomic_load(&lifecycle.epoch);
    while(!atomic_load(&lifecycle.closing)) {
        struct AppEvent event;while(app_event_poll(&sub,&event)==ERROR_NONE)if(event.type==APP_EVENT_CLOSE)ct_lifecycle_close(&lifecycle);
        if(atomic_load(&lifecycle.closing))break;
        if(!atomic_load(&lifecycle.granted)){task_event_group_wait_any(&events,NULL,portMAX_DELAY);previous=micros();continue;}
        unsigned epoch=atomic_load(&lifecycle.epoch);if(epoch!=seen_epoch){previous=micros();seen_epoch=epoch;jump_pending=0;yaw_remainder=0;}
        AiFrame frame;lock_input();ai_consume(&actions,micros(),&frame);unsigned captured=actions.capture_state;
        if(captured==AI_CAPTURE_READY&&atomic_load(&mode)==CAPTURE){conflict_binding=actions.candidate;AiBindResult b=ai_capture_accept(&actions,0,micros());if(b!=AI_BIND_OK)ai_capture_cancel(&actions,micros());last_ui_capture=b==AI_BIND_OK?1:b==AI_BIND_CONFLICT?2:3;}
        unlock_input();
        if(last_ui_capture){unsigned ready=last_ui_capture;last_ui_capture=0;if(ready==1)save_bindings();if(ready==3)snprintf(g->notice,sizeof(g->notice),"Binding rejected or table full.");change_mode(ready==2?CONFLICT:CONTROLS);}
        int cmd=atomic_exchange(&command,CMD_NONE),m=atomic_load(&mode);
        if(!cmd&&(frame.pressed[AI_MENU]||frame.pressed[AI_BACK]))cmd=(m==PLAY?CMD_MENU:m==CAPTURE||m==CONFLICT?CMD_CANCEL:m==CONTROLS?CMD_MENU:CMD_RESUME);
        if(!cmd&&m!=PLAY&&m!=CONVERSATION&&m!=CAPTURE) {
            int steps=frame.axis_pressed[AI_MOVE_Y][0]-frame.axis_pressed[AI_MOVE_Y][1]+atomic_exchange(&menu_steps,0);
            lvgl_lock();if(steps)set_focus(focus_index+steps);
            if(frame.pressed[AI_INTERACT]||frame.pressed[AI_JUMP]){if(focus_count)lv_obj_send_event(focus_buttons[focus_index],LV_EVENT_CLICKED,NULL);}
            lvgl_unlock();
        }
        if(!cmd&&m==PLAY&&frame.pressed[AI_INTERACT])cmd=CMD_INTERACT;
        if(cmd)process_command(cmd);
        m=atomic_load(&mode);if(atomic_load(&lifecycle.closing))break;
        uint64_t current=micros();uint32_t elapsed=(uint32_t)((current-previous)/1000);previous=current;
        if(m==PLAY&&!cmd) {
            if(frame.pressed[AI_VIEW_TOGGLE]&1)first_person=!first_person;
            jump_pending+=frame.pressed[AI_JUMP];if(jump_pending>AI_EDGE_LIMIT)jump_pending=AI_EDGE_LIMIT;
            /* Turn has a legacy integer-degree field. Keep fractional analog look
             * locally, applying camera delta once while physics retains 20ms steps. */
            yaw_remainder+=(float)frame.average_axes[AI_LOOK_X]*(float)(elapsed>250?250:elapsed)/10000.f;
            int turn=(int)yaw_remainder;yaw_remainder-=turn;
            g->state.yaw=(g->state.yaw+turn+720)%360;
            Input in={(int16_t)frame.average_axes[AI_MOVE_Y],(int16_t)frame.average_axes[AI_MOVE_X],0,jump_pending>0,(frame.held&(1u<<AI_RUN))!=0,0};
            r->look_pitch+=(float)frame.average_axes[AI_LOOK_Y]*(float)elapsed/1000000.f;
            int steps=g->substep+(elapsed>250?250:elapsed)>=20;
            game_tick(g,in,elapsed);if(steps&&jump_pending)jump_pending--;
        } else {
            jump_pending=0;yaw_remainder=0;
        }
        if((m==PLAY||m==CONVERSATION)&&atomic_load(&lifecycle.granted)) {
            uint64_t start=micros();r->conversation=m==CONVERSATION;r->first_person=first_person;render(r,g);
            draw_panel(r,0,0,W,12,0x1108);draw_text(r,2,2,"ANAPHORUM",0xffff,W-4);
            if(m==CONVERSATION){draw_panel(r,0,H/2,W,70,0x1108);draw_text(r,2,H/2+1,reply.text+reply_offset,0xffff,W-4);}
            else if(g->notice[0]){draw_panel(r,0,H-32,W,30,0x1108);draw_text(r,2,H-31,g->notice,0xffff,W-4);}
            CtInteraction target;int target_found=ct_interaction_resolve(g,&target);
            lvgl_lock();if(canvas&&epoch==atomic_load(&lifecycle.epoch)) {
                ct_viewport_scale_stride(canvas_pixels,r->pixels,&viewport,viewport_stride);lv_obj_invalidate(canvas);
                char label[80];snprintf(label,sizeof(label),"MOVE\n%d %d",frame.axes[0],frame.axes[1]);lv_label_set_text(move_feedback,label);snprintf(label,sizeof(label),"LOOK\n%d %d",frame.axes[2],frame.axes[3]);lv_label_set_text(look_feedback,label);
                if(target_found||m==CONVERSATION){lv_obj_remove_flag(hint,LV_OBJ_FLAG_HIDDEN);lv_label_set_text(lv_obj_get_child(hint,0),m==CONVERSATION?"More reply":target.label);}else lv_obj_add_flag(hint,LV_OBJ_FLAG_HIDDEN);
            }lvgl_unlock();
            if(qualify&&r->frame%30==0)emit("{\"type\":\"frame\",\"frame\":%lu,\"render_present_us\":%llu,\"viewport_w\":%d,\"viewport_h\":%d}\n",(unsigned long)r->frame,(unsigned long long)(micros()-start),viewport.w,viewport.h);
        }
        if(qualify&&current-last_report>=2000000) {
            lock_input();uint32_t count=input_samples,min=input_min_us,max=input_max_us,ev=input_events,dis=input_disconnects,drop=actions.dropped_events;uint64_t sum=input_total_us;AiControl source=last_source;int value=last_source_value;unlock_input();
            emit("{\"type\":\"input\",\"samples\":%lu,\"mean_us\":%llu,\"min_us\":%lu,\"max_us\":%lu,\"events\":%lu,\"disconnects\":%lu,\"dropped\":%lu}\n",(unsigned long)count,(unsigned long long)(count?sum/count:0),(unsigned long)min,(unsigned long)max,(unsigned long)ev,(unsigned long)dis,(unsigned long)drop);emit("{\"type\":\"source_action\",\"backend\":%u,\"device\":%lu,\"control\":%u,\"value\":%d,\"held\":%lu,\"axes\":[%d,%d,%d,%d],\"jump_edges\":%u,\"interact_edges\":%u}\n",source.backend,(unsigned long)source.device,source.control,value,(unsigned long)frame.held,frame.axes[0],frame.axes[1],frame.axes[2],frame.axes[3],frame.pressed[AI_JUMP],frame.pressed[AI_INTERACT]);last_report=current;
        }
        task_event_group_wait_any(&events,NULL,pdMS_TO_TICKS(m==PLAY||m==CONVERSATION?1:10));
    }
cleanup:
    ct_lifecycle_close(&lifecycle);wake();
    if(i2c_worker){thread_join(i2c_worker,portMAX_DELAY,1);thread_free(i2c_worker);i2c_worker=NULL;}
    if(sampler){thread_join(sampler,portMAX_DELAY,1);thread_free(sampler);sampler=NULL;}
    emit("{\"type\":\"input_joined\",\"frame\":%lu}\n",(unsigned long)r->frame);
    int saved=save_game(g,save_path);emit("{\"type\":\"close_save\",\"ok\":%d}\n",saved);
    if(window)window_manager_remove(window);
    if(subscribed)app_event_unsubscribe(&sub);
    task_event_group_release_bit(&events,window_bit);task_event_group_release_bit(&sampler_events,sampler_bit);task_event_group_release_bit(&i2c_events,i2c_bit);
    task_event_group_destruct(&events);task_event_group_destruct(&sampler_events);task_event_group_destruct(&i2c_events);
    memory_free(canvas_pixels);memory_free(r);memory_free(g);vSemaphoreDelete(input_mutex);
    emit("{\"type\":\"close_done\",\"input_task_joined\":true,\"buffers_released\":true,\"internal_free\":%u,\"psram_free\":%u}\n",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if(telemetry)fclose(telemetry);return result;
}
