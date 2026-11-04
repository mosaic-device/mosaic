#pragma once

#include "../audio_cues/cues.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace mosaic::features::settings {

// Manual brightness in perceived units (see features/brightness). The range starts
// where the screen is still clearly lit; steps are large enough to see.
inline constexpr uint8_t kBrightnessMin = 40;
inline constexpr uint8_t kBrightnessMax = 100;
inline constexpr uint8_t kBrightnessStep = 10;
// Silence is the Sounds switch, so the volume never reaches zero: a volume that says
// sounds are on must be audible.
inline constexpr uint8_t kVolumeMin = 10;
inline constexpr uint8_t kVolumeMax = audio_cues::kMaxVolume;
inline constexpr uint8_t kVolumeStep = 10;

// The owner's device feedback choices.
struct Settings {
    bool auto_brightness = true;
    // Used while automatic brightness is off.
    uint8_t brightness = 70;
    uint8_t volume = audio_cues::kDefaultVolume;
    bool muted = false;
    bool leds_on = true;
    bool operator==(const Settings&) const = default;
};

// What the feedback features receive after settings change. Shared by the
// firmware service and host preview so they apply the same choices.
struct Effects {
    std::optional<uint8_t> manual_brightness;
    uint8_t volume;
    bool muted;
    bool leds_on;
    bool operator==(const Effects&) const = default;
};
Effects effects(const Settings& settings);

// The settings screen's controls, in screen order.
enum class Control : uint8_t { auto_brightness, brightness, sounds, volume, leds };

// Switches flip; the others are levels.
constexpr bool is_switch(Control control) {
    return control == Control::auto_brightness || control == Control::sounds || control == Control::leds;
}

struct Range {
    uint8_t min;
    uint8_t max;
    uint8_t step;
};
// The range of a level control; brightness and volume only.
Range range(Control control);
// A switch's state or a level's value.
int value(const Settings& settings, Control control);

// Why a control can do nothing right now, in a few words to show beside it, or empty
// when it is available: brightness follows the room while automatic, and volume does
// nothing while sounds are off.
std::string_view unavailable_reason(const Settings& settings, Control control);
inline bool available(const Settings& settings, Control control) {
    return unavailable_reason(settings, control).empty();
}

// The settings after flipping a switch; unchanged for anything else.
Settings toggle(Settings settings, Control control);
// The settings after setting a level, snapped to its step and kept in range; unchanged
// when the control is unavailable or not a level.
Settings set_level(Settings settings, Control control, int level);
// Brings every value into range, such as a volume set from the console.
Settings normalize(Settings settings);

// The stored form: a version byte, then each field. Reading rejects anything not
// written by encode() at this version, so the caller falls back to defaults. Records
// from before the volume floor load with their volume raised to it.
inline constexpr size_t kStoredSize = 6;
std::array<uint8_t, kStoredSize> encode(const Settings& settings);
std::optional<Settings> decode(std::span<const uint8_t> stored);

}
