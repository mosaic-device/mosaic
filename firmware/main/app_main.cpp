#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "features/provisioning/service.hpp"
#include "ui/runtime.hpp"
#include "features/provisioning/ui/renderer.hpp"
#include "features/provisioning/presentation/presenter.hpp"

namespace {
constexpr char kTag[] = "mosaic";
}

extern "C" void app_main() {
    // Do not erase NVS on initialization errors: it may contain the owner's credentials.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(kTag, "Starting setup display");
    ESP_ERROR_CHECK(mosaic::ui::start(mosaic::features::provisioning::renderer_callbacks()));
    ESP_LOGI(kTag, "Setup display ready");
    mosaic::features::provisioning::publish_view(
        mosaic::features::provisioning::present(mosaic::features::provisioning::StartupView::starting));

    ESP_LOGI(kTag, "Starting provisioning service");
    const esp_err_t err = mosaic::features::provisioning::start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Provisioning service failed: %s", esp_err_to_name(err));
        // The service publishes failure with its current setup session.
    }
}
