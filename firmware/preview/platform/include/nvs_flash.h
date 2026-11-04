#pragma once
#include "esp_err.h"
// Loads the preview's NVS file; a missing file is an erased partition.
esp_err_t nvs_flash_init();
