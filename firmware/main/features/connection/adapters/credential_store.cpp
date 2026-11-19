#include "credential_store.hpp"
#include "../../../support/secure_erase.hpp"

#include <cstdint>

#include "nvs.h"

namespace mosaic::features::connection::adapters::credential_store {
namespace {
    constexpr const char* kNamespace = "mosaic_device";
    constexpr const char* kKey = "credential";
    constexpr uint32_t kMagic = 0x4d444331; // "MDC1"

    // Stored as one NVS blob; do not change this layout without migration.
    struct Record {
        uint32_t magic = kMagic;
        uint8_t paired = 0;
        uint8_t reserved[3] { };
        std::array<uint8_t, kSecretSize> secret { };
        std::array<char, kUrlCapacity> url { };
    };
} // namespace

esp_err_t load(Credential& out, bool& found) {
    found = false;
    support::secure_erase(out);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err != ESP_OK)
        return err;
    Record record { };
    size_t size = sizeof(record);
    err = nvs_get_blob(handle, kKey, &record, &size);
    nvs_close(handle);
    // Nothing saved, or a record of another size or kind: the device makes a new credential.
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH)
        err = ESP_OK;
    else if (err == ESP_OK && size == sizeof(record) && record.magic == kMagic && record.url.back() == '\0') {
        out.secret = record.secret;
        out.url = record.url;
        out.paired = record.paired != 0;
        found = true;
    }
    support::secure_erase(record);
    return err;
}

esp_err_t save(const Credential& credential) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    Record record { };
    record.paired = credential.paired ? 1 : 0;
    record.secret = credential.secret;
    record.url = credential.url;
    err = nvs_set_blob(handle, kKey, &record, sizeof(record));
    support::secure_erase(record);
    // The new value is written but the old copy wasn't removed; NVS finishes on its next init
    // and reads already return the new value.
    if (err == ESP_ERR_NVS_REMOVE_FAILED)
        err = ESP_OK;
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

} // namespace mosaic::features::connection::adapters::credential_store
