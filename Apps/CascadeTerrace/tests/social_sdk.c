/* Gate 6 proof: canonical events, witnesses, evidence and social
   projections over the Terrace Commons fixture (content/worlds/social.json,
   derived from the Cascade recipe plus the §6.7 example cast: kyra, dax,
   marisol, oren, the guild clerk, and toma behind the station wall).

   Proves, with exact literals:
   - a committed repair informs its actual witnesses through the shared
     witness gate (kyra 940, dax/marisol 790) and leaves the uninformed
     (oren out of range, toma occluded) untouched;
   - retries return the committed receipt: no repeated cost, reward or
     event, byte-identical state;
   - SHOW/TELL/REPORT/EXCHANGE are committed private canonical events
     (tail, never the public feed) that form directed evidence;
   - the NPC-to-NPC claim/contradiction/restitution example runs on the
     same record machinery, including delayed institutional delivery with
     degraded confidence and no stacking on repeated filing;
   - restitution adds evidence without deleting history; arena context
     re-interprets force; pinned salience survives time decay while minor
     evidence decays in fixed integer buckets;
   - checkpoints reproduce views bit-for-bit and malformed wires fail
     closed. */
#include "../content/worlds/social.inc"
#include "../world/state.h"
#include <stdio.h>
#include <string.h>
static WsRecipe r;
static WsState s, loaded;
static uint8_t bytes[16384];
static unsigned checks;
#define CHECK(x)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(x)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            return 1;                                                    \
        }                                                                \
    } while (0)
/* Fixture entity indexes (module order): station 4, market 6, kyra 15,
   hydro_field 16, dax 17, marisol 18, oren 19, clerk 20, toma 21. */
enum { STATION = 4, MARKET = 6, KYRA = 15, FIELD = 16, DAX = 17, MARISOL = 18, OREN = 19, CLERK = 20, TOMA = 21 };
#define H_NPC(i) ((uint32_t)(i) + 1)
#define H_PLAYER 0x10000u
static WsContext at_player(int server, int32_t x, int32_t y, int32_t z) {
    return (WsContext) {101, {{x, y, z}, UINT16_MAX}, (uint8_t)server};
}
static WsOperation mkop(uint16_t action, uint16_t target, uint32_t seq, uint16_t amount, uint16_t aux) {
    return (WsOperation) {seq, s.entities[target].epoch, s.entities[target].revision, action, target, amount, aux};
}
int main(void) {
    CHECK(ws_load(&r, ws_product, sizeof(ws_product)) == WS_OK);
    CHECK(r.count == 23); /* cascade cast + the 6.7 example + the station yard walk surface */
    ws_state_init(&s, &r);
    CHECK(ws_join(&s, 101) == WS_OK);
    s.revision = 1; /* join already bumped; keep arithmetic legible below */
    s.players[0].inventory[1] = 5;
    CHECK(ws_state_validate(&s, &r) == WS_OK);

    /* 1. The canonical Cascade repair, witnessed from the station's south
       edge. Kyra, dax and marisol gain direct ACT evidence at the gate's
       own confidence; oren (far) and toma (occluded by the station wall)
       gain nothing. */
    uint32_t hash = ws_state_hash(&s);
    WsOperation repair = mkop(WS_REPAIR, STATION, 1, 1, 0);
    WsDisposition d = ws_apply(&s, &r, at_player(1, 22000, 4300, -96500), repair);
    CHECK(d.status == WS_OK && d.rewarded && d.world_changed && d.revision == 2);
    CHECK(s.entities[STATION].health == 100 && s.players[0].inventory[1] == 4 && s.players[0].credits == 10);
    CHECK(s.ev_count == 3);
    CHECK(s.ev[0].observer == H_NPC(KYRA) && s.ev[0].teller == 0 && s.ev[0].root == 2 && s.ev[0].kind == WS_EV_ACT && s.ev[0].confidence == 940 && s.ev[0].valence == 300);
    CHECK(s.ev[1].observer == H_NPC(DAX) && s.ev[1].confidence == 790);
    CHECK(s.ev[2].observer == H_NPC(MARISOL) && s.ev[2].confidence == 790);
    WsView v;
    CHECK(ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &v) && v.acts == 1 && v.trust == 282 && v.confidence == 940 && v.watermark == 2);
    CHECK(ws_social_view(&s, H_NPC(DAX), H_PLAYER, &v) && v.trust == 237);
    CHECK(ws_social_view(&s, H_NPC(MARISOL), H_PLAYER, &v) && v.trust == 237);
    CHECK(ws_social_view(&s, H_NPC(OREN), H_PLAYER, &v) && v.acts == 0 && v.trust == 0 && v.confidence == 0); /* uninformed */
    CHECK(ws_social_view(&s, H_NPC(TOMA), H_PLAYER, &v) && v.acts == 0 && v.trust == 0);                      /* occluded */
    uint32_t root = 0;
    uint16_t kind = 0, sal = 0;
    CHECK(ws_social_cite(&s, H_NPC(KYRA), H_PLAYER, &root, &kind, &sal) && root == 2 && kind == WS_EV_ACT);
    CHECK(ws_social_cite(&s, H_NPC(OREN), H_PLAYER, &root, &kind, &sal) == 0); /* honest empty limit */

    /* 2. Retry of the exact committed command replays the receipt and
       touches nothing: no second coupling, reward, event or evidence. */
    hash = ws_state_hash(&s);
    d = ws_apply(&s, &r, at_player(1, 22000, 4300, -96500), repair);
    CHECK(d.status == WS_OK && d.rewarded && d.world_changed && d.revision == 2);
    CHECK(ws_state_hash(&s) == hash);
    CHECK(s.players[0].credits == 10 && s.players[0].inventory[1] == 4 && s.ev_count == 3);
    WsOperation reuse = repair;
    reuse.amount = 2; /* a different op under a consumed sequence is stale */
    CHECK(ws_apply(&s, &r, at_player(1, 22000, 4300, -96500), reuse).status == WS_STALE);
    CHECK(ws_state_hash(&s) == hash);
    /* Already-intact station: a genuinely new repair is denied atomically. */
    WsOperation again = mkop(WS_REPAIR, STATION, 2, 1, 0);
    CHECK(ws_apply(&s, &r, at_player(1, 22000, 4300, -96500), again).status == WS_DENIED);
    CHECK(ws_state_hash(&s) == hash);

    /* 3. A damage/repair cycle from inside the footprint: the act is still
       committed (server authority) and witnessed through the doorway. */
    CHECK(ws_apply(&s, &r, at_player(1, 22000, 4300, -104000), mkop(WS_DAMAGE, STATION, 3, 1, 0)).status == WS_OK);
    CHECK(ws_apply(&s, &r, at_player(1, 22000, 4300, -104000), mkop(WS_REPAIR, STATION, 4, 1, 0)).status == WS_OK);
    CHECK(s.entities[STATION].health == 100 && s.players[0].credits == 20 && s.players[0].inventory[1] == 3);
    CHECK(s.ev_count == 9); /* three acts, each witnessed by all three through the doorway */

    /* 4. SHOW/TELL/EXCHANGE: committed private canonical events. The tail
       grows; the public feed does not. */
    uint16_t feeds = s.feed_count;
    s.players[0].inventory[2] = 1; /* a held item to show */
    CHECK(ws_apply(&s, &r, at_player(0, 22000, 4300, -95200), mkop(WS_SHOW, KYRA, 5, 0, 2)).status == WS_OK);
    CHECK(s.feed_count == feeds); /* private conversation, not news */
    CHECK(s.ev[s.ev_count - 1].observer == H_NPC(KYRA) && s.ev[s.ev_count - 1].subject == H_PLAYER && s.ev[s.ev_count - 1].teller == H_PLAYER && s.ev[s.ev_count - 1].kind == WS_EV_ACT && s.ev[s.ev_count - 1].confidence == 1000 && s.ev[s.ev_count - 1].context == WS_CTX_SHOWN && s.ev[s.ev_count - 1].valence == 50);
    CHECK(ws_apply(&s, &r, at_player(0, 22000, 4300, -95200), mkop(WS_TELL, KYRA, 6, 0, H_NPC(DAX))).status == WS_OK);
    CHECK(s.feed_count == feeds);
    /* The claim is about dax; the telling itself is separately witnessed. */
    CHECK(ws_social_view(&s, H_NPC(KYRA), H_NPC(DAX), &v) && v.claims == 1 && v.trust == 0 && v.confidence == 500);
    CHECK(ws_apply(&s, &r, at_player(0, 22000, 4300, -95200), mkop(WS_EXCHANGE, KYRA, 7, 2, 0x8000u | 2)).status == WS_OK);
    CHECK(s.feed_count == feeds);
    /* Kyra directly saw every act: the repair, the damage, the repair, the
       showing and its presentation record, the telling, and the notarized
       exchange (whose own record replaces the same-root plain projection:
       one cause, one capsule). */
    CHECK(ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &v) && v.acts == 7 && v.trust == 481 && v.confidence == 1000);
    /* Client locality: the same exchange from out of range is denied. */
    CHECK(ws_apply(&s, &r, at_player(0, 80000, 4300, -95200), mkop(WS_EXCHANGE, KYRA, 8, 2, 0x8000u | 2)).status == WS_DENIED);

    /* 5. The NPC-to-NPC example (contract 6.7): dax's false claim, kyra's
       stored claim, marisol's contradiction, oren unchanged, the clerk
       informed only through a delayed filed report, then restitution. */
    uint32_t claim_root = s.revision;
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), H_NPC(DAX), WS_EV_CLAIM, 500, WS_CTX_NONE, 0, 400, claim_root) == WS_OK);
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), H_NPC(MARISOL), WS_EV_CONTRADICT, 900, WS_CTX_NONE, -300, 600, claim_root) == WS_OK);
    CHECK(ws_social_view(&s, H_NPC(KYRA), H_NPC(DAX), &v) && v.claims == 2 && v.contradictions == 1 && v.trust == -270);
    CHECK(ws_social_view(&s, H_NPC(OREN), H_NPC(DAX), &v) && v.claims == 0 && v.trust == 0); /* oren heard nothing */
    CHECK(ws_social_cite(&s, H_NPC(KYRA), H_NPC(DAX), &root, &kind, &sal) && kind == WS_EV_CONTRADICT && sal == 600);
    /* Kyra files with the clerk: the dispatch is pending, not delivered. */
    CHECK(ws_file(&s, &r, H_NPC(CLERK), H_NPC(KYRA), H_NPC(DAX)) == WS_OK);
    CHECK(s.pend_count == 1 && s.pend[0].observer == H_NPC(CLERK) && s.pend[0].teller == H_NPC(KYRA) && s.pend[0].salience == 600);
    hash = ws_state_hash(&s);
    ws_clock_advance(&s, &r, 3599);
    CHECK(s.pend_count == 1);
    CHECK(ws_social_view(&s, H_NPC(CLERK), H_NPC(DAX), &v) && v.reports == 0); /* the guild is not yet informed */
    ws_clock_advance(&s, &r, 1);
    CHECK(s.pend_count == 0);
    CHECK(s.feed_count == feeds + 1 && s.feed[s.feed_count - 1].kind == WS_FACTION_NOTICE); /* public receipt */
    CHECK(ws_social_view(&s, H_NPC(CLERK), H_NPC(DAX), &v) && v.reports == 1 && v.trust == -202 && v.confidence == 675);
    /* Repeated filing of the same cause: delivered records replace, they
       never stack corroboration. */
    CHECK(ws_file(&s, &r, H_NPC(CLERK), H_NPC(KYRA), H_NPC(DAX)) == WS_OK);
    ws_clock_advance(&s, &r, 3600);
    CHECK(ws_social_view(&s, H_NPC(CLERK), H_NPC(DAX), &v) && v.reports == 1 && v.trust == -202);
    /* Restitution adds evidence; the claim and contradiction remain. */
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), 0, WS_EV_RESTITUTION, 800, WS_CTX_RESTITUTION, 300, 500, claim_root) == WS_OK);
    CHECK(ws_social_view(&s, H_NPC(KYRA), H_NPC(DAX), &v) && v.claims == 2 && v.contradictions == 1 && v.restitutions == 1 && v.trust == 210);

    /* 6. Context re-interpretation: consensual force counts half. */
    CHECK(ws_inform(&s, &r, H_NPC(TOMA), H_PLAYER, 0, WS_EV_ACT, 1000, WS_CTX_NONE, -300, 0, claim_root) == WS_OK);
    CHECK(ws_inform(&s, &r, H_NPC(TOMA), H_PLAYER, 0, WS_EV_ACT, 1000, WS_CTX_ARENA, -300, 0, claim_root + 1) == WS_OK);
    CHECK(ws_social_view(&s, H_NPC(TOMA), H_PLAYER, &v) && v.trust == -450);

    /* 7. Decay is a fixed integer bucket per record against the stored
       anchor; query frequency cannot change it and pinned history stays. */
    WsView a, b, c;
    ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &a);
    ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &b);
    ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &c);
    CHECK(a.trust == b.trust && b.trust == c.trust); /* identical across queries */
    int16_t before = a.trust;
    ws_clock_advance(&s, &r, 86400);
    ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &v);
    CHECK(v.trust < before);                     /* minor evidence decayed one bucket */
    ws_social_view(&s, H_NPC(KYRA), H_NPC(DAX), &v);
    CHECK(v.trust == 210); /* pinned claim/contradiction/restitution survive */
    ws_clock_advance(&s, &r, 20u * 86400u);
    ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &v);
    CHECK(v.trust == 0 && v.confidence == 0); /* minor history is gone */
    ws_social_view(&s, H_NPC(KYRA), H_NPC(DAX), &v);
    CHECK(v.trust == 210 && v.restitutions == 1); /* pinned history remains */

    /* 8. A player files a formal report with the clerk in person: the
       delivered record keeps the teller's provenance and claim grade. */
    CHECK(ws_join(&s, 102) == WS_OK);
    CHECK(ws_apply(&s, &r, at_player(0, 86200, 30300, 30000), mkop(WS_REPORT, CLERK, 9, 0, H_NPC(DAX))).status == WS_OK);
    CHECK(s.pend_count == 1 && s.pend[0].teller == H_PLAYER && s.pend[0].context == WS_CTX_DISPATCH);
    ws_clock_advance(&s, &r, 3600);
    CHECK(ws_social_view(&s, H_NPC(CLERK), H_NPC(DAX), &v) && v.reports == 2 && v.claims == 0);
    /* Reported claims stay claim-grade: copying never becomes certainty. */
    CHECK(s.ev[s.ev_count - 1].kind == WS_EV_REPORT && s.ev[s.ev_count - 1].confidence == 375 && s.ev[s.ev_count - 1].teller == H_PLAYER);

    /* 9. Checkpoint compaction: encode/decode and save/restore reproduce
       the full social state bit for bit, views included. */
    size_t n = ws_state_encode(&s, bytes, sizeof(bytes));
    CHECK(n > 0 && bytes[4] == 4 && bytes[5] == 0); /* wire v4 */
    CHECK(ws_state_decode(&loaded, &r, bytes, n) == WS_OK);
    CHECK(ws_state_hash(&s) == ws_state_hash(&loaded));
    WsView v2;
    for (uint32_t obs = 1; obs <= 23; obs++)
        for (uint32_t sub = 1; sub <= 23; sub++) {
            int fa = ws_social_view(&s, obs, sub, &v), fb = ws_social_view(&loaded, obs, sub, &v2);
            CHECK(fa == fb && v.trust == v2.trust && v.acts == v2.acts && v.claims == v2.claims && v.contradictions == v2.contradictions && v.restitutions == v2.restitutions && v.reports == v2.reports && v.confidence == v2.confidence && v.watermark == v2.watermark);
        }
    for (size_t i = 0; i < n; i++) CHECK(ws_state_decode(&loaded, &r, bytes, i) != WS_OK); /* truncated fails closed */
    bytes[n - 1] ^= 1;
    CHECK(ws_state_decode(&loaded, &r, bytes, n) == WS_FORMAT);
    bytes[n - 1] ^= 1;
    remove("build/social-state.0");
    remove("build/social-state.1");
    CHECK(ws_save(&s, "build/social-state"));
    CHECK(ws_restore(&loaded, &r, "build/social-state") && ws_state_hash(&loaded) == ws_state_hash(&s));
    ws_social_view(&s, H_NPC(KYRA), H_NPC(DAX), &v);
    ws_social_view(&loaded, H_NPC(KYRA), H_NPC(DAX), &v2);
    CHECK(v.trust == v2.trust && v.claims == v2.claims);

    /* 10. Bounded memory: the evidence table is a ring; eviction keeps the
       newest causes and the citation stays honest about the rest. */
    for (int i = 0; i < 80; i++) {
        s.revision++; /* each inform follows a committed revision */
        CHECK(ws_inform(&s, &r, H_NPC(OREN), H_NPC(MARISOL), H_NPC(DAX), WS_EV_ACT, 1000, WS_CTX_NONE, 100, 0, s.revision) == WS_OK);
    }
    CHECK(s.ev_count == WS_EV_CAP);
    CHECK(ws_social_view(&s, H_NPC(OREN), H_NPC(MARISOL), &v) && v.acts == WS_EV_CAP);
    CHECK(ws_social_view(&s, H_NPC(KYRA), H_NPC(DAX), &v) && v.trust == 0); /* evicted: honest empty limit */
    CHECK(ws_social_cite(&s, H_NPC(KYRA), H_NPC(DAX), &root, &kind, &sal) == 0);
    CHECK(ws_state_validate(&s, &r) == WS_OK);

    /* 11. Fail-closed authority boundaries. */
    hash = ws_state_hash(&s);
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), 0, 0, 500, WS_CTX_NONE, 0, 0, s.revision) == WS_BOUNDS);            /* kind 0 */
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), 0, WS_EV_REPORT + 1, 500, WS_CTX_NONE, 0, 0, s.revision) == WS_BOUNDS); /* kind past range */
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), 0, WS_EV_ACT, 1001, WS_CTX_NONE, 0, 0, s.revision) == WS_BOUNDS);   /* confidence */
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), 0, WS_EV_ACT, 500, WS_CTX_NONE, 0, 1001, s.revision) == WS_BOUNDS); /* salience */
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), 99, 0, WS_EV_ACT, 500, WS_CTX_NONE, 0, 0, s.revision) == WS_REFERENCE);         /* unknown subject */
    CHECK(ws_inform(&s, &r, 99, H_NPC(DAX), 0, WS_EV_ACT, 500, WS_CTX_NONE, 0, 0, s.revision) == WS_REFERENCE);          /* unknown observer */
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), H_NPC(KYRA), WS_EV_ACT, 500, WS_CTX_NONE, 0, 0, s.revision) == WS_BOUNDS); /* self-report */
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), 0, WS_EV_ACT, 500, WS_CTX_NONE, 0, 0, 0) == WS_REFERENCE);  /* root 0 */
    CHECK(ws_inform(&s, &r, H_NPC(KYRA), H_NPC(DAX), 0, WS_EV_ACT, 500, WS_CTX_NONE, 0, 0, s.revision + 1) == WS_REFERENCE); /* future root */
    CHECK(ws_file(&s, &r, H_PLAYER, H_NPC(KYRA), H_NPC(DAX)) == WS_REFERENCE);  /* clerk must be an NPC */
    CHECK(ws_file(&s, &r, H_NPC(CLERK), H_NPC(OREN), H_NPC(DAX)) == WS_DENIED); /* nothing retained to file */
    CHECK(ws_state_hash(&s) == hash && ws_state_validate(&s, &r) == WS_OK);

    /* 12. Promises stay directed relationships, separate storage from the
       conduct/pattern capsules (acceptance condition 6). */
    CHECK(ws_apply(&s, &r, at_player(0, 22000, 4300, -110000), mkop(WS_PROMISE, FIELD, 10, 0, 102)).status == WS_OK);
    CHECK(s.relations[1][0].promise == 1);
    uint16_t claims_before = 0;
    ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &v);
    claims_before = v.claims;
    CHECK(ws_apply(&s, &r, at_player(0, 22000, 4300, -110000), mkop(WS_KEEP_PROMISE, FIELD, 11, 0, 102)).status == WS_OK);
    CHECK(s.relations[1][0].promise == 0 && s.relations[1][0].reliability == 10 && s.relations[1][0].confidence == 10);
    ws_social_view(&s, H_NPC(KYRA), H_PLAYER, &v);
    CHECK(v.claims == claims_before); /* relationship machinery never wrote conduct capsules */
    CHECK(ws_state_validate(&s, &r) == WS_OK);

    size_t wire = ws_state_encode(&s, bytes, sizeof(bytes));
    printf("{\"stage\":\"social\",\"passed\":%u,\"product_bytes\":%zu,\"modules\":%u,\"evidence\":%u,\"state_bytes\":%zu,\"wire_bytes\":%zu}\n",
           checks, sizeof(ws_product), r.count, s.ev_count, sizeof(WsState), wire);
    return 0;
}
