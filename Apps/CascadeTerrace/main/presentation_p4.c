#include "presentation_p4.h"
#include "game.h"
#include <tactility/memory.h>
#include <stdint.h>
#ifndef CT_PRESENT_PIE
#define CT_PRESENT_PIE 0
#endif
#ifndef CT_PRESENT_PPA
#define CT_PRESENT_PPA 0
#endif
static int backend, opened;
#if CT_PRESENT_PIE
_Static_assert(W%8==0,"PIE row width must contain complete eight-pixel groups");
void ct_pie_expand2x_rows(uint16_t*,uint16_t*,const uint16_t*,uint32_t);
#endif
#if CT_PRESENT_PPA
#include <driver/ppa.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
static ppa_client_handle_t client;
static CtPresentPipe pipe;
static bool finished(ppa_client_handle_t c,ppa_event_data_t* e,void* data) {
    (void)c;(void)e;ct_pipe_complete(data);return false;
}
#endif
size_t ct_present_extra_bytes(void) {return backend==2?W*H*10:0;}
const char* ct_present_name(void) {return backend==2?"ppa-bilinear-experimental":backend==1?"pie-nearest-experimental":"portable-nearest";}
unsigned ct_present_busy(void) {
#if CT_PRESENT_PPA
    return backend==2 && pipe.busy;
#else
    return 0;
#endif
}
int ct_present_open(uint16_t* front,int requested) {
    if (opened) return backend; /* exactly-once per close cycle: a repeat call
                                   cannot re-register a PPA client or leak its
                                   back/snapshot allocations */
    backend=0;(void)front;(void)requested;
    opened=1;
#if CT_PRESENT_PIE
    if(requested==1 && ((uintptr_t)front&15)==0)backend=1;
#endif
#if CT_PRESENT_PPA
    if(requested==2 && ((uintptr_t)front&63)==0) {
        /* 64 covers the configured P4 L1/L2 cache line sizes. Both pointer
         * and lengths are aligned. Source and output are distinct allocations. */
        _Static_assert((W*H*8)%64==0 && (W*H*2)%64==0,"PPA buffer size alignment");
        struct MemoryPolicy policy={MEMORY_CAPABILITY_EXTERNAL|MEMORY_CAPABILITY_DMA,0,64};
        uint16_t* back=memory_alloc_with_policy(W*H*8,&policy);
        uint16_t* snapshot=memory_alloc_with_policy(W*H*2,&policy);
        ppa_client_config_t cfg={.oper_type=PPA_OPERATION_SRM,.max_pending_trans_num=1};
        if(back && snapshot && ppa_register_client(&cfg,&client)==ESP_OK) {
            ppa_event_callbacks_t callbacks={.on_trans_done=finished};
            if(ppa_client_register_event_callbacks(client,&callbacks)==ESP_OK) {
                ct_pipe_init(&pipe,front,back,snapshot,W*H*2);backend=2;return backend;
            }
            ppa_unregister_client(client);client=NULL;
        }
        memory_free(back);memory_free(snapshot);
    }
#endif
    return backend;
}
int ct_present_frame(uint16_t** front,const uint16_t* source) {
#if CT_PRESENT_PPA
    if(backend==2) {
        int ready=ct_pipe_poll(&pipe);
        if(ready)*front=pipe.front;
        if(!ct_pipe_begin(&pipe,source))return ready;
        ppa_srm_oper_config_t op={
            .in={.buffer=pipe.snapshot,.pic_w=W,.pic_h=H,.block_w=W,.block_h=H,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
            .out={.buffer=pipe.back,.buffer_size=W*H*8,.pic_w=W*2,.pic_h=H*2,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
            .scale_x=2,.scale_y=2,.rotation_angle=PPA_SRM_ROTATION_ANGLE_0,
            .mode=PPA_TRANS_MODE_NON_BLOCKING,.user_data=&pipe};
        if(ppa_do_scale_rotate_mirror(client,&op)!=ESP_OK) {
            ct_pipe_rejected(&pipe); /* API rejected: no transaction owns back. */
            ct_expand2x(*front,source,W,H);return 1;
        }
        return ready;
    }
#endif
#if CT_PRESENT_PIE
    if(backend==1 && (((uintptr_t)source|(uintptr_t)*front)&15)==0) {
        for(unsigned y=0;y<H;y++)ct_pie_expand2x_rows(*front+y*W*4,*front+(y*2+1)*W*2,source+y*W,W/8);
        return 1;
    }
#endif
    ct_expand2x(*front,source,W,H);return 1;
}
void ct_present_close(void) {
#if CT_PRESENT_PPA
    if(backend==2) {
        /* Module code and all DMA-owned buffers must outlive the ISR callback.
         * Do not cancel/free an accepted transfer or silently time it out. */
        while(pipe.busy && !atomic_load_explicit(&pipe.completed,memory_order_acquire))vTaskDelay(1);
        while(ppa_unregister_client(client)!=ESP_OK)vTaskDelay(1);
        client=NULL;
        memory_free(pipe.back);memory_free(pipe.snapshot);
    }
#endif
    backend=0;
    opened=0; /* a later ct_present_open may attempt a fresh registration */
}
