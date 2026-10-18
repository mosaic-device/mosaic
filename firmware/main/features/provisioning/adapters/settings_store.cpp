#include "settings_store.hpp"
#include "../../../support/secure_erase.hpp"

#include <cstddef>

#include "nvs.h"

namespace mosaic::features::provisioning::adapters::settings {

esp_err_t load(form::Settings& result, bool& found) {
    found = false;
    support::secure_erase(result);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("mosaic_net", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err != ESP_OK)
        return err;
    size_t size = sizeof(result);
    err = nvs_get_blob(handle, "settings", &result, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err != ESP_OK || size != sizeof(result) || !form::valid_settings(result)) {
        support::secure_erase(result);
        // Unusable settings reopen setup without deleting the stored blob.
        // Actual storage failures still prevent startup.
        return err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH ? ESP_OK : err;
    }
    found = true;
    return ESP_OK;
}

esp_err_t persist(const form::Settings& data) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("mosaic_net", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = nvs_set_blob(handle, "settings", &data, sizeof(data));
    if (err == ESP_OK)
        err = nvs_set_u8(handle, "setup_next", 0);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t load_setup_requested(bool& requested) {
    requested = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("mosaic_net", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err != ESP_OK)
        return err;
    uint8_t value = 0;
    err = nvs_get_u8(handle, "setup_next", &value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err == ESP_OK)
        requested = value != 0;
    return err;
}

esp_err_t set_setup_requested(bool requested) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("mosaic_net", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = nvs_set_u8(handle, "setup_next", requested ? 1 : 0);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

} // namespace mosaic::features::provisioning::adapters::settings
