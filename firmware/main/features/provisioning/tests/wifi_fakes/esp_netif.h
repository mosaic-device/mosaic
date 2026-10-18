#pragma once
#include <cstdint>
#include "esp_err.h"
struct esp_netif_t { };
struct esp_netif_ip_info_t {
    struct {
        uint32_t addr;
    } ip;
};
esp_err_t esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t*);
