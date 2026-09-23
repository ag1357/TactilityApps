#ifndef CT_PRESENTATION_H
#define CT_PRESENTATION_H
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
/* RGB565 nearest-neighbor; disjoint source/destination buffers. */
void ct_expand2x(uint16_t* dst,const uint16_t* src,unsigned w,unsigned h);
/* Single in-flight async transaction. Caller supplies disjoint buffers:
 * front/back each w*h*8 bytes; snapshot w*h*2 bytes. Display ownership swaps
 * only in poll(), called under the frontend display lock. No allocations. */
typedef struct {
    uint16_t *front,*back,*snapshot;
    size_t source_bytes;
    unsigned busy;
    atomic_uint completed;
} CtPresentPipe;
void ct_pipe_init(CtPresentPipe*,uint16_t*,uint16_t*,uint16_t*,size_t);
int ct_pipe_begin(CtPresentPipe*,const uint16_t*);
void ct_pipe_complete(CtPresentPipe*); /* ISR-safe: one lock-free release store. */
int ct_pipe_poll(CtPresentPipe*);
void ct_pipe_rejected(CtPresentPipe*); /* Only after driver confirms not queued. */
#endif
