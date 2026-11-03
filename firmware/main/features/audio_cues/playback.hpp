#pragma once

#include "wav.hpp"

#include <cstddef>
#include <cstdint>

namespace mosaic::features::audio_cues {

// The speaker as playback sees it. Each call reports whether it succeeded; the
// implementation reports its own errors.
class Speaker {
public:
    // Turns the amplifier on for this format.
    virtual bool open(const Pcm& format) = 0;
    virtual bool set_volume(uint8_t percent) = 0;
    // May change the bytes in place.
    virtual bool write(uint8_t* bytes, size_t size) = 0;
    // Turns the amplifier off.
    virtual void close() = 0;

protected:
    ~Speaker() = default;
};

// Silence after the speaker opens, while the DAC and amplifier settle; without it
// the start of the first note is distorted.
constexpr uint32_t kLeadMs = 50;
// Enough silence to fill the I2S DMA buffers (about 70 ms at 22050 Hz) before the
// amplifier turns off. It plays out the cue's tail, and leaves the buffers silent
// so the next open does not replay stale samples.
constexpr uint32_t kTailMs = 80;
// Written in small pieces so muting stops a cue within about 10 ms.
constexpr size_t kChunkBytes = 512;

// Plays pcm at volume, checking audible() before every chunk of the cue so muting
// cuts it short. Once opened, the speaker is always closed, after the silent tail
// when the lead-in played.
void play(const Pcm& pcm, uint8_t volume, Speaker& speaker, bool (*audible)());

}
