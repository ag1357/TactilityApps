#ifndef ANAPHORUM_CONVERSATION_LAYOUT_H
#define ANAPHORUM_CONVERSATION_LAYOUT_H
typedef struct { int x,y,w,h; } CtUiRect;
typedef struct { CtUiRect input,reply; } CtConversationLayout;
/* Screen-space rectangles; the native frontend converts to root-local positions.
 * The firmware's software keyboard occupies the bottom half of the display. */
CtConversationLayout ct_conversation_layout(CtUiRect content,int display_h,int software_entry);
#endif
