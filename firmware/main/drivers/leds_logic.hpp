#pragma once

#include <cstdint>

namespace mosaic::drivers::leds {

// A requested color at full scale. limit() maps it onto the safe output range.
struct Color {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    bool operator==(const Color&) const = default;
};

// The 30% brightness ceiling from the hardware wiki, per channel: a visual preference, not an
// electrical maximum. Every value sent to the LEDs passes through limit(), so no caller can exceed it.
constexpr uint8_t kCeiling = 255 * 30 / 100;

// Full brightness for limit()'s level: the ceiling itself.
constexpr uint8_t kFullLevel = 255;

// Scales a full-scale channel value (0-255) onto 0-kCeiling, rounding to nearest.
// level (0-255) dims it further, such as for a dark room; it can never brighten.
constexpr uint8_t limit(uint8_t value, uint8_t level = kFullLevel) {
    return static_cast<uint8_t>((value * level * kCeiling + 255 * 255 / 2) / (255 * 255));
}

constexpr Color limit(Color color, uint8_t level = kFullLevel) {
    return { limit(color.red, level), limit(color.green, level), limit(color.blue, level) };
}

}
