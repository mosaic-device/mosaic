#include "leds.hpp"
#include "led_strip.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/semphr.h"
namespace mosaic::drivers::leds {
namespace {
    constexpr const char* kTag = "mosaic_leds";
    // WS2812 data, driven directly from 3.3V GPIO through a series resistor.
    constexpr gpio_num_t kRingPin = GPIO_NUM_28;
    constexpr gpio_num_t kStripPin = GPIO_NUM_29;
    constexpr TickType_t kShutdownWait = pdMS_TO_TICKS(100);

    led_strip_handle_t s_ring = nullptr;
    led_strip_handle_t s_strip = nullptr;
    // Serializes frames with the restart blanking, which runs on the restarting task.
    SemaphoreHandle_t s_lock = nullptr;
    bool s_shut_down = false;

    esp_err_t create(gpio_num_t pin, size_t length, led_strip_handle_t* handle) {
        led_strip_config_t config { };
        config.strip_gpio_num = pin;
        config.max_leds = length;
        config.led_model = LED_MODEL_WS2812;
        config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
        led_strip_rmt_config_t rmt { };
        rmt.clk_src = RMT_CLK_SRC_DEFAULT;
        ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&config, &rmt, handle), kTag, "RMT device on GPIO %d", pin);
        return led_strip_clear(*handle);
    }
    template <size_t N> esp_err_t write(led_strip_handle_t handle, std::span<const Color, N> colors) {
        for (size_t i = 0; i < N; ++i) {
            const Color color = limit(colors[i]);
            ESP_RETURN_ON_ERROR(led_strip_set_pixel(handle, i, color.red, color.green, color.blue), kTag, "Pixel");
        }
        return led_strip_refresh(handle);
    }
    void blank_on_shutdown() {
        // Without this the LEDs hold their last frame through the restart. If a frame
        // is stuck, blank anyway: the restart stops its task.
        const bool locked = xSemaphoreTake(s_lock, kShutdownWait) == pdTRUE;
        s_shut_down = true;
        for (led_strip_handle_t handle : { s_ring, s_strip }) {
            const esp_err_t err = led_strip_clear(handle);
            if (err != ESP_OK)
                ESP_LOGE(kTag, "Cannot blank LEDs on restart: %s", esp_err_to_name(err));
        }
        if (locked)
            xSemaphoreGive(s_lock);
    }
}
esp_err_t start() {
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == nullptr)
        return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(create(kRingPin, kRingLength, &s_ring), kTag, "Cannot start ring");
    ESP_RETURN_ON_ERROR(create(kStripPin, kStripLength, &s_strip), kTag, "Cannot start strip");
    ESP_RETURN_ON_ERROR(esp_register_shutdown_handler(blank_on_shutdown), kTag, "Restart blanking");
    ESP_LOGI(kTag, "LEDs ready: ring GPIO %d (%u), strip GPIO %d (%u), ceiling %u/255", kRingPin,
        static_cast<unsigned>(kRingLength), kStripPin, static_cast<unsigned>(kStripLength), kCeiling);
    return ESP_OK;
}
esp_err_t show(std::span<const Color, kRingLength> ring, std::span<const Color, kStripLength> strip) {
    if (s_ring == nullptr || s_strip == nullptr)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (!s_shut_down) {
        err = write(s_ring, ring);
        if (err == ESP_OK)
            err = write(s_strip, strip);
    }
    xSemaphoreGive(s_lock);
    return err;
}
}
