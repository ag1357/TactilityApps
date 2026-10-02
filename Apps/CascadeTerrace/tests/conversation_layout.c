#include "conversation_layout.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    unsigned checks=0;
    for(int display=100;display<=1280;display+=20)
    for(int y=0;y<display;y+=17)
    for(int height=0;height<=display-y;height+=23)
    for(int software=0;software<2;software++) {
        CtUiRect root={13,y,308,height};
        CtConversationLayout c=ct_conversation_layout(root,display,software);
        if(c.input.h) {
            assert(c.input.x>=root.x&&c.input.x+c.input.w<=root.x+root.w);
            assert(c.reply.x>=root.x&&c.reply.x+c.reply.w<=root.x+root.w);
            assert(c.reply.y>=root.y+44);
            assert(c.reply.y+c.reply.h+4==c.input.y);
            assert(c.input.y+c.input.h<=root.y+root.h);
            assert(c.reply.h>0&&c.reply.h<=140);
            if(software)assert(c.input.y+c.input.h<=display/2-4);
        } else assert(!c.reply.h);
        checks++;
    }
    CtConversationLayout c=ct_conversation_layout((CtUiRect){6,18,308,462},480,1);
    assert(c.input.y==196&&c.input.h==40);
    assert(c.reply.y==62&&c.reply.h==130);
    c=ct_conversation_layout((CtUiRect){6,18,308,462},480,0);
    assert(c.input.y==355&&c.reply.y==211&&c.reply.h==140);
    assert(!ct_conversation_layout((CtUiRect){0,0,8,480},480,1).input.h);
    printf("{\"stage\":\"conversation_layout\",\"checks\":%u,\"physical\":\"PENDING\"}\n",checks+5);
}
