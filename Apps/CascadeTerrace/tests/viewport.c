#include "viewport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    int dims[][4]={{0,0,320,480},{0,24,720,696},{0,24,720,1256},{0,32,1280,688},{13,47,640,360},{0,0,241,319},{0,0,1,1},{0,0,0,480}};
    unsigned checks=0;
    uint16_t src[160*240];for(int i=0;i<160*240;i++)src[i]=(uint16_t)i;
    for(unsigned k=0;k<sizeof(dims)/sizeof(dims[0]);k++) {
        int *d=dims[k],rx,ry;CtViewport v=ct_viewport_fit(d[0],d[1],d[2],d[3],160,240);
        assert(v.x>=d[0]&&v.y>=d[1]&&v.x+v.w<=d[0]+d[2]&&v.y+v.h<=d[1]+d[3]);
        assert(!ct_viewport_inverse(&v,v.x-1,v.y,&rx,&ry));
        assert(!ct_viewport_inverse(&v,v.x+v.w,v.y,&rx,&ry));
        uint16_t *out=calloc((size_t)v.w*v.h+1,sizeof(*out));assert(out);
        ct_viewport_scale(out,src,&v);
        for(int y=0;y<v.h;y++)for(int x=0;x<v.w;x++) {
            assert(ct_viewport_inverse(&v,v.x+x,v.y+y,&rx,&ry));
            assert(out[y*v.w+x]==src[ry*160+rx]);checks++;
        }
        free(out);
    }
    printf("{\"stage\":\"viewport\",\"pixel_inverse_checks\":%u,\"sizes\":8}\n",checks);
}
