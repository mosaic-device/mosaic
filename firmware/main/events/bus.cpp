#include "bus.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"

#include <atomic>

ESP_EVENT_DEFINE_BASE(MOSAIC_EVENT);

namespace mosaic::events {
namespace {
    constexpr char kTag[] = "mosaic_events";
    // Human-rate events never fill the queue; a full queue means a handler is stuck.
    constexpr TickType_t kPostWait = pdMS_TO_TICKS(100);

    std::atomic<esp_event_loop_handle_t> loop = nullptr;
    // Serializes status changes so posts arrive in the order statuses were set.
    SemaphoreHandle_t status_lock = nullptr;
    std::atomic<Status> current = Status::disconnected;
} // namespace

esp_err_t start() {
    if (loop.load() != nullptr)
        return ESP_OK;
    status_lock = xSemaphoreCreateMutex();
    if (status_lock == nullptr)
        return ESP_ERR_NO_MEM;
    const esp_event_loop_args_t args {
        .queue_size = 16,
        .task_name = "mosaic_events",
        .task_priority = 4,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY,
    };
    esp_event_loop_handle_t created = nullptr;
    const esp_err_t err = esp_event_loop_create(&args, &created);
    if (err != ESP_OK) {
        vSemaphoreDelete(status_lock);
        status_lock = nullptr;
        return err;
    }
    loop = created;
    return ESP_OK;
}

esp_err_t subscribe(int32_t id, esp_event_handler_t handler, void* arg, esp_event_handler_instance_t* instance) {
    const esp_event_loop_handle_t target = loop.load();
    if (target == nullptr)
        return ESP_ERR_INVALID_STATE;
    return esp_event_handler_instance_register_with(target, MOSAIC_EVENT, id, handler, arg, instance);
}

esp_err_t set_status(Status status) {
    const esp_event_loop_handle_t target = loop.load();
    if (target == nullptr)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(status_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    const Status previous = current.load();
    if (status != previous) {
        // Store first so handlers that call status() see the new value.
        current = status;
        err = esp_event_post_to(target, MOSAIC_EVENT, MOSAIC_EVENT_STATUS_CHANGED, &status, sizeof(status), kPostWait);
        if (err == ESP_OK) {
            ESP_LOGI(kTag, "Status: %s", name(status));
        } else {
            current = previous;
            ESP_LOGE(kTag, "Status %s not posted: %s", name(status), esp_err_to_name(err));
        }
    }
    xSemaphoreGive(status_lock);
    return err;
}

esp_err_t post_notification() {
    const esp_event_loop_handle_t target = loop.load();
    if (target == nullptr)
        return ESP_ERR_INVALID_STATE;
    const esp_err_t err
        = esp_event_post_to(target, MOSAIC_EVENT, MOSAIC_EVENT_NOTIFICATION_RECEIVED, nullptr, 0, kPostWait);
    if (err == ESP_OK)
        ESP_LOGI(kTag, "Notification");
    else
        ESP_LOGE(kTag, "Notification not posted: %s", esp_err_to_name(err));
    return err;
}

Status status() {
    return current.load();
}

} // namespace mosaic::events
