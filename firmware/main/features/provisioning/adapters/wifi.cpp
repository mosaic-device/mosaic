#include "wifi.hpp"
#include "../../../support/secure_erase.hpp"

#include <cstdio>
#include <cstring>

#include "bootloader_random.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace mosaic::features::provisioning::adapters::wifi {
namespace {
    constexpr char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
}

esp_err_t configure_sta(const form::Settings& settings) {
    wifi_config_t cfg { };
    std::memcpy(cfg.sta.ssid, settings.ssid, std::strlen(settings.ssid));
    std::memcpy(cfg.sta.password, settings.pass, std::strlen(settings.pass));
    cfg.sta.threshold.authmode = settings.pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    support::secure_erase(&cfg, sizeof(cfg));
    return err;
}

esp_err_t start_saved(const form::Settings& settings) {
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK)
        err = configure_sta(settings);
    if (err == ESP_OK)
        err = esp_wifi_start();
    if (err == ESP_OK)
        err = esp_wifi_connect();
    return err;
}

esp_err_t configure_ap(esp_netif_t* netif, Ap& ap) {
    uint8_t mac[6] { };
    esp_efuse_mac_get_default(mac);
    std::snprintf(ap.ssid, sizeof(ap.ssid), "Mosaic-%02X%02X%02X", mac[3], mac[4], mac[5]);
    bootloader_random_enable();
    constexpr uint32_t size = sizeof(kAlphabet) - 1;
    constexpr uint32_t threshold = (UINT32_MAX / size) * size;
    for (size_t i = 0; i < 12; ++i) {
        uint32_t value;
        do {
            value = esp_random();
        } while (value >= threshold);
        ap.key[i] = kAlphabet[value % size];
    }
    bootloader_random_disable();
    wifi_config_t cfg { };
    std::memcpy(cfg.ap.ssid, ap.ssid, std::strlen(ap.ssid));
    std::memcpy(cfg.ap.password, ap.key, 12);
    cfg.ap.ssid_len = std::strlen(ap.ssid);
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK)
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    support::secure_erase(&cfg, sizeof(cfg));
    if (err != ESP_OK)
        return err;
    esp_netif_ip_info_t info { };
    err = esp_netif_get_ip_info(netif, &info);
    if (err != ESP_OK)
        return err;
    if (!info.ip.addr || !inet_ntop(AF_INET, &info.ip.addr, ap.ip, sizeof(ap.ip)))
        return ESP_FAIL;
    return ESP_OK;
}

esp_err_t start_ap() {
    return esp_wifi_start();
}
esp_err_t reconnect(const form::Settings& saved) {
    // Reset any old association so this attempt produces a fresh Connected -> GotIp sequence.
    const esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
        return err;
    return start_saved(saved);
}
esp_err_t stop() {
    return esp_wifi_stop();
}
} // namespace mosaic::features::provisioning::adapters::wifi
