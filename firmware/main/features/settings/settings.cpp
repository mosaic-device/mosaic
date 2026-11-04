#include "settings.hpp"

#include <algorithm>

namespace mosaic::features::settings {
namespace {
    constexpr uint8_t kVersion = 1;

    // A stored flag must be exactly 0 or 1.
    std::optional<bool> flag(uint8_t value) {
        if (value > 1)
            return std::nullopt;
        return value == 1;
    }
} // namespace

Effects effects(const Settings& settings) {
    return {
        .manual_brightness = settings.auto_brightness ? std::nullopt : std::optional<uint8_t>(settings.brightness),
        .volume = settings.volume,
        .muted = settings.muted,
        .leds_on = settings.leds_on,
    };
}

Range range(Control control) {
    if (control == Control::volume)
        return { .min = kVolumeMin, .max = kVolumeMax, .step = kVolumeStep };
    return { .min = kBrightnessMin, .max = kBrightnessMax, .step = kBrightnessStep };
}

int value(const Settings& settings, Control control) {
    switch (control) {
    case Control::auto_brightness:
        return settings.auto_brightness;
    case Control::brightness:
        return settings.brightness;
    case Control::sounds:
        return !settings.muted;
    case Control::volume:
        return settings.volume;
    case Control::leds:
        return settings.leds_on;
    }
    return 0;
}

std::string_view unavailable_reason(const Settings& settings, Control control) {
    if (control == Control::brightness && settings.auto_brightness)
        return "Follows room light";
    if (control == Control::volume && settings.muted)
        return "Sounds are off";
    return { };
}

Settings toggle(Settings settings, Control control) {
    switch (control) {
    case Control::auto_brightness:
        settings.auto_brightness = !settings.auto_brightness;
        break;
    case Control::sounds:
        settings.muted = !settings.muted;
        break;
    case Control::leds:
        settings.leds_on = !settings.leds_on;
        break;
    case Control::brightness:
    case Control::volume:
        break;
    }
    return settings;
}

Settings set_level(Settings settings, Control control, int level) {
    if (is_switch(control) || !available(settings, control))
        return settings;
    const Range limits = range(control);
    // Nearest step from the bottom of the range, so a slider dragged between steps lands on one.
    const int steps = (std::clamp<int>(level, limits.min, limits.max) - limits.min + limits.step / 2) / limits.step;
    const auto snapped = static_cast<uint8_t>(std::min<int>(limits.min + steps * limits.step, limits.max));
    if (control == Control::brightness)
        settings.brightness = snapped;
    else
        settings.volume = snapped;
    return settings;
}

Settings normalize(Settings settings) {
    settings.brightness = std::clamp(settings.brightness, kBrightnessMin, kBrightnessMax);
    settings.volume = std::clamp(settings.volume, kVolumeMin, kVolumeMax);
    return settings;
}

std::array<uint8_t, kStoredSize> encode(const Settings& settings) {
    return {
        kVersion,
        settings.auto_brightness,
        settings.brightness,
        settings.volume,
        settings.muted,
        settings.leds_on,
    };
}

std::optional<Settings> decode(std::span<const uint8_t> stored) {
    if (stored.size() != kStoredSize || stored[0] != kVersion)
        return std::nullopt;
    const auto automatic = flag(stored[1]);
    const auto muted = flag(stored[4]);
    const auto leds_on = flag(stored[5]);
    const uint8_t brightness = stored[2];
    const uint8_t volume = stored[3];
    if (!automatic || !muted || !leds_on || brightness < kBrightnessMin || brightness > kBrightnessMax
        || volume > kVolumeMax)
        return std::nullopt;
    return normalize(Settings {
        .auto_brightness = *automatic,
        .brightness = brightness,
        .volume = volume,
        .muted = *muted,
        .leds_on = *leds_on,
    });
}

}
