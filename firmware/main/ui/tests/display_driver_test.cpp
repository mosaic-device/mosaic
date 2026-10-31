#include "../../drivers/display.cpp"
#include <cassert>
namespace {
int64_t clock_us = 1;
bool light = false;
bool held = true;
bool pwm_initialized = false;
bool fail_light = true;
bool race_shutdown = false;
int holds = 0;
int on_calls = 0;
int lit_percent = -1;
int redraws = 0;
void (*shutdown_callback)() = nullptr;
}
int64_t esp_timer_get_time() {
    return clock_us;
}
lv_display_t* bsp_display_start_with_config(const bsp_display_cfg_t* cfg) {
    assert(cfg->rotation == 90 && cfg->tear_avoid_mode == 3);
    assert(cfg->touch_flags.swap_xy == 1 && cfg->touch_flags.mirror_x == 1);
    light = false;
    pwm_initialized = true;
    static lv_display_t display;
    return &display;
}
esp_err_t bsp_display_backlight_off() {
    light = false;
    return ESP_OK;
}
esp_err_t bsp_display_brightness_set(int percent) {
    ++on_calls;
    if (fail_light)
        return ESP_FAIL;
    light = percent > 0;
    lit_percent = percent;
    if (race_shutdown)
        shutdown_callback();
    return ESP_OK;
}
bool bsp_display_lock(unsigned) {
    return true;
}
void bsp_display_unlock() { }
esp_err_t gpio_set_level(int pin, int level) {
    assert(pin == BSP_LCD_BACKLIGHT && level == 0);
    return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t* cfg) {
    assert(cfg->mode == GPIO_MODE_OUTPUT);
    return ESP_OK;
}
esp_err_t gpio_hold_en(int) {
    held = true;
    ++holds;
    return ESP_OK;
}
esp_err_t gpio_hold_dis(int) {
    assert(pwm_initialized && !light);
    held = false;
    return ESP_OK;
}
esp_err_t esp_register_shutdown_handler(void (*callback)()) {
    shutdown_callback = callback;
    return ESP_OK;
}
void lv_obj_invalidate(lv_obj_t*) {
    ++redraws;
}
int main() {
    using namespace mosaic::drivers::display;
    assert(start() == ESP_OK && !held && !light);
    assert(!refresh_ready()); // default screen refresh cannot reveal startup
    note_flush();
    assert(!refresh_ready());
    clock_us += 99'999;
    tick();
    assert(redraws == 0 && !refresh_ready());
    ++clock_us;
    assert(!refresh_ready()); // settle alone is insufficient; redraw required
    tick();
    assert(redraws == 1);
    assert(!refresh_ready() && on_calls == 1 && !light); // backlight failure blocks acknowledgement
    // A level set before the panel lights waits and is used when it lights. The
    // lit panel is never fully dark: setup trusts it to show credentials.
    assert(set_brightness(0) == ESP_OK && on_calls == 1 && s_brightness == kMinBrightness);
    assert(set_brightness(150) == ESP_OK && s_brightness == 100);
    assert(set_brightness(40) == ESP_OK && on_calls == 1);
    fail_light = false;
    race_shutdown = true;
    assert(!refresh_ready() && !light && held); // shutdown during enable is reblanked
    assert(lit_percent == 40 && on_calls == 2);
    assert(holds == 1 && !available());
    // During restart, a new level never relights the panel.
    assert(set_brightness(80) == ESP_OK && on_calls == 2 && !light);
    const int enabled = on_calls;
    prepare_reboot();
    shutdown_callback();
    tick();
    assert(!refresh_ready() && !light && on_calls == enabled && holds == 1);
}
