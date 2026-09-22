/* Compatibility boundary for generation-v1 saves. Game-specific role mapping
   belongs to content, never the World SDK. Terrain and mystery remain legacy. */
#include "../core/game.h"
#include "../world/sdk.h"
#include "worlds/cascade.inc"
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
