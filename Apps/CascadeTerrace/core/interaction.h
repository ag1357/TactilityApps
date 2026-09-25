#ifndef CT_INTERACTION_H
#define CT_INTERACTION_H
#include "game.h"

typedef enum { CT_INTERACT_NONE, CT_INTERACT_NPC, CT_INTERACT_PICKUP,
               CT_INTERACT_REPAIR, CT_INTERACT_TRADE } CtInteractionKind;
typedef struct {
    uint32_t entity_id;
    CtInteractionKind kind;
    Pos position;
    uint32_t range_mm;
    Operation operation; /* Affordance command; NPCs never carry a substitute NPC. */
    const char* label;
} CtInteraction;

/* Generic bounded selection over entities materialized by the current content.
   Zeroes out on no target. Range is 3D; accessibility uses existing traversal.
   Distance wins, then stable identity, then kind, independent of input order. */
int ct_interaction_select(const Game*, const CtInteraction*, size_t, CtInteraction*);
/* Current Cascade content adapter: only actual rendered/authored entities. */
int ct_interaction_resolve(const Game*, CtInteraction*);
/* Refresh a selected identity against current materialized content and reachability. */
int ct_interaction_refresh(const Game*, uint32_t entity_id, CtInteraction*);
int ct_interaction_dialogue_supported(const CtInteraction*);
#endif
