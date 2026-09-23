#pragma once
/* Renderer-evaluation stage timers. aarch64: virtual counter (1-2 ns
 * overhead per sample, safe inside per-triangle hot paths). Fallback:
 * clock_gettime. Ticks are converted with bench_freq(). */
#include <stdint.h>
#include <time.h>

static inline uint64_t bench_now(void) {
#ifdef __aarch64__
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static inline uint64_t bench_freq(void) {
#ifdef __aarch64__
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
#else
    return 1000000000ull;
#endif
}

enum { TR_TRANSFORM = 0, TR_CLIP, TR_SETUP, TR_RASTER, TR_CLEAR, TR_N };

typedef struct {
    uint64_t ticks[TR_N];
    unsigned long long px_tested, px_covered, depth_writes;
} BenchStages;

extern BenchStages bench_st;

static inline void bench_add(int stage, uint64_t delta) { bench_st.ticks[stage] += delta; }
