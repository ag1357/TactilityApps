/* Host-callable reference versions of the PIE kernels, specifying intended
 * output, not proving that the assembly implements it (the .S itself targets P4 only). */
#ifndef PIE_KERNELS_H
#define PIE_KERNELS_H
#include <stdint.h>

static inline void fill_span_u16_ref(uint16_t *dst, uint32_t count, uint16_t color) {
    for(uint32_t i=0;i<count;i++)dst[i]=color;
}

static inline void expand2x_row_u16_ref(uint16_t *dst, const uint16_t *src, uint32_t w) {
    for (uint32_t x = 0; x < w; x++) {
        dst[2 * x] = src[x];
        dst[2 * x + 1] = src[x];
    }
}
#endif
