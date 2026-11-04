#include "mosaic_platform.hpp"
#include "nvs.h"
#include "nvs_flash.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {
// NVS keys are typed: reading a key as another type finds nothing.
enum class Type : uint8_t { u8 = 1, blob = 2 };
struct Entry {
    Type type;
    std::vector<unsigned char> bytes;
};
using Namespace = std::map<std::string, Entry>;
struct Handle {
    std::string name;
    bool writable;
};
// NVS limits namespace and key names to 15 characters.
constexpr size_t kMaxName = 15;

std::mutex g_lock;
std::map<std::string, Namespace> g_store;
std::map<nvs_handle_t, Handle> g_handles;
nvs_handle_t g_next_handle = 1;
std::filesystem::path g_file = "mosaic-preview.nvs";
bool g_initialized = false;
std::atomic_bool g_write_failure = false;

// File format: repeated [namespace\0 key\0 type:u8 length:u32le bytes].
void write_string(std::ofstream& out, const std::string& text) {
    out.write(text.c_str(), static_cast<std::streamsize>(text.size() + 1));
}
bool read_string(std::ifstream& in, std::string& text) {
    return static_cast<bool>(std::getline(in, text, '\0'));
}

// With g_lock held. Writes a temporary file first so a crash cannot truncate NVS.
esp_err_t save() {
    const auto temporary = std::filesystem::path(g_file).concat(".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return ESP_FAIL;
        for (const auto& [name, keys] : g_store)
            for (const auto& [key, entry] : keys) {
                write_string(out, name);
                write_string(out, key);
                const auto type = static_cast<unsigned char>(entry.type);
                const auto length = static_cast<uint32_t>(entry.bytes.size());
                const unsigned char size[4]
                    = { static_cast<unsigned char>(length), static_cast<unsigned char>(length >> 8),
                          static_cast<unsigned char>(length >> 16), static_cast<unsigned char>(length >> 24) };
                out.put(static_cast<char>(type));
                out.write(reinterpret_cast<const char*>(size), 4);
                out.write(reinterpret_cast<const char*>(entry.bytes.data()), static_cast<std::streamsize>(length));
            }
        if (!out)
            return ESP_FAIL;
    }
    std::error_code error;
    std::filesystem::rename(temporary, g_file, error);
    return error ? ESP_FAIL : ESP_OK;
}

bool valid_name(const char* name) {
    return name && name[0] && std::strlen(name) <= kMaxName;
}

// With g_lock held.
const Entry* find(nvs_handle_t handle, const char* key, Type type, esp_err_t& err) {
    const auto open = g_handles.find(handle);
    if (open == g_handles.end()) {
        err = ESP_ERR_NVS_INVALID_HANDLE;
        return nullptr;
    }
    const auto& keys = g_store[open->second.name];
    const auto found = keys.find(key ? key : "");
    if (found == keys.end() || found->second.type != type) {
        err = ESP_ERR_NVS_NOT_FOUND;
        return nullptr;
    }
    err = ESP_OK;
    return &found->second;
}

esp_err_t set(nvs_handle_t handle, const char* key, Type type, const void* value, size_t length) {
    const std::lock_guard lock(g_lock);
    const auto open = g_handles.find(handle);
    if (open == g_handles.end())
        return ESP_ERR_NVS_INVALID_HANDLE;
    if (!open->second.writable)
        return ESP_ERR_NVS_READ_ONLY;
    if (!valid_name(key) || (!value && length))
        return ESP_ERR_INVALID_ARG;
    if (g_write_failure.load())
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    const auto* bytes = static_cast<const unsigned char*>(value);
    g_store[open->second.name][key] = { type, std::vector<unsigned char>(bytes, bytes + length) };
    return ESP_OK;
}
} // namespace

esp_err_t nvs_flash_init() {
    const std::lock_guard lock(g_lock);
    if (g_initialized)
        return ESP_OK;
    g_initialized = true;
    std::ifstream in(g_file, std::ios::binary);
    if (!in)
        return ESP_OK; // Erased flash.
    std::map<std::string, Namespace> loaded;
    std::string name, key;
    while (read_string(in, name) && read_string(in, key)) {
        unsigned char header[5];
        if (!in.read(reinterpret_cast<char*>(header), 5))
            break;
        const uint32_t length = header[1] | header[2] << 8 | header[3] << 16 | static_cast<uint32_t>(header[4]) << 24;
        Entry entry { static_cast<Type>(header[0]), std::vector<unsigned char>(length) };
        if (!in.read(reinterpret_cast<char*>(entry.bytes.data()), length)) {
            std::fprintf(stderr, "Preview NVS file %s is truncated; starting erased\n", g_file.string().c_str());
            return ESP_OK;
        }
        loaded[name][key] = std::move(entry);
    }
    g_store = std::move(loaded);
    return ESP_OK;
}

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle) {
    const std::lock_guard lock(g_lock);
    if (!g_initialized)
        return ESP_ERR_INVALID_STATE;
    if (!valid_name(name) || !handle)
        return ESP_ERR_INVALID_ARG;
    // Opening read-only does not create the namespace.
    if (mode == NVS_READONLY && !g_store.contains(name))
        return ESP_ERR_NVS_NOT_FOUND;
    g_store[name];
    *handle = g_next_handle++;
    g_handles[*handle] = { name, mode == NVS_READWRITE };
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    const std::lock_guard lock(g_lock);
    g_handles.erase(handle);
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    const std::lock_guard lock(g_lock);
    if (!g_handles.contains(handle))
        return ESP_ERR_NVS_INVALID_HANDLE;
    if (g_write_failure.load())
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    return save();
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* length) {
    if (!length)
        return ESP_ERR_INVALID_ARG;
    const std::lock_guard lock(g_lock);
    esp_err_t err;
    const Entry* entry = find(handle, key, Type::blob, err);
    if (!entry)
        return err;
    // A null buffer asks for the stored length.
    if (!out) {
        *length = entry->bytes.size();
        return ESP_OK;
    }
    if (*length < entry->bytes.size())
        return ESP_ERR_NVS_INVALID_LENGTH;
    std::memcpy(out, entry->bytes.data(), entry->bytes.size());
    *length = entry->bytes.size();
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length) {
    return set(handle, key, Type::blob, value, length);
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    const std::lock_guard lock(g_lock);
    esp_err_t err;
    const Entry* entry = find(handle, key, Type::u8, err);
    if (!entry)
        return err;
    *out = entry->bytes.at(0);
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
    return set(handle, key, Type::u8, &value, 1);
}

namespace mosaic::preview::platform {
void set_nvs_file(const std::filesystem::path& path) {
    const std::lock_guard lock(g_lock);
    g_file = path;
}
void set_nvs_write_failure(bool failing) {
    g_write_failure = failing;
}
bool nvs_write_failure() {
    return g_write_failure.load();
}
void erase_nvs() {
    const std::lock_guard lock(g_lock);
    g_store.clear();
    std::error_code error;
    std::filesystem::remove(g_file, error);
}
}
