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
    WsTraveler traveler = {.at = {{lift.pos.x, lift.pos.y + 300, lift.pos.z}, UINT16_MAX}};
    CHECK(ws_use_link(&r, &traveler));
    CHECK(traveler.remaining > 0);
    ws_travel_tick(&traveler, 1000);
    CHECK(traveler.at.pos.y > lift.pos.y + 300 && traveler.remaining > 0);
    ws_travel_tick(&traveler, 20000);
    CHECK(traveler.at.pos.y == 27300 && !traveler.remaining);
    WsModule home;
    ws_materialize(&r, 5, &home);
    traveler.at = (WsAddress) {{home.pos.x, home.pos.y + 300, home.pos.z}, UINT16_MAX};
    CHECK(ws_use_link(&r, &traveler));
    CHECK(traveler.at.scope == 14);
    CHECK(ws_use_link(&r, &traveler));
    CHECK(traveler.at.scope == UINT16_MAX);
    WsAddress a = {{home.pos.x, home.pos.y + 300, home.pos.z}, UINT16_MAX};
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
    CHECK(floor == home.pos.y + 300);
    a.pos.y = floor;
    CHECK(ws_move(&r, &a, 0, 100));
    a.pos.x = home.pos.x + home.size.x / 2 - 400;
    CHECK(!ws_move(&r, &a, 600, 0));
    r.modules[1].id = r.modules[0].id;
    CHECK(ws_validate(&r) == WS_DUPLICATE);
    printf("{\"stage\":\"traversal\",\"passed\":%u,\"recipe_workspace_bytes\":%zu,\"product_bytes\":%zu}\n", checks, sizeof(WsRecipe), sizeof(ws_product));
    return 0;
}
