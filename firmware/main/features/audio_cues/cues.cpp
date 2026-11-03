#include "cues.hpp"

namespace mosaic::features::audio_cues {

std::optional<Cue> StatusCues::on_status(events::Status status) {
    const events::Status previous = previous_;
    previous_ = status;
    if (status != events::Status::connected)
        return std::nullopt;
    const bool first = !connected_before_;
    connected_before_ = true;
    if (first || previous == events::Status::pairing)
        return Cue::connected;
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
