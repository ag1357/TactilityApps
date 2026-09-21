#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Two independent CRC-protected generations. No raw struct/padding on disk. */
#define SAVE_CAP 32768
static uint8_t wire[SAVE_CAP];
/* App-owned single-thread storage worker: bounded scratch avoids task-stack spikes. */
static State scratch_state, best_state;
typedef struct {
    uint8_t* p;
    size_t pos, cap;
    int read, ok;
} Codec;
static uint64_t number(Codec* c, uint64_t v, int n) {
    if (c->pos + (size_t)n > c->cap) {
        c->ok = 0;
        return 0;
    }
    uint64_t out = 0;
    for (int i = 0; i < n; i++) {
        if (c->read) out |= (uint64_t)c->p[c->pos++] << (8 * i);
        else
            c->p[c->pos++] = (uint8_t)(v >> (8 * i));
    }
    return c->read ? out : v;
}
#define N(f, n) \
    do { (f) = number(c, (uint64_t)(f), n); } while (0)
static void event_codec(Codec* c, Event* e) {
    N(e->id, 4);
    N(e->time, 8);
    N(e->actor, 4);
    N(e->target, 4);
    N(e->op, 2);
    N(e->item, 2);
    N(e->amount, 4);
    N(e->status, 1);
    for (size_t i = 0; i < sizeof(e->text); i++) N(e->text[i], 1);
    e->text[sizeof(e->text) - 1] = 0;
}
static void state_codec(Codec* c, State* s) {
    N(s->seed, 4);
    N(s->version, 4);
    N(s->forced_variant, 1);
    N(s->sequence, 4);
    N(s->save_generation, 4);
    N(s->event_next, 4);
    N(s->time, 8);
    N(s->has_field_delta, 1);
    if (s->has_field_delta) {
        N(s->field_start, 8);
        N(s->field_level, 4);
        N(s->field_k, 4);
    } else if (c->read) {
        s->field_start = 8LL * 3600000;
        s->field_level = 750000;
        s->field_k = 0;
    }
    N(s->repaired, 1);
    N(s->funded, 1);
    N(s->evidence_taken, 1);
    N(s->evidence_shown, 1);
    N(s->promise_state, 1);
    N(s->promise_deadline, 8);
    /* Sparse changed inventories relative to the generated archetype. */
    Inventory defaults[3] = {0};
    defaults[1].quantity[IT_CHIT] = 34;
    defaults[1].quantity[IT_CONCENTRATOR] = 1;
    defaults[1].quantity[IT_TOOLKIT] = 1;
    defaults[1].quantity[IT_KEY] = 1;
    defaults[1].quantity[IT_NOTE] = 1;
    defaults[2].quantity[IT_COUPLING] = 1;
    defaults[2].quantity[IT_CHIT] = 1000;
    defaults[2].quantity[IT_CONCENTRATOR] = 1;
    Inventory* inv[] = {&s->player, &s->kyra, &s->market};
    Inventory* def[] = {&defaults[0], &defaults[1], &defaults[2]};
    for (int j = 0; j < 3; j++) {
        uint32_t mask = 0;
        if (!c->read)
            for (int i = 0; i < 20; i++)
                if (inv[j]->quantity[i] != def[j]->quantity[i]) mask |= 1U << i;
        N(mask, 4);
        if (mask >> 20) c->ok = 0;
        for (int i = 0; i < 20; i++) {
            if (mask & (1U << i)) N(inv[j]->quantity[i], 4);
            else if (c->read)
                inv[j]->quantity[i] = def[j]->quantity[i];
        }
    }
    N(s->npc.player, 4);
    N(s->npc.trust, 2);
    N(s->npc.count, 2);
    if (s->npc.count > MEMORY_CAP) {
        c->ok = 0;
        return;
    }
    for (int i = 0; i < s->npc.count; i++) event_codec(c, &s->npc.memories[i]);
    N(s->player_pos.x, 4);
    N(s->player_pos.y, 4);
    N(s->player_pos.z, 4);
    N(s->yaw, 4);
    N(s->vertical_speed, 4);
    N(s->grounded, 1);
    N(s->event_count, 2);
    if (s->event_count > EVENT_CAP) {
        c->ok = 0;
        return;
    }
    for (int i = 0; i < s->event_count; i++) event_codec(c, &s->events[i]);
}
size_t state_encode(const State* s, uint8_t* dst, size_t cap) {
    scratch_state = *s;
    Codec c = {dst, 0, cap, 0, 1};
    state_codec(&c, &scratch_state);
    return c.ok ? c.pos : 0;
}
int state_decode(State* s, const uint8_t* src, size_t n) {
    memset(&scratch_state, 0, sizeof(scratch_state));
    State* copy_ptr = &scratch_state;
#define copy (*copy_ptr)
    Codec c = {(uint8_t*)src, 0, n, 1, 1};
    state_codec(&c, &copy);
    if (!c.ok || c.pos != n || copy.version != GEN_VERSION || copy.time < 0 || copy.field_level < 0 || copy.field_level > 1000000 || copy.field_k < 0 || copy.field_k > 7500 || (copy.forced_variant > 2 && copy.forced_variant != 255) || copy.repaired > 1 || copy.yaw < 0 || copy.yaw >= 360 || copy.npc.trust < -100 || copy.npc.trust > 100) return 0;
    for (int i = 0; i < 20; i++)
        if (copy.player.quantity[i] < 0 || copy.kyra.quantity[i] < 0 || copy.market.quantity[i] < 0) return 0;
    *s = copy;
    return 1;
#undef copy
}
uint32_t crc32(const void* ptr, size_t n) {
    const uint8_t* p = ptr;
    uint32_t c = ~0U;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xedb88320U & -(c & 1));
    }
    return ~c;
}
static void put32(uint8_t* p, uint32_t x) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(x >> (8 * i));
}
static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
int save_game(Game* g, const char* base) {
    char path[512];
    if (strlen(base) > 480) return 0;
    uint32_t old = g->state.save_generation;
    g->state.save_generation++;
    size_t n = state_encode(&g->state, wire + 16, SAVE_CAP - 16);
    if (!n) {
        g->state.save_generation = old;
        return 0;
    }
    memcpy(wire, "CT01", 4);
    put32(wire + 4, (uint32_t)n);
    put32(wire + 8, crc32(wire + 16, n));
    put32(wire + 12, GEN_VERSION);
    snprintf(path, sizeof(path), "%s.%u", base, (unsigned)(g->state.save_generation & 1));
    FILE* f = fopen(path, "wb");
    if (!f) {
        g->state.save_generation = old;
        return 0;
    }
    int ok = fwrite(wire, 1, n + 16, f) == n + 16;
    if (fflush(f) != 0) ok = 0;
#ifndef ESP_PLATFORM
    if (fsync(fileno(f)) != 0) ok = 0;
#endif
    /* ESP-IDF FatFs VFS closes through f_close(), which synchronizes file data and metadata. */
    if (fclose(f) != 0) ok = 0;
    if (!ok) g->state.save_generation = old;
    return ok;
}
int load_game(Game* g, const char* base) {
    memset(&best_state, 0, sizeof(best_state));
    int found = 0;
    char path[512];
    if (strlen(base) > 480) return 0;
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s.%d", base, i);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        size_t n = fread(wire, 1, SAVE_CAP, f);
        int extra = fgetc(f);
        fclose(f);
        if (n < 16 || extra != EOF || memcmp(wire, "CT01", 4) || get32(wire + 12) != GEN_VERSION || get32(wire + 4) != n - 16 || get32(wire + 8) != crc32(wire + 16, n - 16)) continue;
        if (!state_decode(&scratch_state, wire + 16, n - 16)) continue;
        if (!found || scratch_state.save_generation > best_state.save_generation) {
            best_state = scratch_state;
            found = 1;
        }
    }
    if (!found) return 0;
    memset(g, 0, sizeof(*g));
    g->state = best_state;
    generate(&g->world, best_state.seed, best_state.forced_variant == 255 ? -1 : best_state.forced_variant);
    snprintf(g->notice, sizeof(g->notice), "Restored generation %u", (unsigned)best_state.save_generation);
    return 1;
}
