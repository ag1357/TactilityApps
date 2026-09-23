/* Path-2 benchmark: the real CubeCoders/Jet renderer (clone with counter
 * patch only), driven on the exact world-space triangle dumps produced by
 * the instrumented current-renderer benchmark — same scenes, same data,
 * same cameras. Stage timings come from Jet's own public pipeline phases
 * (clearBuffers / prepareFrame / rasterizeBand). Camera axis/sign mapping
 * is calibrated against the current renderer's PPM output. */
#include "Jet.hpp"
#include "bench_timers.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Renderer;

extern unsigned long long jet_px_written, jet_tris_drawn; /* counter patch: global scope in Renderer.cpp */

#ifndef CFG_NAME
#define CFG_NAME "z"
#endif

static const int OUT_W = 240, OUT_H = 160;

struct DumpTri {
    float a[3], b[3], c[3];
    unsigned col;
    float shade;
    int group;
};
struct DumpScene {
    float cam[6]; /* x y z yaw(rad) pitch(rad) light */
    std::vector<DumpTri> tris;
};

static bool parse_dump(const char *path, DumpScene &out) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'C') {
            sscanf(line + 2, "%f %f %f %f %f %f", &out.cam[0], &out.cam[1], &out.cam[2],
                   &out.cam[3], &out.cam[4], &out.cam[5]);
        } else if (line[0] == 'T') {
            DumpTri t;
            if (sscanf(line + 2, "%f %f %f %f %f %f %f %f %f %u %f %d",
                       &t.a[0], &t.a[1], &t.a[2], &t.b[0], &t.b[1], &t.b[2],
                       &t.c[0], &t.c[1], &t.c[2], &t.col, &t.shade, &t.group) == 12)
                out.tris.push_back(t);
        }
    }
    fclose(f);
    return out.tris.size() > 0;
}

/* Exact replication of the current pipeline's per-triangle color bake
 * (transform -> view z -> fog -> RGB565) so both paths draw the same
 * colors without Jet lighting. */
static float g_cy, g_sy, g_cp, g_sp;
static void set_cam_trig(float yaw, float pitch) {
    g_cy = cosf(yaw); g_sy = sinf(yaw); g_cp = cosf(pitch); g_sp = sinf(pitch);
}
static float view_z(const float *v, const float *cam) {
    float x = v[0] - cam[0], y = v[1] - cam[1], z = v[2] - cam[2];
    float f = x * g_sy + z * g_cy;
    return y * g_sp + f * g_cp;
}
static uint16_t bake_color(unsigned col, float z, float shade, float light) {
    float f = z / 210.0f;
    if (f > .88f) f = .88f;
    if (f < 0) f = 0;
    shade *= light;
    int r = (int)(((col >> 16) & 255) * shade * (1 - f) + 92 * f),
        g = (int)(((col >> 8) & 255) * shade * (1 - f) + 121 * f),
        b = (int)((col & 255) * shade * (1 - f) + 154 * f);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

/* Framebuffers: caller-owned, as Jet requires. */
static uint16_t g_fbuf[240 * 160];
static uint16_t g_zbuf[240 * 160];

static Scene *build_scene(const DumpScene &dump, bool gradient_clear) {
    memset(g_fbuf, 0, sizeof(g_fbuf));
    memset(g_zbuf, 0, sizeof(g_zbuf));
    Scene *scene = new Scene(g_fbuf,
#if Z_BUFFERING
                             g_zbuf
#else
                             nullptr
#endif
                             ,
                             OUT_W, OUT_H);
    set_cam_trig(dump.cam[3], dump.cam[4]);
    /* clear matches the current renderer per scene family: the game scene
     * clears to a light-scaled sky gradient, render_world clears flat 0x6393 */
    static uint16_t sky[160];
    if (gradient_clear) {
        for (int y = 0; y < 160; y++) {
            int r = (int)((60 + y * .3f) * dump.cam[5]), g = (int)((88 + y * .3f) * dump.cam[5]),
                b = (int)((131 + y * .25f) * dump.cam[5]);
            sky[y] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        }
        scene->backgroundGradientColors = sky;
    } else {
        scene->backgroundGradientColors = nullptr;
    }
    scene->setBackcolor(0x6393);

    std::map<uint16_t, Material *> mats;
    std::map<int, Object *> objs;
    for (const DumpTri &t : dump.tris) {
        float z = (view_z(t.a, dump.cam) + view_z(t.b, dump.cam) + view_z(t.c, dump.cam)) / 3.0f;
        uint16_t c = bake_color(t.col, z, t.shade, dump.cam[5]);
        Material *m;
        auto it = mats.find(c);
        if (it == mats.end()) {
            m = new Material(c);
            mats[c] = m;
        } else
            m = it->second;
        Object *o;
        auto ot = objs.find(t.group);
        if (ot == objs.end()) {
            o = new Object();
            o->cullingMode = CullingMode::NO_CULLING; /* the current renderer never culls by winding */
            objs[t.group] = o;
            scene->addObject(o);
        } else
            o = ot->second;
        uint16_t base = (uint16_t)o->vertices.size();
        Object::Vertex v;
        v.position = Vector3((int32_t)llroundf(t.a[0] * 1000.0f), (int32_t)llroundf(t.a[1] * 1000.0f), (int32_t)llroundf(t.a[2] * 1000.0f));
        o->vertices.push_back(v);
        v.position = Vector3((int32_t)llroundf(t.b[0] * 1000.0f), (int32_t)llroundf(t.b[1] * 1000.0f), (int32_t)llroundf(t.b[2] * 1000.0f));
        o->vertices.push_back(v);
        v.position = Vector3((int32_t)llroundf(t.c[0] * 1000.0f), (int32_t)llroundf(t.c[1] * 1000.0f), (int32_t)llroundf(t.c[2] * 1000.0f));
        o->vertices.push_back(v);
        Object::Triangle tri;
        tri.v1 = base; tri.v2 = base + 1; tri.v3 = base + 2;
        tri.material = m; /* per-color material: Jet's colorBaked path aliases a
                             shared static material, so all queued baked triangles
                             rasterize with the last-emitted color (clone-only
                             eval finding; avoided here) */
        o->triangles.push_back(tri);
    }
    /* Jet culls per-object via an AABB: recompute from the world-space
     * vertices we just pushed (objects sit at the origin). */
    for (auto &kv : objs) kv.second->calculateBoundingBox();
    return scene;
}

/* Derived exact camera mapping (see Jet Camera::getRotationMatrix +
 * Scene transform vs render.c's transform()):
 *   Jet rot.y = +yaw, rot.x = -pitch (radians), rot.z = 0
 * reproduces the current renderer's camera-space axes term-for-term:
 *   cam_x = x*cy - z*sy ; cam_y = y*cp - f*sp ; cam_z = y*sp + f*cp
 * Only difference: screen-y center 78 (current) vs 80 (Jet) — a 2px bias
 * handled in the parity comparison. */
static Camera *make_camera(const DumpScene &dump) {
    Camera *cam = new Camera();
    cam->position = Vector3((int32_t)llroundf(dump.cam[0] * 1000.0f),
                            (int32_t)llroundf(dump.cam[1] * 1000.0f),
                            (int32_t)llroundf(dump.cam[2] * 1000.0f));
    cam->rotation = Vector3_f(-dump.cam[4], dump.cam[3], 0.0f);
    cam->fovFactor = 145.0f; /* exact match to the current projection */
    cam->nearPlane = 200;    /* 0.2 m, as in the current renderer */
    cam->farPlane = 200000;  /* 200 m: beyond every drawn object */
    return cam;
}

static void write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", OUT_W, OUT_H);
#if HALF_WIDTH_BUFFERS
    const int w = OUT_W / 2;
#else
    const int w = OUT_W;
#endif
    for (int y = 0; y < OUT_H; y++)
        for (int x = 0; x < OUT_W; x++) {
#if HALF_WIDTH_BUFFERS
            uint16_t p = g_fbuf[y * w + x / 2];
#else
            uint16_t p = g_fbuf[y * w + x];
#endif
            uint8_t b3[3] = {(uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
            fwrite(b3, 1, 3, f);
        }
    fclose(f);
}

struct Ppm {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};
static bool load_ppm(const char *path, Ppm &p) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    int n = fscanf(f, "P6 %d %d 255", &p.w, &p.h);
    if (n != 2) { fclose(f); return false; }
    fgetc(f); /* single whitespace after maxval */
    p.rgb.resize((size_t)p.w * p.h * 3);
    fread(p.rgb.data(), 1, p.rgb.size(), f);
    fclose(f);
    return true;
}

/* Parity vs the current renderer's PPM. Jet centers screen-y at H/2 while
 * the current renderer uses H/2-2; try dy in {-2,0,2} and keep the best. */
static double ppm_parity(const char *a_path, const char *b_path) {
    Ppm a, b;
    if (!load_ppm(a_path, a) || !load_ppm(b_path, b) || a.w != b.w || a.h != b.h) return -1;
    double best = 0;
    for (int dy = -2; dy <= 2; dy += 2) {
        size_t same = 0, tot = 0;
        for (int y = 0; y < a.h; y++) {
            int yb = y + dy;
            if (yb < 0 || yb >= a.h) continue;
            for (int x = 0; x < a.w; x++) {
                size_t ia = ((size_t)y * a.w + x) * 3, ib = ((size_t)yb * b.w + x) * 3;
                tot++;
                if (a.rgb[ia] == b.rgb[ib] && a.rgb[ia + 1] == b.rgb[ib + 1] && a.rgb[ia + 2] == b.rgb[ib + 2])
                    same++;
            }
        }
        if (tot && (double)same / (double)tot * 100.0 > best) best = (double)same / (double)tot * 100.0;
    }
    return best;
}

int main(void) {
    const char *scenes[] = {"cascade", "vista", "cave", "close"};
    const char *labels[] = {"cascade-district", "macro-vista", "macro-cave-interior", "macro-ruin-closeup"};
    double freq = (double)bench_freq();
    char path[256], cur[256];

    /* camera mapping verification on the cascade scene (mapping is derived
     * exactly, this just proves the replay pipeline end-to-end) */
    double best_parity = -1;
    {
        DumpScene dump;
        if (!parse_dump("/tmp/renderer-eval/out/cascade.dump.txt", dump)) {
            fprintf(stderr, "dump missing (run bench_current first)\n");
            return 1;
        }
        Scene *scene = build_scene(dump, true);
        Camera *cam = make_camera(dump);
        scene->setCamera(cam);
        scene->render();
        snprintf(path, sizeof(path), "/tmp/renderer-eval/out/calib_%s.ppm", CFG_NAME);
        write_ppm(path);
        best_parity = ppm_parity(path, "/tmp/renderer-eval/out/current_cascade.ppm");
        delete scene;
        delete cam;
        printf("config %s (Z%d SORT%d HALF%d): camera-verify parity %.1f%%\n", CFG_NAME,
#if Z_BUFFERING
               1,
#else
               0,
#endif
#if SORT_TRIANGLES
               1,
#else
               0,
#endif
#if HALF_WIDTH_BUFFERS
               1,
#else
               0,
#endif
               best_parity);
    }

    FILE *json = fopen("/tmp/renderer-eval/out/jet_" CFG_NAME ".json", "w");
    fprintf(json, "{\n \"config\": \"%s\",\n \"camera_verify_parity_pct\": %.2f,\n \"scenes\": [\n",
            CFG_NAME, best_parity);

    for (int sc = 0; sc < 4; sc++) {
        DumpScene dump;
        snprintf(path, sizeof(path), "/tmp/renderer-eval/out/%s.dump.txt", scenes[sc]);
        if (!parse_dump(path, dump)) continue;
        snprintf(cur, sizeof(cur), "/tmp/renderer-eval/out/current_%s.ppm", scenes[sc]);
        Scene *scene = build_scene(dump, sc == 0); /* game scene: gradient clear; macro: flat */
        Camera *cam = make_camera(dump);
        scene->setCamera(cam);
        Rasterizer *ras = scene->getRenderer();
        double clear_ms_buf = 0;

        for (int interlace = 0; interlace < 2; interlace++) {
            ras->interlacedMode = interlace != 0;
            const int frames = 500;
            for (int i = 0; i < 50; i++) { scene->prepareFrame(); scene->rasterizeBand(0, OUT_H); scene->advanceFrameCounter(); }
            jet_px_written = 0; jet_tris_drawn = 0;
            double prep_ms = 0, rast_ms = 0, total_ms = 0;
            for (int i = 0; i < frames; i++) {
                uint64_t t0 = bench_now();
                scene->prepareFrame();
                uint64_t t1 = bench_now();
                scene->rasterizeBand(0, OUT_H);
                uint64_t t2 = bench_now();
                scene->advanceFrameCounter();
                prep_ms += (double)(t1 - t0) / freq * 1000.0;
                rast_ms += (double)(t2 - t1) / freq * 1000.0;
                total_ms += (double)(t2 - t0) / freq * 1000.0;
            }
            prep_ms /= frames; rast_ms /= frames; total_ms /= frames;
            unsigned long long tris = jet_tris_drawn, px = jet_px_written;
            /* clear cost: clearBuffers() is private, so time an empty scene's
             * prepareFrame() on the same buffers/gradient = clear + no-op loop */
            if (!interlace) {
                Scene *empty = build_scene(dump, sc == 0);
                empty->getObjects().clear();
                empty->setCamera(cam); /* prepareFrame() returns early without one */
                uint64_t tc0 = bench_now();
                for (int i = 0; i < frames; i++) empty->prepareFrame();
                clear_ms_buf = (double)(bench_now() - tc0) / freq * 1000.0 / frames;
                delete empty;
            }
            double clear_ms = clear_ms_buf;

            double par = -1;
            if (!interlace) {
                scene->prepareFrame();
                scene->rasterizeBand(0, OUT_H);
                scene->advanceFrameCounter();
                snprintf(path, sizeof(path), "/tmp/renderer-eval/out/jet_%s_%s.ppm", CFG_NAME, scenes[sc]);
                write_ppm(path);
                par = ppm_parity(path, cur);
            }
            printf("%-20s %s total %7.3f ms | clear %6.3f prep %6.3f raster %7.3f | tris %6llu px %8llu | parity %5.1f%%\n",
                   labels[sc], interlace ? "i" : " ", total_ms, clear_ms, prep_ms - clear_ms, rast_ms,
                   tris, px, par);
            fprintf(json, "  {\"name\": \"%s\", \"interlaced\": %s, \"total_ms\": %.4f, \"clear_ms\": %.4f, \"prepare_ms\": %.4f, \"rasterize_ms\": %.4f, \"triangles\": %llu, \"pixels_written\": %llu, \"parity_pct\": %.2f}%s\n",
                    labels[sc], interlace ? "true" : "false", total_ms, clear_ms, prep_ms - clear_ms, rast_ms,
                    tris, px, par,
                    (sc == 3 && interlace) ? "" : ",");
        }
        delete scene;
        delete cam;
    }
    fprintf(json, " ]\n}\n");
    fclose(json);
    return 0;
}
