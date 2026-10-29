#ifdef MOSAIC_ENCODER_TRACE
// Compile this file's Debug logs in without raising the project-wide level, which
// floods early boot logging past the bootloader watchdog.
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif
#include "encoder.hpp"
#include "encoder_logic.hpp"
#include <atomic>
#include "button_gpio.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
namespace mosaic::drivers::encoder {
namespace {
    constexpr const char* kTag = "mosaic_encoder";
    // KY-040 wiring. Power the module from 3.3V, never 5V: its onboard pull-ups go
    // to its VCC pin, and a 5V module would drive 5V into these pins.
    constexpr gpio_num_t kPinA = GPIO_NUM_5;
    constexpr gpio_num_t kPinB = GPIO_NUM_4;
    constexpr gpio_num_t kPinButton = GPIO_NUM_2;
    // Both phases are pulled high between clicks; the switch pulls low when pressed.
    constexpr int kRestLevel = 1;
    constexpr uint8_t kPressedLevel = 0;
    // The hardware counter limits; accum_count carries the total past them.
    constexpr int kCountLimit = 10'000;
    constexpr uint32_t kGlitchFilterNs = 1'000;

    pcnt_unit_handle_t s_unit = nullptr;
    bool s_ready = false;
    DetentCounter s_detents;
    PressLatch s_latch;
    // Debounced button events, counted on the iot_button timer task and consumed by LVGL.
    std::atomic<uint32_t> s_downs { 0 };
    std::atomic<uint32_t> s_ups { 0 };

    bool at_rest() {
        return gpio_get_level(kPinA) == kRestLevel && gpio_get_level(kPinB) == kRestLevel;
    }
    void read(lv_indev_t*, lv_indev_data_t* data) {
        // Only trust rest if the phases were at rest on both sides of the count read.
        const bool rest_before = at_rest();
        int count = 0;
        if (pcnt_unit_get_count(s_unit, &count) != ESP_OK)
            return;
        const int32_t steps = s_detents.update(count, rest_before && at_rest());
        const bool pressed = s_latch.update({ .downs = s_downs.load(), .ups = s_ups.load() });
        if (steps != 0)
            ESP_LOGD(kTag, "Step %+ld (raw %d)", static_cast<long>(steps), count);
        data->enc_diff = static_cast<int16_t>(steps);
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    esp_err_t start_counter() {
        pcnt_unit_config_t unit_config { };
        unit_config.low_limit = -kCountLimit;
        unit_config.high_limit = kCountLimit;
        unit_config.flags.accum_count = 1;
        ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_config, &s_unit), kTag, "PCNT unit");
        const pcnt_glitch_filter_config_t filter { .max_glitch_ns = kGlitchFilterNs };
        ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_unit, &filter), kTag, "PCNT glitch filter");
        // Full 4x decoding: each channel counts its own edges, with the other
        // phase's level giving the direction.
        pcnt_chan_config_t a_config { };
        a_config.edge_gpio_num = kPinA;
        a_config.level_gpio_num = kPinB;
        pcnt_channel_handle_t a = nullptr;
        ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit, &a_config, &a), kTag, "PCNT channel A");
        pcnt_chan_config_t b_config { };
        b_config.edge_gpio_num = kPinB;
        b_config.level_gpio_num = kPinA;
        pcnt_channel_handle_t b = nullptr;
        ESP_RETURN_ON_ERROR(pcnt_new_channel(s_unit, &b_config, &b), kTag, "PCNT channel B");
        ESP_RETURN_ON_ERROR(
            pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE), kTag,
            "PCNT A edges");
        ESP_RETURN_ON_ERROR(
            pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), kTag,
            "PCNT A levels");
        ESP_RETURN_ON_ERROR(
            pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE), kTag,
            "PCNT B edges");
        ESP_RETURN_ON_ERROR(
            pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE), kTag,
            "PCNT B levels");
        // Accumulation only happens when the limits are watch points.
        ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit, kCountLimit), kTag, "PCNT high watch point");
        ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(s_unit, -kCountLimit), kTag, "PCNT low watch point");
        ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_unit), kTag, "PCNT enable");
        ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(s_unit), kTag, "PCNT clear");
        return pcnt_unit_start(s_unit);
    }
    void on_down(void*, void*) {
        s_downs.fetch_add(1);
        ESP_LOGD(kTag, "Button pressed");
    }
    void on_up(void*, void*) {
        s_ups.fetch_add(1);
        ESP_LOGD(kTag, "Button released");
    }
    esp_err_t start_button() {
        // iot_button polls and debounces on its own timer; read() only sees settled events.
        const button_config_t config { };
        button_gpio_config_t gpio { };
        gpio.gpio_num = kPinButton;
        gpio.active_level = kPressedLevel; // Internal pull-up stays on: the KY-040 switch has none.
        button_handle_t button = nullptr;
        ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&config, &gpio, &button), kTag, "Button device");
        ESP_RETURN_ON_ERROR(
            iot_button_register_cb(button, BUTTON_PRESS_DOWN, nullptr, on_down, nullptr), kTag, "Button down");
        return iot_button_register_cb(button, BUTTON_PRESS_UP, nullptr, on_up, nullptr);
    }
}
esp_err_t start(lv_display_t* display) {
    // Single-owner startup; a failed attempt leaves touch as the only input.
    static bool attempted = false;
    if (attempted)
        return s_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    attempted = true;
#ifdef MOSAIC_ENCODER_TRACE
    esp_log_level_set(kTag, ESP_LOG_DEBUG);
#endif
    // The PCNT channels enable internal pull-ups on A/B as a backstop to the module's.
    ESP_RETURN_ON_ERROR(start_counter(), kTag, "Cannot start rotation counter");
    ESP_RETURN_ON_ERROR(start_button(), kTag, "Cannot start button");
    lv_group_t* group = lv_group_create();
    lv_indev_t* indev = lv_indev_create();
    if (!group || !indev)
        return ESP_ERR_NO_MEM;
    lv_group_set_default(group);
    lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(indev, read);
    lv_indev_set_display(indev, display);
    lv_indev_set_group(indev, group);
    s_ready = true;
    ESP_LOGI(kTag, "Rotary encoder ready on A=%d B=%d button=%d", kPinA, kPinB, kPinButton);
    return ESP_OK;
}
}
