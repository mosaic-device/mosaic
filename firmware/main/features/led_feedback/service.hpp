#pragma once
#include "esp_err.h"
namespace mosaic::features::led_feedback {
// Shows the device status and new notifications on the LEDs, from their own task. Call once,
// after drivers::leds::start() and events::start() succeed.
esp_err_t start();
}
