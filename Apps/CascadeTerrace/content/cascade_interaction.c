/* Authored Cascade cast and affordances. Generic selection lives in core. */
#include "../core/interaction.h"
#include <string.h>

static size_t current_candidates(const Game* g, CtInteraction candidates[5]) {
    size_t count = 0;
    candidates[count++] = (CtInteraction){KYRA_ID, CT_INTERACT_NPC, npc_position(g), 2200,
        {OP_NONE, PLAYER_ID, KYRA_ID, IT_CHIT, 0, NULL}, "Talk to Kyra"};
    for (int i = 0; i < 2; i++) {
        if ((g->state.evidence_taken & (1 << i)) || (!i && g->world.variant == 1)) continue;
        candidates[count++] = (CtInteraction){(uint32_t)(1100 + i), CT_INTERACT_PICKUP,
            g->world.evidence[i], 2000, {PICK_UP, PLAYER_ID, 0, i ? IT_LOG : IT_CABLE, 1, NULL},
            i ? "Pick up maintenance log" : "Pick up tap cable"};
    }
    if (!g->state.repaired) {
        Pos station = g->world.sites[STATION].center;
        station.z += g->world.sites[STATION].halfz;
        candidates[count++] = (CtInteraction){1200, CT_INTERACT_REPAIR, station, 2200,
            {REPAIR, PLAYER_ID, 0, IT_COUPLING, 1, NULL}, "Repair station"};
    }
    candidates[count++] = (CtInteraction){3, CT_INTERACT_TRADE, g->world.sites[MARKET].center, 2500,
        {BUY, PLAYER_ID, 3, IT_COUPLING, 1, NULL}, "Buy replacement coupling"};
    return count;
}
int ct_interaction_resolve(const Game* g, CtInteraction* out) {
    if (!g) { if(out) memset(out,0,sizeof(*out));return 0; }
    CtInteraction candidates[5]={0};size_t count=current_candidates(g,candidates);
    return ct_interaction_select(g,candidates,count,out);
}
int ct_interaction_refresh(const Game* g,uint32_t id,CtInteraction* out) {
    if(out)memset(out,0,sizeof(*out));if(!g||!out||!id)return 0;
    CtInteraction candidates[5]={0};size_t count=current_candidates(g,candidates);
    for(size_t i=0;i<count;i++)if(candidates[i].entity_id==id)
        return ct_interaction_select(g,&candidates[i],1,out);
    return 0;
}

int ct_interaction_dialogue_supported(const CtInteraction* target) {
    return target && target->kind == CT_INTERACT_NPC && target->entity_id == KYRA_ID;
}
