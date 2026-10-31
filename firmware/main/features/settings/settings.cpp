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

bool available(const Settings& settings, Change change) {
    switch (change) {
    case Change::brighter:
        return !settings.auto_brightness && settings.brightness < kBrightnessMax;
    case Change::dimmer:
        return !settings.auto_brightness && settings.brightness > kBrightnessMin;
    case Change::louder:
        return settings.volume < audio_cues::kMaxVolume;
    case Change::quieter:
        return settings.volume > 0;
    case Change::auto_brightness:
    case Change::mute:
    case Change::leds:
        return true;
    }
    return false;
}

Settings apply(Settings settings, Change change) {
    if (!available(settings, change))
        return settings;
    switch (change) {
    case Change::auto_brightness:
        settings.auto_brightness = !settings.auto_brightness;
        break;
    case Change::brighter:
        settings.brightness = std::min<uint8_t>(settings.brightness + kBrightnessStep, kBrightnessMax);
        break;
    case Change::dimmer:
        settings.brightness = std::max<uint8_t>(settings.brightness - kBrightnessStep, kBrightnessMin);
        break;
    case Change::louder:
        settings.volume = std::min<uint8_t>(settings.volume + kVolumeStep, audio_cues::kMaxVolume);
        break;
    case Change::quieter:
        settings.volume = static_cast<uint8_t>(std::max(settings.volume - kVolumeStep, 0));
        break;
    case Change::mute:
        settings.muted = !settings.muted;
        break;
    case Change::leds:
        settings.leds_on = !settings.leds_on;
        break;
    }
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
        || volume > audio_cues::kMaxVolume)
        return std::nullopt;
    return Settings {
        .auto_brightness = *automatic,
        .brightness = brightness,
        .volume = volume,
        .muted = *muted,
        .leds_on = *leds_on,
    };
}

}
