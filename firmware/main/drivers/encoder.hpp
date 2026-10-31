#pragma once
#include "esp_err.h"
#include "lvgl.h"
namespace mosaic::drivers::encoder {
// Registers the KY-040 as an LVGL encoder and makes its focus group the default,
// so focusable widgets created afterwards are reachable by turning. Call once,
// with the display lock held.
esp_err_t start(lv_display_t* display);
// Moves turning and clicking to another focus group, such as the active screen's.
// With the display lock held. Does nothing if the encoder did not start.
void set_group(lv_group_t* group);
// Running count of Back presses (the button held for the long-press time). Any task.
uint32_t back_presses();
}
