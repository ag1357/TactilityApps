#ifndef CT_WORLD_STATE_H
#define CT_WORLD_STATE_H
#include "sdk.h"
#define WS_INVENTORY 7
#define WS_FEED_CAP 16
/* Indices are product-local handles; every envelope binds the ancestry and CRC. */
typedef enum { WS_EXTRACT = 1,
               WS_REPAIR,
               WS_DAMAGE,
               WS_TRANSFER,
               WS_GRANT,
               WS_DECORATE,
               WS_AID,
               WS_KILL,
               WS_PROMISE,
               WS_KEEP_PROMISE,
               /* Resource ops target reservoir indices, not entities. They
                  bind to the global state revision, so regional stocks are
                  strictly sequential, and never merge offline. */
               WS_EXCAVATE,
               WS_CONVERT,
               WS_SPEND } WsAction;
typedef enum { WS_BOUNTY,
               WS_TRADE,
               WS_PLAYER_NOTE,
               WS_WORLD_EVENT,
               WS_RUMOR,
               WS_FACTION_NOTICE,
               WS_DISCOVERY,
               WS_NEWS_RESERVED } WsFeedKind;
typedef struct {
    uint32_t epoch, revision, owner;
    uint16_t quantity, health, decor[4];
    uint8_t alive, public_access, behavior, reserved;
} WsEntity;
typedef struct {
    uint32_t id, sequence, credits;
    uint16_t inventory[WS_INVENTORY];
    uint32_t completed[WS_CAP]; /* Monotone epoch watermark per objective. */
} WsPlayer;
typedef struct {
    int16_t trust, reliability, cooperation, aggression;
    uint16_t confidence, promise;
} WsRelationship;
typedef struct {
    uint32_t revision, actor, epoch;
    uint16_t target, kind;
} WsFeed;
typedef struct {
    uint32_t sequence, epoch, base_revision;
    uint16_t action, target, amount, aux;
} WsOperation;
/* Sparse persistent extraction sites. A PIT is recoverable disturbance: it
   refills from the regional stock while the reservoir recovers. An
   EXCAVATION is intentional topology (foundation, cave entrance) and never
   heals. Sites are accounting records of voids, not matter stocks. */
#define WS_SITE_CAP 32
enum { WS_SITE_PIT = 0, WS_SITE_EXCAVATION = 1 };
typedef struct {
    WsPos pos;
    uint16_t reservoir, kind, extent, reserved;
    uint32_t amount;
} WsSite;
/* Sparse persistent creature exceptions (Gate 5). Generated placement and
   schedules are defaults reconstructed from the product; consequential
   history overrides them per creature, keyed by the stable identity:
   DEAD removes the creature, RELOCATED re-anchors it at a declared walk
   surface, PINNED keeps it at its placed home. Everything else about a
   creature stays generated, so an exception never grows the product. */
#define WS_CREX_CAP 32
enum { WS_CREX_DEAD = 1, WS_CREX_RELOCATED = 2, WS_CREX_PINNED = 3 };
typedef struct {
    WsId id;
    uint16_t kind, aux, reserved, pad;
} WsCreatureEx;
typedef struct {
    WsId ancestry;
    uint32_t recipe_crc, revision;
    uint16_t count, player_count, feed_count, tail_count;
    uint16_t reservoir_count, site_count;
    uint16_t creature_count, creature_pad;
    uint32_t clock_s;
    WsEntity entities[WS_CAP];
    WsPlayer players[WS_PLAYER_CAP];
    WsRelationship relations[WS_PLAYER_CAP][WS_PLAYER_CAP];
    WsFeed feed[WS_FEED_CAP];
    WsOperation tail[WS_TAIL_CAP];
    uint32_t level[WS_RESERVOIR_CAP];
    uint16_t material[WS_PLAYER_CAP], shards[WS_PLAYER_CAP];
    WsSite sites[WS_SITE_CAP];
    WsCreatureEx crex[WS_CREX_CAP];
    uint64_t recovered_total, used_total, lost_total;
} WsState;
typedef struct {
    uint32_t player;
    WsAddress at;
    uint8_t server_authority;
} WsContext;
typedef struct {
    WsError status;
    uint8_t world_changed, rewarded, historical, reserved;
    uint32_t revision;
} WsDisposition;
void ws_state_init(WsState*, const WsRecipe*);
WsError ws_join(WsState*, uint32_t);
WsDisposition ws_apply(WsState*, const WsRecipe*, WsContext, WsOperation);
/* Regional resource layer (resource.c): deterministic weather, rate-based
   recovery with pit healing, kind-aware drawdown, bounded lossy material to
   Phos conversion, and the ledger identity used by ws_state_validate. */
void ws_resources_tick(WsState*, const WsRecipe*, uint32_t delta_s);
int ws_ledger_check(const WsState*, const WsRecipe*);
int ws_river_stage(const WsRecipe*, const WsState*, uint16_t feature, uint16_t t, WsRiverSample*);
/* Nonlocal anomaly gates (link kind WS_LINK_ANOMALY, resource.c): the edge
   is declared in the recipe and anchored by link.reserved to a regional
   Phos reservoir. Whether it may be used is a pure function of canonical
   state, never of special-case coordinates: a gate is open while its
   anchored stock holds at least half the region capacity, so depleting the
   lode removes the shortcut and recharging restores it. With no state
   bound, the declared recipe levels decide (view semantics, like stage).
   ws_route_cost_state mirrors ws_route_cost with closed gates absent;
   ws_use_link_state tries open gates first (lowest link index at a shared
   seat), then the ordinary links unchanged. */
int ws_link_open(const WsRecipe*, const WsState*, uint16_t link);
int ws_route_cost_state(const WsRecipe*, const WsState*, uint16_t a, uint16_t b, uint32_t caps, uint64_t* cost, uint16_t* path, size_t cap);
int ws_use_link_state(const WsRecipe*, const WsState*, WsTraveler*);
/* Routed from ws_apply for action >= WS_EXCAVATE; pi is the actor index. */
WsDisposition ws_resource_apply(WsState*, const WsRecipe*, WsContext, WsOperation, int pi);
/* Sparse creature placement and schedules (creature.c): pure functions of
   (recipe, state exceptions, query time). ws_creature_at resolves one
   creature (0 when it does not exist); ws_creature_query materializes the
   creatures inside a window in (species, slot) order and returns the match
   count, or the negated count when the buffer is too small. State-bound
   only through the exception list; the schedule clock is the caller's. */
int ws_creature_at(const WsRecipe*, const WsState*, uint16_t species, uint16_t slot, uint32_t now_s, WsCreatureSample*);
int ws_creature_query(const WsRecipe*, const WsState*, WsPos lo, WsPos hi, uint32_t now_s, WsCreatureSample* out, int cap);
/* Authority mutation for persistent creature exceptions: kind 0 removes
   the record for id. Fails closed (WS_REFERENCE/WS_FULL/WS_BOUNDS) on
   unknown creatures, bad kinds or a full table; the resulting state must
   still pass ws_state_validate. */
WsError ws_creature_except(WsState*, const WsRecipe*, WsId id, uint16_t kind, uint16_t aux);
/* Is id a creature this recipe can generate (validation helper)? */
int ws_creature_known(const WsRecipe*, WsId);
/* Tail and feed recording shared by the entity and resource op paths. */
void ws_record(WsState*, WsContext, WsOperation, uint16_t kind);
/* base must be a server-retained authenticated branch checkpoint, never a
   client-supplied assertion. Routine merge is deterministic, not AI-mediated. */
WsDisposition ws_merge(WsState*, const WsState*, const WsRecipe*, WsContext, WsOperation);
size_t ws_state_encode(const WsState*, uint8_t*, size_t);
WsError ws_state_decode(WsState*, const WsRecipe*, const uint8_t*, size_t);
WsError ws_state_validate(const WsState*, const WsRecipe*);
uint32_t ws_state_hash(const WsState*);
int ws_save(const WsState*, const char*);
int ws_restore(WsState*, const WsRecipe*, const char*);
#endif
