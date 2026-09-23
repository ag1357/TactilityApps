#include "state.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static int player(const WsState* s, uint32_t id) {
    for (int i = 0; i < s->player_count; i++)
        if (s->players[i].id == id) return i;
    return -1;
}
void ws_state_init(WsState* s, const WsRecipe* r) {
    memset(s, 0, sizeof(*s));
    s->ancestry = r->ancestry;
    s->recipe_crc = r->recipe_crc;
    s->revision = r->revision;
    s->count = r->count;
    s->reservoir_count = r->reservoir_count;
    for (int i = 0; i < r->count; i++) {
        s->entities[i].epoch = 1;
        s->entities[i].health = (r->modules[i].flags & WS_REPAIRABLE) ? 0 : 100;
        s->entities[i].alive = 1;
        s->entities[i].public_access = 1;
        s->entities[i].quantity = r->modules[i].quantity;
    }
    for (int i = 0; i < r->reservoir_count; i++) s->level[i] = r->reservoirs[i].level;
}
WsError ws_join(WsState* s, uint32_t id) {
    if (s->player_count > WS_PLAYER_CAP) return WS_BOUNDS;
    if (!id) return WS_DENIED;
    if (player(s, id) >= 0) return WS_OK;
    if (s->player_count >= WS_PLAYER_CAP || s->revision == UINT32_MAX) return WS_FULL;
    s->players[s->player_count++].id = id;
    s->revision++;
    return WS_OK;
}
WsError ws_state_validate(const WsState* s, const WsRecipe* r) {
    if (!r->count || r->count > WS_CAP || s->count > WS_CAP) return WS_BOUNDS;
    if (!ws_id_equal(s->ancestry, r->ancestry) || s->recipe_crc != r->recipe_crc || s->count != r->count) return WS_VERSION;
    if (s->player_count > WS_PLAYER_CAP || s->feed_count > WS_FEED_CAP || s->tail_count > WS_TAIL_CAP) return WS_BOUNDS;
    for (int i = 0; i < s->count; i++) {
        const WsEntity* e = &s->entities[i];
        if (!e->epoch || e->health > 100 || e->alive > 1 || e->public_access > 1 || e->behavior > 8 || e->reserved || e->revision > s->revision) return WS_BOUNDS;
        if (e->owner && player(s, e->owner) < 0) return WS_REFERENCE;
        for (int k = 0; k < 4; k++)
            if (e->decor[k] > 7) return WS_VERSION;
    }
    for (int i = 0; i < s->player_count; i++) {
        if (!s->players[i].id) return WS_REFERENCE;
        for (int j = 0; j < i; j++)
            if (s->players[i].id == s->players[j].id) return WS_DUPLICATE;
    }
    for (int i = 0; i < s->player_count; i++)
        for (int j = 0; j < s->count; j++)
            if (s->players[i].completed[j] > s->entities[j].epoch) return WS_BOUNDS;
    for (int i = 0; i < s->player_count; i++)
        for (int j = 0; j < s->player_count; j++) {
            const WsRelationship* v = &s->relations[i][j];
            if (v->trust < -1000 || v->trust > 1000 || v->reliability < -1000 || v->reliability > 1000 || v->cooperation < -1000 || v->cooperation > 1000 || v->aggression < -1000 || v->aggression > 1000 || v->confidence > 1000 || v->promise > 1) return WS_BOUNDS;
        }
    for (int i = 0; i < s->feed_count; i++)
        if (s->feed[i].target >= (s->count > s->reservoir_count ? s->count : s->reservoir_count) || s->feed[i].kind > WS_NEWS_RESERVED || s->feed[i].revision > s->revision) return WS_BOUNDS;
    for (int i = 0; i < s->tail_count; i++) {
        const WsOperation* o = &s->tail[i];
        if (o->action < WS_EXTRACT || o->action > WS_SPEND) return WS_BOUNDS;
        if (o->action >= WS_EXCAVATE ? o->target >= s->reservoir_count : o->target >= s->count) return WS_BOUNDS;
    }
    /* Regional resource state: levels bounded and the ledger identity exact
       (site records are voids, not stocks, so they stay out of the sum). */
    if (s->reservoir_count > WS_RESERVOIR_CAP || s->site_count > WS_SITE_CAP) return WS_BOUNDS;
    if (s->reservoir_count != r->reservoir_count || (s->reservoir_count && !ws_ledger_check(s, r))) return WS_BOUNDS;
    for (int i = 0; i < s->site_count; i++) {
        const WsSite* site = &s->sites[i];
        if (site->reservoir >= s->reservoir_count || site->kind > WS_SITE_EXCAVATION || site->reserved || !site->amount || site->amount > site->extent) return WS_BOUNDS;
        const WsReservoir* v = &r->reservoirs[site->reservoir];
        if (site->pos.x < v->lo.x || site->pos.x > v->hi.x || site->pos.z < v->lo.z || site->pos.z > v->hi.z) return WS_BOUNDS;
    }
    /* Sparse creature exceptions: bounded, unique, keyed by identities the
       recipe can actually generate, with kind-specific aux bounds. Dead
       creatures carry no aux; relocations name a walk surface; pinned
       creatures name a joined player. */
    if (s->creature_count > WS_CREX_CAP) return WS_BOUNDS;
    for (int i = 0; i < s->creature_count; i++) {
        const WsCreatureEx* e = &s->crex[i];
        if (e->kind < WS_CREX_DEAD || e->kind > WS_CREX_PINNED || e->reserved || e->pad) return WS_BOUNDS;
        if (!ws_creature_known(r, e->id)) return WS_REFERENCE;
        if (e->kind == WS_CREX_DEAD && e->aux) return WS_BOUNDS;
        if (e->kind == WS_CREX_RELOCATED && (e->aux >= r->count || !(r->modules[e->aux].flags & WS_WALK) || (r->modules[e->aux].flags & WS_INTERIOR))) return WS_REFERENCE;
        if (e->kind == WS_CREX_PINNED && e->aux >= s->player_count) return WS_BOUNDS;
        for (int j = 0; j < i; j++)
            if (ws_id_equal(e->id, s->crex[j].id)) return WS_DUPLICATE;
    }
    return WS_OK;
}
void ws_record(WsState* s, WsContext c, WsOperation op, uint16_t kind) {
    if (s->tail_count == WS_TAIL_CAP) {
        memmove(s->tail, s->tail + 1, sizeof(*s->tail) * (WS_TAIL_CAP - 1));
        s->tail_count--;
    }
    s->tail[s->tail_count++] = op;
    if (s->feed_count == WS_FEED_CAP) {
        memmove(s->feed, s->feed + 1, sizeof(*s->feed) * (WS_FEED_CAP - 1));
        s->feed_count--;
    }
    s->feed[s->feed_count++] = (WsFeed) {s->revision, c.player, op.epoch, op.target, kind};
}
static int near(const WsRecipe* r, WsContext c, uint16_t target) {
    WsModule m;
    ws_materialize(r, target, &m);
    uint16_t scope = (m.flags & WS_INTERIOR) ? target : UINT16_MAX;
    return c.at.scope == scope && llabs((int64_t)c.at.pos.x - m.pos.x) <= 3000 && llabs((int64_t)c.at.pos.y - m.pos.y) <= 3000 && llabs((int64_t)c.at.pos.z - m.pos.z) <= 3000;
}
WsDisposition ws_apply(WsState* s, const WsRecipe* r, WsContext c, WsOperation op) {
    WsDisposition d = {WS_DENIED, 0, 0, 0, 0, s->revision};
    if (ws_state_validate(s, r) != WS_OK) {
        d.status = WS_VERSION;
        return d;
    }
    int pi = player(s, c.player);
    if (pi < 0) return d;
    /* Resource ops address reservoirs, not entities, and carry their own
       authority, locality and replay rules (resource.c). */
    if (op.action >= WS_EXCAVATE) return ws_resource_apply(s, r, c, op, pi);
    if (op.target >= s->count) return d;
    WsPlayer* p = &s->players[pi];
    WsEntity* e = &s->entities[op.target];
    const WsModule* m = &r->modules[op.target];
    if (op.sequence <= p->sequence || op.epoch != e->epoch || op.base_revision != e->revision) {
        d.status = WS_STALE;
        return d;
    }
    if (!op.sequence || s->revision == UINT32_MAX) {
        d.status = WS_FULL;
        return d;
    }
    if (!c.server_authority && !near(r, c, op.target)) return d;
    switch (op.action) {
        case WS_EXTRACT:
            if (!(m->flags & WS_RESOURCE) || !op.amount || e->quantity < op.amount || p->inventory[m->phos] > UINT16_MAX - op.amount) return d;
            e->quantity -= op.amount;
            p->inventory[m->phos] += op.amount;
            break;
        case WS_REPAIR:
            if (!(m->flags & WS_REPAIRABLE) || e->health == 100 || !p->inventory[m->phos] || p->completed[op.target] >= e->epoch || p->credits > UINT32_MAX - 10) return d;
            p->inventory[m->phos]--;
            e->health = 100;
            p->completed[op.target] = e->epoch;
            p->credits += 10;
            d.rewarded = 1;
            break;
        case WS_DAMAGE:
            if (!c.server_authority || !(m->flags & WS_REPAIRABLE) || e->epoch == UINT32_MAX) return d;
            e->health = 0;
            e->epoch++;
            break;
        case WS_TRANSFER: {
            int to = player(s, op.aux);
            if (to < 0 || to == pi || m->phos >= WS_INVENTORY || !op.amount || p->inventory[m->phos] < op.amount || s->players[to].inventory[m->phos] > UINT16_MAX - op.amount) return d;
            p->inventory[m->phos] -= op.amount;
            s->players[to].inventory[m->phos] += op.amount;
            break;
        }
        case WS_GRANT:
            if (!c.server_authority || !(m->flags & WS_PROPERTY) || e->owner) return d;
            e->owner = c.player;
            e->public_access = 0;
            break;
        case WS_DECORATE:
            if (e->owner != c.player || op.aux >= 4 || op.amount > 7) return d;
            if (op.amount && !p->inventory[op.amount - 1]) return d;
            if (e->decor[op.aux] && p->inventory[e->decor[op.aux] - 1] == UINT16_MAX) return d;
            if (op.amount) p->inventory[op.amount - 1]--;
            if (e->decor[op.aux]) p->inventory[e->decor[op.aux] - 1]++;
            e->decor[op.aux] = op.amount;
            break;
        case WS_AID:
            if (!(m->flags & WS_NPC) || !e->alive || p->completed[op.target] >= e->epoch || !p->inventory[m->phos] || p->credits > UINT32_MAX - 10) return d;
            p->inventory[m->phos]--;
            p->completed[op.target] = e->epoch;
            p->credits += 10;
            d.rewarded = 1;
            break;
        case WS_KILL:
            if (!c.server_authority || !(m->flags & WS_NPC) || !e->alive || e->epoch == UINT32_MAX) return d;
            e->alive = 0;
            e->epoch++;
            break;
        case WS_PROMISE:
        case WS_KEEP_PROMISE: {
            int to = player(s, op.aux);
            if (to < 0 || to == pi) return d;
            WsRelationship* rel = &s->relations[to][pi];
            if (op.action == WS_PROMISE) {
                if (rel->promise) return d;
                rel->promise = 1;
            } else {
                if (!rel->promise) return d;
                rel->promise = 0;
                if (rel->reliability < 990) rel->reliability += 10;
                if (rel->confidence < 990) rel->confidence += 10;
            }
            break;
        }
        default:
            return d;
    }
    s->revision++;
    e->revision = s->revision;
    p->sequence = op.sequence;
    d.status = WS_OK;
    d.world_changed = 1;
    d.revision = s->revision;
    ws_record(s, c, op, op.action == WS_TRANSFER ? WS_TRADE : WS_WORLD_EVENT);
    return d;
}
WsDisposition ws_merge(WsState* s, const WsState* base, const WsRecipe* r, WsContext c, WsOperation op) {
    WsDisposition d = {WS_DENIED, 0, 0, 0, 0, s->revision};
    if (ws_state_validate(s, r) != WS_OK || ws_state_validate(base, r) != WS_OK || base->revision > s->revision) return d;
    int pi = player(s, c.player), bp = player(base, c.player);
    if (pi < 0 || bp < 0 || op.target >= s->count) return d;
    const WsEntity* b = &base->entities[op.target];
    WsEntity* e = &s->entities[op.target];
    WsPlayer* p = &s->players[pi];
    uint16_t phos = r->modules[op.target].phos;
    /* Only these two historical objectives are merge-conditional in schema 1.
       Transfer, scarce extraction, property and competitive changes stay online. */
    if (op.action != WS_REPAIR && op.action != WS_AID) return d;
    if (op.epoch != b->epoch || op.base_revision != b->revision || op.sequence <= base->players[bp].sequence || !near(r, c, op.target)) return d;
    if (!base->players[bp].inventory[phos] || !p->inventory[phos] || p->completed[op.target] >= op.epoch) return d;
    if (op.action == WS_REPAIR && (!(r->modules[op.target].flags & WS_REPAIRABLE) || b->health == 100)) return d;
    if (op.action == WS_AID && (!(r->modules[op.target].flags & WS_NPC) || !b->alive)) return d;
    if (op.action == WS_REPAIR && e->epoch != b->epoch) {
        d.status = WS_STALE;
        return d;
    }
    if (s->revision == UINT32_MAX || p->credits > UINT32_MAX - 10) {
        d.status = WS_FULL;
        return d;
    }
    d.historical = (uint8_t)(e->epoch != b->epoch);
    if (op.action == WS_REPAIR && e->health != 100) {
        e->health = 100;
        d.world_changed = 1;
    }
    /* Server death is never reversed. Historical aid only earns personal credit. */
    p->inventory[phos]--;
    p->completed[op.target] = op.epoch;
    p->credits += 10;
    d.rewarded = 1;
    if (op.sequence > p->sequence) p->sequence = op.sequence;
    s->revision++;
    if (d.world_changed) e->revision = s->revision;
    d.status = WS_OK;
    d.revision = s->revision;
    ws_record(s, c, op, WS_DISCOVERY);
    return d;
}
