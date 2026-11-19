#pragma once

#include "../credential.hpp"

#include "esp_err.h"

// The device credential, in its own NVS namespace, apart from the provisioning settings, so
// setup recovery never touches it.
namespace mosaic::features::connection::adapters::credential_store {

// Missing or unusable records give ESP_OK with found=false and `out` empty. Storage failures
// return the error with the same empty outputs. Never erases NVS.
esp_err_t load(Credential& out, bool& found);

// Replaces the saved credential in one write: an interrupted save leaves the old one or the
// new one, never part of each. The caller erases its own copy.
esp_err_t save(const Credential& credential);

} // namespace mosaic::features::connection::adapters::credential_store
