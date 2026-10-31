#pragma once

#include "../audio_cues/cues.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace mosaic::features::settings {

// Manual brightness in perceived units (see features/brightness). The range starts
// where the screen is still clearly lit; steps are large enough to see.
inline constexpr uint8_t kBrightnessMin = 40;
inline constexpr uint8_t kBrightnessMax = 100;
inline constexpr uint8_t kBrightnessStep = 10;
inline constexpr uint8_t kVolumeStep = 10;

// The owner's device feedback choices. Defaults and ranges are provisional until #56.
struct Settings {
    bool auto_brightness = true;
    // Used while automatic brightness is off.
    uint8_t brightness = 70;
    uint8_t volume = audio_cues::kDefaultVolume;
    bool muted = false;
    bool leds_on = true;
    bool operator==(const Settings&) const = default;
};

// What the settings screen can ask for.
enum class Change : uint8_t { auto_brightness, brighter, dimmer, louder, quieter, mute, leds };

// Whether a change does anything now: manual brightness is unavailable while it is
// automatic, and nothing steps past its range. The screen disables unavailable controls.
bool available(const Settings& settings, Change change);
// The settings after a change; unchanged when it is unavailable.
Settings apply(Settings settings, Change change);

// The stored form: a version byte, then each field. Reading rejects anything not
// written by encode() at this version, so the caller falls back to defaults.
inline constexpr size_t kStoredSize = 6;
std::array<uint8_t, kStoredSize> encode(const Settings& settings);
std::optional<Settings> decode(std::span<const uint8_t> stored);

}
