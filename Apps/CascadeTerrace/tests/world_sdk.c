#include "../world/sdk.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    WsId root={{1,2,3,4}}, a=ws_child_id(root,10,1), b=ws_child_id(root,11,1);
    assert(ws_id_equal(a,ws_child_id(root,10,1)));
    assert(!ws_id_equal(a,b));
    assert(ws_crc("123456789",9)==0xcbf43926U);
    printf("{\"stage\":\"skeleton\",\"passed\":3,\"recipe_workspace_bytes\":%zu}\n",sizeof(WsRecipe));
    return 0;
}
