#pragma once
#include "esp_err.h"
#include "lvgl.h"
namespace mosaic::drivers::encoder {
// Registers the KY-040 as an LVGL encoder and makes its focus group the default,
// so focusable widgets created afterwards are reachable by turning. Call once,
// with the display lock held.
esp_err_t start(lv_display_t* display);
}
