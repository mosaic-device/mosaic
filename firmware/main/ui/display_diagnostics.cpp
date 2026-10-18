#include <atomic>
#include <cstdint>

#include "driver/ppa.h"
#include "esp_log.h"

namespace {
std::atomic_uint32_t ppa_started { 0 };
std::atomic_uint32_t ppa_finished { 0 };
std::atomic_uint32_t last_width { 0 };
std::atomic_uint32_t last_height { 0 };
std::atomic_uint32_t buffer_wait_started { 0 };
std::atomic_uint32_t buffer_wait_finished { 0 };
} // namespace

extern "C" esp_err_t __real_ppa_do_scale_rotate_mirror(ppa_client_handle_t client, const ppa_srm_oper_config_t* config);

extern "C" esp_err_t __wrap_ppa_do_scale_rotate_mirror(
    ppa_client_handle_t client, const ppa_srm_oper_config_t* config) {
    last_width.store(config->in.block_w, std::memory_order_relaxed);
    last_height.store(config->in.block_h, std::memory_order_relaxed);
    ppa_started.fetch_add(1, std::memory_order_relaxed);
    const esp_err_t result = __real_ppa_do_scale_rotate_mirror(client, config);
    ppa_finished.fetch_add(1, std::memory_order_relaxed);
    return result;
}

// The adapter's private function takes and returns pointers; keep the diagnostic
// hook opaque rather than depending on its internal header or struct layout.
extern "C" void* __real_display_bridge_pipeline_wait_free_buf(void* pipeline);

extern "C" void* __wrap_display_bridge_pipeline_wait_free_buf(void* pipeline) {
    buffer_wait_started.fetch_add(1, std::memory_order_relaxed);
    void* const result = __real_display_bridge_pipeline_wait_free_buf(pipeline);
    buffer_wait_finished.fetch_add(1, std::memory_order_relaxed);
    return result;
}

namespace mosaic::ui {
void log_display_diagnostics() {
    ESP_LOGE("mosaic_display",
        "Refresh stalled: PPA entered=%lu returned=%lu last=%lux%lu; buffer wait entered=%lu returned=%lu",
        static_cast<unsigned long>(ppa_started.load()), static_cast<unsigned long>(ppa_finished.load()),
        static_cast<unsigned long>(last_width.load()), static_cast<unsigned long>(last_height.load()),
        static_cast<unsigned long>(buffer_wait_started.load()),
        static_cast<unsigned long>(buffer_wait_finished.load()));
}
} // namespace mosaic::ui
