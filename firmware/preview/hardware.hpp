#pragma once

#include "features/audio_cues/cues.hpp"
#include "features/brightness/policy.hpp"
#include "features/led_feedback/patterns.hpp"
#include "features/settings/settings.hpp"
#include "drivers/leds.hpp"

#include <array>
#include <optional>

namespace mosaic::preview {

// Host clock and inputs around the firmware's real feedback policies. SDL only
// presents these outputs; it does not choose colors, cues, or brightness.
class Hardware {
public:
    std::optional<features::audio_cues::Cue> set_status(events::Status status);
    features::audio_cues::Cue notification(uint32_t now_ms);
    void acknowledge();
    void set_settings(const features::settings::Settings& settings);
    void set_lux(std::optional<float> lux);
    void tick(uint32_t now_ms);

    events::Status status() const { return status_; }
    bool paired() const { return paired_; }
    std::optional<float> lux() const { return lux_; }
    uint8_t screen_percent() const { return output_.screen_percent; }
    uint8_t led_level() const { return output_.led_level; }
    const auto& ring() const { return ring_; }
    const auto& strip() const { return strip_; }
    const features::settings::Effects& settings_effects() const { return settings_; }

private:
    events::Status status_ = events::Status::disconnected;
    bool paired_ = false;
    features::led_feedback::Feedback feedback_ { status_, paired_ };
    features::audio_cues::StatusCues cues_;
    features::brightness::AutoBrightness brightness_;
    features::settings::Effects settings_ = features::settings::effects(features::settings::Settings { });
    std::optional<float> lux_ = 150.0F;
    features::brightness::Output output_ = features::brightness::output(features::brightness::kFallback);
    uint32_t last_brightness_ms_ = 0;
    uint32_t last_led_ms_ = 0;
    std::array<drivers::leds::Color, drivers::leds::kRingLength> ring_ { };
    std::array<drivers::leds::Color, drivers::leds::kStripLength> strip_ { };
};

} // namespace mosaic::preview
