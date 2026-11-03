#include "hardware.hpp"

namespace mosaic::preview {

std::optional<features::audio_cues::Cue> Hardware::set_status(events::Status status) {
    if (status == status_)
        return std::nullopt;
    paired_ = events::paired_after(paired_, status);
    status_ = status;
    feedback_.set_status(status_, paired_);
    return cues_.on_status(status);
}

features::audio_cues::Cue Hardware::notification(uint32_t now_ms) {
    feedback_.notify(now_ms);
    return features::audio_cues::kNotificationCue;
}

void Hardware::acknowledge() {
    feedback_.acknowledge();
}

void Hardware::set_settings(const features::settings::Settings& settings) {
    settings_ = features::settings::effects(settings);
}

void Hardware::set_lux(std::optional<float> lux) {
    lux_ = lux;
}

void Hardware::tick(uint32_t now_ms) {
    if (now_ms - last_brightness_ms_ >= 200 || last_brightness_ms_ == 0) {
        last_brightness_ms_ = now_ms;
        output_ = features::brightness::output(brightness_.update(lux_, settings_.manual_brightness));
    }
    if (now_ms - last_led_ms_ < 20 && last_led_ms_ != 0)
        return;
    last_led_ms_ = now_ms;
    feedback_.render(now_ms, ring_, strip_);
    for (auto& color : ring_)
        color = settings_.leds_on ? drivers::leds::limit(color, output_.led_level) : drivers::leds::Color { };
    for (auto& color : strip_)
        color = settings_.leds_on ? drivers::leds::limit(color, output_.led_level) : drivers::leds::Color { };
}

} // namespace mosaic::preview
