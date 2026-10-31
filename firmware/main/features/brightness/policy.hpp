#pragma once

#include <cstdint>
#include <optional>

namespace mosaic::features::brightness {

// Perceived brightness, 0-100: 50 looks half as bright as 100. Eyes judge light
// roughly logarithmically, so the policy works in these units, which keeps curve
// points easy to choose and fades even. output() converts them for the drivers.
struct Levels {
    uint8_t screen = 100;
    // A share of the LEDs' 30% ceiling, which the LED driver always applies.
    uint8_t leds = 100;
    bool operator==(const Levels&) const = default;
};

// What the drivers take: PWM duty, which light output follows linearly.
struct Output {
    // Backlight percent. The display driver keeps it at or above its visible minimum.
    uint8_t screen_percent;
    // LED level (0-255) below the 30% ceiling.
    uint8_t led_level;
    bool operator==(const Output&) const = default;
};

// Light output (0-1) for a perceived brightness (0-100), by CIE 1976 lightness.
float luminance(uint8_t perceived);
Output output(Levels levels);

// One point on the ambient light curve.
struct Point {
    float lux;
    Levels levels;
};

// Provisional until #56 defines how brightness follows the room. Between points,
// levels follow log(lux), which matches how eyes judge brightness; outside them
// they hold the end values.
inline constexpr Point kCurve[] = {
    { .lux = 1, .levels = { .screen = 45, .leds = 55 } },
    { .lux = 20, .levels = { .screen = 65, .leds = 75 } },
    { .lux = 150, .levels = { .screen = 85, .leds = 90 } },
    { .lux = 600, .levels = { .screen = 100, .leds = 100 } },
};
// Used while the sensor is missing or failing.
inline constexpr Levels kFallback { .screen = 85, .leds = 90 };

// The levels the curve gives for a light level.
Levels curve(float lux);

// Turns sensor readings into levels that change slowly and never flicker:
// readings are smoothed, small changes in light are ignored, and levels fade
// toward their goal a step per reading.
class AutoBrightness {
public:
    // The levels to show after one reading, or a failed reading (nullopt), which
    // fades to kFallback. A manual level (perceived, 0-100) overrides the light for
    // the screen and LEDs alike; readings still update the smoothing meanwhile, so
    // going back to automatic resumes from the current light. Call about every 200 ms.
    Levels update(std::optional<float> lux, std::optional<uint8_t> manual = std::nullopt);

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
    bool manual_ = false;
    Levels goal_ = kFallback;
    Levels shown_ = kFallback;
};

}
