#pragma once
using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_WIFI_NOT_STARTED = 0x3002;
const char* esp_err_to_name(esp_err_t);
