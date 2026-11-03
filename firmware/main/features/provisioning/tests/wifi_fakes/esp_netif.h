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
esp_netif_t* esp_netif_create_default_wifi_ap();
esp_netif_t* esp_netif_create_default_wifi_sta();
void esp_netif_destroy_default_wifi(void*);
