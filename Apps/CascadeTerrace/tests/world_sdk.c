#include "../world/sdk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../content/worlds/cascade.inc"
static WsRecipe r;
int main(void) {
    WsId root={{1,2,3,4}},a=ws_child_id(root,10,1),b=ws_child_id(root,11,1);
    assert(ws_id_equal(a,ws_child_id(root,10,1))); assert(!ws_id_equal(a,b));
    assert(ws_crc("123456789",9)==0xcbf43926U);
    assert(ws_load(&r,ws_product,sizeof(ws_product))==WS_OK);
    assert(r.count==17); uint16_t path[WS_CAP];
    assert(ws_route(&r,4,14,0,path,WS_CAP)>0);
    uint8_t blocked[WS_CAP]={0}; blocked[5]=1;
    assert(ws_route(&r,4,14,blocked,path,WS_CAP)==0);
    uint8_t bad[sizeof(ws_product)]; memcpy(bad,ws_product,sizeof(bad)); bad[80]^=1;
    assert(ws_load(&r,bad,sizeof(bad))==WS_FORMAT);
    assert(ws_load(&r,ws_product,sizeof(ws_product))==WS_OK);
    r.modules[1].id=r.modules[0].id; assert(ws_validate(&r)==WS_DUPLICATE);
    printf("{\"stage\":\"compiler\",\"passed\":9,\"recipe_workspace_bytes\":%zu,\"product_bytes\":%zu}\n",sizeof(WsRecipe),sizeof(ws_product));
    return 0;
}
