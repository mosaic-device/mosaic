#include "cues.hpp"

namespace mosaic::features::audio_cues {

std::optional<Cue> cue_for(events::Status status) {
    // Provisional until #56: connecting and failing are heard; pairing and
    // disconnecting are left to the screen and LEDs.
    switch (status) {
    case events::Status::connected:
        return Cue::connected;
    case events::Status::error:
        return Cue::error;
    case events::Status::pairing:
    case events::Status::disconnected:
        break;
    }
    return std::nullopt;
}

uint8_t clamp_volume(int percent) {
    if (percent < 0)
        return 0;
    if (percent > kMaxVolume)
        return kMaxVolume;
    return static_cast<uint8_t>(percent);
}

}
