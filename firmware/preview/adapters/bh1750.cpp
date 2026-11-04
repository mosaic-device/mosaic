// The ambient light sensor, set from the hardware panel or the lux command.
#include "../device.hpp"
#include "drivers/bh1750.hpp"

#include <mutex>

namespace {
std::mutex g_lock;
std::optional<float> g_lux = 150.0F;
}

namespace mosaic::drivers::bh1750 {
esp_err_t start() {
    const std::lock_guard lock(g_lock);
    return g_lux ? ESP_OK : ESP_ERR_NOT_FOUND;
}
esp_err_t read(float& lux) {
    const std::lock_guard lock(g_lock);
    if (!g_lux)
        return ESP_ERR_TIMEOUT;
    lux = *g_lux;
    return ESP_OK;
}
} // namespace mosaic::drivers::bh1750

namespace mosaic::preview::device {
void set_lux(std::optional<float> lux) {
    const std::lock_guard lock(g_lock);
    g_lux = lux;
}
std::optional<float> lux() {
    const std::lock_guard lock(g_lock);
    return g_lux;
}
} // namespace mosaic::preview::device
