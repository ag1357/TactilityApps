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
#include "../content/worlds/cascade.inc"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WsRecipe r;
int main(void) {
    WsId root = {{1, 2, 3, 4}}, ida = ws_child_id(root, 10, 1), idb = ws_child_id(root, 11, 1);
    CHECK(ws_id_equal(ida, ws_child_id(root, 10, 1)));
    CHECK(!ws_id_equal(ida, idb));
    CHECK(ws_crc("123456789", 9) == 0xcbf43926U);
    CHECK(ws_load(&r, ws_product, sizeof(ws_product)) == WS_OK);
    CHECK(r.count == 17);
    WsId region_id = r.modules[1].id;
    uint16_t path[WS_CAP];
    CHECK(ws_route(&r, 4, 14, 0, path, WS_CAP) > 0);
    uint8_t blocked[WS_CAP] = {0};
    blocked[5] = 1;
    CHECK(ws_route(&r, 4, 14, blocked, path, WS_CAP) == 0);
    uint8_t bad[sizeof(ws_product)];
    memcpy(bad, ws_product, sizeof(bad));
    bad[80] ^= 1;
    CHECK(ws_load(&r, bad, sizeof(bad)) == WS_FORMAT);
    CHECK(ws_load(&r, ws_product, sizeof(ws_product)) == WS_OK);
    WsModule lift;
    ws_materialize(&r, 11, &lift);
    WsTraveler traveler = {.at = {{lift.pos.x, lift.pos.y, lift.pos.z}, UINT16_MAX}};
    CHECK(ws_use_link(&r, &traveler));
    CHECK(traveler.remaining > 0);
    ws_travel_tick(&traveler, 1000);
    CHECK(traveler.at.pos.y > lift.pos.y && traveler.remaining > 0);
    ws_travel_tick(&traveler, 20000);
    WsModule top;
    ws_materialize(&r, 12, &top);
    CHECK(traveler.at.pos.y == top.pos.y && !traveler.remaining);
    WsModule home;
    ws_materialize(&r, 5, &home);
    traveler.at = (WsAddress) {{home.pos.x, home.pos.y, home.pos.z}, UINT16_MAX};
    CHECK(ws_use_link(&r, &traveler));
    CHECK(traveler.at.scope == 14);
    CHECK(ws_use_link(&r, &traveler));
    CHECK(traveler.at.scope == UINT16_MAX);
    WsAddress a = {{home.pos.x, home.pos.y, home.pos.z}, UINT16_MAX};
    WsAddress b = a;
    b.pos.x += 3000;
    CHECK(ws_witness(&r, a, b, 1000, 1000) > 0);
    b.pos.x += 10000;
    CHECK(ws_witness(&r, a, b, 1000, 1000) == 0);
    b = a;
    b.pos.z += home.size.z / 2 + 2000;
    CHECK(ws_witness(&r, a, b, 1000, 1000) > 0);
    int32_t floor;
    CHECK(ws_ground(&r, a, home.pos.y + 1000, &floor));
    CHECK(floor == home.pos.y);
    a.pos.y = floor;
    CHECK(ws_move(&r, &a, 0, 100));
    a.pos.x = home.pos.x + home.size.x / 2 - 400;
    CHECK(!ws_move(&r, &a, 600, 0));
    r.modules[1].id = r.modules[0].id;
    CHECK(ws_validate(&r) == WS_DUPLICATE);
    r.modules[1].id = region_id;
    CHECK(ws_validate(&r) == WS_OK);
    /* Access ports: default public entrance plus one port per declared walk
       edge crossing the walls; ports are generated from topology. */
    WsPort ports[8];
    int pn = ws_ports(&r, 5, ports, 8);
    CHECK(pn >= 4);
    int entrance = 0;
    for (int i = 0; i < pn && i < 8; i++) {
        CHECK(ports[i].owner == 5);
        CHECK(ports[i].mode == WS_PORT_WALK);
        CHECK(ports[i].width == WS_PORT_MM);
        if (ports[i].wall == WS_WALL_SOUTH && ports[i].pos.x == home.pos.x) entrance = 1;
    }
    CHECK(entrance);
    WsModule station;
    ws_materialize(&r, 4, &station);
    pn = ws_ports(&r, 4, ports, 8);
    int bridge_port = 0;
    for (int i = 0; i < pn && i < 8; i++)
        if (ports[i].wall == WS_WALL_SOUTH && llabs((int64_t)ports[i].pos.x - station.pos.x) > 4000 && llabs((int64_t)ports[i].pos.x - station.pos.x) < 9000)
            bridge_port = 1;
    CHECK(bridge_port);
    /* Continuous direct traversal of a declared edge, mirroring the probe. */
    WsModule bridge;
    ws_materialize(&r, 7, &bridge);
    WsAddress walker = {{station.pos.x, station.pos.y, station.pos.z}, UINT16_MAX};
    int arrived = 0;
    for (int step = 0; step < 10000 && !arrived; step++) {
        int64_t dx = (int64_t)bridge.pos.x - walker.pos.x, dz = (int64_t)bridge.pos.z - walker.pos.z;
        if (dx * dx + dz * dz < 150LL * 150) {
            arrived = 1;
            break;
        }
        int64_t ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
        int64_t m = ax > az ? ax : az;
        if (!ws_move(&r, &walker, (int32_t)(dx * 100 / m), (int32_t)(dz * 100 / m))) break;
    }
    CHECK(arrived);
    /* The descent edge whose route is overlapped by the collinear
       lift_top->rooftop deck and the bridge->home ramp: the most local
       corridor must keep the walker on its own path all the way down. */
    WsModule lift_base;
    ws_materialize(&r, 11, &lift_base);
    walker = (WsAddress) {{home.pos.x, home.pos.y, home.pos.z}, UINT16_MAX};
    arrived = 0;
    for (int step = 0; step < 10000 && !arrived; step++) {
        int64_t dx = (int64_t)lift_base.pos.x - walker.pos.x, dz = (int64_t)lift_base.pos.z - walker.pos.z;
        if (dx * dx + dz * dz < 150LL * 150) {
            arrived = 1;
            break;
        }
        int64_t ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
        int64_t m = ax > az ? ax : az;
        if (!ws_move(&r, &walker, (int32_t)(dx * 100 / m), (int32_t)(dz * 100 / m))) break;
    }
    CHECK(arrived);
    /* Capability-aware reachability over the same declared topology. */
    CHECK(ws_reachable(&r, 4, 7, 0) == WS_REACH_WALK);
    CHECK(ws_reachable(&r, 4, 12, WS_CAP_WALK) == WS_REACH_CONDITIONAL);
    CHECK(ws_reachable(&r, 4, 12, WS_CAP_WALK | WS_CAP_LIFT) == WS_REACH_ABILITY);
    CHECK(ws_reachable(&r, 4, 14, WS_CAP_WALK) == WS_REACH_CONDITIONAL);
    CHECK(ws_reachable(&r, 4, 14, WS_CAP_WALK | WS_CAP_PORTAL) == WS_REACH_ABILITY);
    CHECK(ws_reachable(&r, 4, 9, WS_CAP_WALK) == WS_REACH_INVALID);
    uint16_t saved_links = r.link_count;
    r.link_count = 0;
    CHECK(ws_reachable(&r, 4, 7, WS_CAP_WALK) == WS_REACH_INACCESSIBLE);
    r.link_count = saved_links;
    CHECK(ws_reachable(&r, 4, 7, WS_CAP_WALK) == WS_REACH_WALK);
    /* Latent interiors expose their own local ports; walk edges that cannot
       host a legal port are rejected by validation. */
    CHECK(ws_ports(&r, 14, ports, 8) >= 1);
    r.modules[7].pos.x = 50000;
    CHECK(ws_validate(&r) == WS_BOUNDS);
    r.modules[7].pos.x = 0;
    CHECK(ws_validate(&r) == WS_OK);
    /* An NPC sealed inside a room with no access ports is invalid content:
       shrink the latent room below entrance width and re-parent the NPC. */
    uint16_t npc_parent = r.modules[15].parent;
    int32_t latent_w = r.modules[14].size.x;
    r.modules[15].parent = 14;
    r.modules[14].size.x = 3000;
    CHECK(ws_validate(&r) == WS_DISCONNECTED);
    r.modules[15].parent = npc_parent;
    r.modules[14].size.x = latent_w;
    CHECK(ws_validate(&r) == WS_OK);
    printf("{\"stage\":\"traversal\",\"passed\":%u,\"recipe_workspace_bytes\":%zu,\"product_bytes\":%zu}\n", checks, sizeof(WsRecipe), sizeof(ws_product));
    return 0;
}
