#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#ifdef MOSAIC_UI_GALLERY
#include "ui/gallery/gallery.hpp"
#else
#include "app/screens.hpp"
#include "drivers/leds.hpp"
#include "features/audio_cues/service.hpp"
#include "features/brightness/service.hpp"
#include "features/clock/service.hpp"
#include "features/settings/service.hpp"
#include "features/led_feedback/service.hpp"
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
    // First, so the LEDs stay dark from power-up until a feature sets them.
    const esp_err_t leds_err = mosaic::drivers::leds::start();
    if (leds_err != ESP_OK)
        ESP_LOGE(kTag, "LEDs unavailable: %s", esp_err_to_name(leds_err));
    // Do not erase NVS on initialization errors: it may contain the owner's credentials.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // Before provisioning joins Wi-Fi, so the first join starts time sync.
    const esp_err_t clock_err = mosaic::features::clock::start();
    if (clock_err != ESP_OK)
        ESP_LOGE(kTag, "Clock unavailable: %s", esp_err_to_name(clock_err));
    ESP_LOGI(kTag, "Starting setup display");
    ESP_ERROR_CHECK(mosaic::ui::start(mosaic::app::screens()));
    ESP_LOGI(kTag, "Setup display ready");
    mosaic::features::provisioning::publish_view(
        mosaic::features::provisioning::present(mosaic::features::provisioning::StartupView::starting));

    // Feedback features subscribe once the bus starts; the M3 connection and debug triggers publish after them.
    ESP_ERROR_CHECK(mosaic::events::start());
    if (leds_err == ESP_OK) {
        const esp_err_t feedback_err = mosaic::features::led_feedback::start();
        if (feedback_err != ESP_OK)
            ESP_LOGE(kTag, "LED feedback unavailable: %s", esp_err_to_name(feedback_err));
    }
    const esp_err_t cues_err = mosaic::features::audio_cues::start();
    if (cues_err != ESP_OK)
        ESP_LOGE(kTag, "Audio cues unavailable: %s", esp_err_to_name(cues_err));
    const esp_err_t brightness_err = mosaic::features::brightness::start();
    if (brightness_err != ESP_OK)
        ESP_LOGE(kTag, "Ambient brightness unavailable: %s", esp_err_to_name(brightness_err));
    // After the features it configures; storage failures fall back to defaults.
    const esp_err_t settings_err = mosaic::features::settings::start();
    if (settings_err != ESP_OK)
        ESP_LOGE(kTag, "Settings will not be saved: %s", esp_err_to_name(settings_err));
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
