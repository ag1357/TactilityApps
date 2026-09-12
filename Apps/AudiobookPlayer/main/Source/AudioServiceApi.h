// SPDX-License-Identifier: Apache-2.0
#pragma once

// C ABI exported by the firmware's tactility-audio kernel module
// (Tactility/Source/service/audio/AudioExports.cpp in the Tactility repo).
// ABI-stable; declared here because the SDK does not ship a header for it.

extern "C" {

float tactility_audio_get_output_volume();
void tactility_audio_set_output_volume(float percent);
bool tactility_audio_is_output_muted();
void tactility_audio_set_output_muted(bool muted);
bool tactility_audio_consume_play_pause_request();
void tactility_audio_clear_play_pause_request();

}
