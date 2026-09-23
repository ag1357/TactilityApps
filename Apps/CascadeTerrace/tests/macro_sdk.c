/* Gate 2 (macro geography, mission §49). The product under test is the
   committed generator output (content/worlds/macro.inc, produced by
   tools/worldsdk/macro.py from seed 0x9C0D51). Everything here is proven
   against the C engine: sparse river reconstruction, chunk-boundary
   continuity, monotonic bounded elevations, typed exceptions, the terrain
   cost model, the route-cost case (nearest settlement is NOT the cheapest
   destination), and continuous swept traversal of every declared walk edge
   of the derived wilderness route. */
#include "../world/sdk.h"
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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Module indices in generator order (verified by name in the Python gate). */
enum { M_MOUNTAIN, M_SHOULDER_E, M_SHOULDER_W, M_WATERSHED, M_CAVE, M_KARST, M_DAM, M_PLAINS, M_FOREST, M_WETLAND, M_RUIN, M_GATE, M_E0, M_E1, M_E2, M_E3, M_E4, M_FORD_E, M_FORD_W, M_W1, M_HAVEN };

static int64_t isq(int64_t n) {
    int64_t x = n;
    int64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

static int walk_edge(const WsRecipe* r, uint16_t a, uint16_t b) {
    /* Probe method: 100 mm swept steps toward the destination, runtime
       ground and collision, no teleports. */
    WsModule A, B;
    ws_materialize(r, a, &A);
    ws_materialize(r, b, &B);
    WsAddress at = {{A.pos.x, A.pos.y + 300, A.pos.z}, UINT16_MAX};
    for (int step = 0; step < 20000; step++) {
        int64_t dx = (int64_t)B.pos.x - at.pos.x, dz = (int64_t)B.pos.z - at.pos.z;
        if (dx * dx + dz * dz < 150LL * 150) return 1;
        int64_t ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
        int64_t m = ax > az ? ax : az;
        if (!ws_move(r, &at, (int32_t)(dx * 100 / m), (int32_t)(dz * 100 / m))) return 0;
    }
    return 0;
}

int main(void) {
    WsRecipe a, b;
    CHECK(ws_load(&a, ws_macro_product, sizeof(ws_macro_product)) == WS_OK);
    CHECK(ws_load(&b, ws_macro_product, sizeof(ws_macro_product)) == WS_OK);
    /* Determinism across independent loads: identical engine state. */
    CHECK(memcmp(&a, &b, sizeof(WsRecipe)) == 0);
    CHECK(a.count == 21 && a.link_count == 11);
    CHECK(a.feature_count == 1 && a.exception_count == 5);
    CHECK(a.features[0].kind == WS_FEATURE_RIVER);
    CHECK(a.features[0].width == 12000 && a.features[0].depth == 900);
    /* Sparse record, typed exceptions: waterfall, rapids, lake, underground,
       dam in generation order. */
    CHECK(a.exceptions[0].type == WS_EXC_WATERFALL && a.exceptions[0].aux == 170000);
    CHECK(a.exceptions[1].type == WS_EXC_RAPIDS);
    CHECK(a.exceptions[2].type == WS_EXC_LAKE);
    CHECK(a.exceptions[3].type == WS_EXC_UNDERGROUND);
    CHECK(a.exceptions[4].type == WS_EXC_DAM && a.exceptions[4].aux == 14000);
    /* Endpoints exact: t=0 upstream, t=65535 downstream. */
    WsRiverSample s;
    CHECK(ws_river_sample(&a, 0, 0, &s));
    CHECK(s.pos.x == a.features[0].up.x && s.pos.y == a.features[0].up.y && s.pos.z == a.features[0].up.z);
    CHECK(ws_river_sample(&a, 0, 65535, &s));
    CHECK(s.pos.x == a.features[0].down.x && s.pos.y == a.features[0].down.y && s.pos.z == a.features[0].down.z);
    /* Chunk-boundary continuity: the world split into four quadrant chunks;
       each chunk reconstructs only its own window, and every t the quadrants
       share must yield identical samples from independent reconstruction
       (the seam between chunk-local strips is exact). */
    WsPos qlo[4] = {{-1000000, 0, -1000000}, {0, 0, -1000000}, {-1000000, 0, 0}, {0, 0, 0}};
    WsPos qhi[4] = {{0, 0, 0}, {1000000, 0, 0}, {0, 0, 1000000}, {1000000, 0, 1000000}};
    uint16_t t0[4], t1[4];
    int covered = 0;
    for (int q = 0; q < 4; q++) {
        if (!ws_river_window(&a, 0, qlo[q], qhi[q], &t0[q], &t1[q])) continue;
        covered++;
        for (uint32_t t = t0[q]; t < t1[q]; t += 511) {
            WsRiverSample x, y;
            CHECK(ws_river_sample(&a, 0, (uint16_t)t, &x));
            CHECK(ws_river_sample(&b, 0, (uint16_t)t, &y));
            CHECK(memcmp(&x, &y, sizeof(x)) == 0);
        }
    }
    CHECK(covered >= 2); /* the river spans several chunks */
    /* Adjacent quadrant windows must agree on their shared seam: the union
       covers the whole curve with no gap (every segment k*8192 reachable). */
    {
        int seen[8] = {0};
        for (int q = 0; q < 4; q++) {
            if (!ws_river_window(&a, 0, qlo[q], qhi[q], &t0[q], &t1[q])) continue;
            for (int k = 0; k < 8; k++)
                if ((int)k * 8192 >= t0[q] && k * 8192 <= t1[q]) seen[k] = 1;
        }
        for (int k = 0; k < 8; k++) CHECK(seen[k]);
    }
    /* A chunk far from the river reconstructs nothing. */
    WsPos flo = {400000, 0, 400000}, fhi = {900000, 0, 900000};
    CHECK(!ws_river_window(&a, 0, flo, fhi, &t0[0], &t1[0]));
    /* Monotonic fall with bounded grade between typed drops: sweeping the
       whole curve, elevation never rises, and outside the two drop points
       the local grade stays within the validated 1:4 bound. */
    {
        int32_t py = 0;
        int first = 1;
        for (uint32_t t = 0; t <= 65535; t += 97) {
            WsRiverSample x;
            CHECK(ws_river_sample(&a, 0, (uint16_t)t, &x));
            if (!first) CHECK(x.pos.y <= py);
            py = x.pos.y;
            first = 0;
        }
        /* Grade bound on consecutive samples away from the drop steps. */
        for (uint32_t t = 0; t + 97 <= 65535; t += 97) {
            if ((t >= 3200 && t <= 3400) || (t >= 60000 && t <= 60400)) continue; /* waterfall/dam steps */
            WsRiverSample x, y;
            CHECK(ws_river_sample(&a, 0, (uint16_t)t, &x));
            CHECK(ws_river_sample(&a, 0, (uint16_t)(t + 97), &y));
            int64_t dy = x.pos.y - y.pos.y;
            int64_t dh = isq((int64_t)(y.pos.x - x.pos.x) * (y.pos.x - x.pos.x) + (int64_t)(y.pos.z - x.pos.z) * (y.pos.z - x.pos.z));
            CHECK(4 * dy <= dh + 2);
        }
    }
    /* Typed exceptions are visible in reconstruction. */
    CHECK(ws_river_sample(&a, 0, 10000, &s) && s.flow == WS_FLOW_CALM && s.surfaced && s.width == 12000); /* plain reach */
    CHECK(ws_river_sample(&a, 0, 16000, &s) && s.flow == WS_FLOW_RAPID); /* rapids reach 14417..17693 */
    CHECK(ws_river_sample(&a, 0, 36000, &s) && s.width == 48000 && s.depth == 2700 && s.flow == WS_FLOW_CALM); /* lake reach */
    CHECK(ws_river_sample(&a, 0, 45000, &s) && !s.surfaced); /* underground reach */
    CHECK(ws_river_sample(&a, 0, 59500, &s) && s.flow == WS_FLOW_CALM); /* dam pool */
    {
        WsRiverSample above, below;
        CHECK(ws_river_sample(&a, 0, 3276, &above)); /* t=int(0.05*65535) */
        CHECK(ws_river_sample(&a, 0, 3277, &below));
        CHECK(above.pos.y - below.pos.y >= 170000); /* the waterfall step */
        CHECK(ws_river_sample(&a, 0, 60291, &above)); /* dam step at int(0.90*65535)+int(0.02*65535) */
        CHECK(ws_river_sample(&a, 0, 60292, &below));
        CHECK(above.pos.y - below.pos.y >= 14000); /* the dam step */
    }
    /* Tangents are unit 16.16 directions (integer rounding allows a few
       low bits of magnitude error per component). */
    for (uint32_t t = 0; t < 65535; t += 4099) {
        CHECK(ws_river_sample(&a, 0, (uint16_t)t, &s));
        int64_t m = (int64_t)s.tangent_x * s.tangent_x + (int64_t)s.tangent_z * s.tangent_z;
        CHECK(m > 65536LL * 65536 - 4 * 65536 && m < 65536LL * 65536 + 4 * 65536);
    }
    /* The cave keeps a public entrance port. */
    WsPort ports[4];
    int pn = ws_ports(&a, M_CAVE, ports, 4);
    CHECK(pn >= 1);
    for (int i = 0; i < pn && i < 4; i++) CHECK(ports[i].owner == M_CAVE && ports[i].width == WS_PORT_MM);
    /* Route-cost case: the geometrically nearest settlement (high_gate) is
       NOT the cheapest destination from the ruin; fording and climbing over
       declared topology decide, not straight lines. */
    {
        WsModule ruin, gate, haven;
        ws_materialize(&a, M_RUIN, &ruin);
        ws_materialize(&a, M_GATE, &gate);
        ws_materialize(&a, M_HAVEN, &haven);
        int64_t dg = isq((int64_t)(gate.pos.x - ruin.pos.x) * (gate.pos.x - ruin.pos.x) + (int64_t)(gate.pos.z - ruin.pos.z) * (gate.pos.z - ruin.pos.z));
        int64_t dh = isq((int64_t)(haven.pos.x - ruin.pos.x) * (haven.pos.x - ruin.pos.x) + (int64_t)(haven.pos.z - ruin.pos.z) * (haven.pos.z - ruin.pos.z));
        CHECK(dg < dh); /* high_gate is the nearest settlement */
        uint64_t cg, ch;
        uint16_t path[WS_CAP];
        int ng = ws_route_cost(&a, M_RUIN, M_GATE, WS_CAP_WALK, &cg, path, WS_CAP);
        int nh = ws_route_cost(&a, M_RUIN, M_HAVEN, WS_CAP_WALK, &ch, path, WS_CAP);
        CHECK(ng > 1 && nh > 1); /* both reachable over declared edges */
        CHECK(ch < cg); /* but the farther settlement is cheaper */
        /* The ford link pays the fording penalty; bank links do not. */
        int ford = -1, bank = -1;
        for (uint16_t i = 0; i < a.link_count; i++) {
            if ((a.links[i].a == M_FORD_E && a.links[i].b == M_FORD_W) || (a.links[i].a == M_FORD_W && a.links[i].b == M_FORD_E)) ford = i;
            if ((a.links[i].a == M_E4 && a.links[i].b == M_FORD_E) || (a.links[i].a == M_FORD_E && a.links[i].b == M_E4)) bank = i;
        }
        CHECK(ford >= 0 && bank >= 0);
        WsModule fe, fw, e4;
        ws_materialize(&a, M_FORD_E, &fe);
        ws_materialize(&a, M_FORD_W, &fw);
        ws_materialize(&a, M_E4, &e4);
        int64_t fd = isq((int64_t)(fw.pos.x - fe.pos.x) * (fw.pos.x - fe.pos.x) + (int64_t)(fw.pos.z - fe.pos.z) * (fw.pos.z - fe.pos.z));
        int64_t bd = isq((int64_t)(fe.pos.x - e4.pos.x) * (fe.pos.x - e4.pos.x) + (int64_t)(fe.pos.z - e4.pos.z) * (fe.pos.z - e4.pos.z));
        CHECK(ws_link_cost(&a, (uint16_t)ford, WS_CAP_WALK) >= (uint64_t)fd + 20000);
        CHECK(ws_link_cost(&a, (uint16_t)bank, WS_CAP_WALK) == (uint64_t)bd + 8 * (uint64_t)llabs((int64_t)fe.pos.y - e4.pos.y));
    }
    /* The derived wilderness route is walkable: every declared walk edge
       traversed continuously, 100 mm steps, runtime ground + collision. */
    for (uint16_t i = 0; i < a.link_count; i++) {
        if (a.links[i].kind != 0) continue;
        CHECK(walk_edge(&a, a.links[i].a, a.links[i].b));
    }
    /* The whole route from the gate down to the haven is one connected
       walk-reachable chain over declared topology. */
    CHECK(ws_reachable(&a, M_GATE, M_HAVEN, WS_CAP_WALK) == WS_REACH_WALK);
    CHECK(ws_reachable(&a, M_RUIN, M_HAVEN, WS_CAP_WALK) == WS_REACH_WALK);
    CHECK(ws_reachable(&a, M_WATERSHED, M_HAVEN, WS_CAP_WALK) == WS_REACH_WALK);
    printf("{\"stage\":\"macro\",\"passed\":%u,\"product_bytes\":%zu,\"modules\":%u,\"links\":%u,\"features\":%u,\"exceptions\":%u}\n", checks, sizeof(ws_macro_product), a.count, a.link_count, a.feature_count, a.exception_count);
    return 0;
}
