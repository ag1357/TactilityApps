#ifndef ANAPHORUM_MULTIPLAYER_CLIENT_H
#define ANAPHORUM_MULTIPLAYER_CLIENT_H
#include "../core/game.h"
#include <stdint.h>
#define MP_PEER_CAP 32
/* Identity namespaces are separate even when their storage width matches. */
typedef struct {uint8_t bytes[16];} MpAccountId;
typedef struct {uint8_t bytes[16];} MpPlayerId;
typedef struct {uint8_t bytes[16];} MpDeviceId;
typedef struct {uint8_t bytes[16];} MpSessionId;
typedef struct {uint8_t bytes[16];} MpWorldId;
typedef struct {MpPlayerId id;Pos pos;int facing;uint32_t phase;} MpPeer;
typedef struct MpClient MpClient;
/* POSIX transport is isolated here. Native transport can implement the same
 * message mailbox without changing simulation or the identity protocol. */
MpClient *mp_open(const char *host,unsigned port,const char *identity_path);
void mp_close(MpClient *);
/* Called from the normal gameplay thread; worker never touches Game/LVGL. */
void mp_pump(MpClient *,Game *);
void mp_motion(MpClient *,Game *,Input,unsigned ms);
int mp_action(MpClient *,OpCode,ItemId,int amount,unsigned legacy_target);
/* A disconnected enrolled device can journal a bounded local history.
 * Server independently accepts/denies each event; local state is no proof. */
int mp_offline_action(MpClient *,Game *,Operation);
/* Separate local cache never overwrites the standalone offline save. */
int mp_local_cache(MpClient *,Game *,int load);
unsigned mp_peers(MpClient *,MpPeer *,unsigned cap);
int mp_connected(MpClient *);
void mp_report(MpClient *,const char *path);
#endif
