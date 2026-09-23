/* Host-callable reference versions of the PIE kernels, used to prove the
 * semantics the assembly implements (the .S itself targets P4 only). */
#ifndef PIE_KERNELS_H
#define PIE_KERNELS_H
#include <stdint.h>

static inline void fill_span_u16_ref(uint16_t *dst, uint32_t count, uint16_t color) {
    /* paired-u32 fill, the fastest portable form (matches Jet's span fill) */
    uint32_t pair = ((uint32_t)color << 16) | color;
    uint32_t *d32 = (uint32_t *)dst;
    uint32_t n = count >> 1;
    while (n--) *d32++ = pair;
    if (count & 1) dst[count - 1] = color;
}

static inline void expand2x_row_u16_ref(uint16_t *dst, const uint16_t *src, uint32_t w) {
    for (uint32_t x = 0; x < w; x++) {
        dst[2 * x] = src[x];
        dst[2 * x + 1] = src[x];
    }
}
#endif
