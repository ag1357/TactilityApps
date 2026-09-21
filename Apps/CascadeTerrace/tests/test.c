#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static int checks, failures;
#define CHECK(x)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(x)) {                                                      \
            failures++;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
        }                                                                \
    } while (0)
static Game g, loaded;
static Renderer renderer;
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
int main(void) {
    char err[160];
    double start = now();
    for (uint32_t seed = 0; seed < 100; seed++) {
        generate(&g.world, seed, -1);
        CHECK(validate_world(&g.world, err, sizeof(err)));
        if (failures) fprintf(stderr, "%s\n", err);
        Generated b;
        generate(&b, seed, -1);
        CHECK(!memcmp(&b, &g.world, sizeof(b)));
    }
    double gentime = now() - start;
    game_new(&g, 42, -1);
    CHECK(sizeof(NpcMemory) <= 8192);
    CHECK(sizeof(State) < 65536);
    CHECK(sizeof(Renderer) + sizeof(Game) + 32768 < 400 * 1024);
    for (int repaired = 0; repaired <= 1; repaired++)
        for (int minutes = 0; minutes < 1440; minutes += 7) {
            g.state.repaired = (uint8_t)repaired;
            g.state.field_level = 100000;
            g.state.field_start = g.state.time;
            double eq = repaired ? 1000000 * (1 - 4000. / 9600) : 750000;
            int actual = field_level(&g, g.state.time + (int64_t)minutes * 60000);
            int expected = (int)(eq + (100000 - eq) * exp(-.0096 * minutes));
            CHECK(abs(actual - expected) < 8);
        }
    game_new(&g, 42, -1);
    int64_t initial = g.state.time;
    game_tick(&g, (Input) {0}, 100);
    CHECK(g.state.time - initial == 3000);
    Pos p = g.state.player_pos;
    for (int i = 0; i < 50; i++) game_tick(&g, (Input) {.forward = 1000}, 20);
    CHECK(abs(g.state.player_pos.z - p.z + 3000) < 100);
    for (int i = 0; i < 200; i++) game_tick(&g, (Input) {.jump = i == 0}, 20);
    CHECK(g.state.grounded);
    for (int variant = 0; variant < 3; variant++) {
        game_new(&g, 42, variant);
        g.state.player_pos = npc_position(&g);
        Conversation c = {0};
        Reply r;
        dialogue(&g, &c, "I saw a strange light near the river.", 1, &r);
        CHECK(g.state.npc.count == 1);
        CHECK(g.state.npc.memories[0].status == CLAIM);
        NpcView v;
        npc_view(&g, &v);
        for (int i = 0; i < v.count; i++) CHECK(v.facts[i].status != UNKNOWN);
        CHECK(!game_apply(&g, (Operation) {REPAIR, PLAYER_ID, 0, IT_COUPLING, 1, NULL}));
        g.state.player.quantity[IT_COUPLING] = 1; /* Explicit test fixture, never gameplay. */
        CHECK(game_apply(&g, (Operation) {REPAIR, PLAYER_ID, 0, IT_COUPLING, 1, NULL}));
        CHECK(g.state.repaired);
        CHECK(g.state.player.quantity[IT_COUPLING] == 0);
        CHECK(g.state.npc.trust == 30);
        CHECK(!game_apply(&g, (Operation) {REPAIR, PLAYER_ID, 0, IT_COUPLING, 1, NULL}));
        dialogue(&g, &c, "I will deliver supplies tomorrow.", 1, &r);
        CHECK(g.state.promise_state == 1);
        world_advance(&g, 86400001);
        CHECK(g.state.promise_state == 3);
        CHECK(save_game(&g, "/tmp/cascade-variant.save"));
        CHECK(load_game(&loaded, "/tmp/cascade-variant.save"));
        CHECK(loaded.world.variant == variant);
    }
    NpcView hidden[3];
    for (int v = 0; v < 3; v++) {
        game_new(&g, 55, v);
        npc_view(&g, &hidden[v]);
    }
    CHECK(!memcmp(&hidden[0], &hidden[1], sizeof(NpcView)));
    CHECK(!memcmp(&hidden[1], &hidden[2], sizeof(NpcView)));
    for (int v = 0; v < 3; v++) {
        game_new(&g, 55, v);
        g.state.player_pos = npc_position(&g);
        g.state.player.quantity[IT_LOG] = 1;
        CHECK(game_apply(&g, (Operation) {SHOW, PLAYER_ID, KYRA_ID, IT_LOG, 1, NULL}));
        NpcView observed;
        npc_view(&g, &observed);
        int found = 0;
        for (int j = 0; j < observed.count; j++)
            if (observed.facts[j].id == 30) {
                found = 1;
                CHECK(!strcmp(observed.facts[j].object, v == 0 ? "recorded as completed" : "not recorded as completed"));
            }
        CHECK(found);
    }
    game_new(&g, 123, -1);
    g.state.player_pos = npc_position(&g);
    Conversation c = {0};
    Reply r;
    dialogue(&g, &c, "I found unusual footprints.", 1, &r);
    g.state.player.quantity[IT_COUPLING] = 1;
    CHECK(game_apply(&g, (Operation) {REPAIR, PLAYER_ID, 0, IT_COUPLING, 1, NULL}));
    uint8_t buf[32768], buf2[32768];
    size_t n = state_encode(&g.state, buf, sizeof(buf));
    CHECK(n > 0 && n < 32768);
    State s;
    CHECK(state_decode(&s, buf, n));
    size_t n2 = state_encode(&s, buf2, sizeof(buf2));
    CHECK(n == n2 && !memcmp(buf, buf2, n));
    const char* path = "/tmp/cascade-test.save";
    unlink("/tmp/cascade-test.save.0");
    unlink("/tmp/cascade-test.save.1");
    CHECK(save_game(&g, path));
    CHECK(load_game(&loaded, path));
    CHECK(loaded.state.repaired && loaded.state.npc.count == g.state.npc.count);
    g.state.player.quantity[IT_CHIT] = 79;
    CHECK(save_game(&g, path));
    FILE* f = fopen("/tmp/cascade-test.save.0", "rb");
    uint8_t filebuf[32768];
    size_t bytes = fread(filebuf, 1, sizeof(filebuf), f);
    fclose(f);
    for (size_t cut = 0; cut < bytes; cut += 31) {
        f = fopen("/tmp/cascade-test.save.0", "wb");
        fwrite(filebuf, 1, cut, f);
        fclose(f);
        CHECK(load_game(&loaded, path));
        CHECK(loaded.state.player.quantity[IT_CHIT] == 0);
    }
    for (size_t pos = 0; pos < bytes; pos += 29) {
        filebuf[pos] ^= 0x80;
        f = fopen("/tmp/cascade-test.save.0", "wb");
        fwrite(filebuf, 1, bytes, f);
        fclose(f);
        CHECK(load_game(&loaded, path));
        CHECK(loaded.state.player.quantity[IT_CHIT] == 0);
        filebuf[pos] ^= 0x80;
    }
    game_new(&g, 42, -1);
    start = now();
    for (int i = 0; i < 60; i++) { render(&renderer, &g); }
    double rendertime = (now() - start) * 1000 / 60;
    CHECK(renderer.triangles > 100);
    CHECK(renderer.pixels_written > 1000);
    printf("{\"checks\":%d,\"failures\":%d,\"seeds\":100,\"seed_validation_ms\":%.3f,\"render_mean_ms\":%.3f,\"game_bytes\":%zu,\"renderer_bytes\":%zu,\"npc_bytes\":%zu,\"state_bytes\":%zu,\"save_bytes\":%zu,\"wire_crc32\":%u,\"physical_status\":\"PENDING\"}\n", checks, failures, gentime * 1000, rendertime, sizeof(Game), sizeof(Renderer), sizeof(NpcMemory), sizeof(State), n, crc32(buf, n));
    return failures ? 1 : 0;
}
