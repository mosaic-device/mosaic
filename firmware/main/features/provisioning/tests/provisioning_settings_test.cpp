#include "../adapters/settings_store.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "nvs.h"

using mosaic::features::provisioning::form::Settings;
namespace store = mosaic::features::provisioning::adapters::settings;

namespace {
constexpr esp_err_t kReadError = 0x201;
constexpr esp_err_t kWriteError = 0x202;
constexpr esp_err_t kOpenError = 0x204;
constexpr nvs_handle_t kHandle = 42;

// NVS as ESP-IDF behaves: each set stores its item as it happens, so a later failure or power
// loss doesn't undo earlier writes. write_budget limits how many writes succeed; past it every
// write fails and stores nothing, as after a power loss.
struct Fake {
    std::map<std::string, std::vector<unsigned char>> blobs;
    std::map<std::string, uint8_t> flags;
    std::map<std::string, esp_err_t> read_errors;
    esp_err_t open_error = ESP_OK;
    int write_budget = -1;
    // Stores the value, then reports the old copy couldn't be removed, as ESP-IDF can.
    bool remove_failed = false;
    int opens = 0;
    int closes = 0;
    int writes = 0;
    int commits = 0;
    nvs_open_mode_t mode = NVS_READONLY;
} fake;

Settings network(const char* ssid) {
    Settings value { };
    std::strcpy(value.ssid, ssid);
    std::strcpy(value.pass, "secretpass");
    std::strcpy(value.ws_url, "wss://example.org/device");
    return value;
}

const Settings kOld = network("Old network");
const Settings kNew = network("New network");

void reset() {
    fake = Fake { };
}

// The layout devices were saved with before the record: a settings blob and a flag.
void legacy(const Settings& settings, uint8_t flag) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&settings);
    fake.blobs["settings"].assign(bytes, bytes + sizeof(settings));
    fake.flags["setup_next"] = flag;
}

bool cleared(const Settings& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (size_t i = 0; i < sizeof(value); ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

bool same(const Settings& a, const Settings& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

// What the next boot decides from, read the way the provisioning service reads it.
struct Boot {
    bool found;
    Settings settings;
    bool setup;
};

Boot boot() {
    Boot result { false, { }, true };
    assert(store::load(result.settings, result.found) == ESP_OK);
    assert(store::load_setup_requested(result.setup) == ESP_OK);
    return result;
}

bool boots_into_setup(const Boot& state) {
    return !state.found || state.setup;
}

bool uses(const Boot& state, const Settings& settings) {
    return state.found && !state.setup && same(state.settings, settings);
}
} // namespace

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle) {
    assert(std::strcmp(name, "mosaic_net") == 0);
    ++fake.opens;
    fake.mode = mode;
    if (fake.open_error != ESP_OK)
        return fake.open_error;
    if (mode == NVS_READONLY && fake.blobs.empty() && fake.flags.empty())
        return ESP_ERR_NVS_NOT_FOUND;
    *handle = kHandle;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* length) {
    assert(handle == kHandle);
    if (auto error = fake.read_errors.find(key); error != fake.read_errors.end())
        return error->second;
    auto blob = fake.blobs.find(key);
    if (blob == fake.blobs.end())
        return ESP_ERR_NVS_NOT_FOUND;
    if (*length < blob->second.size())
        return ESP_ERR_NVS_INVALID_LENGTH;
    std::memcpy(out, blob->second.data(), blob->second.size());
    *length = blob->second.size();
    return ESP_OK;
}

bool spend_write() {
    if (fake.write_budget == 0)
        return false;
    if (fake.write_budget > 0)
        --fake.write_budget;
    ++fake.writes;
    return true;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t length) {
    assert(handle == kHandle && fake.mode == NVS_READWRITE);
    if (!spend_write())
        return kWriteError;
    const auto* bytes = static_cast<const unsigned char*>(data);
    fake.blobs[key].assign(bytes, bytes + length);
    return fake.remove_failed ? ESP_ERR_NVS_REMOVE_FAILED : ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* value) {
    assert(handle == kHandle);
    if (auto error = fake.read_errors.find(key); error != fake.read_errors.end())
        return error->second;
    auto flag = fake.flags.find(key);
    if (flag == fake.flags.end())
        return ESP_ERR_NVS_NOT_FOUND;
    *value = flag->second;
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
    assert(handle == kHandle && fake.mode == NVS_READWRITE);
    if (!spend_write())
        return kWriteError;
    fake.flags[key] = value;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == kHandle);
    ++fake.commits;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    assert(handle == kHandle);
    ++fake.closes;
}

int main() {
    // Nothing saved: setup opens.
    reset();
    Boot state = boot();
    assert(!state.found && !state.setup && cleared(state.settings));

    // A save is one write, read back by the next boot.
    reset();
    assert(store::persist(kNew) == ESP_OK);
    assert(fake.writes == 1 && fake.commits == 1 && fake.closes == 1 && fake.blobs.count("record") == 1);
    assert(uses(boot(), kNew));

    // First setup: a save that fails, or loses power, at any write leaves setup to open again,
    // never the settings whose save failed.
    for (int budget : { 0, 1 }) {
        reset();
        fake.write_budget = budget;
        const esp_err_t err = store::persist(kNew);
        fake.write_budget = -1;
        if (err == ESP_OK)
            assert(uses(boot(), kNew));
        else
            assert(boots_into_setup(boot()) && !boot().found);
    }

    // ESP-IDF can write the new value, then fail to remove the old copy; NVS finishes on its next
    // init and the next boot uses the new value. That save, and that request, count as done.
    for (bool from_legacy : { true, false }) {
        reset();
        if (from_legacy)
            legacy(kOld, 1);
        fake.remove_failed = true;
        assert(store::persist(kNew) == ESP_OK);
        fake.remove_failed = false;
        assert(uses(boot(), kNew));
    }
    reset();
    legacy(kOld, 0);
    fake.remove_failed = true;
    assert(store::set_setup_requested(true) == ESP_OK);
    fake.remove_failed = false;
    state = boot();
    assert(state.found && same(state.settings, kOld) && state.setup);

    // Devices saved with the earlier layout keep their settings and their setup request.
    for (uint8_t flag : { 0, 1, 2 }) {
        reset();
        legacy(kOld, flag);
        state = boot();
        assert(state.found && same(state.settings, kOld) && state.setup == (flag != 0));
        assert(fake.writes == 0);
    }

    // Recovery, from either layout: an interrupted save keeps the old settings and setup
    // reopens; a finished one uses the new settings.
    for (bool from_legacy : { true, false }) {
        for (int budget : { 0, 1 }) {
            reset();
            legacy(kOld, from_legacy ? 1 : 0);
            if (!from_legacy)
                assert(store::set_setup_requested(true) == ESP_OK);
            fake.write_budget = budget;
            const esp_err_t err = store::persist(kNew);
            fake.write_budget = -1;
            state = boot();
            if (err == ESP_OK) {
                assert(uses(state, kNew));
            } else {
                assert(state.found && same(state.settings, kOld) && state.setup);
            }
        }
    }

    // Asking for setup keeps the saved settings and changes only the flag, in one write.
    reset();
    legacy(kOld, 0);
    assert(store::set_setup_requested(true) == ESP_OK);
    assert(fake.writes == 1);
    state = boot();
    assert(state.found && same(state.settings, kOld) && state.setup);
    assert(store::set_setup_requested(false) == ESP_OK);
    assert(uses(boot(), kOld));

    // An interrupted request changes nothing.
    reset();
    legacy(kOld, 0);
    fake.write_budget = 0;
    assert(store::set_setup_requested(true) == kWriteError);
    fake.write_budget = -1;
    assert(uses(boot(), kOld));

    // A request with nothing saved still opens setup.
    reset();
    assert(store::set_setup_requested(true) == ESP_OK);
    state = boot();
    assert(!state.found && state.setup);

    // Unusable settings in either layout reopen setup without erasing anything.
    {
        reset();
        Settings bad = kOld;
        bad.magic = 0;
        legacy(bad, 0);
        state = boot();
        assert(!state.found && cleared(state.settings) && fake.writes == 0);

        reset();
        legacy(kOld, 0);
        fake.blobs["settings"].pop_back();
        state = boot();
        assert(!state.found && cleared(state.settings));

        reset();
        legacy(kOld, 0);
        fake.blobs["settings"].push_back(0);
        state = boot();
        assert(!state.found && cleared(state.settings) && fake.blobs["settings"].size() == sizeof(Settings) + 1);

        reset();
        assert(store::persist(kNew) == ESP_OK);
        fake.blobs["record"].pop_back();
        state = boot();
        assert(!state.found && !state.setup && cleared(state.settings));

        reset();
        assert(store::persist(kNew) == ESP_OK);
        fake.blobs["record"][0] ^= 0xff;
        state = boot();
        assert(!state.found && cleared(state.settings));

        // Persist doesn't validate the caller's settings; boot does.
        reset();
        assert(store::persist(bad) == ESP_OK);
        assert(!boot().found);
    }

    // Storage failures are reported, with outputs cleared, and nothing is written.
    for (const char* key : { "record", "settings" }) {
        reset();
        legacy(kOld, 1);
        fake.read_errors[key] = kReadError;
        Settings result = kOld;
        bool found = true;
        assert(store::load(result, found) == kReadError);
        assert(!found && cleared(result));
        assert(store::set_setup_requested(true) == kReadError);
        assert(fake.writes == 0);
    }
    reset();
    legacy(kOld, 1);
    fake.read_errors["setup_next"] = kReadError;
    bool requested = true;
    assert(store::load_setup_requested(requested) == kReadError && !requested);

    reset();
    legacy(kOld, 1);
    fake.open_error = kOpenError;
    Settings result = kOld;
    bool found = true;
    assert(store::load(result, found) == kOpenError && !found && cleared(result));
    requested = true;
    assert(store::load_setup_requested(requested) == kOpenError && !requested);
    assert(store::persist(kNew) == kOpenError);
    assert(store::set_setup_requested(true) == kOpenError);
    assert(fake.writes == 0 && fake.closes == 0);
}
