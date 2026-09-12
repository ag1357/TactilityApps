// SPDX-License-Identifier: Apache-2.0
// App entry: event loop + lifecycle. The widget tree is created by the window
// manager via createWidgets/destroyWidgets (AudiobookPlayerUI.cpp).

#include "AudiobookPlayer.h"

#include <app/event.h>
#include <app/scheduler.h>

#include <lvgl/lvgl.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/log.h>

extern "C" {

int main(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

    Context self {};
    self.appInstanceId = app_scheduler_current_app_id();

    TaskEventGroup eventGroup {};
    task_event_group_construct(&eventGroup);
    self.eventGroup = &eventGroup;

    initAppData(&self);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &eventGroup) == ERROR_NONE);

    WindowId window = window_manager_create_ext(self.appInstanceId, createWidgets, destroyWidgets, &self);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&eventGroup, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
            }
            if (shouldClose) break;
        }
    }

    // Flush the resume position, tear down the playback task and the sidecar
    // worker (FIFO queue: pending saves drain before the Shutdown job).
    maybeSaveSidecarNow(&self);
    if (lvgl_try_lock(1000 / portTICK_PERIOD_MS)) {
        if (self.pollTimer != nullptr) {
            lv_timer_del(self.pollTimer);
            self.pollTimer = nullptr;
        }
        lvgl_unlock();
    }
    self.playback.stopRequested.store(true);
    self.playback.shutdown.store(true);
    for (int i = 0; i < 80 && self.playback.task != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    self.sidecarWorker.stop();

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&eventGroup);

    return 0;
}

}
