#include "platform_lifecycle.h"

void ct_lifecycle_init(CtLifecycle* life) {
    atomic_init(&life->granted, 0);
    atomic_init(&life->closing, 0);
    atomic_init(&life->epoch, 0);
}
void ct_lifecycle_grant(CtLifecycle* life) {
    if (atomic_load(&life->closing)) return;
    atomic_fetch_add(&life->epoch, 1);
    atomic_store(&life->granted, 1);
    /* Close dominates even if it raced this callback between the first test
       and publication. A closed lifecycle can never become active again. */
    if (atomic_load(&life->closing)) atomic_store(&life->granted, 0);
}
void ct_lifecycle_revoke(CtLifecycle* life) {
    atomic_store(&life->granted, 0);
    atomic_fetch_add(&life->epoch, 1);
}
void ct_lifecycle_close(CtLifecycle* life) {
    atomic_store(&life->closing, 1);
    atomic_store(&life->granted, 0);
    atomic_fetch_add(&life->epoch, 1);
}
int ct_lifecycle_active(const CtLifecycle* life) {
    return !atomic_load(&life->closing) && atomic_load(&life->granted);
}
