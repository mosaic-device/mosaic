#include "../settings.hpp"
#include "../store.hpp"

#include <cassert>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "nvs.h"

using namespace mosaic::features::settings;

namespace {
constexpr esp_err_t kStorageError = 0x1234;
constexpr nvs_handle_t kHandle = 7;

// In-memory NVS holding one blob, with failures to inject.
struct Fake {
    bool has_namespace = false;
    bool has_blob = false;
    std::vector<uint8_t> blob;
    // Blob writes not yet committed.
    std::vector<uint8_t> pending;
    bool pending_write = false;
    esp_err_t open_error = ESP_OK;
    esp_err_t read_error = ESP_OK;
    esp_err_t write_error = ESP_OK;
    esp_err_t commit_error = ESP_OK;
    std::string opened;
    int open_handles = 0;
} fake;
} // namespace

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle) {
    fake.opened = name;
    if (fake.open_error != ESP_OK)
        return fake.open_error;
    if (!fake.has_namespace && mode == NVS_READONLY)
        return ESP_ERR_NVS_NOT_FOUND;
    fake.has_namespace = true;
    ++fake.open_handles;
    *handle = kHandle;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* length) {
    assert(handle == kHandle && std::string(key) == "settings");
    if (fake.read_error != ESP_OK)
        return fake.read_error;
    if (!fake.has_blob)
        return ESP_ERR_NVS_NOT_FOUND;
    if (*length < fake.blob.size())
        return ESP_ERR_NVS_INVALID_LENGTH;
    std::memcpy(out, fake.blob.data(), fake.blob.size());
    *length = fake.blob.size();
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t length) {
    assert(handle == kHandle && std::string(key) == "settings");
    if (fake.write_error != ESP_OK)
        return fake.write_error;
    const auto* bytes = static_cast<const uint8_t*>(data);
    fake.pending.assign(bytes, bytes + length);
    fake.pending_write = true;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == kHandle);
    if (fake.commit_error != ESP_OK)
        return fake.commit_error;
    if (fake.pending_write) {
        fake.blob = fake.pending;
        fake.has_blob = true;
        fake.pending_write = false;
    }
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) {
    assert(handle == kHandle);
    --fake.open_handles;
    fake.pending_write = false; // Uncommitted writes are lost.
}

namespace {
constexpr Settings kCustom {
    .auto_brightness = false,
    .brightness = 50,
    .volume = 30,
    .muted = true,
    .leds_on = false,
};

void reset() {
    fake = Fake { };
}

void defaults() {
    const Settings settings;
    assert(settings.auto_brightness && settings.brightness == 70);
    assert(settings.volume == mosaic::features::audio_cues::kDefaultVolume);
    assert(!settings.muted && settings.leds_on);
    // Defaults are themselves valid stored settings.
    assert(decode(encode(settings)) == settings);
}

void changes_and_availability() {
    Settings settings;
    // Manual brightness is unavailable, and does nothing, while brightness is automatic.
    assert(!available(settings, Change::brighter) && !available(settings, Change::dimmer));
    assert(apply(settings, Change::brighter) == settings);
    settings = apply(settings, Change::auto_brightness);
    assert(!settings.auto_brightness);
    assert(apply(settings, Change::brighter).brightness == 80);
    assert(apply(settings, Change::dimmer).brightness == 60);
    // Steps stop at the ends of each range, and become unavailable there.
    for (int i = 0; i < 20; ++i)
        settings = apply(settings, Change::brighter);
    assert(settings.brightness == kBrightnessMax && !available(settings, Change::brighter));
    for (int i = 0; i < 20; ++i)
        settings = apply(settings, Change::dimmer);
    assert(settings.brightness == kBrightnessMin && !available(settings, Change::dimmer));
    for (int i = 0; i < 20; ++i)
        settings = apply(settings, Change::quieter);
    assert(settings.volume == 0 && !available(settings, Change::quieter));
    for (int i = 0; i < 20; ++i)
        settings = apply(settings, Change::louder);
    assert(settings.volume == 100 && !available(settings, Change::louder));
    // Toggles are always available and flip back.
    for (Change toggle : { Change::auto_brightness, Change::mute, Change::leds }) {
        assert(available(settings, toggle));
        assert(apply(apply(settings, toggle), toggle) == settings);
        assert(!(apply(settings, toggle) == settings));
    }
    // Muting keeps the volume for when sound comes back.
    assert(apply(settings, Change::mute).volume == settings.volume);
}

void stored_form_round_trips_and_rejects_anything_else() {
    assert(decode(encode(kCustom)) == kCustom);
    const auto good = encode(kCustom);
    const auto rejected = [&](size_t index, uint8_t value) {
        auto bad = good;
        bad[index] = value;
        return !decode(bad);
    };
    assert(rejected(0, 2)); // Another version.
    assert(rejected(1, 2)); // A flag that is not 0 or 1.
    assert(rejected(2, kBrightnessMin - 1));
    assert(rejected(2, kBrightnessMax + 1));
    assert(rejected(3, 101));
    assert(rejected(4, 7));
    assert(rejected(5, 0xFF));
    assert(!decode(std::span<const uint8_t>(good.data(), good.size() - 1)));
    assert(!decode({ }));
}

void first_boot_loads_defaults() {
    reset();
    Settings loaded = kCustom;
    assert(store::load(loaded) == ESP_OK && loaded == Settings { });
    // The namespace exists but nothing was saved yet.
    fake.has_namespace = true;
    loaded = kCustom;
    assert(store::load(loaded) == ESP_OK && loaded == Settings { });
}

void saved_settings_load_after_restart() {
    reset();
    assert(store::save(kCustom) == ESP_OK);
    // Its own namespace, never provisioning's.
    assert(fake.opened == "mosaic_device");
    Settings loaded;
    assert(store::load(loaded) == ESP_OK && loaded == kCustom);
    assert(fake.opened == "mosaic_device");
    // A later save replaces it.
    Settings louder = kCustom;
    louder.volume = 90;
    assert(store::save(louder) == ESP_OK);
    assert(store::load(loaded) == ESP_OK && loaded == louder);
    assert(fake.open_handles == 0);
}

void invalid_saved_data_loads_defaults() {
    for (const std::vector<uint8_t>& blob : {
             std::vector<uint8_t> { 9, 1, 70, 60, 0, 1 }, // Unknown version.
             std::vector<uint8_t> { 1, 1, 70 }, // Too short.
             std::vector<uint8_t>(32, 1), // Too long for this layout.
             std::vector<uint8_t> { 1, 1, 200, 60, 0, 1 }, // Out of range.
         }) {
        reset();
        fake.has_namespace = true;
        fake.has_blob = true;
        fake.blob = blob;
        Settings loaded = kCustom;
        assert(store::load(loaded) == ESP_ERR_INVALID_VERSION && loaded == Settings { });
        assert(fake.open_handles == 0);
    }
}

void storage_failures_load_defaults_and_report() {
    reset();
    store::save(kCustom);
    fake.open_error = kStorageError;
    Settings loaded = kCustom;
    assert(store::load(loaded) == kStorageError && loaded == Settings { });
    fake.open_error = ESP_OK;
    fake.read_error = kStorageError;
    loaded = kCustom;
    assert(store::load(loaded) == kStorageError && loaded == Settings { });
    assert(fake.open_handles == 0);
}

void save_failures_are_reported_and_keep_the_old_settings() {
    for (esp_err_t Fake::* failure : { &Fake::open_error, &Fake::write_error, &Fake::commit_error }) {
        reset();
        assert(store::save(Settings { }) == ESP_OK);
        fake.*failure = kStorageError;
        assert(store::save(kCustom) == kStorageError);
        fake.*failure = ESP_OK;
        Settings loaded = kCustom;
        assert(store::load(loaded) == ESP_OK && loaded == Settings { });
        assert(fake.open_handles == 0);
    }
}
} // namespace

int main() {
    defaults();
    changes_and_availability();
    stored_form_round_trips_and_rejects_anything_else();
    first_boot_loads_defaults();
    saved_settings_load_after_restart();
    invalid_saved_data_loads_defaults();
    storage_failures_load_defaults_and_report();
    save_failures_are_reported_and_keep_the_old_settings();
}
