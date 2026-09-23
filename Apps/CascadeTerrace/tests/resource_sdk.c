/* Gate 3 (regional resources and ecology, mission resource rules). The
   product under test is the committed generator output (content/worlds/
   macro.inc, schema 3: four geography-derived reservoirs). Everything here
   is proven against the C engine with exact expected numbers:

   - excavation persists: an intentional EXCAVATION site never heals, while
     recoverable PIT disturbance heals from the recovering regional stock
     (buried matter is accounted, never lost silently);
   - material to Geo-phos conversion is bounded and lossy in both directions
     (3:1 refine, 2:1 deposit; no duplication);
   - water overdraw depletes the lake country visibly: river stage width and
     depth shrink with the regional level, the lake/wetland reach dries to a
     marsh band below a quarter stock and to a dry bed at zero, while
     non-lake reaches in the same region keep their strip; deterministic
     rainfall recovers the level day by day;
   - Phos depletion recharges through the same tick, with pit healing;
   - biomass harvests leave no topology scar and regrow by weather and zone;
   - the conservation ledger identity holds after every operation and tick,
     across save/restore compaction and the wire version 2 format, while
     schema 1 states keep the published byte layout (tests/world_state.c);
   - resource ops are fail-closed: bad targets, dust conversions, replays,
     non-local draws and carry overflow all reject transactionally;
   - stage lookups are pure functions of position: every quadrant chunk
     whose window intersects the water region reports identical samples. */
#include "../world/state.h"
static unsigned checks;
#define CHECK(x)                                                 \
    do {                                                         \
        checks++;                                                \
        if (!(x)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
            return 1;                                            \
        }                                                        \
    } while (0)
#include "../content/worlds/macro.inc"
#include <stdio.h>
#include <string.h>

/* Reservoir indices in generator order: terrain, water, biomass, Phos. */
enum { R_SPOIL, R_LAKE, R_FOREST, R_KARST };
/* Days 0..20 of deterministic weather for seed 0x9C0D51 (0 clear 1 rain
   2 storm), cross-checked against ws_weather below. */
static const uint32_t WEATHER[21] = {0, 1, 0, 1, 2, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 2, 1, 2, 2, 1, 2};

static WsRecipe a, b;
static WsState s;

static WsContext where(int32_t x, int32_t y, int32_t z, int server) {
    return (WsContext) {0x5151, {{x, y, z}, UINT16_MAX}, (uint8_t)server};
}

static WsContext center_of(int i, int server) {
    const WsReservoir* v = &a.reservoirs[i];
    return where((v->lo.x + v->hi.x) / 2, (v->lo.y + v->hi.y) / 2, (v->lo.z + v->hi.z) / 2, server);
}

static WsOperation rop(uint16_t action, uint16_t target, uint32_t seq, uint16_t amount, uint16_t aux) {
    WsOperation op = {seq, 1, s.revision, action, target, amount, aux};
    return op;
}

static WsDisposition play(uint16_t action, uint16_t target, uint32_t seq, uint16_t amount, uint16_t aux, WsContext c) {
    /* Every ws_apply re-validates the state (ledger included) at entry, so
       a broken invariant makes the next op fail; each case also checks the
       ledger explicitly at its end, and every tick loop validates inline. */
    return ws_apply(&s, &a, c, rop(action, target, seq, amount, aux));
}

/* Stage at t must report exactly these width/depth/surfaced values for the
   current water level (expected numbers derived independently of the C). */
static int stage_is(uint16_t t, uint32_t width, uint32_t depth, uint32_t surfaced) {
    WsRiverSample g;
    CHECK(ws_river_stage(&a, &s, 0, t, &g));
    CHECK(g.width == width && g.depth == depth && g.surfaced == surfaced);
    return 1;
}

int main(void) {
    CHECK(ws_load(&a, ws_macro_product, sizeof(ws_macro_product)) == WS_OK);
    CHECK(ws_load(&b, ws_macro_product, sizeof(ws_macro_product)) == WS_OK);
    CHECK(memcmp(&a, &b, sizeof(WsRecipe)) == 0);
    /* --- product records: four geography-derived reservoirs --- */
    CHECK(a.reservoir_count == 4);
    CHECK(a.reservoirs[R_SPOIL].kind == WS_RES_TERRAIN && a.reservoirs[R_SPOIL].zone == 2);
    CHECK(a.reservoirs[R_SPOIL].capacity == 400000 && a.reservoirs[R_SPOIL].level == 400000 && a.reservoirs[R_SPOIL].rate == 2500);
    CHECK(a.reservoirs[R_LAKE].kind == WS_RES_WATER && a.reservoirs[R_LAKE].zone == 1);
    CHECK(a.reservoirs[R_LAKE].capacity == 300000 && a.reservoirs[R_LAKE].level == 300000 && a.reservoirs[R_LAKE].rate == 12000);
    CHECK(a.reservoirs[R_FOREST].kind == WS_RES_BIOMASS && a.reservoirs[R_FOREST].zone == 1);
    CHECK(a.reservoirs[R_FOREST].capacity == 200000 && a.reservoirs[R_FOREST].level == 200000 && a.reservoirs[R_FOREST].rate == 15000);
    CHECK(a.reservoirs[R_KARST].kind == WS_RES_PHOS && a.reservoirs[R_KARST].zone == 3);
    CHECK(a.reservoirs[R_KARST].capacity == 160000 && a.reservoirs[R_KARST].level == 160000 && a.reservoirs[R_KARST].rate == 8000);
    /* Anchoring: the cave is carved inside the massif spoil region and the
       lake reach lives inside its water field. */
    CHECK(a.reservoirs[R_SPOIL].lo.x <= -223315 && -223315 <= a.reservoirs[R_SPOIL].hi.x);
    CHECK(a.reservoirs[R_SPOIL].lo.z <= -565000 && -565000 <= a.reservoirs[R_SPOIL].hi.z);
    {
        int hits = 0;
        for (uint32_t t = 32767; t <= 39975; t += 512) {
            WsRiverSample g;
            CHECK(ws_river_sample(&a, 0, (uint16_t)t, &g));
            const WsReservoir* v = &a.reservoirs[R_LAKE];
            if (g.pos.x >= v->lo.x && g.pos.x <= v->hi.x && g.pos.z >= v->lo.z && g.pos.z <= v->hi.z) hits++;
        }
        CHECK(hits >= 14);
    }
    /* Weather: deterministic, matches the committed schedule, and the first
       60 days cover clear, rain and storm. */
    {
        int seen[3] = {0, 0, 0};
        for (uint32_t d = 0; d <= 20; d++) CHECK(ws_weather(&a, d) == WEATHER[d]);
        for (uint32_t d = 0; d < 60; d++) seen[ws_weather(&a, d)] = 1;
        CHECK(seen[0] && seen[1] && seen[2]);
        CHECK(ws_weather(&a, 7) == ws_weather(&b, 7));
    }

    /* --- case 1: excavation, conversion, time advance (the proof) --- */
    ws_state_init(&s, &a);
    CHECK(s.reservoir_count == 4 && s.site_count == 0 && s.clock_s == 0);
    CHECK(s.level[R_SPOIL] == 400000 && s.level[R_LAKE] == 300000 && s.level[R_FOREST] == 200000 && s.level[R_KARST] == 160000);
    CHECK(ws_ledger_check(&s, &a) && ws_state_validate(&s, &a) == WS_OK);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    /* pit: recoverable disturbance (client stands inside the region) */
    CHECK(play(WS_EXCAVATE, R_SPOIL, 1, 30000, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(s.material[0] == 30000 && s.level[R_SPOIL] == 370000 && s.site_count == 1);
    CHECK(s.sites[0].kind == WS_SITE_PIT && s.sites[0].amount == 30000 && s.sites[0].reservoir == R_SPOIL);
    CHECK(s.sites[0].extent == 30000);
    /* intentional topology at the cave mouth: never heals */
    CHECK(play(WS_EXCAVATE, R_SPOIL, 2, 5000, 1, where(-223315, 90300, -565000, 0)).status == WS_OK);
    CHECK(s.material[0] == 35000 && s.level[R_SPOIL] == 365000 && s.site_count == 2);
    CHECK(s.sites[1].kind == WS_SITE_EXCAVATION && s.sites[1].amount == 5000);
    /* bounded lossy conversion 3:1, then spend some shards */
    CHECK(play(WS_CONVERT, R_SPOIL, 3, 30000, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(s.material[0] == 5000 && s.shards[0] == 10000 && s.lost_total == 20000);
    CHECK(play(WS_SPEND, R_SPOIL, 4, 4000, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(s.shards[0] == 6000 && s.used_total == 4000);
    /* advance 20 days: the pit heals out of the recovering stock on day 1
       (buried matter is lost_total, not vanished), the terrain then refills
       to capacity by day 9; the excavation site persists throughout */
    for (int day = 1; day <= 20; day++) {
        ws_resources_tick(&s, &a, 86400);
        CHECK(ws_state_validate(&s, &a) == WS_OK && ws_ledger_check(&s, &a));
        if (day == 1) {
            CHECK(s.level[R_SPOIL] == 342500 && s.recovered_total == 7500);
            CHECK(s.lost_total == 50000 && s.site_count == 1);
            CHECK(s.sites[0].kind == WS_SITE_EXCAVATION && s.sites[0].amount == 5000);
        }
    }
    CHECK(s.clock_s == 20u * 86400);
    CHECK(s.level[R_SPOIL] == 400000 && s.recovered_total == 65000);
    CHECK(s.lost_total == 50000); /* 20000 conversion loss + 30000 buried */
    CHECK(s.site_count == 1 && s.sites[0].kind == WS_SITE_EXCAVATION && s.sites[0].amount == 5000);
    CHECK(s.material[0] == 5000 && s.shards[0] == 6000 && s.used_total == 4000);
    /* identity: 400000+300000+200000+160000 + 5000 + 6000 + 4000 + 50000
       == 1060000 initial + 65000 recovered */
    CHECK(ws_ledger_check(&s, &a));
    /* the ledger catches corruption: one stolen unit breaks it */
    {
        WsState t = s;
        t.level[R_LAKE] -= 1;
        CHECK(!ws_ledger_check(&t, &a));
    }

    /* --- case 2: water overdraw, visible drawdown, rainfall recovery --- */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(stage_is(36000, 48000, 2700, 1)); /* full: the declared lake reach */
    for (int i = 1; i <= 4; i++)
        CHECK(play(WS_EXCAVATE, R_LAKE, (uint32_t)i, 65535, 0, center_of(R_LAKE, 0)).status == WS_OK);
    CHECK(s.level[R_LAKE] == 37860 && s.used_total == 4 * 65535 && s.site_count == 0);
    /* drawdown visibility, observed at each recorded level (pure reads; the
       level is restored before the next op so the ledger stays exact) */
    s.level[R_LAKE] = 234465;
    CHECK(stage_is(36000, 37513, 2110, 1));
    s.level[R_LAKE] = 168930;
    CHECK(stage_is(36000, 27028, 1520, 1));
    s.level[R_LAKE] = 103395;
    CHECK(stage_is(36000, 16542, 930, 1));
    s.level[R_LAKE] = 37860;
    CHECK(stage_is(36000, 6057, 340, 0)); /* below a quarter: marsh band */
    /* the fifth draw overdraws to depletion: partial take, no site */
    CHECK(play(WS_EXCAVATE, R_LAKE, 5, 65535, 0, center_of(R_LAKE, 0)).status == WS_OK);
    CHECK(s.level[R_LAKE] == 0 && s.used_total == 300000 && s.site_count == 0);
    CHECK(stage_is(36000, 48000, 2700, 0)); /* dry bed: strip not rendered */
    CHECK(play(WS_EXCAVATE, R_LAKE, 6, 100, 0, center_of(R_LAKE, 0)).status == WS_DENIED); /* empty */
    /* a client outside the region cannot draw from it */
    CHECK(play(WS_EXCAVATE, R_LAKE, 6, 1000, 0, center_of(R_SPOIL, 0)).status == WS_DENIED);
    CHECK(s.level[R_LAKE] == 0 && s.used_total == 300000);
    /* rainfall recovery day by day (days 1..10: rain clear rain storm clear
       rain clear rain clear clear); the lake refills to capacity on day 8 */
    static const uint32_t after_day[10] = {48000, 72000, 120000, 192000, 216000, 264000, 288000, 300000, 300000, 300000};
    for (int day = 1; day <= 10; day++) {
        ws_resources_tick(&s, &a, 86400);
        CHECK(s.level[R_LAKE] == after_day[day - 1]);
        CHECK(ws_state_validate(&s, &a) == WS_OK && ws_ledger_check(&s, &a));
    }
    CHECK(s.recovered_total == 300000);
    CHECK(stage_is(36000, 48000, 2700, 1)); /* recovered: strip restored */
    /* marsh discrimination at 72000: the lake reach dries, a plain reach
       inside the same water region keeps its (narrower) strip */
    s.level[R_LAKE] = 72000;
    CHECK(stage_is(36000, 11519, 647, 0));
    CHECK(stage_is(31456, 2879, 215, 1));
    s.level[R_LAKE] = 120000;
    CHECK(stage_is(36000, 19199, 1079, 1));
    s.level[R_LAKE] = 300000; /* restore the true level */
    /* server authority draws from anywhere; a client still cannot */
    CHECK(play(WS_EXCAVATE, R_LAKE, 7, 1000, 0, center_of(R_SPOIL, 1)).status == WS_OK);
    CHECK(play(WS_EXCAVATE, R_LAKE, 8, 1000, 0, center_of(R_SPOIL, 0)).status == WS_DENIED);
    CHECK(s.level[R_LAKE] == 299000 && s.used_total == 301000);
    CHECK(ws_ledger_check(&s, &a));
    /* NULL or empty state reports declared geometry (backward compatible) */
    {
        WsRiverSample g;
        CHECK(ws_river_stage(&a, NULL, 0, 36000, &g));
        CHECK(g.width == 48000 && g.depth == 2700 && g.surfaced == 1);
        WsState empty;
        memset(&empty, 0, sizeof(empty));
        CHECK(ws_river_stage(&a, &empty, 0, 36000, &g));
        CHECK(g.width == 48000 && g.depth == 2700 && g.surfaced == 1);
    }

    /* --- case 3: Phos depletion and recharge with pit healing --- */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(play(WS_EXCAVATE, R_KARST, 1, 50000, 0, center_of(R_KARST, 0)).status == WS_OK);
    CHECK(s.shards[0] == 50000 && s.level[R_KARST] == 110000 && s.site_count == 1);
    CHECK(s.sites[0].kind == WS_SITE_PIT && s.sites[0].amount == 50000);
    /* days 1..4: +32000, heal 50000, then +32000, +32000, +4000 (storm day
       4 doubles the Phos rate); the leached pit fully heals on day 1 */
    for (int day = 1; day <= 4; day++) {
        ws_resources_tick(&s, &a, 86400);
        CHECK(ws_state_validate(&s, &a) == WS_OK && ws_ledger_check(&s, &a));
    }
    CHECK(s.level[R_KARST] == 160000 && s.recovered_total == 100000 && s.lost_total == 50000);
    CHECK(s.site_count == 0);
    for (int day = 5; day <= 6; day++) ws_resources_tick(&s, &a, 86400);
    CHECK(s.level[R_KARST] == 160000 && s.recovered_total == 100000); /* full: runoff */

    /* --- case 4: biomass harvest leaves no scar, regrows by weather --- */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(play(WS_EXCAVATE, R_FOREST, 1, 20000, 0, center_of(R_FOREST, 0)).status == WS_OK);
    CHECK(s.material[0] == 20000 && s.level[R_FOREST] == 180000 && s.site_count == 0);
    ws_resources_tick(&s, &a, 86400); /* day 1 rain: 60000 inflow, 20000 room */
    CHECK(s.level[R_FOREST] == 200000 && s.recovered_total == 20000);
    for (int day = 2; day <= 5; day++) ws_resources_tick(&s, &a, 86400);
    CHECK(s.level[R_FOREST] == 200000 && s.recovered_total == 20000 && s.site_count == 0);
    CHECK(ws_ledger_check(&s, &a));

    /* --- case 5: compaction, persistence, merge and fail-closed ops --- */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(play(WS_EXCAVATE, R_SPOIL, 1, 30000, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(play(WS_EXCAVATE, R_SPOIL, 2, 5000, 1, where(-223315, 90300, -565000, 0)).status == WS_OK);
    CHECK(play(WS_CONVERT, R_SPOIL, 3, 30000, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(play(WS_SPEND, R_SPOIL, 4, 4000, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    /* wire version 2, exact resource layout, round trip identity */
    {
        uint8_t wire[12000];
        size_t n = ws_state_encode(&s, wire, sizeof(wire));
        size_t base = 48 + 21 * 28 + 1 * 538 + 1 * 12 + 4 * 16 + 4 * 20;
        CHECK(n == base + 8 + 16 + 4 + 2 * 24 + 24);
        CHECK(wire[4] == 2 && wire[5] == 0); /* wire version 2 marker */
        WsState t;
        CHECK(ws_state_decode(&t, &a, wire, n) == WS_OK);
        CHECK(ws_state_hash(&t) == ws_state_hash(&s));
        CHECK(t.site_count == 2 && t.sites[1].kind == WS_SITE_EXCAVATION && t.sites[1].amount == 5000);
        CHECK(t.material[0] == 5000 && t.shards[0] == 6000 && t.lost_total == 20000);
        CHECK(t.clock_s == s.clock_s && t.reservoir_count == 4);
        CHECK(ws_state_decode(&t, &a, wire, n - 1) != WS_OK); /* truncation */
        CHECK(ws_save(&s, "build/resource-state"));
        WsState loaded;
        CHECK(ws_restore(&loaded, &a, "build/resource-state"));
        CHECK(ws_state_hash(&loaded) == ws_state_hash(&s));
        CHECK(loaded.site_count == 2 && loaded.clock_s == s.clock_s);
    }
    /* resource ops never merge offline */
    {
        WsState base_s = s;
        WsOperation op = {9, 1, s.revision, WS_EXCAVATE, R_SPOIL, 1000, 0};
        CHECK(ws_merge(&s, &base_s, &a, center_of(R_SPOIL, 0), op).status == WS_DENIED);
    }
    /* fail-closed: bad target, zero amount, wrong epoch, replay, stale
       base, unknown convert direction, dust, no stock, over-spend */
    CHECK(play(WS_EXCAVATE, 4, 9, 1000, 0, center_of(R_SPOIL, 0)).status == WS_DENIED);
    CHECK(play(WS_EXCAVATE, R_SPOIL, 9, 0, 0, center_of(R_SPOIL, 0)).status == WS_DENIED);
    {
        WsOperation op = {9, 2, s.revision, WS_EXCAVATE, R_SPOIL, 1000, 0};
        CHECK(ws_apply(&s, &a, center_of(R_SPOIL, 0), op).status == WS_DENIED);
        op.sequence = 4; /* replayed sequence */
        op.epoch = 1;
        CHECK(ws_apply(&s, &a, center_of(R_SPOIL, 0), op).status == WS_STALE);
        op.sequence = 9;
        op.base_revision = s.revision + 1; /* not the current branch */
        CHECK(ws_apply(&s, &a, center_of(R_SPOIL, 0), op).status == WS_STALE);
    }
    CHECK(play(WS_CONVERT, R_SPOIL, 10, 30000, 2, center_of(R_SPOIL, 0)).status == WS_DENIED);
    CHECK(play(WS_CONVERT, R_SPOIL, 10, 2, 0, center_of(R_SPOIL, 0)).status == WS_DENIED); /* dust */
    CHECK(play(WS_CONVERT, R_SPOIL, 10, 30000, 0, center_of(R_SPOIL, 0)).status == WS_DENIED); /* no stock */
    CHECK(play(WS_SPEND, R_SPOIL, 10, 6001, 0, center_of(R_SPOIL, 0)).status == WS_DENIED); /* over-spend */
    CHECK(s.shards[0] == 6000 && s.material[0] == 5000); /* nothing applied */
    /* transactional carry overflow: level and sites unchanged */
    CHECK(play(WS_EXCAVATE, R_SPOIL, 11, 60000, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(s.material[0] == 65000);
    {
        uint32_t level_before = s.level[R_SPOIL];
        unsigned sites_before = s.site_count;
        CHECK(play(WS_EXCAVATE, R_SPOIL, 12, 1000, 0, center_of(R_SPOIL, 0)).status == WS_FULL);
        CHECK(s.level[R_SPOIL] == level_before && s.site_count == sites_before);
    }
    /* bounded lossy round trip: 6 material -> 2 shards -> 1 material */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(play(WS_EXCAVATE, R_SPOIL, 1, 6, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(play(WS_CONVERT, R_SPOIL, 2, 6, 0, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(s.material[0] == 0 && s.shards[0] == 2 && s.lost_total == 4);
    CHECK(play(WS_CONVERT, R_SPOIL, 3, 2, 1, center_of(R_SPOIL, 0)).status == WS_OK);
    CHECK(s.material[0] == 1 && s.shards[0] == 0 && s.lost_total == 5);
    CHECK(ws_ledger_check(&s, &a) && ws_state_validate(&s, &a) == WS_OK);
    /* tail and feed recorded the resource history with reservoir targets */
    CHECK(s.tail_count >= 3 && s.tail[0].action == WS_EXCAVATE && s.tail[2].action == WS_CONVERT);
    CHECK(s.feed_count >= 3 && s.feed[0].kind == WS_DISCOVERY);
    CHECK(s.feed[0].target == R_SPOIL && s.feed[0].target < s.reservoir_count);

    /* --- cross-chunk: every quadrant window intersecting the water region
       reconstructs identical staged samples (pure function of position) --- */
    {
        WsPos quads[4][2] = {
            {{-1000000, -1000000, -1000000}, {0, 1000000, 0}},
            {{0, -1000000, -1000000}, {1000000, 1000000, 0}},
            {{-1000000, -1000000, 0}, {0, 1000000, 1000000}},
            {{0, -1000000, 0}, {1000000, 1000000, 1000000}},
        };
        ws_state_init(&s, &a);
        CHECK(ws_join(&s, 0x5151) == WS_OK);
        s.level[R_LAKE] = 150000; /* half stock: width visibly scaled */
        int covering = 0;
        for (int q = 0; q < 4; q++) {
            uint16_t t0, t1;
            if (!ws_river_window(&a, 0, quads[q][0], quads[q][1], &t0, &t1)) continue;
            for (uint32_t t = 32767; t <= 39975; t += 1024) {
                if ((uint16_t)t < t0 || (uint16_t)t > t1) continue;
                WsRiverSample g, h;
                CHECK(ws_river_stage(&a, &s, 0, (uint16_t)t, &g));
                CHECK(ws_river_stage(&a, &s, 0, (uint16_t)t, &h));
                CHECK(memcmp(&g, &h, sizeof(g)) == 0);
                CHECK(ws_river_sample(&a, 0, (uint16_t)t, &h));
                if (g.surfaced) {
                    /* scaled inside the water field, declared outside it */
                    const WsReservoir* v = &a.reservoirs[R_LAKE];
                    int inside = h.pos.x >= v->lo.x && h.pos.x <= v->hi.x && h.pos.z >= v->lo.z && h.pos.z <= v->hi.z;
                    CHECK(inside ? (g.width != h.width || g.depth != h.depth) : (g.width == h.width && g.depth == h.depth));
                }
                covering++;
            }
        }
        CHECK(covering >= 16); /* the water region spans chunk windows */
        CHECK(stage_is(36000, 24000, 1350, 1)); /* rho 32768 at half stock */
    }

    printf("{\"stage\":\"resource\",\"passed\":%u,\"product_bytes\":%zu,\"reservoirs\":%u,\"state_bytes\":%zu}\n", checks, sizeof(ws_macro_product), a.reservoir_count, sizeof(WsState));
    return 0;
}
