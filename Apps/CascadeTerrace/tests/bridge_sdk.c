/* Gate 6 bridge proof: the legacy Cascade gameplay crossed into the
   canonical event boundary without changing a single gameplay outcome.

   Two games run the same script from the same seed: game A applies every
   operation directly (the plain game), game B applies the same operations
   through the bridge (game first, canonical echo second). After every
   operation the two legacy State structs must be byte-identical: repair
   cycles, economy, promise lifecycle, npc trust, notices, the event log.

   The canonical side is then proven on its own terms: witnesses and their
   confidences ((30000 - manhattan)/30 through the shared gate), directed
   evidence for speech/claims/promises, exchange events, retry receipts,
   the honest unmapped boundary (market trades, notes), hidden-truth
   isolation (the canonical state hash is identical across gameplay
   variants while the legacy states differ), and wire v4 checkpoints
   reproducing the whole bridged state. */
#include "../content/cascade_adapter.h"
#include "../content/worlds/cascade.inc"
#include <stdio.h>
#include <string.h>
static WsRecipe recipe;
static unsigned checks;
#define CHECK(x)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(x)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            return 1;                                                    \
        }                                                                \
    } while (0)
/* The canonical cast: station 4, market 6, kyra 15, waterfall resource 9. */
#define KYRA_H 16u
#define PLAYER_H 0x10000u
typedef struct {
    Game game;
    Bridge bridge;
} Side;
static void seed_side(Game* g) {
    g->state.player.quantity[IT_CHIT] = 100;
    g->state.player.quantity[IT_COUPLING] = 2;
    g->state.player.quantity[IT_CABLE] = 1;
    g->state.player.quantity[IT_NOTE] = 1;
}
static void teleport(Game* g, int32_t x, int32_t y, int32_t z) {
    g->state.player_pos = (Pos) {x, y, z};
    g->state.grounded = 1;
}
/* One scripted operation on both sides plus the outcome-preservation proof. */
static BridgeResult step(Side* a, Side* b, Operation o, uint32_t subject, BridgeStatus expect) {
    BridgeResult none = {BRIDGE_DENIED, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}};
    if (game_apply(&a->game, o) != (expect != BRIDGE_DENIED)) goto fail;
    BridgeResult r = bridge_apply(&b->bridge, &b->game, &recipe, o, subject);
    if (r.status != expect) goto fail;
    if (memcmp(&a->game.state, &b->game.state, sizeof(State)) != 0) goto fail; /* outcomes unchanged */
    if (memcmp(&a->game.world, &b->game.world, sizeof(Generated)) != 0) goto fail;
    if (ws_state_validate(&b->bridge.ws, &recipe) != WS_OK) goto fail;
    return r;
fail:
    checks++;
    fprintf(stderr, "FAIL %s:%d: step(%d)\n", __FILE__, __LINE__, (int)o.op);
    return none;
}
#define STEPCALL(cond)                                       \
    do {                                                     \
        checks++;                                            \
        if (!(cond)) {                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                        \
        }                                                    \
    } while (0)
/* Find one retained record by kind (order between the transition evidence
   and the command echo is commit order, not table order). */
static const WsEvidence* find_ev(const WsState* s, uint16_t kind) {
    for (int i = 0; i < s->ev_count; i++)
        if (s->ev[i].kind == kind) return &s->ev[i];
    return NULL;
}
int main(void) {
    CHECK(ws_load(&recipe, ws_product, sizeof(ws_product)) == WS_OK);
    CHECK(recipe.count == 17);
    Side a, b;
    game_new(&a.game, 42, -1);
    game_new(&b.game, 42, -1);
    seed_side(&a.game);
    seed_side(&b.game);
    CHECK(bridge_init(&b.bridge, &b.game, &recipe) == 1);
    CHECK(memcmp(&a.game.state, &b.game.state, sizeof(State)) == 0);
    CHECK(b.bridge.ws.player_count == 1 && b.bridge.ws.players[0].id == PLAYER_ID);
    CHECK(b.bridge.ws.revision == recipe.revision + 1); /* the join */

    /* 1. A market trade commits in the game but has no canonical
       counterparty person: the boundary stays silent, honestly. */
    teleport(&a.game, 100000, 30000, 30000);
    teleport(&b.game, 100000, 30000, 30000);
    Operation buy = {BUY, PLAYER_ID, 0, IT_COUPLING, 1, NULL};
    step(&a, &b, buy, 0, BRIDGE_UNMAPPED);
    CHECK(a.game.state.player.quantity[IT_COUPLING] == 3 && a.game.state.player.quantity[IT_CHIT] == 40);
    CHECK(b.bridge.ws.ev_count == 0 && b.bridge.ws.revision == 1); /* nothing crossed */

    /* 2. The repair cycle at the station's south edge: kyra witnesses the
       repair and the damage from her spot at confidence 900 ((30000 -
       3000)/30 through the shared gate), and the canonical station tracks
       the same cycle (epoch per damage, one repair per cycle). */
    teleport(&a.game, 22000, 4000, -98000);
    teleport(&b.game, 22000, 4000, -98000);
    Operation repair = {REPAIR, PLAYER_ID, 0, IT_COUPLING, 1, NULL};
    Operation damage = {DAMAGE, PLAYER_ID, 0, IT_COUPLING, 1, NULL};
    BridgeResult r1 = step(&a, &b, repair, 0, BRIDGE_COMMITTED);
    STEPCALL(r1.status == BRIDGE_COMMITTED);
    CHECK(r1.d.rewarded && a.game.state.repaired);
    CHECK(b.bridge.ws.entities[4].health == 100 && b.bridge.ws.players[0].credits == 10);
    CHECK(b.bridge.ws.ev_count == 1 && b.bridge.ws.ev[0].observer == KYRA_H && b.bridge.ws.ev[0].subject == PLAYER_H && b.bridge.ws.ev[0].teller == 0 && b.bridge.ws.ev[0].valence == 300 && b.bridge.ws.ev[0].kind == WS_EV_ACT && b.bridge.ws.ev[0].confidence == 900);
    step(&a, &b, damage, 0, BRIDGE_COMMITTED);
    CHECK(!a.game.state.repaired && b.bridge.ws.entities[4].health == 0 && b.bridge.ws.entities[4].epoch == 2);
    CHECK(b.bridge.ws.ev_count == 2 && b.bridge.ws.ev[1].valence == -300 && b.bridge.ws.ev[1].confidence == 900);
    step(&a, &b, repair, 0, BRIDGE_COMMITTED);
    CHECK(a.game.state.repaired && b.bridge.ws.players[0].credits == 20);
    CHECK(b.bridge.ws.ev_count == 3);
    /* The legacy economy moved identically on both sides. */
    CHECK(a.game.state.player.quantity[IT_COUPLING] == 1 && b.game.state.player.quantity[IT_COUPLING] == 1);

    /* 3. Speech beside kyra: showing the cable, a claim about her
       maintenance log, a promise. Claims stay claim-grade. */
    teleport(&a.game, 22000, 4000, -96000);
    teleport(&b.game, 22000, 4000, -96000);
    Operation show = {SHOW, PLAYER_ID, KYRA_ID, IT_CABLE, 1, NULL};
    step(&a, &b, show, 0, BRIDGE_COMMITTED);
    CHECK(a.game.state.evidence_shown & 1);
    CHECK(b.bridge.ws.ev[b.bridge.ws.ev_count - 1].context == WS_CTX_SHOWN && b.bridge.ws.ev[b.bridge.ws.ev_count - 1].teller == PLAYER_H && b.bridge.ws.ev[b.bridge.ws.ev_count - 1].kind == WS_EV_ACT && b.bridge.ws.ev[b.bridge.ws.ev_count - 1].confidence == 1000);
    Operation tell = {TELL, PLAYER_ID, KYRA_ID, IT_CABLE, 0, "Your maintenance log says the coupling failed."};
    step(&a, &b, tell, KYRA_H, BRIDGE_COMMITTED);
    WsView v;
    CHECK(ws_social_view(&b.bridge.ws, KYRA_H, KYRA_H, &v) && v.claims == 1 && v.trust == 0); /* a claim is a claim */
    CHECK(find_ev(&b.bridge.ws, WS_EV_CLAIM)->teller == PLAYER_H && find_ev(&b.bridge.ws, WS_EV_CLAIM)->confidence == 500);
    Operation promise = {PROMISE, PLAYER_ID, KYRA_ID, IT_COUPLING, 1, "I will repair the field line."};
    step(&a, &b, promise, 0, BRIDGE_COMMITTED);
    CHECK(a.game.state.promise_state == 1);
    CHECK(ws_social_view(&b.bridge.ws, KYRA_H, PLAYER_H, &v) && v.promises == 1);
    const WsEvidence* pv = find_ev(&b.bridge.ws, WS_EV_PROMISE);
    CHECK(pv && pv->salience >= WS_EV_PINNED && pv->teller == PLAYER_H && pv->context == WS_CTX_PROMISE && pv->confidence == 500 && pv->valence == 50);
    CHECK(b.bridge.ws.tail[b.bridge.ws.tail_count - 1].action == WS_PROMISE); /* the command echo */

    /* 4. Keeping the promise by giving kyra the coupling: one act, two
       canonical meanings (the notarized exchange and the kept obligation,
       authority-observed, teller 0). */
    Operation give = {GIVE, PLAYER_ID, KYRA_ID, IT_COUPLING, 1, NULL};
    step(&a, &b, give, 0, BRIDGE_COMMITTED);
    CHECK(a.game.state.promise_state == 2);
    CHECK(ws_social_view(&b.bridge.ws, KYRA_H, PLAYER_H, &v) && v.kept == 1 && v.promises == 1);
    const WsEvidence* kv = find_ev(&b.bridge.ws, WS_EV_KEPT);
    CHECK(kv && kv->teller == 0 && kv->valence == 200 && kv->confidence == 900 && kv->salience == 400);
    CHECK(find_ev(&b.bridge.ws, WS_EV_ACT) && b.bridge.ws.ev[b.bridge.ws.ev_count - 1].teller == 0 && b.bridge.ws.ev[b.bridge.ws.ev_count - 1].valence == 100);

    /* 5. A second promise, then let the deadline lapse: the breach is
       authority-observed (no command echo) and pinned so time never
       forgives it. */
    step(&a, &b, promise, 0, BRIDGE_COMMITTED);
    CHECK(a.game.state.promise_state == 1);
    Operation wait = {WAIT, PLAYER_ID, 0, IT_CHIT, 1441, NULL};
    step(&a, &b, wait, 0, BRIDGE_UNMAPPED); /* time mechanics: no canonical echo */
    CHECK(a.game.state.promise_state == 3);
    CHECK(ws_social_view(&b.bridge.ws, KYRA_H, PLAYER_H, &v) && v.breaches == 1 && v.promises == 2);
    const WsEvidence* bv = find_ev(&b.bridge.ws, WS_EV_BREACH);
    CHECK(bv && bv->salience >= WS_EV_PINNED && bv->valence == -200 && bv->teller == 0 && bv->confidence == 1000);

    /* 6. A transfer to kyra is a notarized exchange; its canonical command
       carries a retry receipt. */
    Operation transfer = {TRANSFER, PLAYER_ID, KYRA_ID, IT_CHIT, 5, NULL};
    BridgeResult rt = step(&a, &b, transfer, 0, BRIDGE_COMMITTED);
    STEPCALL(rt.status == BRIDGE_COMMITTED);
    CHECK(rt.canonical.action == WS_EXCHANGE);
    CHECK(a.game.state.player.quantity[IT_CHIT] == 35 && b.game.state.player.quantity[IT_CHIT] == 35);
    uint32_t hash = ws_state_hash(&b.bridge.ws);
    Pos keep = b.game.state.player_pos;
    WsContext retry = {PLAYER_ID, {{keep.x, keep.y, keep.z}, UINT16_MAX}, 1};
    WsDisposition replay = ws_apply(&b.bridge.ws, &recipe, retry, rt.canonical);
    CHECK(replay.status == WS_OK && replay.revision == rt.d.revision); /* the receipt */
    CHECK(ws_state_hash(&b.bridge.ws) == hash);                         /* nothing re-charged */

    /* 7. Extraction at the falls: the canonical waterfall stock pays out;
       nobody is informed (kyra is far out of range). */
    teleport(&a.game, 0, 0, -125000);
    teleport(&b.game, 0, 0, -125000);
    CHECK(b.bridge.ws.entities[9].quantity == 5000);
    Operation extract = {EXTRACT, PLAYER_ID, 0, IT_CHIT, 60, NULL};
    step(&a, &b, extract, 0, BRIDGE_COMMITTED);
    CHECK(b.bridge.ws.entities[9].quantity == 4940);
    uint16_t ev_before = b.bridge.ws.ev_count;
    /* The note has no canonical slot: bounded vocabulary, honest silence. */
    Operation show_note = {SHOW, PLAYER_ID, KYRA_ID, IT_NOTE, 1, NULL};
    teleport(&a.game, 22000, 4000, -96000);
    teleport(&b.game, 22000, 4000, -96000);
    step(&a, &b, show_note, 0, BRIDGE_UNMAPPED);
    CHECK(a.game.state.evidence_shown == 1);  /* only the cable was shown */
    CHECK(b.bridge.ws.ev_count == ev_before); /* nothing formed */
    /* Taking without consent is refused by the game: nothing crosses. */
    Operation take = {TAKE, PLAYER_ID, KYRA_ID, IT_TOOLKIT, 1, NULL};
    step(&a, &b, take, 0, BRIDGE_DENIED);

    /* 8. The final projection with exact literals: eleven witnessed or
       notarized acts (each promise and keeping is both a seen act and a
       directed obligation record), one claim about kyra, two promises, one
       kept, one breach, and the trust arithmetic
       (270 - 270 + 270 + 48 + 50 + 0 + 48 + 25 + 193 + 180 + 100 + 48 +
       25 - 200 + 100). */
    CHECK(ws_social_view(&b.bridge.ws, KYRA_H, PLAYER_H, &v));
    CHECK(v.acts == 11 && v.claims == 0 && v.promises == 2 && v.kept == 1 && v.breaches == 1 && v.reports == 0);
    CHECK(v.trust == 887);
    uint32_t root = 0;
    uint16_t kind = 0, sal = 0;
    CHECK(ws_social_cite(&b.bridge.ws, KYRA_H, PLAYER_H, &root, &kind, &sal) && kind == WS_EV_BREACH && sal >= WS_EV_PINNED);
    CHECK(ws_state_validate(&b.bridge.ws, &recipe) == WS_OK);

    /* 9. Hidden-truth isolation: the same script on three gameplay variants
       produces one identical canonical state (three equal hashes: also the
       determinism proof), while the legacy states differ by the variant
       byte. The boundary carries only what happened, never which hidden
       truth the terrain encodes. */
    static uint32_t hashes[3];
    static State legacy[3];
    Operation script[] = {buy, repair, damage, repair, show, tell, promise, give, promise, wait, transfer};
    static const Pos spots[] = {{100000, 30000, 30000}, {22000, 4000, -98000}, {22000, 4000, -98000}, {22000, 4000, -98000}, {22000, 4000, -96000}, {22000, 4000, -96000}, {22000, 4000, -96000}, {22000, 4000, -96000}, {22000, 4000, -96000}, {22000, 4000, -96000}, {22000, 4000, -96000}};
    for (int i = 0; i < 3; i++) {
        Side x, y;
        game_new(&x.game, 42, i);
        game_new(&y.game, 42, i);
        seed_side(&x.game);
        seed_side(&y.game);
        CHECK(bridge_init(&y.bridge, &y.game, &recipe) == 1);
        for (size_t k = 0; k < sizeof(script) / sizeof(script[0]); k++) {
            teleport(&x.game, spots[k].x, spots[k].y, spots[k].z);
            teleport(&y.game, spots[k].x, spots[k].y, spots[k].z);
            CHECK(game_apply(&x.game, script[k]) == 1);
            CHECK(bridge_apply(&y.bridge, &y.game, &recipe, script[k], script[k].op == TELL ? KYRA_H : 0).status != BRIDGE_DENIED);
            CHECK(memcmp(&x.game.state, &y.game.state, sizeof(State)) == 0); /* outcomes still identical */
        }
        CHECK(ws_state_validate(&y.bridge.ws, &recipe) == WS_OK);
        hashes[i] = ws_state_hash(&y.bridge.ws);
        legacy[i] = y.game.state;
        if (i) {
            CHECK(hashes[i] == hashes[0]);    /* the canonical layer never saw the variant */
            CHECK(memcmp(&legacy[i], &legacy[0], sizeof(State)) != 0); /* the legacy states do */
        }
    }

    /* 10. Wire v4 checkpoint of the bridged state round-trips exactly. */
    static uint8_t bytes[16384];
    size_t n = ws_state_encode(&b.bridge.ws, bytes, sizeof(bytes));
    CHECK(n > 48 && bytes[4] == 4 && bytes[5] == 0);
    WsState loaded;
    CHECK(ws_state_decode(&loaded, &recipe, bytes, n) == WS_OK);
    CHECK(ws_state_hash(&loaded) == ws_state_hash(&b.bridge.ws));
    WsView v2;
    ws_social_view(&b.bridge.ws, KYRA_H, PLAYER_H, &v);
    ws_social_view(&loaded, KYRA_H, PLAYER_H, &v2);
    CHECK(v.trust == v2.trust && v.claims == v2.claims && v.breaches == v2.breaches && v.kept == v2.kept);
    remove("build/bridge-state.0");
    remove("build/bridge-state.1");
    CHECK(ws_save(&b.bridge.ws, "build/bridge-state"));
    WsState restored;
    CHECK(ws_restore(&restored, &recipe, "build/bridge-state"));
    CHECK(ws_state_hash(&restored) == ws_state_hash(&b.bridge.ws));

    printf("{\"stage\":\"bridge\",\"passed\":%u,\"legacy_state\":%zu,\"ws_state\":%zu,\"evidence\":%u,\"revision\":%u,\"wire_bytes\":%zu}\n",
           checks, sizeof(State), sizeof(WsState), b.bridge.ws.ev_count, b.bridge.ws.revision, n);
    return 0;
}
