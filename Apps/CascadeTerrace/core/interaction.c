#include "interaction.h"
#include <string.h>

int ct_interaction_select(const Game* g, const CtInteraction* candidates, size_t count, CtInteraction* out) {
    CtInteraction selected = {0};
    uint64_t best = UINT64_MAX;
    if (!out) return 0;
    if (!g || !candidates) { memset(out, 0, sizeof(*out)); return 0; }
    for (size_t i = 0; i < count; i++) {
        const CtInteraction* c = &candidates[i];
        if (!c->entity_id || c->kind == CT_INTERACT_NONE || !c->range_mm || c->range_mm > 10000) continue;
        int64_t dx = (int64_t)c->position.x - g->state.player_pos.x;
        int64_t dy = (int64_t)c->position.y - g->state.player_pos.y;
        int64_t dz = (int64_t)c->position.z - g->state.player_pos.z;
        int64_t range = c->range_mm;
        /* Bound before squaring so even malformed external positions cannot overflow. */
        if (dx < -range || dx > range || dy < -range || dy > range || dz < -range || dz > range) continue;
        uint64_t distance = (uint64_t)(dx * dx + dy * dy + dz * dz);
        if (distance > (uint64_t)(range * range) || !can_stand(g, g->state.player_pos, 300) ||
            !walk_edge(g, g->state.player_pos, c->position)) continue;
        if (distance < best || (distance == best && (c->entity_id < selected.entity_id ||
            (c->entity_id == selected.entity_id && c->kind < selected.kind)))) {
            best = distance;
            selected = *c;
        }
    }
    *out = selected;
    return selected.kind != CT_INTERACT_NONE;
}

