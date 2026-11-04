#pragma once
// Host stand-in for ESP-IDF logging: "I (ms) tag: message" lines on stdout.
#include <cstdarg>

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

// Tags log at Info and above unless raised here, as on the device.
void esp_log_level_set(const char* tag, esp_log_level_t level);
#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
void esp_log_write(esp_log_level_t level, const char* tag, const char* format, ...);

#define ESP_LOGE(tag, format, ...) esp_log_write(ESP_LOG_ERROR, tag, format __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGD(tag, format, ...) esp_log_write(ESP_LOG_DEBUG, tag, format __VA_OPT__(, ) __VA_ARGS__)
#define ESP_LOGV(tag, format, ...) esp_log_write(ESP_LOG_VERBOSE, tag, format __VA_OPT__(, ) __VA_ARGS__)
