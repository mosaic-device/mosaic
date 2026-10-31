#pragma once
#include "esp_err.h"
#include "lvgl.h"
namespace mosaic::drivers::display {
esp_err_t start();
lv_display_t* handle();
bool lock();
void unlock();
void tick();
void note_flush();
bool refresh_ready();
bool available();
void prepare_reboot();
// Backlight level in percent (default 100), clamped to 10-100 so a lit panel stays
// visible. Any task. Before startup lights the panel, it is used when the panel
// lights; during restart it is ignored.
esp_err_t set_brightness(uint8_t percent);
}
