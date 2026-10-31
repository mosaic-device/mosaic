#ifdef MOSAIC_BRIGHTNESS_TRACE
// Compile this file's Debug logs in without raising the project-wide level.
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif
#include "service.hpp"
#include "policy.hpp"
#include "../../drivers/bh1750.hpp"
#include "../../drivers/display.hpp"
#include "../../drivers/leds.hpp"
#include <atomic>
#include <cmath>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
namespace mosaic::features::brightness {
namespace {
    constexpr const char* kTag = "mosaic_brightness";
    // A little longer than one sensor measurement (up to 180 ms).
    constexpr TickType_t kReadPeriod = pdMS_TO_TICKS(200);
    // How often a missing or failed sensor is retried, quietly.
    constexpr int64_t kRetryUs = 5'000'000;
    constexpr int16_t kAutomatic = -1;

    // The manual level, or kAutomatic. Written by any task.
    std::atomic<int16_t> s_manual = kAutomatic;

    void apply(Output out) {
        // Each driver keeps its own floor and ceiling; a failure here repeats next change.
        drivers::display::set_brightness(out.screen_percent);
        drivers::leds::set_level(out.led_level);
    }
    void run(void*) {
        AutoBrightness brightness;
        esp_err_t err = drivers::bh1750::start();
        bool sensing = err == ESP_OK;
        if (sensing)
            ESP_LOGI(kTag, "Ambient light sensor ready");
        else
            ESP_LOGW(kTag, "Ambient light sensor unavailable (%s); using fixed brightness", esp_err_to_name(err));
        int64_t retry_at = esp_timer_get_time() + kRetryUs;
        Levels applied { };
        bool any_applied = false;
        while (true) {
            // Waiting first also lets the first measurement after start() finish.
            vTaskDelay(kReadPeriod);
            if (!sensing && esp_timer_get_time() >= retry_at) {
                retry_at = esp_timer_get_time() + kRetryUs;
                if (drivers::bh1750::start() == ESP_OK) {
                    sensing = true;
                    ESP_LOGI(kTag, "Ambient light sensor ready");
                    continue; // Its first measurement is not ready yet.
                }
            }
            std::optional<float> lux;
            if (sensing) {
                float reading = 0;
                err = drivers::bh1750::read(reading);
                if (err == ESP_OK) {
                    lux = reading;
                } else {
                    sensing = false;
                    retry_at = esp_timer_get_time() + kRetryUs;
                    ESP_LOGW(kTag, "Ambient light sensor failed (%s); using fixed brightness", esp_err_to_name(err));
                }
            }
            const int16_t manual = s_manual.load();
            const Levels levels = brightness.update(
                lux, manual == kAutomatic ? std::nullopt : std::optional<uint8_t>(static_cast<uint8_t>(manual)));
            if (!any_applied || !(levels == applied)) {
                const Output out = output(levels);
                apply(out);
                applied = levels;
                any_applied = true;
                ESP_LOGD(kTag, "%ld lux: screen %u (%u%% PWM), LEDs %u (%u/255)", lux ? std::lround(*lux) : -1L,
                    levels.screen, out.screen_percent, levels.leds, out.led_level);
            }
        }
    }
}
void set_manual(std::optional<uint8_t> level) {
    s_manual = level ? static_cast<int16_t>(*level) : kAutomatic;
}
esp_err_t start() {
#ifdef MOSAIC_BRIGHTNESS_TRACE
    esp_log_level_set(kTag, ESP_LOG_DEBUG);
#endif
    return xTaskCreate(run, "mosaic_brightness", 3072, nullptr, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
}
