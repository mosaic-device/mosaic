#pragma once
#include "clock.hpp"
#include <span>
#include <string_view>
#include "esp_err.h"
namespace mosaic::features::clock {
// Sets the time zone and syncs time over SNTP each time Wi-Fi joins, then keeps it in
// sync hourly while connected. Never waits for the network. Call once, after
// esp_netif_init() and the default event loop exist.
esp_err_t start();
// Any task. The Home clock's text: the local time, or the placeholder until synced.
std::string_view now_text(std::span<char, kTextSize> buffer);
}
