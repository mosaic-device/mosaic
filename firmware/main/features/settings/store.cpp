#include "store.hpp"

#include "nvs.h"

namespace mosaic::features::settings::store {
namespace {
    constexpr char kNamespace[] = "mosaic_device";
    constexpr char kKey[] = "settings";
} // namespace

esp_err_t load(Settings& result) {
    result = Settings { };
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK; // First boot: the namespace does not exist yet.
    if (err != ESP_OK)
        return err;
    std::array<uint8_t, kStoredSize> stored { };
    size_t size = stored.size();
    err = nvs_get_blob(handle, kKey, stored.data(), &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    // A blob of another size (an older or newer layout) is not valid here.
    if (err == ESP_ERR_NVS_INVALID_LENGTH)
        return ESP_ERR_INVALID_VERSION;
    if (err != ESP_OK)
        return err;
    const auto decoded = decode(std::span<const uint8_t>(stored.data(), size));
    if (!decoded)
        return ESP_ERR_INVALID_VERSION;
    result = *decoded;
    return ESP_OK;
}

esp_err_t save(const Settings& settings) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    const auto stored = encode(settings);
    err = nvs_set_blob(handle, kKey, stored.data(), stored.size());
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

}
