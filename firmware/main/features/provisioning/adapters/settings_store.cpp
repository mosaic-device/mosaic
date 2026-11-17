#include "settings_store.hpp"
#include "../../../support/secure_erase.hpp"

#include <cstddef>
#include <cstdint>

#include "nvs.h"

namespace mosaic::features::provisioning::adapters::settings {

namespace {

constexpr const char* kNamespace = "mosaic_net";

// The current layout: the settings and the setup flag in one item. ESP-IDF keeps either the old
// or the new value of a single item when a write is interrupted, so the next boot never sees a
// half-finished save.
constexpr const char* kRecordKey = "record";
constexpr uint32_t kRecordMagic = 0x4d4f5352;

// The earlier layout: two items written one after the other. Still read until the next save
// replaces it with a record; never written again.
constexpr const char* kLegacySettingsKey = "settings";
constexpr const char* kLegacyFlagKey = "setup_next";

// Stored as one NVS blob; do not change this layout without migration.
struct Record {
    uint32_t magic = kRecordMagic;
    uint8_t setup_next = 0;
    uint8_t has_settings = 0;
    uint8_t reserved[2] { };
    form::Settings settings { };
};

// What boot decides from: saved settings, if usable, and whether setup opens next.
struct State {
    bool found = false;
    bool setup_next = false;
    form::Settings settings { };
};

// Reads the earlier layout's settings blob. Unusable settings leave found false and the blob in
// place; actual storage failures return an error.
esp_err_t read_legacy_settings(nvs_handle_t handle, State& state) {
    size_t size = sizeof(state.settings);
    esp_err_t err = nvs_get_blob(handle, kLegacySettingsKey, &state.settings, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = ESP_OK;
    else if (err == ESP_OK && size == sizeof(state.settings) && form::valid_settings(state.settings))
        state.found = true;
    else if (err == ESP_ERR_NVS_INVALID_LENGTH)
        err = ESP_OK;
    if (!state.found)
        support::secure_erase(state.settings);
    return err;
}

esp_err_t read_legacy_flag(nvs_handle_t handle, State& state) {
    uint8_t value = 0;
    esp_err_t err = nvs_get_u8(handle, kLegacyFlagKey, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err == ESP_OK)
        state.setup_next = value != 0;
    return err;
}

// Reads the record, or the earlier layout when there is none. On error the state is cleared.
esp_err_t read_state(nvs_handle_t handle, State& state, bool need_settings, bool need_flag) {
    Record record { };
    size_t size = sizeof(record);
    esp_err_t err = nvs_get_blob(handle, kRecordKey, &record, &size);
    if (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH) {
        // A record of the wrong size or kind is unusable: setup opens and nothing is erased.
        if (err == ESP_OK && size == sizeof(record) && record.magic == kRecordMagic) {
            state.setup_next = record.setup_next != 0;
            state.found = record.has_settings != 0 && form::valid_settings(record.settings);
            if (state.found)
                state.settings = record.settings;
        }
        support::secure_erase(record);
        return ESP_OK;
    }
    support::secure_erase(record);
    if (err != ESP_ERR_NVS_NOT_FOUND)
        return err;

    err = need_settings ? read_legacy_settings(handle, state) : ESP_OK;
    if (err == ESP_OK && need_flag)
        err = read_legacy_flag(handle, state);
    if (err != ESP_OK)
        support::secure_erase(state);
    return err;
}

// Opens read-only and reads the state. A missing namespace is an empty state, not an error.
esp_err_t load_state(State& state, bool need_settings, bool need_flag) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (err != ESP_OK)
        return err;
    err = read_state(handle, state, need_settings, need_flag);
    nvs_close(handle);
    return err;
}

// Replaces the whole state with one write. ESP-IDF stores the item as it is set, and commit
// can't fail for an open handle, so the set is the one point where a save can fail.
esp_err_t write_record(nvs_handle_t handle, const State& state) {
    Record record { };
    record.setup_next = state.setup_next ? 1 : 0;
    record.has_settings = state.found ? 1 : 0;
    if (state.found)
        record.settings = state.settings;
    esp_err_t err = nvs_set_blob(handle, kRecordKey, &record, sizeof(record));
    support::secure_erase(record);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    return err;
}

} // namespace

esp_err_t load(form::Settings& result, bool& found) {
    found = false;
    support::secure_erase(result);
    State state { };
    esp_err_t err = load_state(state, true, false);
    if (err == ESP_OK && state.found) {
        result = state.settings;
        found = true;
    }
    support::secure_erase(state);
    return err;
}

esp_err_t persist(const form::Settings& data) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    State state { };
    state.found = true;
    state.settings = data;
    err = write_record(handle, state);
    support::secure_erase(state);
    nvs_close(handle);
    return err;
}

esp_err_t load_setup_requested(bool& requested) {
    requested = false;
    State state { };
    esp_err_t err = load_state(state, false, true);
    if (err == ESP_OK)
        requested = state.setup_next;
    support::secure_erase(state);
    return err;
}

esp_err_t set_setup_requested(bool requested) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    // The saved settings are kept as they are, so setup can be left without losing them.
    State state { };
    err = read_state(handle, state, true, false);
    if (err == ESP_OK) {
        state.setup_next = requested;
        err = write_record(handle, state);
    }
    support::secure_erase(state);
    nvs_close(handle);
    return err;
}

} // namespace mosaic::features::provisioning::adapters::settings
