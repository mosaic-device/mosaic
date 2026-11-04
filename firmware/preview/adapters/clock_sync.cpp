// Time sync: the host clock is already right, so each Wi-Fi join syncs at once, as a
// prompt SNTP answer would.
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "features/clock/sync.hpp"

namespace mosaic::features::clock::sync {
namespace {
    void (*s_on_synced)() = nullptr;
    void on_got_ip(void*, esp_event_base_t, int32_t, void*) {
        s_on_synced();
    }
}
esp_err_t start(void (*on_synced)()) {
    s_on_synced = on_synced;
    return esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, nullptr, nullptr);
}
} // namespace mosaic::features::clock::sync
