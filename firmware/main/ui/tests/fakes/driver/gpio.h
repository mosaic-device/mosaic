#pragma once
#include "esp_err.h"
#include <cstdint>
constexpr int GPIO_MODE_OUTPUT = 1;
struct gpio_config_t {
    uint64_t pin_bit_mask;
    int mode;
};
esp_err_t gpio_set_level(int, int);
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_hold_en(int);
esp_err_t gpio_hold_dis(int);
