// SPDX-License-Identifier: Apache-2.0
// App glue: storage init, sidecar adoption, transport, seek/chapters,
// last-played and library index plumbing.

#include "AudiobookPlayer.h"
#include "AudioServiceApi.h"
#include "FileUtil.h"

#include <app/paths.h>

#include <tactility/log.h>

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

// ---- Storage init ----------------------------------------------------------

void initAppData(Context* self) {
    self->playback.shutdown.store(false);
    self->playback.pending.store(PlaybackRequest::None);

    char dir[192] = {0};
    if (app_paths_get_user_data_directory(APP_ID, dir, sizeof(dir)) != ERROR_NONE) {
        LOG_E(TAG, "app_paths_get_user_data_directory failed");
    }
    self->appDataDir = dir;
    self->sidecarWorker.appDataDir = self->appDataDir;
    self->sidecarWorker.dir = self->appDataDir + "/" + RESUME_TRACKING_DIR;
    self->sidecarWorker.lastPlayedPath = self->appDataDir + "/" + LAST_PLAYED_FILE;
    self->sidecarWorker.libraryIndexPath = self->appDataDir + "/" + LIBRARY_INDEX_FILE;
}

// Starts the sidecar worker and playback task on first show; also kicks off
// the last-played restore. Idempotent: createWidgets reruns on resurface.
void ensureBackendStarted(Context* self) {
    if (self->backendStarted) {
        return;
    }
    self->backendStarted = true;

    self->sidecarWorker.start();
    self->sidecarWorker.requestInitStorage();

    BaseType_t ok = xTaskCreate(
        &playbackTaskEntry, "audio_pb", 12 * 1024,
        &self->playback, tskIDLE_PRIORITY + 3, &self->playback.task);
    if (ok != pdPASS) {
        LOG_E(TAG, "Failed to create playback task");
        self->playback.task = nullptr;
    }
}

// ---- Sidecar adoption (poll timer consumes worker results) ------------------

static void loadSidecarForPath(Context* self, const std::string& path) {
    {
        std::lock_guard lg(self->sidecarMutex);
        if (path == self->sidecarLoadPendingPath) return; // already in flight
        self->sidecarLoadPendingPath = path;
    }
    self->sidecarWorker.requestLoad(path);
}

static void loadSidecarForCurrentTrack(Context* self) {
    std::string path;
    {
        std::lock_guard lg(self->playback.playlistMutex);
        int idx = self->playback.playlistIndex.load();
        if (idx < 0 || idx >= (int) self->playback.playlist.size()) return;
        path = self->playback.playlist[idx];
    }
    loadSidecarForPath(self, path);
}

// Adopt a completed sidecar Load job if it matches the track we asked for.
static void adoptLoadedSidecarIfReady(Context* self) {
    if (!self->sidecarWorker.loadedReady.load()) return;
    std::lock_guard lg(self->sidecarMutex);
    if (self->sidecarLoadPendingPath.empty()) return;
    std::string loadedPath;
    SidecarData data;
    {
        std::lock_guard lg2(self->sidecarWorker.loadedMutex);
        if (self->sidecarWorker.loadedMp3Path != self->sidecarLoadPendingPath) return;
        loadedPath = self->sidecarWorker.loadedMp3Path;
        data = std::move(self->sidecarWorker.loadedData);
    }
    self->sidecarWorker.loadedReady.store(false);
    self->currentSidecar = std::move(data);
    self->currentSidecarPath = loadedPath;
    self->sidecarLoadPendingPath.clear();
    self->lastSavedPositionMs = -1;
    self->lastRenderedPositionSec = -1;
    self->lastEditChapterIdx = -1;
    if (self->trackingSwitch != nullptr) {
        if (self->currentSidecar.tracking_enabled) {
            lv_obj_add_state(self->trackingSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(self->trackingSwitch, LV_STATE_CHECKED);
        }
    }
    // Seed the position display from the saved position only when idle; during
    // playback the decoder owns the position counter.
    if (!self->playback.playing.load()) {
        self->playback.currentPositionMs.store(
            self->currentSidecar.last_position_ms > 0 ? self->currentSidecar.last_position_ms : 0);
        self->playback.currentFileBytes.store(self->currentSidecar.last_position_bytes);
    }
    self->playback.nowPlayingDirty.store(true);
}

static void adoptPlaylistFromFolder(Context* self, const std::string& pickedPath) {
    LOG_I(TAG, "adoptPlaylistFromFolder enqueue picked='%s'", pickedPath.c_str());
    self->sidecarWorker.requestAdoptPlaylist(pickedPath);
}

static void applyReadyPlaylistIfAny(Context* self) {
    if (!self->sidecarWorker.playlistReady.load()) return;
    std::vector<std::string> files;
    int idx = -1;
    std::string picked;
    {
        std::lock_guard lg(self->sidecarWorker.playlistReadyMutex);
        files = std::move(self->sidecarWorker.playlistReadyFiles);
        idx = self->sidecarWorker.playlistReadyIndex;
        picked = std::move(self->sidecarWorker.playlistReadyPicked);
        self->sidecarWorker.playlistReadyIndex = -1;
    }
    self->sidecarWorker.playlistReady.store(false);
    if (files.empty()) {
        LOG_W(TAG, "No MP3 files found for %s", picked.c_str());
        return;
    }
    self->libraryRootPath = dirnameOf(picked);
    {
        std::lock_guard lg(self->playback.playlistMutex);
        self->playback.playlist = std::move(files);
        self->playback.playlistIndex.store(idx);
    }
    loadSidecarForCurrentTrack(self);
    publishNowPlaying(&self->playback, picked);
    LOG_I(TAG, "Playlist loaded: %d entries, starting at %d",
          (int) self->playback.playlist.size(), idx);
}

static void applyReadyScanIfAny(Context* self) {
    if (!self->sidecarWorker.scanReady.load()) return;
    std::string root;
    size_t count = 0;
    bool saved = false;
    {
        std::lock_guard lg(self->sidecarWorker.scanMutex);
        root = self->sidecarWorker.scanRoot;
        count = self->sidecarWorker.scanCount;
        saved = self->sidecarWorker.scanSaved;
    }
    self->sidecarWorker.scanReady.store(false);
    char buf[96];
    snprintf(buf, sizeof(buf),
             saved ? "Indexed %zu tracks." : "Index failed (%zu tracks).",
             count);
    if (self->statusLabel != nullptr) lv_label_set_text(self->statusLabel, buf);
    LOG_I(TAG, "Library scan root='%s' count=%zu saved=%d",
          root.c_str(), count, (int) saved);
}

static void saveLastPlayed(Context* self, const std::string& path) {
    if (path == self->savedLastPath) return;
    self->savedLastPath = path;
    self->sidecarWorker.requestSaveLastPlayed(path);
}

static void applyReadyLastPlayedIfAny(Context* self) {
    if (!self->sidecarWorker.lastPlayedReady.load()) return;
    std::string path;
    {
        std::lock_guard lg(self->sidecarWorker.lastPlayedMutex);
        path = self->sidecarWorker.lastPlayedReadyPath;
        self->sidecarWorker.lastPlayedReadyPath.clear();
    }
    self->sidecarWorker.lastPlayedReady.store(false);
    if (!path.empty()) {
        self->lastPlayedFilePath = path;
        self->savedLastPath = path;
        adoptPlaylistFromFolder(self, path);
    }
}

void refreshFromPlaybackState(Context* self) {
    applyReadyLastPlayedIfAny(self);
    applyReadyPlaylistIfAny(self);
    applyReadyScanIfAny(self);
    adoptLoadedSidecarIfReady(self);

    // Play/pause symbol follows live state (cheap; only redrawn on change).
    bool showPause = self->playback.playing.load() && !self->playback.paused.load();
    if (self->playPauseLabel != nullptr && showPause != self->lastPlayPauseWasPause) {
        self->lastPlayPauseWasPause = showPause;
        lv_label_set_text(self->playPauseLabel, showPause ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }

    if (self->playback.nowPlayingDirty.exchange(false)) {
        std::string path;
        {
            std::lock_guard lg(self->playback.nowPlayingMutex);
            path = self->playback.nowPlayingPath;
        }
        if (self->nowPlayingLabel != nullptr) {
            if (path.empty()) {
                lv_label_set_text(self->nowPlayingLabel, "(nothing playing)");
            } else {
                lv_label_set_text(self->nowPlayingLabel, fileLastPathSegment(path).c_str());
            }
        }
        // Playlist advanced on the playback thread: load the new track's sidecar.
        if (!path.empty()) {
            bool needLoad = false;
            {
                std::lock_guard lg(self->sidecarMutex);
                needLoad = (path != self->currentSidecarPath && path != self->sidecarLoadPendingPath);
            }
            if (needLoad) {
                loadSidecarForPath(self, path);
            }
        }
        // Info line: kind + playlist position.
        if (self->infoLabel != nullptr) {
            std::string kind;
            {
                std::lock_guard lg(self->sidecarMutex);
                kind = self->currentSidecar.kind;
            }
            if (kind.empty()) kind = "audio";
            kind[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(kind[0])));
            std::string info = kind;
            {
                std::lock_guard lg(self->playback.playlistMutex);
                int idx = self->playback.playlistIndex.load();
                size_t total = self->playback.playlist.size();
                if (total > 0 && idx >= 0) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), " - %d of %zu", idx + 1, total);
                    info += buf;
                }
            }
            lv_label_set_text(self->infoLabel, info.c_str());
        }
        // Edge buttons: chapter jumps for audiobooks, track skip for music.
        bool audiobookMode = false;
        {
            std::lock_guard lg(self->sidecarMutex);
            audiobookMode = (self->currentSidecar.kind == "audiobook");
        }
        if (self->edgeLeftLabel != nullptr && self->edgeRightLabel != nullptr) {
            if (audiobookMode) {
                lv_label_set_text(self->edgeLeftLabel, LV_SYMBOL_LEFT " Ch");
                lv_label_set_text(self->edgeRightLabel, "Ch " LV_SYMBOL_RIGHT);
            } else {
                lv_label_set_text(self->edgeLeftLabel, LV_SYMBOL_PREV);
                lv_label_set_text(self->edgeRightLabel, LV_SYMBOL_NEXT);
            }
        }
    }
}

// ---- Sidecar save cadence ---------------------------------------------------

// Periodic crash-recovery checkpoint, at most every ~180 s of active
// playback. Deliberate actions force an immediate save instead.
void maybeSaveSidecar(Context* self) {
    if (!self->playback.playing.load()) return;
    int64_t pos = self->playback.currentPositionMs.load();
    int64_t bytes = self->playback.currentFileBytes.load();
    if (pos <= 0) return;
    {
        std::lock_guard lg(self->sidecarMutex);
        if (self->currentSidecarPath.empty()) return;
        if (!self->currentSidecar.tracking_enabled) return;
        if (self->lastSavedPositionMs >= 0 && std::abs(pos - self->lastSavedPositionMs) < 15000) return;
        self->currentSidecar.last_position_ms = pos;
        self->currentSidecar.last_position_bytes = bytes;
        self->lastSavedPositionMs = pos;
        self->sidecarWorker.requestSave(sidecarPathFor(self->currentSidecarPath, self->sidecarWorker.dir),
                                        self->currentSidecar);
    }
}

// Force-save regardless of the debounce interval (pause/hide/close).
void maybeSaveSidecarNow(Context* self) {
    int64_t pos = self->playback.currentPositionMs.load();
    int64_t bytes = self->playback.currentFileBytes.load();
    if (pos <= 0) return;
    {
        std::lock_guard lg(self->sidecarMutex);
        if (self->currentSidecarPath.empty()) return;
        if (!self->currentSidecar.tracking_enabled) return;
        self->currentSidecar.last_position_ms = pos;
        self->currentSidecar.last_position_bytes = bytes;
        self->lastSavedPositionMs = pos;
        self->sidecarWorker.requestSave(sidecarPathFor(self->currentSidecarPath, self->sidecarWorker.dir),
                                        self->currentSidecar);
    }
}

// ---- Transport --------------------------------------------------------------

static void startPlayback(Context* self) {
    int idx = -1;
    size_t total = 0;
    std::string firstPath;
    {
        std::lock_guard lg(self->playback.playlistMutex);
        idx = self->playback.playlistIndex.load();
        total = self->playback.playlist.size();
        if (idx >= 0 && idx < static_cast<int>(total)) firstPath = self->playback.playlist[idx];
    }
    LOG_I(TAG, "Play tapped: playlist size=%zu idx=%d first=%s",
          total, idx, firstPath.c_str());
    if (total == 0 || idx < 0 || idx >= static_cast<int>(total)) {
        lv_label_set_text(self->statusLabel, "Pick an MP3 first.");
        return;
    }
    // Resume starts 1 s before the saved position so the listener re-hears the
    // last moment of context; the byte offset is scaled proportionally.
    int64_t resumeBytes = 0;
    int64_t resumeMs = 0;
    {
        std::lock_guard lg(self->sidecarMutex);
        if (self->currentSidecar.tracking_enabled &&
            self->currentSidecar.last_position_bytes > 0) {
            resumeMs = self->currentSidecar.last_position_ms;
            resumeBytes = self->currentSidecar.last_position_bytes;
            if (resumeMs > 1000) {
                int64_t adjustedMs = resumeMs - 1000;
                resumeBytes = (resumeBytes * adjustedMs) / resumeMs;
                resumeMs = adjustedMs;
            } else {
                resumeMs = 0;
                resumeBytes = 0;
            }
        }
    }
    self->playback.paused.store(false);
    self->playback.nextStartBytes.store(resumeBytes);
    self->playback.nextStartMs.store(resumeMs);
    self->playback.stopRequested.store(true); // stop any current run first
    self->playback.pending.store(PlaybackRequest::PlayCurrent);
    lv_label_set_text(self->statusLabel,
                      resumeBytes > 0 ? "Resuming..." : "Playing...");
}

static void pausePlayback(Context* self) {
    maybeSaveSidecarNow(self);
    self->playback.paused.store(true);
    self->playback.stopRequested.store(true);
    lv_label_set_text(self->statusLabel, "Pausing...");
}

static void resumePausedPlayback(Context* self) {
    int64_t resumeMs = self->playback.currentPositionMs.load();
    int64_t resumeBytes = self->playback.currentFileBytes.load();
    if (resumeMs > 1000 && resumeBytes > 0) {
        int64_t adjustedMs = resumeMs - 1000;
        resumeBytes = (resumeBytes * adjustedMs) / resumeMs;
        resumeMs = adjustedMs;
    } else {
        resumeMs = 0;
        resumeBytes = 0;
    }
    self->playback.nextStartBytes.store(resumeBytes);
    self->playback.nextStartMs.store(resumeMs);
    self->playback.stopRequested.store(false);
    self->playback.paused.store(false);
    self->playback.pending.store(PlaybackRequest::PlayCurrent);
    lv_label_set_text(self->statusLabel, "Playing...");
}

// Unified play/pause: playing -> pause; paused -> resume; stopped -> start.
// Shared by the on-screen button and the headset Play/Pause media key.
void handlePlayPauseToggle(Context* self) {
    if (self->playback.playing.load() && !self->playback.paused.load()) {
        pausePlayback(self);
    } else if (self->playback.paused.load() && !self->playback.playing.load()) {
        resumePausedPlayback(self);
    } else if (self->playback.paused.load()) {
        lv_label_set_text(self->statusLabel, "Pausing...");
    } else {
        startPlayback(self);
    }
}

void trackNext(Context* self) {
    maybeSaveSidecarNow(self);
    self->playback.paused.store(false);
    if (self->playback.playing.load()) {
        // Cursor advances on the playback thread.
        self->playback.skipNext.store(true);
    } else {
        {
            std::lock_guard lg(self->playback.playlistMutex);
            int idx = self->playback.playlistIndex.load();
            int size = static_cast<int>(self->playback.playlist.size());
            if (idx + 1 < size) {
                self->playback.playlistIndex.store(idx + 1);
            }
        }
        loadSidecarForCurrentTrack(self);
        self->playback.pending.store(PlaybackRequest::PlayCurrent);
    }
}

void trackPrev(Context* self) {
    maybeSaveSidecarNow(self);
    self->playback.paused.store(false);
    if (self->playback.playing.load()) {
        self->playback.skipPrev.store(true);
    } else {
        {
            std::lock_guard lg(self->playback.playlistMutex);
            int idx = self->playback.playlistIndex.load();
            if (idx > 0) {
                self->playback.playlistIndex.store(idx - 1);
            }
        }
        loadSidecarForCurrentTrack(self);
        self->playback.pending.store(PlaybackRequest::PlayCurrent);
    }
}

// ---- Seek / chapters --------------------------------------------------------

// Sets seekRequestMs so the playback loop returns Seeked and runPlaylist
// restarts the current file at the new byte offset.
void requestSeekMs(Context* self, int64_t targetMs) {
    if (targetMs < 0) targetMs = 0;
    if (!self->playback.playing.load()) {
        // Not playing: prime nextStartBytes for the next PlayCurrent.
        int64_t bpMsX1000 = self->playback.bytesPerMsX1000.load();
        int64_t startBytes = 0;
        if (bpMsX1000 > 0) startBytes = (targetMs * bpMsX1000) / 1000;
        self->playback.nextStartMs.store(targetMs);
        self->playback.nextStartBytes.store(startBytes);
        return;
    }
    self->playback.seekRequestMs.store(targetMs);
}

void chapterPrev(Context* self) {
    int64_t pos = self->playback.currentPositionMs.load();
    int64_t target = 0;
    {
        std::lock_guard lg(self->sidecarMutex);
        // 2 s grace so a quick double-tap goes back further, not to "now".
        int64_t threshold = pos - 2000;
        for (const auto& c : self->currentSidecar.chapters) {
            if (c.start_ms <= threshold) target = c.start_ms;
            else break;
        }
    }
    requestSeekMs(self, target);
}

void chapterNext(Context* self) {
    int64_t pos = self->playback.currentPositionMs.load();
    int64_t target = -1;
    {
        std::lock_guard lg(self->sidecarMutex);
        for (const auto& c : self->currentSidecar.chapters) {
            if (c.start_ms > pos + 500) { target = c.start_ms; break; }
        }
    }
    if (target >= 0) requestSeekMs(self, target);
}

// Chapter editor: mark the current position, nudge the selected marker.
// Marking implies resume tracking; every change is saved immediately.

static void saveSidecarSnapshotLocked(Context* self) {
    // Call with sidecarMutex held.
    self->sidecarWorker.requestSave(sidecarPathFor(self->currentSidecarPath, self->sidecarWorker.dir),
                                    self->currentSidecar);
}

void markChapter(Context* self) {
    int64_t pos = self->playback.currentPositionMs.load();
    {
        std::lock_guard lg(self->sidecarMutex);
        if (self->currentSidecarPath.empty()) {
            lv_label_set_text(self->statusLabel, "Pick a track first.");
            return;
        }
        auto& chapters = self->currentSidecar.chapters;
        // A chapter within 2 s is selected for nudging, not duplicated.
        for (size_t i = 0; i < chapters.size(); ++i) {
            if (std::abs(chapters[i].start_ms - pos) < 2000) {
                self->lastEditChapterIdx = static_cast<int>(i);
                std::string msg = chapters[i].title + " already here; nudge to adjust.";
                lv_label_set_text(self->statusLabel, msg.c_str());
                return;
            }
        }
        Chapter chapter;
        chapter.start_ms = pos;
        chapters.push_back(chapter);
        normalizeChapters(chapters);
        for (size_t i = 0; i < chapters.size(); ++i) {
            if (chapters[i].start_ms == pos) {
                self->lastEditChapterIdx = static_cast<int>(i);
                break;
            }
        }
        self->currentSidecar.tracking_enabled = true; // chapters require the file
        saveSidecarSnapshotLocked(self);
        std::string msg = "Marked " + chapters[self->lastEditChapterIdx].title +
                          " at " + formatDurationMs(pos);
        lv_label_set_text(self->statusLabel, msg.c_str());
    }
    if (self->trackingSwitch != nullptr) {
        lv_obj_add_state(self->trackingSwitch, LV_STATE_CHECKED);
    }
}

void nudgeChapter(Context* self, int64_t deltaMs) {
    {
        std::lock_guard lg(self->sidecarMutex);
        if (self->currentSidecarPath.empty() || self->lastEditChapterIdx < 0 ||
            self->lastEditChapterIdx >= static_cast<int>(self->currentSidecar.chapters.size())) {
            lv_label_set_text(self->statusLabel, "Mark a chapter first.");
            return;
        }
        auto& chapters = self->currentSidecar.chapters;
        int64_t newStart = chapters[self->lastEditChapterIdx].start_ms + deltaMs;
        if (newStart < 0) newStart = 0;
        chapters[self->lastEditChapterIdx].start_ms = newStart;
        normalizeChapters(chapters);
        for (size_t i = 0; i < chapters.size(); ++i) {
            if (chapters[i].start_ms == newStart) {
                self->lastEditChapterIdx = static_cast<int>(i);
                break;
            }
        }
        saveSidecarSnapshotLocked(self);
        std::string msg = chapters[self->lastEditChapterIdx].title +
                          " -> " + formatDurationMs(newStart);
        lv_label_set_text(self->statusLabel, msg.c_str());
    }
}

// ---- Library ----------------------------------------------------------------

void requestLibraryRescan(Context* self) {
    if (self->playback.playing.load() || self->playback.paused.load()) {
        lv_label_set_text(self->statusLabel, "Stop playback before Rescan.");
        return;
    }
    if (self->libraryRootPath.empty()) {
        lv_label_set_text(self->statusLabel, "Pick an MP3 first.");
        return;
    }
    self->sidecarWorker.requestScanIndex(self->libraryRootPath);
    lv_label_set_text(self->statusLabel, "Scanning library...");
}

// Called by the file browser when an MP3 is selected.
void onFilePicked(Context* self, const std::string& path) {
    LOG_I(TAG, "Selected: %s", path.c_str());
    self->lastPlayedFilePath = path;
    self->libraryRootPath = dirnameOf(path);
    adoptPlaylistFromFolder(self, path);
    saveLastPlayed(self, path);
}
