#define _POSIX_C_SOURCE 200809L
#include "../core/platform_lifecycle.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

/* Exercise the production ownership primitive with a genuinely independent
   renderer thread. This is host scheduling evidence, not a physical P4 claim. */
typedef struct {
    CtLifecycle life;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t worker;
    unsigned started, completed, presented, discarded;
    int in_flight, exited;
} Harness;
static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition); return 1; } } while (0)
static void pause_ms(long ms) {
    struct timespec duration = {ms/1000, (ms%1000)*1000000};
    while (nanosleep(&duration,&duration) && errno == EINTR) { }
}
static void* render_worker(void* context) {
    Harness* h = context;
    pthread_mutex_lock(&h->mutex);
    for (;;) {
        while (!ct_lifecycle_active(&h->life) && !atomic_load(&h->life.closing))
            pthread_cond_wait(&h->changed,&h->mutex);
        if (atomic_load(&h->life.closing)) break;
        unsigned epoch = atomic_load(&h->life.epoch);
        h->in_flight = 1; h->started++;
        pthread_cond_broadcast(&h->changed);
        pthread_mutex_unlock(&h->mutex);
        pause_ms(150); /* Slow software renderer stand-in, without holding input/UI locks. */
        pthread_mutex_lock(&h->mutex);
        h->in_flight = 0; h->completed++;
        if (ct_lifecycle_active(&h->life) && epoch == atomic_load(&h->life.epoch)) h->presented++;
        else h->discarded++;
        pthread_cond_broadcast(&h->changed);
    }
    h->exited = 1;
    pthread_cond_broadcast(&h->changed);
    pthread_mutex_unlock(&h->mutex);
    return NULL;
}
/* Caller owns mutex. A deadline turns regressions into failures instead of hangs. */
static int await_counter(Harness* h, const unsigned* counter, unsigned target) {
    struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec += 3;
    while (*counter < target)
        if (pthread_cond_timedwait(&h->changed,&h->mutex,&deadline)) return 0;
    return 1;
}
static int run_lifecycle(int close_during_render) {
    Harness h = {0};
    ct_lifecycle_init(&h.life);
    CHECK(!ct_lifecycle_active(&h.life));
    CHECK(!pthread_mutex_init(&h.mutex,NULL));
    CHECK(!pthread_cond_init(&h.changed,NULL));
    CHECK(!pthread_create(&h.worker,NULL,render_worker,&h));
    pause_ms(25);
    pthread_mutex_lock(&h.mutex);
    CHECK(h.started == 0); /* Initial revoked state really blocks. */
    for (unsigned pass=1;pass<=3;pass++) {
        unsigned old_epoch=atomic_load(&h.life.epoch);
        ct_lifecycle_grant(&h.life);pthread_cond_broadcast(&h.changed);
        CHECK(ct_lifecycle_active(&h.life));
        CHECK(atomic_load(&h.life.epoch)>old_epoch);
        CHECK(await_counter(&h,&h.started,pass));
        CHECK(h.in_flight);
        ct_lifecycle_revoke(&h.life);pthread_cond_broadcast(&h.changed);
        CHECK(!ct_lifecycle_active(&h.life));
        CHECK(await_counter(&h,&h.completed,pass));
        CHECK(h.discarded == pass && h.presented == 0);
        unsigned completed=h.completed;
        pthread_mutex_unlock(&h.mutex);pause_ms(40);pthread_mutex_lock(&h.mutex);
        CHECK(h.completed == completed && h.started == completed && !h.in_flight);
    }
    if (close_during_render) {
        ct_lifecycle_grant(&h.life);pthread_cond_broadcast(&h.changed);
        CHECK(await_counter(&h,&h.started,4));
        CHECK(h.in_flight);
    }
    ct_lifecycle_close(&h.life);pthread_cond_broadcast(&h.changed);
    CHECK(!ct_lifecycle_active(&h.life));
    ct_lifecycle_grant(&h.life); /* Late create callback cannot resurrect app. */
    CHECK(!ct_lifecycle_active(&h.life) && !atomic_load(&h.life.granted));
    pthread_mutex_unlock(&h.mutex);
    CHECK(!pthread_join(h.worker,NULL));
    CHECK(h.exited && !h.in_flight);
    CHECK(h.completed == h.started && h.discarded == h.started);
    CHECK(!pthread_cond_destroy(&h.changed));
    CHECK(!pthread_mutex_destroy(&h.mutex));
    return 0;
}
int main(void) {
    if (run_lifecycle(0) || run_lifecycle(1)) return 1;
    printf("VI-P2 lifecycle: %u checks passed (150 ms render, revoke/resume, close/join)\n",checks);
    return 0;
}
