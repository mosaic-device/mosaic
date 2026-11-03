#include "playback.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace mosaic::features::audio_cues {
namespace {
    // Writes through a scratch copy, since the speaker may scale samples in place; null
    // bytes write silence. Stops early when no longer audible, unless writing silence.
    bool write(Speaker& speaker, const uint8_t* bytes, size_t size, bool (*audible)()) {
        std::array<uint8_t, kChunkBytes> chunk;
        for (size_t at = 0; at < size; at += chunk.size()) {
            if (bytes && !audible())
                return false;
            const size_t count = std::min(chunk.size(), size - at);
            if (bytes)
                std::memcpy(chunk.data(), bytes + at, count);
            else
                chunk.fill(0);
            if (!speaker.write(chunk.data(), count))
                return false;
        }
        return true;
    }
}

void play(const Pcm& pcm, uint8_t volume, Speaker& speaker, bool (*audible)()) {
    if (!audible() || !speaker.open(pcm))
        return;
    const size_t frame = static_cast<size_t>(pcm.channels) * pcm.bits_per_sample / 8;
    const auto silence
        = [&](uint32_t ms) { return write(speaker, nullptr, pcm.sample_rate * ms / 1000 * frame, audible); };
    if (speaker.set_volume(volume) && silence(kLeadMs)) {
        // Muting may cut the cue short; the silent tail still flushes the buffers.
        write(speaker, pcm.samples.data(), pcm.samples.size(), audible);
        silence(kTailMs);
    }
    speaker.close();
}

}
