#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static Game g;
static Renderer r;
static NpcView v;
static unsigned char wire[32768];
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static int cmp(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}
int main(void) {
    double frames[200], saves[20], cold[100];
    for (int i = 0; i < 100; i++) {
        double t = now();
        game_new(&g, (unsigned)i, -1);
        render(&r, &g);
        cold[i] = now() - t;
    }
    game_new(&g, 42, -1);
    double sum = 0;
    for (int i = 0; i < 200; i++) {
        g.state.yaw = i * 7 % 360;
        double t = now();
        render(&r, &g);
        frames[i] = now() - t;
        sum += frames[i];
    }
    qsort(frames, 200, sizeof(double), cmp);
    qsort(cold, 100, sizeof(double), cmp);
    double t = now();
    for (int i = 0; i < 10000; i++) npc_view(&g, &v);
    double query = (now() - t) / 10000;
    for (int i = 0; i < 20; i++) {
        t = now();
        if (!save_game(&g, "/tmp/cascade-benchmark.save")) return 1;
        saves[i] = now() - t;
    }
    qsort(saves, 20, sizeof(double), cmp);
    printf("{\"platform\":\"desktop_x86_64\",\"render_mean_ms\":%.6f,\"render_p95_ms\":%.6f,\"cold_generation_plus_render_p95_ms\":%.6f,\"world_view_mean_ms\":%.6f,\"save_p95_ms\":%.6f,\"save_bytes\":%zu,\"game_bytes\":%zu,\"render_bytes\":%zu,\"npc_view_workspace_bytes\":%zu,\"npc_record_bytes\":%zu,\"streaming\":\"not implemented\",\"P4_estimate\":null}\n", sum / 200, frames[189], cold[94], query, saves[18], state_encode(&g.state, wire, sizeof(wire)) + 16, sizeof(g), sizeof(r), sizeof(v), sizeof(NpcMemory));
    return 0;
}
