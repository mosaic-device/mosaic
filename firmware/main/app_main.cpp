#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#ifdef MOSAIC_UI_GALLERY
#include "ui/gallery/gallery.hpp"
#else
#include "features/provisioning/service.hpp"
#include "ui/runtime.hpp"
#include "features/provisioning/ui/renderer.hpp"
#include "features/provisioning/presentation/presenter.hpp"
#include "events/bus.hpp"
#ifdef MOSAIC_DEBUG_TRIGGERS
#include "features/debug_triggers/console.hpp"
#endif
#endif

namespace {
constexpr char kTag[] = "mosaic";
}

extern "C" void app_main() {
#ifdef MOSAIC_UI_GALLERY
    ESP_LOGW(kTag, "Development gallery: provisioning and networking are not started");
    ESP_ERROR_CHECK(mosaic::ui::start(mosaic::ui::gallery::renderer_callbacks()));
#else
    // Do not erase NVS on initialization errors: it may contain the owner's credentials.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(kTag, "Starting setup display");
    ESP_ERROR_CHECK(mosaic::ui::start(mosaic::features::provisioning::renderer_callbacks()));
    ESP_LOGI(kTag, "Setup display ready");
    mosaic::features::provisioning::publish_view(
        mosaic::features::provisioning::present(mosaic::features::provisioning::StartupView::starting));

    // Feedback features subscribe before this; the M3 connection and debug triggers publish after it.
    ESP_ERROR_CHECK(mosaic::events::start());
#ifdef MOSAIC_DEBUG_TRIGGERS
    ESP_LOGW(kTag, "Development debug triggers enabled");
    ESP_ERROR_CHECK(mosaic::features::debug_triggers::start());
#endif

    ESP_LOGI(kTag, "Starting provisioning service");
    const esp_err_t err = mosaic::features::provisioning::start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Provisioning service failed: %s", esp_err_to_name(err));
        // The service publishes failure with its current setup session.
    }
#endif
}
