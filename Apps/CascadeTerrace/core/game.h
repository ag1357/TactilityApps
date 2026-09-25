#ifndef CASCADE_GAME_H
#define CASCADE_GAME_H
#include <stddef.h>
#include <stdint.h>
#define GEN_VERSION 1
#define MAP_N 81
/* Bounded internal render budget. The platform fits this image to its current
   content viewport; these dimensions do not describe a physical display. */
#define W 160
#define H 240
#define EVENT_CAP 96
#define MEMORY_CAP 32
#define MAX_FACTS 48
#define PLAYER_ID 100
#define KYRA_ID 1
#define MM 1000
/* Canonical coordinates are millimetres; time is simulated milliseconds. */
typedef struct {
    int32_t x, y, z;
} Pos;
typedef enum { STATION,
               HOME,
               MARKET,
               BRIDGE,
               TUNNEL,
               STRUCT_COUNT } SiteId;
typedef enum { UNKNOWN,
               KNOWLEDGE,
               BELIEF,
               RUMOR,
               CLAIM,
               MEMORY } Epistemic;
typedef enum { OP_NONE,
               EXTRACT,
               CONDENSE,
               REPAIR,
               DAMAGE,
               PICK_UP,
               DROP,
               GIVE,
               TAKE,
               BUY,
               SELL,
               TRANSFER,
               SHOW,
               TELL,
               PROMISE,
               WAIT,
               FUND,
               BREAK_PROMISE } OpCode;
typedef enum { IT_CHIT,
               IT_COUPLING,
               IT_CABLE,
               IT_LOG,
               IT_CONCENTRATOR,
               IT_TOOLKIT,
               IT_KEY,
               IT_NOTE,
               IT_COUNT } ItemId;
typedef struct {
    uint32_t id;
    int64_t time;
    uint32_t actor, target;
    uint16_t op, item;
    int32_t amount;
    uint8_t status;
    char text[128];
} Event;
typedef struct {
    int32_t quantity[20];
} Inventory;
typedef struct {
    uint32_t player;
    int16_t trust;
    uint16_t count;
    Event memories[MEMORY_CAP];
} NpcMemory;
typedef struct {
    Pos center;
    int32_t halfx, halfz, height;
} Site;
typedef struct {
    uint32_t seed, version;
    int16_t height[MAP_N * MAP_N];
    Site sites[STRUCT_COUNT];
    Pos evidence[2];
    uint8_t variant;
} Generated;
typedef struct {
    uint32_t seed, version, sequence, save_generation, event_next;
    int64_t time, field_start;
    int32_t field_level, field_k;
    uint8_t forced_variant; /* 255 derives truth from seed; other values are explicit harness overrides. */
    uint8_t repaired, funded, evidence_taken, evidence_shown, has_field_delta;
    uint8_t promise_state;
    int64_t promise_deadline;
    Inventory player, kyra, market;
    NpcMemory npc;
    Pos player_pos;
    int32_t yaw, vertical_speed;
    uint8_t grounded;
    uint16_t event_count;
    Event events[EVENT_CAP];
} State;
typedef enum { CT_LOCOMOTION_IDLE, CT_LOCOMOTION_WALK,
               CT_LOCOMOTION_RUN, CT_LOCOMOTION_AIR } CtLocomotion;
/* Transient presentation state, deliberately outside the saved State wire.
   Actor orientation is independent of the local camera's legacy State.yaw. */
typedef struct {
    int32_t facing;
    int32_t move_x, move_z; /* Actual displacement in the last 20 ms step. */
    uint32_t phase_milliradians;
    CtLocomotion locomotion;
} CtActorPresentation;
typedef struct {
    Generated world;
    State state;
    uint32_t substep;
    char notice[192];
    CtActorPresentation actor;
} Game;
typedef struct {
    int16_t forward, strafe, turn;
    uint8_t jump, run, meditate;
} Input;
typedef struct {
    OpCode op;
    uint32_t actor, target;
    ItemId item;
    int32_t amount;
    const char* text;
} Operation;
typedef struct {
    uint32_t id;
    uint32_t source;
    Epistemic status;
    char subject[40], relation[48], object[128];
} Fact;
typedef struct {
    Fact facts[MAX_FACTS];
    uint16_t count;
    uint32_t focus;
} NpcView;
typedef struct {
    char text[1024];
    uint32_t evidence[8];
    uint8_t statuses[8], count, abstained;
} Reply;
typedef struct {
    uint32_t subject;
    char relation[48];
    uint32_t last_ids[8];
    uint8_t last_count;
} Conversation;
uint32_t hash32(uint32_t x);
void game_new(Game*, uint32_t seed, int variant);
void generate(Generated*, uint32_t, int);
int32_t ground_at(const Generated*, int32_t, int32_t);
Pos npc_position(const Game*);
int walk_edge(const Game*, Pos, Pos);
int can_stand(const Game*, Pos, int32_t);
void game_tick(Game*, Input, uint32_t real_ms);
int game_apply(Game*, Operation);
int32_t field_level(const Game*, int64_t time);
int market_price(const Game*);
void world_advance(Game*, int64_t game_ms);
int validate_world(const Generated*, char*, size_t);
void npc_view(const Game*, NpcView*);
void dialogue(Game*, Conversation*, const char*, int compositional, Reply*);
void cognition_dialogue(Game*, Conversation*, const char*, Reply*);
void general_dialogue(Game*, Conversation*, const char*, int, Reply*);
int save_game(Game*, const char*);
int load_game(Game*, const char*);
size_t state_encode(const State*, uint8_t*, size_t);
int state_decode(State*, const uint8_t*, size_t);
uint32_t crc32(const void*, size_t);
const char* site_name(int);
const char* item_name(int);
const char* op_name(int);
#endif
