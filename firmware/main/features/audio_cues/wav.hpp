#pragma once

#include <cstdint>
#include <span>

namespace mosaic::features::audio_cues {

// Uncompressed PCM samples inside a WAV file.
struct Pcm {
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    std::span<const uint8_t> samples;
};

// Finds the PCM format and samples in a RIFF WAVE file, skipping other chunks. False
// for anything else: another encoding, a missing chunk, or a chunk running past the end.
bool parse_wav(std::span<const uint8_t> file, Pcm& pcm);

}
