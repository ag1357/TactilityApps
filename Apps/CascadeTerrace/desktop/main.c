#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "render.h"
#include "action_input.h"
#include "viewport.h"
#include "interaction.h"
#include <assert.h>
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static Game game;
static Renderer frame;
static Conversation convo;
static Reply reply;
static SDL_Window* window;
static SDL_Renderer* display;
static SDL_Texture* texture;
static char entry[128], save_path[256] = "cascade.save", capture_dir[256];
static int capture_no, dialogue_open, headless, comp = 0, reply_offset;
static AiInput actions;
static CtViewport viewport;
static CtInteraction conversation_target;
static char bindings_path[300], menu_notice[96];
static int menu, selected_action, menu_row, binding_page, running = 1, focused = 1, qualify;
static int selftest;
static const char *selftest_dir;
static int touch_x[2], touch_y[2], touch_active[2];
static SDL_FingerID touch_owner[2], button_owner;
static int touch_button_active, jump_pending;
static int64_t yaw_fraction;
static SDL_GameController *controllers[8];
static uint32_t controller_identity[8];
static uint64_t now_us(void) { return SDL_GetTicks64() * 1000; }
static void layout(void) {
    int width, height; SDL_GetWindowSize(window, &width, &height);
    viewport = ct_viewport_fit(0, 0, width, height, W, H);
}
static void clear_input(void) {
    ai_clear(&actions, now_us()); jump_pending=0; yaw_fraction=0;
    touch_button_active=0; ai_disconnect(&actions,4,now_us());
    for (int i=0;i<2;i++) { touch_active[i]=0; ai_disconnect(&actions, 2u+i, now_us()); }
}
static void set_menu(int state) { menu=state; clear_input(); }
static void bindings_save(void) {
    snprintf(menu_notice,sizeof(menu_notice),"%s", ai_bindings_save(&actions,bindings_path) ? "Controls saved" : "Controls save failed");
}
static void gamepad_defaults(void) {
    const unsigned axis_actions[]={AI_MOVE_X,AI_MOVE_Y,AI_LOOK_X,AI_LOOK_Y};
    for (unsigned i=0;i<4;i++) for(int sign=-1;sign<=1;sign+=2) {
        AiBinding b={{AI_BACKEND_GAMEPAD,(uint16_t)i,0,AI_ANALOG},(int8_t)sign,(uint8_t)axis_actions[i],(int8_t)(i==1?-sign:sign)};
        ai_bind(&actions,b,0,now_us());
    }
    const struct { unsigned control, action; } buttons[]={
        {SDL_CONTROLLER_BUTTON_A,AI_JUMP},{SDL_CONTROLLER_BUTTON_B,AI_BACK},
        {SDL_CONTROLLER_BUTTON_X,AI_INTERACT},{SDL_CONTROLLER_BUTTON_Y,AI_VIEW_TOGGLE},
        {SDL_CONTROLLER_BUTTON_BACK,AI_BACK},{SDL_CONTROLLER_BUTTON_START,AI_MENU},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,AI_RUN},{SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,AI_RUN}};
    for (unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);i++) {
        AiBinding b={{AI_BACKEND_GAMEPAD,(uint16_t)buttons[i].control,0,AI_DIGITAL},1,(uint8_t)buttons[i].action,1};
        ai_bind(&actions,b,0,now_us());
    }
}
static void draw_menu(void) {
    char text[180];
    draw_panel(&frame,0,0,W,H,0x1108);
    draw_text(&frame,5,6,"ANAPHORUM",0xffff,0);
    if(menu==1) {
        const char *rows[]={"Resume","Save","Controls","Quit"};
        for(int i=0;i<4;i++) {
            draw_panel(&frame,5,40+i*30,W-10,24,menu_row==i?0x3292:0x218e);
            draw_text(&frame,10,47+i*30,rows[i],0xffff,0);
        }
        draw_text(&frame,5,174,"Arrows / D-pad: choose",0xbfff,W-10);
        draw_text(&frame,5,198,"Enter / A: select",0xbfff,W-10);
        draw_text(&frame,5,222,menu_notice,0xffb4,W-10);
    } else if(menu==2) {
        draw_text(&frame,5,19,"CONTROLS: select action",0x5ff6,W-10);
        for(int i=0;i<AI_ACTION_COUNT;i++) {
            if(i==selected_action) draw_panel(&frame,3,32+i*10,W-6,10,0x3292);
            draw_text(&frame,5,32+i*10,ai_action_name((AiAction)i),0xffff,0);
        }
        unsigned count=0, shown=0;
        for(unsigned i=0;i<actions.binding_count;i++) if(actions.bindings[i].action==selected_action) {
            if(count++<(unsigned)binding_page) continue;
            if(shown==3) break;
            ai_format_binding(&actions.bindings[i],text,sizeof(text));
            draw_text(&frame,4,139+shown*18,text,0xbfff,W-8); shown++;
        }
        draw_text(&frame,5,192,"Bindings: PgDn / tap",0xffb4,W-10);
        draw_panel(&frame,3,202,W-6,12,0x3292);
        draw_text(&frame,5,203,selected_action<AI_JUMP?"Bind -       Bind +":"        Bind new",0xffff,0);
        draw_text(&frame,5,218,"Defaults [R]",0xffff,0);
        draw_text(&frame,100,229,"Back",0xffff,0);
    } else {
        draw_text(&frame,5,36,ai_action_name((AiAction)selected_action),0x5ff6,W-10);
        draw_text(&frame,5,59,menu==4?"Binding conflicts":"Press a control or move an axis",0xffff,W-10);
        if(menu==4) {
            ai_format_binding(&actions.candidate,text,sizeof(text));
            draw_text(&frame,5,93,text,0xffb4,W-10);
            draw_text(&frame,5,150,"Replace conflicting binding?",0xffff,W-10);
            draw_panel(&frame,5,184,W-10,18,0x3292);
            draw_text(&frame,9,188,"Replace [Enter/A]",0xffff,0);
        }
        draw_text(&frame,5,218,"Cancel: Esc/Start/tap",0xffff,0);
    }
}
static int64_t dist2(Pos a, Pos b) {
    int64_t x = a.x - b.x, z = a.z - b.z;
    return x * x + z * z;
}
static void present(void) {
    frame.conversation = dialogue_open;
    render(&frame, &game);
    char hud[160];
    int minute = (int)(game.state.time / 60000);
    snprintf(hud, sizeof(hud), "ANAPHORUM D%d %02d:%02d", minute / 1440 + 1, minute / 60 % 24, minute % 60);
    draw_panel(&frame, 0, 0, W, 11, 0x192b);
    draw_text(&frame, 3, 1, hud, 0xffff, 0);
    snprintf(hud, sizeof(hud), "%d CHITS  PHOS %d/100  %s", game.state.player.quantity[IT_CHIT], game.state.player.quantity[19] / 1000, game.state.repaired ? "INTACT" : "DAMAGED");
    draw_panel(&frame, 0, 11, W, 10, 0x218e);
    draw_text(&frame, 3, 11, hud, 0xbfff, 0);
    if (dialogue_open) {
        draw_panel(&frame, 0, H / 2 - 40, W, H / 2 + 25, 0x1108);
        draw_panel(&frame,W-31,23,30,13,0x3292);draw_text(&frame,W-29,26,"BACK",0xffff,0);
        draw_text(&frame, 3, H / 2 - 39, "KYRA | ESC TO LEAVE", 0x5ff6, 0);
        char visible_reply[257];snprintf(visible_reply,sizeof(visible_reply),"%.256s",reply.text+reply_offset);
        draw_text(&frame, 3, H / 2 - 28, visible_reply, 0xffff, W-4);
        draw_panel(&frame, 0, H - 12, W, 12, 0x298f);
        draw_text(&frame, 3, H - 10, entry, 0xffb4, W-4);
    } else {
        draw_panel(&frame, 0, H - 23, W, 23, 0x1108);
        draw_text(&frame, 3, H - 22, game.notice, 0xffff, W-4);
    }
    if (!dialogue_open && !menu) {
        CtInteraction target;
        draw_panel(&frame,W-31,23,30,13,0x3292); draw_text(&frame,W-29,26,"MENU",0xffff,0);
        if(ct_interaction_resolve(&game,&target)) draw_text(&frame,3,38,target.label,0xffb4,W-6);
        draw_panel(&frame,5,H-69,43,35,touch_active[0]?0x3292:0x218e);
        draw_text(&frame,9,H-64,"MOVE",0xffff,0);
        draw_panel(&frame,W-48,H-69,43,35,touch_active[1]?0x3292:0x218e);
        draw_text(&frame,W-43,H-64,"LOOK",0xffff,0);
        for(int i=0;i<2;i++) if(touch_active[i]) draw_panel(&frame,touch_x[i]-2,touch_y[i]-2,5,5,0xffb4);
        draw_text(&frame,56,H-69,"JUMP",0xffff,0);
        draw_text(&frame,56,H-55,"ACT",0xffff,0);
        draw_text(&frame,56,H-41,"RUN",0xffff,0);
    }
    if(menu) draw_menu();
    layout();
    SDL_UpdateTexture(texture, NULL, frame.pixels, W * 2);
    SDL_RenderClear(display);
    SDL_Rect destination={viewport.x,viewport.y,viewport.w,viewport.h};
    SDL_RenderCopy(display, texture, NULL, &destination);
    SDL_RenderPresent(display);
    if (*capture_dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%05d.ppm", capture_dir, capture_no++);
        screenshot(&frame, path);
    }
}
static int apply(OpCode op, ItemId item, int amount, uint32_t target) {
    int ok = game_apply(&game, (Operation) {op, PLAYER_ID, target, item, amount, NULL});
    if (!ok) snprintf(game.notice, sizeof(game.notice), "Action unavailable here or requirements unmet.");
    return ok;
}
static int talk(const char* s) {
    CtInteraction nearby;
    int found=dialogue_open?ct_interaction_refresh(&game,conversation_target.entity_id,&nearby):ct_interaction_resolve(&game,&nearby);
    if (!found || !ct_interaction_dialogue_supported(&nearby)) {
        snprintf(game.notice, sizeof(game.notice), "Find Kyra; stand within two metres.");
        return 0;
    }
    dialogue_open = 1;
    conversation_target=nearby; clear_input();
    Pos n=nearby.position;
    game.state.yaw = (int)(atan2(n.x - game.state.player_pos.x, n.z - game.state.player_pos.z) * 180 / 3.141592653589793 + 360) % 360;
    reply_offset = 0;
    uint64_t a = SDL_GetPerformanceCounter();
    dialogue(&game, &convo, s, comp, &reply);
    double ms = (SDL_GetPerformanceCounter() - a) * 1000. / SDL_GetPerformanceFrequency();
    printf("DIALOGUE %.4f ms | %s\n%s\n", ms, s, reply.text);
    return 1;
}
/* Autopilot only produces heading/forward input through the gameplay tick. */
static int segment(int x, int z) {
    dialogue_open = 0;
    Pos target = {x, 0, z};
    int stalled = 0;
    int64_t last = dist2(game.state.player_pos, target);
    for (int i = 0; i < 12000; i++) {
        int64_t d = dist2(game.state.player_pos, target);
        if (d < 350LL * 350) {
            present();
            return 1;
        }
        game.state.yaw = (int)(atan2(x - game.state.player_pos.x, z - game.state.player_pos.z) * 180 / 3.141592653589793 + 360) % 360;
        game_tick(&game, (Input) {.forward = 1000, .run = 1}, 20);
        if (i % 20 == 0) {
            present();
            if (llabs(last - d) < 100) stalled++;
            else
                stalled = 0;
            last = d;
            if (stalled > 10) break;
        }
    }
    fprintf(stderr, "NAVIGATION FAILED at %d,%d -> %d,%d\n", game.state.player_pos.x, game.state.player_pos.z, x, z);
    return 0;
}
static int navigate(int x, int z) {
    int sx = (game.state.player_pos.x + 202500) / 5000, sz = (game.state.player_pos.z + 202500) / 5000;
    int tx = (x + 202500) / 5000, tz = (z + 202500) / 5000;
    if (sx < 1 || sz < 1 || tx < 1 || tz < 1 || tx >= MAP_N - 1 || tz >= MAP_N - 1) return 0;
    int16_t parent[MAP_N * MAP_N];
    uint16_t queue[MAP_N * MAP_N], route[MAP_N * MAP_N];
    for (int i = 0; i < MAP_N * MAP_N; i++) parent[i] = -1;
    int start = sz * MAP_N + sx, goal = tz * MAP_N + tx, head = 0, tail = 0;
    parent[start] = (int16_t)start;
    queue[tail++] = (uint16_t)start;
    while (head < tail && parent[goal] < 0) {
        int u = queue[head++], ux = u % MAP_N, uz = u / MAP_N;
        int dx[] = {1, -1, 0, 0}, dz[] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = ux + dx[k], nz = uz + dz[k];
            if (nx < 1 || nz < 1 || nx >= MAP_N - 1 || nz >= MAP_N - 1) continue;
            int v = nz * MAP_N + nx;
            Pos p = {nx * 5000 - 200000, 0, nz * 5000 - 200000};
            if (parent[v] < 0 && abs(game.world.height[v] - game.world.height[u]) * 10 <= 4950 && walk_edge(&game, (Pos) {ux * 5000 - 200000, 0, uz * 5000 - 200000}, p)) {
                parent[v] = (int16_t)u;
                queue[tail++] = (uint16_t)v;
            }
        }
    }
    if (parent[goal] < 0) {
        fprintf(stderr, "NO ROUTE to %d,%d\n", x, z);
        return 0;
    }
    int n = 0;
    for (int v = goal; v != start; v = parent[v]) route[n++] = (uint16_t)v;
    if (!segment(sx * 5000 - 200000, sz * 5000 - 200000)) return 0;
    for (int i = n - 1; i >= 0; i--) {
        int v = route[i];
        if (!segment((v % MAP_N) * 5000 - 200000, (v / MAP_N) * 5000 - 200000)) return 0;
    }
    return segment(x, z);
}
static ItemId item(const char* s) {
    for (int i = 0; i < IT_COUNT; i++)
        if (strstr(item_name(i), s)) return (ItemId)i;
    return IT_COUNT;
}
static int command(char* line) {
    line[strcspn(line, "\r\n")] = 0;
    if (!*line || *line == '#') return 1;
    char op[32] = {0}, arg[200] = {0};
    sscanf(line, "%31s %199[^\n]", op, arg);
    int ok = 1;
    if (!strcmp(op, "say")) ok = talk(arg);
    else if (!strcmp(op, "walk")) {
        int x, z;
        if (sscanf(arg, "%d %d", &x, &z) != 2) return 0;
        ok = navigate(x, z);
    } else if (!strcmp(op, "site")) {
        int i = atoi(arg);
        if (i < 0 || i >= STRUCT_COUNT) return 0;
        Pos p = game.world.sites[i].center;
        if (i != MARKET) p.z += game.world.sites[i].halfz + 2000;
        ok = navigate(p.x, p.z);
    } else if (!strcmp(op, "kyra")) {
        Pos p = npc_position(&game);
        p.z += 1500;
        ok = navigate(p.x, p.z);
    } else if (!strcmp(op, "look"))
        game.state.yaw = (atoi(arg) % 360 + 360) % 360;
    else if (!strcmp(op, "wait"))
        ok = apply(WAIT, IT_CHIT, atoi(arg), 0);
    else if (!strcmp(op, "morning")) {
        int m = (int)(game.state.time / 60000 % 1440);
        ok = apply(WAIT, IT_CHIT, (480 - m + 1440) % 1440, 0);
    } else if (!strcmp(op, "meditate"))
        ok = apply(EXTRACT, IT_CHIT, atoi(arg), 0);
    else if (!strcmp(op, "condense"))
        ok = apply(CONDENSE, IT_CHIT, atoi(arg), 0);
    else if (!strcmp(op, "buy"))
        ok = apply(BUY, item(arg), 1, 3);
    else if (!strcmp(op, "pick"))
        ok = apply(PICK_UP, item(arg), 1, 0);
    else if (!strcmp(op, "show"))
        ok = apply(SHOW, item(arg), 1, KYRA_ID);
    else if (!strcmp(op, "repair"))
        ok = apply(REPAIR, IT_COUPLING, 1, 0);
    else if (!strcmp(op, "save"))
        ok = save_game(&game, save_path);
    else if (!strcmp(op, "load")) {
        memset(&convo, 0, sizeof(convo));
        ok = load_game(&game, save_path);
    } else if (!strcmp(op, "snapshot")) {
        present();
        ok = screenshot(&frame, arg);
    } else if (!strcmp(op, "leave")) {
        dialogue_open = 0; memset(&conversation_target,0,sizeof(conversation_target));
    } else if (!strcmp(op, "new")) {
        game_new(&game, (uint32_t)strtoul(arg, NULL, 10), -1);
        memset(&convo, 0, sizeof(convo));
    } else if (!strcmp(op, "quit"))
        return 2;
    else
        return 0;
    printf("COMMAND %s: %s\n", line, ok ? "OK" : "FAIL");
    present();
    return ok;
}
static void menu_activate(void) {
    if(menu_row==0) set_menu(0);
    else if(menu_row==1) snprintf(menu_notice,sizeof(menu_notice),"%s",save_game(&game,save_path)?"Game saved":"Save failed");
    else if(menu_row==2) set_menu(2);
    else running=0;
}
static void capture_begin(int sign) {
    ai_capture_begin(&actions,(AiAction)selected_action,sign,now_us()); menu=3;
}
static void capture_finish(int replace) {
    AiBindResult result=ai_capture_accept(&actions,replace,now_us());
    if(result==AI_BIND_CONFLICT) menu=4;
    else if(result==AI_BIND_OK) {menu=2;binding_page=0;bindings_save();}
    else {snprintf(menu_notice,sizeof(menu_notice),"Binding rejected (%d)",result);ai_capture_cancel(&actions,now_us());menu=2;}
}
static void ui_key(SDL_Keycode key) {
    if(key==SDLK_ESCAPE) {
        if(menu>=3) {ai_capture_cancel(&actions,now_us());menu=2;}
        else set_menu(menu==2?1:0);
    } else if(menu==1) {
        if(key==SDLK_UP||key==SDLK_w) menu_row=(menu_row+3)%4;
        else if(key==SDLK_DOWN||key==SDLK_s) menu_row=(menu_row+1)%4;
        else if(key==SDLK_RETURN||key==SDLK_SPACE) menu_activate();
    } else if(menu==2) {
        if(key==SDLK_UP||key==SDLK_DOWN) {selected_action=(selected_action+(key==SDLK_UP?AI_ACTION_COUNT-1:1))%AI_ACTION_COUNT;binding_page=0;}
        else if(key==SDLK_RETURN||key==SDLK_EQUALS||key==SDLK_RIGHT) capture_begin(1);
        else if((key==SDLK_MINUS||key==SDLK_LEFT)&&selected_action<AI_JUMP) capture_begin(-1);
        else if(key==SDLK_PAGEDOWN) {unsigned count=0;for(unsigned i=0;i<actions.binding_count;i++)count+=actions.bindings[i].action==selected_action;binding_page=count?(binding_page+3)%(int)count:0;}
        else if(key==SDLK_r) {ai_defaults(&actions,now_us());gamepad_defaults();bindings_save();}
    } else if(menu==4&&(key==SDLK_RETURN||key==SDLK_SPACE)) capture_finish(1);
}
static void ui_point(int x,int y) {
    if(menu==1&&y>=40&&y<160) {menu_row=(y-40)/30;menu_activate();}
    else if(menu==2) {
        if(y>=32&&y<132) {selected_action=(y-32)/10;binding_page=0;}
        else if(y>=139&&y<202) ui_key(SDLK_PAGEDOWN);
        else if(y>=202&&y<215) capture_begin(selected_action<AI_JUMP&&x<W/2?-1:1);
        else if(y>=215&&y<228) ui_key(SDLK_r);
        else if(y>=228) ui_key(SDLK_ESCAPE);
    } else if(menu==4&&y>=184&&y<204) capture_finish(1);
    else if(menu>=3&&y>=210) ui_key(SDLK_ESCAPE);
}
static unsigned key_control(SDL_Keycode key) {
    if(key==SDLK_LSHIFT||key==SDLK_RSHIFT)return AI_KEY_SHIFT;
    if(key==SDLK_UP)return AI_KEY_UP;if(key==SDLK_DOWN)return AI_KEY_DOWN;
    if(key==SDLK_LEFT)return AI_KEY_LEFT;if(key==SDLK_RIGHT)return AI_KEY_RIGHT;
    if(key>=0&&key<256)return (unsigned)key;
    return 0x200u+(unsigned)SDL_GetScancodeFromKey(key);
}
static uint64_t event_time(uint32_t timestamp) {
    uint64_t now=SDL_GetTicks64(); uint32_t age=(uint32_t)now-timestamp;
    return (age<0x80000000u&&age<=now?now-age:now)*1000;
}
static void controller_add(int device) {
    if(!SDL_IsGameController(device))return;
    for(unsigned i=0;i<8;i++) if(!controllers[i]) {
        controllers[i]=SDL_GameControllerOpen(device);if(!controllers[i])return;
        SDL_JoystickGUID guid=SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(controllers[i]));
        uint32_t hash=2166136261u;
        for(unsigned j=0;j<sizeof(guid.data);j++)hash=(hash^guid.data[j])*16777619u;
        /* Serial identifies a unit across ports; path distinguishes equal GUIDs
           when no serial exists (such devices may need rebinding after a move). */
        const char *unit=SDL_GameControllerGetSerial(controllers[i]);
        if(!unit||!*unit)unit=SDL_GameControllerPath(controllers[i]);
        if(unit)for(;*unit;unit++)hash=(hash^(unsigned char)*unit)*16777619u;
        controller_identity[i]=hash?hash:1;return;
    }
}
static void touch_event(SDL_FingerID id,int x,int y,int down,int release,uint64_t stamp) {
    int rx=0,ry=0,inside=ct_viewport_inverse(&viewport,x,y,&rx,&ry);
    if(release) {
        for(int i=0;i<2;i++)if(touch_active[i]&&touch_owner[i]==id){touch_active[i]=0;ai_disconnect(&actions,2u+i,stamp);}
        if(touch_button_active&&button_owner==id){touch_button_active=0;ai_disconnect(&actions,4,stamp);}return;
    }
    if(!inside) {
        for(int i=0;i<2;i++)if(touch_active[i]&&touch_owner[i]==id){touch_active[i]=0;ai_disconnect(&actions,2u+i,stamp);}
        if(touch_button_active&&button_owner==id){touch_button_active=0;ai_disconnect(&actions,4,stamp);}return;
    }
    if(menu){if(down)ui_point(rx,ry);return;}
    if(dialogue_open) {if(down&&ry<40){dialogue_open=0;memset(&conversation_target,0,sizeof(conversation_target));clear_input();}return;}
    if(down&&rx>=W-34&&ry>=21&&ry<=40){set_menu(1);return;}
    if(down&&rx>=50&&rx<105&&ry>=H-72&&ry<H-27) {
        if(touch_button_active&&button_owner!=id)return;
        button_owner=id;touch_button_active=1;
        unsigned action=ry<H-58?AI_JUMP:ry<H-44?AI_INTERACT:AI_RUN;
        ai_device_event(&actions,4,(AiControl){AI_BACKEND_TOUCH,(uint16_t)action,1,AI_DIGITAL},1,stamp);return;
    }
    if(ry<70)return;
    int side=rx<W/2?0:1;
    for(int i=0;i<2;i++)if(touch_active[i]&&touch_owner[i]==id)side=i;
    if(touch_active[side]&&touch_owner[side]!=id)return;
    touch_owner[side]=id;touch_active[side]=1;touch_x[side]=rx;touch_y[side]=ry;
    int center=side?W-26:26;
    int vx=(rx-center)*40,vy=(H-50-ry)*40;
    ai_device_event(&actions,2u+side,(AiControl){AI_BACKEND_TOUCH,side?AI_LOOK_X:AI_MOVE_X,1,AI_ANALOG},vx,stamp);
    ai_device_event(&actions,2u+side,(AiControl){AI_BACKEND_TOUCH,side?AI_LOOK_Y:AI_MOVE_Y,1,AI_ANALOG},side?-vy:vy,stamp);
}
static void process_event(const SDL_Event *event) {
    SDL_Event e=*event;uint64_t stamp=event_time(e.common.timestamp);
    if(e.type==SDL_QUIT){running=0;return;}
    if(e.type==SDL_WINDOWEVENT) {
        if(e.window.event==SDL_WINDOWEVENT_FOCUS_LOST||e.window.event==SDL_WINDOWEVENT_MINIMIZED){focused=0;clear_input();}
        else if(e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED||e.window.event==SDL_WINDOWEVENT_RESTORED){focused=1;clear_input();}
        else if(e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED)layout();return;
    }
    if(e.type==SDL_CONTROLLERDEVICEADDED){controller_add(e.cdevice.which);return;}
    if(e.type==SDL_CONTROLLERDEVICEREMOVED) {
        for(unsigned i=0;i<8;i++)if(controllers[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i]))==e.cdevice.which){
            ai_disconnect(&actions,100u+(uint32_t)e.cdevice.which,stamp);SDL_GameControllerClose(controllers[i]);controllers[i]=NULL;
        }return;
    }
    /* Releases still reconcile while unfocused; never synthesize a timeout release. */
    if(!focused&&e.type!=SDL_KEYUP&&e.type!=SDL_CONTROLLERBUTTONUP&&e.type!=SDL_FINGERUP&&
       e.type!=SDL_MOUSEBUTTONUP&&!(e.type==SDL_CONTROLLERAXISMOTION&&abs(e.caxis.value)<=AI_RELEASE_ZONE*32767/1000))return;
    if(e.type==SDL_MOUSEBUTTONDOWN||e.type==SDL_MOUSEBUTTONUP||e.type==SDL_MOUSEMOTION){
        if(e.type!=SDL_MOUSEMOTION||e.motion.state&SDL_BUTTON_LMASK)touch_event(-1,e.type==SDL_MOUSEMOTION?e.motion.x:e.button.x,e.type==SDL_MOUSEMOTION?e.motion.y:e.button.y,e.type==SDL_MOUSEBUTTONDOWN,e.type==SDL_MOUSEBUTTONUP,stamp);return;
    }
    if(e.type==SDL_FINGERDOWN||e.type==SDL_FINGERMOTION||e.type==SDL_FINGERUP){int width,height;SDL_GetWindowSize(window,&width,&height);touch_event(e.tfinger.fingerId,(int)(e.tfinger.x*width),(int)(e.tfinger.y*height),e.type==SDL_FINGERDOWN,e.type==SDL_FINGERUP,stamp);return;}
    if(e.type==SDL_TEXTINPUT&&dialogue_open&&strlen(entry)+strlen(e.text.text)<sizeof(entry)){strcat(entry,e.text.text);return;}
    if(e.type==SDL_KEYDOWN||e.type==SDL_KEYUP) {
        if(e.key.repeat)return;
        unsigned key=key_control(e.key.keysym.sym);int down=e.type==SDL_KEYDOWN;
        if(down&&menu&&menu!=3){ui_key(e.key.keysym.sym);return;}
        if(down&&menu==3&&e.key.keysym.sym==SDLK_ESCAPE){ui_key(SDLK_ESCAPE);return;}
        if(down&&dialogue_open){
            SDL_Keycode k=e.key.keysym.sym;
            if(k==SDLK_ESCAPE){dialogue_open=0;memset(&conversation_target,0,sizeof(conversation_target));clear_input();}
            else if(k==SDLK_RETURN){talk(entry);entry[0]=0;}
            else if(k==SDLK_BACKSPACE&&*entry)entry[strlen(entry)-1]=0;
            else if(k==SDLK_PAGEDOWN){reply_offset+=192;if(reply_offset>=(int)strlen(reply.text))reply_offset=0;}
            return;
        }
        /* Two Shift keys have distinct instances, preserving a held right Shift on left release. */
        uint32_t instance=e.key.keysym.sym==SDLK_RSHIFT?6:1;
        ai_device_event(&actions,instance,(AiControl){AI_BACKEND_KEYBOARD,(uint16_t)key,0,AI_DIGITAL},down,stamp);
        if(down&&qualify&&!menu&&!dialogue_open){
            SDL_Keycode k=e.key.keysym.sym;
            if(k==SDLK_F5)save_game(&game,save_path);else if(k==SDLK_F9)load_game(&game,save_path);
            else if(k==SDLK_m)apply(EXTRACT,IT_CHIT,20,0);else if(k==SDLK_c)apply(CONDENSE,IT_CHIT,1,0);
            else if(k==SDLK_b)apply(BUY,IT_COUPLING,1,3);else if(k==SDLK_n)apply(WAIT,IT_CHIT,60,0);
        }
    } else if(e.type==SDL_CONTROLLERAXISMOTION||e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP) {
        int axis=e.type==SDL_CONTROLLERAXISMOTION;
        SDL_JoystickID instance=axis?e.caxis.which:e.cbutton.which;uint32_t identity=0;
        for(unsigned i=0;i<8;i++)if(controllers[i]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i]))==instance)identity=controller_identity[i];
        if(!identity)return;
        int down=e.type==SDL_CONTROLLERBUTTONDOWN;
        if(down&&menu==3&&e.cbutton.button==SDL_CONTROLLER_BUTTON_START){ui_key(SDLK_ESCAPE);return;}
        if(down&&menu&&menu!=3){
            SDL_Keycode key=0;
            switch(e.cbutton.button){case SDL_CONTROLLER_BUTTON_DPAD_UP:key=SDLK_UP;break;case SDL_CONTROLLER_BUTTON_DPAD_DOWN:key=SDLK_DOWN;break;case SDL_CONTROLLER_BUTTON_DPAD_LEFT:key=SDLK_LEFT;break;case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:key=SDLK_RIGHT;break;case SDL_CONTROLLER_BUTTON_A:key=SDLK_RETURN;break;case SDL_CONTROLLER_BUTTON_B:key=SDLK_ESCAPE;break;case SDL_CONTROLLER_BUTTON_X:key=SDLK_PAGEDOWN;break;case SDL_CONTROLLER_BUTTON_Y:key=SDLK_r;break;}
            ui_key(key);return;
        }
        if(down&&dialogue_open&&e.cbutton.button==SDL_CONTROLLER_BUTTON_B){dialogue_open=0;memset(&conversation_target,0,sizeof(conversation_target));clear_input();return;}
        ai_device_event(&actions,100u+(uint32_t)instance,(AiControl){AI_BACKEND_GAMEPAD,axis?e.caxis.axis:e.cbutton.button,identity,axis?AI_ANALOG:AI_DIGITAL},axis?(int)e.caxis.value*1000/32767:down,stamp);
    }
    if(menu==3&&actions.capture_state==AI_CAPTURE_READY)capture_finish(0);
}
static void consume_actions(uint32_t dt) {
    AiFrame input;ai_consume(&actions,now_us(),&input);
    if(!focused||dialogue_open)return;
    if(menu) {
        if(menu==1||menu==2) {
            for(unsigned n=0;n<input.axis_pressed[AI_MOVE_Y][0];n++)ui_key(SDLK_DOWN);
            for(unsigned n=0;n<input.axis_pressed[AI_MOVE_Y][1];n++)ui_key(SDLK_UP);
            if(input.axis_pressed[AI_MOVE_X][0])ui_key(SDLK_LEFT);
            if(input.axis_pressed[AI_MOVE_X][1])ui_key(SDLK_RIGHT);
        }
        return;
    }
    if(input.pressed[AI_MENU]||input.pressed[AI_BACK]){set_menu(1);return;}
    if(input.pressed[AI_VIEW_TOGGLE])frame.first_person=!frame.first_person;
    if(input.pressed[AI_INTERACT]) {
        CtInteraction target;
        if(ct_interaction_resolve(&game,&target)) {
            if(ct_interaction_dialogue_supported(&target)){conversation_target=target;talk("Hello");return;}
            if(target.kind!=CT_INTERACT_NPC)game_apply(&game,target.operation);
            else snprintf(game.notice,sizeof(game.notice),"%s has no conversation available.",target.label);
        } else snprintf(game.notice,sizeof(game.notice),"No nearby interaction.");
    }
    frame.look_pitch+=input.average_axes[AI_LOOK_Y]*(float)dt/1000000.f;
    if(frame.look_pitch>.9f)frame.look_pitch=.9f;if(frame.look_pitch<-.9f)frame.look_pitch=-.9f;
    /* Keep sub-degree analog camera motion and jump edges across a frame that
       is shorter than the core's fixed 20ms physics step. */
    if(dt>250)dt=250;
    yaw_fraction+=(int64_t)input.average_axes[AI_LOOK_X]*dt;
    int yaw_step=(int)(yaw_fraction/10000);yaw_fraction%=10000;
    game.state.yaw=(game.state.yaw+yaw_step+360)%360;
    jump_pending|=input.pressed[AI_JUMP]!=0;
    int advanced=game.substep+dt>=20;
    game_tick(&game,(Input){.forward=input.average_axes[AI_MOVE_Y],.strafe=input.average_axes[AI_MOVE_X],
        .jump=jump_pending,.run=(input.held&(1u<<AI_RUN))!=0},dt);
    if(advanced)jump_pending=0;
}
static void screenshot_window(const char *name) {
    int width,height;SDL_GetRendererOutputSize(display,&width,&height);
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
    assert(surface);assert(!SDL_RenderReadPixels(display,NULL,surface->format->format,surface->pixels,surface->pitch));
    char path[512];snprintf(path,sizeof(path),"%s/%s.bmp",selftest_dir,name);assert(!SDL_SaveBMP(surface,path));SDL_FreeSurface(surface);
}
static int platform_selftest(void) {
    mkdir(selftest_dir,0755);
    const int sizes[][2]={{320,440},{640,320},{480,480},{960,640}};
    for(unsigned i=0;i<4;i++) {SDL_SetWindowSize(window,sizes[i][0],sizes[i][1]);present();int x,y;assert(ct_viewport_inverse(&viewport,viewport.x,viewport.y,&x,&y)&&x==0&&y==0);assert(!ct_viewport_inverse(&viewport,viewport.x-1,viewport.y,&x,&y));char name[50];snprintf(name,sizeof(name),"layout-%dx%d",sizes[i][0],sizes[i][1]);screenshot_window(name);}
    set_menu(1);present();screenshot_window("menu");menu_row=2;menu_activate();assert(menu==2);present();screenshot_window("controls");
    selected_action=AI_JUMP;capture_begin(1);
    SDL_Event event={0};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_w;event.common.timestamp=SDL_GetTicks();process_event(&event);assert(menu==4);present();screenshot_window("binding-conflict");
    ui_key(SDLK_ESCAPE);assert(menu==2);assert(actions.capture_state==AI_CAPTURE_IDLE);
    event.type=SDL_KEYUP;process_event(&event);capture_begin(1);event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_z;process_event(&event);assert(menu==2);
    set_menu(0);event.type=SDL_KEYUP;process_event(&event);AiFrame in;ai_consume(&actions,now_us(),&in);
    SDL_Event press=event;press.type=SDL_KEYDOWN;press.common.timestamp=SDL_GetTicks();
    SDL_Event release=press;release.type=SDL_KEYUP;release.common.timestamp+=10;
    SDL_Delay(150);process_event(&press);process_event(&release);ai_consume(&actions,now_us(),&in);assert(in.pressed[AI_JUMP]==1&&in.released[AI_JUMP]==1&&!(in.held&(1u<<AI_JUMP)));
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_w;process_event(&event);event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;process_event(&event);assert(!focused);ai_consume(&actions,now_us(),&in);assert(!in.axes[AI_MOVE_Y]);
    event.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;process_event(&event);assert(focused);
    /* A held key cannot revive at regrant; a real release then repress can. */
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_w;process_event(&event);
    ai_consume(&actions,now_us(),&in);assert(!in.axes[AI_MOVE_Y]);
    event.type=SDL_KEYUP;process_event(&event);event.type=SDL_KEYDOWN;process_event(&event);
    ai_consume(&actions,now_us(),&in);assert(in.axes[AI_MOVE_Y]>0);
    event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;process_event(&event);
    event.type=SDL_KEYUP;event.key.keysym.sym=SDLK_w;process_event(&event);
    event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;process_event(&event);
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_w;process_event(&event);
    ai_consume(&actions,now_us(),&in);assert(in.axes[AI_MOVE_Y]>0);clear_input();
    /* Outside-viewport drag clears its own finger, preserving a second stick. */
    int tx=viewport.x+36*viewport.w/W,ty=viewport.y+(H-50)*viewport.h/H;
    touch_event(11,tx,ty,1,0,now_us());touch_event(22,viewport.x+(W-16)*viewport.w/W,ty,1,0,now_us());
    ai_consume(&actions,now_us(),&in);assert(in.axes[AI_MOVE_X]>0&&in.axes[AI_LOOK_X]>0);
    touch_event(11,viewport.x-1,ty,0,0,now_us());ai_consume(&actions,now_us(),&in);
    assert(!in.axes[AI_MOVE_X]&&in.axes[AI_LOOK_X]>0);clear_input();
    set_menu(1);menu_row=0;
    ai_device_event(&actions,20,(AiControl){AI_BACKEND_TOUCH,AI_MOVE_Y,1,AI_ANALOG},-1000,now_us());
    ai_device_event(&actions,20,(AiControl){AI_BACKEND_TOUCH,AI_MOVE_Y,1,AI_ANALOG},0,now_us());
    consume_actions(0);assert(menu_row==1); /* fast down-and-release remains one menu step */
    /* Capacity and malformed candidates surface as rejection, never success. */
    AiInput saved=actions;
    for(unsigned i=0;i<AI_MAX_BINDINGS;i++)actions.bindings[i]=(AiBinding){{AI_BACKEND_KEYBOARD,(uint16_t)(1000+i),0,AI_DIGITAL},1,AI_JUMP,1};
    actions.binding_count=AI_MAX_BINDINGS;selected_action=AI_JUMP;capture_begin(1);
    actions.candidate=(AiBinding){{AI_BACKEND_KEYBOARD,'z',0,AI_DIGITAL},1,AI_JUMP,1};actions.capture_state=AI_CAPTURE_READY;
    capture_finish(0);assert(menu==2&&strstr(menu_notice,"rejected"));
    capture_begin(1);actions.candidate.source.kind=0;actions.capture_state=AI_CAPTURE_READY;
    capture_finish(0);assert(menu==2&&strstr(menu_notice,"rejected"));actions=saved;bindings_save();
    set_menu(1);menu_row=3;menu_activate();assert(!running);
    printf("VI-P2 desktop: layouts, menu, capture/conflict/capacity, short tap through 150ms stall, queued menu tap, focus reconciliation, per-finger outside release, Quit passed.\n");return 0;
}
int main(int argc, char** argv) {
    uint32_t seed = 42;
    int variant = -1, load = 0;
    const char* script = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--qualify")) qualify=1;
        else if (!strcmp(argv[i], "--platform-selftest")&&i+1<argc) {selftest=1;headless=1;selftest_dir=argv[++i];}
        else if (!strcmp(argv[i], "--load"))
            load = 1;
        else if (!strcmp(argv[i], "--baseline"))
            comp = 0;
        else if (!strcmp(argv[i], "--patched-cognition"))
            comp = 2;
        else if (!strcmp(argv[i], "--general-cognition"))
            comp = 3;
        else if (!strcmp(argv[i], "--learned-cognition"))
            comp = 4;
        else if (!strcmp(argv[i], "--experimental-composition"))
            comp = 1;
        else if (i + 1 < argc) {
            if (!strcmp(argv[i], "--seed")) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
            else if (!strcmp(argv[i], "--variant"))
                variant = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--script"))
                script = argv[++i];
            else if (!strcmp(argv[i], "--save"))
                snprintf(save_path, sizeof(save_path), "%s", argv[++i]);
            else if (!strcmp(argv[i], "--capture"))
                snprintf(capture_dir, sizeof(capture_dir), "%s", argv[++i]);
            else {
                fprintf(stderr, "Unknown argument\n");
                return 2;
            }
        }
    }
    if (headless) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER)) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("Anaphorum", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 640, headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    display = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture = SDL_CreateTexture(display, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);
    if (!window || !display || !texture) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawColor(display,0,0,0,255);
    layout();ai_init(&actions);gamepad_defaults();
    snprintf(bindings_path,sizeof(bindings_path),"%s.bindings",save_path);
    if(selftest)snprintf(bindings_path,sizeof(bindings_path),"%s/controls-test.bindings",selftest_dir);
    ai_bindings_load(&actions,bindings_path,now_us());
    for(int device=0;device<SDL_NumJoysticks();device++)controller_add(device);
    render_load_assets("assets/kyra.mesh");
    game_new(&game, seed, variant);
    if (load && !load_game(&game, save_path)) {
        fprintf(stderr, "Cannot load save\n");
        return 1;
    }
    if (*capture_dir) mkdir(capture_dir, 0755);
    present();
    if (script) {
        FILE* f = fopen(script, "r");
        if (!f) {
            perror(script);
            return 1;
        }
        char line[300];
        int failed = 0;
        while (fgets(line, sizeof(line), f)) {
            int result = command(line);
            if (result == 2) break;
            if (!result) {
                failed = 1;
                break;
            }
        }
        fclose(f);
        SDL_Quit();
        return failed;
    }
    if(selftest){int result=platform_selftest();SDL_Quit();return result;}
    if (headless) {
        SDL_Quit();
        return 0;
    }
    SDL_StartTextInput();
    uint32_t previous = SDL_GetTicks();
    while(running) {
        SDL_Event event;
        if(!focused) {
            if(SDL_WaitEventTimeout(&event,250))process_event(&event);
            previous=SDL_GetTicks();continue;
        }
        while(SDL_PollEvent(&event))process_event(&event);
        uint32_t current=SDL_GetTicks(),dt=current-previous;previous=current;
        consume_actions(dt);
        if(focused&&running)present();
        SDL_Delay(10);
    }
    clear_input();
    for(unsigned i=0;i<8;i++)if(controllers[i])SDL_GameControllerClose(controllers[i]);
    save_game(&game, save_path);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(display);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
