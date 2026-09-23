#include "state.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/* Regional resource and ecology layer. Everything here is bookkeeping over
   bounded regional stocks: there is no fluid simulation, no grid and no
   per-voxel state. Drawdown changes what river samples report inside a
   water region (ws_river_stage) and what players carry; nothing else moves.

   Conservation identity (checked by ws_ledger_check after every apply):
     sum(level) + sum(material) + sum(shards) + used + lost
       == sum(recipe initial levels) + recovered
   Sites are records of voids, not matter stocks, so they never appear in
   the identity. All arithmetic is integer and deterministic; the Python
   SDK mirrors it bit for bit. */

/* Recovery multiplier per reservoir kind (terrain, water, biomass, Phos)
   and weather (clear, rain, storm). Rain and storms refill catchments;
   storms erode standing biomass; weather never touches solid terrain. */
static const uint32_t WS_WEATHER_MULT[4][3] = {
    {1, 1, 1},
    {1, 2, 3},
    {1, 2, 1},
    {1, 1, 2},
};

uint32_t ws_weather(const WsRecipe* r, uint32_t day) {
    return ws_hash(r->seed ^ (day * 0x9E3779B9u + 0x85EBCA6Bu)) % 3;
}

int ws_river_stage(const WsRecipe* r, const WsState* s, uint16_t feature, uint16_t t, WsRiverSample* out) {
    /* Declared geometry first; a state that is not bound to this recipe (or
       an empty state) reports exactly the Gate 2 reconstruction. */
    if (!ws_river_sample(r, feature, t, out)) return 0;
    if (!s || !s->reservoir_count || s->reservoir_count != r->reservoir_count) return 1;
    for (int i = 0; i < s->reservoir_count; i++) {
        const WsReservoir* v = &r->reservoirs[i];
        if (v->kind != WS_RES_WATER) continue;
        /* Regional water field: 2D footprint containment, first (and only,
           since same-kind overlap is rejected at load) matching region. */
        if (out->pos.x < v->lo.x || out->pos.x > v->hi.x || out->pos.z < v->lo.z || out->pos.z > v->hi.z) continue;
        uint32_t lvl = s->level[i] <= v->capacity ? s->level[i] : v->capacity;
        if (!lvl) {
            out->surfaced = 0; /* dry bed: the reach renders as ground */
            return 1;
        }
        uint64_t rho = (uint64_t)lvl * 65536 / v->capacity;
        uint64_t w = (uint64_t)out->width * rho / 65536;
        uint64_t d = (uint64_t)out->depth * rho / 65536;
        out->width = w ? (uint16_t)w : 1;
        out->depth = d ? (uint16_t)d : 1;
        if (rho < 16384) {
            /* Below a quarter, lake and wetland reaches dry back to marsh:
               the widened strip vanishes before the whole river does. */
            for (int j = 0; j < r->exception_count; j++) {
                const WsException* e = &r->exceptions[j];
                if (e->feature == feature && e->type == WS_EXC_LAKE && (uint32_t)t >= e->at && (uint32_t)t <= (uint32_t)e->at + e->length) {
                    out->surfaced = 0;
                    break;
                }
            }
        }
        return 1;
    }
    return 1;
}

void ws_resources_tick(WsState* s, const WsRecipe* r, uint32_t delta_s) {
    if (!delta_s || !s->reservoir_count || s->reservoir_count != r->reservoir_count) return;
    s->clock_s += delta_s;
    uint32_t day = s->clock_s / 86400;
    uint32_t weather = ws_weather(r, day);
    for (int i = 0; i < s->reservoir_count; i++) {
        const WsReservoir* v = &r->reservoirs[i];
        if (s->level[i] >= v->capacity) continue; /* full: inflow is runoff */
        uint64_t mult = WS_WEATHER_MULT[v->kind - 1][weather] * (1 + v->zone);
        if (v->kind == WS_RES_BIOMASS) {
            /* Biomass recovery depends on regional available Phos: the
               lowest-index overlapping Phos reservoir doubles the rate at
               full stock. Overlap is by 2D footprint, like stage lookup. */
            for (int j = 0; j < r->reservoir_count; j++) {
                const WsReservoir* o = &r->reservoirs[j];
                if (o->kind != WS_RES_PHOS) continue;
                if (v->lo.x >= o->hi.x || o->lo.x >= v->hi.x || v->lo.z >= o->hi.z || o->lo.z >= v->hi.z) continue;
                mult *= 1 + (uint64_t)s->level[j] / o->capacity;
                break;
            }
        }
        uint64_t inflow = (uint64_t)v->rate * mult * delta_s / 86400;
        uint64_t room = (uint64_t)v->capacity - s->level[i];
        uint64_t add = inflow < room ? inflow : room;
        s->level[i] += (uint32_t)add;
        s->recovered_total += add;
    }
    /* Pit healing: available regional stock settles into the oldest scars
    first (site order is creation order). What settles is buried matter,
    accounted as lost; it does not vanish. Fully healed pits compact away. */
    for (int i = 0; i < s->reservoir_count; i++) {
        for (int j = 0; j < s->site_count; j++) {
            WsSite* site = &s->sites[j];
            if (site->reservoir != (uint16_t)i || site->kind != WS_SITE_PIT || !site->amount) continue;
            uint32_t h = site->amount < s->level[i] ? site->amount : s->level[i];
            if (!h) continue; /* starved region: the scar waits */
            site->amount -= h;
            s->level[i] -= h;
            s->lost_total += h;
            if (!site->amount) {
                memmove(s->sites + j, s->sites + j + 1, sizeof(*s->sites) * (size_t)(s->site_count - j - 1));
                s->site_count--;
                j--;
            }
        }
    }
}

int ws_ledger_check(const WsState* s, const WsRecipe* r) {
    if (s->reservoir_count != r->reservoir_count) return 0;
    uint64_t lhs = 0, rhs = 0;
    for (int i = 0; i < s->reservoir_count; i++) {
        if (s->level[i] > r->reservoirs[i].capacity) return 0;
        lhs += s->level[i];
        rhs += r->reservoirs[i].level;
    }
    for (int i = 0; i < s->player_count; i++) lhs += s->material[i] + s->shards[i];
    lhs += s->used_total + s->lost_total;
    rhs += s->recovered_total;
    return lhs == rhs;
}

/* ---- nonlocal anomaly gates (Gate 4) ----
   A gate is a typed topology edge (link kind WS_LINK_ANOMALY) anchored to a
   regional Phos reservoir by link.reserved. Openness is a pure function of
   canonical state: the gate holds while the anchored lode keeps at least
   half its capacity. Depleting the lode removes the shortcut; recharging
   restores it. Ordinary geography is never touched either way. */

int ws_link_open(const WsRecipe* r, const WsState* s, uint16_t link) {
    if (!r || link >= r->link_count || r->links[link].kind != WS_LINK_ANOMALY) return 0;
    uint16_t anchor = r->links[link].reserved;
    if (anchor >= r->reservoir_count || r->reservoir_count > WS_RESERVOIR_CAP) return 0;
    const WsReservoir* v = &r->reservoirs[anchor];
    if (v->kind != WS_RES_PHOS) return 0;
    uint32_t level = v->level;
    if (s && s->reservoir_count == r->reservoir_count) {
        /* Bound canonical state decides; an unbound or empty state reports
           the declared levels, exactly like ws_river_stage. */
        level = s->level[anchor] <= v->capacity ? s->level[anchor] : v->capacity;
    }
    return 2 * level >= v->capacity;
}

int ws_use_link_state(const WsRecipe* r, const WsState* s, WsTraveler* t) {
    if (t->remaining) return 0;
    /* Open gates first: at a seat shared with an ordinary lift, an open
       gate wins (lowest link index among gates). A closed gate is simply
       absent: the traveler never enters it, and any ordinary link at the
       same seat still serves. A crossing is instant, like a portal: the
       destination is reconstructed from its own local region records. */
    for (uint16_t i = 0; i < r->link_count; i++) {
        const WsLink* l = &r->links[i];
        if (l->kind != WS_LINK_ANOMALY || !ws_link_open(r, s, i)) continue;
        for (int side = 0; side < 2; side++) {
            uint16_t from = side ? l->b : l->a, to = side ? l->a : l->b;
            WsModule a, b;
            ws_materialize(r, from, &a);
            ws_materialize(r, to, &b);
            uint16_t scope = (a.flags & WS_INTERIOR) ? from : UINT16_MAX;
            if (scope != t->at.scope) continue;
            if (llabs((int64_t)t->at.pos.x - a.pos.x) > 2500 || llabs((int64_t)t->at.pos.z - a.pos.z) > 2500 || llabs((int64_t)t->at.pos.y - a.pos.y) > 1000) continue;
            t->destination = (WsAddress) {{b.pos.x, b.pos.y, b.pos.z}, (b.flags & WS_INTERIOR) ? to : UINT16_MAX};
            t->at = t->destination;
            t->remaining = 0;
            return 1;
        }
    }
    return ws_use_link(r, t);
}

int ws_route_cost_state(const WsRecipe* r, const WsState* s, uint16_t a, uint16_t b, uint32_t caps, uint64_t* cost, uint16_t* path, size_t cap) {
    /* Deterministic Dijkstra with lowest-index tie-breaking over declared
       topology, mirroring ws_route_cost exactly, except closed gates are
       absent edges. This is a navigation query over canonical state. */
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
            if (l->kind == WS_LINK_ANOMALY && !ws_link_open(r, s, i)) continue;
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

WsDisposition ws_resource_apply(WsState* s, const WsRecipe* r, WsContext c, WsOperation op, int pi) {
    WsDisposition d = {WS_DENIED, 0, 0, 0, 0, s->revision};
    if (op.target >= s->reservoir_count) return d;
    WsPlayer* p = &s->players[pi];
    /* Resource ops bind to the global state revision: regional stocks are
       shared, so their history is strictly sequential, never pipelined. */
    if (op.sequence <= p->sequence || op.base_revision != s->revision) {
        d.status = WS_STALE;
        return d;
    }
    if (op.epoch != 1) return d; /* resource ops carry epoch 1 */
    if (!op.sequence || s->revision == UINT32_MAX) {
        d.status = WS_FULL;
        return d;
    }
    const WsReservoir* v = &r->reservoirs[op.target];
    if (op.action == WS_EXCAVATE) {
        if (!op.amount) return d;
        /* Regional action: a client stands inside the region it works. */
        if (!c.server_authority && (c.at.pos.x < v->lo.x || c.at.pos.x > v->hi.x || c.at.pos.z < v->lo.z || c.at.pos.z > v->hi.z)) return d;
        uint32_t take = op.amount < s->level[op.target] ? op.amount : s->level[op.target];
        if (!take) return d; /* depleted: nothing to take, nothing to record */
        int site_kind = -1;
        if (v->kind == WS_RES_TERRAIN || v->kind == WS_RES_PHOS) site_kind = (op.aux & 1) ? WS_SITE_EXCAVATION : WS_SITE_PIT;
        if (site_kind >= 0 && s->site_count >= WS_SITE_CAP) {
            d.status = WS_FULL;
            return d;
        }
        if (v->kind != WS_RES_WATER) {
            uint16_t* carry = v->kind == WS_RES_PHOS ? &s->shards[pi] : &s->material[pi];
            if (*carry > UINT16_MAX - take) {
                d.status = WS_FULL;
                return d;
            }
        }
        s->level[op.target] -= take;
        if (v->kind == WS_RES_WATER) s->used_total += take; /* drawn water is consumed */
        else if (v->kind == WS_RES_PHOS) s->shards[pi] = (uint16_t)(s->shards[pi] + take);
        else s->material[pi] = (uint16_t)(s->material[pi] + take); /* terrain and biomass both yield carried material */
        if (site_kind >= 0) {
            WsSite* site = &s->sites[s->site_count++];
            site->pos = c.at.pos;
            site->reservoir = op.target;
            site->kind = (uint16_t)site_kind;
            site->extent = (uint16_t)take;
            site->reserved = 0;
            site->amount = take;
        }
    } else if (op.action == WS_CONVERT) {
        /* Bounded, lossy, no duplication: 3 material refine to 1 Geo-phos
           shard; 2 shards deposit to 1 material. Remainders are lost. */
        if (!op.amount || op.aux > 1) return d;
        int to_shards = op.aux == 0;
        uint16_t *src = to_shards ? &s->material[pi] : &s->shards[pi], *dst = to_shards ? &s->shards[pi] : &s->material[pi];
        if (*src < op.amount) return d;
        uint16_t out = (uint16_t)(to_shards ? op.amount / 3 : op.amount / 2);
        if (!out) return d; /* sub-threshold dust is denied, not eaten */
        if (*dst > UINT16_MAX - out) {
            d.status = WS_FULL;
            return d;
        }
        *src = (uint16_t)(*src - op.amount);
        *dst = (uint16_t)(*dst + out);
        s->lost_total += op.amount - out;
    } else if (op.action == WS_SPEND) {
        if (!op.amount || s->shards[pi] < op.amount) return d;
        s->shards[pi] = (uint16_t)(s->shards[pi] - op.amount);
        s->used_total += op.amount;
    } else
        return d;
    s->revision++;
    p->sequence = op.sequence;
    d.status = WS_OK;
    d.world_changed = 1;
    d.revision = s->revision;
    ws_record(s, c, op, op.action == WS_EXCAVATE ? WS_DISCOVERY : WS_WORLD_EVENT);
    return d;
}
