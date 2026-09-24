#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "render.h"
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
static int64_t dist2(Pos a, Pos b) {
    int64_t x = a.x - b.x, z = a.z - b.z;
    return x * x + z * z;
}
static void present(void) {
    frame.conversation = dialogue_open;
    render(&frame, &game);
    char hud[160];
    int minute = (int)(game.state.time / 60000);
    snprintf(hud, sizeof(hud), "CASCADE TERRACE | DAY %d %02d:%02d", minute / 1440 + 1, minute / 60 % 24, minute % 60);
    draw_panel(&frame, 0, 0, W, 11, 0x192b);
    draw_text(&frame, 3, 1, hud, 0xffff, 0);
    snprintf(hud, sizeof(hud), "%d CHITS  PHOS %d/100  %s", game.state.player.quantity[IT_CHIT], game.state.player.quantity[19] / 1000, game.state.repaired ? "INTACT" : "DAMAGED");
    draw_panel(&frame, 0, 11, W, 10, 0x218e);
    draw_text(&frame, 3, 11, hud, 0xbfff, 0);
    if (dialogue_open) {
        draw_panel(&frame, 0, H / 2 - 40, W, H / 2 - 52, 0x1108);
        draw_text(&frame, 3, H / 2 - 39, "KYRA | ESC TO LEAVE", 0x5ff6, 0);
        draw_text(&frame, 3, H / 2 - 28, reply.text + reply_offset, 0xffff, 237);
        draw_panel(&frame, 0, H - 12, W, 12, 0x298f);
        draw_text(&frame, 3, H - 10, entry, 0xffb4, 237);
    } else {
        draw_panel(&frame, 0, H - 23, W, 23, 0x1108);
        draw_text(&frame, 3, H - 22, game.notice, 0xffff, 237);
    }
    SDL_UpdateTexture(texture, NULL, frame.pixels, W * 2);
    SDL_RenderClear(display);
    SDL_RenderCopy(display, texture, NULL, NULL);
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
    Pos n = npc_position(&game);
    if (dist2(n, game.state.player_pos) > 2200LL * 2200) {
        snprintf(game.notice, sizeof(game.notice), "Find Kyra; stand within two metres.");
        return 0;
    }
    dialogue_open = 1;
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
        dialogue_open = 0;
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
int main(int argc, char** argv) {
    uint32_t seed = 42;
    int variant = -1, load = 0;
    const char* script = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("Cascade Terrace", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 640, headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    display = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    texture = SDL_CreateTexture(display, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);
    if (!window || !display || !texture) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    /* Aspect-correct letterbox in the resizable window (portrait frame). */
    SDL_RenderSetLogicalSize(display, W, H);
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
    if (headless) {
        SDL_Quit();
        return 0;
    }
    SDL_StartTextInput();
    int running = 1;
    uint32_t previous = SDL_GetTicks();
    int touch_forward = 0, touch_strafe = 0, touch_turn = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_TEXTINPUT && dialogue_open && strlen(entry) + strlen(e.text.text) < sizeof(entry)) strcat(entry, e.text.text);
            if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION) {
                if (e.tfinger.x < .4f) {
                    touch_forward = (int)((.72f - e.tfinger.y) * 4000);
                    touch_strafe = (int)((e.tfinger.x - .2f) * 4000);
                } else
                    touch_turn = (int)(e.tfinger.dx * 500);
            }
            if (e.type == SDL_FINGERUP) touch_forward = touch_strafe = touch_turn = 0;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if (dialogue_open) {
                    if (k == SDLK_ESCAPE) dialogue_open = 0;
                    else if (k == SDLK_PAGEDOWN) {
                        reply_offset += 192;
                        if (reply_offset >= (int)strlen(reply.text)) reply_offset = 0;
                    } else if (k == SDLK_RETURN) {
                        talk(entry);
                        entry[0] = 0;
                    } else if (k == SDLK_BACKSPACE && *entry)
                        entry[strlen(entry) - 1] = 0;
                } else {
                    if (k == SDLK_ESCAPE) running = 0;
                    else if (k == SDLK_t) {
                        dialogue_open = 1;
                        reply.text[0] = 0;
                    } else if (k == SDLK_e) {
                        if (dist2(game.state.player_pos, npc_position(&game)) < 2200LL * 2200) talk("Hello");
                        else
                            apply(REPAIR, IT_COUPLING, 1, 0);
                    } else if (k == SDLK_r)
                        apply(REPAIR, IT_COUPLING, 1, 0);
                    else if (k == SDLK_m)
                        apply(EXTRACT, IT_CHIT, 20, 0);
                    else if (k == SDLK_c)
                        apply(CONDENSE, IT_CHIT, 1, 0);
                    else if (k == SDLK_b)
                        apply(BUY, IT_COUPLING, 1, 3);
                    else if (k == SDLK_F5)
                        save_game(&game, save_path);
                    else if (k == SDLK_F9)
                        load_game(&game, save_path);
                    else if (k == SDLK_p) {
                        if (!apply(PICK_UP, IT_CABLE, 1, 0)) apply(PICK_UP, IT_LOG, 1, 0);
                    } else if (k == SDLK_o) {
                        if (!apply(SHOW, IT_CABLE, 1, KYRA_ID)) apply(SHOW, IT_LOG, 1, KYRA_ID);
                    } else if (k == SDLK_n)
                        apply(WAIT, IT_CHIT, 60, 0);
                }
            }
        }
        const Uint8* keys = SDL_GetKeyboardState(NULL);
        uint32_t current = SDL_GetTicks(), dt = current - previous;
        previous = current;
        if (!dialogue_open) {
            Input in = {(int16_t)((keys[SDL_SCANCODE_W] - keys[SDL_SCANCODE_S]) * 1000 + touch_forward), (int16_t)((keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]) * 1000 + touch_strafe), (int16_t)((keys[SDL_SCANCODE_LEFT] - keys[SDL_SCANCODE_RIGHT]) * 2 + touch_turn), keys[SDL_SCANCODE_SPACE], keys[SDL_SCANCODE_LSHIFT], 0};
            if (in.forward > 1000) in.forward = 1000;
            if (in.forward < -1000) in.forward = -1000;
            if (in.strafe > 1000) in.strafe = 1000;
            if (in.strafe < -1000) in.strafe = -1000;
            game_tick(&game, in, dt);
        }
        present();
        SDL_Delay(16);
    }
    save_game(&game, save_path);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(display);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
