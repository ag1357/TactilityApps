// SPDX-License-Identifier: Apache-2.0
// LVGL UI: widget tree, transport/editor controls, poll timer.

#include "AudiobookPlayer.h"
#include "AudioServiceApi.h"

#include <app/event.h>

#include <lvgl/widgets/sliderbox.h>
#include <lvgl/widgets/toolbar.h>
#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

// Progress is chapter-relative for audiobooks (elapsed within the current
// chapter vs chapter length) and track-relative for music.
static void updateProgressUi(Context* self) {
    if (self->progressLabel == nullptr) return;
    int64_t pos = self->playback.currentPositionMs.load();
    if (pos / 1000 == self->lastRenderedPositionSec) return;
    self->lastRenderedPositionSec = pos / 1000;

    int64_t shownPos = pos;
    int64_t shownTotal = 0;
    {
        std::lock_guard lg(self->sidecarMutex);
        // Frozen at sidecar load: a live fallback made the total move during playback.
        shownTotal = self->currentSidecar.duration_ms > 0 ? self->currentSidecar.duration_ms : 0;
        if (self->currentSidecar.kind == "audiobook" && !self->currentSidecar.chapters.empty()) {
            const auto& chapters = self->currentSidecar.chapters;
            int64_t chStart = 0;
            int64_t chEnd = shownTotal;
            for (size_t i = 0; i < chapters.size(); ++i) {
                if (chapters[i].start_ms <= pos) {
                    chStart = chapters[i].start_ms;
                    chEnd = (i + 1 < chapters.size()) ? chapters[i + 1].start_ms : shownTotal;
                } else {
                    break;
                }
            }
            shownPos = pos - chStart;
            if (shownPos < 0) shownPos = 0;
            shownTotal = chEnd - chStart;
        }
    }

    std::string text = formatDurationMs(shownPos);
    if (shownTotal > 0) text += " / " + formatDurationMs(shownTotal);
    lv_label_set_text(self->progressLabel, text.c_str());
    if (self->progressBar != nullptr && shownTotal > 0 && shownPos <= shownTotal) {
        lv_bar_set_value(self->progressBar, static_cast<int32_t>((shownPos * 1000) / shownTotal), LV_ANIM_OFF);
    } else if (self->progressBar != nullptr) {
        lv_bar_set_value(self->progressBar, 0, LV_ANIM_OFF);
    }
}

// lv_timer (UI thread): reflects playback state into the labels.
static void pollTimerCb(lv_timer_t* timer) {
    auto* self = static_cast<Context*>(lv_timer_get_user_data(timer));
    // Headset/media Play/Pause button: the service latch is set from the HID
    // pump task, so act on it here (LVGL thread) where the toggle is safe.
    if (tactility_audio_consume_play_pause_request()) {
        handlePlayPauseToggle(self);
    }
    // The playback thread parked after an output error (e.g. codec rebind on
    // headset attach): persist the interruption point and tell the user.
    if (self->playback.pendingErrorSave.exchange(false)) {
        maybeSaveSidecarNow(self);
        if (self->statusLabel != nullptr) {
            lv_label_set_text(self->statusLabel, "Output changed - paused");
        }
    }
    refreshFromPlaybackState(self);
    updateProgressUi(self);
    // Track external volume changes (e.g. headset buttons) on the slider.
    // lvgl_sliderbox_set_value does not fire the value-changed callback, so
    // this cannot feed back into setOutputVolume.
    if (self->volumeSliderBox != nullptr && lv_tick_elaps(self->lastVolumeUserMs) > 1500) {
        auto globalVolume = static_cast<int32_t>(tactility_audio_get_output_volume());
        if (globalVolume != lvgl_sliderbox_get_value(self->volumeSliderBox)) {
            lvgl_sliderbox_set_value(self->volumeSliderBox, globalVolume, LV_ANIM_OFF);
        }
    }
    // Tick every 500 ms; checkpoint the sidecar every ~180 s of ticks.
    self->saveCounterTicks++;
    if (self->saveCounterTicks >= 360) {
        self->saveCounterTicks = 0;
        maybeSaveSidecar(self);
    }
}

// ---- Button callbacks -------------------------------------------------------

static void onBack30Cb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    int64_t pos = self->playback.currentPositionMs.load();
    requestSeekMs(self, pos - 30000);
}

static void onFwd30Cb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    int64_t pos = self->playback.currentPositionMs.load();
    requestSeekMs(self, pos + 30000);
}

static void onMarkChapterCb(lv_event_t* e) {
    markChapter(static_cast<Context*>(lv_event_get_user_data(e)));
}

static void onNudgeBack5Cb(lv_event_t* e) {
    nudgeChapter(static_cast<Context*>(lv_event_get_user_data(e)), -5000);
}
static void onNudgeBack1Cb(lv_event_t* e) {
    nudgeChapter(static_cast<Context*>(lv_event_get_user_data(e)), -1000);
}
static void onNudgeFwd1Cb(lv_event_t* e) {
    nudgeChapter(static_cast<Context*>(lv_event_get_user_data(e)), 1000);
}
static void onNudgeFwd5Cb(lv_event_t* e) {
    nudgeChapter(static_cast<Context*>(lv_event_get_user_data(e)), 5000);
}

static void onEditModeSwitchCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    auto* sw = lv_event_get_target_obj(e);
    bool editMode = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (self->editorRow != nullptr) {
        if (editMode) {
            lv_obj_remove_flag(self->editorRow, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(self->editorRow, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void onPickFileCb(lv_event_t* e) {
    fileBrowserShow(static_cast<Context*>(lv_event_get_user_data(e)));
}

static void onRescanCb(lv_event_t* e) {
    requestLibraryRescan(static_cast<Context*>(lv_event_get_user_data(e)));
}

static void onPlayPauseCb(lv_event_t* e) {
    handlePlayPauseToggle(static_cast<Context*>(lv_event_get_user_data(e)));
}

// Edge buttons are mode-aware: chapter jumps for audiobooks, track
// prev/next for music (audiobook mode intentionally has no next-book).
static void onEdgeLeftCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    bool audiobookMode;
    {
        std::lock_guard lg(self->sidecarMutex);
        audiobookMode = (self->currentSidecar.kind == "audiobook");
    }
    if (audiobookMode) chapterPrev(self);
    else trackPrev(self);
}

static void onEdgeRightCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    bool audiobookMode;
    {
        std::lock_guard lg(self->sidecarMutex);
        audiobookMode = (self->currentSidecar.kind == "audiobook");
    }
    if (audiobookMode) chapterNext(self);
    else trackNext(self);
}

static void onAutoplaySwitchCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    auto* sw = lv_event_get_target_obj(e);
    self->playback.autoplay.store(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

// Enable/disable resume tracking for the current track. Enabling writes the
// sidecar immediately; disabling keeps the file (chapters survive).
static void onTrackingSwitchCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    auto* sw = lv_event_get_target_obj(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    std::string sidecarPath;
    SidecarData snapshot;
    {
        std::lock_guard lg(self->sidecarMutex);
        if (self->currentSidecarPath.empty()) return;
        self->currentSidecar.tracking_enabled = enabled;
        if (enabled) {
            int64_t pos = self->playback.currentPositionMs.load();
            int64_t bytes = self->playback.currentFileBytes.load();
            if (pos > 0) {
                self->currentSidecar.last_position_ms = pos;
                self->currentSidecar.last_position_bytes = bytes;
            }
            snapshot = self->currentSidecar;
            sidecarPath = sidecarPathFor(self->currentSidecarPath, self->sidecarWorker.dir);
        }
    }
    if (enabled && !sidecarPath.empty()) {
        self->sidecarWorker.requestSave(sidecarPath, snapshot);
    }
    lv_label_set_text(self->statusLabel, enabled ? "Resume tracking on." : "Resume tracking off.");
}

// The SliderBox owns its slider/+/-/value-label; the event target is the box.
static void onVolumeChangedCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    self->lastVolumeUserMs = lv_tick_get();
    auto* sliderBox = static_cast<lv_obj_t*>(lv_event_get_target(e));
    tactility_audio_set_output_volume(static_cast<float>(lvgl_sliderbox_get_value(sliderBox)));
    if (tactility_audio_is_output_muted()) {
        tactility_audio_set_output_muted(false);
    }
}

static void onToolbarCloseCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    app_event_emit_close(self->appInstanceId);
}

// ---- Widget helpers ---------------------------------------------------------

// Button with a centered label; `outLabel` receives the label when the caller
// retargets its text later (play/pause symbol, mode-dependent edges).
static lv_obj_t* addButton(Context* self, lv_obj_t* parent, const char* text, lv_event_cb_t callback,
                           lv_obj_t** outLabel = nullptr) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_height(button, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_pad_ver(button, 9, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, self);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (outLabel != nullptr) {
        *outLabel = label;
    }
    return button;
}

// Borderless, zero-padding flex row: the building block for control rows, so
// rows don't inherit lv_obj's default card styling.
static lv_obj_t* addRow(lv_obj_t* parent, int32_t gap) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, gap, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    return row;
}

// Label + switch pair for the toggle row.
static lv_obj_t* addToggle(Context* self, lv_obj_t* parent, const char* text, bool checked, lv_event_cb_t callback) {
    lv_obj_t* column = lv_obj_create(parent);
    lv_obj_remove_flag(column, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(column, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(column, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(column, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(column, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t* label = lv_label_create(column);
    lv_label_set_text(label, text);

    lv_obj_t* toggle = lv_switch_create(column);
    if (checked) {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(toggle, callback, LV_EVENT_VALUE_CHANGED, self);
    return toggle;
}

// ---- Widget tree ------------------------------------------------------------

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* self = static_cast<Context*>(userData);
    self->contentParent = parent;

    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, "Audio");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, &onToolbarCloseCb, self);

    self->mainPanel = lv_obj_create(parent);
    lv_obj_remove_flag(self->mainPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(self->mainPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(self->mainPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(self->mainPanel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(self->mainPanel, 7, LV_PART_MAIN);
    lv_obj_set_style_border_width(self->mainPanel, 0, LV_PART_MAIN);
    lv_obj_set_flex_grow(self->mainPanel, 1);
    lv_obj_set_width(self->mainPanel, LV_PCT(100));
    lv_obj_t* body = self->mainPanel;

    // Title. Dots (not circular scroll) on purpose: a marquee re-invalidates
    // the panel continuously, which this SPI display pays for every frame.
    self->nowPlayingLabel = lv_label_create(body);
    lv_label_set_long_mode(self->nowPlayingLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(self->nowPlayingLabel, LV_PCT(100));
    lv_obj_set_style_text_align(self->nowPlayingLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(self->nowPlayingLabel, "(nothing playing)");

    self->infoLabel = lv_label_create(body);
    lv_label_set_long_mode(self->infoLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(self->infoLabel, LV_PCT(100));
    lv_obj_set_style_text_align(self->infoLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(self->infoLabel, "No track selected");

    self->progressBar = lv_bar_create(body);
    lv_obj_set_width(self->progressBar, LV_PCT(100));
    lv_obj_set_height(self->progressBar, 8);
    lv_bar_set_range(self->progressBar, 0, 1000);
    lv_bar_set_value(self->progressBar, 0, LV_ANIM_OFF);

    self->progressLabel = lv_label_create(body);
    lv_obj_set_style_text_align(self->progressLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(self->progressLabel, "0:00");

    // Transport. The edge buttons are chapter jumps in audiobook mode and
    // track skips in music mode; refreshFromPlaybackState() retargets their
    // labels, so they start with the music-mode symbols.
    lv_obj_t* transportRow = addRow(body, 5);
    addButton(self, transportRow, LV_SYMBOL_PREV, &onEdgeLeftCb, &self->edgeLeftLabel);
    addButton(self, transportRow, "-30", &onBack30Cb);
    addButton(self, transportRow, LV_SYMBOL_PLAY, &onPlayPauseCb, &self->playPauseLabel);
    addButton(self, transportRow, "+30", &onFwd30Cb);
    addButton(self, transportRow, LV_SYMBOL_NEXT, &onEdgeRightCb, &self->edgeRightLabel);

    lv_obj_t* volumeRow = addRow(body, 6);
    lv_obj_t* volumeCaption = lv_label_create(volumeRow);
    lv_label_set_text(volumeCaption, LV_SYMBOL_VOLUME_MAX);
    lv_obj_t* volumeSliderBox = lvgl_sliderbox_create(
        volumeRow, 0, 100, 5, static_cast<int32_t>(tactility_audio_get_output_volume()));
    lv_obj_set_flex_grow(volumeSliderBox, 1);
    self->volumeSliderBox = volumeSliderBox;
    lvgl_sliderbox_add_value_changed_cb(volumeSliderBox, &onVolumeChangedCb, self);

    bool trackingOn = false;
    {
        std::lock_guard lg(self->sidecarMutex);
        trackingOn = self->currentSidecar.tracking_enabled;
    }
    lv_obj_t* toggleRow = addRow(body, 4);
    addToggle(self, toggleRow, "Autoplay", self->playback.autoplay.load(), &onAutoplaySwitchCb);
    self->trackingSwitch = addToggle(self, toggleRow, "Resume", trackingOn, &onTrackingSwitchCb);
    addToggle(self, toggleRow, "Edit", false, &onEditModeSwitchCb);

    // Chapter editor, revealed by the Edit toggle.
    self->editorRow = addRow(body, 4);
    lv_obj_add_flag(self->editorRow, LV_OBJ_FLAG_HIDDEN);
    addButton(self, self->editorRow, "Mark", &onMarkChapterCb);
    addButton(self, self->editorRow, "-5s", &onNudgeBack5Cb);
    addButton(self, self->editorRow, "-1s", &onNudgeBack1Cb);
    addButton(self, self->editorRow, "+1s", &onNudgeFwd1Cb);
    addButton(self, self->editorRow, "+5s", &onNudgeFwd5Cb);

    lv_obj_t* libraryRow = addRow(body, 6);
    addButton(self, libraryRow, LV_SYMBOL_DIRECTORY " Pick MP3", &onPickFileCb);
    addButton(self, libraryRow, LV_SYMBOL_REFRESH " Rescan", &onRescanCb);

    self->statusLabel = lv_label_create(body);
    lv_label_set_long_mode(self->statusLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(self->statusLabel, LV_PCT(100));
    lv_obj_set_style_text_align(self->statusLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(self->statusLabel, "Pick an MP3 to begin.");

    // The refresh path is change-driven, so on a re-show force a repaint of
    // widgets that are correct in state but brand new on screen.
    self->playback.nowPlayingDirty.store(true);
    self->lastRenderedPositionSec = -1;
    self->lastPlayPauseWasPause = !(self->playback.playing.load() && !self->playback.paused.load());

    refreshFromPlaybackState(self);
    // A Play/Pause request made while the app was closed must not fire on open.
    tactility_audio_clear_play_pause_request();
    self->pollTimer = lv_timer_create(&pollTimerCb, 500, self);

    ensureBackendStarted(self);
}

void destroyWidgets(void* userData) {
    auto* self = static_cast<Context*>(userData);

    maybeSaveSidecarNow(self);
    if (self->pollTimer != nullptr) {
        lv_timer_del(self->pollTimer);
        self->pollTimer = nullptr;
    }
    self->nowPlayingLabel = nullptr;
    self->infoLabel = nullptr;
    self->progressBar = nullptr;
    self->progressLabel = nullptr;
    self->playPauseLabel = nullptr;
    self->edgeLeftLabel = nullptr;
    self->edgeRightLabel = nullptr;
    self->statusLabel = nullptr;
    self->trackingSwitch = nullptr;
    self->editorRow = nullptr;
    self->volumeSliderBox = nullptr;
    self->mainPanel = nullptr;
    self->browserPanel = nullptr;
    self->contentParent = nullptr;
}
