#ifndef CASCADE_RENDER_H
#define CASCADE_RENDER_H
#include <stdint.h>
#include "game.h"
#include "../world/sdk.h"
#include "../world/state.h"
typedef struct {
    uint16_t pixels[W * H], depth[W * H];
    uint32_t triangles, pixels_written;
    uint32_t raster_candidates, depth_tests; /* Profiling counters, reset per frame. */
    uint32_t clear_us, scene_us; /* Per-frame stage times; stay 0 without a clock. */
    float camera_x, camera_y, camera_z, yaw, pitch;
    float look_pitch; /* Private local look offset in radians; never saved. */
    int conversation;
    int first_person;
    uint32_t frame;
} Renderer;
/* Optional microsecond clock for per-frame stage timing. Frontends may set it
   (native: esp_timer). Unset stage times stay 0 and rendering is unchanged. */
void render_set_clock(uint64_t (*now_us)(void));
int render_load_assets(const char*);
void render(Renderer*, const Game*);
void render_world(Renderer*,const WsRecipe*,WsAddress,int,WsMetrics*);
/* Optional live-state binding: when set, river strips render through
   ws_river_stage, so regional water drawdown is visible without any fluid
   simulation. Unbound (NULL) rendering stays exactly the declared geometry. */
void render_bind_state(const WsState*);
void render_world_peer(WsPos);
void render_world_marker(WsPos,uint32_t);
void draw_text(Renderer*, int, int, const char*, uint16_t, int);
void draw_panel(Renderer*, int, int, int, int, uint16_t);
int screenshot(const Renderer*, const char*);
#endif
