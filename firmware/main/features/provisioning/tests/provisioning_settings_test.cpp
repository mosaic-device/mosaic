#include "../adapters/settings_store.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <initializer_list>

#include "nvs.h"

using mosaic::features::provisioning::form::Settings;
namespace store = mosaic::features::provisioning::adapters::settings;

namespace {
constexpr esp_err_t kReadError = 0x201;
constexpr esp_err_t kWriteError = 0x202;
constexpr esp_err_t kCommitError = 0x203;
constexpr esp_err_t kOpenError = 0x204;
constexpr nvs_handle_t kHandle = 42;

struct Fake {
    esp_err_t open_error = ESP_OK;
    esp_err_t read_error = ESP_OK;
    esp_err_t write_error = ESP_OK;
    esp_err_t commit_error = ESP_OK;
    esp_err_t flag_read_error = ESP_OK;
    esp_err_t flag_write_error = ESP_OK;
    uint8_t flag = 0;
    int flag_reads = 0;
    int flag_writes = 0;
    Settings stored { };
    size_t blob_size = sizeof(Settings);
    bool partial_read = false;
    int opens = 0;
    int reads = 0;
    int writes = 0;
    int commits = 0;
    int closes = 0;
    nvs_open_mode_t mode = NVS_READONLY;
} fake;

Settings valid() {
    Settings value { };
    std::strcpy(value.ssid, "Test network");
    std::strcpy(value.pass, "secretpass");
    std::strcpy(value.ws_url, "wss://example.org/device");
    return value;
}

void reset() {
    fake = Fake { };
    fake.stored = valid();
}

bool cleared(const Settings& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (size_t i = 0; i < sizeof(value); ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}
} // namespace

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle) {
    assert(std::strcmp(name, "mosaic_net") == 0);
    ++fake.opens;
    fake.mode = mode;
    if (fake.open_error == ESP_OK)
        *handle = kHandle;
    return fake.open_error;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* length) {
    assert(handle == kHandle && std::strcmp(key, "settings") == 0);
    assert(*length == sizeof(Settings));
    ++fake.reads;
    if (fake.read_error == ESP_OK || fake.partial_read) {
        const size_t count = fake.blob_size < sizeof(Settings) ? fake.blob_size : sizeof(Settings);
        std::memcpy(out, &fake.stored, count);
        *length = fake.blob_size;
    }
    return fake.read_error;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* data, size_t length) {
    assert(handle == kHandle && std::strcmp(key, "settings") == 0);
    assert(length == sizeof(Settings));
    assert(std::memcmp(data, &fake.stored, length) == 0);
    ++fake.writes;
    return fake.write_error;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* value) {
    assert(handle == kHandle && std::strcmp(key, "setup_next") == 0);
    ++fake.flag_reads;
    *value = fake.flag;
    return fake.flag_read_error;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
    assert(handle == kHandle && std::strcmp(key, "setup_next") == 0);
    ++fake.flag_writes;
    if (fake.flag_write_error == ESP_OK)
        fake.flag = value;
    return fake.flag_write_error;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == kHandle);
    ++fake.commits;
    return fake.commit_error;
}

void nvs_close(nvs_handle_t handle) {
    assert(handle == kHandle);
    ++fake.closes;
}

int main() {
    reset();
    Settings result { };
    bool found = false;
    assert(store::load(result, found) == ESP_OK);
    assert(found && std::memcmp(&result, &fake.stored, sizeof(result)) == 0);
    assert(fake.mode == NVS_READONLY && fake.opens == 1 && fake.reads == 1 && fake.closes == 1);

    reset();
    fake.open_error = ESP_ERR_NVS_NOT_FOUND;
    result = valid();
    found = true;
    assert(store::load(result, found) == ESP_OK);
    assert(!found && cleared(result));
    assert(fake.reads == 0 && fake.closes == 0);

    reset();
    fake.open_error = kOpenError;
    result = valid();
    assert(store::load(result, found) == kOpenError);
    assert(!found && cleared(result));
    assert(fake.reads == 0 && fake.closes == 0);

    reset();
    fake.read_error = ESP_ERR_NVS_NOT_FOUND;
    result = valid();
    assert(store::load(result, found) == ESP_OK);
    assert(!found && cleared(result));
    assert(fake.closes == 1);

    reset();
    fake.read_error = kReadError;
    fake.partial_read = true;
    result = valid();
    assert(store::load(result, found) == kReadError);
    assert(!found && cleared(result) && fake.closes == 1);

    // Failed reads clear a stale found flag as well as partial credentials.
    reset();
    fake.read_error = kReadError;
    result = valid();
    found = true;
    assert(store::load(result, found) == kReadError);
    assert(!found && cleared(result));
    found = false;

    reset();
    fake.blob_size = sizeof(Settings) - 1;
    result = valid();
    assert(store::load(result, found) == ESP_OK);
    assert(!found && cleared(result) && fake.closes == 1);

    reset();
    fake.stored.magic = 0;
    result = valid();
    assert(store::load(result, found) == ESP_OK);
    assert(!found && cleared(result) && fake.closes == 1);

    reset();
    fake.stored.pass[0] = 0;
    fake.stored.ssid[0] = 0;
    result = valid();
    assert(store::load(result, found) == ESP_OK);
    assert(!found && cleared(result));

    // Oversized blobs report INVALID_LENGTH; leave NVS untouched and reopen setup.
    reset();
    fake.blob_size = sizeof(Settings) + 1;
    fake.read_error = ESP_ERR_NVS_INVALID_LENGTH;
    result = valid();
    found = true;
    assert(store::load(result, found) == ESP_OK);
    assert(!found && cleared(result) && fake.closes == 1);
    assert(fake.writes == 0 && fake.commits == 0);

    reset();
    assert(store::persist(fake.stored) == ESP_OK);
    assert(fake.mode == NVS_READWRITE && fake.writes == 1 && fake.commits == 1 && fake.closes == 1);

    reset();
    fake.open_error = kOpenError;
    assert(store::persist(fake.stored) == kOpenError);
    assert(fake.writes == 0 && fake.commits == 0 && fake.closes == 0);

    reset();
    fake.write_error = kWriteError;
    assert(store::persist(fake.stored) == kWriteError);
    assert(fake.writes == 1 && fake.commits == 0 && fake.closes == 1);

    reset();
    fake.commit_error = kCommitError;
    assert(store::persist(fake.stored) == kCommitError);
    assert(fake.writes == 1 && fake.commits == 1 && fake.closes == 1);

    // Persist intentionally does not validate or alter the caller's blob.
    reset();
    fake.stored.magic = 0;
    assert(store::persist(fake.stored) == ESP_OK);
    assert(fake.commits == 1);
    // Saving credentials clears a pending setup request in the same commit.
    reset();
    fake.flag = 1;
    assert(store::persist(fake.stored) == ESP_OK);
    assert(fake.flag == 0 && fake.flag_writes == 1 && fake.commits == 1);
    reset();
    fake.flag_write_error = kWriteError;
    assert(store::persist(fake.stored) == kWriteError);
    assert(fake.writes == 1 && fake.flag_writes == 1 && fake.commits == 0 && fake.closes == 1);
    reset();
    fake.write_error = kWriteError;
    assert(store::persist(fake.stored) == kWriteError);
    assert(fake.flag_writes == 0 && fake.commits == 0);

    for (uint8_t value : { 0, 1, 2 }) {
        reset();
        fake.flag = value;
        bool requested = true;
        assert(store::load_setup_requested(requested) == ESP_OK);
        assert(requested == (value != 0));
        assert(fake.mode == NVS_READONLY && fake.flag_reads == 1 && fake.closes == 1);
    }
    for (bool missing_namespace : { false, true }) {
        reset();
        if (missing_namespace)
            fake.open_error = ESP_ERR_NVS_NOT_FOUND;
        else
            fake.flag_read_error = ESP_ERR_NVS_NOT_FOUND;
        bool requested = true;
        assert(store::load_setup_requested(requested) == ESP_OK && !requested);
        assert(fake.closes == (missing_namespace ? 0 : 1));
    }
    reset();
    fake.open_error = kOpenError;
    bool requested = true;
    assert(store::load_setup_requested(requested) == kOpenError && !requested);
    assert(fake.flag_reads == 0 && fake.closes == 0);
    reset();
    fake.flag = 1;
    fake.flag_read_error = kReadError;
    requested = true;
    assert(store::load_setup_requested(requested) == kReadError && !requested);
    assert(fake.closes == 1);
    for (bool value : { false, true }) {
        reset();
        assert(store::set_setup_requested(value) == ESP_OK);
        assert(fake.flag == value && fake.flag_writes == 1 && fake.writes == 0);
        assert(fake.mode == NVS_READWRITE && fake.commits == 1 && fake.closes == 1);
    }
    reset();
    fake.open_error = kOpenError;
    assert(store::set_setup_requested(true) == kOpenError);
    assert(fake.flag_writes == 0 && fake.commits == 0 && fake.closes == 0);
    reset();
    fake.flag_write_error = kWriteError;
    assert(store::set_setup_requested(true) == kWriteError);
    assert(fake.flag_writes == 1 && fake.commits == 0 && fake.closes == 1);
    reset();
    fake.commit_error = kCommitError;
    assert(store::set_setup_requested(true) == kCommitError);
    assert(fake.flag_writes == 1 && fake.commits == 1 && fake.closes == 1);
}
