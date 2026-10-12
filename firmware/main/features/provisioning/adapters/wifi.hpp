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

esp_err_t configure_sta(const form::Settings& settings);
esp_err_t start_saved(const form::Settings& settings);
// Stop Wi-Fi before generating AP credentials; caller must show setup synchronously
// before calling start_ap() and only then start HTTP/DNS.
esp_err_t configure_ap(esp_netif_t* netif, Ap& ap);
esp_err_t start_ap();
esp_err_t reconnect(const form::Settings& saved);
esp_err_t stop();
} // namespace mosaic::features::provisioning::adapters::wifi
