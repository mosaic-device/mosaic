// The credential store against a fake NVS that behaves as ESP-IDF does: each set stores its item
// as it happens.
#include "../adapters/credential_store.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "nvs.h"

using namespace mosaic::features::connection;
namespace store = adapters::credential_store;

namespace {
constexpr esp_err_t kReadError = 0x201;
constexpr esp_err_t kWriteError = 0x202;

struct Fake {
    std::map<std::string, std::vector<unsigned char>> blobs;
    esp_err_t read_error = ESP_OK;
    esp_err_t write_error = ESP_OK;
    bool remove_failed = false;
    bool namespace_exists = false;
    int opens = 0;
    int closes = 0;
    int commits = 0;
} fake;

std::string key(nvs_handle_t, const char* name) {
    return std::string("mosaic_device/") + name;
}

Credential credential(const char* url, uint8_t first, bool paired) {
    std::array<uint8_t, kSecretSize> secret { };
    secret.fill(first);
    Credential value;
    assert(fresh(secret, url, value));
    value.paired = paired;
    return value;
}

Credential loaded(bool& found, esp_err_t expected = ESP_OK) {
    Credential out = credential("wss://stale.example/ws/device", 9, true);
    assert(store::load(out, found) == expected);
    return out;
}

void nothing_saved_is_not_found() {
    bool found = true;
    assert(loaded(found) == Credential { } && !found);
    fake.namespace_exists = true;
    assert(loaded(found) == Credential { } && !found);
}

void a_saved_credential_comes_back() {
    const Credential paired = credential("wss://mosaic.example/ws/device", 7, true);
    assert(store::save(paired) == ESP_OK);
    assert(fake.commits == 1);
    bool found = false;
    assert(loaded(found) == paired && found);
    // Replaced in one write: the new one wins.
    const Credential replacement = credential("wss://mosaic.example/ws/device", 8, false);
    assert(store::save(replacement) == ESP_OK);
    assert(loaded(found) == replacement && found);
    assert(fake.opens == fake.closes);
}

void unusable_records_are_ignored_not_erased() {
    bool found = true;
    auto& blob = fake.blobs["mosaic_device/credential"];
    const auto good = blob;
    // Another kind of record.
    blob[0] ^= 0xFF;
    assert(loaded(found) == Credential { } && !found);
    // A URL without its terminator: after the magic, the flags and the secret.
    blob = good;
    blob[8 + kSecretSize + kUrlCapacity - 1] = 'x';
    assert(loaded(found) == Credential { } && !found);
    // Another size.
    blob = good;
    blob.pop_back();
    assert(loaded(found) == Credential { } && !found);
    // Still there for whoever reads it next.
    assert(!fake.blobs["mosaic_device/credential"].empty());
    blob = good;
}

void storage_failures_are_reported() {
    bool found = true;
    fake.read_error = kReadError;
    assert(loaded(found, kReadError) == Credential { } && !found);
    fake.read_error = ESP_OK;
    const auto before = fake.blobs;
    fake.write_error = kWriteError;
    assert(store::save(credential("wss://mosaic.example/ws/device", 1, false)) == kWriteError);
    assert(fake.blobs == before);
    fake.write_error = ESP_OK;
    // Stored, but the old copy wasn't removed: still saved.
    fake.remove_failed = true;
    const Credential next = credential("wss://next.example/ws/device", 2, true);
    assert(store::save(next) == ESP_OK);
    fake.remove_failed = false;
    assert(loaded(found) == next && found);
    assert(fake.opens == fake.closes);
}
} // namespace

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle) {
    assert(std::strcmp(name, "mosaic_device") == 0);
    if (mode == NVS_READONLY && !fake.namespace_exists)
        return ESP_ERR_NVS_NOT_FOUND;
    fake.namespace_exists = true;
    ++fake.opens;
    *handle = 1;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char* name, void* out, size_t* length) {
    if (fake.read_error != ESP_OK)
        return fake.read_error;
    const auto found = fake.blobs.find(key(handle, name));
    if (found == fake.blobs.end())
        return ESP_ERR_NVS_NOT_FOUND;
    if (found->second.size() > *length)
        return ESP_ERR_NVS_INVALID_LENGTH;
    std::memcpy(out, found->second.data(), found->second.size());
    *length = found->second.size();
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char* name, const void* data, size_t length) {
    if (fake.write_error != ESP_OK)
        return fake.write_error;
    const auto* bytes = static_cast<const unsigned char*>(data);
    fake.blobs[key(handle, name)].assign(bytes, bytes + length);
    return fake.remove_failed ? ESP_ERR_NVS_REMOVE_FAILED : ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t, const char*, uint8_t*) {
    assert(false);
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t, const char*, uint8_t) {
    assert(false);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t) {
    ++fake.commits;
    return ESP_OK;
}

void nvs_close(nvs_handle_t) {
    ++fake.closes;
}

int main() {
    nothing_saved_is_not_found();
    a_saved_credential_comes_back();
    unusable_records_are_ignored_not_erased();
    storage_failures_are_reported();
    std::puts("Credential store tests passed");
}
