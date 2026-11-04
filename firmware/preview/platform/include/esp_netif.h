#pragma once
#include "esp_err.h"
#include "esp_event.h"
// Network interfaces are simulated by the preview's Wi-Fi adapter; this is only the
// opaque handle and the IP events other features listen for.
typedef struct esp_netif_obj esp_netif_t;
esp_err_t esp_netif_init();
ESP_EVENT_DECLARE_BASE(IP_EVENT);
enum { IP_EVENT_STA_GOT_IP = 0, IP_EVENT_STA_LOST_IP = 1 };
