#include "viewport.h"
#include <string.h>
#include <stddef.h>
CtViewport ct_viewport_fit(int x,int y,int w,int h,int rw,int rh) {
    CtViewport v={x,y,0,0,rw,rh};
    if(w<=0||h<=0||rw<=0||rh<=0)return v;
    if((int64_t)w*rh<=(int64_t)h*rw){v.w=w;v.h=(int)((int64_t)w*rh/rw);}
    else {v.h=h;v.w=(int)((int64_t)h*rw/rh);}
    v.x=x+(w-v.w)/2;v.y=y+(h-v.h)/2;return v;
}
int ct_viewport_inverse(const CtViewport* v,int x,int y,int* rx,int* ry) {
    if(!v||v->w<=0||v->h<=0||x<v->x||y<v->y||
       (int64_t)x>= (int64_t)v->x+v->w||(int64_t)y>= (int64_t)v->y+v->h)return 0;
    *rx=(int)((int64_t)(x-v->x)*v->render_w/v->w);
    *ry=(int)((int64_t)(y-v->y)*v->render_h/v->h);return 1;
}
void ct_viewport_scale_stride(uint16_t* dst,const uint16_t* src,const CtViewport* v,unsigned stride) {
    /* Exact incremental floor mapping: (sx, row base) equal the direct
     * x*render_w/w and y*render_h/h integer divisions for all nonnegative
     * operands (rem = x*rw mod w), without a per-pixel division. The column
     * mapping is row-invariant; each row reads one source line. Paired
     * stores use memcpy so two-byte-aligned buffers stay alias-safe; the
     * packed pair layout targets the supported little-endian builds. */
    unsigned rw=(unsigned)v->render_w,w=(unsigned)v->w;
    int pairable = w>=2 && (stride&1u)==0u && ((uintptr_t)dst&3u)==0u;
    for(int y=0;y<v->h;y++) {
        const uint16_t* row=src+(size_t)((int64_t)y*v->render_h/v->h)*rw;
        uint16_t* out=dst+(size_t)y*stride;
        unsigned sx=0,rem=0;
        if(pairable) {
            int x=0;
            for(;x+1<v->w;x+=2) {
                uint32_t p=row[sx];
                rem+=rw;while(rem>=w){sx++;rem-=w;}
                p|=(uint32_t)row[sx]<<16;
                rem+=rw;while(rem>=w){sx++;rem-=w;}
                memcpy(out+x,&p,sizeof p);
            }
            if(x<v->w)out[x]=row[sx];
        } else {
            for(int x=0;x<v->w;x++) {
                out[x]=row[sx];
                rem+=rw;while(rem>=w){sx++;rem-=w;}
            }
        }
    }
}

void ct_viewport_scale(uint16_t* dst,const uint16_t* src,const CtViewport* v) {
    ct_viewport_scale_stride(dst,src,v,(unsigned)v->w);
}
