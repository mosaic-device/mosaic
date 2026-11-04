#pragma once
#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_ERROR(x, log_tag, format, ...)                                                                   \
    do {                                                                                                               \
        const esp_err_t mosaic_err_ = (x);                                                                             \
        if (mosaic_err_ != ESP_OK) {                                                                                   \
            ESP_LOGE(log_tag, "%s(%d): " format, __func__, __LINE__ __VA_OPT__(, ) __VA_ARGS__);                       \
            return mosaic_err_;                                                                                        \
        }                                                                                                              \
    } while (0)
