#include "display.hpp"
#include <algorithm>
#include <atomic>
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
namespace mosaic::drivers::display {
namespace {
    constexpr uint32_t kLockTimeoutMs = 250;
    constexpr int64_t kStartupSettleUs = 100'000;
    // The setup handshake treats a lit backlight as a visible screen, so a running
    // panel is never dimmed below this.
    constexpr uint8_t kMinBrightness = 10;
    lv_display_t* s_display = nullptr;
    // Read by set_brightness() on other tasks; set only on the LVGL task.
    std::atomic_bool s_backlight_on { false };
    std::atomic<uint8_t> s_brightness { 100 };
    bool s_initial_screen_flushed = false;
    int64_t s_backlight_enable_at = 0;
    bool s_startup_redraw_requested = false;
    std::atomic_bool s_shutting_down { false };
    void blank_on_shutdown() {
        if (s_shutting_down.exchange(true))
            return; // Early blanking and the shutdown callback share one handoff.
        const esp_err_t err = bsp_display_backlight_off();
        if (err != ESP_OK)
            ESP_LOGE("mosaic_display", "Cannot blank backlight on restart: %s", esp_err_to_name(err));
        // PWM is reset during restart. Drive the active-high backlight low and
        // latch the pad until the next boot has restored zero-duty PWM.
        gpio_config_t pin { };
        pin.pin_bit_mask = 1ULL << BSP_LCD_BACKLIGHT;
        pin.mode = GPIO_MODE_OUTPUT;
        esp_err_t hold_err = gpio_set_level(BSP_LCD_BACKLIGHT, 0);
        if (hold_err == ESP_OK)
            hold_err = gpio_config(&pin);
        if (hold_err == ESP_OK)
            hold_err = gpio_hold_en(BSP_LCD_BACKLIGHT);
        if (hold_err != ESP_OK)
            ESP_LOGE("mosaic_display", "Cannot hold backlight off across restart: %s", esp_err_to_name(hold_err));
    }

}
esp_err_t start() {
    // Single-owner startup; BSP has no full teardown for partial failure.
    static bool attempted = false;
    if (attempted)
        return s_display ? ESP_OK : ESP_ERR_INVALID_STATE;
    attempted = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_90,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = { .swap_xy = 1, .mirror_x = 1, .mirror_y = 0 },
    };
#pragma GCC diagnostic pop
    s_display = bsp_display_start_with_config(&cfg);
    if (!s_display)
        return ESP_FAIL;
    // The BSP has now configured the backlight PWM with duty=0, so releasing
    // a previous reboot's pad hold cannot reveal panel initialization.
    const esp_err_t release_err = gpio_hold_dis(BSP_LCD_BACKLIGHT);
    if (release_err != ESP_OK)
        return release_err;
    const esp_err_t shutdown_err = esp_register_shutdown_handler(blank_on_shutdown);
    if (shutdown_err != ESP_OK)
        return shutdown_err;
    return ESP_OK;
}
lv_display_t* handle() {
    return s_display;
}
bool lock() {
    return bsp_display_lock(kLockTimeoutMs);
}
void unlock() {
    bsp_display_unlock();
}
bool available() {
    return s_display && !s_shutting_down.load();
}
void tick() {
    if (!s_backlight_on && s_backlight_enable_at && !s_startup_redraw_requested
        && esp_timer_get_time() >= s_backlight_enable_at && !s_shutting_down.load()) {
        s_startup_redraw_requested = true;
        lv_obj_invalidate(lv_display_get_screen_active(s_display));
    }
}
void note_flush() {
    s_initial_screen_flushed = true;
}
bool refresh_ready() {
    if (!s_initial_screen_flushed)
        return false;
    // Do not relight the panel once restart blanking has begun.
    if (s_shutting_down.load()) {
        bsp_display_backlight_off();
        return false;
    }
    if (!s_backlight_on) {
        if (!s_backlight_enable_at) {
            s_backlight_enable_at = esp_timer_get_time() + kStartupSettleUs;
            return false;
        }
        if (!s_startup_redraw_requested || esp_timer_get_time() < s_backlight_enable_at)
            return false;
        // Mark the backlight on before reading the level: a concurrent set_brightness()
        // either sees it on and applies its level, or stored that level before this read.
        s_backlight_on = true;
        const esp_err_t err = bsp_display_brightness_set(s_brightness.load());
        if (err != ESP_OK) {
            s_backlight_on = false;
            ESP_LOGE("mosaic_display", "Backlight failed: %s", esp_err_to_name(err));
            return false;
        }
        if (s_shutting_down.load()) {
            bsp_display_backlight_off();
            return false;
        }
    }
    return true;
}
esp_err_t set_brightness(uint8_t percent) {
    s_brightness = std::clamp<uint8_t>(percent, kMinBrightness, 100);
    // Before startup lights the panel, the level waits; during restart it stays dark.
    if (!s_backlight_on.load() || s_shutting_down.load())
        return ESP_OK;
    return bsp_display_brightness_set(s_brightness.load());
}
void prepare_reboot() {
    blank_on_shutdown();
}
}
