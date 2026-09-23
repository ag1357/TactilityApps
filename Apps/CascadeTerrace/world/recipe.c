#include "sdk.h"
#include <string.h>
static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int32_t i32(const uint8_t* p) {
    uint32_t u = u32(p);
    return u <= INT32_MAX ? (int32_t)u : -1 - (int32_t)(~u);
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
void ws_materialize(const WsRecipe* r, uint16_t i, WsModule* out) {
    if (i >= r->count || i >= WS_CAP) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = r->modules[i];
    /* Correlated placement is an explicit recipe parameter; no entity names. */
    int32_t j = (int32_t)(ws_hash(r->seed ^ out->seed) % 5) - 2;
    out->pos.x += j * out->jitter_x;
    out->pos.z += j * out->jitter_z;
}
int ws_route(const WsRecipe* r, uint16_t a, uint16_t b, const uint8_t* blocked, uint16_t* path, size_t cap) {
    uint16_t queue[WS_CAP], prev[WS_CAP];
    size_t head = 0, tail = 0, n = 0;
    if (a >= r->count || b >= r->count || r->count > WS_CAP || r->link_count > WS_LINK_CAP) return 0;
    if (blocked && (blocked[a] || blocked[b])) return 0;
    for (int i = 0; i < WS_CAP; i++) prev[i] = UINT16_MAX;
    queue[tail++] = a;
    prev[a] = a;
    while (head < tail) {
        uint16_t at = queue[head++];
        if (at == b) break;
        for (int i = 0; i < r->link_count; i++) {
            const WsLink* l = &r->links[i];
            uint16_t next = l->a == at ? l->b : l->b == at ? l->a
                                                           : UINT16_MAX;
            if (next >= r->count || prev[next] != UINT16_MAX || (blocked && blocked[next])) continue;
            prev[next] = at;
            queue[tail++] = next;
        }
    }
    if (prev[b] == UINT16_MAX) return 0;
    for (uint16_t at = b;; at = prev[at]) {
        queue[n++] = at;
        if (at == a) break;
    }
    if (n > cap) return -(int)n;
    for (size_t i = 0; i < n; i++) path[i] = queue[n - 1 - i];
    return (int)n;
}
/* Hop count over ordinary (walk/lift/portal) links only, gates excluded.
   A far end with no ordinary path at all is conventionally nonadjacent
   too, so the answer saturates instead of failing. */
static int ordinary_hops(const WsRecipe* r, uint16_t a, uint16_t b) {
    uint16_t queue[WS_CAP];
    int depth[WS_CAP];
    uint8_t seen[WS_CAP] = {0};
    size_t head = 0, tail = 0;
    queue[tail] = a;
    depth[a] = 0;
    seen[a] = 1;
    tail++;
    while (head < tail) {
        uint16_t at = queue[head++];
        for (int i = 0; i < r->link_count; i++) {
            const WsLink* l = &r->links[i];
            if (l->kind == WS_LINK_ANOMALY) continue;
            uint16_t next = l->a == at ? l->b : l->b == at ? l->a : UINT16_MAX;
            if (next >= r->count || seen[next]) continue;
            seen[next] = 1;
            depth[next] = depth[at] + 1;
            queue[tail++] = next;
        }
    }
    return seen[b] ? depth[b] : WS_LINK_CAP;
}
WsError ws_validate(const WsRecipe* r) {
    if (!r->count || r->count > WS_CAP || r->link_count > WS_LINK_CAP) return WS_BOUNDS;
    for (int i = 0; i < r->count; i++) {
        const WsModule* m = &r->modules[i];
        if (m->parent != UINT16_MAX && m->parent >= i) return WS_REFERENCE;
        if ((m->kind & 255) > WS_ROOM_MODULE || (m->kind >> 8) > WS_OBJECT || m->phos > 6 || (m->flags & ~127)) return WS_VERSION;
        if (m->size.x < 1 || m->size.y < 1 || m->size.z < 1 || m->size.x > 400000 || m->size.y > 400000 || m->size.z > 400000 || m->affinity > 1000) return WS_BOUNDS;
        if (m->pos.x < -1000000 || m->pos.x > 1000000 || m->pos.y < -1000000 || m->pos.y > 1000000 || m->pos.z < -1000000 || m->pos.z > 1000000) return WS_BOUNDS;
        for (int j = 0; j < i; j++)
            if (ws_id_equal(m->id, r->modules[j].id)) return WS_DUPLICATE;
    }
    for (int i = 0; i < r->link_count; i++) {
        const WsLink* l = &r->links[i];
        if (l->a >= r->count || l->b >= r->count || l->a == l->b) return WS_REFERENCE;
        if (l->kind > WS_LINK_ANOMALY || (l->kind != WS_LINK_ANOMALY && l->reserved)) return WS_VERSION;
        if (!(r->modules[l->a].flags & WS_WALK) || !(r->modules[l->b].flags & WS_WALK)) return WS_REFERENCE;
    }
    /* Sparse world-scale features: fail closed on unknown kinds, impossible
       geometry or misplaced exceptions. Rivers must fall monotonically
       within a bounded slope after subtracting typed drops. */
    if (r->feature_count > WS_FEATURE_CAP || r->exception_count > WS_EXCEPTION_CAP) return WS_BOUNDS;    for (int i = 0; i < r->feature_count; i++) {
        const WsFeature* f = &r->features[i];
        if (f->kind > WS_FEATURE_RIVER || f->flow > WS_FLOW_RAPID) return WS_VERSION;
        if (f->up.x < -1000000 || f->up.x > 1000000 || f->up.y < -1000000 || f->up.y > 1000000 || f->up.z < -1000000 || f->up.z > 1000000) return WS_BOUNDS;
        if (f->down.x < -1000000 || f->down.x > 1000000 || f->down.y < -1000000 || f->down.y > 1000000 || f->down.z < -1000000 || f->down.z > 1000000) return WS_BOUNDS;
        if (f->width < 200 || f->width > 32000 || f->depth < 50 || f->depth > 8000) return WS_BOUNDS;
        for (int j = 0; j < i; j++)
            if (ws_id_equal(f->id, r->features[j].id)) return WS_DUPLICATE;
        int64_t dx = (int64_t)f->down.x - f->up.x, dz = (int64_t)f->down.z - f->up.z;
        int64_t drop = (int64_t)f->up.y - f->down.y, steps = 0;
        if ((!dx && !dz) || drop < 1) return WS_BOUNDS;
        for (int j = 0; j < r->exception_count; j++) {
            const WsException* e = &r->exceptions[j];
            if (e->feature == i && (e->type == WS_EXC_WATERFALL || e->type == WS_EXC_DAM)) steps += e->aux;
        }
        if (steps > drop) return WS_BOUNDS;
        if ((drop - steps) * 4 > isqrt64(dx * dx + dz * dz)) return WS_BOUNDS;
    }
    for (int i = 0; i < r->exception_count; i++) {
        const WsException* e = &r->exceptions[i];
        if (e->feature >= r->feature_count) return WS_REFERENCE;
        if (e->type < WS_EXC_WATERFALL || e->type > WS_EXC_UNDERGROUND) return WS_VERSION;
        if ((uint32_t)e->at + e->length > 65535) return WS_BOUNDS;
        if (e->type == WS_EXC_WATERFALL || e->type == WS_EXC_DAM) {
            /* Point or wall drops stay strictly before the downstream end so
               the endpoint elevation is reachable; aux is the drop in mm. */
            if ((e->type == WS_EXC_WATERFALL && e->length) || e->aux < 1 || e->aux > 200000 || (uint32_t)e->at + e->length > 65534) return WS_BOUNDS;
        } else if (e->length < 1 || e->aux)
            return WS_BOUNDS;
    }
    /* Regional reservoirs: fail closed on unknown kinds, degenerate or
       out-of-bounds regions, impossible stocks or rates, duplicate identity
       and same-kind overlap (which would make region lookup ambiguous). */
    if (r->reservoir_count > WS_RESERVOIR_CAP) return WS_BOUNDS;
    for (int i = 0; i < r->reservoir_count; i++) {
        const WsReservoir* v = &r->reservoirs[i];
        if (v->kind < WS_RES_TERRAIN || v->kind > WS_RES_PHOS || v->zone > 3 || v->reserved) return WS_VERSION;
        if (v->lo.x < -1000000 || v->lo.x > 1000000 || v->lo.y < -1000000 || v->lo.y > 1000000 || v->lo.z < -1000000 || v->lo.z > 1000000) return WS_BOUNDS;
        if (v->hi.x < -1000000 || v->hi.x > 1000000 || v->hi.y < -1000000 || v->hi.y > 1000000 || v->hi.z < -1000000 || v->hi.z > 1000000) return WS_BOUNDS;
        if (v->lo.x >= v->hi.x || v->lo.y >= v->hi.y || v->lo.z >= v->hi.z) return WS_BOUNDS;
        if (!v->capacity || v->capacity > 400000 || v->level > v->capacity || !v->rate || v->rate > 65535) return WS_BOUNDS;
        for (int j = 0; j < i; j++)
            if (ws_id_equal(v->id, r->reservoirs[j].id)) return WS_DUPLICATE;
        for (int j = 0; j < i; j++) {
            const WsReservoir* o = &r->reservoirs[j];
            if (o->kind != v->kind) continue;
            if (v->lo.x < o->hi.x && o->lo.x < v->hi.x && v->lo.y < o->hi.y && o->lo.y < v->hi.y && v->lo.z < o->hi.z && o->lo.z < v->hi.z) return WS_BOUNDS;
        }
    }
    /* Nonlocal anomaly gates: typed topology edges anchored to a regional
       Phos lode. The gate seat (a) stands inside the anchored region, the
       far end (b) outside it, and the two ends must be conventionally
       nonadjacent (no ordinary path of fewer than three links), so a gate
       is a genuine shortcut between regions that keep their ordinary
       geography. One Phos region hosts at most one gate. Local walk
       realizability (ws_topology) never applies: gates are adjacency-graph
       topology, not local geometry. */
    {
        uint8_t anchored[WS_RESERVOIR_CAP] = {0};
        for (int i = 0; i < r->link_count; i++) {
            const WsLink* l = &r->links[i];
            if (l->kind != WS_LINK_ANOMALY) continue;
            if (l->reserved >= r->reservoir_count) return WS_REFERENCE;
            const WsReservoir* v = &r->reservoirs[l->reserved];
            if (v->kind != WS_RES_PHOS) return WS_VERSION;
            if (anchored[l->reserved]++) return WS_DUPLICATE;
            WsModule A, B;
            ws_materialize(r, l->a, &A);
            ws_materialize(r, l->b, &B);
            if (A.pos.x < v->lo.x || A.pos.x > v->hi.x || A.pos.z < v->lo.z || A.pos.z > v->hi.z) return WS_BOUNDS;
            if (B.pos.x >= v->lo.x && B.pos.x <= v->hi.x && B.pos.z >= v->lo.z && B.pos.z <= v->hi.z) return WS_BOUNDS;
            if (ordinary_hops(r, l->a, l->b) < 3) return WS_BOUNDS;
        }
    }
    int first = -1;
    uint16_t path[WS_CAP];
    for (int i = 0; i < r->count; i++)
        if (r->modules[i].flags & WS_WALK) {
            if (first < 0) first = i;
            else if (ws_route(r, (uint16_t)first, (uint16_t)i, 0, path, WS_CAP) <= 0)
                return WS_DISCONNECTED;
        }
    /* Sparse creature species (schema 4): fail closed on unknown kinds,
       out-of-range habitats, degenerate populations or schedules, duplicate
       identity and anchors that are not walk surfaces. A REQUIRED species
       must anchor on a settlement reachable by baseline public walking
       from the first declared walk surface: required NPCs are publicly
       reachable, never sealed behind a capability. FAUNA habitats must be
       declared biome reservoirs. */
    if (r->creature_count > WS_CREATURE_CAP) return WS_BOUNDS;
    for (int i = 0; i < r->creature_count; i++) {
        const WsCreature* c = &r->creatures[i];
        if (c->kind < WS_CRE_FAUNA || c->kind > WS_CRE_REQUIRED) return WS_VERSION;
        if (!c->slots || c->slots > WS_CRE_SLOTS_MAX || !c->stations || c->stations > 4) return WS_BOUNDS;
        if (c->period < 600 || c->period > 64800 || c->radius < 2000 || c->radius > 60000) return WS_BOUNDS;
        for (int j = 0; j < i; j++)
            if (ws_id_equal(c->id, r->creatures[j].id)) return WS_DUPLICATE;
        if (c->kind == WS_CRE_FAUNA) {
            if (c->habitat >= r->reservoir_count) return WS_REFERENCE;
        } else {
            if (c->habitat >= r->count || !(r->modules[c->habitat].flags & WS_WALK) || (r->modules[c->habitat].flags & WS_INTERIOR)) return WS_REFERENCE;
            if (c->kind == WS_CRE_REQUIRED && (first < 0 || ws_reachable(r, (uint16_t)first, c->habitat, WS_CAP_WALK) != WS_REACH_WALK)) return WS_REFERENCE;
        }
    }
    /* Declared walk edges must be realizable as continuous space. */
    return ws_topology(r);
}
WsError ws_load(WsRecipe* r, const uint8_t* p, size_t n) {
    /* Transactional: validate bytes before touching output; caller stages final validation. */
    if (n < 48 || memcmp(p, "CWS1", 4)) return WS_FORMAT;
    if (u16(p + 6) != WS_GENERATOR) return WS_VERSION;
    unsigned schema = u16(p + 4), count, links, features = 0, exceptions = 0, reservoirs = 0, creatures = 0;
    size_t body = 48;
    if (schema == WS_SCHEMA) {
        count = u16(p + 44);
        links = u16(p + 46);
        if (n != 48 + 64 * count + 8 * links || u32(p + 8) != n) return WS_BOUNDS;
    } else if (schema == WS_SCHEMA_FEATURES) {
        if (n < 52) return WS_FORMAT;
        count = u16(p + 44);
        links = u16(p + 46);
        features = u16(p + 48);
        exceptions = u16(p + 50);
        body = 52;
        if (n != 52 + 64 * count + 8 * links + 56 * features + 12 * exceptions || u32(p + 8) != n) return WS_BOUNDS;
    } else if (schema == WS_SCHEMA_RESOURCES) {
        if (n < 54) return WS_FORMAT;
        count = u16(p + 44);
        links = u16(p + 46);
        features = u16(p + 48);
        exceptions = u16(p + 50);
        reservoirs = u16(p + 52);
        body = 54;
        if (n != 54 + 64 * count + 8 * links + 56 * features + 12 * exceptions + 64 * reservoirs || u32(p + 8) != n) return WS_BOUNDS;
    } else if (schema == WS_SCHEMA_CREATURES) {
        if (n < 56) return WS_FORMAT;
        count = u16(p + 44);
        links = u16(p + 46);
        features = u16(p + 48);
        exceptions = u16(p + 50);
        reservoirs = u16(p + 52);
        creatures = u16(p + 54);
        body = 56;
        if (n != 56 + 64 * count + 8 * links + 56 * features + 12 * exceptions + 64 * reservoirs + 32 * creatures || u32(p + 8) != n) return WS_BOUNDS;
    } else
        return WS_VERSION;
    if (!count || count > WS_CAP || links > WS_LINK_CAP || features > WS_FEATURE_CAP || exceptions > WS_EXCEPTION_CAP || reservoirs > WS_RESERVOIR_CAP || creatures > WS_CREATURE_CAP) return WS_BOUNDS;
    if (ws_crc(p + 16, n - 16) != u32(p + 12)) return WS_FORMAT;
    memset(r, 0, sizeof(*r));
    for (int i = 0; i < 4; i++) r->ancestry.word[i] = u32(p + 16 + 4 * i);
    r->seed = u32(p + 32);
    r->epoch = u32(p + 36);
    r->revision = u32(p + 40);
    r->recipe_crc = u32(p + 12);
    r->count = (uint16_t)count;
    r->link_count = (uint16_t)links;
    r->feature_count = (uint16_t)features;
    r->exception_count = (uint16_t)exceptions;
    r->reservoir_count = (uint16_t)reservoirs;
    r->creature_count = (uint16_t)creatures;
    p += body;
    for (unsigned i = 0; i < count; i++, p += 64) {
        WsModule* m = &r->modules[i];
        for (int j = 0; j < 4; j++) m->id.word[j] = u32(p + j * 4);
        m->parent = u16(p + 16);
        m->kind = u16(p + 18);
        m->flags = u16(p + 20);
        m->phos = u16(p + 22);
        m->pos = (WsPos) {i32(p + 24), i32(p + 28), i32(p + 32)};
        m->size = (WsPos) {i32(p + 36), i32(p + 40), i32(p + 44)};
        m->color = u32(p + 48);
        m->seed = u32(p + 52);
        m->jitter_x = (int16_t)u16(p + 56);
        m->jitter_z = (int16_t)u16(p + 58);
        m->affinity = u16(p + 60);
        m->quantity = u16(p + 62);
    }
    for (unsigned i = 0; i < links; i++, p += 8) r->links[i] = (WsLink) {u16(p), u16(p + 2), u16(p + 4), u16(p + 6)};
    if (schema >= WS_SCHEMA_FEATURES) {
        for (unsigned i = 0; i < features; i++, p += 56) {
            WsFeature* f = &r->features[i];
            for (int j = 0; j < 4; j++) f->id.word[j] = u32(p + j * 4);
            f->kind = u16(p + 16);
            f->flow = u16(p + 18);
            f->up = (WsPos) {i32(p + 20), i32(p + 24), i32(p + 28)};
            f->down = (WsPos) {i32(p + 32), i32(p + 36), i32(p + 40)};
            f->width = u16(p + 44);
            f->depth = u16(p + 46);
            f->seed = u32(p + 48);
            f->reserved = u32(p + 52);
        }
        for (unsigned i = 0; i < exceptions; i++, p += 12) r->exceptions[i] = (WsException) {u16(p), u16(p + 2), u16(p + 4), u16(p + 6), u32(p + 8)};
    }
    if (schema >= WS_SCHEMA_RESOURCES) {
        for (unsigned i = 0; i < reservoirs; i++, p += 64) {
            WsReservoir* v = &r->reservoirs[i];
            for (int j = 0; j < 4; j++) v->id.word[j] = u32(p + j * 4);
            v->kind = u16(p + 16);
            v->zone = u16(p + 18);
            v->lo = (WsPos) {i32(p + 20), i32(p + 24), i32(p + 28)};
            v->hi = (WsPos) {i32(p + 32), i32(p + 36), i32(p + 40)};
            v->capacity = u32(p + 44);
            v->level = u32(p + 48);
            v->rate = u32(p + 52);
            v->reserved = u32(p + 56);
        }
    }
    if (schema == WS_SCHEMA_CREATURES) {
        for (unsigned i = 0; i < creatures; i++, p += 32) {
            WsCreature* c = &r->creatures[i];
            for (int j = 0; j < 4; j++) c->id.word[j] = u32(p + j * 4);
            c->kind = u16(p + 16);
            c->habitat = u16(p + 18);
            c->slots = u16(p + 20);
            c->period = u16(p + 22);
            c->stations = u16(p + 24);
            c->radius = u16(p + 26);
            c->seed = u32(p + 28);
        }
    }
    WsError e = ws_validate(r);
    if (e != WS_OK) memset(r, 0, sizeof(*r));
    return e;
}
WsFidelity ws_fidelity(WsPos a, WsPos b, int interior) {
    int64_t x = (int64_t)a.x - b.x, y = (int64_t)a.y - b.y, z = (int64_t)a.z - b.z;
    if (x > 180000 || x < -180000 || y > 180000 || y < -180000 || z > 180000 || z < -180000) return WS_UNLOADED;
    uint64_t d = (uint64_t)(x * x + y * y + z * z);
    if (d < 25000ULL * 25000) return WS_ACTIVE;
    if (interior) return WS_UNLOADED;
    if (d < 65000ULL * 65000) return WS_MATERIALIZED;
    return d < 180000ULL * 180000 ? WS_PROXY : WS_UNLOADED;
}
static int chord_cross(WsPos a, WsPos b, WsPos c, WsPos d, int64_t* q16) {
    int64_t d1x = b.x - a.x, d1z = b.z - a.z, d2x = d.x - c.x, d2z = d.z - c.z;
    int64_t den = d1x * d2z - d1z * d2x;
    if (!den) return 0;
    int64_t ax = c.x - a.x, az = c.z - a.z;
    int64_t q = (ax * d2z - az * d2x) * 65536 / den;
    int64_t u = (ax * d1z - az * d1x) * 65536 / den;
    if (q < 0 || q > 65536 || u < 0 || u > 65536) return 0;
    *q16 = q;
    return 1;
}
static int river_crossings(const WsRecipe* r, uint16_t fi, WsPos a, WsPos b) {
    /* The feature polyline is the same 8 reconstructed segments the renderer
       draws, so fording cost matches visible water. Distinct crossings are
       deduplicated so touching a meander node does not double-ford. */
    WsRiverSample s[2];
    int64_t qs[8];
    int count = 0;
    for (int k = 0; k < 8; k++) {
        uint16_t ta = (uint16_t)(k * 8192), tb = (uint16_t)(k == 7 ? 65535 : (k + 1) * 8192);
        if (!ws_river_sample(r, fi, ta, &s[0]) || !ws_river_sample(r, fi, tb, &s[1])) continue;
        int64_t q;
        if (!chord_cross(a, b, s[0].pos, s[1].pos, &q)) continue;
        int i = count++;
        while (i > 0 && qs[i - 1] > q) {
            qs[i] = qs[i - 1];
            i--;
        }
        qs[i] = q;
    }
    int crossings = 0;
    int64_t last = INT64_MIN;
    for (int i = 0; i < count; i++)
        if (last == INT64_MIN || qs[i] - last > 4096) {
            crossings++;
            last = qs[i];
        }
    return crossings;
}
uint64_t ws_link_cost(const WsRecipe* r, uint16_t i, uint32_t caps) {
    if (i >= r->link_count) return UINT64_MAX;
    const WsLink* l = &r->links[i];
    if (l->kind == WS_LINK_LIFT && !(caps & WS_CAP_LIFT)) return UINT64_MAX;
    if (l->kind == WS_LINK_PORTAL && !(caps & WS_CAP_PORTAL)) return UINT64_MAX;
    if (l->kind == WS_LINK_ANOMALY) {
        if (!(caps & WS_CAP_ANOMALY)) return UINT64_MAX;
        return WS_ANOMALY_COST; /* fixed gate toll; openness is canonical state */
    }
    WsModule a, b;
    ws_materialize(r, l->a, &a);
    ws_materialize(r, l->b, &b);
    int64_t dx = (int64_t)b.pos.x - a.pos.x, dy = (int64_t)b.pos.y - a.pos.y, dz = (int64_t)b.pos.z - a.pos.z;
    int64_t climb = dy < 0 ? -dy : dy;
    if (l->kind == WS_LINK_LIFT) return 500 + (uint64_t)climb / 2;
    if (l->kind == WS_LINK_PORTAL) return 2000;
    uint64_t cost = (uint64_t)isqrt64(dx * dx + dz * dz) + 8 * (uint64_t)climb;
    for (uint16_t f = 0; f < r->feature_count; f++) cost += 20000 * (uint64_t)river_crossings(r, f, a.pos, b.pos);
    return cost;
}
int ws_route_cost(const WsRecipe* r, uint16_t a, uint16_t b, uint32_t caps, uint64_t* cost, uint16_t* path, size_t cap) {
    /* Deterministic Dijkstra with lowest-index tie-breaking over declared
       topology; a terrain-aware navigation query, not a transport system. */
    uint64_t dist[WS_CAP];
    uint16_t prev[WS_CAP];
    uint8_t done[WS_CAP];
    if (a >= r->count || b >= r->count || r->count > WS_CAP || r->link_count > WS_LINK_CAP) return 0;
    for (int i = 0; i < WS_CAP; i++) {
        dist[i] = UINT64_MAX;
        prev[i] = UINT16_MAX;
        done[i] = 0;
    }
    dist[a] = 0;
    for (;;) {
        int u = -1;
        for (int i = 0; i < r->count; i++)
            if (!done[i] && dist[i] < UINT64_MAX && (u < 0 || dist[i] < dist[u])) u = i;
        if (u < 0) break;
        done[u] = 1;
        for (uint16_t i = 0; i < r->link_count; i++) {
            const WsLink* l = &r->links[i];
            uint16_t v;
            if (l->a == (uint16_t)u && !done[l->b]) v = l->b;
            else if (l->b == (uint16_t)u && !done[l->a]) v = l->a;
            else continue;
            uint64_t c = ws_link_cost(r, i, caps);
            if (c == UINT64_MAX) continue;
            if (dist[u] + c < dist[v]) {
                dist[v] = dist[u] + c;
                prev[v] = (uint16_t)u;
            }
        }
    }
    if (dist[b] == UINT64_MAX) return 0;
    uint16_t queue[WS_CAP];
    size_t n = 0;
    for (uint16_t at = b;; at = prev[at]) {
        queue[n++] = at;
        if (at == a) break;
    }
    if (n > cap) return -(int)n;
    for (size_t i = 0; i < n; i++) path[i] = queue[n - 1 - i];
    if (cost) *cost = dist[b];
    return (int)n;
}
