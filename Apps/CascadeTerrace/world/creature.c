#include "state.h"
#include <limits.h>
#include <string.h>

/* Sparse deterministic creature placement and schedules (Gate 5). Nothing
   here simulates: every answer is a pure function of (recipe, optional
   state exceptions, query time), so chunk unload/reload, time skips and
   repeated queries are all the same computation. The product stores
   bounded species records; entities materialize only inside the queried
   window, and active memory is bounded by the caps, not by world size.

   Placement rules (mirrored bit for bit in the Python SDK):
   - FAUNA slots live inside the biome reservoir extent. A candidate sits
     on the terrain surface: the top of the highest solid box under the
     candidate, or the region floor where no solid box contains it. Room
     interiors are excluded so wildlife stays exterior.
   - NPC/REQUIRED slots live on their anchor's walk surface, inside its
     extent with a margin, outside every solid box and room span. The
     anchor center is the deterministic fallback when no candidate fits.
   Stations:
   - NPC/REQUIRED stations are the anchor's walk-reachable neighbours: BFS
     over walk links only, at most two hops, lowest (hop, index) first.
   - FAUNA stations are further biome points, kept within the species
     radius of home so a population stays local.
   Schedules:
   - A slot's cycle is its species period with a per-slot phase offset. In
     cycle weights, each dwell is WS_CRE_DWELL and each travel is the route
     cost (terrain-aware, walk links) or chord length divided by
     WS_CRE_SPEED, so the schedule is resolved from time and the access
     graph together. Travel follows the declared route path: cross-chunk
     movement is existing topology, never new geometry.
   All arithmetic is integer; divisions truncate toward zero exactly like
   the Python mirror's c_div. */

enum { WS_CRE_DWELL = 64, WS_CRE_SPEED = 4096, WS_CRE_TRIES = 16, WS_CRE_HOPS = 2 };

WsId ws_creature_id(const WsRecipe* r, uint16_t species, uint16_t slot) {
    if (!r || species >= r->creature_count || species >= WS_CREATURE_CAP) {
        WsId none = {{0, 0, 0, 0}};
        return none;
    }
    return ws_child_id(r->creatures[species].id, (uint32_t)slot + 1, 0x4352u);
}

/* ---- shared placement vocabulary (mirrored) ---- */

static int shape_of(const WsModule* m) { return m->kind & 255; }

/* Is the point strictly inside a solid box, or inside a room's span?
   Solid boxes occupy [pos.y - size.y, pos.y] over [pos +/- size/2]; room
   walls rise from [pos.y - WS_FLOOR_MM, pos.y + size.y] over the outer
   extent. Standing exactly on a box top is free. */
static int blocked(const WsRecipe* r, WsPos p) {
    for (uint16_t i = 0; i < r->count; i++) {
        const WsModule* s = &r->modules[i];
        int shape = shape_of(s);
        if (shape != WS_BOX && shape != WS_ROOM_MODULE) continue;
        int solid_box = (s->flags & WS_SOLID) && shape == WS_BOX;
        if (!solid_box && shape != WS_ROOM_MODULE) continue;
        WsModule m;
        ws_materialize(r, i, &m);
        if (shape == WS_BOX) {
            if ((int64_t)p.x > m.pos.x - m.size.x / 2 && (int64_t)p.x < m.pos.x + m.size.x / 2 &&
                (int64_t)p.z > m.pos.z - m.size.z / 2 && (int64_t)p.z < m.pos.z + m.size.z / 2 &&
                (int64_t)p.y > m.pos.y - m.size.y && (int64_t)p.y < m.pos.y)
                return 1;
        } else if ((int64_t)p.x > m.pos.x - m.size.x / 2 && (int64_t)p.x < m.pos.x + m.size.x / 2 &&
                   (int64_t)p.z > m.pos.z - m.size.z / 2 && (int64_t)p.z < m.pos.z + m.size.z / 2 &&
                   (int64_t)p.y > m.pos.y - WS_FLOOR_MM && (int64_t)p.y < m.pos.y + m.size.y)
            return 1;
    }
    return 0;
}

/* Top of the highest solid box containing (x, z), or floor_y when none:
   the terrain surface under a biome point. */
static int32_t terrain_top(const WsRecipe* r, int32_t x, int32_t z, int32_t floor_y) {
    int32_t top = INT32_MIN;
    for (uint16_t i = 0; i < r->count; i++) {
        const WsModule* s = &r->modules[i];
        if (!((s->flags & WS_SOLID) && shape_of(s) == WS_BOX)) continue;
        WsModule m;
        ws_materialize(r, i, &m);
        if ((int64_t)x < m.pos.x - m.size.x / 2 || (int64_t)x > m.pos.x + m.size.x / 2) continue;
        if ((int64_t)z < m.pos.z - m.size.z / 2 || (int64_t)z > m.pos.z + m.size.z / 2) continue;
        if (m.pos.y > top) top = m.pos.y;
    }
    return top == INT32_MIN ? floor_y : top;
}

static int32_t spread(uint32_t salt, uint32_t key, uint32_t radius) {
    return (int32_t)(ws_hash(salt ^ key) % (2 * radius + 1)) - (int32_t)radius;
}

static int64_t chord3(WsPos a, WsPos b) {
    int64_t dx = (int64_t)b.x - a.x, dy = (int64_t)b.y - a.y, dz = (int64_t)b.z - a.z;
    int64_t n = dx * dx + dy * dy + dz * dz;
    if (n <= 0) return 0;
    int64_t x = n, y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* Position at distance d along the polyline, 16.16 interpolation per
   component with truncation toward zero (mirrored by c_div). */
static WsPos point_at(const WsPos* pts, int n, uint64_t d) {
    for (int i = 0; i + 1 < n; i++) {
        int64_t lc = chord3(pts[i], pts[i + 1]);
        if (lc <= 0) continue;
        if (d < (uint64_t)lc) {
            int64_t t = (int64_t)(d * 65536 / (uint64_t)lc);
            WsPos p;
            p.x = pts[i].x + (int32_t)(((int64_t)pts[i + 1].x - pts[i].x) * t / 65536);
            p.y = pts[i].y + (int32_t)(((int64_t)pts[i + 1].y - pts[i].y) * t / 65536);
            p.z = pts[i].z + (int32_t)(((int64_t)pts[i + 1].z - pts[i].z) * t / 65536);
            return p;
        }
        d -= (uint64_t)lc;
    }
    return pts[n - 1];
}

/* ---- per-slot derivation ---- */

typedef struct {
    WsPos st[4];      /* station points; st[0] is the placed home */
    uint16_t mod[4];  /* station modules (anchored kinds); fauna uses none */
    int n;            /* station count 1..4 */
    int anchored;     /* anchored (NPC/REQUIRED/relocated) vs biome fauna */
} Slot;

static void derive_home(const WsRecipe* r, const WsCreature* c, uint16_t slot, int anchored, uint16_t anchor, WsPos* home) {
    uint32_t s1 = ws_hash(c->seed ^ ((uint32_t)slot + 1) * 0x9E3779B9u);
    if (anchored) {
        WsModule a;
        ws_materialize(r, anchor, &a);
        int64_t hx = a.size.x / 2 - 600, hz = a.size.z / 2 - 600;
        for (int i = 0; i < WS_CRE_TRIES; i++) {
            WsPos p = {a.pos.x + spread(s1, 0xA1u + (uint32_t)i * 0x85EBCA6Bu, c->radius),
                       a.pos.y,
                       a.pos.z + spread(s1, 0xC3u + (uint32_t)i * 0x2545F491u, c->radius)};
            if ((int64_t)p.x - a.pos.x > hx || (int64_t)a.pos.x - p.x > hx) continue;
            if ((int64_t)p.z - a.pos.z > hz || (int64_t)a.pos.z - p.z > hz) continue;
            if (blocked(r, p)) continue;
            *home = p;
            return;
        }
        *home = a.pos;
        return;
    }
    const WsReservoir* v = &r->reservoirs[c->habitat];
    uint64_t sx = (uint64_t)(v->hi.x - v->lo.x), sz = (uint64_t)(v->hi.z - v->lo.z);
    for (int i = 0; i < WS_CRE_TRIES; i++) {
        uint32_t fx = ws_hash(s1 ^ (0x57u + (uint32_t)i * 0x85EBCA6Bu));
        uint32_t fz = ws_hash(s1 ^ (0x2Bu + (uint32_t)i * 0x2545F491u));
        int32_t x = v->lo.x + (int32_t)(sx * fx >> 32);
        int32_t z = v->lo.z + (int32_t)(sz * fz >> 32);
        WsPos p = {x, terrain_top(r, x, z, v->lo.y), z};
        if (p.y > v->hi.y) continue;
        if (blocked(r, p)) continue;
        *home = p;
        return;
    }
    int32_t x = v->lo.x + (int32_t)(sx / 2), z = v->lo.z + (int32_t)(sz / 2);
    *home = (WsPos) {x, terrain_top(r, x, z, v->lo.y), z};
}

static void derive_slot(const WsRecipe* r, const WsCreature* c, uint16_t slot, int anchored, uint16_t anchor, Slot* out) {
    derive_home(r, c, slot, anchored, anchor, &out->st[0]);
    out->mod[0] = anchored ? anchor : UINT16_MAX;
    out->anchored = anchored;
    out->n = 1;
    int want = c->stations < 1 ? 1 : c->stations;
    if (anchored) {
        /* Stations from the access graph: the anchor's walk neighbours, BFS
           over walk links only, at most WS_CRE_HOPS hops, lowest (hop,
           index) first. Only reachable surfaces become stations, so an
           anchored schedule never leaves declared topology. */
        uint16_t queue[WS_CAP];
        int depth[WS_CAP];
        uint8_t seen[WS_CAP] = {0};
        size_t head = 0, tail = 0;
        queue[tail] = (uint16_t)anchor;
        depth[anchor] = 0;
        seen[anchor] = 1;
        tail++;
        while (head < tail && depth[queue[head]] < WS_CRE_HOPS) {
            uint16_t at = queue[head++];
            for (uint16_t i = 0; i < r->link_count; i++) {
                const WsLink* l = &r->links[i];
                if (l->kind != WS_LINK_WALK) continue;
                uint16_t next = l->a == at ? l->b : l->b == at ? l->a : UINT16_MAX;
                if (next >= r->count || seen[next]) continue;
                seen[next] = 1;
                depth[next] = depth[at] + 1;
                queue[tail++] = next;
            }
        }
        for (int d = 1; d <= WS_CRE_HOPS && out->n < want; d++)
            for (uint16_t i = 0; i < r->count && out->n < want; i++) {
                if (!seen[i] || depth[i] != d) continue;
                if (!(r->modules[i].flags & WS_WALK)) continue;
                WsModule m;
                ws_materialize(r, i, &m);
                out->st[out->n] = m.pos;
                out->mod[out->n] = i;
                out->n++;
            }
    } else {
        /* Biome stations: further deterministic points in the same extent,
           kept within the species radius of home so a population stays
           local; the fallback is home itself. */
        const WsReservoir* v = &r->reservoirs[c->habitat];
        uint64_t sx = (uint64_t)(v->hi.x - v->lo.x), sz = (uint64_t)(v->hi.z - v->lo.z);
        uint32_t s2 = ws_hash(c->seed ^ ((uint32_t)slot + 1) * 0x68E31DA4u);
        for (int j = 1; j < want; j++) {
            uint32_t fx = ws_hash(s2 ^ (0xE7u + (uint32_t)j * 0x85EBCA6Bu));
            uint32_t fz = ws_hash(s2 ^ (0x2Du + (uint32_t)j * 0x2545F491u));
            int32_t x = v->lo.x + (int32_t)(sx * fx >> 32);
            int32_t z = v->lo.z + (int32_t)(sz * fz >> 32);
            WsPos p = {x, terrain_top(r, x, z, v->lo.y), z};
            int64_t dx = (int64_t)p.x - out->st[0].x, dz = (int64_t)p.z - out->st[0].z;
            if (p.y > v->hi.y || dx * dx + dz * dz > (int64_t)c->radius * c->radius || blocked(r, p)) p = out->st[0];
            out->st[j] = p;
            out->mod[j] = UINT16_MAX;
            out->n++;
        }
    }
}

/* Resolve one creature at now_s. Returns 0 when the creature does not
   exist (out of range or a DEAD exception). */
int ws_creature_at(const WsRecipe* r, const WsState* s, uint16_t species, uint16_t slot, uint32_t now_s, WsCreatureSample* out) {
    if (!r || species >= r->creature_count || species >= WS_CREATURE_CAP || slot >= WS_CRE_SLOTS_MAX || slot >= r->creatures[species].slots) return 0;
    const WsCreature* c = &r->creatures[species];
    WsId id = ws_creature_id(r, species, slot);
    int anchored = c->kind != WS_CRE_FAUNA;
    uint16_t anchor = c->habitat;
    if (s) {
        for (int i = 0; i < s->creature_count && i < WS_CREX_CAP; i++) {
            const WsCreatureEx* e = &s->crex[i];
            if (!ws_id_equal(e->id, id)) continue;
            if (e->kind == WS_CREX_DEAD) return 0;
            if (e->kind == WS_CREX_RELOCATED) {
                if (e->aux >= r->count) return 0;
                anchor = e->aux;
                anchored = 1;
            } else if (e->kind == WS_CREX_PINNED) {
                Slot slotv;
                derive_slot(r, c, slot, anchored, anchor, &slotv);
                *out = (WsCreatureSample) {id, slotv.st[0], species, slot, 0, 0};
                return 1;
            }
            break;
        }
    }
    Slot slotv;
    derive_slot(r, c, slot, anchored, anchor, &slotv);
    uint32_t period = c->period ? c->period : 1;
    uint32_t s1 = ws_hash(c->seed ^ ((uint32_t)slot + 1) * 0x9E3779B9u);
    uint32_t u = (now_s + ws_hash(s1 ^ 0x5Bu) % period) % period;
    if (slotv.n <= 1) {
        *out = (WsCreatureSample) {id, slotv.st[0], species, slot, 0, 0};
        return 1;
    }
    /* Cycle weights: dwell is fixed, travel scales with the route cost (or
       chord length) so the access graph shapes the schedule. */
    uint32_t w[4];
    for (int j = 0; j < slotv.n; j++) {
        int next = j + 1 == slotv.n ? 0 : j + 1;
        if (slotv.anchored) {
            uint64_t cost;
            uint16_t path[WS_CAP];
            int n = ws_route_cost(r, slotv.mod[j], slotv.mod[next], WS_CAP_WALK, &cost, path, WS_CAP);
            if (n > 0)
                w[j] = cost / WS_CRE_SPEED + 1;
            else
                w[j] = (uint32_t)(chord3(slotv.st[j], slotv.st[next]) / WS_CRE_SPEED) + 1;
        } else
            w[j] = (uint32_t)(chord3(slotv.st[j], slotv.st[next]) / WS_CRE_SPEED) + 1;
    }
    uint64_t total = 0;
    for (int j = 0; j < slotv.n; j++) total += WS_CRE_DWELL + w[j];
    uint64_t p = (uint64_t)u * total / period;
    uint64_t cum = 0;
    for (int j = 0; j < slotv.n; j++) {
        int next = j + 1 == slotv.n ? 0 : j + 1;
        if (p < cum + WS_CRE_DWELL) {
            *out = (WsCreatureSample) {id, slotv.st[j], species, slot, (uint16_t)j, 0};
            return 1;
        }
        cum += WS_CRE_DWELL;
        if (p < cum + w[j]) {
            /* Interpolation runs along the actual travel polyline: the
               declared route path for anchored kinds, the chord for biome
               fauna. d is the distance walked into this segment. */
            WsPos line[WS_CAP + 1];
            int n = 2;
            line[0] = slotv.st[j];
            line[1] = slotv.st[next];
            if (slotv.anchored) {
                uint16_t path[WS_CAP];
                int m = ws_route_cost(r, slotv.mod[j], slotv.mod[next], WS_CAP_WALK, 0, path, WS_CAP);
                if (m > 0) {
                    n = m;
                    for (int k = 0; k < m; k++) {
                        WsModule mod;
                        ws_materialize(r, path[k], &mod);
                        line[k] = mod.pos;
                    }
                }
            }
            int64_t len = 0;
            for (int k = 0; k + 1 < n; k++) len += chord3(line[k], line[k + 1]);
            uint64_t d = len > 0 ? (p - cum) * (uint64_t)len / w[j] : 0;
            *out = (WsCreatureSample) {id, point_at(line, n, d), species, slot, (uint16_t)j, 1};
            return 1;
        }
        cum += w[j];
    }
    *out = (WsCreatureSample) {id, slotv.st[0], species, slot, 0, 0};
    return 1;
}

int ws_creature_query(const WsRecipe* r, const WsState* s, WsPos lo, WsPos hi, uint32_t now_s, WsCreatureSample* out, int cap) {
    if (!r || cap < 0) return 0;
    int matches = 0;
    for (uint16_t sp = 0; sp < r->creature_count && sp < WS_CREATURE_CAP; sp++) {
        for (uint16_t k = 0; k < r->creatures[sp].slots && k < WS_CRE_SLOTS_MAX; k++) {
            WsCreatureSample one;
            if (!ws_creature_at(r, s, sp, k, now_s, &one)) continue;
            if (one.pos.x < lo.x || one.pos.x > hi.x || one.pos.y < lo.y || one.pos.y > hi.y || one.pos.z < lo.z || one.pos.z > hi.z) continue;
            matches++;
            if (matches <= cap) out[matches - 1] = one;
        }
    }
    return matches <= cap ? matches : -matches;
}

int ws_creature_known(const WsRecipe* r, WsId id) {
    if (!r) return 0;
    for (uint16_t sp = 0; sp < r->creature_count && sp < WS_CREATURE_CAP; sp++)
        for (uint16_t k = 0; k < r->creatures[sp].slots && k < WS_CRE_SLOTS_MAX; k++)
            if (ws_id_equal(ws_creature_id(r, sp, k), id)) return 1;
    return 0;
}

WsError ws_creature_except(WsState* s, const WsRecipe* r, WsId id, uint16_t kind, uint16_t aux) {
    if (!s || !r) return WS_BOUNDS;
    if (!ws_creature_known(r, id)) return WS_REFERENCE;
    if (kind > WS_CREX_PINNED) return WS_BOUNDS;
    if (kind == WS_CREX_DEAD && aux) return WS_BOUNDS;
    if (kind == WS_CREX_RELOCATED && (aux >= r->count || !(r->modules[aux].flags & WS_WALK) || (r->modules[aux].flags & WS_INTERIOR))) return WS_REFERENCE;
    if (kind == WS_CREX_PINNED && aux >= s->player_count) return WS_BOUNDS;
    int at = -1;
    for (int i = 0; i < s->creature_count; i++)
        if (ws_id_equal(s->crex[i].id, id)) {
            at = i;
            break;
        }
    if (!kind) {
        if (at < 0) return WS_OK; /* removing an absent exception is a no-op */
        memmove(s->crex + at, s->crex + at + 1, sizeof(*s->crex) * (size_t)(s->creature_count - at - 1));
        s->creature_count--;
        return WS_OK;
    }
    if (at < 0) {
        if (s->creature_count >= WS_CREX_CAP) return WS_FULL;
        at = s->creature_count++;
    }
    s->crex[at] = (WsCreatureEx) {id, kind, aux, 0, 0};
    return WS_OK;
}
