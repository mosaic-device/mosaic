#pragma once

#include <cstdint>
#include <optional>

namespace mosaic::features::brightness {

struct Levels {
    // Backlight percent. The display driver keeps it at or above its visible minimum.
    uint8_t screen = 100;
    // LED level (0-255) below the 30% ceiling; the LED driver applies the ceiling.
    uint8_t leds = 255;
    bool operator==(const Levels&) const = default;
};

// One point on the ambient light curve.
struct Point {
    float lux;
    Levels levels;
};

// Provisional until #56 defines how brightness follows the room. Between points,
// levels follow log(lux), which matches how eyes judge brightness; outside them
// they hold the end values.
inline constexpr Point kCurve[] = {
    { .lux = 1, .levels = { .screen = 15, .leds = 64 } },
    { .lux = 20, .levels = { .screen = 35, .leds = 128 } },
    { .lux = 150, .levels = { .screen = 65, .leds = 200 } },
    { .lux = 600, .levels = { .screen = 100, .leds = 255 } },
};
// Used while the sensor is missing or failing.
inline constexpr Levels kFallback { .screen = 70, .leds = 200 };

// The levels the curve gives for a light level.
Levels curve(float lux);

// Turns sensor readings into levels that change slowly and never flicker:
// readings are smoothed, small changes in light are ignored, and levels fade
// toward their goal a step per reading.
class AutoBrightness {
public:
    // The levels to show after one reading, or a failed reading (nullopt), which
    // fades to kFallback. Call about every 200 ms.
    Levels update(std::optional<float> lux);

private:
    bool started_ = false;
    bool sensing_ = false;
    // Smoothed light, as log10(lux).
    float smoothed_ = 0;
    // The smoothed light the goal was chosen for. The goal holds until the light
    // moves away from it by more than the hysteresis, then follows the light while
    // readings stay ahead of the smoothing in the direction it moved (+1 or -1).
    float goal_light_ = 0;
    bool following_ = false;
    float direction_ = 0;
    Levels goal_ = kFallback;
    Levels shown_ = kFallback;
};

}
