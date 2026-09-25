#include "viewport.h"
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
void ct_viewport_scale(uint16_t* dst,const uint16_t* src,const CtViewport* v) {
    for(int y=0;y<v->h;y++)for(int x=0;x<v->w;x++)
        dst[(size_t)y*v->w+x]=src[(size_t)((int64_t)y*v->render_h/v->h)*v->render_w+(int64_t)x*v->render_w/v->w];
}
