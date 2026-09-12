// SPDX-License-Identifier: Apache-2.0
// Decode/write loop and playlist runner on a dedicated task.

#include "AudiobookPlayer.h"

#include <esp_audio_simple_dec.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_stream.h>
#include <tactility/log.h>

#include <cstring>
#include <vector>

#ifdef ESP_PLATFORM

static Device* findAudioStreamDevice() {
    Device* result = nullptr;
    device_for_each_of_type(&AUDIO_STREAM_TYPE, &result, [](Device* device, void* ctx) -> bool {
        if (device_is_ready(device)) {
            *static_cast<Device**>(ctx) = device;
            return false;
        }
        return true;
    });
    return result;
}

#endif

void publishNowPlaying(PlaybackState* state, const std::string& path) {
    {
        std::lock_guard lg(state->nowPlayingMutex);
        state->nowPlayingPath = path;
    }
    state->nowPlayingDirty.store(true);
}

// Play one MP3 end-to-end, returning why the run finished. startBytes > 0
// seeks first: MP3 decoders resync at the next valid frame.
static PlaybackReason runFileOnce(PlaybackState* state, const std::string& path,
                                  int64_t startBytes, int64_t startMs) {
#ifndef ESP_PLATFORM
    // No decoder hardware: idle so the UI can be exercised.
    LOG_W(TAG, "Simulator build: not actually playing %s", path.c_str());
    state->playing.store(true);
    publishNowPlaying(state, path);
    while (!state->shutdown.load() && !state->stopRequested.load() &&
           !state->skipNext.load() && !state->skipPrev.load() &&
           state->seekRequestMs.load() < 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    state->playing.store(false);
    if (state->shutdown.load()) return PlaybackReason::Shutdown;
    if (state->skipNext.exchange(false)) return PlaybackReason::SkippedNext;
    if (state->skipPrev.exchange(false)) return PlaybackReason::SkippedPrev;
    if (state->seekRequestMs.exchange(-1) >= 0) return PlaybackReason::Seeked;
    return PlaybackReason::Stopped;
#else
    Device* streamDevice = findAudioStreamDevice();
    if (streamDevice == nullptr) {
        LOG_E(TAG, "No audio-stream device found");
        return PlaybackReason::OpenFailed;
    }

    if (!hasMp3Extension(path)) {
        LOG_E(TAG, "Unsupported audio format (mp3 only): %s", path.c_str());
        return PlaybackReason::OpenFailed;
    }

    FILE* fp = nullptr;
    int64_t fileSize = 0;
    {
        fp = fopen(path.c_str(), "rb");
        if (fp != nullptr) {
            if (fseek(fp, 0, SEEK_END) == 0) {
                long sz = ftell(fp);
                if (sz >= 0) fileSize = sz;
            }
            fseek(fp, 0, SEEK_SET);
            if (startBytes > 0 && startBytes < fileSize) {
                fseek(fp, (long) startBytes, SEEK_SET);
            } else {
                startBytes = 0;
                startMs = 0;
            }
        }
    }
    if (fp == nullptr) {
        LOG_E(TAG, "Failed to open file: %s", path.c_str());
        return PlaybackReason::OpenFailed;
    }
    state->currentFileSize.store(fileSize);
    state->currentFileBytes.store(startBytes);
    state->currentPositionMs.store(startMs);

    esp_audio_simple_dec_cfg_t decCfg = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg = nullptr,
        .cfg_size = 0,
        .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t dec = nullptr;
    esp_audio_err_t derr = esp_audio_simple_dec_open(&decCfg, &dec);
    if (derr != ESP_AUDIO_ERR_OK) {
        LOG_E(TAG, "esp_audio_simple_dec_open failed: %d", (int) derr);
        fclose(fp);
        return PlaybackReason::OpenFailed;
    }

    constexpr size_t inChunkSize = 4096;
    std::vector<uint8_t> inBuf(inChunkSize);
    std::vector<uint8_t> outBuf(1152 * 2 * sizeof(int16_t) * 2);

    AudioStreamHandle handle = nullptr;
    AudioStreamConfig openedCfg = {};
    uint32_t bytesPerPcmSec = 0;
    bool eos = false;
    size_t inFill = 0;
    size_t inOffset = 0;
    size_t totalPcmBytes = 0;
    int64_t fileOffset = startBytes;
    PlaybackReason reason = PlaybackReason::ReachedEnd;

    state->playing.store(true);
    publishNowPlaying(state, path);
    LOG_I(TAG, "Play start: %s (startBytes=%lld startMs=%lld size=%lld)",
          path.c_str(), (long long) startBytes, (long long) startMs, (long long) fileSize);

    while (true) {
        if (state->shutdown.load()) { reason = PlaybackReason::Shutdown; break; }
        if (state->stopRequested.load()) { reason = PlaybackReason::Stopped; break; }
        if (state->skipNext.exchange(false)) { reason = PlaybackReason::SkippedNext; break; }
        if (state->skipPrev.exchange(false)) { reason = PlaybackReason::SkippedPrev; break; }
        if (state->seekRequestMs.load() >= 0) { reason = PlaybackReason::Seeked; break; }

        // Pause quirk: hold position without writing; holding the output
        // stream open would repeat the final DMA buffer on this hardware.
        while (state->paused.load() &&
               !state->shutdown.load() && !state->stopRequested.load() &&
               !state->skipNext.load() && !state->skipPrev.load() &&
               state->seekRequestMs.load() < 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        if (!eos && inFill - inOffset < inChunkSize / 2) {
            size_t remaining = inFill - inOffset;
            if (inOffset > 0 && remaining > 0) {
                memmove(inBuf.data(), inBuf.data() + inOffset, remaining);
            }
            inOffset = 0;
            inFill = remaining;

            size_t want = inChunkSize - inFill;
            size_t got = fread(inBuf.data() + inFill, 1, want, fp);
            inFill += got;
            fileOffset += static_cast<int64_t>(got);
            state->currentFileBytes.store(fileOffset);
            if (got == 0) {
                eos = true;
            }
        }

        if (eos && inOffset >= inFill) {
            break; // natural end of file, reason stays ReachedEnd
        }

        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = inBuf.data() + inOffset;
        raw.len = static_cast<uint32_t>(inFill - inOffset);
        raw.eos = eos;
        raw.consumed = 0;

        esp_audio_simple_dec_out_t out = {};
        out.buffer = outBuf.data();
        out.len = static_cast<uint32_t>(outBuf.size());

        derr = esp_audio_simple_dec_process(dec, &raw, &out);
        if (derr == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            uint32_t needed = out.needed_size > 0 ? out.needed_size : (uint32_t) outBuf.size() * 2;
            outBuf.resize(needed);
            continue;
        }
        if (derr != ESP_AUDIO_ERR_OK) {
            LOG_E(TAG, "esp_audio_simple_dec_process failed: %d", (int) derr);
            break;
        }
        inOffset += raw.consumed;

        if (out.decoded_size > 0) {
            if (handle == nullptr) {
                esp_audio_simple_dec_info_t info = {};
                if (esp_audio_simple_dec_get_info(dec, &info) != ESP_AUDIO_ERR_OK) {
                    LOG_E(TAG, "Decoder info not ready after first decoded frame");
                    break;
                }
                openedCfg.sample_rate = info.sample_rate;
                openedCfg.bits_per_sample = info.bits_per_sample ? info.bits_per_sample : 16;
                openedCfg.channels = info.channel;
                bytesPerPcmSec = openedCfg.sample_rate * openedCfg.channels * (openedCfg.bits_per_sample / 8);
                LOG_I(TAG, "Opening output: %u Hz, %u ch, %u bit",
                      (unsigned) openedCfg.sample_rate, (unsigned) openedCfg.channels, (unsigned) openedCfg.bits_per_sample);
                error_t oerr = audio_stream_open_output(streamDevice, &openedCfg, &handle);
                if (oerr != ERROR_NONE) {
                    LOG_E(TAG, "audio_stream_open_output failed: %d", oerr);
                    handle = nullptr;
                    break;
                }
            }

            size_t bytesWritten = 0;
            error_t werr;
            do {
                werr = audio_stream_write(handle, out.buffer, out.decoded_size, &bytesWritten, pdMS_TO_TICKS(250));
            } while (werr == ERROR_TIMEOUT &&
                     !state->stopRequested.load() &&
                     !state->shutdown.load() &&
                     !state->skipNext.load() &&
                     !state->skipPrev.load() &&
                     state->seekRequestMs.load() < 0);
            if (werr != ERROR_NONE) {
                // A stalled or rejected write (e.g. the codec rebinds when a
                // headset is attached) must not read as end-of-file: autoplay
                // would skip the track. Attribute the exit to whichever
                // control requested it, else a genuine output error.
                if (state->shutdown.load()) {
                    reason = PlaybackReason::Shutdown;
                } else if (state->stopRequested.load()) {
                    reason = PlaybackReason::Stopped;
                } else if (state->skipNext.exchange(false)) {
                    reason = PlaybackReason::SkippedNext;
                } else if (state->skipPrev.exchange(false)) {
                    reason = PlaybackReason::SkippedPrev;
                } else if (state->seekRequestMs.load() >= 0) {
                    reason = PlaybackReason::Seeked;
                } else {
                    if (werr != ERROR_TIMEOUT) {
                        LOG_E(TAG, "audio_stream_write failed: %d", werr);
                    }
                    reason = PlaybackReason::WriteFailed;
                }
                break;
            }
            totalPcmBytes += bytesWritten;

            if (bytesPerPcmSec > 0) {
                int64_t sessionMs = (int64_t) totalPcmBytes * 1000 / (int64_t) bytesPerPcmSec;
                int64_t positionMs = startMs + sessionMs;
                state->currentPositionMs.store(positionMs);
                // Learn bytes-per-ms of the input MP3 (fixed-point *1000) so
                // ms seeks translate to exact byte offsets.
                if (positionMs > 500 && fileOffset > startBytes) {
                    int64_t bpMsX1000 = ((fileOffset - startBytes) * 1000) / (positionMs - startMs);
                    state->bytesPerMsX1000.store(bpMsX1000);
                }
            }
        }
    }

    if (handle != nullptr) {
        audio_stream_close(handle);
    }
    esp_audio_simple_dec_close(dec);
    fclose(fp);

    state->playing.store(false);
    LOG_I(TAG, "Play end (%zu PCM bytes, reason=%d)", totalPcmBytes, (int) reason);
    return reason;
#endif // ESP_PLATFORM
}

// First-run startBytes/startMs come from nextStart* (set by the UI before
// requesting PlayCurrent, e.g. to resume an audiobook).
static void runPlaylist(PlaybackState* state) {
    int64_t startBytes = state->nextStartBytes.exchange(0);
    int64_t startMs = state->nextStartMs.exchange(0);

    while (true) {
        if (state->shutdown.load()) break;
        if (state->stopRequested.load()) {
            state->stopRequested.store(false);
            break;
        }

        std::string path;
        {
            std::lock_guard lg(state->playlistMutex);
            int idx = state->playlistIndex.load();
            if (idx < 0 || idx >= static_cast<int>(state->playlist.size())) {
                break; // playlist exhausted
            }
            path = state->playlist[idx];
        }

        PlaybackReason reason = runFileOnce(state, path, startBytes, startMs);
        startBytes = 0;
        startMs = 0;
        if (reason != PlaybackReason::Stopped &&
            reason != PlaybackReason::WriteFailed) {
            state->paused.store(false);
        }

        int nextIdx = state->playlistIndex.load();
        int size = 0;
        {
            std::lock_guard lg(state->playlistMutex);
            size = static_cast<int>(state->playlist.size());
        }

        switch (reason) {
            case PlaybackReason::Shutdown:
                return;
            case PlaybackReason::Stopped:
                state->stopRequested.store(false);
                if (!state->paused.load()) {
                    publishNowPlaying(state, "");
                }
                return;
            case PlaybackReason::SkippedNext:
                nextIdx = (nextIdx + 1 < size) ? nextIdx + 1 : -1;
                break;
            case PlaybackReason::SkippedPrev:
                nextIdx = (nextIdx > 0) ? nextIdx - 1 : 0;
                break;
            case PlaybackReason::ReachedEnd:
            case PlaybackReason::OpenFailed:
                // Skip past a broken file in autoplay mode; otherwise stop.
                if (state->autoplay.load() && nextIdx + 1 < size) {
                    nextIdx += 1;
                } else {
                    publishNowPlaying(state, "");
                    return;
                }
                break;
            case PlaybackReason::WriteFailed:
                // Park paused on the same track at the interruption point;
                // Play resumes there once the output is available again.
                state->stopRequested.store(false);
                state->paused.store(true);
                state->pendingErrorSave.store(true);
                return;
            case PlaybackReason::Seeked: {
                // Restart the SAME track: translate ms to bytes via the
                // learned bytes-per-ms.
                int64_t targetMs = state->seekRequestMs.exchange(-1);
                if (targetMs < 0) targetMs = 0;
                int64_t bpMsX1000 = state->bytesPerMsX1000.load();
                int64_t fileSize = state->currentFileSize.load();
                int64_t targetBytes = 0;
                if (bpMsX1000 > 0) {
                    targetBytes = (targetMs * bpMsX1000) / 1000;
                } else if (fileSize > 0 && state->currentPositionMs.load() > 0) {
                    targetBytes = (fileSize * targetMs) / std::max<int64_t>(1, state->currentPositionMs.load());
                }
                if (fileSize > 0 && targetBytes >= fileSize) targetBytes = fileSize - 1024;
                if (targetBytes < 0) targetBytes = 0;
                startBytes = targetBytes;
                startMs = targetMs;
                break;
            }
        }

        state->playlistIndex.store(nextIdx);
        if (nextIdx < 0) {
            publishNowPlaying(state, "");
            return;
        }
    }
}

void playbackTaskEntry(void* param) {
    auto* state = static_cast<PlaybackState*>(param);

    while (!state->shutdown.load()) {
        auto request = state->pending.exchange(PlaybackRequest::None);

        switch (request) {
            case PlaybackRequest::PlayCurrent:
                state->stopRequested.store(false);
                state->skipNext.store(false);
                state->skipPrev.store(false);
                runPlaylist(state);
                break;
            case PlaybackRequest::Stop:
            case PlaybackRequest::None:
            default:
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
        }
    }

    state->task = nullptr;
    vTaskDelete(nullptr);
}
