#pragma once
// Host stand-in for ESP-IDF's esp_err.h: the same codes, so firmware code compares and
// reports them unchanged.
#include <cstdint>

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_VERSION 0x10A
#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_READ_ONLY (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_WIFI_BASE 0x3000
#define ESP_ERR_WIFI_NOT_STARTED (ESP_ERR_WIFI_BASE + 7)

const char* esp_err_to_name(esp_err_t code);

[[noreturn]] void mosaic_preview_error_check_failed(esp_err_t code, const char* expression, const char* file, int line);

#define ESP_ERROR_CHECK(x)                                                                                             \
    do {                                                                                                               \
        const esp_err_t mosaic_err_ = (x);                                                                             \
        if (mosaic_err_ != ESP_OK)                                                                                     \
            mosaic_preview_error_check_failed(mosaic_err_, #x, __FILE__, __LINE__);                                    \
    } while (0)
