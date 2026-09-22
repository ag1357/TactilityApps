#include "game.h"
#include "../content/cascade_adapter.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
static int32_t abs32(int32_t x) { return x < 0 ? -x : x; }
const char* site_name(int i) {
    static const char* n[] = {"Hydro-phos station", "Kyra's home", "terrace market", "footbridge", "maintenance tunnel"};
    return i >= 0 && i < 5 ? n[i] : "valley";
}
const char* item_name(int i) {
    static const char* n[] = {"Hydro-phos chits", "replacement coupling", "tap cable", "maintenance log", "concentrator", "repair toolkit", "station key", "Oren's note"};
    return i >= 0 && i < IT_COUNT ? n[i] : "item";
}
const char* op_name(int i) {
    static const char* n[] = {"none", "extracted Phos", "condensed Phos", "repaired", "damaged", "picked up", "dropped", "gave", "took", "bought", "sold", "transferred chits", "showed", "said", "promised", "waited", "funded", "broke a promise"};
    return i >= 0 && i <= BREAK_PROMISE ? n[i] : "acted";
}
static int32_t base_height(uint32_t seed, int x, int z) {
    int ax = abs32(x), h = x > 0 ? (ax * ax / 260) : (ax * ax / 550);
    if (z < -135) h += (-135 - z) * 2;
    if (abs32(z) > 175) h += (abs32(z) - 175) * 2;
    if (ax > 175) h += (ax - 175) * 2;
    h = h * 1000 + (int)(hash32(seed ^ (uint32_t)(x + 200) * 397U ^ (uint32_t)(z + 200) * 711U) % 1700) - 850;
    if (ax < 7 && z > -135) h = -1200;
    return h < -1200 ? -1200 : h;
}
/* Routes are generated surfaces, not a second navigation graph. Validation below
   flood-fills the same sampled surface used by movement. */
static int on_route(int x, int z, Pos a, Pos b, int* height) {
    int64_t dx = (b.x - a.x) / 1000, dz = (b.z - a.z) / 1000, px = x - a.x / 1000, pz = z - a.z / 1000;
    int64_t den = dx * dx + dz * dz, t = px * dx + pz * dz;
    if (!den) return 0;
    if (t < 0) t = 0;
    if (t > den) t = den;
    int64_t ex = px * den - dx * t, ez = pz * den - dz * t;
    if (ex * ex + ez * ez > 49 * den * den) return 0;
    *height = a.y + (int)((b.y - a.y) * t / den);
    return 1;
}
void generate(Generated* w, uint32_t seed, int variant) {
    memset(w, 0, sizeof(*w));
    w->seed = seed;
    w->version = GEN_VERSION;
    w->variant = variant >= 0 ? (uint8_t)(variant % 3) : (uint8_t)(hash32(seed ^ 0xa217U) % 3);
    if (!cascade_recipe_sites(w,seed)) return;
    Pos nodes[7] = {w->sites[STATION].center, {22000, 3000, -75000}, w->sites[HOME].center, w->sites[MARKET].center, {-22000, 3000, -75000}, w->sites[TUNNEL].center, {-22000, 0, 30000}};
    const int links[][2] = {{0, 1}, {1, 2}, {2, 3}, {1, 4}, {4, 5}, {5, 6}, {6, 4}};
    for (int iz = 0; iz < MAP_N; iz++)
        for (int ix = 0; ix < MAP_N; ix++) {
            int x = ix * 5 - 200, z = iz * 5 - 200, h = base_height(seed, x, z), rh;
            for (size_t j = 0; j < sizeof(links) / sizeof(links[0]); j++)
                if (on_route(x, z, nodes[links[j][0]], nodes[links[j][1]], &rh)) h = rh;
            for (int j = 0; j < STRUCT_COUNT; j++) {
                Site* s = &w->sites[j];
                if (abs32(x * 1000 - s->center.x) < s->halfx + 6500 && abs32(z * 1000 - s->center.z) < s->halfz + 6500) h = s->center.y;
            }
            w->height[iz * MAP_N + ix] = (int16_t)(h / 10);
        }
    w->evidence[0] = w->sites[(hash32(seed ^ 99) & 1) ? MARKET : TUNNEL].center;
    w->evidence[0].z += 6000;
    w->evidence[1] = w->sites[STATION].center;
    w->evidence[1].z -= 5000;
}
int32_t ground_at(const Generated* w, int32_t x, int32_t z) {
    x += 200000;
    z += 200000;
    if (x < 0) x = 0;
    if (z < 0) z = 0;
    if (x > 399999) x = 399999;
    if (z > 399999) z = 399999;
    int ix = x / 5000, iz = z / 5000, fx = x % 5000, fz = z % 5000;
    int a = w->height[iz * MAP_N + ix] * 10, b = w->height[iz * MAP_N + ix + 1] * 10, c = w->height[(iz + 1) * MAP_N + ix] * 10, d = w->height[(iz + 1) * MAP_N + ix + 1] * 10;
    return (int)(((int64_t)a * (5000 - fx) * (5000 - fz) + (int64_t)b * fx * (5000 - fz) + (int64_t)c * (5000 - fx) * fz + (int64_t)d * fx * fz) / 25000000);
}
Pos npc_position(const Game* g) {
    int64_t m = g->state.time / 60000 % 1440;
    SiteId s = m >= 420 && m < 900 ? STATION : m >= 900 && m < 1020 ? MARKET
                                                                    : HOME;
    Pos p = g->world.sites[s].center;
    p.z += g->world.sites[s].halfz + 2000;
    p.y = ground_at(&g->world, p.x, p.z);
    return p;
}
int can_stand(const Game* g, Pos p, int32_t radius) {
    if (abs32(p.x) > 194000 || abs32(p.z) > 194000) return 0;
    if (abs32(p.x) < 6500 + radius && p.z > -130000 && abs32(p.z + 75000) > 5000) return 0;
    for (int i = 0; i < STRUCT_COUNT; i++) {
        if (i == BRIDGE || i == MARKET) continue;
        const Site* s = &g->world.sites[i];
        int x = abs32(p.x - s->center.x), z = abs32(p.z - s->center.z);
        if (x < s->halfx + radius && z < s->halfz + radius) {
            if (x > s->halfx - 500 - radius || z > s->halfz - 500 - radius) {
                if (!(p.z > s->center.z && x < 2200 - radius)) return 0;
            }
        }
    }
    return 1;
}
int walk_edge(const Game* g, Pos a, Pos b) {
    int steps = (abs32(b.x - a.x) + abs32(b.z - a.z)) / 100 + 1;
    int old = ground_at(&g->world, a.x, a.z);
    for (int i = 1; i <= steps; i++) {
        Pos p = {a.x + (b.x - a.x) * i / steps, 0, a.z + (b.z - a.z) * i / steps};
        int h = ground_at(&g->world, p.x, p.z);
        if (!can_stand(g, p, 350) || h - old > 110) return 0;
        old = h;
    }
    return 1;
}
int validate_world(const Generated* w, char* error, size_t cap) {
    Game g = {0};
    g.world = *w;
    uint8_t seen[MAP_N * MAP_N] = {0};
    uint16_t q[MAP_N * MAP_N];
    int head = 0, tail = 0;
    Pos start = {22000, 0, -90000};
    int sx = (start.x + 200000) / 5000, sz = (start.z + 200000) / 5000;
    int startidx = sz * MAP_N + sx;
    seen[startidx] = 1;
    q[tail++] = (uint16_t)startidx;
    while (head < tail) {
        int u = q[head++], x = u % MAP_N, z = u / MAP_N;
        int dx[] = {1, -1, 0, 0}, dz[] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], nz = z + dz[k];
            if (nx < 1 || nz < 1 || nx >= MAP_N - 1 || nz >= MAP_N - 1) continue;
            int v = nz * MAP_N + nx;
            Pos p = {nx * 5000 - 200000, w->height[v] * 10, nz * 5000 - 200000};
            if (!seen[v] && abs32((w->height[v] - w->height[u]) * 10) <= 5000 && walk_edge(&g, (Pos) {x * 5000 - 200000, 0, z * 5000 - 200000}, p)) {
                seen[v] = 1;
                q[tail++] = (uint16_t)v;
            }
        }
    }
    for (int i = 0; i < STRUCT_COUNT; i++) {
        const Site* s = &w->sites[i];
        Pos p = s->center;
        p.z += s->halfz + 2000;
        int ix = (p.x + 200000) / 5000, iz = (p.z + 200000) / 5000;
        int ok = 0;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++)
                if (seen[(iz + dz) * MAP_N + ix + dx]) ok = 1;
        if (!ok) {
            snprintf(error, cap, "seed %u: route to %s unreachable", (unsigned)w->seed, site_name(i));
            return 0;
        }
        for (int corner = 0; corner < 4; corner++) {
            int xx = s->center.x + ((corner & 1) ? s->halfx : -s->halfx);
            int zz = s->center.z + ((corner & 2) ? s->halfz : -s->halfz);
            if (abs32(ground_at(w, xx, zz) - s->center.y) > 30) {
                snprintf(error, cap, "seed %u: foundation corner %d", (unsigned)w->seed, i);
                return 0;
            }
        }
        if (abs32(ground_at(w, s->center.x, s->center.z) - s->center.y) > 30) {
            snprintf(error, cap, "seed %u: foundation %d", (unsigned)w->seed, i);
            return 0;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (i == 0 && w->variant == 1) continue;
        Pos p = w->evidence[i];
        int site = i == 1 ? STATION : ((hash32(w->seed ^ 99) & 1) ? MARKET : TUNNEL);
        Pos door = w->sites[site].center;
        door.z += w->sites[site].halfz + 2000;
        if (!walk_edge(&g, door, p)) {
            snprintf(error, cap, "seed %u: evidence route", (unsigned)w->seed);
            return 0;
        }
        if (!can_stand(&g, p, 300)) {
            snprintf(error, cap, "seed %u: evidence collision", (unsigned)w->seed);
            return 0;
        }
    }
    if (w->sites[BRIDGE].halfx < 12000 || w->sites[BRIDGE].center.x != 0) {
        snprintf(error, cap, "seed %u: bridge", (unsigned)w->seed);
        return 0;
    }
    if (!can_stand(&g, start, 300)) {
        snprintf(error, cap, "seed %u: spawn", (unsigned)w->seed);
        return 0;
    }
    return 1;
}
