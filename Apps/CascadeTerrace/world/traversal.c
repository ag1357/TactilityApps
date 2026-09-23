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
/* ---- sparse river reconstruction ----
   The whole river is 8 pinned meander segments plus a monotonic elevation
   profile with typed drops. Every value is a pure function of the sparse
   record and t, so a chunk reconstructs its window without any neighbour
   state and adjacent chunks agree exactly on shared boundary values. */
static int32_t river_lateral(const WsFeature* f, int64_t amplitude, int k) {
    if (k <= 0 || k >= 8) return 0;
    uint32_t m = (uint32_t)(2 * amplitude + 1);
    return (int32_t)(ws_hash(f->seed ^ (uint32_t)(k * 0x9E3779B1u)) % m) - (int32_t)amplitude;
}
static WsPos river_plan(const WsRecipe* r, uint16_t fi, uint16_t t) {
    const WsFeature* f = &r->features[fi];
    int64_t dx = (int64_t)f->down.x - f->up.x, dz = (int64_t)f->down.z - f->up.z;
    int64_t L = isqrt64(dx * dx + dz * dz);
    int64_t a = (int64_t)f->width * 3;
    if (a > L / 4) a = L / 4;
    int seg = t / 8192;
    if (seg > 7) seg = 7;
    int64_t u = t - seg * 8192;
    /* Segment 7 spans 57344..65535 (8191 units), so its lateral blend must
       complete by t=65535: the downstream endpoint is pinned exactly. */
    int64_t span = seg == 7 ? 8191 : 8192;
    int32_t l0 = river_lateral(f, a, seg), l1 = river_lateral(f, a, seg + 1);
    int64_t lat = l0 + (l1 - l0) * u / span;
    WsPos p;
    p.x = (int32_t)((int64_t)f->up.x + dx * t / 65535 - dz * lat / L);
    p.z = (int32_t)((int64_t)f->up.z + dz * t / 65535 + dx * lat / L);
    p.y = 0;
    return p;
}
static int32_t river_elevation(const WsRecipe* r, uint16_t fi, uint16_t t) {
    const WsFeature* f = &r->features[fi];
    int64_t base = (int64_t)f->up.y - f->down.y, stepped = 0;
    for (int i = 0; i < r->exception_count; i++) {
        const WsException* e = &r->exceptions[i];
        if (e->feature != fi || (e->type != WS_EXC_WATERFALL && e->type != WS_EXC_DAM)) continue;
        base -= e->aux;
        uint16_t at = e->type == WS_EXC_DAM ? (uint16_t)(e->at + e->length) : e->at;
        if (t > at) stepped += e->aux;
    }
    return (int32_t)((int64_t)f->up.y - base * t / 65535 - stepped);
}
int ws_river_sample(const WsRecipe* r, uint16_t fi, uint16_t t, WsRiverSample* out) {
    if (fi >= r->feature_count || !out || r->features[fi].kind != WS_FEATURE_RIVER) return 0;
    const WsFeature* f = &r->features[fi];
    out->pos = river_plan(r, fi, t);
    out->pos.y = river_elevation(r, fi, t);
    int seg = t / 8192;
    if (seg > 7) seg = 7;
    WsPos a = river_plan(r, fi, (uint16_t)(seg * 8192));
    WsPos b = river_plan(r, fi, (uint16_t)(seg == 7 ? 65535 : (seg + 1) * 8192));
    int64_t cx = b.x - a.x, cz = b.z - a.z;
    int64_t cl = isqrt64(cx * cx + cz * cz);
    out->tangent_x = cl ? (int32_t)(cx * 65536 / cl) : 65536;
    out->tangent_z = cl ? (int32_t)(cz * 65536 / cl) : 0;
    out->width = f->width;
    out->depth = f->depth;
    out->flow = f->flow;
    out->surfaced = 1;
    for (int i = 0; i < r->exception_count; i++) {
        const WsException* e = &r->exceptions[i];
        if (e->feature != fi) continue;
        if (t < e->at || (uint32_t)t > (uint32_t)e->at + e->length) continue;
        if (e->type == WS_EXC_RAPIDS)
            out->flow = WS_FLOW_RAPID;
        else if (e->type == WS_EXC_LAKE) {
            out->width = (uint16_t)(f->width * 4 > 65535 ? 65535 : f->width * 4);
            out->depth = (uint16_t)(f->depth * 3 > 65535 ? 65535 : f->depth * 3);
            out->flow = WS_FLOW_CALM;
        } else if (e->type == WS_EXC_UNDERGROUND)
            out->surfaced = 0;
        else if (e->type == WS_EXC_DAM)
            out->flow = WS_FLOW_CALM;
    }
    return 1;
}
int ws_river_window(const WsRecipe* r, uint16_t fi, WsPos lo, WsPos hi, uint16_t* t0, uint16_t* t1) {
    /* Horizontal chunk window (y ignored): the spanning t range whose
       segments can reach it, widened to cover lake reaches. Gaps inside the
       span are the caller's concern; sampling stays chunk-local. */
    if (fi >= r->feature_count || r->features[fi].kind != WS_FEATURE_RIVER) return 0;
    const WsFeature* f = &r->features[fi];
    int64_t margin = (int64_t)f->width * 4 + 500;
    int first = -1, last = -1;
    for (int k = 0; k < 8; k++) {
        WsPos a = river_plan(r, fi, (uint16_t)(k * 8192));
        WsPos b = river_plan(r, fi, (uint16_t)(k == 7 ? 65535 : (k + 1) * 8192));
        int64_t x0 = a.x < b.x ? a.x : b.x, x1 = a.x < b.x ? b.x : a.x;
        int64_t z0 = a.z < b.z ? a.z : b.z, z1 = a.z < b.z ? b.z : a.z;
        if (x1 + margin < lo.x || x0 - margin > hi.x || z1 + margin < lo.z || z0 - margin > hi.z) continue;
        if (first < 0) first = k * 8192;
        last = k == 7 ? 65535 : (k + 1) * 8192;
    }
    if (first < 0) return 0;
    if (t0) *t0 = (uint16_t)first;
    if (t1) *t1 = (uint16_t)last;
    return 1;
}
