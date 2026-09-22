#include "sdk.h"
#include <stdlib.h>
int ws_ground(const WsRecipe *r,WsAddress at,int32_t ceiling,int32_t *height) {
    int found=ws_surface(r,at,ceiling,height); int32_t best=found?*height:INT32_MIN;
    if(at.scope==UINT16_MAX) for(uint16_t i=0;i<r->link_count;i++) {
        WsLink l=r->links[i]; if(l.kind!=0)continue;
        WsModule a,b;ws_materialize(r,l.a,&a);ws_materialize(r,l.b,&b);
        int64_t dx=(int64_t)b.pos.x-a.pos.x,dz=(int64_t)b.pos.z-a.pos.z;
        int64_t den=dx*dx+dz*dz,px=(int64_t)at.pos.x-a.pos.x,pz=(int64_t)at.pos.z-a.pos.z;
        if(!den)continue;
        int64_t t=px*dx+pz*dz;if(t<0||t>den)continue;
        int64_t ex=px-dx*t/den,ez=pz-dz*t/den;
        if(ex*ex+ez*ez>2000LL*2000)continue;
        int32_t y=a.pos.y+(int32_t)(((int64_t)b.pos.y-a.pos.y)*t/den)+300;
        if(y<=ceiling&&y>best){found=1;best=y;}
    }
    if(found)*height=best;
    return found;
}
int ws_move(const WsRecipe *r,WsAddress *at,int32_t dx,int32_t dz) {
    /* Short swept steps prevent tunnelling. No teleport fallback on failed motion. */
    if(llabs(dx)>1000||llabs(dz)>1000)return 0;
    int steps=(abs(dx)+abs(dz))/100+1;WsAddress old=*at;
    for(int i=1;i<=steps;i++) {
        WsAddress next=*at; next.pos.x=old.pos.x+dx*i/steps;next.pos.z=old.pos.z+dz*i/steps;
        int32_t y;if(!ws_ground(r,next,at->pos.y+350,&y)||y<at->pos.y-500)return 0;
        next.pos.y=y;if(ws_collision(r,next,300))return 0;*at=next;
    }
    return 1;
}
int ws_use_link(const WsRecipe *r,WsTraveler *t) {
    if(t->remaining)return 0;
    for(uint16_t i=0;i<r->link_count;i++) {
        WsLink l=r->links[i];if(!l.kind)continue;
        for(int side=0;side<2;side++) {
            uint16_t from=side?l.b:l.a,to=side?l.a:l.b;WsModule a,b;
            ws_materialize(r,from,&a);ws_materialize(r,to,&b);
            uint16_t scope=(a.flags&WS_INTERIOR)?from:UINT16_MAX;
            if(scope!=t->at.scope)continue;
            if(llabs((int64_t)t->at.pos.x-a.pos.x)>2500||llabs((int64_t)t->at.pos.z-a.pos.z)>2500||llabs((int64_t)t->at.pos.y-a.pos.y)>1000)continue;
            t->destination=(WsAddress){{b.pos.x,b.pos.y+300,b.pos.z},(b.flags&WS_INTERIOR)?to:UINT16_MAX};
            if(l.kind==2){t->at=t->destination;return 1;}
            t->remaining=(uint32_t)(llabs((int64_t)b.pos.y-a.pos.y)/2+1);return 1;
        }
    }
    return 0;
}
void ws_travel_tick(WsTraveler *t,uint32_t ms) {
    if(!t->remaining)return;
    if(ms>=t->remaining){t->at=t->destination;t->remaining=0;return;}
    t->at.pos.x+=(int32_t)(((int64_t)t->destination.pos.x-t->at.pos.x)*ms/t->remaining);
    t->at.pos.y+=(int32_t)(((int64_t)t->destination.pos.y-t->at.pos.y)*ms/t->remaining);
    t->at.pos.z+=(int32_t)(((int64_t)t->destination.pos.z-t->at.pos.z)*ms/t->remaining);
    t->remaining-=ms;
}
