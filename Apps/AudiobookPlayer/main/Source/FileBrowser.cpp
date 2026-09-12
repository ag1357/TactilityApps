// SPDX-License-Identifier: Apache-2.0
// In-app file browser: directory listing with folder navigation and MP3
// selection, built on the LVGL thread like EpubReader's.

#include "AudiobookPlayer.h"
#include "FileUtil.h"

#include <lvgl/lvgl.h>

#include <dirent.h>

#include <algorithm>

namespace {

struct BrowseEntry {
    std::string name;
    std::string path;
    bool isDir;
};

// Browsing starts at the last-played folder when known, else the SD root.
std::string initialBrowsePath(Context* self) {
    if (!self->lastPlayedFilePath.empty()) {
        std::string dir = dirnameOf(self->lastPlayedFilePath);
        if (fileIsDirectory(dir)) return dir;
    }
    return "/sdcard";
}

// Directory listing for the current browse path: subfolders first, then MP3s.
std::vector<BrowseEntry> listBrowseEntries(Context* self) {
    std::vector<BrowseEntry> dirs;
    std::vector<BrowseEntry> files;
    DIR* dir = opendir(self->browsePath.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            std::string path = fileChildPath(self->browsePath, name);
            bool isDir = entry->d_type == DT_DIR;
            // FAT d_type quirk: only stat non-MP3 names.
            if (!isDir && !hasMp3Extension(name)) {
                isDir = fileIsDirectory(path);
            }
            if (isDir) {
                dirs.push_back({name, path, true});
            } else if (hasMp3Extension(name)) {
                files.push_back({name, path, false});
            }
        }
        closedir(dir);
    }
    auto byName = [](const BrowseEntry& a, const BrowseEntry& b) {
        return natLess(a.name, b.name);
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    dirs.insert(dirs.end(), files.begin(), files.end());
    return dirs;
}

// Frees the browser panel and brings the main view back. `rebuild` re-opens
// the browser in browsePath (directory navigation).
void browserClose(Context* self, bool rebuild) {
    if (self->browserPanel != nullptr) {
        lv_obj_delete(self->browserPanel);
        self->browserPanel = nullptr;
    }
    if (rebuild) {
        fileBrowserShow(self);
        return;
    }
    if (self->mainPanel != nullptr) {
        lv_obj_remove_flag(self->mainPanel, LV_OBJ_FLAG_HIDDEN);
    }
    // Repaint everything: the main view may have changed (picked track).
    self->playback.nowPlayingDirty.store(true);
    self->lastRenderedPositionSec = -1;
    refreshFromPlaybackState(self);
}

void onBrowserEntryCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    auto* button = lv_event_get_target_obj(e);
    auto* entry = static_cast<BrowseEntry*>(lv_obj_get_user_data(button));
    if (entry->isDir) {
        self->browsePath = entry->path;
        browserClose(self, /*rebuild=*/true);
    } else {
        std::string path = entry->path;
        onFilePicked(self, path);
        browserClose(self, /*rebuild=*/false);
    }
}

void onBrowserCancelCb(lv_event_t* e) {
    browserClose(static_cast<Context*>(lv_event_get_user_data(e)), false);
}

void onBrowserUpCb(lv_event_t* e) {
    auto* self = static_cast<Context*>(lv_event_get_user_data(e));
    if (self->browsePath == "/") return;
    std::string parent = dirnameOf(self->browsePath);
    if (parent == self->browsePath) return;
    self->browsePath = parent;
    browserClose(self, /*rebuild=*/true);
}

} // namespace

void fileBrowserShow(Context* self) {
    if (self->contentParent == nullptr) return;
    if (self->browserPanel != nullptr) return; // already browsing

    if (self->browsePath.empty()) {
        self->browsePath = initialBrowsePath(self);
    }

    if (self->mainPanel != nullptr) {
        lv_obj_add_flag(self->mainPanel, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* panel = lv_obj_create(self->contentParent);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_width(panel, LV_PCT(100));
    self->browserPanel = panel;

    lv_obj_t* navRow = lv_obj_create(panel);
    lv_obj_remove_flag(navRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(navRow, LV_PCT(100));
    lv_obj_set_height(navRow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(navRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(navRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(navRow, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(navRow, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(navRow, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t* upButton = lv_btn_create(navRow);
    lv_obj_add_event_cb(upButton, &onBrowserUpCb, LV_EVENT_CLICKED, self);
    lv_obj_t* upLabel = lv_label_create(upButton);
    lv_label_set_text(upLabel, LV_SYMBOL_UP " Up");
    lv_obj_center(upLabel);

    lv_obj_t* cancelButton = lv_btn_create(navRow);
    lv_obj_add_event_cb(cancelButton, &onBrowserCancelCb, LV_EVENT_CLICKED, self);
    lv_obj_t* cancelLabel = lv_label_create(cancelButton);
    lv_label_set_text(cancelLabel, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_center(cancelLabel);

    lv_obj_t* pathLabel = lv_label_create(panel);
    lv_label_set_long_mode(pathLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(pathLabel, LV_PCT(100));
    lv_obj_set_style_text_align(pathLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(pathLabel, self->browsePath.c_str());

    lv_obj_t* list = lv_list_create(panel);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);

    // Entry vector is heap-allocated and stable; buttons reference its
    // elements and it is freed on the panel's delete.
    auto* entries = new std::vector<BrowseEntry>(listBrowseEntries(self));
    lv_obj_set_user_data(list, entries);
    lv_obj_add_event_cb(list, [](lv_event_t* e) {
        delete static_cast<std::vector<BrowseEntry>*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    }, LV_EVENT_DELETE, nullptr);

    for (auto& entry : *entries) {
        std::string text = entry.isDir ? (LV_SYMBOL_DIRECTORY " " + entry.name) : entry.name;
        lv_obj_t* button = lv_list_add_button(list, nullptr, text.c_str());
        lv_obj_set_user_data(button, &entry);
        lv_obj_add_event_cb(button, &onBrowserEntryCb, LV_EVENT_CLICKED, self);
    }
}
