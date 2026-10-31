#include "service.hpp"
#include "store.hpp"
#include "../audio_cues/service.hpp"
#include "../brightness/service.hpp"
#include "../led_feedback/service.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
namespace mosaic::features::settings {
namespace {
    constexpr const char* kTag = "mosaic_settings";
    // Quiet time after the last change before saving.
    constexpr TickType_t kSaveDelay = pdMS_TO_TICKS(1000);

    portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
    Settings s_current; // s_mux
    TaskHandle_t s_saver = nullptr;

    void apply_to_features(const Settings& settings) {
        brightness::set_manual(settings.auto_brightness ? std::nullopt : std::optional<uint8_t>(settings.brightness));
        audio_cues::set_volume(settings.volume);
        audio_cues::set_muted(settings.muted);
        led_feedback::set_enabled(settings.leds_on);
    }
    void save_later(void*) {
        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            // Each further change restarts the wait.
            while (ulTaskNotifyTake(pdTRUE, kSaveDelay) > 0) { }
            const esp_err_t err = store::save(current());
            if (err != ESP_OK)
                ESP_LOGE(kTag, "Cannot save settings (%s); they apply until restart", esp_err_to_name(err));
        }
    }
}
esp_err_t start() {
    Settings loaded;
    const esp_err_t err = store::load(loaded);
    if (err == ESP_ERR_INVALID_VERSION)
        ESP_LOGW(kTag, "Saved settings are not valid; using defaults");
    else if (err != ESP_OK)
        ESP_LOGW(kTag, "Cannot read settings (%s); using defaults", esp_err_to_name(err));
    portENTER_CRITICAL(&s_mux);
    s_current = loaded;
    portEXIT_CRITICAL(&s_mux);
    apply_to_features(loaded);
    if (xTaskCreate(save_later, "mosaic_settings", 3072, nullptr, 2, &s_saver) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
Settings current() {
    portENTER_CRITICAL(&s_mux);
    const Settings settings = s_current;
    portEXIT_CRITICAL(&s_mux);
    return settings;
}
namespace {
    // Replaces the settings in one step, then applies and saves them if they changed.
    template <class Update> void update(Update next) {
        portENTER_CRITICAL(&s_mux);
        const Settings before = s_current;
        s_current = next(before);
        const Settings after = s_current;
        portEXIT_CRITICAL(&s_mux);
        if (after == before)
            return;
        apply_to_features(after);
        if (s_saver)
            xTaskNotifyGive(s_saver);
    }
}
void set(const Settings& settings) {
    update([&](const Settings&) { return settings; });
}
void change(Change change) {
    update([change](const Settings& before) { return apply(before, change); });
}
}
