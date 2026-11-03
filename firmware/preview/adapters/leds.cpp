// The ring and strip as frames the hardware panel draws.
#include "../device.hpp"
#include "drivers/leds.hpp"
#include "esp_log.h"
#include "esp_system.h"

#include <algorithm>
#include <mutex>

namespace mosaic::drivers::leds {
namespace {
    constexpr const char* kTag = "mosaic_leds";
    std::mutex s_lock;
    bool s_started = false;
    bool s_shut_down = false;
    std::array<Color, kRingLength> s_ring_frame { };
    std::array<Color, kStripLength> s_strip_frame { };
    uint8_t s_level = kFullLevel;

    void blank_on_shutdown() {
        const std::lock_guard lock(s_lock);
        s_shut_down = true;
    }
}
esp_err_t start() {
    const std::lock_guard lock(s_lock);
    if (!s_started && esp_register_shutdown_handler(blank_on_shutdown) != ESP_OK)
        return ESP_FAIL;
    s_started = true;
    ESP_LOGI(kTag, "LEDs ready: ring (%u), strip (%u), ceiling %u/255", static_cast<unsigned>(kRingLength),
        static_cast<unsigned>(kStripLength), kCeiling);
    return ESP_OK;
}
esp_err_t show(std::span<const Color, kRingLength> ring, std::span<const Color, kStripLength> strip) {
    const std::lock_guard lock(s_lock);
    if (!s_started)
        return ESP_ERR_INVALID_STATE;
    std::ranges::copy(ring, s_ring_frame.begin());
    std::ranges::copy(strip, s_strip_frame.begin());
    return ESP_OK;
}
esp_err_t set_level(uint8_t level) {
    const std::lock_guard lock(s_lock);
    if (!s_started)
        return ESP_ERR_INVALID_STATE;
    s_level = level;
    return ESP_OK;
}
} // namespace mosaic::drivers::leds

namespace mosaic::preview::device {
Leds leds() {
    namespace leds = drivers::leds;
    const std::lock_guard lock(leds::s_lock);
    Leds shown { { }, { }, leds::s_level };
    if (leds::s_shut_down)
        return shown;
    // The same limit the device's driver applies to every pixel it sends.
    for (size_t i = 0; i < shown.ring.size(); ++i)
        shown.ring[i] = leds::limit(leds::s_ring_frame[i], leds::s_level);
    for (size_t i = 0; i < shown.strip.size(); ++i)
        shown.strip[i] = leds::limit(leds::s_strip_frame[i], leds::s_level);
    return shown;
}
} // namespace mosaic::preview::device
