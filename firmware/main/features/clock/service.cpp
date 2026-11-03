#include "service.hpp"
#include "sync.hpp"
#include <atomic>
#include <cstdlib>
#include <ctime>
#include "esp_check.h"
#include "esp_log.h"
namespace mosaic::features::clock {
namespace {
    constexpr const char* kTag = "mosaic_clock";

    // Set once SNTP has set the time this boot. Time kept across a restart without
    // Wi-Fi is not trusted, so this starts false on every boot.
    std::atomic_bool s_synced = false;

    void on_synced() {
        if (!s_synced.exchange(true))
            ESP_LOGI(kTag, "Time synced");
    }
}
esp_err_t start() {
    // POSIX rules for the fixed zone; localtime_r() follows them from here on.
    if (setenv("TZ", kTimeZone, 1) != 0)
        return ESP_FAIL;
    tzset();
    ESP_RETURN_ON_ERROR(sync::start(on_synced), kTag, "Time sync");
    ESP_LOGI(kTag, "Clock waiting for Wi-Fi; time zone %s", kTimeZone);
    return ESP_OK;
}
std::string_view now_text(std::span<char, kTextSize> buffer) {
    return text(s_synced.load(), std::time(nullptr), buffer);
}
std::string_view today_text(std::span<char, kDateSize> buffer) {
    return date(s_synced.load(), std::time(nullptr), buffer);
}
}
