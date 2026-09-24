#include "state.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static int player(const WsState* s, uint32_t id) {
    for (int i = 0; i < s->player_count; i++)
        if (s->players[i].id == id) return i;
    return -1;
}
/* Authority-assigned contextual valence of each committed action: what a
   witness's projection should make of the act itself. Claims and reports
   carry no valence (they are counted, never believed); arena and defense
   contexts re-interpret force at view time. */
static const int16_t WS_VALENCE[] = {
    [WS_EXTRACT] = 0,   [WS_REPAIR] = 300,  [WS_DAMAGE] = -300, [WS_TRANSFER] = 100,
    [WS_GRANT] = 0,     [WS_DECORATE] = 0,  [WS_AID] = 300,     [WS_KILL] = -500,
    [WS_PROMISE] = 50,  [WS_KEEP_PROMISE] = 200,               [WS_EXCAVATE] = -100,
    [WS_CONVERT] = 0,   [WS_SPEND] = 0,     [WS_SHOW] = 50,     [WS_TELL] = 0,
    [WS_REPORT] = 0,    [WS_EXCHANGE] = 100,
};
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
        if (o->action < WS_EXTRACT || o->action > WS_EXCHANGE) return WS_BOUNDS;
        if (o->action >= WS_EXCAVATE && o->action <= WS_SPEND ? o->target >= s->reservoir_count : o->target >= s->count) return WS_BOUNDS;
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
    /* Directed evidence capsules and pending dispatches (Gate 6): handles
       must resolve, classes/weights must be bounded, anchors must reference
       committed revisions, and no (observer, root, teller, kind) pair may
       appear twice (repeats replace, they never stack corroboration).
       Receipts must match the player they belong to. */
    if (s->ev_count > WS_EV_CAP || s->pend_count > WS_PEND_CAP) return WS_BOUNDS;
    for (int i = 0; i < s->ev_count; i++) {
        const WsEvidence* e = &s->ev[i];
        if (e->kind < WS_EV_ACT || e->kind > WS_EV_REPORT || e->confidence > 1000 || e->salience > 1000 || e->context > WS_CTX_DISPATCH) return WS_BOUNDS;
        if (e->reserved || e->pad || e->valence < -1000 || e->valence > 1000) return WS_BOUNDS;
        if (!ws_ev_handle_ok(s, r, e->observer) || !ws_ev_handle_ok(s, r, e->subject)) return WS_REFERENCE;
        if (e->teller && (e->teller == e->observer || !ws_ev_handle_ok(s, r, e->teller))) return WS_REFERENCE;
        if (!e->root || e->root > s->revision) return WS_BOUNDS;
        for (int j = 0; j < i; j++)
            if (e->observer == s->ev[j].observer && e->root == s->ev[j].root && e->teller == s->ev[j].teller && e->kind == s->ev[j].kind) return WS_DUPLICATE;
    }
    for (int i = 0; i < s->pend_count; i++) {
        const WsPending* p = &s->pend[i];
        if (p->kind < WS_EV_ACT || p->kind > WS_EV_REPORT || p->confidence > 1000 || p->salience > 1000 || p->context > WS_CTX_DISPATCH) return WS_BOUNDS;
        if (p->reserved || p->pad || p->valence < -1000 || p->valence > 1000) return WS_BOUNDS;
        if (!ws_ev_handle_ok(s, r, p->observer) || !ws_ev_handle_ok(s, r, p->subject) || !p->teller || p->teller == p->observer || !ws_ev_handle_ok(s, r, p->teller)) return WS_REFERENCE;
        if (!p->root || p->root > s->revision) return WS_BOUNDS;
    }
    for (int i = 0; i < s->player_count; i++) {
        const WsReceipt* rc = &s->receipt[i];
        if (!rc->sequence) continue;
        if (rc->status > WS_FULL || rc->action < WS_EXTRACT || rc->action > WS_EXCHANGE || rc->flags & ~7u || rc->revision > s->revision) return WS_BOUNDS;
        if (rc->action >= WS_EXCAVATE && rc->action <= WS_SPEND ? rc->target >= s->reservoir_count : rc->target >= s->count) return WS_BOUNDS;
        if (s->players[i].sequence < rc->sequence) return WS_BOUNDS;
    }
    return WS_OK;
}
/* Evidence formation shared by every commit path: after the canonical tail
   entry, the authority projects the act onto its actual NPC witnesses
   through the shared witness gate (distance, scope and coarse occlusion).
   An informed observer gains direct ACT-class evidence at the gate's own
   confidence; everyone else is unchanged. Teller 0 marks direct
   observation, so reported copies of the same root can never outrank
   having been there. */
static void witness_project(WsState* s, const WsRecipe* r, WsContext c, WsOperation op) {
    for (uint16_t i = 0; i < r->count; i++) {
        if (!(r->modules[i].flags & WS_NPC) || !s->entities[i].alive) continue;
        WsModule m;
        ws_materialize(r, i, &m);
        WsAddress at = {{m.pos.x, m.pos.y, m.pos.z}, (m.flags & WS_INTERIOR) ? i : UINT16_MAX};
        uint16_t confidence = ws_witness(r, at, c.at, 1000, 1000);
        if (!confidence) continue; /* uninformed observers stay unchanged */
        WsEvidence e = {(uint32_t)i + 1, (uint32_t)(WS_EV_PLAYER + player(s, c.player)), 0, s->revision,
                        WS_EV_ACT, confidence, 0, s->clock_s, 0, 0, WS_VALENCE[op.action], 0};
        ws_evidence_put(s, &e);
    }
}
void ws_record(WsState* s, const WsRecipe* r, WsContext c, WsOperation op, uint16_t feed_kind) {
    if (s->tail_count == WS_TAIL_CAP) {
        memmove(s->tail, s->tail + 1, sizeof(*s->tail) * (WS_TAIL_CAP - 1));
        s->tail_count--;
    }
    s->tail[s->tail_count++] = op;
    if (feed_kind != WS_QUIET) {
        if (s->feed_count == WS_FEED_CAP) {
            memmove(s->feed, s->feed + 1, sizeof(*s->feed) * (WS_FEED_CAP - 1));
            s->feed_count--;
        }
        s->feed[s->feed_count++] = (WsFeed) {s->revision, c.player, op.epoch, op.target, feed_kind};
    }
    witness_project(s, r, c, op);
}
static int near(const WsRecipe* r, WsContext c, uint16_t target) {
    WsModule m;
    ws_materialize(r, target, &m);
    uint16_t scope = (m.flags & WS_INTERIOR) ? target : UINT16_MAX;
    return c.at.scope == scope && llabs((int64_t)c.at.pos.x - m.pos.x) <= 3000 && llabs((int64_t)c.at.pos.y - m.pos.y) <= 3000 && llabs((int64_t)c.at.pos.z - m.pos.z) <= 3000;
}
/* Store the exact disposition of the last committed entity command so a
   transport retry replays the receipt instead of re-charging. */
static void keep_receipt(WsState* s, int pi, WsOperation op, WsDisposition d) {
    WsReceipt* rc = &s->receipt[pi];
    *rc = (WsReceipt) {op.sequence, op.epoch, op.base_revision, d.revision,
                       op.action, op.target, op.amount, op.aux, (uint16_t)d.status,
                       (uint16_t)((d.rewarded ? 1 : 0) | (d.world_changed ? 2 : 0) | (d.historical ? 4 : 0))};
}
/* Shared commit tail for the Gate 6 information ops: canonical event,
   private record (no public feed), receipt. root is the new revision. */
static WsDisposition inform_commit(WsState* s, const WsRecipe* r, int pi, WsContext c, WsOperation op, WsDisposition d) {
    s->revision++;
    s->players[pi].sequence = op.sequence;
    d.status = WS_OK;
    d.world_changed = 1;
    d.revision = s->revision;
    keep_receipt(s, pi, op, d);
    ws_record(s, r, c, op, WS_QUIET);
    return d;
}
WsDisposition ws_apply(WsState* s, const WsRecipe* r, WsContext c, WsOperation op) {
    WsDisposition d = {WS_DENIED, 0, 0, 0, 0, s->revision};
    if (ws_state_validate(s, r) != WS_OK) {
        d.status = WS_VERSION;
        return d;
    }
    ws_social_settle(s, r);
    int pi = player(s, c.player);
    if (pi < 0) return d;
    /* Resource ops address reservoirs, not entities, and carry their own
       authority, locality and replay rules (resource.c). */
    if (op.action >= WS_EXCAVATE && op.action <= WS_SPEND) return ws_resource_apply(s, r, c, op, pi);
    if (op.target >= s->count) return d;
    WsPlayer* p = &s->players[pi];
    WsEntity* e = &s->entities[op.target];
    const WsModule* m = &r->modules[op.target];
    /* Retry of the exact committed command returns its receipt unchanged:
       no repeated cost, reward or event. A stored sequence under a different
       op is sequence reuse, which stays stale. */
    if (op.sequence == p->sequence && p->sequence) {
        const WsReceipt* rc = &s->receipt[pi];
        if (rc->sequence == op.sequence && rc->epoch == op.epoch && rc->base_revision == op.base_revision && rc->action == op.action && rc->target == op.target && rc->amount == op.amount && rc->aux == op.aux) {
            WsDisposition replay = {(WsError)rc->status, (uint8_t)((rc->flags >> 1) & 1), (uint8_t)(rc->flags & 1), (uint8_t)((rc->flags >> 2) & 1), 0, rc->revision};
            return replay;
        }
        d.status = WS_STALE;
        return d;
    }
    if (op.sequence < p->sequence || op.epoch != e->epoch || op.base_revision != e->revision) {
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
        /* ---- Gate 6 information vocabulary: the state effect is directed
           evidence, never inventory; the act is a committed canonical event
           but not public news (WS_QUIET), so private conversations stay
           private and hidden truth cannot leak through the feed. ---- */
        case WS_SHOW: {
            if (!(m->flags & WS_NPC) || !e->alive || op.aux >= WS_INVENTORY || !p->inventory[op.aux]) return d;
            WsEvidence v = {(uint32_t)op.target + 1, WS_EV_PLAYER + (uint32_t)pi, WS_EV_PLAYER + (uint32_t)pi, 0,
                            WS_EV_ACT, 1000, WS_CTX_SHOWN, 0, 0, 0, WS_VALENCE[WS_SHOW], 0};
            d = inform_commit(s, r, pi, c, op, d);
            v.root = s->revision;
            v.clock_s = s->clock_s;
            ws_evidence_put(s, &v);
            return d;
        }
        case WS_TELL: {
            /* A claim is recorded as a claim: the authority never verifies
               speech, so free text cannot commit canonical truth. */
            if (!(m->flags & WS_NPC) || !e->alive || !ws_ev_handle_ok(s, r, op.aux)) return d;
            WsEvidence v = {(uint32_t)op.target + 1, op.aux, WS_EV_PLAYER + (uint32_t)pi, 0,
                            WS_EV_CLAIM, 500, WS_CTX_NONE, 0, 0, 0, 0, 0};
            d = inform_commit(s, r, pi, c, op, d);
            v.root = s->revision;
            v.clock_s = s->clock_s;
            ws_evidence_put(s, &v);
            return d;
        }
        case WS_REPORT: {
            /* File a formal claim with the clerk for delayed institutional
               delivery: the report is the player's own assertion (provenance
               is the teller), so its delivered confidence stays claim-grade
               no matter how often it is repeated. NPC-filed reports, which
               carry real retained evidence, go through ws_file. */
            if (!(m->flags & WS_NPC) || !e->alive || !ws_ev_handle_ok(s, r, op.aux)) return d;
            uint32_t clerk = (uint32_t)op.target + 1, teller = WS_EV_PLAYER + (uint32_t)pi;
            int at = -1;
            for (int i = 0; i < s->pend_count; i++)
                if (s->pend[i].observer == clerk && s->pend[i].teller == teller && s->pend[i].subject == op.aux) {
                    at = i;
                    break;
                }
            if (at < 0 && s->pend_count >= WS_PEND_CAP) {
                d.status = WS_FULL;
                return d;
            }
            s->revision++;
            p->sequence = op.sequence;
            if (at < 0) at = s->pend_count++;
            s->pend[at] = (WsPending) {clerk, op.aux, teller, s->revision, s->clock_s + WS_REPORT_DELAY_S,
                                       WS_EV_CLAIM, 500, WS_CTX_DISPATCH, 0, 0, 0, 0};
            d.status = WS_OK;
            d.world_changed = 1;
            d.revision = s->revision;
            keep_receipt(s, pi, op, d);
            ws_record(s, r, c, op, WS_QUIET);
            return d;
        }
        case WS_EXCHANGE: {
            /* Notarized exchange: the goods accounting stays with the
               callers this gate; the counterparty person gains direct
               cooperative evidence about the actor, and the event is
               committed with retry safety like every command. */
            if ((op.aux & 0x7FFFu) >= WS_INVENTORY || !op.amount) return d;
            if (!(m->flags & WS_NPC) || !e->alive) return d;
            WsEvidence v = {(uint32_t)op.target + 1, WS_EV_PLAYER + (uint32_t)pi, 0, 0,
                            WS_EV_ACT, 1000, WS_CTX_NONE, 0, 0, 0, WS_VALENCE[WS_EXCHANGE], 0};
            d = inform_commit(s, r, pi, c, op, d);
            v.root = s->revision;
            v.clock_s = s->clock_s;
            ws_evidence_put(s, &v);
            return d;
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
    keep_receipt(s, pi, op, d);
    ws_record(s, r, c, op, op.action == WS_TRANSFER ? WS_TRADE : WS_WORLD_EVENT);
    return d;
}
WsDisposition ws_merge(WsState* s, const WsState* base, const WsRecipe* r, WsContext c, WsOperation op) {
    WsDisposition d = {WS_DENIED, 0, 0, 0, 0, s->revision};
    if (ws_state_validate(s, r) != WS_OK || ws_state_validate(base, r) != WS_OK || base->revision > s->revision) return d;
    int pi = player(s, c.player), bp = player(base, c.player);
    ws_social_settle(s, r);
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
    ws_record(s, r, c, op, WS_DISCOVERY);
    return d;
}
