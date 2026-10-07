/* Real LVGL, production app helpers. Firmware keyboard hooks are simulated,
 * not a claim that this host test executes Tactility or physical input. */
#include "conversation_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 3
#error This regression test must use firmware-matched LVGL 9.3.
#endif
static int hardware,software_visible;
static void keyboard_hook(lv_event_t *e) {
    if(lv_event_get_code(e)==LV_EVENT_FOCUSED)software_visible=!hardware;
    if(lv_event_get_code(e)==LV_EVENT_DEFOCUSED||lv_event_get_code(e)==LV_EVENT_READY)software_visible=0;
}
static void rect_matches(lv_obj_t *obj,CtUiRect expected) {
    lv_area_t actual;lv_obj_get_coords(obj,&actual);
    assert(actual.x1==expected.x&&actual.y1==expected.y);
    assert(lv_area_get_width(&actual)==expected.w&&lv_area_get_height(&actual)==expected.h);
}
int main(void) {
    lv_init();
    lv_display_t *display=lv_display_create(320,480);assert(display);
    lv_group_t *group=lv_group_create();lv_group_set_default(group);
    lv_obj_t *root=lv_obj_create(lv_screen_active());
    lv_obj_set_style_pad_all(root,0,0);lv_obj_set_style_border_width(root,0,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(root,6,18);lv_obj_set_size(root,308,462);
    lv_obj_t *button=lv_button_create(root);
    lv_obj_t *reply=lv_label_create(root),*input=lv_textarea_create(root);
    lv_textarea_set_one_line(input,true);
    lv_obj_align(input,LV_ALIGN_BOTTOM_MID,0,-85);
    lv_obj_add_event_cb(input,keyboard_hook,LV_EVENT_ALL,NULL);
    /* Match the firmware's automatic textarea registration. */
    lv_group_add_obj(group,input);
    ct_conversation_blur(input);lv_obj_add_flag(input,LV_OBJ_FLAG_HIDDEN);
    for(int i=0;i<20;i++){lv_group_focus_next(group);assert(lv_group_get_focused(group)!=input);}
    assert(!software_visible&&!lv_obj_get_group(input));
    /* Entering conversation does not focus the text field. */
    lv_obj_remove_flag(input,LV_OBJ_FLAG_HIDDEN);
    assert(!software_visible&&!lv_obj_has_state(input,LV_STATE_FOCUSED));
    CtUiRect content={6,18,308,462};
    for(int software=0;software<2;software++) {
        CtConversationLayout c=ct_conversation_layout(content,480,software);
        ct_conversation_place(input,c.input,content.x,content.y);
        ct_conversation_place(reply,c.reply,content.x,content.y);
        lv_obj_update_layout(root);
        rect_matches(input,c.input);rect_matches(reply,c.reply);
    }
    ct_conversation_focus(input);assert(software_visible&&lv_obj_has_state(input,LV_STATE_FOCUSED));
    ct_conversation_blur(input);assert(!software_visible&&!lv_obj_has_state(input,LV_STATE_FOCUSED));
    ct_conversation_focus(input);assert(software_visible);
    /* Wired app-owned text input isn't in the firmware keyboard ledger.
     * It must blur even if pointer focus or a hotplug follows software entry. */
    ct_conversation_entry(input,1);
    assert(!software_visible&&!lv_obj_get_group(input)&&!lv_obj_has_state(input,LV_STATE_FOCUSED));
    ct_conversation_entry(input,1);assert(!software_visible);
    lv_textarea_set_text(input,"wired text");assert(!strcmp(lv_textarea_get_text(input),"wired text"));
    ct_conversation_entry(input,0);assert(software_visible&&lv_obj_has_state(input,LV_STATE_FOCUSED));
    hardware=1;ct_conversation_blur(input);ct_conversation_focus(input);assert(!software_visible);
    /* Hardware unplug while focused: the next intentional tap re-emits focus. */
    hardware=0;ct_conversation_focus(input);assert(software_visible);
    /* Software submit hides keyboard; retapping the same focused field works. */
    lv_obj_send_event(input,LV_EVENT_READY,NULL);assert(!software_visible);
    ct_conversation_focus(input);assert(software_visible);
    ct_conversation_blur(input);lv_obj_add_flag(input,LV_OBJ_FLAG_HIDDEN);
    lv_group_focus_obj(button);assert(!software_visible&&!lv_obj_get_group(input));
    assert(!lv_obj_has_state(input,LV_STATE_FOCUSED|LV_STATE_FOCUS_KEY|LV_STATE_EDITED));
    lv_obj_delete(root);lv_group_set_default(NULL);lv_group_delete(group);lv_display_delete(display);
    lv_deinit();
    puts("PASS real LVGL 9.3: production placement, ungrouped menus, intentional focus, wired entry/disconnect, submit/retap, unplug/retap, leave blur");
}
