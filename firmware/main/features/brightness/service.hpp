#pragma once
#include "esp_err.h"
namespace mosaic::features::brightness {
// Follows ambient light with the screen and LED brightness, from its own task. Without
// the sensor, it logs once and holds fixed levels, retrying the sensor in the
// background. Call once, after the display and LED drivers start.
esp_err_t start();
}
