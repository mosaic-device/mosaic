#include "service.hpp"
#include "patterns.hpp"
#include "../../drivers/leds.hpp"
#include "../../events/bus.hpp"
#include <array>
#include <atomic>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
namespace mosaic::features::led_feedback {
namespace {
    namespace leds = drivers::leds;
    constexpr const char* kTag = "mosaic_led_feedback";
    // 50 frames per second keeps fades smooth; a frame that matches the last is not sent.
    constexpr TickType_t kFrameTicks = pdMS_TO_TICKS(20);

    TaskHandle_t s_task = nullptr;
    // Written by bus handlers, read by the LED task, which the handlers wake.
    std::atomic<events::Status> s_status = events::Status::disconnected;
    std::atomic<uint32_t> s_notifications = 0;

    uint32_t now_ms() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }
    void on_status(void*, esp_event_base_t, int32_t, void* data) {
        s_status = *static_cast<const events::Status*>(data);
        xTaskNotifyGive(s_task);
    }
    void on_notification(void*, esp_event_base_t, int32_t, void*) {
        s_notifications.fetch_add(1);
        xTaskNotifyGive(s_task);
    }
    void run(void*) {
        Feedback feedback(s_status.load(), now_ms());
        uint32_t notifications = s_notifications.load();
        // The driver starts the LEDs dark, so a dark frame needs no write.
        std::array<Color, leds::kRingLength> ring { }, shown_ring { };
        std::array<Color, leds::kStripLength> strip { }, shown_strip { };
        esp_err_t last_err = ESP_OK;
        while (true) {
            const uint32_t now = now_ms();
            feedback.set_status(s_status.load(), now);
            const uint32_t received = s_notifications.load();
            if (received != notifications) {
                notifications = received;
                feedback.notify(now);
            }
            feedback.render(now, ring, strip);
            if (ring != shown_ring || strip != shown_strip) {
                const esp_err_t err = leds::show(ring, strip);
                if (err == ESP_OK) {
                    shown_ring = ring;
                    shown_strip = strip;
                } else if (err != last_err) {
                    ESP_LOGE(kTag, "Cannot show LED frame: %s", esp_err_to_name(err));
                }
                last_err = err;
            }
            ulTaskNotifyTake(pdTRUE, kFrameTicks);
        }
    }
}
esp_err_t start() {
    if (xTaskCreate(run, "mosaic_leds", 3072, nullptr, 3, &s_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(events::subscribe(MOSAIC_EVENT_STATUS_CHANGED, on_status, nullptr, nullptr), kTag, "Status");
    ESP_RETURN_ON_ERROR(
        events::subscribe(MOSAIC_EVENT_NOTIFICATION_RECEIVED, on_notification, nullptr, nullptr), kTag, "Notification");
    // A status set before subscribing would otherwise be missed. The bus stores the
    // status before posting it, so this never replaces a newer one with an older one.
    s_status = events::status();
    xTaskNotifyGive(s_task);
    return ESP_OK;
}
}
