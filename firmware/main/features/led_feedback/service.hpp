#pragma once
#include "esp_err.h"
namespace mosaic::features::led_feedback {
// Shows the device status and new notifications on the LEDs, from their own task. Call once,
// after drivers::leds::start() and events::start() succeed.
esp_err_t start();
// Any task. Off keeps both outputs dark; on resumes the current pattern. On by default.
void set_enabled(bool enabled);
}
