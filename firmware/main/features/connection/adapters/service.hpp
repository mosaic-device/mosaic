#pragma once

#include "esp_err.h"

// Keeps the device connected to the server saved during setup, from its own task: it opens
// the secure WebSocket whenever Wi-Fi is online and follows the policy's retries. It never
// opens setup or changes saved settings.
namespace mosaic::features::connection::adapters::service {

// Call once, after NVS, esp_netif_init(), esp_event_loop_create_default() and events::start(),
// and before provisioning starts Wi-Fi, so the first connection isn't missed. Repeated calls
// do nothing.
esp_err_t start();

}
