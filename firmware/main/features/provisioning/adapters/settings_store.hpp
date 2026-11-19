#pragma once

#include "../settings_validation.hpp"
#include "esp_err.h"

namespace mosaic::features::provisioning::adapters::settings {

// Settings and the setup-next flag are stored together as one mosaic_net/record blob, so an
// interrupted or failed save leaves the next boot with either the whole old state or the whole
// new one. Devices saved with the earlier layout (a settings blob and a setup_next flag) are
// read as before until their next save writes a record.

// Call after NVS initialization, from the provisioning owner task.
// Missing or invalid settings return ESP_OK with found=false and result cleared, allowing fresh
// setup. Storage failures return an error with the same cleared outputs. Never erases NVS.
esp_err_t load(form::Settings& result, bool& found);

// Saves the settings and clears the setup request in one write. The caller owns validation and
// the lifetime/erasure of its credential-bearing data.
esp_err_t persist(const form::Settings& data);

// Missing namespace/key defaults to false; storage errors leave the output false.
esp_err_t load_setup_requested(bool& requested);
// Keeps the saved settings as they are and changes only the flag, in one write.
esp_err_t set_setup_requested(bool requested);

} // namespace mosaic::features::provisioning::adapters::settings
