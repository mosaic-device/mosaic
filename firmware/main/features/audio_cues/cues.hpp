#pragma once

#include "../../events/status.hpp"

#include <cstdint>
#include <optional>

namespace mosaic::features::audio_cues {

// One per generated WAV file in generated/.
enum class Cue : uint8_t { notification, connected };

// Which status changes are heard. Connecting chimes when pairing completes, and the first
// time the device connects after starting, so the owner hears that it is ready. A
// reconnect after the server or Wi-Fi drops is silent, so a blip at night never wakes
// anyone. Nothing else chimes: errors and pairing are explained on screen.
class StatusCues {
public:
    // The cue for a change to this status, if any.
    std::optional<Cue> on_status(events::Status status);

private:
    events::Status previous_ = events::Status::disconnected;
    bool connected_before_ = false;
};
// The cue for each new notification.
constexpr Cue kNotificationCue = Cue::notification;

// Speaker output as a percentage of the codec's range. Device settings store the
// owner's choice; this is the first-boot default.
constexpr uint8_t kDefaultVolume = 60;
constexpr uint8_t kMaxVolume = 100;

struct Output {
    uint8_t volume = kDefaultVolume;
    bool muted = false;
    // Muted or at zero volume, a cue is skipped entirely and the amplifier stays off.
    bool audible() const {
        return !muted && volume > 0;
    }
};

// Clamps a requested volume into 0-kMaxVolume.
uint8_t clamp_volume(int percent);

}
