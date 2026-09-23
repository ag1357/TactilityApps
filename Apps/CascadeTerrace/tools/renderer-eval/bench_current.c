/* Path-1 benchmark: the current core/render.c pipeline (instrumented copy),
 * driven on representative scenes from the real products, with per-stage
 * timings, traffic counters, an identical-frame PPM, and a world-space
 * triangle dump the Jet benchmark replays on exactly the same data. */
#include "game.h"
#include "render.h"
#include "bench_timers.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int bench_group;
extern FILE *bench_dump;

static Renderer R;
static Game G;
static WsRecipe recipe;

/* macro.json module order (stable committed product) */
enum { M_MOUNTAIN = 0, M_SHOULDER_E, M_SHOULDER_W, M_WATERSHED, M_CAVE,
       M_KARST, M_DAM, M_PLAINS, M_FOREST, M_WETLAND, M_RUIN, M_GATE,
       M_E0, M_E1, M_E2, M_E3, M_E4, M_FORD_E, M_FORD_W, M_W1, M_HAVEN, M_PHOS };

typedef struct {
    const char *name;
    double stage_ms[TR_N];
    double total_ms;
    unsigned tris, px_written;
    unsigned long long px_tested, px_covered, depth_writes;
    unsigned dump_groups;
} SceneResult;

static WsPos module_center(int idx) {
    WsModule m;
    ws_materialize(&recipe, (uint16_t)idx, &m);
    return m.pos;
}

static double yaw_toward(WsPos from, WsPos to) {
    double dx = (double)to.x - from.x, dz = (double)to.z - from.z;
    double deg = atan2(dx, dz) * (180.0 / 3.14159265358979323846);
    return deg < 0 ? deg + 360.0 : deg;
}

static void run_scene(const char *name, void (*draw)(void), int frames,
                      const char *dump_path, SceneResult *out) {
    memset(out, 0, sizeof(*out));
    out->name = name;
    /* frame 0 with the dump enabled: identical geometry for the Jet replay */
    char path[256];
    snprintf(path, sizeof(path), "%s.dump.txt", dump_path);
    bench_dump = fopen(path, "w");
    draw();
    fclose(bench_dump);
    bench_dump = NULL;
    memset(&bench_st, 0, sizeof(bench_st));
    for (int i = 0; i < 50; i++) draw(); /* warm caches; untimed */
    memset(&bench_st, 0, sizeof(bench_st));
    uint64_t t0 = bench_now();
    for (int i = 0; i < frames; i++) draw();
    uint64_t total = bench_now() - t0;
    double freq = (double)bench_freq();
    for (int s = 0; s < TR_N; s++) out->stage_ms[s] = (double)bench_st.ticks[s] / freq / frames * 1000.0;
    out->total_ms = (double)total / freq / frames * 1000.0;
    out->tris = R.triangles;
    out->px_written = R.pixels_written;
    out->px_tested = bench_st.px_tested / (unsigned long long)frames;
    out->px_covered = bench_st.px_covered / (unsigned long long)frames;
    out->depth_writes = bench_st.depth_writes / (unsigned long long)frames;
    /* count dump groups */
    FILE *f = fopen(path, "r");
    if (f) {
        char line[512];
        int seen[16] = {0};
        while (fgets(line, sizeof(line), f))
            if (line[0] == 'T') {
                int g = atoi(strrchr(line, ' ') + 1);
                if (g >= 0 && g < 16 && !seen[g]++) out->dump_groups++;
            }
        fclose(f);
    }
}

static void draw_cascade(void) { render(&R, &G); }

static WsAddress vista_addr, cave_addr, close_addr;
static int vista_yaw, close_yaw;
static WsMetrics metrics;

static void draw_vista(void) { render_world(&R, &recipe, vista_addr, vista_yaw, &metrics); }
static void draw_cave(void) { render_world(&R, &recipe, cave_addr, 0, &metrics); }
static void draw_close(void) { render_world(&R, &recipe, close_addr, close_yaw, &metrics); }

int main(void) {
    if (system("mkdir -p /tmp/renderer-eval/out")) return 1;
    memset(&R, 0, sizeof(R));
    memset(&G, 0, sizeof(G));
    int frames = 500;
    SceneResult results[4];
    int nres = 0;

    /* Scene 1: the actual legacy game frame (cascade district, noon). */
    game_new(&G, 42, -1);
    G.state.time = 43200000; /* noon: light == 1, comparable to render_world */
    R.conversation = 0;
    run_scene("cascade-district", draw_cascade, frames,
              "/tmp/renderer-eval/out/cascade", &results[nres++]);
    screenshot(&R, "/tmp/renderer-eval/out/current_cascade.ppm");

    /* Scenes 2-4: the committed macro product through render_world. */
    FILE *f = fopen("/media/cloud/2982-E16B/tactility_p4_work/repos/TactilityApps/Apps/CascadeTerrace/build/macro.cws", "rb");
    if (!f) { fprintf(stderr, "macro.cws missing\n"); return 1; }
    static unsigned char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (ws_load(&recipe, buf, (unsigned)n) != 0) { fprintf(stderr, "ws_load failed\n"); return 1; }

    WsPos ws = module_center(M_WATERSHED), haven = module_center(M_HAVEN),
          cave = module_center(M_CAVE), e3 = module_center(M_E3), karst = module_center(M_KARST);
    vista_addr.pos = ws;
    vista_addr.pos.y += 1700;
    vista_addr.scope = 0xFFFF;
    vista_yaw = (int)yaw_toward(ws, haven);
    run_scene("macro-vista", draw_vista, frames, "/tmp/renderer-eval/out/vista", &results[nres++]);
    screenshot(&R, "/tmp/renderer-eval/out/current_vista.ppm");

    cave_addr.pos = cave;
    cave_addr.pos.y += 1700;
    cave_addr.scope = M_CAVE;
    run_scene("macro-cave-interior", draw_cave, frames, "/tmp/renderer-eval/out/cave", &results[nres++]);
    screenshot(&R, "/tmp/renderer-eval/out/current_cave.ppm");

    close_addr.pos = e3;
    close_addr.pos.y += 1700;
    close_addr.scope = 0xFFFF;
    close_yaw = (int)yaw_toward(e3, karst);
    run_scene("macro-ruin-closeup", draw_close, frames, "/tmp/renderer-eval/out/close", &results[nres++]);
    screenshot(&R, "/tmp/renderer-eval/out/current_close.ppm");

    /* Presentation micro-benchmarks (the P4 frontend's exact upscale loop,
     * a u32-pair variant, a Jet-style half-width expand loop, and a plain
     * 300 KB memcpy as the DMA/PPA floor). */
    static uint16_t canvas[480 * 320];
    double pres[4];
    const char *pres_names[4] = {"pixel_loop_240x160_to_480x320", "u32pair_rowdup_480x320",
                                  "halfwidth_expand_120x160_to_240x160", "memcpy_300KB"};
    for (int k = 0; k < 4; k++) {
        int iters = 2000;
        /* warm */
        for (int i = 0; i < 50; i++) {
            if (k == 0) {
                for (int y = 0; y < 320; y++)
                    for (int x = 0; x < 480; x++) canvas[y * 480 + x] = R.pixels[(y / 2) * 240 + x / 2];
            } else if (k == 1) {
                for (int y = 0; y < 160; y++) {
                    uint32_t *d0 = (uint32_t *)&canvas[y * 2 * 480], *d1 = (uint32_t *)&canvas[(y * 2 + 1) * 480];
                    for (int x = 0; x < 240; x++) {
                        uint16_t p = R.pixels[y * 240 + x];
                        uint32_t v = (uint32_t)p | ((uint32_t)p << 16);
                        d0[x] = d1[x] = v;
                    }
                }
            } else if (k == 2) {
                for (int y = 0; y < 160; y++) {
                    uint16_t *dst = &canvas[y * 240];
                    const uint16_t *src = &R.pixels[y * 240];
                    for (int x = 0; x < 120; x++) {
                        uint16_t p = src[x];
                        dst[2 * x] = dst[2 * x + 1] = (uint16_t)((p << 8) | (p >> 8));
                    }
                }
            } else {
                memcpy(canvas, R.pixels, 300000);
            }
        }
        uint64_t t0 = bench_now();
        for (int i = 0; i < iters; i++) {
            if (k == 0) {
                for (int y = 0; y < 320; y++)
                    for (int x = 0; x < 480; x++) canvas[y * 480 + x] = R.pixels[(y / 2) * 240 + x / 2];
            } else if (k == 1) {
                for (int y = 0; y < 160; y++) {
                    uint32_t *d0 = (uint32_t *)&canvas[y * 2 * 480], *d1 = (uint32_t *)&canvas[(y * 2 + 1) * 480];
                    for (int x = 0; x < 240; x++) {
                        uint16_t p = R.pixels[y * 240 + x];
                        uint32_t v = (uint32_t)p | ((uint32_t)p << 16);
                        d0[x] = d1[x] = v;
                    }
                }
            } else if (k == 2) {
                for (int y = 0; y < 160; y++) {
                    uint16_t *dst = &canvas[y * 240];
                    const uint16_t *src = &R.pixels[y * 240];
                    for (int x = 0; x < 120; x++) {
                        uint16_t p = src[x];
                        dst[2 * x] = dst[2 * x + 1] = (uint16_t)((p << 8) | (p >> 8));
                    }
                }
            } else {
                memcpy(canvas, R.pixels, 300000);
            }
        }
        pres[k] = (double)(bench_now() - t0) / (double)bench_freq() / iters * 1000.0;
    }

    /* JSON report */
    FILE *out = fopen("/tmp/renderer-eval/out/current.json", "w");
    fprintf(out, "{\n \"host\": \"aarch64 (bench host; not P4)\",\n \"scenes\": [\n");
    const char *stage_names[TR_N] = {"transform", "clip", "setup", "rasterize", "clear"};
    for (int i = 0; i < nres; i++) {
        SceneResult *r = &results[i];
        fprintf(out, "  {\"name\": \"%s\", \"total_ms\": %.4f, \"stages_ms\": {", r->name, r->total_ms);
        for (int s = 0; s < TR_N; s++)
            fprintf(out, "%s\"%s\": %.4f", s ? ", " : "", stage_names[s], r->stage_ms[s]);
        fprintf(out, "}, \"triangles\": %u, \"pixels_written\": %u, \"px_tested\": %llu, \"px_covered\": %llu, \"depth_writes\": %llu, \"groups\": %u}%s\n",
                r->tris, r->px_written, r->px_tested, r->px_covered, r->depth_writes, r->dump_groups,
                i + 1 < nres ? "," : "");
    }
    fprintf(out, " ],\n \"presentation_ms\": {\n");
    for (int k = 0; k < 4; k++)
        fprintf(out, "  \"%s\": %.4f%s\n", pres_names[k], pres[k], k + 1 < 4 ? "," : "");
    fprintf(out, " }\n}\n");
    fclose(out);

    for (int i = 0; i < nres; i++) {
        SceneResult *r = &results[i];
        printf("%-20s total %7.3f ms | xform %6.3f clip %6.3f setup %6.3f raster %7.3f clear %6.3f | tris %5u px %6u depth %6llu tested %8llu\n",
               r->name, r->total_ms, r->stage_ms[TR_TRANSFORM], r->stage_ms[TR_CLIP], r->stage_ms[TR_SETUP],
               r->stage_ms[TR_RASTER], r->stage_ms[TR_CLEAR], r->tris, r->px_written, r->depth_writes, r->px_tested);
    }
    for (int k = 0; k < 4; k++) printf("presentation %-38s %7.4f ms\n", pres_names[k], pres[k]);
    return 0;
}
