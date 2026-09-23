/* Isolate which dump groups Jet draws vs drops. Usage: ./bench_groups <scene> <group> */
#include "Jet.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace Renderer;
extern unsigned long long jet_px_written, jet_tris_drawn;

static const int OUT_W = 240, OUT_H = 160;
struct DumpTri { float a[3], b[3], c[3]; unsigned col; float shade; int group; };
struct DumpScene { float cam[6]; std::vector<DumpTri> tris; };

static bool parse_dump(const char *path, DumpScene &out) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'C') sscanf(line + 2, "%f %f %f %f %f %f", &out.cam[0], &out.cam[1], &out.cam[2], &out.cam[3], &out.cam[4], &out.cam[5]);
        else if (line[0] == 'T') {
            DumpTri t;
            if (sscanf(line + 2, "%f %f %f %f %f %f %f %f %f %u %f %d",
                       &t.a[0], &t.a[1], &t.a[2], &t.b[0], &t.b[1], &t.b[2],
                       &t.c[0], &t.c[1], &t.c[2], &t.col, &t.shade, &t.group) == 12)
                out.tris.push_back(t);
        }
    }
    fclose(f);
    return true;
}

static uint16_t g_fbuf[240 * 160], g_zbuf[240 * 160];

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: %s <scene> <group|-1=all>\n", argv[0]); return 1; }
    DumpScene dump;
    char path[256];
    snprintf(path, sizeof(path), "/tmp/renderer-eval/out/%s.dump.txt", argv[1]);
    if (!parse_dump(path, dump)) { printf("no dump\n"); return 1; }
    int group = atoi(argv[2]);

    memset(g_fbuf, 0, sizeof(g_fbuf)); memset(g_zbuf, 0, sizeof(g_zbuf));
    Scene *scene = new Scene(g_fbuf, g_zbuf, OUT_W, OUT_H);
    static uint16_t sky[160];
    for (int y = 0; y < 160; y++) {
        int r = (int)((60 + y * .3f) * dump.cam[5]), g = (int)((88 + y * .3f) * dump.cam[5]), b = (int)((131 + y * .25f) * dump.cam[5]);
        sky[y] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
    }
    scene->backgroundGradientColors = sky;
    scene->setBackcolor(0x6393);

    Material *m = new Material(0xFFFF);
    Object *o = new Object();
    o->cullingMode = CullingMode::NO_CULLING; /* the current renderer never culls by winding */
    scene->addObject(o);
    int fed = 0;
    for (const DumpTri &t : dump.tris) {
        if (group >= 0 && t.group != group) continue;
        fed++;
        uint16_t base = (uint16_t)o->vertices.size();
        Object::Vertex v;
        v.position = Vector3((int32_t)llroundf(t.a[0] * 1000), (int32_t)llroundf(t.a[1] * 1000), (int32_t)llroundf(t.a[2] * 1000));
        o->vertices.push_back(v);
        v.position = Vector3((int32_t)llroundf(t.b[0] * 1000), (int32_t)llroundf(t.b[1] * 1000), (int32_t)llroundf(t.b[2] * 1000));
        o->vertices.push_back(v);
        v.position = Vector3((int32_t)llroundf(t.c[0] * 1000), (int32_t)llroundf(t.c[1] * 1000), (int32_t)llroundf(t.c[2] * 1000));
        o->vertices.push_back(v);
        Object::Triangle tri;
        tri.v1 = base; tri.v2 = base + 1; tri.v3 = base + 2;
        tri.material = m;
        o->triangles.push_back(tri);
    }
    o->calculateBoundingBox();
    Camera *cam = new Camera();
    cam->position = Vector3((int32_t)llroundf(dump.cam[0] * 1000), (int32_t)llroundf(dump.cam[1] * 1000), (int32_t)llroundf(dump.cam[2] * 1000));
    cam->rotation = Vector3_f(-dump.cam[4], dump.cam[3], 0.0f);
    cam->fovFactor = 145.0f;
    cam->nearPlane = 200;
    cam->farPlane = 200000;
    scene->setCamera(cam);
    scene->render();
    printf("scene %s group %d: fed %d tris, Jet drew %llu, px %llu\n", argv[1], group, fed, jet_tris_drawn, jet_px_written);
    snprintf(path, sizeof(path), "/tmp/renderer-eval/out/group_%s_%d.ppm", argv[1], group);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", OUT_W, OUT_H);
    for (int i = 0; i < OUT_W * OUT_H; i++) {
        uint16_t p = g_fbuf[i];
        uint8_t b3[3] = {(uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(b3, 1, 3, f);
    }
    fclose(f);
    return 0;
}
