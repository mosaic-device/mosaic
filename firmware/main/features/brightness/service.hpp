#pragma once
#include <cstdint>
#include <optional>
#include "esp_err.h"
namespace mosaic::features::brightness {
// Follows ambient light with the screen and LED brightness, from its own task. Without
// the sensor, it logs once and holds fixed levels, retrying the sensor in the
// background. Call once, after the display and LED drivers start.
esp_err_t start();
// Any task. A manual perceived level (0-100) for the screen and LEDs instead of
// following the light, or nullopt to follow it again. Fades like any other change.
void set_manual(std::optional<uint8_t> level);
}
