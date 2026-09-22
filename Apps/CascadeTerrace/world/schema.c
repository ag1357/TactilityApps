#include "sdk.h"
uint32_t ws_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
uint32_t ws_crc(const void* data, size_t n) {
    const uint8_t* p = data;
    uint32_t c = ~0U;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1)));
    }
    return ~c;
}
WsId ws_child_id(WsId p, uint32_t key, uint32_t version) {
    WsId r;
    for (int i = 0; i < 4; i++) r.word[i] = ws_hash(p.word[i] ^ ws_hash(key + 0x9e3779b9U * (uint32_t)(i + 1)) ^ version);
    return r;
}
int ws_id_equal(WsId a, WsId b) { return a.word[0] == b.word[0] && a.word[1] == b.word[1] && a.word[2] == b.word[2] && a.word[3] == b.word[3]; }
const char* ws_error(WsError e) {
    static const char* names[] = {"OK", "FORMAT", "VERSION", "BOUNDS", "DUPLICATE", "REFERENCE", "DISCONNECTED", "DENIED", "STALE", "FULL"};
    return (unsigned)e < sizeof(names) / sizeof(*names) ? names[e] : "INVALID_ERROR";
}
