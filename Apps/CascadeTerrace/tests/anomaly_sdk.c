/* Gate 4 (nonlocal Phos topology). The product under test is the committed
   generator output (content/worlds/macro.inc, schema 3): one deterministic
   Phos-rich ruin (phos_ruin) on the karst ridge top, linked to the
   upper-valley ruin by a typed anomaly gate anchored to the karst Phos
   reservoir. Everything here is proven against the C engine with exact
   expected numbers:

   - the gate is declared topology (link kind 3, anchor = Phos reservoir
     index), its seat inside the anchored region and its far end outside,
     conventionally nonadjacent (the closed-gate ordinary route is three
     links: two walks and the winze lift; the direct walk the gate would
     replace is unrealizable through the karst flank, which is why the
     ordinary access is a lift);
   - ordinary geography is preserved exactly: the Gate 2 cost-case literals
     are unchanged, walk-only planning never sees the gate, and the
     stateless link scan serves the same lift as before;
   - openness is a pure function of canonical state, never coordinates:
     the gate holds at half the anchored stock (80000 of 160000), closes
     one unit below, and reopens through deterministic recharge;
   - traversal is state-gated: an open gate crosses instantly in both
     directions, a closed gate is absent (the ruin seat simply fails; the
     phos seat falls back to the ordinary lift), and traveling locks out
     further use; the destination is reconstructed from its own local
     records (ground, collision, active fidelity, recipe untouched);
   - routing with canonical state: the open gate halves the ruin->haven
     journey (1545498 vs the ordinary 2668102); closing it restores the
     ordinary route and cost bit for bit, and the ordinary ruin->gate
     climb is untouched either way;
   - depleting the lode through the op path removes the shortcut without
     corrupting anything: the ledger identity holds after every op, the
     intentional excavation sites persist, water stage is unaffected by
     Phos drawdown;
   - checkpoint/save roundtrip carries the deciding levels, so gate state
     survives encode/decode and save/restore exactly;
   - malformed products fail closed in ws_load: unknown kinds, bad
     anchors, non-Phos anchors, seats outside the anchored region and
     conventionally adjacent ends all reject, even with a recomputed CRC. */
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

/* Reservoir and module indices in generator order (verified by name in the
   Python gate). */
enum { R_SPOIL, R_LAKE, R_FOREST, R_KARST };
enum { M_RUIN = 10, M_GATE = 11, M_E0 = 12, M_E1 = 13, M_E2 = 14, M_E3 = 15, M_E4 = 16, M_FORD_E = 17, M_FORD_W = 18, M_W1 = 19, M_HAVEN = 20, M_PHOS_RUIN = 21 };
/* Weather days 0..6 for seed 0x9C0D51 (0 clear 1 rain 2 storm). */
static const uint32_t WEATHER[7] = {0, 1, 0, 1, 2, 0, 1};

static WsRecipe a;
static WsState s;

static WsContext where(int32_t x, int32_t y, int32_t z, int server) {
    return (WsContext) {0x5151, {{x, y, z}, UINT16_MAX}, (uint8_t)server};
}

static WsDisposition play(uint16_t action, uint16_t target, uint32_t seq, uint16_t amount, uint16_t aux, WsContext c) {
    WsOperation op = {seq, 1, s.revision, action, target, amount, aux};
    return ws_apply(&s, &a, c, op);
}

static WsTraveler seat(int i) {
    WsModule m;
    ws_materialize(&a, (uint16_t)i, &m);
    WsTraveler t;
    memset(&t, 0, sizeof(t));
    t.at = (WsAddress) {{m.pos.x, m.pos.y, m.pos.z}, UINT16_MAX};
    return t;
}

static int find_link(uint16_t kind, uint16_t x, uint16_t y) {
    for (int i = 0; i < a.link_count; i++) {
        const WsLink* l = &a.links[i];
        if (l->kind == kind && ((l->a == x && l->b == y) || (l->a == y && l->b == x))) return i;
    }
    return -1;
}

/* Rewrite one link record with a recomputed CRC: an attacker who can fix
   the checksum still cannot smuggle an invalid gate past validation.
   Returns 1 only if the mutated product STILL loads (never expected for
   the invalid cases below). */
static int mutated_load(uint16_t link, uint16_t na, uint16_t nb, uint16_t nkind, uint16_t nres) {
    uint8_t buf[4096];
    size_t n = sizeof(ws_macro_product);
    memcpy(buf, ws_macro_product, n);
    size_t table = 54 + 64 * (size_t)a.count; /* schema 3: after the modules */
    size_t at = table + 8 * (size_t)link;
    buf[at] = (uint8_t)na;
    buf[at + 1] = (uint8_t)(na >> 8);
    buf[at + 2] = (uint8_t)nb;
    buf[at + 3] = (uint8_t)(nb >> 8);
    buf[at + 4] = (uint8_t)nkind;
    buf[at + 5] = (uint8_t)(nkind >> 8);
    buf[at + 6] = (uint8_t)nres;
    buf[at + 7] = (uint8_t)(nres >> 8);
    uint32_t crc = ws_crc(buf + 16, n - 16);
    for (int i = 0; i < 4; i++) buf[12 + i] = (uint8_t)(crc >> (i * 8));
    WsRecipe probe;
    return ws_load(&probe, buf, n) == WS_OK;
}

int main(void) {
    CHECK(ws_load(&a, ws_macro_product, sizeof(ws_macro_product)) == WS_OK);
    CHECK(a.count == 22 && a.link_count == 13 && a.reservoir_count == 4);
    for (uint32_t d = 0; d < 7; d++) CHECK(ws_weather(&a, d) == WEATHER[d]);

    /* --- the gate record: declared topology anchored to the Phos lode --- */
    int gate = find_link(WS_LINK_ANOMALY, M_PHOS_RUIN, M_RUIN);
    int lift = find_link(WS_LINK_LIFT, M_PHOS_RUIN, M_E3);
    CHECK(gate == 12 && lift == 11); /* generator order: winze lift, then gate */
    CHECK(a.links[gate].a == M_PHOS_RUIN && a.links[gate].b == M_RUIN);
    CHECK(a.links[gate].reserved == R_KARST);
    CHECK(a.reservoirs[R_KARST].kind == WS_RES_PHOS && a.reservoirs[R_KARST].capacity == 160000);
    {
        const WsReservoir* v = &a.reservoirs[R_KARST];
        WsModule seatm, farm;
        ws_materialize(&a, M_PHOS_RUIN, &seatm);
        ws_materialize(&a, M_RUIN, &farm);
        /* the seat stands inside the anchored region, the far end outside */
        CHECK(seatm.pos.x >= v->lo.x && seatm.pos.x <= v->hi.x && seatm.pos.z >= v->lo.z && seatm.pos.z <= v->hi.z);
        CHECK(farm.pos.x < v->lo.x || farm.pos.x > v->hi.x || farm.pos.z < v->lo.z || farm.pos.z > v->hi.z);
        /* exact committed seat and far-end positions (independent of C) */
        CHECK(seatm.pos.x == -27171 && seatm.pos.y == 164282 && seatm.pos.z == 474302);
        CHECK(farm.pos.x == -208672 && farm.pos.y == 244567 && farm.pos.z == 38666);
        /* the seat really is the karst top: standing there is collision free
           with solid ground at the plane elevation */
        WsTraveler t = seat(M_PHOS_RUIN);
        int32_t h;
        CHECK(ws_ground(&a, t.at, t.at.pos.y + 350, &h) && h == seatm.pos.y);
        CHECK(!ws_collision(&a, t.at, 300));
    }

    /* --- ordinary geography preserved: exact Gate 2 literals --- */
    {
        uint64_t cg, ch;
        uint16_t path[WS_CAP];
        int ng = ws_route_cost(&a, M_RUIN, M_GATE, WS_CAP_WALK, &cg, path, WS_CAP);
        CHECK(ng == 5 && cg == 3246425);
        uint16_t expect_g[5] = {M_RUIN, M_E2, M_E1, M_E0, M_GATE};
        CHECK(memcmp(path, expect_g, sizeof(expect_g)) == 0);
        int nh = ws_route_cost(&a, M_RUIN, M_HAVEN, WS_CAP_WALK, &ch, path, WS_CAP);
        CHECK(nh == 8 && ch == 2668102);
        uint16_t expect_h[8] = {M_RUIN, M_E2, M_E3, M_E4, M_FORD_E, M_FORD_W, M_W1, M_HAVEN};
        CHECK(memcmp(path, expect_h, sizeof(expect_h)) == 0);
        /* walk-only planning never sees the gate; its toll is separate */
        CHECK(ws_link_cost(&a, (uint16_t)gate, WS_CAP_WALK) == UINT64_MAX);
        CHECK(ws_link_cost(&a, (uint16_t)gate, WS_CAP_ANOMALY) == WS_ANOMALY_COST);
        CHECK(ws_link_cost(&a, (uint16_t)gate, WS_CAP_WALK | WS_CAP_ANOMALY) == WS_ANOMALY_COST);
        /* the winze lift keeps its own climb-based toll */
        WsModule e3m, seatm;
        ws_materialize(&a, M_E3, &e3m);
        ws_materialize(&a, M_PHOS_RUIN, &seatm);
        CHECK(ws_link_cost(&a, (uint16_t)lift, WS_CAP_WALK | WS_CAP_LIFT) == 500 + (uint64_t)(seatm.pos.y - e3m.pos.y) / 2);
        CHECK(ws_link_cost(&a, (uint16_t)lift, WS_CAP_WALK | WS_CAP_LIFT) == 3007);
        CHECK(ws_link_cost(&a, (uint16_t)lift, WS_CAP_WALK) == UINT64_MAX);
        /* reachability classes: the ordinary walk chain is intact, and the
           karst top needs a capability either way */
        CHECK(ws_reachable(&a, M_RUIN, M_HAVEN, WS_CAP_WALK) == WS_REACH_WALK);
        CHECK(ws_reachable(&a, M_RUIN, M_PHOS_RUIN, WS_CAP_WALK) == WS_REACH_CONDITIONAL);
        CHECK(ws_reachable(&a, M_RUIN, M_PHOS_RUIN, WS_CAP_WALK | WS_CAP_LIFT) == WS_REACH_ABILITY);
        CHECK(ws_reachable(&a, M_RUIN, M_PHOS_RUIN, WS_CAP_WALK | WS_CAP_ANOMALY) == WS_REACH_ABILITY);
    }

    /* --- openness: a pure function of canonical state, exact boundary --- */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(s.level[R_KARST] == 160000 && ws_link_open(&a, &s, (uint16_t)gate) == 1);
    s.level[R_KARST] = 80000;
    CHECK(ws_link_open(&a, &s, (uint16_t)gate) == 1); /* half stock still holds */
    s.level[R_KARST] = 79999;
    CHECK(!ws_link_open(&a, &s, (uint16_t)gate)); /* one unit below closes */
    s.level[R_KARST] = 0;
    CHECK(!ws_link_open(&a, &s, (uint16_t)gate));
    /* non-gates and out-of-range queries are never open */
    CHECK(!ws_link_open(&a, &s, (uint16_t)lift));
    CHECK(!ws_link_open(&a, &s, 0));
    CHECK(!ws_link_open(&a, &s, a.link_count));
    /* unbound or empty state reports the declared levels, like stage */
    CHECK(ws_link_open(&a, NULL, (uint16_t)gate) == 1);
    {
        WsState empty;
        memset(&empty, 0, sizeof(empty));
        CHECK(ws_link_open(&a, &empty, (uint16_t)gate) == 1);
    }
    s.level[R_KARST] = 160000;

    /* --- traversal: state-gated, instant, both directions, local --- */
    {
        WsTraveler t = seat(M_PHOS_RUIN);
        CHECK(ws_use_link_state(&a, &s, &t));
        CHECK(t.at.pos.x == -208672 && t.at.pos.y == 244567 && t.at.pos.z == 38666 && t.at.scope == UINT16_MAX);
        CHECK(!t.remaining && t.destination.scope == UINT16_MAX);
    }
    {
        /* destination locality: the arrival point is reconstructed from its
           own local records only - active fidelity, local ground, no
           collision, and the recipe itself untouched by the crossing */
        WsRecipe before = a;
        WsTraveler t = seat(M_RUIN);
        CHECK(ws_use_link_state(&a, &s, &t));
        CHECK(t.at.pos.x == -27171 && t.at.pos.y == 164282 && t.at.pos.z == 474302 && t.at.scope == UINT16_MAX);
        CHECK(ws_fidelity(t.at.pos, t.at.pos, 0) == WS_ACTIVE);
        int32_t h;
        CHECK(ws_ground(&a, t.at, t.at.pos.y + 350, &h) && h == 164282);
        CHECK(!ws_collision(&a, t.at, 300));
        CHECK(memcmp(&before, &a, sizeof(WsRecipe)) == 0);
    }
    {
        /* a closed gate is absent: the ruin seat has no other non-walk
           link, so use simply fails there */
        s.level[R_KARST] = 79999;
        WsTraveler t = seat(M_RUIN);
        CHECK(!ws_use_link_state(&a, &s, &t));
        CHECK(t.at.pos.x == -208672 && t.at.pos.z == 38666); /* untouched */
        /* the phos seat falls back to the ordinary winze lift: timed travel
           down to the trail, never a crossing */
        WsTraveler u = seat(M_PHOS_RUIN);
        CHECK(ws_use_link_state(&a, &s, &u));
        CHECK(u.at.pos.x == -27171 && u.at.pos.y == 164282 && u.at.pos.z == 474302);
        CHECK(u.destination.pos.x == -149969 && u.destination.pos.y == 159267 && u.destination.pos.z == 454026);
        CHECK(u.remaining == 2508); /* |164282-159267|/2 + 1 */
        CHECK(!ws_use_link_state(&a, &s, &u)); /* traveling locks out use */
        /* the stateless scan serves the same lift at the same seat: the
           published Gate 1-3 behavior is unchanged by the gate */
        WsTraveler v = seat(M_PHOS_RUIN);
        CHECK(ws_use_link(&a, &v));
        CHECK(v.destination.pos.x == -149969 && v.remaining == 2508);
        WsTraveler w = seat(M_RUIN);
        CHECK(!ws_use_link(&a, &w));
        /* with no bound state the declared levels decide (view semantics) */
        WsTraveler x = seat(M_RUIN);
        CHECK(ws_use_link_state(&a, NULL, &x));
        CHECK(x.at.pos.x == -27171 && x.at.pos.y == 164282 && x.at.pos.z == 474302);
        s.level[R_KARST] = 160000;
    }

    /* --- routing with canonical state: the shortcut and its removal --- */
    {
        uint64_t cost;
        uint16_t path[WS_CAP];
        uint32_t all = WS_CAP_WALK | WS_CAP_LIFT | WS_CAP_ANOMALY;
        int n;
        /* open gate: the direct crossing, and the halved haven journey */
        n = ws_route_cost_state(&a, &s, M_RUIN, M_PHOS_RUIN, all, &cost, path, WS_CAP);
        CHECK(n == 2 && cost == 1500 && path[0] == M_RUIN && path[1] == M_PHOS_RUIN);
        n = ws_route_cost_state(&a, &s, M_PHOS_RUIN, M_RUIN, all, &cost, path, WS_CAP);
        CHECK(n == 2 && cost == 1500);
        n = ws_route_cost_state(&a, &s, M_RUIN, M_HAVEN, all, &cost, path, WS_CAP);
        CHECK(n == 8 && cost == 1545498);
        uint16_t via_gate[8] = {M_RUIN, M_PHOS_RUIN, M_E3, M_E4, M_FORD_E, M_FORD_W, M_W1, M_HAVEN};
        CHECK(memcmp(path, via_gate, sizeof(via_gate)) == 0);
        /* the ordinary climb to the gate settlement is untouched */
        n = ws_route_cost_state(&a, &s, M_RUIN, M_GATE, all, &cost, path, WS_CAP);
        CHECK(n == 5 && cost == 3246425);
        /* closed gate: the shortcut vanishes, ordinary route and cost
           return bit for bit, and the far end still needs a capability */
        s.level[R_KARST] = 79999;
        n = ws_route_cost_state(&a, &s, M_RUIN, M_PHOS_RUIN, all, &cost, path, WS_CAP);
        CHECK(n == 4 && cost == 1130118);
        uint16_t ordinary[4] = {M_RUIN, M_E2, M_E3, M_PHOS_RUIN};
        CHECK(memcmp(path, ordinary, sizeof(ordinary)) == 0);
        n = ws_route_cost_state(&a, &s, M_PHOS_RUIN, M_RUIN, all, &cost, path, WS_CAP);
        CHECK(n == 4 && cost == 1130118);
        n = ws_route_cost_state(&a, &s, M_RUIN, M_HAVEN, all, &cost, path, WS_CAP);
        CHECK(n == 8 && cost == 2668102);
        uint16_t ordinary_h[8] = {M_RUIN, M_E2, M_E3, M_E4, M_FORD_E, M_FORD_W, M_W1, M_HAVEN};
        CHECK(memcmp(path, ordinary_h, sizeof(ordinary_h)) == 0);
        n = ws_route_cost_state(&a, &s, M_RUIN, M_GATE, all, &cost, path, WS_CAP);
        CHECK(n == 5 && cost == 3246425);
        CHECK(!ws_route_cost_state(&a, &s, M_RUIN, M_PHOS_RUIN, WS_CAP_WALK, &cost, path, WS_CAP));
        /* water stage is a pure read of the water region: Phos drawdown
           never touches it */
        WsRiverSample g;
        CHECK(ws_river_stage(&a, &s, 0, 36000, &g));
        CHECK(g.width == 48000 && g.depth == 2700 && g.surfaced == 1);
        s.level[R_KARST] = 160000;
    }

    /* --- depletion through the op path removes the shortcut --- */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(play(WS_EXCAVATE, R_KARST, 1, 65535, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(s.level[R_KARST] == 94465 && s.shards[0] == 65535);
    CHECK(ws_link_open(&a, &s, (uint16_t)gate) == 1); /* still above half */
    /* the carry is capped: deposit the shards to material (2:1, lossy)
       before drawing more */
    CHECK(play(WS_CONVERT, R_KARST, 2, 65535, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(s.shards[0] == 0 && s.material[0] == 32767 && s.lost_total == 32768);
    CHECK(play(WS_EXCAVATE, R_KARST, 3, 14465, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(s.level[R_KARST] == 80000 && ws_link_open(&a, &s, (uint16_t)gate) == 1); /* the boundary holds */
    CHECK(play(WS_EXCAVATE, R_KARST, 4, 1, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(s.level[R_KARST] == 79999);
    CHECK(!ws_link_open(&a, &s, (uint16_t)gate)); /* the shortcut is gone */
    CHECK(s.site_count == 3); /* intentional excavations: they never heal */
    CHECK(s.sites[0].kind == WS_SITE_EXCAVATION && s.sites[2].kind == WS_SITE_EXCAVATION);
    CHECK(ws_ledger_check(&s, &a) && ws_state_validate(&s, &a) == WS_OK);
    {
        WsTraveler t = seat(M_RUIN);
        CHECK(!ws_use_link_state(&a, &s, &t)); /* and cannot be crossed */
        uint64_t cost;
        uint16_t path[WS_CAP];
        int n = ws_route_cost_state(&a, &s, M_RUIN, M_HAVEN, WS_CAP_WALK | WS_CAP_LIFT | WS_CAP_ANOMALY, &cost, path, WS_CAP);
        CHECK(n == 8 && cost == 2668102); /* ordinary geography intact */
    }
    /* --- recharge restores it through the deterministic weather --- */
    ws_resources_tick(&s, &a, 86400); /* day 1 rain: phos +32000 */
    CHECK(s.level[R_KARST] == 111999 && ws_link_open(&a, &s, (uint16_t)gate) == 1);
    CHECK(ws_ledger_check(&s, &a) && ws_state_validate(&s, &a) == WS_OK);
    {
        WsTraveler t = seat(M_RUIN);
        CHECK(ws_use_link_state(&a, &s, &t)); /* reopened: crossing again */
        CHECK(t.at.pos.x == -27171 && t.at.pos.y == 164282 && t.at.pos.z == 474302);
    }
    /* a full drain closes it for longer: two days of recharge to reopen */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(play(WS_EXCAVATE, R_KARST, 1, 65535, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(play(WS_CONVERT, R_KARST, 2, 65535, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(play(WS_EXCAVATE, R_KARST, 3, 65535, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(s.level[R_KARST] == 28930 && !ws_link_open(&a, &s, (uint16_t)gate));
    ws_resources_tick(&s, &a, 86400);
    CHECK(s.level[R_KARST] == 60930 && !ws_link_open(&a, &s, (uint16_t)gate));
    ws_resources_tick(&s, &a, 86400);
    CHECK(s.level[R_KARST] == 92930 && ws_link_open(&a, &s, (uint16_t)gate) == 1);
    CHECK(ws_ledger_check(&s, &a));

    /* --- checkpoint/save roundtrip carries the deciding levels --- */
    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);
    CHECK(play(WS_EXCAVATE, R_KARST, 1, 14466, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(s.level[R_KARST] == 145534 && ws_link_open(&a, &s, (uint16_t)gate) == 1);
    CHECK(play(WS_CONVERT, R_KARST, 2, 14466, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(play(WS_EXCAVATE, R_KARST, 3, 65535, 1, where(-27171, 164282, 474302, 1)).status == WS_OK);
    CHECK(s.level[R_KARST] == 79999 && !ws_link_open(&a, &s, (uint16_t)gate));
    {
        uint8_t wire[12000];
        size_t n = ws_state_encode(&s, wire, sizeof(wire));
        size_t base = 48 + 22 * 28 + 1 * 538 + 1 * 12 + 3 * 16 + 3 * 20;
        CHECK(n == base + 8 + 16 + 4 + 2 * 24 + 24); /* 2 excavation sites */
        CHECK(wire[4] == 2);
        WsState t;
        CHECK(ws_state_decode(&t, &a, wire, n) == WS_OK);
        CHECK(ws_state_hash(&t) == ws_state_hash(&s));
        CHECK(t.level[R_KARST] == 79999 && !ws_link_open(&a, &t, (uint16_t)gate));
        CHECK(ws_state_decode(&t, &a, wire, n - 1) != WS_OK); /* truncation */
        CHECK(ws_save(&s, "build/anomaly-state"));
        WsState loaded;
        CHECK(ws_restore(&loaded, &a, "build/anomaly-state"));
        CHECK(ws_state_hash(&loaded) == ws_state_hash(&s));
        CHECK(!ws_link_open(&a, &loaded, (uint16_t)gate)); /* still closed after restore */
        loaded.level[R_KARST] = 160000;
        CHECK(ws_link_open(&a, &loaded, (uint16_t)gate) == 1); /* open states stay open */
    }

    /* --- malformed products fail closed, CRC recomputed or not --- */
    CHECK(!mutated_load((uint16_t)gate, M_PHOS_RUIN, M_RUIN, 4, R_KARST)); /* unknown kind */
    CHECK(!mutated_load((uint16_t)gate, M_PHOS_RUIN, M_RUIN, WS_LINK_ANOMALY, 4)); /* anchor out of range */
    CHECK(!mutated_load((uint16_t)gate, M_PHOS_RUIN, M_RUIN, WS_LINK_ANOMALY, R_SPOIL)); /* anchor not Phos */
    CHECK(!mutated_load((uint16_t)gate, M_RUIN, M_PHOS_RUIN, WS_LINK_ANOMALY, R_KARST)); /* seat outside the region */
    CHECK(!mutated_load((uint16_t)gate, M_PHOS_RUIN, M_E2, WS_LINK_ANOMALY, R_KARST)); /* conventionally adjacent end */
    /* control: the direct walk the gate replaces is unrealizable through
       the karst flank - that is why the ordinary access is a lift */
    CHECK(!mutated_load((uint16_t)lift, M_PHOS_RUIN, M_E3, WS_LINK_WALK, 0));
    {
        /* a second gate on the same lode is a duplicate anchor: the valid
           gate retargets to the haven (still a legal gate), and the lift
           record becomes a second gate anchored to the same lode */
        uint8_t buf[4096];
        size_t n = sizeof(ws_macro_product);
        memcpy(buf, ws_macro_product, n);
        size_t table = 54 + 64 * (size_t)a.count;
        size_t g = table + 8 * (size_t)gate, l = table + 8 * (size_t)lift;
        buf[g + 2] = (uint8_t)M_HAVEN; /* gate 1: phos_ruin -> haven (legal) */
        buf[g + 3] = (uint8_t)(M_HAVEN >> 8);
        buf[l] = (uint8_t)M_PHOS_RUIN; /* gate 2: same anchor, same seat */
        buf[l + 1] = (uint8_t)(M_PHOS_RUIN >> 8);
        buf[l + 2] = (uint8_t)M_E3;
        buf[l + 3] = (uint8_t)(M_E3 >> 8);
        buf[l + 4] = 3;
        buf[l + 5] = 0;
        buf[l + 6] = (uint8_t)R_KARST;
        buf[l + 7] = 0;
        uint32_t crc = ws_crc(buf + 16, n - 16);
        for (int i = 0; i < 4; i++) buf[12 + i] = (uint8_t)(crc >> (i * 8));
        WsRecipe probe;
        CHECK(ws_load(&probe, buf, n) != WS_OK); /* one gate per Phos region */
    }

    printf("{\"stage\":\"anomaly\",\"passed\":%u,\"product_bytes\":%zu,\"modules\":%u,\"links\":%u,\"gate_cost\":%u,\"open_level\":%u}\n", checks, sizeof(ws_macro_product), a.count, a.link_count, WS_ANOMALY_COST, 80000);
    return 0;
}
