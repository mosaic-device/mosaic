#include "wifi.hpp"
#include "../../../support/secure_erase.hpp"

#include <cstdio>
#include <cstring>

#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace mosaic::features::provisioning::adapters::wifi {
namespace {
    constexpr char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    OnEvent listener = nullptr;
    esp_netif_t* ap_interface = nullptr;
    esp_netif_t* sta_interface = nullptr;
    esp_event_handler_instance_t wifi_handler = nullptr;
    esp_event_handler_instance_t ip_handler = nullptr;
    bool radio_initialized = false;

    void on_wifi(void*, esp_event_base_t, int32_t id, void*) {
        if (id == WIFI_EVENT_STA_CONNECTED)
            listener(Event::connected);
        if (id == WIFI_EVENT_STA_DISCONNECTED)
            listener(Event::disconnected);
    }
    void on_ip(void*, esp_event_base_t, int32_t id, void*) {
        if (id == IP_EVENT_STA_GOT_IP)
            listener(Event::got_ip);
    }
}

esp_err_t init(OnEvent on_event, const char*& stage) {
    listener = on_event;
    stage = "Wi-Fi netifs";
    ap_interface = esp_netif_create_default_wifi_ap();
    sta_interface = esp_netif_create_default_wifi_sta();
    if (!ap_interface || !sta_interface)
        return ESP_ERR_NO_MEM;
    stage = "Wi-Fi init";
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
        return err;
    radio_initialized = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK)
        return err;
    stage = "Wi-Fi event registration";
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, nullptr, &wifi_handler);
    if (err != ESP_OK)
        return err;
    stage = "IP event registration";
    return esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, nullptr, &ip_handler);
}

esp_err_t deinit() {
    if (ip_handler) {
        const esp_err_t err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        if (err != ESP_OK)
            return err;
        ip_handler = nullptr;
    }
    if (wifi_handler) {
        const esp_err_t err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
        if (err != ESP_OK)
            return err;
        wifi_handler = nullptr;
    }
    if (radio_initialized) {
        const esp_err_t err = esp_wifi_deinit();
        if (err != ESP_OK)
            return err;
        radio_initialized = false;
    }
    return ESP_OK;
}

void release() {
    if (sta_interface) {
        esp_netif_destroy_default_wifi(sta_interface);
        sta_interface = nullptr;
    }
    if (ap_interface) {
        esp_netif_destroy_default_wifi(ap_interface);
        ap_interface = nullptr;
    }
}

bool initialized() {
    return radio_initialized;
}

esp_netif_t* ap_netif() {
    return ap_interface;
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

esp_err_t configure_ap(Ap& credentials) {
    uint8_t mac[6] { };
    esp_efuse_mac_get_default(mac);
    std::snprintf(credentials.ssid, sizeof(credentials.ssid), "Mosaic-%02X%02X%02X", mac[3], mac[4], mac[5]);
    bootloader_random_enable();
    constexpr uint32_t size = sizeof(kAlphabet) - 1;
    constexpr uint32_t threshold = (UINT32_MAX / size) * size;
    for (size_t i = 0; i < 12; ++i) {
        uint32_t value;
        do {
            value = esp_random();
        } while (value >= threshold);
        credentials.key[i] = kAlphabet[value % size];
    }
    bootloader_random_disable();
    wifi_config_t cfg { };
    std::memcpy(cfg.ap.ssid, credentials.ssid, std::strlen(credentials.ssid));
    std::memcpy(cfg.ap.password, credentials.key, 12);
    cfg.ap.ssid_len = std::strlen(credentials.ssid);
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK)
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    support::secure_erase(&cfg, sizeof(cfg));
    if (err != ESP_OK)
        return err;
    esp_netif_ip_info_t info { };
    err = esp_netif_get_ip_info(ap_interface, &info);
    if (err != ESP_OK)
        return err;
    if (!info.ip.addr || !inet_ntop(AF_INET, &info.ip.addr, credentials.ip, sizeof(credentials.ip)))
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
    const esp_err_t err = esp_wifi_stop();
    return err == ESP_ERR_WIFI_NOT_STARTED ? ESP_OK : err;
}

bool associated(const char* ssid) {
    wifi_ap_record_t record { };
    return esp_wifi_sta_get_ap_info(&record) == ESP_OK
        && (!ssid || std::strncmp(reinterpret_cast<const char*>(record.ssid), ssid, sizeof(record.ssid)) == 0);
}

bool has_ip(const char* ssid) {
    esp_netif_ip_info_t info { };
    return associated(ssid) && sta_interface && esp_netif_get_ip_info(sta_interface, &info) == ESP_OK
        && info.ip.addr != 0;
}
} // namespace mosaic::features::provisioning::adapters::wifi
