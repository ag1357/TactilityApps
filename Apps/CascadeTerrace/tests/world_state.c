#include "../content/worlds/cascade.inc"
#include "../world/state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static WsRecipe r;
static WsState s, base, loaded;
static uint8_t bytes[12000];
static unsigned checks;
#define CHECK(x)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(x)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            return 1;                                                    \
        }                                                                \
    } while (0)
static WsContext ctx(uint32_t player, uint16_t target, int server) {
    WsModule m;
    ws_materialize(&r, target, &m);
    return (WsContext) {player, {{m.pos.x, m.pos.y + 300, m.pos.z}, (m.flags & WS_INTERIOR) ? target : UINT16_MAX}, (uint8_t)server};
}
static WsOperation op(uint16_t action, uint16_t target, uint32_t seq) { return (WsOperation) {seq, s.entities[target].epoch, s.entities[target].revision, action, target, 1, 0}; }
static void setup(void) {
    ws_state_init(&s, &r);
    assert(ws_join(&s, 101) == WS_OK);
    assert(ws_join(&s, 102) == WS_OK);
    /* Explicit qualification provisioning, not a client authority bypass. */
    s.players[0].inventory[1] = 60000;
    s.players[1].inventory[1] = 100;
    s.players[0].inventory[0] = 2;
    s.players[1].inventory[0] = 2;
}
int main(void) {
    CHECK(ws_load(&r, ws_product, sizeof(ws_product)) == WS_OK);
    FILE* fixture = fopen("tests/fixtures/world-sdk-v1.hex", "r");
    CHECK(fixture != NULL);
    size_t fixture_size = 0;
    unsigned byte;
    while (fscanf(fixture, "%2x", &byte) == 1) {
        CHECK(fixture_size < sizeof(bytes));
        bytes[fixture_size++] = (uint8_t)byte;
    }
    fclose(fixture);
    CHECK(ws_state_decode(&loaded, &r, bytes, fixture_size) == WS_OK);
    CHECK(ws_state_encode(&loaded, bytes, sizeof(bytes)) == fixture_size);
    setup();
    base = s;
    WsOperation repair = op(WS_REPAIR, 4, 1);
    WsDisposition d = ws_merge(&s, &base, &r, ctx(101, 4, 0), repair);
    CHECK(d.status == WS_OK && d.rewarded && s.entities[4].health == 100); /* A */
    uint32_t hash = ws_state_hash(&s);
    d = ws_merge(&s, &base, &r, ctx(101, 4, 0), repair);
    CHECK(d.status != WS_OK && ws_state_hash(&s) == hash); /* E */
    d = ws_merge(&s, &base, &r, ctx(102, 4, 0), repair);
    CHECK(d.status == WS_OK && d.rewarded && !d.world_changed && s.players[1].credits == 10); /* B */
    WsOperation damage = op(WS_DAMAGE, 4, 2);
    CHECK(ws_apply(&s, &r, ctx(101, 4, 1), damage).status == WS_OK);
    d = ws_merge(&s, &base, &r, ctx(102, 4, 0), repair);
    CHECK(d.status != WS_OK && s.entities[4].health == 0); /* C */
    repair = op(WS_REPAIR, 4, 3);
    CHECK(ws_apply(&s, &r, ctx(101, 4, 0), repair).status == WS_OK && s.players[0].credits == 20);
    setup();
    base = s;
    repair = op(WS_REPAIR, 4, 1);
    CHECK(ws_apply(&s, &r, ctx(101, 4, 0), repair).status == WS_OK);
    damage = op(WS_DAMAGE, 4, 2);
    CHECK(ws_apply(&s, &r, ctx(101, 4, 1), damage).status == WS_OK);
    CHECK(ws_merge(&s, &base, &r, ctx(102, 4, 0), repair).status == WS_STALE && s.entities[4].health == 0);
    setup();
    base = s;
    WsOperation aid = op(WS_AID, 15, 1);
    WsOperation kill = op(WS_KILL, 15, 1);
    CHECK(ws_apply(&s, &r, ctx(102, 15, 1), kill).status == WS_OK);
    d = ws_merge(&s, &base, &r, ctx(101, 15, 0), aid);
    CHECK(d.status == WS_OK && d.historical && d.rewarded && !s.entities[15].alive); /* D */
    CHECK(ws_merge(&s, &base, &r, ctx(101, 15, 0), aid).status != WS_OK);
    WsOperation grant = op(WS_GRANT, 14, 2);
    CHECK(ws_apply(&s, &r, ctx(101, 14, 0), grant).status == WS_DENIED);
    CHECK(ws_apply(&s, &r, ctx(101, 14, 1), grant).status == WS_OK);
    WsOperation decor = op(WS_DECORATE, 14, 3);
    CHECK(ws_apply(&s, &r, ctx(102, 14, 0), decor).status == WS_DENIED);
    CHECK(ws_apply(&s, &r, ctx(101, 14, 0), decor).status == WS_OK);
    CHECK(s.entities[14].decor[0] == 1 && s.players[0].inventory[0] == 1);
    hash = ws_state_hash(&s);
    CHECK(ws_apply(&s, &r, ctx(101, 14, 0), decor).status == WS_STALE && ws_state_hash(&s) == hash);
    WsOperation transfer = op(WS_TRANSFER, 16, 4);
    transfer.aux = 102;
    transfer.amount = 7;
    CHECK(ws_apply(&s, &r, ctx(101, 16, 0), transfer).status == WS_OK);
    CHECK(s.players[1].inventory[1] == 107);
    WsContext far = ctx(101, 16, 0);
    far.at.pos.x += 100000;
    transfer = op(WS_EXTRACT, 16, 5);
    CHECK(ws_apply(&s, &r, far, transfer).status == WS_DENIED);
    CHECK(ws_apply(&s, &r, ctx(101, 16, 0), transfer).status == WS_OK);
    size_t n = ws_state_encode(&s, bytes, sizeof(bytes));
    CHECK(n > 0 && ws_state_decode(&loaded, &r, bytes, n) == WS_OK && ws_state_hash(&s) == ws_state_hash(&loaded));
    for (size_t i = 0; i < n; i++) CHECK(ws_state_decode(&loaded, &r, bytes, i) != WS_OK);
    bytes[n - 1] ^= 1;
    CHECK(ws_state_decode(&loaded, &r, bytes, n) == WS_FORMAT);
    bytes[n - 1] ^= 1;
    CHECK(ws_save(&s, "build/sdk-state"));
    CHECK(ws_restore(&loaded, &r, "build/sdk-state") && ws_state_hash(&loaded) == ws_state_hash(&s));
    uint32_t fallback = ws_state_hash(&s);
    s.revision += 2;
    CHECK(ws_save(&s, "build/sdk-state"));
    CHECK(ws_restore(&loaded, &r, "build/sdk-state") && loaded.revision == s.revision);
    /* Corrupt whichever slot contains the newer checkpoint. */
    for (int slot = 0; slot < 2; slot++) {
        char path[64];
        snprintf(path, sizeof(path), "build/sdk-state.%d", slot);
        FILE* f = fopen(path, "rb");
        CHECK(f != NULL);
        size_t size = fread(bytes, 1, sizeof(bytes), f);
        fclose(f);
        CHECK(ws_state_decode(&loaded, &r, bytes, size) == WS_OK);
        if (loaded.revision == s.revision) {
            f = fopen(path, "wb");
            CHECK(f != NULL);
            CHECK(fwrite("torn", 1, 4, f) == 4);
            fclose(f);
        }
    }
    CHECK(ws_restore(&loaded, &r, "build/sdk-state") && ws_state_hash(&loaded) == fallback);
    printf("{\"suite\":\"authority_merge_persistence\",\"checks\":%u,\"status\":\"PASS\",\"cases\":[\"A\",\"B\",\"C\",\"D\",\"E\"],\"state_bytes\":%zu,\"wire_bytes\":%zu}\n", checks, sizeof(s), n);
    FILE* report = fopen("results/worldsdk/compaction.json", "w");
    CHECK(report != NULL);
    fprintf(report, "{\"semantics\":\"fixed population: repair, damage, transfers, ownership, room decoration, promises; arbitrary unique history is not compressed\",\"rows\":[");
    const int histories[] = {10, 1000, 10000, 100000};
    for (int run = 0; run < 4; run++) {
        setup();
        WsOperation own = op(WS_GRANT, 14, 1);
        CHECK(ws_apply(&s, &r, ctx(101, 14, 1), own).status == WS_OK);
        int events = histories[run];
        clock_t start = clock();
        for (int i = 0; i < events; i++) {
            static const uint16_t actions[] = {WS_DAMAGE, WS_REPAIR, WS_TRANSFER, WS_TRANSFER, WS_DECORATE, WS_DECORATE, WS_PROMISE, WS_KEEP_PROMISE};
            int k = i % 8, pi = k == 3 ? 1 : 0;
            uint16_t target = k < 2 ? 4 : k < 4 ? 16
                : k < 6                         ? 14
                                                : 15;
            WsOperation o = op(actions[k], target, s.players[pi].sequence + 1);
            if (k == 2 || k == 6 || k == 7) o.aux = 102;
            if (k == 3) o.aux = 101;
            if (k == 5) o.amount = 0;
            CHECK(ws_apply(&s, &r, ctx(s.players[pi].id, target, 1), o).status == WS_OK);
        }
        uint32_t before = ws_state_hash(&s);
        s.tail_count = 0;
        n = ws_state_encode(&s, bytes, sizeof(bytes));
        clock_t restore = clock();
        CHECK(ws_state_decode(&loaded, &r, bytes, n) == WS_OK);
        double us = (clock() - restore) * 1e6 / CLOCKS_PER_SEC;
        CHECK(ws_state_hash(&loaded) == before);
        fprintf(report, "%s{\"events\":%d,\"uncompacted_operation_bytes\":%d,\"recipe_bytes\":%zu,\"resolved_state_bytes\":%zu,\"tail_bytes\":0,\"workspace_bytes\":%zu,\"reconstruct_us\":%.3f,\"history_ms\":%.3f,\"fidelity_hash\":%u,\"exact\":true}", run ? "," : "", events, events * 20, sizeof(ws_product), n, sizeof(s), us, (clock() - start) * 1000. / CLOCKS_PER_SEC, (unsigned)before);
    }
    fprintf(report, "]}\n");
    fclose(report);
    return 0;
}
