#pragma once
#include "cues.hpp"
#include <cstdint>
#include <span>
namespace mosaic::features::audio_cues {
// The WAV file for each cue, as built into the image.
std::span<const uint8_t> sound_file(Cue cue);
}
