#pragma once

#include "features/audio_cues/cues.hpp"
#include "features/audio_cues/wav.hpp"
#include "features/settings/settings.hpp"

#include <SDL.h>
#include <array>
#include <vector>

namespace mosaic::preview {

// Host speaker adapter: selects no cues itself, and plays the firmware's WAVs.
class Audio {
public:
    bool start();
    void play(features::audio_cues::Cue cue, const features::settings::Effects& settings);
    bool playing() const;
    void close();

private:
    SDL_AudioDeviceID device_ = 0;
    std::array<std::vector<uint8_t>, 2> files_;
    std::array<features::audio_cues::Pcm, 2> pcm_;
};

} // namespace mosaic::preview
