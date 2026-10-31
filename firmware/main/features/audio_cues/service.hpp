#pragma once
#include <cstdint>
#include "esp_err.h"
namespace mosaic::features::audio_cues {
// Plays cues for new notifications and status changes through the speaker, from its
// own task. Call once, after events::start() succeeds.
esp_err_t start();
// Any task. Applies from the next cue; 0 is silent. Values above 100 are clamped.
void set_volume(uint8_t percent);
uint8_t volume();
// Any task. Muting skips cues entirely and stops one that is playing, so the
// amplifier stays off.
void set_muted(bool muted);
bool muted();
}
