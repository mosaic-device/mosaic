#include "audio.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace mosaic::preview {

bool Audio::start() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "Audio unavailable: %s\n", SDL_GetError());
        return false;
    }
    constexpr std::array names { "notification.wav", "connected.wav" };
    for (size_t i = 0; i < names.size(); ++i) {
        const auto path = std::filesystem::path(MOSAIC_PREVIEW_AUDIO_DIR) / names[i];
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "Cannot read cue: %s\n", path.string().c_str());
            return false;
        }
        files_[i].assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (!features::audio_cues::parse_wav(files_[i], pcm_[i])) {
            std::fprintf(stderr, "Invalid cue WAV: %s\n", path.string().c_str());
            return false;
        }
    }
    const auto& first = pcm_[0];
    if (first.sample_rate != pcm_[1].sample_rate || first.channels != pcm_[1].channels
        || first.bits_per_sample != pcm_[1].bits_per_sample || first.bits_per_sample != 16) {
        std::fprintf(stderr, "Preview cues must share a 16-bit PCM format\n");
        return false;
    }
    SDL_AudioSpec wanted { };
    wanted.freq = static_cast<int>(first.sample_rate);
    wanted.format = AUDIO_S16LSB;
    wanted.channels = static_cast<uint8_t>(first.channels);
    wanted.samples = 1024;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (!device_) {
        std::fprintf(stderr, "Speaker unavailable: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void Audio::play(features::audio_cues::Cue cue, const features::settings::Effects& settings) {
    if (!device_ || settings.muted || !settings.volume)
        return;
    const auto samples = pcm_[static_cast<size_t>(cue)].samples;
    std::vector<uint8_t> mixed(samples.size(), 0);
    SDL_MixAudioFormat(mixed.data(), samples.data(), AUDIO_S16LSB, static_cast<uint32_t>(samples.size()),
        static_cast<int>(settings.volume) * SDL_MIX_MAXVOLUME / 100);
    if (SDL_QueueAudio(device_, mixed.data(), static_cast<uint32_t>(mixed.size())) != 0)
        std::fprintf(stderr, "Cannot play cue: %s\n", SDL_GetError());
}

bool Audio::playing() const {
    return device_ && SDL_GetQueuedAudioSize(device_) > 0;
}

void Audio::close() {
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
}

} // namespace mosaic::preview
