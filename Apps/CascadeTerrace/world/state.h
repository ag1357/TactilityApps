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
               WS_KEEP_PROMISE } WsAction;
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
typedef struct {
    WsId ancestry;
    uint32_t recipe_crc, revision;
    uint16_t count, player_count, feed_count, tail_count;
    WsEntity entities[WS_CAP];
    WsPlayer players[WS_PLAYER_CAP];
    WsRelationship relations[WS_PLAYER_CAP][WS_PLAYER_CAP];
    WsFeed feed[WS_FEED_CAP];
    WsOperation tail[WS_TAIL_CAP];
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
