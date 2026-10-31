#include "../cues.hpp"
#include "../wav.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <string_view>
#include <vector>

using namespace mosaic::features::audio_cues;
using mosaic::events::Status;

namespace {
using Bytes = std::vector<uint8_t>;

void put16(Bytes& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}
void put32(Bytes& bytes, uint32_t value) {
    put16(bytes, static_cast<uint16_t>(value));
    put16(bytes, static_cast<uint16_t>(value >> 16));
}
void tag(Bytes& bytes, std::string_view text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
}
void format_chunk(Bytes& bytes, uint16_t encoding = 1, uint16_t channels = 1, uint16_t bits = 16) {
    tag(bytes, "fmt ");
    put32(bytes, 16);
    put16(bytes, encoding);
    put16(bytes, channels);
    put32(bytes, 22050);
    put32(bytes, 22050 * channels * bits / 8);
    put16(bytes, static_cast<uint16_t>(channels * bits / 8));
    put16(bytes, bits);
}
void data_chunk(Bytes& bytes, const Bytes& samples) {
    tag(bytes, "data");
    put32(bytes, static_cast<uint32_t>(samples.size()));
    bytes.insert(bytes.end(), samples.begin(), samples.end());
}
Bytes riff(const Bytes& chunks) {
    Bytes bytes;
    tag(bytes, "RIFF");
    put32(bytes, static_cast<uint32_t>(4 + chunks.size()));
    tag(bytes, "WAVE");
    bytes.insert(bytes.end(), chunks.begin(), chunks.end());
    return bytes;
}
const Bytes kSamples { 1, 0, 2, 0, 3, 0 };

void parses_a_plain_wav() {
    Bytes chunks;
    format_chunk(chunks);
    data_chunk(chunks, kSamples);
    const Bytes file = riff(chunks);
    Pcm pcm;
    assert(parse_wav(file, pcm));
    assert(pcm.sample_rate == 22050 && pcm.channels == 1 && pcm.bits_per_sample == 16);
    assert(pcm.samples.size() == kSamples.size() && pcm.samples.data() == file.data() + 44);
}

void skips_other_chunks_with_padding() {
    Bytes chunks;
    format_chunk(chunks);
    tag(chunks, "LIST");
    put32(chunks, 3); // Odd length: one pad byte follows.
    chunks.insert(chunks.end(), { 'a', 'b', 'c', 0 });
    data_chunk(chunks, kSamples);
    const Bytes file = riff(chunks); // Samples point into it.
    Pcm pcm;
    assert(parse_wav(file, pcm));
    assert(pcm.samples.size() == kSamples.size() && pcm.samples[0] == 1);
}

void rejects_anything_else() {
    Pcm untouched;
    untouched.sample_rate = 7;
    const auto rejected = [&](const Bytes& file) {
        Pcm pcm = untouched;
        return !parse_wav(file, pcm) && pcm.sample_rate == 7;
    };
    assert(rejected({ }));
    assert(rejected({ 'R', 'I', 'F', 'F' }));
    {
        Bytes chunks;
        format_chunk(chunks);
        data_chunk(chunks, kSamples);
        Bytes file = riff(chunks);
        file[8] = 'X'; // Not WAVE.
        assert(rejected(file));
    }
    {
        Bytes chunks; // No format chunk before the samples.
        data_chunk(chunks, kSamples);
        assert(rejected(riff(chunks)));
    }
    {
        Bytes chunks; // Compressed, not PCM.
        format_chunk(chunks, 2);
        data_chunk(chunks, kSamples);
        assert(rejected(riff(chunks)));
    }
    {
        Bytes chunks; // Stereo 16-bit frames are 4 bytes; 6 bytes is not whole frames.
        format_chunk(chunks, 1, 2);
        data_chunk(chunks, kSamples);
        assert(rejected(riff(chunks)));
    }
    {
        Bytes chunks; // The data chunk claims more bytes than the file has.
        format_chunk(chunks);
        data_chunk(chunks, kSamples);
        Bytes file = riff(chunks);
        file.resize(file.size() - 2);
        assert(rejected(file));
    }
    {
        Bytes chunks; // No data chunk at all.
        format_chunk(chunks);
        assert(rejected(riff(chunks)));
    }
}

// The firmware embeds these files; each must parse into the format playback opens.
void committed_cues_parse(int count, char** paths) {
    assert(count == 3);
    for (int i = 0; i < count; ++i) {
        std::FILE* file = std::fopen(paths[i], "rb");
        assert(file);
        Bytes bytes;
        for (int c; (c = std::fgetc(file)) != EOF;)
            bytes.push_back(static_cast<uint8_t>(c));
        std::fclose(file);
        Pcm pcm;
        assert(parse_wav(bytes, pcm));
        assert(pcm.sample_rate == 22050 && pcm.channels == 1 && pcm.bits_per_sample == 16);
        assert(!pcm.samples.empty());
    }
}

void status_cues() {
    assert(cue_for(Status::connected) == Cue::connected);
    assert(cue_for(Status::error) == Cue::error);
    assert(!cue_for(Status::pairing));
    assert(!cue_for(Status::disconnected));
    // Each event that has a cue has its own sound.
    assert(kNotificationCue != Cue::connected && kNotificationCue != Cue::error);
}

void volume_and_mute() {
    assert(clamp_volume(-5) == 0);
    assert(clamp_volume(0) == 0);
    assert(clamp_volume(42) == 42);
    assert(clamp_volume(250) == kMaxVolume);
    assert(kDefaultVolume > 0 && kDefaultVolume <= kMaxVolume);
    assert(Output { }.audible());
    // Mute silences at any volume, and zero volume is silent too.
    for (int volume : { 0, 1, 60, 100 })
        assert(!(Output { .volume = static_cast<uint8_t>(volume), .muted = true }.audible()));
    assert(!(Output { .volume = 0, .muted = false }.audible()));
    assert((Output { .volume = 1, .muted = false }.audible()));
}
} // namespace

int main(int argc, char** argv) {
    parses_a_plain_wav();
    skips_other_chunks_with_padding();
    rejects_anything_else();
    committed_cues_parse(argc - 1, argv + 1);
    status_cues();
    volume_and_mute();
}
