#pragma once

#include "settings.hpp"
#include "esp_err.h"

namespace mosaic::features::settings::store {

// Device settings live in their own NVS namespace, apart from provisioning's
// mosaic_net, so neither can disturb the other. Call after NVS initialization.

// Fills result with the saved settings, or with the defaults: on first boot (nothing
// saved; ESP_OK), when the saved data is not valid (ESP_ERR_INVALID_VERSION), or on a
// storage error (that error). Never erases NVS.
esp_err_t load(Settings& result);
// Writes and commits the settings.
esp_err_t save(const Settings& settings);

}
