#pragma once

#include "../settings_validation.hpp"
#include "esp_err.h"
#include "esp_netif.h"

namespace mosaic::features::provisioning::adapters::wifi {
struct Ap {
    char ssid[33] { };
    char key[13] { };
    char ip[16] { };
};

enum class Event : uint8_t { connected, disconnected, got_ip };
// Called on the event loop task. Must only attempt a nonblocking, bounded handoff.
using OnEvent = void (*)(Event);

// Creates the AP and STA netifs, initializes the radio with RAM-only storage and
// subscribes on_event. On failure, stage names the step that failed; call deinit()
// and release() to undo the steps that succeeded.
esp_err_t init(OnEvent on_event, const char*& stage);
// Unsubscribes and deinitializes the radio. Safe to repeat; retry on failure, and
// never release() while it is failing, because event callbacks may still run.
esp_err_t deinit();
// Destroys the netifs. Call only after deinit() succeeds or init() never ran.
void release();
bool initialized();
esp_netif_t* ap_netif();

esp_err_t configure_sta(const form::Settings& settings);
esp_err_t start_saved(const form::Settings& settings);
// Stop Wi-Fi before generating AP credentials; caller must show setup synchronously
// before calling start_ap() and only then start HTTP/DNS.
esp_err_t configure_ap(Ap& ap);
esp_err_t start_ap();
esp_err_t reconnect(const form::Settings& saved);
// Treats a radio that never started as already stopped.
esp_err_t stop();
// Whether the station is associated, with ssid when it is not null.
bool associated(const char* ssid);
// Whether the station is associated with ssid and has an IPv4 address.
bool has_ip(const char* ssid);
} // namespace mosaic::features::provisioning::adapters::wifi
