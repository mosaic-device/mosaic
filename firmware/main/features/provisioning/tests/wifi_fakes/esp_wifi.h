#pragma once
#include <cstdint>
#include "esp_err.h"
enum wifi_mode_t { WIFI_MODE_STA, WIFI_MODE_AP };
enum wifi_interface_t { WIFI_IF_STA, WIFI_IF_AP };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK };
struct wifi_config_t {
    struct {
        uint8_t ssid[32] { };
        uint8_t password[64] { };
        struct {
            wifi_auth_mode_t authmode { };
        } threshold;
    } sta;
    struct {
        uint8_t ssid[32] { };
        uint8_t password[64] { };
        uint8_t ssid_len { };
        uint8_t max_connection { };
        wifi_auth_mode_t authmode { };
    } ap;
};
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_connect();
esp_err_t esp_wifi_disconnect();
esp_err_t esp_wifi_set_mode(wifi_mode_t);
esp_err_t esp_wifi_set_config(wifi_interface_t, wifi_config_t*);
