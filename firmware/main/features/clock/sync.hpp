#pragma once
#include "esp_err.h"
namespace mosaic::features::clock::sync {
// Syncs time over SNTP each time Wi-Fi joins, then keeps it in sync hourly while
// connected, calling on_synced after each sync from the SNTP task. Never waits for the
// network. Call once, after esp_netif_init() and the default event loop exist.
esp_err_t start(void (*on_synced)());
}
