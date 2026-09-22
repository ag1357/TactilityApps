#include "../world/sdk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../content/worlds/cascade.inc"
static WsRecipe r;
int main(void) {
    WsId root={{1,2,3,4}},ida=ws_child_id(root,10,1),idb=ws_child_id(root,11,1);
    assert(ws_id_equal(ida,ws_child_id(root,10,1))); assert(!ws_id_equal(ida,idb));
    assert(ws_crc("123456789",9)==0xcbf43926U);
    assert(ws_load(&r,ws_product,sizeof(ws_product))==WS_OK);
    assert(r.count==17); uint16_t path[WS_CAP];
    assert(ws_route(&r,4,14,0,path,WS_CAP)>0);
    uint8_t blocked[WS_CAP]={0}; blocked[5]=1;
    assert(ws_route(&r,4,14,blocked,path,WS_CAP)==0);
    uint8_t bad[sizeof(ws_product)]; memcpy(bad,ws_product,sizeof(bad)); bad[80]^=1;
    assert(ws_load(&r,bad,sizeof(bad))==WS_FORMAT);
    assert(ws_load(&r,ws_product,sizeof(ws_product))==WS_OK);
    WsModule lift; ws_materialize(&r,11,&lift);
    WsTraveler traveler={.at={{lift.pos.x,lift.pos.y+300,lift.pos.z},UINT16_MAX}};
    assert(ws_use_link(&r,&traveler)); assert(traveler.remaining>0);
    ws_travel_tick(&traveler,1000); assert(traveler.at.pos.y>lift.pos.y+300 && traveler.remaining>0);
    ws_travel_tick(&traveler,20000); assert(traveler.at.pos.y==27300 && !traveler.remaining);
    WsModule home;ws_materialize(&r,5,&home);
    traveler.at=(WsAddress){{home.pos.x,home.pos.y+300,home.pos.z},UINT16_MAX};
    assert(ws_use_link(&r,&traveler));assert(traveler.at.scope==14);
    assert(ws_use_link(&r,&traveler));assert(traveler.at.scope==UINT16_MAX);
    WsAddress a={{home.pos.x,home.pos.y+300,home.pos.z},UINT16_MAX};
    WsAddress b=a;b.pos.x+=3000;assert(ws_witness(&r,a,b,1000,1000)>0);
    b.pos.x+=10000;assert(ws_witness(&r,a,b,1000,1000)==0);
    b=a;b.pos.z+=home.size.z/2+2000;assert(ws_witness(&r,a,b,1000,1000)>0);
    int32_t floor;assert(ws_ground(&r,a,home.pos.y+1000,&floor));assert(floor==home.pos.y+300);
    a.pos.y=floor;assert(ws_move(&r,&a,0,100));
    a.pos.x=home.pos.x+home.size.x/2-400;assert(!ws_move(&r,&a,600,0));
    r.modules[1].id=r.modules[0].id; assert(ws_validate(&r)==WS_DUPLICATE);
    printf("{\"stage\":\"traversal\",\"passed\":23,\"recipe_workspace_bytes\":%zu,\"product_bytes\":%zu}\n",sizeof(WsRecipe),sizeof(ws_product));
    return 0;
}
