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
}
