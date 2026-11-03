#pragma once
#include <cstdint>
typedef const char* esp_event_base_t;
typedef void* esp_event_loop_handle_t;
typedef void* esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void*, esp_event_base_t, int32_t, void*);
#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id
