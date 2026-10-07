#ifndef ANAPHORUM_CONVERSATION_WIDGETS_H
#define ANAPHORUM_CONVERSATION_WIDGETS_H
#include "conversation_layout.h"
#include <lvgl.h>
/* Clear any prior bottom/center alignment before applying root-local offsets. */
static inline void ct_conversation_place(lv_obj_t *widget,CtUiRect rect,int root_x,int root_y) {
    lv_obj_set_size(widget,rect.w,rect.h);
    lv_obj_align(widget,LV_ALIGN_TOP_LEFT,rect.x-root_x,rect.y-root_y);
}
static inline void ct_conversation_blur(lv_obj_t *widget) {
    lv_group_remove_obj(widget);
    lv_obj_remove_state(widget,LV_STATE_FOCUSED|LV_STATE_FOCUS_KEY|LV_STATE_EDITED);
    lv_obj_send_event(widget,LV_EVENT_DEFOCUSED,NULL);
}
static inline void ct_conversation_focus(lv_obj_t *widget) {
    int focused=lv_obj_has_state(widget,LV_STATE_FOCUSED);
    lv_group_t *group=lv_group_get_default();
    if(group){lv_group_add_obj(group,widget);lv_group_focus_obj(widget);if(focused)lv_obj_send_event(widget,LV_EVENT_FOCUSED,NULL);}
    else {lv_obj_add_state(widget,LV_STATE_FOCUSED);lv_obj_send_event(widget,LV_EVENT_FOCUSED,NULL);}
}
/* App-owned character adapters already write the textarea directly. They are
 * not kernel keyboard devices, so do not focus and trigger the firmware's
 * software keyboard. A later tap after disconnect restores normal focus. */
static inline void ct_conversation_entry(lv_obj_t *widget,int direct_text_input) {
    if(direct_text_input)ct_conversation_blur(widget);
    else ct_conversation_focus(widget);
}
#endif
