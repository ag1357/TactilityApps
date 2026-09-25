#ifndef CT_PLATFORM_LIFECYCLE_H
#define CT_PLATFORM_LIFECYCLE_H
#include <stdatomic.h>

/* Window callbacks publish ownership without taking application locks.
   The platform supplies its own wake/wait primitive after these transitions.
   epoch invalidates in-flight presentation and resets elapsed-time accounting. */
typedef struct {
    atomic_int granted, closing;
    atomic_uint epoch;
} CtLifecycle;

void ct_lifecycle_init(CtLifecycle*);
void ct_lifecycle_grant(CtLifecycle*);
void ct_lifecycle_revoke(CtLifecycle*);
void ct_lifecycle_close(CtLifecycle*);
int ct_lifecycle_active(const CtLifecycle*);
#endif
