#pragma once

#include "status.hpp"

#include "esp_err.h"
#include "esp_event.h"

// Device events for navigation, LED and speaker feedback, on a dedicated esp_event loop. The M3
// connection publishes here; development builds can also publish over serial (MOSAIC_DEBUG_TRIGGERS).
ESP_EVENT_DECLARE_BASE(MOSAIC_EVENT);

enum : int32_t {
    // Data: the new mosaic::events::Status. Posted only when the status changes.
    MOSAIC_EVENT_STATUS_CHANGED,
    // No data. Posted once per new notification.
    MOSAIC_EVENT_NOTIFICATION_RECEIVED,
};

namespace mosaic::events {

// Call once from app_main before anything publishes or subscribes; repeated calls do nothing.
esp_err_t start();
// Handlers run one event at a time on the bus task and must not block: hand audio or LVGL work
// to the feature's own task. Accepts ESP_EVENT_ANY_ID. ESP_ERR_INVALID_STATE before start().
esp_err_t subscribe(int32_t id, esp_event_handler_t handler, void* arg, esp_event_handler_instance_t* instance);

// Task context only. Before start() these return ESP_ERR_INVALID_STATE. If the queue stays full
// for 100 ms, they log and return ESP_ERR_TIMEOUT and the status is unchanged.
esp_err_t set_status(Status status);
esp_err_t post_notification();
// The last status that was successfully posted. Initially disconnected.
Status status();

} // namespace mosaic::events
