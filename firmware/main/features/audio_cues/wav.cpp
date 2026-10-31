#include "wav.hpp"

#include <string_view>

namespace mosaic::features::audio_cues {
namespace {
    constexpr uint16_t kPcmFormat = 1;
    constexpr size_t kChunkHeader = 8;

    uint16_t u16(std::span<const uint8_t> bytes, size_t at) {
        return static_cast<uint16_t>(bytes[at] | bytes[at + 1] << 8);
    }
    uint32_t u32(std::span<const uint8_t> bytes, size_t at) {
        return static_cast<uint32_t>(u16(bytes, at)) | static_cast<uint32_t>(u16(bytes, at + 2)) << 16;
    }
    bool tag(std::span<const uint8_t> bytes, size_t at, std::string_view expected) {
        return std::string_view(reinterpret_cast<const char*>(bytes.data() + at), 4) == expected;
    }
} // namespace

bool parse_wav(std::span<const uint8_t> file, Pcm& pcm) {
    if (file.size() < 12 || !tag(file, 0, "RIFF") || !tag(file, 8, "WAVE"))
        return false;
    Pcm found;
    bool have_format = false;
    for (size_t at = 12; file.size() - at >= kChunkHeader;) {
        const size_t size = u32(file, at + 4);
        const size_t body = at + kChunkHeader;
        if (size > file.size() - body)
            return false;
        if (tag(file, at, "fmt ")) {
            if (size < 16 || u16(file, body) != kPcmFormat)
                return false;
            found.channels = u16(file, body + 2);
            found.sample_rate = u32(file, body + 4);
            found.bits_per_sample = u16(file, body + 14);
            have_format = true;
        } else if (tag(file, at, "data")) {
            const size_t frame = static_cast<size_t>(found.channels) * (found.bits_per_sample / 8);
            if (!have_format || found.channels == 0 || found.sample_rate == 0 || found.bits_per_sample % 8 != 0
                || frame == 0 || size % frame != 0)
                return false;
            found.samples = file.subspan(body, size);
            pcm = found;
            return true;
        }
        // Chunks are padded to an even length.
        at = body + size + (size & 1);
        if (at > file.size())
            return false;
    }
    return false;
}

}
