#ifndef CT_WORLD_SDK_H
#define CT_WORLD_SDK_H
#include <stddef.h>
#include <stdint.h>
#define WS_SCHEMA 1
/* Products carrying the sparse feature section use schema 2; schema 1
   products remain byte-compatible with every published artifact. */
#define WS_SCHEMA_FEATURES 2
#define WS_GENERATOR 1
#define WS_CAP 128
#define WS_LINK_CAP 256
#define WS_PLAYER_CAP 8
#define WS_TAIL_CAP 16
/* Wire formats are explicitly little endian; never serialize these structs. */
typedef struct {
    uint32_t word[4];
} WsId;
typedef struct {
    int32_t x, y, z;
} WsPos;
typedef enum { WS_WORLD,
               WS_REGION,
               WS_SETTLEMENT,
               WS_DISTRICT,
               WS_STRUCTURE,
               WS_FLOOR,
               WS_ROOM,
               WS_OBJECT } WsLevel;
typedef enum { WS_UNLOADED,
               WS_PROXY,
               WS_MATERIALIZED,
               WS_ACTIVE } WsFidelity;
typedef enum { WS_BOX,
               WS_PLANE,
               WS_RAMP,
               WS_ROOM_MODULE } WsShape;
typedef enum { WS_OK,
               WS_FORMAT,
               WS_VERSION,
               WS_BOUNDS,
               WS_DUPLICATE,
               WS_REFERENCE,
               WS_DISCONNECTED,
               WS_DENIED,
               WS_STALE,
               WS_FULL } WsError;
enum { WS_WALK = 1,
       WS_SOLID = 2,
       WS_REPAIRABLE = 4,
       WS_PROPERTY = 8,
       WS_NPC = 16,
       WS_RESOURCE = 32,
       WS_INTERIOR = 64 };
typedef struct {
    WsId id;
    uint16_t parent, kind, flags, phos;
    WsPos pos, size;
    uint32_t color, seed;
    int16_t jitter_x, jitter_z;
    uint16_t affinity, quantity;
} WsModule;
typedef struct {
    uint16_t a, b, kind, reserved;
} WsLink;
/* Sparse world-scale features (rivers now; roads, ridges, canyons, coastlines
   and Phos flows later). A feature is reconstructed independently per chunk
   from its compact record: no fluid simulation, no global polyline. */
#define WS_FEATURE_CAP 8
#define WS_EXCEPTION_CAP 24
enum { WS_FEATURE_RIVER = 1 };
enum { WS_EXC_WATERFALL = 1, WS_EXC_RAPIDS = 2, WS_EXC_LAKE = 3, WS_EXC_DAM = 4, WS_EXC_UNDERGROUND = 5 };
enum { WS_FLOW_CALM = 0, WS_FLOW_RAPID = 1 };
typedef struct {
    WsId id;
    uint16_t kind, flow;
    WsPos up, down;
    uint16_t width, depth;
    uint32_t seed, reserved;
} WsFeature;
typedef struct {
    uint16_t feature, type, at, length;
    uint32_t aux;
} WsException;
typedef struct {
    WsId ancestry;
    uint32_t seed, epoch, revision, recipe_crc;
    uint16_t count, link_count, feature_count, exception_count;
    WsModule modules[WS_CAP];
    WsLink links[WS_LINK_CAP];
    WsFeature features[WS_FEATURE_CAP];
    WsException exceptions[WS_EXCEPTION_CAP];
} WsRecipe;
typedef struct {
    WsPos pos;
    uint16_t scope;
} WsAddress;
typedef struct {
    uint32_t triangles, vertices, active, materialized, proxy;
} WsMetrics;
uint32_t ws_hash(uint32_t);
uint32_t ws_crc(const void*, size_t);
WsId ws_child_id(WsId, uint32_t, uint32_t);
int ws_id_equal(WsId, WsId);
const char* ws_error(WsError);
WsError ws_validate(const WsRecipe*);
WsError ws_load(WsRecipe*, const uint8_t*, size_t);
void ws_materialize(const WsRecipe*, uint16_t, WsModule*);
int ws_route(const WsRecipe*, uint16_t, uint16_t, const uint8_t*, uint16_t*, size_t);
WsFidelity ws_fidelity(WsPos, WsPos, int);
typedef void (*WsBoxFn)(void*, WsPos, WsPos, uint32_t);
void ws_boxes(const WsModule*, WsBoxFn, void*);
int ws_collision(const WsRecipe*, WsAddress, int32_t);
int ws_surface(const WsRecipe*, WsAddress, int32_t, int32_t*);
uint16_t ws_witness(const WsRecipe*, WsAddress, WsAddress, uint16_t, uint16_t);
typedef struct {
    WsAddress at, destination;
    uint32_t remaining;
} WsTraveler;
int ws_ground(const WsRecipe*, WsAddress, int32_t, int32_t*);
int ws_move(const WsRecipe*, WsAddress*, int32_t, int32_t);
int ws_use_link(const WsRecipe*, WsTraveler*);
void ws_travel_tick(WsTraveler*, uint32_t);
/* Spatial surfaces: a module's pos.y is the elevation of its walkable top
   surface. Emitted solid geometry occupies [pos.y - height, pos.y]; room
   walls extend to [pos.y - WS_FLOOR_MM, pos.y + size.y]. */
#define WS_FLOOR_MM 300
/* Access ports: declared walk topology is generated before geometry. Every
   walk edge crossing a room wall cuts a doorway; every room keeps one
   default public entrance port on its south wall when it fits. Visual
   openings are collision openings. */
#define WS_PORT_MM 4000
#define WS_PORT_WALL_MM 300
#define WS_PORT_MAX 96
typedef enum { WS_WALL_NORTH, WS_WALL_EAST, WS_WALL_SOUTH, WS_WALL_WEST } WsWall;
enum { WS_PORT_WALK = 0 };
typedef struct {
    WsPos pos;
    uint16_t owner, wall, mode, width;
} WsPort;
void ws_geometry(const WsRecipe*, uint16_t, WsBoxFn, void*);
int ws_ports(const WsRecipe*, uint16_t, WsPort*, int);
/* Capability-aware reachability: WALK uses baseline walk edges only; ABILITY
   needs lift/portal links covered by the declared capability set; CONDITIONAL
   needs capabilities outside the set; INACCESSIBLE has no route between two
   walk surfaces; INVALID queries a non-walk endpoint. */
typedef enum {
    WS_REACH_WALK,
    WS_REACH_ABILITY,
    WS_REACH_CONDITIONAL,
    WS_REACH_INACCESSIBLE,
    WS_REACH_INVALID
} WsReach;
enum { WS_CAP_WALK = 1u, WS_CAP_LIFT = 2u, WS_CAP_PORTAL = 4u };
WsReach ws_reachable(const WsRecipe*, uint16_t, uint16_t, uint32_t);
/* Walk-edge realizability: coordinate frames, legal access ports, unblocked
   direct routes and baseline-walkable slopes. ws_validate calls this. */
WsError ws_topology(const WsRecipe*);
/* River reconstruction from the sparse record. t is the curve parameter in
   [0, 65535]; t=0 is the upstream endpoint, t=65535 the downstream endpoint.
   Pure function of (record, t): adjacent chunks evaluating a shared boundary
   t get identical position, elevation, tangent, width and flow class. */
typedef struct {
    WsPos pos;
    int32_t tangent_x, tangent_z; /* unit direction in 16.16 */
    uint16_t width, depth, flow;
    uint16_t surfaced; /* 0 inside underground reaches */
} WsRiverSample;
int ws_river_sample(const WsRecipe*, uint16_t feature, uint16_t t, WsRiverSample*);
/* t range whose curve can intersect a chunk window; the caller samples only
   this range, so reconstruction stays chunk-local. */
int ws_river_window(const WsRecipe*, uint16_t feature, WsPos lo, WsPos hi, uint16_t* t0, uint16_t* t1);
/* Deterministic terrain-aware travel cost over declared topology. Walk cost
   is horizontal length plus 8x elevation change plus a fording penalty per
   river crossing; lifts and portals cost fixed amounts. Links whose
   capability is outside caps cost UINT64_MAX. This is a navigation query,
   not a transport system. */
uint64_t ws_link_cost(const WsRecipe*, uint16_t link, uint32_t caps);
int ws_route_cost(const WsRecipe*, uint16_t a, uint16_t b, uint32_t caps, uint64_t* cost, uint16_t* path, size_t cap);
/* All coordinates are bounded local millimetres, not global float positions. */
#endif
