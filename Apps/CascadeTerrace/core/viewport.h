#ifndef ANAPHORUM_VIEWPORT_H
#define ANAPHORUM_VIEWPORT_H
#include <stdint.h>
typedef struct { int x,y,w,h,render_w,render_h; } CtViewport;
/* All coordinates share the content-root coordinate space, including safe-area origin. */
CtViewport ct_viewport_fit(int x,int y,int w,int h,int render_w,int render_h);
int ct_viewport_inverse(const CtViewport*,int screen_x,int screen_y,int *render_x,int *render_y);
void ct_viewport_scale_stride(uint16_t *dst,const uint16_t *src,const CtViewport*,unsigned stride_pixels);
void ct_viewport_scale(uint16_t *dst,const uint16_t *src,const CtViewport*);
#endif
