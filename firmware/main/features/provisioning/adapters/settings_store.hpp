#pragma once

#include "../settings_validation.hpp"
#include "esp_err.h"

namespace mosaic::features::provisioning::adapters::settings {

// Call after NVS initialization, from the provisioning owner task.
// Reads the versioned mosaic_net/settings blob. Missing or invalid settings
// return ESP_OK with found=false and result cleared, allowing fresh setup.
// Storage failures return an error with the same cleared outputs. Never erases NVS.
esp_err_t load(form::Settings& result, bool& found);

// Writes the blob and clears setup_next in one commit. The caller
// owns validation and the lifetime/erasure of its credential-bearing data.
esp_err_t persist(const form::Settings& data);

// Missing namespace/key defaults to false; storage errors leave the output false.
esp_err_t load_setup_requested(bool& requested);
esp_err_t set_setup_requested(bool requested);

} // namespace mosaic::features::provisioning::adapters::settings
