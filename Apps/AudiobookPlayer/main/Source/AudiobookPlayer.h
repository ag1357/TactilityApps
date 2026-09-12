// SPDX-License-Identifier: Apache-2.0
#pragma once

// Requires firmware that exports the audio decoder, audio service, cJSON and
// app event symbols to ELF apps (see the manifest note in Tactility).

#include <lvgl.h>

#include <tactility/freertos/freertos.h>
#include <tactility/freertos/queue.h>
#include <tactility/freertos/task.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct TaskEventGroup;

constexpr auto* TAG = "AudioApp";
// Also the storage id: all persisted state is keyed by it.
constexpr auto* APP_ID = "one.tactility.audio";
constexpr auto* LAST_PLAYED_FILE = "lastplayed.txt";
constexpr auto* LIBRARY_INDEX_FILE = "library-index.json";
constexpr auto* RESUME_TRACKING_DIR = "resume-tracking";
constexpr size_t MAX_INDEX_TRACKS = 512;
constexpr size_t MAX_INDEX_DIRS = 256;

// UI thread -> playback thread request.
enum class PlaybackRequest : uint8_t {
    None,
    PlayCurrent,
    Stop,
};

// Why a single-file run ended; drives the playlist loop's next step.
enum class PlaybackReason : uint8_t {
    ReachedEnd,
    Stopped,
    SkippedNext,
    SkippedPrev,
    Shutdown,
    OpenFailed,
    Seeked,
    // Output write failed (e.g. codec rebind on headset attach): park paused
    // on the same track instead of autoplay-advancing.
    WriteFailed,
};

struct Chapter {
    int number = 1;
    std::string title;
    int64_t start_ms = 0;
};

struct LibraryIndexEntry {
    std::string path;
    std::string kind;
    std::string title;
    int64_t size_bytes = 0;
    int64_t duration_ms = 0;
};

// In-memory mirror of a `.taudio.json` sidecar.
struct SidecarData {
    std::string audio_file;
    std::string kind; // "audiobook" | "music" | "audio"
    std::string title;
    std::string author;
    // Resume tracking is opt-in: default-on for detected audiobooks, off for
    // music so plain music folders never spawn tracking files.
    bool tracking_enabled = false;
    int64_t duration_ms = 0;
    int64_t last_position_ms = 0;
    // Byte offset in addition to ms so seeking is exact regardless of bitrate.
    int64_t last_position_bytes = 0;
    std::vector<Chapter> chapters;
};

// Shared state between the LVGL thread and the playback thread. The playback
// thread never touches lv_obj; it publishes state the poll timer reads.
struct PlaybackState {
    std::atomic<PlaybackRequest> pending{PlaybackRequest::None};
    std::atomic<bool> shutdown{false};
    std::atomic<bool> skipNext{false};
    std::atomic<bool> skipPrev{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> autoplay{true};
    std::atomic<bool> playing{false};
    // Pause stops the active run and resumes from the captured byte/ms
    // position: holding the output stream open without writing repeats the
    // final DMA buffer on this hardware.
    std::atomic<bool> paused{false};
    // Set by the playback thread after an output error parked playback; the
    // poll timer persists the sidecar at the interruption point.
    std::atomic<bool> pendingErrorSave{false};

    // Snapshot so playlist iteration never holds the mutex through writes.
    std::mutex playlistMutex;
    std::vector<std::string> playlist;
    std::atomic<int> playlistIndex{-1};

    std::mutex nowPlayingMutex;
    std::string nowPlayingPath;
    std::atomic<bool> nowPlayingDirty{true};

    // Position / seek plumbing. bytesPerMsX1000 is learned from decoded audio
    // (fixed-point *1000; 0 = unknown) and translates ms seeks to byte offsets.
    std::atomic<int64_t> currentPositionMs{0};
    std::atomic<int64_t> currentFileBytes{0};
    std::atomic<int64_t> currentFileSize{0};
    std::atomic<int64_t> bytesPerMsX1000{0};
    std::atomic<int64_t> seekRequestMs{-1};
    std::atomic<int64_t> nextStartBytes{0};
    std::atomic<int64_t> nextStartMs{0};

    TaskHandle_t task = nullptr;
};

// Job posted to the SidecarWorker queue.
struct SidecarJob {
    enum class Type : uint8_t { InitStorage, Load, Save, AdoptPlaylist, SaveLastPlayed, ScanIndex, Shutdown } type;
    std::string mp3Path {};     // Load / AdoptPlaylist / SaveLastPlayed: track/root path
    std::string sidecarPath {}; // Save: explicit destination
    SidecarData data {};        // Save: snapshot to write
};

// Keeps all SD card I/O off the LVGL thread; results land in the "Ready"
// slots the poll timer consumes.
struct SidecarWorker {
    std::string appDataDir;
    std::string dir; // app user-data resume-tracking directory (SD card)
    std::string lastPlayedPath;
    std::string libraryIndexPath;
    QueueHandle_t queue = nullptr;
    TaskHandle_t task = nullptr;

    std::mutex loadedMutex;
    SidecarData loadedData;
    std::string loadedMp3Path;
    std::atomic<bool> loadedReady{false};

    std::mutex playlistReadyMutex;
    std::vector<std::string> playlistReadyFiles;
    int playlistReadyIndex = -1;
    std::string playlistReadyPicked;
    std::atomic<bool> playlistReady{false};

    std::mutex lastPlayedMutex;
    std::string lastPlayedReadyPath;
    std::atomic<bool> lastPlayedReady{false};

    std::mutex scanMutex;
    std::string scanRoot;
    size_t scanCount = 0;
    bool scanSaved = false;
    std::atomic<bool> scanReady{false};

    void start();
    void stop();
    bool enqueue(struct SidecarJob* job, TickType_t timeout = 0);
    void requestInitStorage();
    void requestLoad(const std::string& mp3Path);
    void requestSave(const std::string& sidecarPath, const SidecarData& data);
    void requestAdoptPlaylist(const std::string& pickedPath);
    void requestSaveLastPlayed(const std::string& path);
    void requestScanIndex(const std::string& rootPath);
};

struct Context {
    uint32_t appInstanceId = 0;
    TaskEventGroup* eventGroup = nullptr;

    PlaybackState playback;

    std::string lastPlayedFilePath;
    std::string savedLastPath;
    std::string appDataDir;
    std::string libraryRootPath;

    // Sidecar state for the currently-selected track; all I/O on sidecarWorker.
    std::mutex sidecarMutex;
    SidecarData currentSidecar;
    std::string currentSidecarPath;     // MP3 path the sidecar is for; empty = none
    std::string sidecarLoadPendingPath; // MP3 path a Load job was requested for
    int64_t lastSavedPositionMs = -1;
    // Sidecar checkpoint cadence: at most every ~180 s of active playback.
    uint32_t saveCounterTicks = 0;
    SidecarWorker sidecarWorker;
    int64_t lastRenderedPositionSec = -1; // throttles position label redraws

    lv_obj_t* contentParent = nullptr;   // window body the views are built into
    lv_obj_t* mainPanel = nullptr;
    lv_obj_t* browserPanel = nullptr;
    std::string browsePath;

    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* nowPlayingLabel = nullptr;
    lv_obj_t* infoLabel = nullptr;
    lv_obj_t* progressBar = nullptr;
    lv_obj_t* progressLabel = nullptr;
    lv_obj_t* playPauseLabel = nullptr;
    lv_obj_t* edgeLeftLabel = nullptr;
    lv_obj_t* edgeRightLabel = nullptr;
    // Only the tracking switch is retargeted by non-UI events (a loaded
    // sidecar can turn tracking on by itself).
    lv_obj_t* trackingSwitch = nullptr;
    lv_obj_t* editorRow = nullptr;
    int lastEditChapterIdx = -1; // chapter targeted by nudge (sidecarMutex)
    bool lastPlayPauseWasPause = false;
    lv_timer_t* pollTimer = nullptr;

    // Volume slider edits the global output volume (the same value headset
    // buttons change). lastVolumeUserMs suppresses the external-change sync
    // briefly after a user edit so a drag is never fought.
    lv_obj_t* volumeSliderBox = nullptr;
    uint32_t lastVolumeUserMs = 0;

    bool backendStarted = false; // guards ensureBackendStarted()
};

// ---- PlaybackService.cpp ---------------------------------------------------

// Playlist runner task: consumes PlaybackRequest and advances the playlist.
void playbackTaskEntry(void* param);
void publishNowPlaying(PlaybackState* state, const std::string& path);

// ---- SidecarStore.cpp ------------------------------------------------------

bool natLess(const std::string& a, const std::string& b);
std::string formatDurationMs(int64_t ms);
bool hasMp3Extension(const std::string& name);
std::string dirnameOf(const std::string& path);
int64_t fileSizeOf(const std::string& path);
std::string sidecarPathFor(const std::string& mp3Path, const std::string& sidecarDir);
SidecarData loadSidecar(const std::string& mp3Path, const std::string& sidecarPath);
bool saveSidecar(const std::string& sidecarPath, const SidecarData& data);
std::vector<std::string> scanFolderForMp3(const std::string& folder);
bool saveLibraryIndex(const std::string& indexPath,
                      const std::string& root,
                      const std::vector<LibraryIndexEntry>& entries);
void normalizeChapters(std::vector<Chapter>& chapters);

// ---- AudiobookPlayer.cpp ---------------------------------------------------

void initAppData(Context* self);
void ensureBackendStarted(Context* self);
void maybeSaveSidecar(Context* self);
void maybeSaveSidecarNow(Context* self);
void requestSeekMs(Context* self, int64_t targetMs);
void handlePlayPauseToggle(Context* self);
void trackNext(Context* self);
void trackPrev(Context* self);
void chapterPrev(Context* self);
void chapterNext(Context* self);
void markChapter(Context* self);
void nudgeChapter(Context* self, int64_t deltaMs);
void requestLibraryRescan(Context* self);
void onFilePicked(Context* self, const std::string& path);
void refreshFromPlaybackState(Context* self);

// ---- AudiobookPlayerUI.cpp -------------------------------------------------

void createWidgets(lv_obj_t* parent, void* userData);
void destroyWidgets(void* userData);

// ---- FileBrowser.cpp -------------------------------------------------------

// In-app browser replacing the framework FileSelection child app (not
// exported to external apps). Shows browserPanel, hides mainPanel.
void fileBrowserShow(Context* self);
