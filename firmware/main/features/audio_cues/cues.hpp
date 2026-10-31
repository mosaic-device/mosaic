#pragma once

#include "../../events/status.hpp"

#include <cstdint>
#include <optional>

namespace mosaic::features::audio_cues {

// One per generated WAV file in generated/.
enum class Cue : uint8_t { notification, connected, error };

// The cue for a status change, if any. Provisional until #56 defines the sounds.
std::optional<Cue> cue_for(events::Status status);
// The cue for each new notification.
constexpr Cue kNotificationCue = Cue::notification;

// Speaker output as a percentage of the codec's range. The default is provisional
// until device settings (#88) store the owner's choice.
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
