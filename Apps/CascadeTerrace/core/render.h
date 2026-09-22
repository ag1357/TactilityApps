#ifndef CASCADE_RENDER_H
#define CASCADE_RENDER_H
#include "game.h"
#include "../world/sdk.h"
typedef struct {
    uint16_t pixels[W * H], depth[W * H];
    uint32_t triangles, pixels_written;
    float camera_x, camera_y, camera_z, yaw, pitch;
    int conversation;
    uint32_t frame;
} Renderer;
int render_load_assets(const char*);
void render(Renderer*, const Game*);
void render_world(Renderer*,const WsRecipe*,WsAddress,int,WsMetrics*);
void render_world_peer(WsPos);
void render_world_marker(WsPos,uint32_t);
void draw_text(Renderer*, int, int, const char*, uint16_t, int);
void draw_panel(Renderer*, int, int, int, int, uint16_t);
int screenshot(const Renderer*, const char*);
#endif
