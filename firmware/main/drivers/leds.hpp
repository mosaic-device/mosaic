#pragma once
#include "leds_logic.hpp"
#include <cstddef>
#include <span>
#include "esp_err.h"
namespace mosaic::drivers::leds {
// The 8-LED ring, and the strip's planned length from the hardware wiki. Update the strip
// length once the strip is cut and its count is recorded there.
constexpr size_t kRingLength = 8;
constexpr size_t kStripLength = 12;
// Turns both outputs dark and blanks them again on restart. Call first in startup so
// the LEDs stay off until a feature sets them. Call once.
esp_err_t start();
// Sends one frame to both outputs. Every color passes through limit() at the current
// level, so nothing can exceed the brightness ceiling. Ignored once a restart has
// blanked the LEDs.
esp_err_t show(std::span<const Color, kRingLength> ring, std::span<const Color, kStripLength> strip);
// Dims every frame by level (0-255, starting at kFullLevel) below the ceiling, and
// resends the last frame so static patterns follow too. Any task.
esp_err_t set_level(uint8_t level);
}
