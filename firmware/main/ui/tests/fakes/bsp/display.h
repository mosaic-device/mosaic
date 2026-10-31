#pragma once
#include "lvgl.h"
#include "esp_err.h"
struct bsp_display_cfg_t {
    int lv_adapter_cfg;
    int rotation;
    int tear_avoid_mode;
    struct {
        int swap_xy;
        int mirror_x;
        int mirror_y;
    } touch_flags;
};
#define ESP_LV_ADAPTER_DEFAULT_CONFIG() 0
constexpr int ESP_LV_ADAPTER_ROTATE_90 = 90;
constexpr int ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL = 3;
lv_display_t* bsp_display_start_with_config(const bsp_display_cfg_t*);
esp_err_t bsp_display_backlight_off();
esp_err_t bsp_display_brightness_set(int percent);
bool bsp_display_lock(unsigned);
void bsp_display_unlock();
