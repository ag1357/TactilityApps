/* Compatibility boundary for generation-v1 saves. Game-specific role mapping
   belongs to content, never the World SDK. Terrain and mystery remain legacy. */
#include "cascade_adapter.h"
#include "worlds/cascade.inc"
#include <string.h>
static WsRecipe product;
static int loaded;
int cascade_recipe_sites(Generated* w, uint32_t seed) {
    if (!loaded) {
        if (ws_load(&product, ws_product, sizeof(ws_product)) != WS_OK) return 0;
        loaded = 1;
    }
    product.seed = seed;
    for (int i = 0; i < STRUCT_COUNT; i++) {
        WsModule m;
        ws_materialize(&product, (uint16_t)(i + 4), &m);
        w->sites[i] = (Site) {{m.pos.x, m.pos.y, m.pos.z}, m.size.x / 2, m.size.z / 2, i == BRIDGE ? 0 : m.size.y};
    }
    return 1;
}

/* ---- Gate 6 canonical event boundary ------------------------------------
   The Cascade cast in recipe indexes (module order): station 4 (repairable
   room), market 6 (trade plane), kyra 15 (the NPC beside the station), and
   the waterfall hydro resource 9 (the legacy extraction zone's stock). */
enum { CAST_STATION = 4, CAST_MARKET = 6, CAST_KYRA = 15, CAST_FIELD = 9 };
#define H_NPC(i) ((uint32_t)(i) + 1)
/* Canonical slot for a legacy item: slots are positional and bounded, so
   IT_NOTE (7) has no canonical slot and stays unmapped. */
static int slot_of(uint16_t item) { return item < WS_INVENTORY ? (int)item : -1; }
int bridge_init(Bridge* b, Game* g, const WsRecipe* r) {
    memset(b, 0, sizeof(*b));
    if (!g || !r || r->count <= CAST_KYRA) return 0;
    if (!(r->modules[CAST_STATION].flags & WS_REPAIRABLE) || !(r->modules[CAST_KYRA].flags & WS_NPC) || !(r->modules[CAST_FIELD].flags & WS_RESOURCE)) return 0;
    b->station = CAST_STATION;
    b->market = CAST_MARKET;
    b->kyra = CAST_KYRA;
    b->field = CAST_FIELD;
    ws_state_init(&b->ws, r);
    if (ws_join(&b->ws, PLAYER_ID) != WS_OK) return 0;
    b->sequence = 1;
    b->last_promise = g->state.promise_state;
    return 1;
}
/* The canonical context is the authority view of the act: the player's
   actual position at commit time, server authority (the legacy layer has
   already enforced its own locality; the witness gate still measures from
   the real position, so evidence confidence reflects real observation). */
static WsContext at(Game* g) {
    Pos p = g->state.player_pos;
    return (WsContext) {PLAYER_ID, {{p.x, p.y, p.z}, UINT16_MAX}, 1};
}
/* Promise lifecycle as canonical evidence. The promise itself and its
   keeping are player commands (tail events); a lapsed deadline is an
   authority-observed world fact (no command echo). A promise is only the
   promiser's speech (claim-grade, teller = the player); the keeping and the
   lapse are authority-observed facts (direct, teller 0). Active obligations
   and breaches are pinned (salience >= WS_EV_PINNED) so they survive decay:
   forgiveness-by-time never applies to obligations owed or grave history. */
static WsOperation promise_evidence(Bridge* b, Game* g, const WsRecipe* r, uint16_t kind, int16_t valence, uint16_t salience, uint16_t context, int tail_action) {
    b->ws.revision++;
    WsOperation pop = {0, 0, 0, 0, 0, 0, 0};
    if (tail_action) {
        pop = (WsOperation) {b->sequence++, 1, 0, (uint16_t)tail_action, b->kyra, 0, 0};
        ws_record(&b->ws, r, at(g), pop, WS_QUIET);
    }
    uint32_t teller = kind == WS_EV_PROMISE ? WS_EV_PLAYER : 0;
    uint16_t confidence = kind == WS_EV_PROMISE ? 500 : (kind == WS_EV_KEPT ? 900 : 1000);
    ws_inform(&b->ws, r, H_NPC(b->kyra), WS_EV_PLAYER, teller, kind, confidence, context, valence, salience, b->ws.revision);
    return pop;
}
BridgeResult bridge_apply(Bridge* b, Game* g, const WsRecipe* r, Operation o, uint32_t subject) {
    BridgeResult out = {BRIDGE_DENIED, {WS_DENIED, 0, 0, 0, 0, b->ws.revision}, {0, 0, 0, 0, 0, 0, 0}};
    if (!b || !g || !r) return out;
    uint8_t before = b->last_promise;
    /* The game is the gameplay authority: nothing crosses the boundary
       unless the act actually happened there. */
    if (!game_apply(g, o)) return out;
    /* Promise lifecycle transitions observed by the authority. */
    uint8_t now = g->state.promise_state;
    if (before == 1 && now == 2) out.canonical = promise_evidence(b, g, r, WS_EV_KEPT, 200, 400, WS_CTX_NONE, WS_KEEP_PROMISE);
    else if (before == 1 && now == 3) promise_evidence(b, g, r, WS_EV_BREACH, -200, WS_EV_PINNED, WS_CTX_NONE, 0);
    b->last_promise = now;
    out.status = BRIDGE_UNMAPPED;
    out.d.revision = b->ws.revision;
    WsPlayer* p = &b->ws.players[0];
    WsOperation canon = {0, 0, 0, 0, 0, 0, 0};
    int mapped = 0;
    switch (o.op) {
        case REPAIR:
        case DAMAGE: {
            /* The canonical station tracks the same intact/damaged cycle:
               the ws epoch advances on damage exactly like the legacy
               repaired flag flips, so repairs stay one-per-cycle. */
            WsEntity* e = &b->ws.entities[b->station];
            if (o.op == REPAIR) p->inventory[r->modules[b->station].phos] = (uint16_t)g->state.player.quantity[IT_COUPLING];
            canon = (WsOperation) {b->sequence++, e->epoch, e->revision, o.op == REPAIR ? WS_REPAIR : WS_DAMAGE, b->station, 1, 0};
            mapped = 1;
            break;
        }
        case EXTRACT: {
            WsEntity* e = &b->ws.entities[b->field];
            canon = (WsOperation) {b->sequence++, e->epoch, e->revision, WS_EXTRACT, b->field, (uint16_t)(o.amount ? o.amount : 1), 0};
            mapped = 1;
            break;
        }
        case SHOW: {
            int slot = slot_of(o.item);
            if (slot < 0) return out; /* bounded canonical inventory: no slot for this item */
            p->inventory[slot] = (uint16_t)g->state.player.quantity[o.item];
            WsEntity* e = &b->ws.entities[b->kyra];
            canon = (WsOperation) {b->sequence++, e->epoch, e->revision, WS_SHOW, b->kyra, 0, (uint16_t)slot};
            mapped = 1;
            break;
        }
        case TELL: {
            if (!ws_ev_handle_ok(&b->ws, r, subject)) return out; /* claims need a subject */
            WsEntity* e = &b->ws.entities[b->kyra];
            canon = (WsOperation) {b->sequence++, e->epoch, e->revision, WS_TELL, b->kyra, 0, (uint16_t)subject};
            mapped = 1;
            break;
        }
        case BUY:
        case SELL:
            /* Market trades have no canonical counterparty person: the
               market is an institution, not an NPC. The game outcome
               stands; the boundary stays silent rather than inventing a
               witness. */
            if (o.target != KYRA_ID) return out;
            /* a trade with the person is a notarized exchange */
            /* fall through */
        case TRANSFER:
        case GIVE: {
            /* Trades with the person are notarized exchange events; the
               goods accounting stays with the game (this gate). */
            WsEntity* e = &b->ws.entities[b->kyra];
            canon = (WsOperation) {b->sequence++, e->epoch, e->revision, WS_EXCHANGE, b->kyra, (uint16_t)(o.amount ? o.amount : 1), 0};
            mapped = 1;
            break;
        }
        case PROMISE:
            out.canonical = promise_evidence(b, g, r, WS_EV_PROMISE, 50, WS_EV_PINNED, WS_CTX_PROMISE, WS_PROMISE);
            out.status = BRIDGE_COMMITTED;
            out.d.status = WS_OK;
            out.d.world_changed = 1;
            out.d.revision = b->ws.revision;
            return out;
        default:
            /* WAIT, CONDENSE, PICK_UP, DROP and auto events (FUND,
               BREAK_PROMISE) have no canonical echo: they are legacy
               economy or time mechanics, and the promise observations
               above already carry the social meaning. */
            return out;
    }
    if (!mapped) return out;
    out.canonical = canon;
    out.d = ws_apply(&b->ws, r, at(g), canon);
    out.status = out.d.status == WS_OK ? BRIDGE_COMMITTED : BRIDGE_REFUSED;
    return out;
}
