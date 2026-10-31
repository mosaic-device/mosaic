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

// Scales a full-scale channel value (0-255) onto 0-kCeiling, rounding to nearest.
constexpr uint8_t limit(uint8_t value) {
    return static_cast<uint8_t>((value * kCeiling + 127) / 255);
}

constexpr Color limit(Color color) {
    return { limit(color.red), limit(color.green), limit(color.blue) };
}

}
