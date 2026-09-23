// Renderer-evaluation config: z-buffered, full width — the apples-to-apples
// match for the current core/render.c pipeline (per-pixel depth, 240x160).
#define JET32_WORLD_SCALE 8
#define RENDER_TILE_BUFFER 0
#define TILE_WIDTH  32
#define TILE_HEIGHT 32
#define FAST_Z 1
#define LAZY_Z 0
#define SCREEN_DOOR_ALPHA 0
#define SKIP_ZERO_AREA_TRIANGLES 1
#define NOISE_ALPHA 0
#define Z_BUFFERING 1
#define SORT_TRIANGLES 0
#define SORT_SCENE_OBJECTS 0
#define SORT_SCENE_REVERSE 0
#define DEPTH_ALPHA_BLEND 0
#define TEXTURE_MAPPING 0
#define PERSPECTIVE_CORRECT_TEXTURES 0
#define BILINEAR_FILTER 0
#define LIGHTING 0
#define Z_BRIGHTNESS 0
#define FLOAT_CAMERA_ANGLES 1
#define FLOAT_SIN_CACHE_SCALE 10
#define FLOAT_TAN_CACHE_SCALE 1
#define HALF_WIDTH_BUFFERS 1
#define FIELD_BUFFERS 0
#define SSR_FIELD_REFLECT 0
#define POSTFX_CRT         0
#define POSTFX_CELLSHADING 0
#define POSTFX_ANTIALIASING 0
#define POSTFX_BLOOM        0
#define POSTFX_MOTION_BLUR  0
#define POSTFX_CHROMATIC    0
#define POSTFX_PIXELATE     0
#define CRT_SCANLINE_INTENSITY 48
#define MOTION_BLUR_STRENGTH   50
#define CHROMATIC_OFFSET        2
#define PIXELATE_SIZE           4
#define CELLSHADING_CELL_BITS   4
#define DEBUG_OVERDRAW 0
#define zBrightFar   (1600 * JET32_WORLD_SCALE)
#define zBrightNear  ( 200 * JET32_WORLD_SCALE)
#define zBrightScale 48
#define depthFogFar  (1<<30) /* eval: fog baked into vertex colors already */
#define depthFogNear (1<<30) /* eval: fog baked into vertex colors already */
#define CHECKERBOARD_MODE 0
#define CHECKERBOARD_RECONSTRUCTION 0
#define MAX_PICK_QUERIES 0
#if defined(ESP_PLATFORM)
#define ESP32
#endif
