#pragma once
#include <cstdint>
#include "esp_err.h"
using esp_event_base_t = const char*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
using esp_event_handler_instance_t = void*;
inline constexpr int32_t ESP_EVENT_ANY_ID = -1;
inline constexpr esp_event_base_t WIFI_EVENT = "WIFI_EVENT", IP_EVENT = "IP_EVENT";
inline constexpr int32_t IP_EVENT_STA_GOT_IP = 0;
esp_err_t esp_event_handler_instance_register(
    esp_event_base_t, int32_t, esp_event_handler_t, void*, esp_event_handler_instance_t*);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t, esp_event_handler_instance_t);
