// SPDX-License-Identifier: Apache-2.0
// Sidecar persistence: per-track resume/chapter state, last-played pointer,
// library index, and the worker task that keeps it off the LVGL thread.

#include "AudiobookPlayer.h"
#include "FileUtil.h"

#include <cJSON.h>
#include <tactility/log.h>

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <string_view>

// Case-insensitive natural comparison so "Book 2" sorts before "Book 10".
bool natLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        bool aIsDigit = std::isdigit(static_cast<unsigned char>(a[i])) != 0;
        bool bIsDigit = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
        if (aIsDigit && bIsDigit) {
            // Compare numeric runs by value, not lexicographically.
            unsigned long long av = 0, bv = 0;
            while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) {
                av = av * 10 + static_cast<unsigned long long>(a[i++] - '0');
            }
            while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j]))) {
                bv = bv * 10 + static_cast<unsigned long long>(b[j++] - '0');
            }
            if (av != bv) return av < bv;
        } else {
            char ac = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
            char bc = static_cast<char>(std::tolower(static_cast<unsigned char>(b[j])));
            if (ac != bc) return ac < bc;
            i++;
            j++;
        }
    }
    return a.size() < b.size();
}

std::string formatDurationMs(int64_t ms) {
    if (ms < 0) ms = 0;
    int64_t s = ms / 1000;
    int64_t h = s / 3600;
    int64_t m = (s % 3600) / 60;
    int64_t sec = s % 60;
    char buf[32];
    if (h > 0) snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", (long long) h, (long long) m, (long long) sec);
    else snprintf(buf, sizeof(buf), "%lld:%02lld", (long long) m, (long long) sec);
    return buf;
}

bool hasMp3Extension(const std::string& name) {
    if (name.size() < 4) return false;
    auto ext = name.substr(name.size() - 4);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp3";
}

std::string dirnameOf(const std::string& path) {
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return path.substr(0, slash);
}

int64_t fileSizeOf(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_size);
}

// Sidecar filename: MP3 path with a leading "/sdcard/" stripped and '/'/' '
// flattened, so names stay short and stable across mounts.
std::string sidecarPathFor(const std::string& mp3Path, const std::string& sidecarDir) {
    std::string flat = mp3Path;
    constexpr std::string_view prefix = "/sdcard/";
    if (flat.rfind(prefix, 0) == 0) flat = flat.substr(prefix.size());
    for (auto& c : flat) {
        if (c == '/' || c == ' ') c = '_';
    }
    if (flat.size() > 4) flat = flat.substr(0, flat.size() - 4); // drop .mp3
    return sidecarDir + "/" + flat + ".taudio.json";
}

static std::string detectKindFromPath(const std::string& mp3Path) {
    std::string lower = mp3Path;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("/audiobooks/") != std::string::npos) return "audiobook";
    if (lower.find("/music/") != std::string::npos) return "music";
    return "audio";
}

static bool isSupportedKind(const std::string& kind) {
    return kind == "audiobook" || kind == "music" || kind == "audio";
}

void normalizeChapters(std::vector<Chapter>& chapters) {
    if (chapters.empty()) {
        chapters.push_back({.number = 1, .title = "Chapter 1", .start_ms = 0});
    }
    for (auto& chapter : chapters) {
        if (chapter.start_ms < 0) chapter.start_ms = 0;
    }
    std::sort(chapters.begin(), chapters.end(),
              [](const Chapter& a, const Chapter& b) { return a.start_ms < b.start_ms; });
    if (chapters.front().start_ms != 0) {
        chapters.insert(chapters.begin(), {.number = 1, .title = "Chapter 1", .start_ms = 0});
    }
    chapters.erase(std::unique(chapters.begin(), chapters.end(),
                               [](const Chapter& a, const Chapter& b) {
                                   return a.start_ms == b.start_ms;
                               }),
                   chapters.end());
    for (size_t i = 0; i < chapters.size(); ++i) {
        chapters[i].number = static_cast<int>(i) + 1;
        chapters[i].title = "Chapter " + std::to_string(i + 1);
    }
}

// Missing/corrupt sidecar yields defaults with kind derived from the path.
SidecarData loadSidecar(const std::string& mp3Path, const std::string& sidecarPath) {
    SidecarData data;
    auto slash = mp3Path.find_last_of('/');
    data.audio_file = (slash == std::string::npos) ? mp3Path : mp3Path.substr(slash + 1);
    if (data.audio_file.size() > 4 &&
        (data.audio_file.substr(data.audio_file.size() - 4) == ".mp3" ||
         data.audio_file.substr(data.audio_file.size() - 4) == ".MP3")) {
        data.title = data.audio_file.substr(0, data.audio_file.size() - 4);
    } else {
        data.title = data.audio_file;
    }
    data.kind = detectKindFromPath(mp3Path);
    data.tracking_enabled = (data.kind == "audiobook");
    data.chapters.push_back({.number = 1, .title = "Chapter 1", .start_ms = 0});

    if (sidecarPath.empty() || !fileIsFile(sidecarPath)) {
        normalizeChapters(data.chapters);
        return data;
    }

    std::string body;
    {
        FILE* fp = fopen(sidecarPath.c_str(), "rb");
        if (fp == nullptr) return data;
        char buf[512];
        size_t got;
        while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) body.append(buf, got);
        fclose(fp);
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        LOG_W(TAG, "Sidecar %s is not valid JSON; ignoring", sidecarPath.c_str());
        return data;
    }

    auto readString = [&](const char* key, std::string& out) {
        auto* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr && cJSON_IsString(item) && item->valuestring != nullptr) {
            out = item->valuestring;
        }
    };
    auto readNumber = [&](const char* key, int64_t& out) {
        auto* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr && cJSON_IsNumber(item)) {
            out = static_cast<int64_t>(item->valuedouble);
        }
    };
    auto readBool = [&](const char* key, bool& out) {
        auto* item = cJSON_GetObjectItem(root, key);
        if (item != nullptr && cJSON_IsBool(item)) {
            out = cJSON_IsTrue(item);
        }
    };

    readString("audio_file", data.audio_file);
    readString("kind", data.kind);
    readString("title", data.title);
    readString("author", data.author);
    readBool("tracking_enabled", data.tracking_enabled);
    readNumber("duration_ms", data.duration_ms);
    readNumber("last_position_ms", data.last_position_ms);
    readNumber("last_position_bytes", data.last_position_bytes);
    if (!isSupportedKind(data.kind)) data.kind = detectKindFromPath(mp3Path);
    if (data.duration_ms < 0) data.duration_ms = 0;
    if (data.last_position_ms < 0) data.last_position_ms = 0;
    if (data.last_position_bytes < 0) data.last_position_bytes = 0;

    auto* chapters = cJSON_GetObjectItem(root, "chapters");
    if (chapters != nullptr && cJSON_IsArray(chapters)) {
        std::vector<Chapter> parsed;
        int n = cJSON_GetArraySize(chapters);
        for (int i = 0; i < n; ++i) {
            auto* c = cJSON_GetArrayItem(chapters, i);
            if (c == nullptr) continue;
            Chapter ch;
            auto* num = cJSON_GetObjectItem(c, "number");
            auto* ti = cJSON_GetObjectItem(c, "title");
            auto* st = cJSON_GetObjectItem(c, "start_ms");
            if (num != nullptr && cJSON_IsNumber(num)) ch.number = num->valueint;
            if (ti != nullptr && cJSON_IsString(ti) && ti->valuestring != nullptr) ch.title = ti->valuestring;
            if (st != nullptr && cJSON_IsNumber(st)) ch.start_ms = static_cast<int64_t>(st->valuedouble);
            parsed.push_back(ch);
        }
        if (!parsed.empty()) {
            data.chapters = std::move(parsed);
        }
    }

    cJSON_Delete(root);
    normalizeChapters(data.chapters);
    return data;
}

bool saveSidecar(const std::string& sidecarPath, const SidecarData& data) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) return false;
    cJSON_AddStringToObject(root, "schema", "tactility-audio-v1");
    cJSON_AddStringToObject(root, "audio_file", data.audio_file.c_str());
    cJSON_AddStringToObject(root, "kind", data.kind.c_str());
    cJSON_AddStringToObject(root, "title", data.title.c_str());
    cJSON_AddStringToObject(root, "author", data.author.c_str());
    cJSON_AddBoolToObject(root, "tracking_enabled", data.tracking_enabled);
    cJSON_AddNumberToObject(root, "duration_ms", static_cast<double>(data.duration_ms));
    cJSON_AddNumberToObject(root, "last_position_ms", static_cast<double>(data.last_position_ms));
    cJSON_AddNumberToObject(root, "last_position_bytes", static_cast<double>(data.last_position_bytes));
    cJSON* chapters = cJSON_AddArrayToObject(root, "chapters");
    if (chapters != nullptr) {
        for (const auto& c : data.chapters) {
            cJSON* item = cJSON_CreateObject();
            if (item == nullptr) continue;
            cJSON_AddNumberToObject(item, "number", c.number);
            cJSON_AddStringToObject(item, "title", c.title.c_str());
            cJSON_AddNumberToObject(item, "start_ms", static_cast<double>(c.start_ms));
            cJSON_AddItemToArray(chapters, item);
        }
    }
    char* rendered = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (rendered == nullptr) return false;

    // Atomic replace (tmp + rename): a crash mid-write leaves the previous
    // checkpoint intact.
    std::string tmpPath = sidecarPath + ".tmp";
    bool ok = false;
    {
        FILE* fp = fopen(tmpPath.c_str(), "wb");
        if (fp != nullptr) {
            size_t len = strlen(rendered);
            ok = (fwrite(rendered, 1, len, fp) == len);
            if (fclose(fp) != 0) ok = false;
        }
    }
    if (ok && ::rename(tmpPath.c_str(), sidecarPath.c_str()) != 0) {
        // Filesystems that refuse rename-over-existing: drop the target and
        // retry, degrading to truncate-write as the worst case.
        ::unlink(sidecarPath.c_str());
        ok = (::rename(tmpPath.c_str(), sidecarPath.c_str()) == 0);
    }
    if (!ok) {
        ::unlink(tmpPath.c_str());
    }
    free(rendered);
    if (!ok) LOG_E(TAG, "saveSidecar: write failed for %s", sidecarPath.c_str());
    return ok;
}

// ---- MP3 duration estimation (no decode) -----------------------------------

static int64_t id3v2TagSize(const uint8_t* header, size_t size) {
    if (size < 10 || header[0] != 'I' || header[1] != 'D' || header[2] != '3') return 0;
    int64_t tagSize =
        ((header[6] & 0x7f) << 21) |
        ((header[7] & 0x7f) << 14) |
        ((header[8] & 0x7f) << 7) |
        (header[9] & 0x7f);
    if ((header[5] & 0x10) != 0) tagSize += 10; // footer present
    return tagSize + 10;
}

static int mp3BitrateKbpsFromHeader(uint32_t header) {
    if ((header & 0xffe00000u) != 0xffe00000u) return 0;
    int version = (header >> 19) & 0x3; // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
    int layer = (header >> 17) & 0x3;   // 1=Layer III, 2=Layer II, 3=Layer I
    int bitrateIndex = (header >> 12) & 0xf;
    int sampleIndex = (header >> 10) & 0x3;
    if (version == 1 || layer == 0 || bitrateIndex == 0 || bitrateIndex == 0xf || sampleIndex == 0x3) {
        return 0;
    }

    static constexpr int mpeg1Layer1[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0};
    static constexpr int mpeg1Layer2[16] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
    static constexpr int mpeg1Layer3[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static constexpr int mpeg2Layer1[16] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0};
    static constexpr int mpeg2Layer23[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};

    if (version == 3) {
        if (layer == 3) return mpeg1Layer1[bitrateIndex];
        if (layer == 2) return mpeg1Layer2[bitrateIndex];
        return mpeg1Layer3[bitrateIndex];
    }
    if (layer == 3) return mpeg2Layer1[bitrateIndex];
    return mpeg2Layer23[bitrateIndex];
}

static int64_t estimateMp3DurationMs(const std::string& path, int64_t fileSize) {
    if (fileSize <= 0) return 0;

    int64_t audioStart = 0;
    uint8_t scan[2048];
    size_t got = 0;
    {
        FILE* fp = fopen(path.c_str(), "rb");
        if (fp == nullptr) return (fileSize * 8) / 128; // conservative 128 kbps fallback
        got = fread(scan, 1, sizeof(scan), fp);
        audioStart = id3v2TagSize(scan, got);
        if (audioStart > 0 && audioStart < fileSize) {
            fseek(fp, static_cast<long>(audioStart), SEEK_SET);
            got = fread(scan, 1, sizeof(scan), fp);
        }
        fclose(fp);
    }

    int bitrateKbps = 0;
    for (size_t i = 0; i + 3 < got; ++i) {
        uint32_t header =
            (static_cast<uint32_t>(scan[i]) << 24) |
            (static_cast<uint32_t>(scan[i + 1]) << 16) |
            (static_cast<uint32_t>(scan[i + 2]) << 8) |
            static_cast<uint32_t>(scan[i + 3]);
        bitrateKbps = mp3BitrateKbpsFromHeader(header);
        if (bitrateKbps > 0) {
            audioStart += static_cast<int64_t>(i);
            break;
        }
    }
    if (bitrateKbps <= 0) bitrateKbps = 128;
    int64_t audioBytes = std::max<int64_t>(1, fileSize - audioStart);
    return (audioBytes * 8) / bitrateKbps;
}

// ---- Folder / library scans -------------------------------------------------

std::vector<std::string> scanFolderForMp3(const std::string& folder) {
    std::vector<std::string> out;
    int seen = 0;
    DIR* dir = opendir(folder.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            seen++;
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            // Quirk: some FAT VFS backends report DT_UNKNOWN; trust the
            // extension check instead of d_type.
            if (hasMp3Extension(name)) {
                out.push_back(fileChildPath(folder, name));
            }
        }
        closedir(dir);
    }
    LOG_I(TAG, "scanFolderForMp3: saw %d entries, %zu are MP3", seen, out.size());
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return natLess(fileLastPathSegment(a), fileLastPathSegment(b));
    });
    return out;
}

static std::string detectKindForIndex(const std::string& path, int64_t sizeBytes) {
    std::string kind = detectKindFromPath(path);
    if (kind != "audio") return kind;
    // Duration is unknown without decoding; file size is a cheap proxy. Long
    // single-file books are commonly tens of MB.
    constexpr int64_t audiobookSizeFloor = 32LL * 1024LL * 1024LL;
    return sizeBytes >= audiobookSizeFloor ? "audiobook" : "audio";
}

static std::vector<LibraryIndexEntry> scanLibraryForMp3(const std::string& root) {
    std::vector<LibraryIndexEntry> entries;
    std::vector<std::string> dirs;
    dirs.push_back(root);

    for (size_t dirIndex = 0; dirIndex < dirs.size(); ++dirIndex) {
        if (dirIndex >= MAX_INDEX_DIRS || entries.size() >= MAX_INDEX_TRACKS) break;
        const std::string folder = dirs[dirIndex];
        DIR* dir = opendir(folder.c_str());
        if (dir == nullptr) continue;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entries.size() >= MAX_INDEX_TRACKS || dirs.size() >= MAX_INDEX_DIRS) break;
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            std::string path = fileChildPath(folder, name);
            bool isDir = entry->d_type == DT_DIR || entry->d_type == DT_CHR;
            if (!isDir && !hasMp3Extension(name)) {
                isDir = fileIsDirectory(path);
            }
            if (isDir) {
                dirs.push_back(std::move(path));
            } else if (hasMp3Extension(name)) {
                LibraryIndexEntry indexed;
                indexed.path = path;
                indexed.title = fileLastPathSegment(path);
                indexed.size_bytes = fileSizeOf(path);
                indexed.duration_ms = estimateMp3DurationMs(path, indexed.size_bytes);
                indexed.kind = detectKindForIndex(path, indexed.size_bytes);
                entries.push_back(std::move(indexed));
            }
        }
        closedir(dir);
    }

    std::sort(entries.begin(), entries.end(), [](const LibraryIndexEntry& a, const LibraryIndexEntry& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        std::string aFolder = dirnameOf(a.path);
        std::string bFolder = dirnameOf(b.path);
        if (aFolder != bFolder) return natLess(aFolder, bFolder);
        return natLess(a.title, b.title);
    });
    return entries;
}

bool saveLibraryIndex(const std::string& indexPath,
                      const std::string& root,
                      const std::vector<LibraryIndexEntry>& entries) {
    if (indexPath.empty()) return false;
    cJSON* doc = cJSON_CreateObject();
    if (doc == nullptr) return false;
    cJSON_AddStringToObject(doc, "schema", "tactility-audio-index-v1");
    cJSON_AddStringToObject(doc, "root", root.c_str());
    cJSON_AddNumberToObject(doc, "count", static_cast<double>(entries.size()));
    cJSON* tracks = cJSON_AddArrayToObject(doc, "tracks");
    for (const auto& entry : entries) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "path", entry.path.c_str());
        cJSON_AddStringToObject(item, "kind", entry.kind.c_str());
        cJSON_AddStringToObject(item, "title", entry.title.c_str());
        cJSON_AddNumberToObject(item, "size_bytes", static_cast<double>(entry.size_bytes));
        cJSON_AddNumberToObject(item, "duration_ms", static_cast<double>(entry.duration_ms));
        cJSON_AddItemToArray(tracks, item);
    }
    char* rendered = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (rendered == nullptr) return false;

    bool ok = false;
    {
        FILE* fp = fopen(indexPath.c_str(), "wb");
        if (fp != nullptr) {
            size_t len = strlen(rendered);
            ok = fwrite(rendered, 1, len, fp) == len;
            fclose(fp);
        }
    }
    free(rendered);
    if (!ok) LOG_E(TAG, "saveLibraryIndex: write failed for %s", indexPath.c_str());
    return ok;
}

// ---- Worker task -------------------------------------------------------------

static void workerEntry(void* param) {
    auto* self = static_cast<SidecarWorker*>(param);
    while (true) {
        SidecarJob* job = nullptr;
        if (xQueueReceive(self->queue, &job, portMAX_DELAY) != pdTRUE || job == nullptr) {
            continue;
        }
        switch (job->type) {
            case SidecarJob::Type::InitStorage: {
                fileFindOrCreateDirectory(self->appDataDir, 0775);
                if (!fileFindOrCreateDirectory(self->dir, 0775)) {
                    LOG_E(TAG, "Failed to create resume-tracking dir %s", self->dir.c_str());
                }
                std::string loaded;
                if (fileIsFile(self->lastPlayedPath)) {
                    FILE* fp = fopen(self->lastPlayedPath.c_str(), "r");
                    if (fp != nullptr) {
                        char buf[512];
                        if (fgets(buf, sizeof(buf), fp) != nullptr) {
                            loaded = buf;
                            while (!loaded.empty() && (loaded.back() == '\n' || loaded.back() == '\r')) {
                                loaded.pop_back();
                            }
                        }
                        fclose(fp);
                    }
                }
                if (!loaded.empty()) {
                    std::lock_guard lg(self->lastPlayedMutex);
                    self->lastPlayedReadyPath = std::move(loaded);
                    self->lastPlayedReady.store(true);
                }
                break;
            }
            case SidecarJob::Type::Load: {
                SidecarData data = loadSidecar(job->mp3Path, sidecarPathFor(job->mp3Path, self->dir));
                if (data.duration_ms <= 0) {
                    data.duration_ms = estimateMp3DurationMs(job->mp3Path, fileSizeOf(job->mp3Path));
                }
                {
                    std::lock_guard lg(self->loadedMutex);
                    self->loadedData = std::move(data);
                    self->loadedMp3Path = job->mp3Path;
                }
                self->loadedReady.store(true);
                break;
            }
            case SidecarJob::Type::Save:
                saveSidecar(job->sidecarPath, job->data);
                break;
            case SidecarJob::Type::SaveLastPlayed: {
                if (self->lastPlayedPath.empty()) break;
                FILE* fp = fopen(self->lastPlayedPath.c_str(), "w");
                if (fp != nullptr) {
                    fputs(job->mp3Path.c_str(), fp);
                    fclose(fp);
                }
                break;
            }
            case SidecarJob::Type::AdoptPlaylist: {
                // Folder listing is SD I/O: worker thread only.
                std::string picked = job->mp3Path;
                auto files = scanFolderForMp3(dirnameOf(picked));
                int idx = -1;
                for (size_t i = 0; i < files.size(); ++i) {
                    if (files[i] == picked) {
                        idx = static_cast<int>(i);
                        break;
                    }
                }
                if (idx < 0 && !files.empty()) idx = 0;
                {
                    std::lock_guard lg(self->playlistReadyMutex);
                    self->playlistReadyFiles = std::move(files);
                    self->playlistReadyIndex = idx;
                    self->playlistReadyPicked = std::move(picked);
                }
                self->playlistReady.store(true);
                break;
            }
            case SidecarJob::Type::ScanIndex: {
                std::string root = job->mp3Path;
                auto entries = scanLibraryForMp3(root);
                bool saved = saveLibraryIndex(self->libraryIndexPath, root, entries);
                size_t count = entries.size();
                {
                    std::lock_guard lg(self->scanMutex);
                    self->scanRoot = std::move(root);
                    self->scanCount = count;
                    self->scanSaved = saved;
                }
                self->scanReady.store(true);
                break;
            }
            case SidecarJob::Type::Shutdown:
                delete job;
                self->task = nullptr;
                vTaskDelete(nullptr);
                return;
        }
        delete job;
    }
}

void SidecarWorker::start() {
    queue = xQueueCreate(8, sizeof(SidecarJob*));
    if (queue == nullptr) {
        LOG_E(TAG, "Failed to create sidecar queue");
        return;
    }
    BaseType_t ok = xTaskCreate(&workerEntry, "audio_sc", 6 * 1024,
                                this, tskIDLE_PRIORITY + 1, &task);
    if (ok != pdPASS) {
        LOG_E(TAG, "Failed to create sidecar worker task");
        task = nullptr;
    }
}

bool SidecarWorker::enqueue(SidecarJob* job, TickType_t timeout) {
    if (queue == nullptr) {
        delete job;
        return false;
    }
    if (xQueueSend(queue, &job, timeout) != pdTRUE) {
        LOG_W(TAG, "Sidecar queue full; dropping job type=%d", (int) job->type);
        delete job;
        return false;
    }
    return true;
}

void SidecarWorker::requestInitStorage() {
    enqueue(new SidecarJob{.type = SidecarJob::Type::InitStorage}, pdMS_TO_TICKS(100));
}

void SidecarWorker::requestLoad(const std::string& mp3Path) {
    enqueue(new SidecarJob{.type = SidecarJob::Type::Load, .mp3Path = mp3Path}, pdMS_TO_TICKS(50));
}

void SidecarWorker::requestSave(const std::string& sidecarPath, const SidecarData& data) {
    enqueue(new SidecarJob{.type = SidecarJob::Type::Save, .sidecarPath = sidecarPath, .data = data});
}

void SidecarWorker::requestAdoptPlaylist(const std::string& pickedPath) {
    enqueue(new SidecarJob{.type = SidecarJob::Type::AdoptPlaylist, .mp3Path = pickedPath}, pdMS_TO_TICKS(50));
}

void SidecarWorker::requestSaveLastPlayed(const std::string& path) {
    enqueue(new SidecarJob{.type = SidecarJob::Type::SaveLastPlayed, .mp3Path = path}, pdMS_TO_TICKS(50));
}

void SidecarWorker::requestScanIndex(const std::string& rootPath) {
    enqueue(new SidecarJob{.type = SidecarJob::Type::ScanIndex, .mp3Path = rootPath});
}

void SidecarWorker::stop() {
    if (task == nullptr) return;
    auto* job = new SidecarJob{.type = SidecarJob::Type::Shutdown};
    // Block briefly so shutdown is reliably delivered.
    if (queue != nullptr && xQueueSend(queue, &job, pdMS_TO_TICKS(500)) != pdTRUE) {
        delete job;
    }
    for (int i = 0; i < 80 && task != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (task != nullptr) {
        LOG_W(TAG, "Sidecar worker did not stop before timeout");
        return;
    }
    if (queue != nullptr) {
        vQueueDelete(queue);
        queue = nullptr;
    }
}
