#include "sdk.h"
#include <stdlib.h>

static int64_t idiv(int64_t a, int64_t b) {
    return a / b;
}

static int64_t isqrt64(int64_t n) {
    if (n <= 0) return 0;
    int64_t x = n, y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* Walkable height of one module at a point in its own footprint. */
static int32_t surface_at(const WsModule* m, int32_t x, int32_t z) {
    (void)x;
    int32_t y = m->pos.y;
    if ((m->kind & 255) == WS_RAMP) {
        int64_t t = (int64_t)z - m->pos.z + m->size.z / 2;
        int k = (int)(t * 8 / m->size.z);
        if (k < 0) k = 0;
        if (k > 7) k = 7;
        y += m->size.y * (k + 1) / 8;
    }
    return y;
}

/* Scaled t (0..65536) where the center->(dx,dz) ray leaves m's footprint. */
static int64_t exit_scale(const WsModule* m, int64_t dx, int64_t dz) {
    int64_t t = 65536;
    int64_t d[2] = {dx, dz}, h[2] = {m->size.x / 2, m->size.z / 2};
    for (int i = 0; i < 2; i++) {
        if (!d[i]) continue;
        int64_t ti = idiv((d[i] > 0 ? h[i] : -h[i]) << 16, d[i]);
        if (ti < t) t = ti;
    }
    return t < 0 ? 0 : t;
}

/* Scaled t where the ray from (dx,dz) away enters m's footprint; m is the
   segment endpoint, so this is measured from the other endpoint's centre. */
static int64_t entry_scale(const WsModule* m, int64_t dx, int64_t dz) {
    int64_t t = 0;
    int64_t d[2] = {dx, dz}, h[2] = {m->size.x / 2, m->size.z / 2};
    for (int i = 0; i < 2; i++) {
        if (!d[i]) continue;
        int64_t ti = idiv((d[i] > 0 ? (d[i] - h[i]) : (d[i] + h[i])) << 16, d[i]);
        if (ti > t) t = ti;
    }
    return t > 65536 ? 65536 : t;
}

int ws_ground(const WsRecipe* r, WsAddress at, int32_t ceiling, int32_t* height) {
    if (llabs(at.pos.x) > 1000000 || llabs(at.pos.y) > 1000000 || llabs(at.pos.z) > 1000000) return 0;
    int found = ws_surface(r, at, ceiling, height);
    int32_t best = found ? *height : INT32_MIN;
    /* Corridors are local paths, not slabs: where declared corridor bands
       overlap, the most local path (nearest centerline) provides the
       ground, so a walker on its own route cannot be captured by a
       foreign band that later ends in an impassable drop. Exact distance
       ties (collinear or crossing decks) resolve to the higher surface,
       which keeps a walker on the deck it is already standing on; the
       ceiling filter still excludes decks above reach. */
    int cfound = 0;
    int32_t cbest = 0;
    int64_t cdist = 0;
    if (at.scope == UINT16_MAX)
        for (uint16_t i = 0; i < r->link_count; i++) {
            WsLink l = r->links[i];
            if (l.kind != 0) continue;
            WsModule a, b;
            ws_materialize(r, l.a, &a);
            ws_materialize(r, l.b, &b);
            int64_t dx = (int64_t)b.pos.x - a.pos.x, dz = (int64_t)b.pos.z - a.pos.z;
            int64_t den = dx * dx + dz * dz, px = (int64_t)at.pos.x - a.pos.x, pz = (int64_t)at.pos.z - a.pos.z;
            if (!den) continue;
            int64_t t = px * dx + pz * dz;
            if (t < 0 || t > den) continue;
            int64_t ex = px - dx * t / den, ez = pz - dz * t / den, d2 = ex * ex + ez * ez;
            if (d2 > 2000LL * 2000) continue;
            /* Approach corridor: flat through each endpoint's collision
               skirt, then a straight blend between the endpoint surfaces
               measured at the footprint boundaries. The corridor never
               runs beneath a slab it must deliver the walker onto. */
            int64_t len = isqrt64(den);
            int64_t skirt = len ? idiv(300LL << 16, len) : 0;
            int64_t ta_raw = exit_scale(&a, dx, dz), tb_raw = entry_scale(&b, dx, dz);
            int32_t xa = a.pos.x + (int32_t)(dx * ta_raw / 65536), za = a.pos.z + (int32_t)(dz * ta_raw / 65536);
            int32_t xb = a.pos.x + (int32_t)(dx * tb_raw / 65536), zb = a.pos.z + (int32_t)(dz * tb_raw / 65536);
            int32_t ya = surface_at(&a, xa, za), yb = surface_at(&b, xb, zb);
            int64_t ta = ta_raw + skirt, tb = tb_raw - skirt;
            int64_t ts = idiv(t << 16, den);
            int64_t f;
            if (tb > ta) {
                f = idiv((ts - ta) * 65536, tb - ta);
                if (f < 0) f = 0;
                if (f > 65536) f = 65536;
            } else
                f = ts < 32768 ? 0 : 65536;
            int32_t y = ya + (int32_t)((int64_t)(yb - ya) * f / 65536);
            if (y > ceiling) continue;
            if (!cfound || d2 < cdist || (d2 == cdist && y > cbest)) {
                cfound = 1;
                cbest = y;
                cdist = d2;
            }
        }
    if (cfound && (!found || cbest > best)) {
        found = 1;
        best = cbest;
    }
    if (found) *height = best;
    return found;
}
int ws_move(const WsRecipe* r, WsAddress* at, int32_t dx, int32_t dz) {
    /* Short swept steps prevent tunnelling. No teleport fallback on failed motion. */
    if (llabs(at->pos.x) > 1000000 || llabs(at->pos.y) > 1000000 || llabs(at->pos.z) > 1000000) return 0;
    if (llabs(dx) > 1000 || llabs(dz) > 1000) return 0;
    int steps = (abs(dx) + abs(dz)) / 100 + 1;
    WsAddress old = *at;
    for (int i = 1; i <= steps; i++) {
        WsAddress next = *at;
        next.pos.x = old.pos.x + dx * i / steps;
        next.pos.z = old.pos.z + dz * i / steps;
        int32_t y;
        if (!ws_ground(r, next, at->pos.y + 350, &y) || y < at->pos.y - 500) return 0;
        next.pos.y = y;
        if (ws_collision(r, next, 300)) return 0;
        *at = next;
    }
    return 1;
}
int ws_use_link(const WsRecipe* r, WsTraveler* t) {
    if (t->remaining) return 0;
    for (uint16_t i = 0; i < r->link_count; i++) {
        WsLink l = r->links[i];
        if (!l.kind) continue;
        for (int side = 0; side < 2; side++) {
            uint16_t from = side ? l.b : l.a, to = side ? l.a : l.b;
            WsModule a, b;
            ws_materialize(r, from, &a);
            ws_materialize(r, to, &b);
            uint16_t scope = (a.flags & WS_INTERIOR) ? from : UINT16_MAX;
            if (scope != t->at.scope) continue;
            if (llabs((int64_t)t->at.pos.x - a.pos.x) > 2500 || llabs((int64_t)t->at.pos.z - a.pos.z) > 2500 || llabs((int64_t)t->at.pos.y - a.pos.y) > 1000) continue;
            t->destination = (WsAddress) {{b.pos.x, b.pos.y, b.pos.z}, (b.flags & WS_INTERIOR) ? to : UINT16_MAX};
            if (l.kind == 2) {
                t->at = t->destination;
                return 1;
            }
            t->remaining = (uint32_t)(llabs((int64_t)b.pos.y - a.pos.y) / 2 + 1);
            return 1;
        }
    }
    return 0;
}
void ws_travel_tick(WsTraveler* t, uint32_t ms) {
    if (!t->remaining) return;
    if (ms >= t->remaining) {
        t->at = t->destination;
        t->remaining = 0;
        return;
    }
    t->at.pos.x += (int32_t)(((int64_t)t->destination.pos.x - t->at.pos.x) * ms / t->remaining);
    t->at.pos.y += (int32_t)(((int64_t)t->destination.pos.y - t->at.pos.y) * ms / t->remaining);
    t->at.pos.z += (int32_t)(((int64_t)t->destination.pos.z - t->at.pos.z) * ms / t->remaining);
    t->remaining -= ms;
}
