#ifndef CT_CASCADE_ADAPTER_H
#define CT_CASCADE_ADAPTER_H
#include "../core/game.h"
#include "../world/state.h"
int cascade_recipe_sites(Generated*, uint32_t);

/* Gate 6 canonical event boundary. The legacy Game stays the gameplay
   authority: every outcome (movement, economy, field, promise state, npc
   trust) is computed there and is byte-identical whether or not the bridge
   is attached. What the bridge adds is the same acts crossing one canonical
   boundary into the authority state: versioned events with retry receipts,
   witness projection onto actual NPCs, and directed evidence, so the social
   layer observes exactly what the game actually did. */
typedef struct {
    WsState ws;        /* canonical authority layer */
    uint32_t sequence; /* canonical command counter (receipt keys) */
    uint16_t station, market, kyra, field; /* recipe cast, checked at init */
    uint8_t last_promise;                  /* legacy promise_state last seen */
} Bridge;
typedef enum {
    BRIDGE_COMMITTED = 0, /* committed on both layers */
    BRIDGE_DENIED,        /* the game refused: nothing crossed */
    BRIDGE_UNMAPPED,      /* the game committed; no canonical mapping (bounded vocabulary) */
    BRIDGE_REFUSED        /* the game committed; the canonical layer refused (e.g. resource exhausted) */
} BridgeStatus;
typedef struct {
    BridgeStatus status;
    WsDisposition d;       /* canonical disposition (status 0 when unmapped) */
    WsOperation canonical; /* the exact canonical op applied: replay replays the receipt */
} BridgeResult;
/* Attach the boundary to a fresh game. Returns 0 when the recipe cast does
   not match the Cascade product (station repairable, market present, kyra
   an NPC, field a resource). */
int bridge_init(Bridge*, Game*, const WsRecipe*);
/* Apply one legacy operation through both layers: the game first (the
   gameplay authority), then its canonical echo. `subject` is the evidence
   subject handle for TELL (claims are about someone; the dialogue layer
   knows who). The legacy State is never written by the bridge. */
BridgeResult bridge_apply(Bridge*, Game*, const WsRecipe*, Operation o, uint32_t subject);
#endif
