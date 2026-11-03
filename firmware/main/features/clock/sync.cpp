#include "sync.hpp"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
namespace mosaic::features::clock::sync {
namespace {
    constexpr const char* kTag = "mosaic_clock";
    constexpr char kServer[] = "pool.ntp.org";
    void (*s_on_synced)() = nullptr;

    void on_sync(struct timeval*) {
        s_on_synced();
    }
    void on_got_ip(void*, esp_event_base_t, int32_t, void*) {
        // Each join syncs at once, so the time is fresh after a reconnect too.
        const esp_err_t err = esp_netif_sntp_start();
        if (err != ESP_OK)
            ESP_LOGW(kTag, "Cannot start time sync: %s", esp_err_to_name(err));
    }
}
esp_err_t start(void (*on_synced)()) {
    s_on_synced = on_synced;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(kServer);
    // Started by the first Wi-Fi join; until then there is nothing to reach.
    config.start = false;
    config.sync_cb = on_sync;
    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&config), kTag, "SNTP");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, nullptr, nullptr),
        kTag, "Wi-Fi join");
    return ESP_OK;
}
}
