#include "game.h"
#include "render.h"
#include <stdio.h>
#include <string.h>
/* Band equivalence: disjoint row bands drawn sequentially with the band API
 * (both counter slots, arbitrary boundaries) must produce exactly the serial
 * render() pixels, depth and counters. Depth is tested per pixel, so band
 * partition cannot change the outcome; this pins that property. */
static Game g;
static Renderer serial, banded;
static unsigned cases, failures;
static void prepare_case(unsigned seed, int yaw, int fp) {
    game_new(&g, (int)seed, -1);
    g.state.time = 43200000;
    g.state.yaw = yaw;
    memset(&serial, 0xee, sizeof serial);
    memset(&banded, 0xee, sizeof banded);
    serial.first_person = banded.first_person = fp;
}
static void compare(const char* what, unsigned seed, int yaw) {
    if (memcmp(serial.pixels, banded.pixels, sizeof serial.pixels) || memcmp(serial.depth, banded.depth, sizeof serial.depth)) {
        printf("band %s pixels/depth mismatch seed %u yaw %d\n", what, seed, yaw);
        failures++;
        return;
    }
    if (serial.triangles != banded.triangles || serial.pixels_written != banded.pixels_written ||
        serial.raster_candidates != banded.raster_candidates || serial.depth_tests != banded.depth_tests) {
        printf("band %s counter mismatch seed %u yaw %d: tri %u/%u writes %u/%u cand %u/%u tests %u/%u\n",
               what, seed, yaw, serial.triangles, banded.triangles, serial.pixels_written, banded.pixels_written,
               serial.raster_candidates, banded.raster_candidates, serial.depth_tests, banded.depth_tests);
        failures++;
    }
}
static void banded_two(void) {
    render_prepare(&banded, &g);
    render_clear(&banded);
    render_scene(&banded, &g, 0, H / 2);
    render_scene(&banded, &g, H / 2, H);
    render_band_fold(&banded);
}
static void banded_three(void) {
    render_prepare(&banded, &g);
    render_clear(&banded);
    render_scene(&banded, &g, 0, H / 3);
    render_scene(&banded, &g, H / 3, 2 * H / 3);
    render_scene(&banded, &g, 2 * H / 3, H);
    render_band_fold(&banded);
}
static void banded_odd(void) {
    render_prepare(&banded, &g);
    render_clear(&banded);
    render_scene(&banded, &g, 0, 97);
    render_scene(&banded, &g, 97, H - 1);
    render_scene(&banded, &g, H - 1, H);
    render_band_fold(&banded);
}
static void banded_helper_slot(void) {
    render_band_thread(1);
    render_prepare(&banded, &g);
    render_clear(&banded);
    render_scene(&banded, &g, 0, H / 2);
    render_scene(&banded, &g, H / 2, H);
    render_band_thread(0);
    render_band_fold(&banded);
}
int main(void) {
    for (unsigned seed = 1; seed <= 12; seed++)
        for (int yaw = 0; yaw < 360; yaw += 37)
            for (int fp = 0; fp <= 1; fp++) {
                prepare_case(seed, yaw, fp);
                render(&serial, &g);
                banded_two();
                compare("two", seed, yaw);
                cases++;
                /* Same frame again through three bands, an odd boundary, and
                 * the helper counter slot; serial reference re-rendered. */
                render(&serial, &g);
                banded_three();
                compare("three", seed, yaw);
                cases++;
                render(&serial, &g);
                banded_odd();
                compare("odd", seed, yaw);
                cases++;
                render(&serial, &g);
                banded_helper_slot();
                compare("slot", seed, yaw);
                cases++;
            }
    if (failures) { printf("{\"stage\":\"raster-band\",\"cases\":%u,\"failures\":%u}\n", cases, failures); return 1; }
    printf("{\"stage\":\"raster-band\",\"cases\":%u,\"parity\":1}\n", cases);
    return 0;
}
