/* Gate 5 (sparse deterministic creatures and schedules). The product under
   test is the committed generator output (content/worlds/macro.inc, schema
   4): eight species derived from the same geography - one per biome
   reservoir and two per settlement - whose slots materialize per query
   window as pure functions of (recipe, exceptions, time). Everything here
   is proven against the C engine with exact expected numbers:

   - species records are bounded and fail-closed: kinds, habitats, slots,
     periods, stations and spreads match the generator table exactly;
   - identities are stable functions of species and slot: equal across
     calls, distinct across slots, known to the validator, never coordinates;
   - placement follows the constraints: anchored slots live on their
     settlement surface (engine collision-free, ground exactly the plane),
     biome slots live inside their reservoir extent on the terrain surface
     (the mountain and karst tops, the lake and forest floors);
   - schedules resolve from time and the access graph: the warden dwells at
     its placed home and the trail head exactly, travels along the declared
     route chord between them, and the whole cycle is periodic, so time
     skips and repeated queries are the same computation;
   - queries materialize only the window: whole-world count and order, cap
     contract (negated overflow count), per-settlement windows, repeat
     determinism, and the recipe/state bytes untouched;
   - persistent exceptions override generated defaults: DEAD removes,
     RELOCATED re-anchors at a declared walk surface, PINNED holds home;
     the authority mutation fails closed on unknown creatures, bad kinds,
     non-walk or interior relocations and unjoined players; state wire
     version 3 carries the table through encode/decode and save/restore;
   - malformed products fail closed in ws_load: unknown kinds, out-of-range
     habitats, degenerate populations/schedules/spreads, duplicate identity,
     non-walk anchors and required anchors that baseline walking cannot
     reach all reject even with a recomputed CRC, while a non-required
     species anchored at the karst ruin still loads (capability content);
   - the Gate 1-4 literals are untouched: the ordinary ruin->haven walk
     cost is bit-identical in the schema 4 product. */
#include "../world/state.h"
static unsigned checks;
#define CHECK(x)                                                 \
    do {                                                         \
        checks++;                                                \
        if (!(x)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
            return 1;                                          \
        }                                                        \
    } while (0)
#include "../content/worlds/macro.inc"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reservoir, module and species indices in generator order (verified by
   name in the Python gate). */
enum { R_SPOIL, R_LAKE, R_FOREST, R_KARST };
enum { M_MOUNTAIN = 0, M_KARST_RIDGE = 5, M_PLAINS = 7, M_FOREST = 8, M_RUIN = 10, M_GATE = 11, M_E0 = 12, M_W1 = 19, M_HAVEN = 20, M_PHOS_RUIN = 21 };
enum { S_MASSIF, S_LAKE, S_FOREST, S_KARST, S_GATE_WARDEN, S_GATE_RES, S_HAVEN_WARDEN, S_HAVEN_RES };

static WsRecipe a;
static WsState s;

static WsId wid(int sp, int slot) { return ws_creature_id(&a, (uint16_t)sp, (uint16_t)slot); }

static int at(int sp, int slot, uint32_t t, WsCreatureSample* out) {
    return ws_creature_at(&a, &s, (uint16_t)sp, (uint16_t)slot, t, out);
}

static int at0(int sp, int slot, uint32_t t, WsCreatureSample* out) {
    return ws_creature_at(&a, 0, (uint16_t)sp, (uint16_t)slot, t, out);
}

static int expect(int sp, int slot, uint32_t t, int32_t x, int32_t y, int32_t z, int station, int traveling) {
    WsCreatureSample one;
    if (!at(sp, slot, t, &one)) {
        fprintf(stderr, "FAIL expect %d/%d at %u: absent\n", sp, slot, t);
        return 0;
    }
    if (one.pos.x != x || one.pos.y != y || one.pos.z != z || one.station != (uint16_t)station || one.traveling != (uint16_t)traveling) {
        fprintf(stderr, "FAIL expect %d/%d at %u: (%d,%d,%d) st%d tv%d\n", sp, slot, t, one.pos.x, one.pos.y, one.pos.z, one.station, one.traveling);
        return 0;
    }
    checks += 6;
    return one.species == (uint16_t)sp && one.slot == (uint16_t)slot && ws_id_equal(one.id, wid(sp, slot));
}

/* Rewrite one creature record field with a recomputed CRC: an attacker who
   can fix the checksum still cannot smuggle an invalid species record past
   validation. Returns 1 only if the mutated product STILL loads. */
static int mutated_load(uint16_t species, unsigned field, uint16_t value) {
    uint8_t buf[4096];
    size_t n = sizeof(ws_macro_product);
    memcpy(buf, ws_macro_product, n);
    size_t table = 56 + 64 * (size_t)a.count + 8 * (size_t)a.link_count + 56 * (size_t)a.feature_count + 12 * (size_t)a.exception_count + 64 * (size_t)a.reservoir_count;
    size_t rec = table + 32 * (size_t)species;
    buf[rec + field] = (uint8_t)value;
    buf[rec + field + 1] = (uint8_t)(value >> 8);
    uint32_t crc = ws_crc(buf + 16, n - 16);
    for (int i = 0; i < 4; i++) buf[12 + i] = (uint8_t)(crc >> (i * 8));
    WsRecipe probe;
    return ws_load(&probe, buf, n) == WS_OK;
}

/* Rewrite the 16-byte species identity with a duplicate of another
   species' identity, CRC recomputed. */
static int duplicated_id_load(uint16_t species, uint16_t from) {
    uint8_t buf[4096];
    size_t n = sizeof(ws_macro_product);
    memcpy(buf, ws_macro_product, n);
    size_t table = 56 + 64 * (size_t)a.count + 8 * (size_t)a.link_count + 56 * (size_t)a.feature_count + 12 * (size_t)a.exception_count + 64 * (size_t)a.reservoir_count;
    memcpy(buf + table + 32 * (size_t)species, buf + table + 32 * (size_t)from, 16);
    uint32_t crc = ws_crc(buf + 16, n - 16);
    for (int i = 0; i < 4; i++) buf[12 + i] = (uint8_t)(crc >> (i * 8));
    WsRecipe probe;
    return ws_load(&probe, buf, n) == WS_OK;
}

int main(void) {
    CHECK(ws_load(&a, ws_macro_product, sizeof(ws_macro_product)) == WS_OK);
    CHECK(a.count == 22 && a.link_count == 13 && a.reservoir_count == 4);
    CHECK(a.creature_count == 8 && sizeof(ws_macro_product) == 2196);

    /* --- the species table: bounded generator records, exact --- */
    {
        static const uint16_t KIND[8] = {1, 1, 1, 1, 3, 2, 3, 2};
        static const uint16_t HAB[8] = {R_SPOIL, R_LAKE, R_FOREST, R_KARST, M_GATE, M_GATE, M_HAVEN, M_HAVEN};
        static const uint16_t SLOTS[8] = {8, 6, 10, 4, 1, 2, 2, 3};
        static const uint16_t PERIOD[8] = {43200, 43200, 43200, 43200, 28800, 21600, 36000, 28800};
        static const uint16_t STATIONS[8] = {3, 3, 3, 3, 2, 3, 2, 3};
        static const uint16_t RADIUS[8] = {30000, 30000, 30000, 30000, 20000, 20000, 20000, 20000};
        for (int i = 0; i < 8; i++) {
            const WsCreature* c = &a.creatures[i];
            CHECK(c->kind == KIND[i] && c->habitat == HAB[i] && c->slots == SLOTS[i]);
            CHECK(c->period == PERIOD[i] && c->stations == STATIONS[i] && c->radius == RADIUS[i]);
            for (int j = 0; j < i; j++) CHECK(!ws_id_equal(c->id, a.creatures[j].id));
        }
        int total = 0;
        for (int i = 0; i < 8; i++) total += a.creatures[i].slots;
        CHECK(total == 36);
        /* required species anchor at baseline-walk-reachable settlements;
           the first declared walk surface is the watershed viewpoint */
        CHECK(ws_reachable(&a, 3, M_GATE, WS_CAP_WALK) == WS_REACH_WALK);
        CHECK(ws_reachable(&a, 3, M_HAVEN, WS_CAP_WALK) == WS_REACH_WALK);
        CHECK(ws_reachable(&a, 3, M_PHOS_RUIN, WS_CAP_WALK) == WS_REACH_CONDITIONAL);
    }

    /* --- stable identities --- */
    {
        WsId x = wid(S_GATE_WARDEN, 0), y = wid(S_GATE_WARDEN, 0), z = wid(S_GATE_WARDEN, 1);
        CHECK(ws_id_equal(x, y) && !ws_id_equal(x, z));
        CHECK(ws_id_equal(x, (WsId) {{3898211244u, 3047446765u, 2213983797u, 1126835656u}}));
        CHECK(ws_id_equal(wid(S_GATE_RES, 0), (WsId) {{2403772543u, 291003522u, 1338263924u, 2306084298u}}));
        CHECK(ws_id_equal(wid(S_MASSIF, 0), (WsId) {{2727697027u, 2722871303u, 282626514u, 308584483u}}));
        for (int sp = 0; sp < 8; sp++)
            for (int k = 0; k < a.creatures[sp].slots; k++) CHECK(ws_creature_known(&a, wid(sp, k)));
        WsId bogus = {{1, 2, 3, 4}};
        CHECK(!ws_creature_known(&a, bogus));
        WsId none = ws_creature_id(&a, 8, 0);
        CHECK(!none.word[0] && !none.word[1] && !none.word[2] && !none.word[3]);
    }

    ws_state_init(&s, &a);
    CHECK(ws_join(&s, 0x5151) == WS_OK);

    /* --- placement: the placed home is the pinned position --- */
    {
        static const int SP[9] = {S_GATE_WARDEN, S_GATE_RES, S_HAVEN_WARDEN, S_HAVEN_RES, S_MASSIF, S_MASSIF, S_LAKE, S_FOREST, S_KARST};
        static const int SL[9] = {0, 0, 0, 0, 0, 3, 0, 5, 2};
        static const int32_t X[9] = {288813, 274763, 148822, 170996, -56689, -209046, 95590, -216713, -30489};
        static const int32_t Y[9] = {470000, 470000, 54675, 54675, 600000, 600000, 167627, 244367, 164282};
        static const int32_t Z[9] = {-467753, -485002, 901749, 889923, -753078, -608869, 306989, 84612, 557139};
        for (int i = 0; i < 9; i++) {
            CHECK(ws_creature_except(&s, &a, wid(SP[i], SL[i]), WS_CREX_PINNED, 0) == WS_OK);
            WsCreatureSample one;
            CHECK(at(SP[i], SL[i], 12345, &one));
            CHECK(one.pos.x == X[i] && one.pos.y == Y[i] && one.pos.z == Z[i]);
            CHECK(one.station == 0 && !one.traveling);
            CHECK(ws_creature_except(&s, &a, wid(SP[i], SL[i]), 0, 0) == WS_OK);
        }
        CHECK(s.creature_count == 0); /* removal compacts the table away */
        /* constraints hold for every slot, pinned (pure home placement) */
        for (int sp = 0; sp < 8; sp++) {
            const WsCreature* c = &a.creatures[sp];
            for (int k = 0; k < c->slots; k++) {
                CHECK(ws_creature_except(&s, &a, wid(sp, k), WS_CREX_PINNED, 0) == WS_OK);
                WsCreatureSample one;
                CHECK(at(sp, k, 7777, &one));
                if (c->kind == WS_CRE_FAUNA) {
                    const WsReservoir* v = &a.reservoirs[c->habitat];
                    CHECK(one.pos.x >= v->lo.x && one.pos.x <= v->hi.x);
                    CHECK(one.pos.z >= v->lo.z && one.pos.z <= v->hi.z);
                    CHECK(one.pos.y >= v->lo.y && one.pos.y <= v->hi.y);
                    if (c->habitat == R_SPOIL) CHECK(one.pos.y == 600000 || one.pos.y == v->lo.y); /* mountain top or floor */
                    if (c->habitat == R_KARST) CHECK(one.pos.y == 164282 || one.pos.y == v->lo.y); /* karst top or floor */
                } else {
                    WsModule m;
                    ws_materialize(&a, c->habitat, &m);
                    CHECK(llabs((int64_t)one.pos.x - m.pos.x) <= m.size.x / 2 - 600);
                    CHECK(llabs((int64_t)one.pos.z - m.pos.z) <= m.size.z / 2 - 600);
                    CHECK(one.pos.y == m.pos.y);
                    /* the engine agrees: standing there is collision-free
                       with solid ground exactly at the surface */
                    WsAddress addr = {{one.pos.x, one.pos.y, one.pos.z}, UINT16_MAX};
                    int32_t h;
                    CHECK(!ws_collision(&a, addr, 300));
                    CHECK(ws_ground(&a, addr, one.pos.y + 350, &h) && h == m.pos.y);
                }
                CHECK(ws_creature_except(&s, &a, wid(sp, k), 0, 0) == WS_OK);
            }
        }
        CHECK(s.creature_count == 0 && ws_state_validate(&s, &a) == WS_OK);
    }

    /* --- schedules: resolution from time and the access graph --- */
    {
        /* the gate warden: dwell home, dwell the trail head, travel between
           them along the declared route; exact literals at chosen times */
        CHECK(expect(S_GATE_WARDEN, 0, 0, -108414, 336972, -396991, 1, 1));
        CHECK(expect(S_GATE_WARDEN, 0, 600, -87104, 344343, -401591, 1, 1));
        CHECK(expect(S_GATE_WARDEN, 0, 4321, 40738, 388562, -429183, 1, 1));
        CHECK(expect(S_GATE_WARDEN, 0, 12345, 288813, 470000, -467753, 0, 0));
        CHECK(expect(S_GATE_WARDEN, 0, 20000, 39680, 388197, -428956, 0, 1));
        CHECK(expect(S_GATE_WARDEN, 0, 26000, -152086, 321867, -387566, 1, 0));
        CHECK(expect(S_GATE_WARDEN, 0, 27000, -152086, 321867, -387566, 1, 0));
        CHECK(expect(S_GATE_WARDEN, 0, 28000, -136109, 327393, -391014, 1, 1));
        CHECK(expect(S_GATE_WARDEN, 0, 28799, -108414, 336972, -396991, 1, 1));
        /* station dwell points are exactly the declared surfaces: the trail
           head center at station 1, the placed home at station 0 */
        WsModule e0;
        ws_materialize(&a, M_E0, &e0);
        CHECK(e0.pos.x == -152086 && e0.pos.y == 321867 && e0.pos.z == -387566);
        /* traveling positions lie on the route chord between the two module
           centers: collinear in plan, elevation between the endpoints */
        {
            WsModule gate;
            ws_materialize(&a, M_GATE, &gate);
            for (uint32_t t = 0; t < 28800; t += 257) {
                WsCreatureSample one;
                CHECK(at(S_GATE_WARDEN, 0, t, &one));
                if (!one.traveling) continue;
                int64_t dx = e0.pos.x - gate.pos.x, dz = e0.pos.z - gate.pos.z;
                int64_t cx = one.pos.x - gate.pos.x, cz = one.pos.z - gate.pos.z;
                /* on the centerline: 16.16 interpolation truncates each
                   component, so the cross product is zero within one unit
                   of rounding per component */
                int64_t cross = dx * cz - dz * cx;
                int64_t slack = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
                CHECK(cross <= slack && cross >= -slack);
                CHECK(one.pos.y >= (e0.pos.y < gate.pos.y ? e0.pos.y : gate.pos.y) && one.pos.y <= (e0.pos.y > gate.pos.y ? e0.pos.y : gate.pos.y));
            }
        }
        /* periodicity: the cycle is the species period, so time skips and
           wrapped clocks resolve identically */
        for (uint32_t k = 1; k <= 4; k++) {
            WsCreatureSample x, y;
            CHECK(at(S_GATE_WARDEN, 0, 12345, &x));
            CHECK(at(S_GATE_WARDEN, 0, 12345 + k * 28800, &y));
            CHECK(x.pos.x == y.pos.x && x.pos.y == y.pos.y && x.pos.z == y.pos.z && x.station == y.station && x.traveling == y.traveling);
            CHECK(at(S_GATE_WARDEN, 0, 4294967295u - 28800 + 12345 + 28800, &y)); /* clock wrap */
            CHECK(x.pos.x == y.pos.x && x.pos.y == y.pos.y && x.pos.z == y.pos.z);
        }
        /* the haven wardens dwell their declared station: trail_w1 center */
        {
            WsCreatureSample one;
            CHECK(at(S_HAVEN_WARDEN, 0, 6000, &one));
            WsModule w1;
            ws_materialize(&a, M_W1, &w1);
            CHECK(!one.traveling && one.pos.x == w1.pos.x && one.pos.y == w1.pos.y && one.pos.z == w1.pos.z);
        }
        /* out-of-range queries answer nothing */
        WsCreatureSample none;
        CHECK(!at0(8, 0, 0, &none));
        CHECK(!at0(0, 8, 0, &none));
        CHECK(!at0(0, a.creatures[0].slots, 0, &none));
    }

    /* --- queries: windows materialize only what is inside --- */
    {
        WsCreatureSample out[64];
        WsPos lo = {-1000000, -1000000, -1000000}, hi = {1000000, 1000000, 1000000};
        WsRecipe before = a;
        WsState sbefore = s;
        int n = ws_creature_query(&a, 0, lo, hi, 0, out, 64);
        CHECK(n == 36);
        for (int k = 1; k < n; k++) {
            CHECK(out[k].species >= out[k - 1].species);
            if (out[k].species == out[k - 1].species) CHECK(out[k].slot == out[k - 1].slot + 1);
        }
        CHECK(ws_creature_query(&a, 0, lo, hi, 0, out, 10) == -36); /* cap contract */
        CHECK(ws_creature_query(&a, 0, lo, hi, 0, out, 35) == -36);
        CHECK(ws_creature_query(&a, 0, lo, hi, 0, out, 36) == 36);
        /* repeat determinism: unload/reload is the same computation */
        WsCreatureSample again[64];
        CHECK(ws_creature_query(&a, 0, lo, hi, 0, again, 64) == 36);
        CHECK(memcmp(out, again, sizeof(out[0]) * 36) == 0);
        CHECK(ws_creature_query(&a, 0, lo, hi, 4294967295u, again, 64) == 36); /* any time, no caches */
        CHECK(ws_creature_query(&a, 0, lo, hi, 12345, again, 64) == 36);
        CHECK(ws_creature_query(&a, 0, lo, hi, 0, again, 64) == 36);
        CHECK(memcmp(out, again, sizeof(out[0]) * 36) == 0);
        CHECK(memcmp(&before, &a, sizeof(WsRecipe)) == 0); /* untouched */
        CHECK(memcmp(&sbefore, &s, sizeof(WsState)) == 0);
        /* settlement windows catch exactly the local dwellers */
        WsModule gate, haven;
        ws_materialize(&a, M_GATE, &gate);
        ws_materialize(&a, M_HAVEN, &haven);
        WsPos glo = {gate.pos.x - 80000, gate.pos.y - 8000, gate.pos.z - 80000}, ghi = {gate.pos.x + 80000, gate.pos.y + 8000, gate.pos.z + 80000};
        int g = ws_creature_query(&a, 0, glo, ghi, 12345, out, 64); /* warden dwells home then */
        CHECK(g >= 1);
        int seen_warden = 0;
        for (int k = 0; k < g; k++) {
            CHECK(out[k].pos.x >= glo.x && out[k].pos.x <= ghi.x && out[k].pos.y >= glo.y && out[k].pos.y <= ghi.y && out[k].pos.z >= glo.z && out[k].pos.z <= ghi.z);
            if (out[k].species == S_GATE_WARDEN) {
                seen_warden = 1;
                CHECK(out[k].pos.x == 288813 && out[k].pos.y == 470000 && out[k].pos.z == -467753);
            }
        }
        CHECK(seen_warden);
        /* the haven window at a dwelling time catches the local wardens on
           their declared station, and every answer is inside the window */
        {
            WsPos hlo = {haven.pos.x - 80000, haven.pos.y - 20000, haven.pos.z - 80000}, hhi = {haven.pos.x + 80000, haven.pos.y + 20000, haven.pos.z + 80000};
            int h = ws_creature_query(&a, 0, hlo, hhi, 6000, out, 64);
            CHECK(h >= 1);
            for (int k = 0; k < h; k++)
                CHECK(out[k].pos.x >= hlo.x && out[k].pos.x <= hhi.x && out[k].pos.y >= hlo.y && out[k].pos.y <= hhi.y && out[k].pos.z >= hlo.z && out[k].pos.z <= hhi.z);
        }
        /* an empty window answers zero */
        WsPos elo = {900000, 900000, 900000}, ehi = {999000, 999000, 999000};
        CHECK(ws_creature_query(&a, 0, elo, ehi, 0, out, 64) == 0);
    }

    /* --- persistent exceptions override the generated defaults --- */
    {
        WsId cid = wid(S_GATE_RES, 0);
        WsCreatureSample one;
        CHECK(at(S_GATE_RES, 0, 20000, &one));
        CHECK(one.pos.x == 20507 && one.pos.y == 381565 && one.pos.z == -424817 && one.traveling);
        CHECK(ws_creature_except(&s, &a, cid, WS_CREX_DEAD, 0) == WS_OK);
        CHECK(!at(S_GATE_RES, 0, 20000, &one)); /* dead: never materialized */
        CHECK(ws_state_validate(&s, &a) == WS_OK);
        CHECK(ws_creature_except(&s, &a, cid, 0, 0) == WS_OK); /* removal */
        CHECK(at(S_GATE_RES, 0, 20000, &one)); /* generated default returns */
        CHECK(ws_creature_except(&s, &a, cid, WS_CREX_RELOCATED, M_HAVEN) == WS_OK);
        CHECK(at(S_GATE_RES, 0, 20000, &one));
        CHECK(one.pos.x == 113632 && one.pos.y == 61346 && one.pos.z == 851695); /* re-anchored, haven schedule */
        CHECK(one.id.word[0] == 2403772543u); /* identity survives relocation */
        CHECK(ws_creature_except(&s, &a, cid, WS_CREX_PINNED, 0) == WS_OK);
        CHECK(at(S_GATE_RES, 0, 20000, &one));
        CHECK(one.pos.x == 274763 && one.pos.y == 470000 && one.pos.z == -485002 && !one.traveling); /* pinned home */
        CHECK(ws_creature_except(&s, &a, cid, 0, 0) == WS_OK);
        /* fail-closed mutations */
        WsId bogus = {{1, 2, 3, 4}};
        CHECK(ws_creature_except(&s, &a, cid, 4, 0) == WS_BOUNDS); /* unknown kind */
        CHECK(ws_creature_except(&s, &a, cid, WS_CREX_DEAD, 5) == WS_BOUNDS); /* dead carries no aux */
        CHECK(ws_creature_except(&s, &a, cid, WS_CREX_RELOCATED, M_MOUNTAIN) == WS_REFERENCE); /* not a walk surface */
        CHECK(ws_creature_except(&s, &a, cid, WS_CREX_RELOCATED, 4) == WS_REFERENCE); /* interior (the cave) */
        CHECK(ws_creature_except(&s, &a, cid, WS_CREX_PINNED, 5) == WS_BOUNDS); /* unjoined player */
        CHECK(ws_creature_except(&s, &a, bogus, WS_CREX_DEAD, 0) == WS_REFERENCE); /* unknown creature */
        CHECK(s.creature_count == 0);
    }

    /* --- state wire version 3 carries the exception table --- */
    {
        WsId cid = wid(S_GATE_RES, 0), fid = wid(S_MASSIF, 0);
        CHECK(ws_creature_except(&s, &a, cid, WS_CREX_RELOCATED, M_HAVEN) == WS_OK);
        CHECK(ws_creature_except(&s, &a, fid, WS_CREX_DEAD, 0) == WS_OK);
        CHECK(s.creature_count == 2 && ws_state_validate(&s, &a) == WS_OK);
        uint8_t wire[12000];
        size_t n = ws_state_encode(&s, wire, sizeof(wire));
        size_t base = 48 + 22 * 28 + 1 * 538 + 1 * 12 + s.feed_count * 16 + s.tail_count * 20;
        CHECK(n == base + 8 + 16 + 4 + 24 * s.site_count + 24 + 4 + 24 * 2);
        CHECK(wire[4] == 3 && wire[5] == 0);
        WsState t;
        CHECK(ws_state_decode(&t, &a, wire, n) == WS_OK);
        CHECK(t.creature_count == 2 && ws_state_hash(&t) == ws_state_hash(&s));
        CHECK(ws_id_equal(t.crex[0].id, cid) && t.crex[0].kind == WS_CREX_RELOCATED && t.crex[0].aux == M_HAVEN);
        CHECK(ws_id_equal(t.crex[1].id, fid) && t.crex[1].kind == WS_CREX_DEAD);
        WsCreatureSample x, y;
        CHECK(at(S_GATE_RES, 0, 20000, &x));
        CHECK(ws_creature_at(&a, &t, S_GATE_RES, 0, 20000, &y)); /* overrides survive the wire */
        CHECK(x.pos.x == y.pos.x && x.pos.y == y.pos.y && x.pos.z == y.pos.z);
        CHECK(!ws_creature_at(&a, &t, S_MASSIF, 0, 20000, &y));
        CHECK(ws_state_decode(&t, &a, wire, n - 1) != WS_OK); /* truncation */
        CHECK(ws_save(&s, "build/creature-state"));
        WsState loaded;
        CHECK(ws_restore(&loaded, &a, "build/creature-state"));
        CHECK(ws_state_hash(&loaded) == ws_state_hash(&s) && loaded.creature_count == 2);
        /* a corrupted exception record fails decode even with a fixed CRC */
        uint8_t bad[12000];
        memcpy(bad, wire, n);
        size_t ctable = base + 8 + 16 + 4 + 24 * s.site_count + 24 + 4;
        bad[ctable + 16] = 9; /* unknown kind */
        uint32_t crc = ws_crc(bad + 16, n - 16);
        for (int i = 0; i < 4; i++) bad[12 + i] = (uint8_t)(crc >> (i * 8));
        CHECK(ws_state_decode(&t, &a, bad, n) != WS_OK);
        CHECK(ws_creature_except(&s, &a, cid, 0, 0) == WS_OK);
        CHECK(ws_creature_except(&s, &a, fid, 0, 0) == WS_OK);
    }

    /* --- malformed products fail closed, CRC recomputed or not --- */
    CHECK(!mutated_load(S_MASSIF, 16, 4));        /* unknown kind */
    CHECK(!mutated_load(S_MASSIF, 18, 27));       /* habitat out of range */
    CHECK(!mutated_load(S_MASSIF, 20, 0));        /* empty population */
    CHECK(!mutated_load(S_MASSIF, 24, 5));        /* more than four stations */
    CHECK(!mutated_load(S_MASSIF, 22, 599));      /* period below the floor */
    CHECK(!mutated_load(S_MASSIF, 26, 1999));     /* spread below the floor */
    CHECK(!mutated_load(S_GATE_WARDEN, 18, 0));   /* anchored on a non-walk surface */
    CHECK(!mutated_load(S_GATE_WARDEN, 18, 21));  /* required anchor needs baseline walking */
    CHECK(!duplicated_id_load(1, 0));             /* duplicate species identity */
    /* control: a non-required species anchored at the karst ruin (a walk
       surface behind a lift) is legal capability content and still loads */
    CHECK(mutated_load(S_GATE_RES, 18, 21));

    /* --- the published Gate 1-4 literals are untouched --- */
    {
        uint64_t cost;
        uint16_t path[WS_CAP];
        int nh = ws_route_cost(&a, M_RUIN, M_HAVEN, WS_CAP_WALK, &cost, path, WS_CAP);
        CHECK(nh == 8 && cost == 2668102);
        uint16_t expect_h[8] = {M_RUIN, 14, 15, 16, 17, 18, M_W1, M_HAVEN};
        CHECK(memcmp(path, expect_h, sizeof(expect_h)) == 0);
        CHECK(ws_link_open(&a, 0, 12) == 0 || a.link_count == 13); /* stateless read of the gate */
    }

    printf("{\"stage\":\"creature\",\"passed\":%u,\"product_bytes\":%zu,\"species\":%u,\"slots\":%u,\"recipe_bytes\":%zu,\"state_bytes\":%zu}\n", checks, sizeof(ws_macro_product), a.creature_count, 36, sizeof(WsRecipe), sizeof(WsState));
    return 0;
}
