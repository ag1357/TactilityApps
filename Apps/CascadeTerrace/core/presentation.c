#include "presentation.h"
#include <string.h>
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"ISR completion requires lock-free unsigned atomics");
void ct_expand2x(uint16_t* dst,const uint16_t* src,unsigned w,unsigned h) {
    for(unsigned y=0;y<h;y++) {
        uint16_t *a=dst+(size_t)y*4*w,*b=a+2*w;
        for(unsigned x=0;x<w;x++) {
            uint32_t v=src[(size_t)y*w+x];v|=v<<16;
            /* memcpy permits two-byte-aligned buffers without aliasing UB. */
            memcpy(a+2*x,&v,sizeof(v));memcpy(b+2*x,&v,sizeof(v));
        }
    }
}
void ct_pipe_init(CtPresentPipe* p,uint16_t* front,uint16_t* back,uint16_t* snapshot,size_t bytes) {
    p->front=front;p->back=back;p->snapshot=snapshot;p->source_bytes=bytes;p->busy=0;atomic_init(&p->completed,0);
}
int ct_pipe_begin(CtPresentPipe* p,const uint16_t* src) {
    if(p->busy)return 0;
    memcpy(p->snapshot,src,p->source_bytes);atomic_store_explicit(&p->completed,0,memory_order_relaxed);p->busy=1;return 1;
}
void ct_pipe_complete(CtPresentPipe* p) { atomic_store_explicit(&p->completed,1,memory_order_release); }
int ct_pipe_poll(CtPresentPipe* p) {
    if(!p->busy || !atomic_load_explicit(&p->completed,memory_order_acquire))return 0;
    uint16_t* old=p->front;p->front=p->back;p->back=old;p->busy=0;return 1;
}
void ct_pipe_rejected(CtPresentPipe* p) { p->busy=0;atomic_store_explicit(&p->completed,0,memory_order_relaxed); }
