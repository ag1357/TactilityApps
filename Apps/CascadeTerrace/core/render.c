#include "render.h"
#include "font.inc"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    int16_t x, y, z, w, h, d;
    uint32_t rgb;
    int16_t bone;
} MeshPart;
#include "kyra.inc"
int render_load_assets(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char bytes[6 + 16 * 18];
    size_t n = fread(bytes, 1, sizeof(bytes), f);
    int extra = fgetc(f);
    fclose(f);
    if (n < 6 || extra != EOF || memcmp(bytes, "CTM1", 4)) return 0;
    int count = bytes[4] | bytes[5] << 8;
    if (count < 1 || count > 16 || n != 6 + (size_t)count * 18) return 0;
    MeshPart parts[16] = {0};
    for (int i = 0; i < count; i++) {
        unsigned char* b = bytes + 6 + i * 18;
        int16_t values[6];
        for (int j = 0; j < 6; j++) values[j] = (int16_t)(b[j * 2] | b[j * 2 + 1] << 8);
        parts[i] = (MeshPart) {values[0], values[1], values[2], values[3], values[4], values[5], (uint32_t)b[12] | (uint32_t)b[13] << 8 | (uint32_t)b[14] << 16 | (uint32_t)b[15] << 24, (int16_t)(b[16] | b[17] << 8)};
        if (parts[i].w < 0 || parts[i].h < 0 || parts[i].d < 0 || parts[i].w > 3000 || parts[i].h > 3000 || parts[i].d > 3000 || abs(parts[i].bone) > 1) return 0;
    }
    memcpy(kyra_parts, parts, sizeof(parts));
    kyra_count = count;
    return 1;
}
typedef struct {
    float x, y, z;
} V;
typedef struct {
    float x, y, z;
} P;
static Renderer* rr;
static float cy, sy, cp, sp, light;
static uint16_t color(int r, int g, int b) {
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}
static uint16_t fog(uint32_t col, float z, float shade) {
    float f = z / 210;
    if (f > .88f) f = .88f;
    if (f < 0) f = 0;
    shade *= light;
    int r = (int)(((col >> 16) & 255) * shade * (1 - f) + 92 * f), g = (int)(((col >> 8) & 255) * shade * (1 - f) + 121 * f), b = (int)((col & 255) * shade * (1 - f) + 154 * f);
    return color(r, g, b);
}
static V transform(V v) {
    float x = v.x - rr->camera_x, y = v.y - rr->camera_y, z = v.z - rr->camera_z;
    float f = x * sy + z * cy;
    return (V) {x * cy - z * sy, y * cp - f * sp, y * sp + f * cp};
}
static P project(V v) { return (P) {120 + v.x * 145 / v.z, 78 - v.y * 145 / v.z, v.z}; }
static float edge(P a, P b, float x, float y) { return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x); }
static void raster(V aa, V bb, V cc, uint32_t col, float shade) {
    P a = project(aa), b = project(bb), c = project(cc);
    float area = edge(a, b, c.x, c.y);
    if (fabsf(area) < .02f) return;
    int minx = (int)floorf(fminf(a.x, fminf(b.x, c.x))), maxx = (int)ceilf(fmaxf(a.x, fmaxf(b.x, c.x))), miny = (int)floorf(fminf(a.y, fminf(b.y, c.y))), maxy = (int)ceilf(fmaxf(a.y, fmaxf(b.y, c.y)));
    if (minx < 0) minx = 0;
    if (maxx >= W) maxx = W - 1;
    if (miny < 0) miny = 0;
    if (maxy >= H) maxy = H - 1;
    if (minx > maxx || miny > maxy) return;
    rr->triangles++;
    float ia = 1 / a.z, ib = 1 / b.z, ic = 1 / c.z;
    uint16_t rgb = fog(col, (a.z + b.z + c.z) / 3, shade);
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            float u = edge(b, c, x + .5f, y + .5f) / area, v = edge(c, a, x + .5f, y + .5f) / area, w = 1 - u - v;
            if (u < 0 || v < 0 || w < 0) continue;
            float iz = u * ia + v * ib + w * ic;
            int depth = (int)(100 / iz);
            int pos = y * W + x;
            if (depth < rr->depth[pos]) {
                rr->depth[pos] = (uint16_t)depth;
                rr->pixels[pos] = rgb;
                rr->pixels_written++;
            }
        }
}
static void tri(V a, V b, V c, uint32_t col, float shade) {
    V input[5] = {transform(a), transform(b), transform(c)}, out[5];
    int count = 0;
    for (int i = 0; i < 3; i++) {
        V p = input[i], q = input[(i + 1) % 3];
        int pin = p.z >= .2f, qin = q.z >= .2f;
        if (pin) out[count++] = p;
        if (pin != qin) {
            float t = (.2f - p.z) / (q.z - p.z);
            out[count++] = (V) {p.x + t * (q.x - p.x), p.y + t * (q.y - p.y), .2f};
        }
    }
    for (int i = 1; i < count - 1; i++) raster(out[0], out[i], out[i + 1], col, shade);
}
static void quad(V a, V b, V c, V d, uint32_t col, float shade) {
    tri(a, b, c, col, shade);
    tri(a, c, d, col, shade);
}
static void box(float x, float y, float z, float w, float h, float d, uint32_t col) {
    V a = {x - w, y, z - d}, b = {x + w, y, z - d}, c = {x + w, y, z + d}, dd = {x - w, y, z + d};
    V e = a, f = b, g = c, hh = dd;
    e.y += h;
    f.y += h;
    g.y += h;
    hh.y += h;
    quad(a, b, f, e, col, .8f);
    quad(b, c, g, f, col, .65f);
    quad(c, dd, hh, g, col, 1);
    quad(dd, a, e, hh, col, .9f);
    quad(e, f, g, hh, col, 1.1f);
}
static void cone(float x, float y, float z, float r, float h, uint32_t col) {
    for (int i = 0; i < 6; i++) {
        float a = i * 1.04719755f, b = (i + 1) * 1.04719755f;
        tri((V) {x + r * cosf(a), y, z + r * sinf(a)}, (V) {x, y + h, z}, (V) {x + r * cosf(b), y, z + r * sinf(b)}, col, .7f + i * .07f);
    }
}
static void person(float x, float y, float z, int kyra, float phase) {
    float walk = sinf(phase) * .12f;
    if (kyra) {
        for (int i = 0; i < kyra_count; i++) {
            MeshPart* p = &kyra_parts[i];
            box(x + p->x / 1000.f, y + p->y / 1000.f, z + p->z / 1000.f + walk * p->bone, p->w / 1000.f, p->h / 1000.f, p->d / 1000.f, p->rgb);
        }
        return;
    }
    uint32_t cloth = kyra ? 0x39bcc5 : 0xe8ad66;
    box(x - .20f, y, z + walk, .13f, .72f, .14f, 0x253647);
    box(x + .20f, y, z - walk, .13f, .72f, .14f, 0x253647);
    box(x, y + .65f, z, .35f, .60f, .22f, cloth);
    box(x, y + 1.26f, z, .23f, .39f, .22f, 0xd9a376);
    box(x, y + 1.59f, z, .27f, .17f, .25f, kyra ? 0x493e66 : 0x28344f);
    box(x - .48f, y + .75f, z - walk, .10f, .45f, .12f, cloth);
    box(x + .48f, y + .75f, z + walk, .10f, .45f, .12f, cloth);
    box(x, y + 1.28f, z + .231f, .19f, .08f, .02f, 0x79dce3);
    if (kyra) box(x + .29f, y + .72f, z, .12f, .2f, .29f, 0xc09755);
}
static void building(const Game* g, int i) {
    const Site* s = &g->world.sites[i];
    float x = s->center.x / 1000.f, y = s->center.y / 1000.f, z = s->center.z / 1000.f, w = s->halfx / 1000.f, d = s->halfz / 1000.f, h = s->height / 1000.f;
    uint32_t wall = i == STATION ? 0x9fa7b6 : i == HOME ? 0xc49c87
                                                        : 0x8c879d;
    if (i == BRIDGE) {
        box(x, y - .7f, z, w, .8f, d, 0xb69a8b);
        for (int side = -1; side <= 1; side += 2) {
            box(x, y + 1, z + side * (d - .4f), w, .15f, .15f, 0x66bbc8);
            for (int p = -16; p <= 16; p += 4) box(x + p, y, z + side * (d - .4f), .12f, 1.2f, .12f, 0x454f6b);
        }
        box(18, y, z, 2, 26, 2, 0x89809e);
        box(18, y + 26, z, 3, 1, 3, 0x65cad4);
        return;
    }
    box(x, y - .5f, z, w + .5f, .5f, d + .5f, 0x5f6279);
    if (i == MARKET) {
        box(x, y - 4, z, w, 4, d, 0x897c92);
        for (int side = -1; side <= 1; side += 2) {
            box(x + side * 5, y, z, .2f, 4, .2f, 0x59485d);
            box(x + side * 5, y, z + 5, .2f, 4, .2f, 0x59485d);
        }
        quad((V) {x - 6, y + 4, z - 1}, (V) {x + 6, y + 4, z - 1}, (V) {x + 6, y + 3, z + 6}, (V) {x - 6, y + 3, z + 6}, 0xbc6e91, 1);
        box(x, y, z + 2, 4, 1.2f, 1, 0xc29461);
        for (int k = 0; k < 5; k++) box(x - 3 + k * 1.5f, y + 1.2f, z + 2, .4f, .5f, .4f, k % 2 ? 0x67cec3 : 0xd7ba66);
        return;
    }
    box(x - w, y, z, .4f, h, d, wall);
    box(x + w, y, z, .4f, h, d, wall);
    box(x, y, z - d, w, h, .4f, wall);
    box(x - (w + 2.2f) / 2, y, z + d, (w - 2.2f) / 2, h, .4f, wall);
    box(x + (w + 2.2f) / 2, y, z + d, (w - 2.2f) / 2, h, .4f, wall);
    box(x, y + 3.4f, z + d, 2.2f, h - 3.4f, .4f, wall);
    quad((V) {x - w - .8f, y + h, z - d - .8f}, (V) {x + w + .8f, y + h, z - d - .8f}, (V) {x + w + .8f, y + h + 1, z}, (V) {x - w - .8f, y + h + 1, z}, 0x4d536c, 1);
    quad((V) {x - w - .8f, y + h + 1, z}, (V) {x + w + .8f, y + h + 1, z}, (V) {x + w + .8f, y + h, z + d + .8f}, (V) {x - w - .8f, y + h, z + d + .8f}, 0x64647e, 1);
    box(x, y + 3.15f, z + d + .5f, 2.6f, .18f, .12f, 0x4bdfd5);
    for (int k = -1; k <= 1; k += 2) box(x + k * w * .62f, y + 2, z + d + .45f, 1.8f, 1.7f, .06f, 0x28536c);
    if (i == STATION) {
        box(x, y + .2f, z - 6, 3, 1.4f, 1, 0x283d54);
        box(x, y + 1.6f, z - 6, 2.5f, .7f, .8f, 0x5ddacf);
        box(x, y, z + d + .8f, 1, .9f, .55f, g->state.repaired ? 0x65dfb6 : 0xde805e);
        for (int k = 0; k < 4; k++) {
            float a = g->state.time / 12000.f + k * 1.570796f;
            tri((V) {x - w - .5f, y + 4, z}, (V) {x - w - .5f, y + 4 + 3 * cosf(a), z + 3 * sinf(a)}, (V) {x - w - .5f, y + 4 + 2 * cosf(a + .5f), z + 2 * sinf(a + .5f)}, 0x7ddcdb, 1);
        }
    }
}
void render(Renderer* r, const Game* g) {
    rr = r;
    r->triangles = r->pixels_written = 0;
    r->frame++;
    float yaw = g->state.yaw * .01745329252f;
    r->yaw = yaw;
    r->pitch = r->conversation ? 0 : -.19f;
    cy = cosf(yaw);
    sy = sinf(yaw);
    cp = cosf(r->pitch);
    sp = sinf(r->pitch);
    float px = g->state.player_pos.x / 1000.f, py = g->state.player_pos.y / 1000.f, pz = g->state.player_pos.z / 1000.f;
    r->camera_x = px - (r->conversation ? 0 : 6) * sy;
    r->camera_z = pz - (r->conversation ? 0 : 6) * cy;
    r->camera_y = py + (r->conversation ? 1.5f : 4.3f);
    float ch = ground_at(&g->world, (int)(r->camera_x * 1000), (int)(r->camera_z * 1000)) / 1000.f + 1;
    if (r->camera_y < ch) r->camera_y = ch;
    light = .35f + .65f * fmaxf(0, sinf((g->state.time % 86400000) / 86400000.f * 6.2831853f - 1.570796f));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            r->pixels[y * W + x] = color((int)((60 + y * .3f) * light), (int)((88 + y * .3f) * light), (int)((131 + y * .25f) * light));
            r->depth[y * W + x] = 65535;
        }
    for (int iz = 0; iz < MAP_N - 1; iz++)
        for (int ix = 0; ix < MAP_N - 1; ix++) {
            float x = ix * 5 - 200, z = iz * 5 - 200;
            if (fabsf(x - px) > 170 || fabsf(z - pz) > 170) continue;
            V a = {x, g->world.height[iz * MAP_N + ix] / 100.f, z}, b = {x + 5, g->world.height[iz * MAP_N + ix + 1] / 100.f, z}, c = {x + 5, g->world.height[(iz + 1) * MAP_N + ix + 1] / 100.f, z + 5}, d = {x, g->world.height[(iz + 1) * MAP_N + ix] / 100.f, z + 5};
            uint32_t h = hash32(g->world.seed ^ (uint32_t)(iz * MAP_N + ix));
            uint32_t col = a.y > 32 ? 0x8b849e : a.y > 16 ? 0x788e8b
                                                          : 0x668e79;
            if ((h & 15) == 0) col += 0x060602;
            tri(a, b, c, col, .86f);
            tri(a, c, d, col, 1);
        }
    for (int z = -130; z < 180; z += 10) {
        if (z >= -80 && z < -70) continue;
        quad((V) {-6, .2f, z}, (V) {6, .2f, z}, (V) {6, .2f, z + 10}, (V) {-6, .2f, z + 10}, 0x438cab + (z % 30 == 0 ? 0x082221 : 0), 1);
    }
    for (int k = 0; k < 6; k++) {
        float x = -5 + k * 1.7f;
        quad((V) {x, 1, -133}, (V) {x + 1.7f, 1, -133}, (V) {x + 1.7f, 43, -136}, (V) {x, 43, -136}, k % 2 ? 0x83cbd7 : 0xbedfe2, 1.3f);
        float y = 42 - fmodf(g->state.time / 180.f + k * 7, 42);
        box(x, y, -132.8f, .7f, .5f, .03f, 0xc5f2ee);
    }
    for (int i = 0; i < STRUCT_COUNT; i++) building(g, i);
    for (int i = 0; i < 105; i++) {
        uint32_t h = hash32(g->world.seed + (uint32_t)i * 73);
        float x = -160 + (int)(h % 115), z = -110 + (int)((h >> 12) % 260);
        if (fabsf(x - px) > 110 || fabsf(z - pz) > 110) continue;
        int skip = 0;
        for (int s = 0; s < STRUCT_COUNT; s++)
            if (fabsf(x - g->world.sites[s].center.x / 1000.f) < 20 && fabsf(z - g->world.sites[s].center.z / 1000.f) < 22) skip = 1;
        if (skip) continue;
        float y = ground_at(&g->world, (int)(x * 1000), (int)(z * 1000)) / 1000.f;
        box(x, y, z, .3f, 4, .3f, 0x786658);
        cone(x, y + 2, z, 2.6f, 7 + (h % 4), 0x477d72);
        cone(x, y + 5, z, 1.9f, 5, 0x5d9986);
    }
    for (int i = 0; i < 2; i++)
        if (!(g->state.evidence_taken & (1 << i)) && !(i == 0 && g->world.variant == 1)) {
            Pos p = g->world.evidence[i];
            box(p.x / 1000.f, p.y / 1000.f + .15f, p.z / 1000.f, i ? .45f : .8f, .25f, .3f, i ? 0xebd7a2 : 0xdcb66c);
        }
    Pos n = npc_position(g);
    person(n.x / 1000.f, n.y / 1000.f, n.z / 1000.f, 1, g->state.time / 6000.f);
    if (!r->conversation) person(px, py, pz, 0, g->state.time / 900.f);
    /* Phos motes are rendering only: no particle state in canonical save. */
    for (int i = 0; i < 18; i++) {
        uint32_t h = hash32((uint32_t)i + g->world.seed);
        float x = (int)(h % 2000) / 100.f - 10, z = -125 + (int)((h >> 12) % 1200) / 100.f, y = 1 + fmodf(g->state.time / 4000.f + i, 7);
        box(x, y, z, .07f, .15f, .07f, 0x93ffe2);
    }
}
void draw_panel(Renderer* r, int x, int y, int w, int h, uint16_t col) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < W && j >= 0 && j < H) r->pixels[j * W + i] = col;
}
void draw_text(Renderer* r, int x, int y, const char* s, uint16_t col, int wrap) {
    int start = x;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '\n' || (wrap && x + 5 >= wrap)) {
            y += 9;
            x = start;
            if (c == '\n') continue;
        }
        if (y + 8 >= H) break;
        if (c < 32 || c > 126) c = '?';
        for (int yy = 0; yy < 8; yy++)
            for (int xx = 0; xx < 5; xx++)
                if (font[c - 32][yy] & (1 << xx)) {
                    int dx = x + xx, dy = y + yy;
                    if (dx >= 0 && dx < W && dy >= 0 && dy < H) r->pixels[dy * W + dx] = col;
                }
        x += 6;
    }
}
int screenshot(const Renderer* r, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = r->pixels[i];
        uint8_t b[3] = {(uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(b, 1, 3, f);
    }
    return fclose(f) == 0;
}

static void world_box(void *ctx,WsPos p,WsPos s,uint32_t c) {
    WsMetrics *m=ctx;m->triangles+=10;m->vertices+=8;
    box(p.x/1000.f,p.y/1000.f,p.z/1000.f,s.x/2000.f,s.y/1000.f,s.z/2000.f,c);
}
void render_world(Renderer *r,const WsRecipe *world,WsAddress player,int yaw,WsMetrics *metrics) {
    memset(metrics,0,sizeof(*metrics));rr=r;r->triangles=r->pixels_written=0;r->frame++;
    r->yaw=yaw*.01745329252f;r->pitch=-.19f;cy=cosf(r->yaw);sy=sinf(r->yaw);cp=cosf(r->pitch);sp=sinf(r->pitch);light=1;
    r->camera_x=player.pos.x/1000.f-6*sy;r->camera_z=player.pos.z/1000.f-6*cy;r->camera_y=player.pos.y/1000.f+4.3f;
    for(int i=0;i<W*H;i++){r->pixels[i]=0x6393;r->depth[i]=65535;}
    for(uint16_t i=0;i<world->count;i++) {
        WsModule m;ws_materialize(world,i,&m);if((m.kind>>8)<WS_STRUCTURE)continue;
        if(player.scope==UINT16_MAX&&(m.flags&WS_INTERIOR))continue;
        if(player.scope!=UINT16_MAX&&i!=player.scope&&m.parent!=player.scope)continue;
        WsFidelity f=ws_fidelity(m.pos,player.pos,(m.flags&WS_INTERIOR)!=0);
        if(f==WS_UNLOADED)continue;
        if(f==WS_PROXY){metrics->proxy++;world_box(metrics,m.pos,m.size,m.color);}
        else {if(f==WS_ACTIVE)metrics->active++;else metrics->materialized++;ws_boxes(&m,world_box,metrics);}
    }
    if(player.scope==UINT16_MAX) for(uint16_t i=0;i<world->link_count;i++) {
        WsLink l=world->links[i];if(l.kind)continue;WsModule a,b;ws_materialize(world,l.a,&a);ws_materialize(world,l.b,&b);
        float dx=(b.pos.x-a.pos.x)/1000.f,dz=(b.pos.z-a.pos.z)/1000.f,d=sqrtf(dx*dx+dz*dz);if(d<.1f)continue;
        float ox=-dz*2/d,oz=dx*2/d;
        V aa={a.pos.x/1000.f,a.pos.y/1000.f+.3f,a.pos.z/1000.f},bb={b.pos.x/1000.f,b.pos.y/1000.f+.3f,b.pos.z/1000.f};
        quad((V){aa.x+ox,aa.y,aa.z+oz},(V){bb.x+ox,bb.y,bb.z+oz},(V){bb.x-ox,bb.y,bb.z-oz},(V){aa.x-ox,aa.y,aa.z-oz},0xaaa394,1);
        metrics->triangles+=2;metrics->vertices+=4;
    }
    person(player.pos.x/1000.f,player.pos.y/1000.f,player.pos.z/1000.f,0,r->frame/20.f);
}
