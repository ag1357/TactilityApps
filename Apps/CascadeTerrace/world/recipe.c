#include "sdk.h"
#include <string.h>
static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int32_t i32(const uint8_t* p) {
    uint32_t u = u32(p);
    return u <= INT32_MAX ? (int32_t)u : -1 - (int32_t)(~u);
}
void ws_materialize(const WsRecipe* r, uint16_t i, WsModule* out) {
    if (i >= r->count || i >= WS_CAP) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = r->modules[i];
    /* Correlated placement is an explicit recipe parameter; no entity names. */
    int32_t j = (int32_t)(ws_hash(r->seed ^ out->seed) % 5) - 2;
    out->pos.x += j * out->jitter_x;
    out->pos.z += j * out->jitter_z;
}
int ws_route(const WsRecipe* r, uint16_t a, uint16_t b, const uint8_t* blocked, uint16_t* path, size_t cap) {
    uint16_t queue[WS_CAP], prev[WS_CAP];
    size_t head = 0, tail = 0, n = 0;
    if (a >= r->count || b >= r->count || r->count > WS_CAP || r->link_count > WS_LINK_CAP) return 0;
    if (blocked && (blocked[a] || blocked[b])) return 0;
    for (int i = 0; i < WS_CAP; i++) prev[i] = UINT16_MAX;
    queue[tail++] = a;
    prev[a] = a;
    while (head < tail) {
        uint16_t at = queue[head++];
        if (at == b) break;
        for (int i = 0; i < r->link_count; i++) {
            const WsLink* l = &r->links[i];
            uint16_t next = l->a == at ? l->b : l->b == at ? l->a
                                                           : UINT16_MAX;
            if (next >= r->count || prev[next] != UINT16_MAX || (blocked && blocked[next])) continue;
            prev[next] = at;
            queue[tail++] = next;
        }
    }
    if (prev[b] == UINT16_MAX) return 0;
    for (uint16_t at = b;; at = prev[at]) {
        queue[n++] = at;
        if (at == a) break;
    }
    if (n > cap) return -(int)n;
    for (size_t i = 0; i < n; i++) path[i] = queue[n - 1 - i];
    return (int)n;
}
WsError ws_validate(const WsRecipe* r) {
    if (!r->count || r->count > WS_CAP || r->link_count > WS_LINK_CAP) return WS_BOUNDS;
    for (int i = 0; i < r->count; i++) {
        const WsModule* m = &r->modules[i];
        if (m->parent != UINT16_MAX && m->parent >= i) return WS_REFERENCE;
        if ((m->kind & 255) > WS_ROOM_MODULE || (m->kind >> 8) > WS_OBJECT || m->phos > 6 || (m->flags & ~127)) return WS_VERSION;
        if (m->size.x < 1 || m->size.y < 1 || m->size.z < 1 || m->size.x > 400000 || m->size.y > 400000 || m->size.z > 400000 || m->affinity > 1000) return WS_BOUNDS;
        if (m->pos.x < -1000000 || m->pos.x > 1000000 || m->pos.y < -1000000 || m->pos.y > 1000000 || m->pos.z < -1000000 || m->pos.z > 1000000) return WS_BOUNDS;
        for (int j = 0; j < i; j++)
            if (ws_id_equal(m->id, r->modules[j].id)) return WS_DUPLICATE;
    }
    for (int i = 0; i < r->link_count; i++) {
        const WsLink* l = &r->links[i];
        if (l->a >= r->count || l->b >= r->count || l->a == l->b) return WS_REFERENCE;
        if (l->kind > 2 || l->reserved) return WS_VERSION;
        if (!(r->modules[l->a].flags & WS_WALK) || !(r->modules[l->b].flags & WS_WALK)) return WS_REFERENCE;
    }
    int first = -1;
    uint16_t path[WS_CAP];
    for (int i = 0; i < r->count; i++)
        if (r->modules[i].flags & WS_WALK) {
            if (first < 0) first = i;
            else if (ws_route(r, (uint16_t)first, (uint16_t)i, 0, path, WS_CAP) <= 0)
                return WS_DISCONNECTED;
        }
    return WS_OK;
}
WsError ws_load(WsRecipe* r, const uint8_t* p, size_t n) {
    /* Transactional: validate bytes before touching output; caller stages final validation. */
    if (n < 48 || memcmp(p, "CWS1", 4)) return WS_FORMAT;
    if (u16(p + 4) != WS_SCHEMA || u16(p + 6) != WS_GENERATOR) return WS_VERSION;
    unsigned count = u16(p + 44), links = u16(p + 46);
    if (!count || count > WS_CAP || links > WS_LINK_CAP || n != 48 + 64 * count + 8 * links || u32(p + 8) != n) return WS_BOUNDS;
    if (ws_crc(p + 16, n - 16) != u32(p + 12)) return WS_FORMAT;
    memset(r, 0, sizeof(*r));
    for (int i = 0; i < 4; i++) r->ancestry.word[i] = u32(p + 16 + 4 * i);
    r->seed = u32(p + 32);
    r->epoch = u32(p + 36);
    r->revision = u32(p + 40);
    r->recipe_crc = u32(p + 12);
    r->count = (uint16_t)count;
    r->link_count = (uint16_t)links;
    p += 48;
    for (unsigned i = 0; i < count; i++, p += 64) {
        WsModule* m = &r->modules[i];
        for (int j = 0; j < 4; j++) m->id.word[j] = u32(p + j * 4);
        m->parent = u16(p + 16);
        m->kind = u16(p + 18);
        m->flags = u16(p + 20);
        m->phos = u16(p + 22);
        m->pos = (WsPos) {i32(p + 24), i32(p + 28), i32(p + 32)};
        m->size = (WsPos) {i32(p + 36), i32(p + 40), i32(p + 44)};
        m->color = u32(p + 48);
        m->seed = u32(p + 52);
        m->jitter_x = (int16_t)u16(p + 56);
        m->jitter_z = (int16_t)u16(p + 58);
        m->affinity = u16(p + 60);
        m->quantity = u16(p + 62);
    }
    for (unsigned i = 0; i < links; i++, p += 8) r->links[i] = (WsLink) {u16(p), u16(p + 2), u16(p + 4), u16(p + 6)};
    WsError e = ws_validate(r);
    if (e != WS_OK) memset(r, 0, sizeof(*r));
    return e;
}
WsFidelity ws_fidelity(WsPos a, WsPos b, int interior) {
    int64_t x = (int64_t)a.x - b.x, y = (int64_t)a.y - b.y, z = (int64_t)a.z - b.z;
    if (x > 180000 || x < -180000 || y > 180000 || y < -180000 || z > 180000 || z < -180000) return WS_UNLOADED;
    uint64_t d = (uint64_t)(x * x + y * y + z * z);
    if (d < 25000ULL * 25000) return WS_ACTIVE;
    if (interior) return WS_UNLOADED;
    if (d < 65000ULL * 65000) return WS_MATERIALIZED;
    return d < 180000ULL * 180000 ? WS_PROXY : WS_UNLOADED;
}
