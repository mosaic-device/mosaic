#ifdef MOSAIC_BRIGHTNESS_TRACE
// Compile this file's Debug logs in without raising the project-wide level.
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif
#include "service.hpp"
#include "policy.hpp"
#include "../../drivers/bh1750.hpp"
#include "../../drivers/display.hpp"
#include "../../drivers/leds.hpp"
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

    void apply(Levels levels) {
        // Each driver keeps its own floor and ceiling; a failure here repeats next change.
        drivers::display::set_brightness(levels.screen);
        drivers::leds::set_level(levels.leds);
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
            const Levels levels = brightness.update(lux);
            if (!any_applied || !(levels == applied)) {
                apply(levels);
                applied = levels;
                any_applied = true;
                if (lux)
                    ESP_LOGD(kTag, "%ld lux: screen %u%%, LEDs %u/255", std::lround(*lux), levels.screen, levels.leds);
                else
                    ESP_LOGD(kTag, "No reading: screen %u%%, LEDs %u/255", levels.screen, levels.leds);
            }
        }
    }
}
esp_err_t start() {
#ifdef MOSAIC_BRIGHTNESS_TRACE
    esp_log_level_set(kTag, ESP_LOG_DEBUG);
#endif
    return xTaskCreate(run, "mosaic_brightness", 3072, nullptr, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
}
