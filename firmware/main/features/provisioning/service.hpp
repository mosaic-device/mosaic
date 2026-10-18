#pragma once

#include "esp_err.h"

namespace mosaic::features::provisioning {

// Call after ui::start(), NVS, esp_netif_init(), and esp_event_loop_create_default().
// Starts one long-lived provisioning owner task; repeated calls do not start another.
// Does not initialize NVS, esp_netif, the event loop, or the hosted C6 transport.
esp_err_t start();

} // namespace mosaic::features::provisioning
