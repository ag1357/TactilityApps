#include "conversation_layout.h"
CtConversationLayout ct_conversation_layout(CtUiRect content,int display_h,int software_entry) {
    CtConversationLayout out={0};
    if(content.w<=8||content.h<=52||display_h<=0)return out;
    int top=content.y+44,bottom=content.y+content.h-85;
    if(software_entry&&bottom>display_h/2-4)bottom=display_h/2-4;
    if(bottom-top<10)return out;
    int input_h=bottom-top<84?(bottom-top-4)/2:40;
    int reply_h=bottom-top-input_h-4;
    if(reply_h>140)reply_h=140;
    out.input=(CtUiRect){content.x+4,bottom-input_h,content.w-8,input_h};
    out.reply=(CtUiRect){content.x+4,out.input.y-4-reply_h,content.w-8,reply_h};
    return out;
}
