#pragma once
// Host NVS: typed keys in namespaces, kept in one file so settings survive restarts.
#include "esp_err.h"

#include <cstddef>
#include <cstdint>

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value);
