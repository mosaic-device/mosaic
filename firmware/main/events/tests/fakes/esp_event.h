#pragma once
// Synchronous esp_event: posting runs matching handlers immediately on the caller.
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using esp_event_base_t = const char*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
using esp_event_handler_instance_t = void*;
#define ESP_EVENT_ANY_ID -1
#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t const id = #id

struct esp_event_loop_args_t {
    int32_t queue_size;
    const char* task_name;
    UBaseType_t task_priority;
    uint32_t task_stack_size;
    BaseType_t task_core_id;
};

struct FakeLoop {
    struct Handler {
        esp_event_base_t base;
        int32_t id;
        esp_event_handler_t handler;
        void* arg;
    };
    std::vector<Handler> handlers;
};
using esp_event_loop_handle_t = FakeLoop*;

inline int fake_loops_created = 0;
inline esp_err_t fake_loop_create_result = ESP_OK;
inline esp_err_t fake_post_result = ESP_OK;
inline int fake_posts = 0;

inline esp_err_t esp_event_loop_create(const esp_event_loop_args_t*, esp_event_loop_handle_t* loop) {
    if (fake_loop_create_result != ESP_OK)
        return fake_loop_create_result;
    ++fake_loops_created;
    *loop = new FakeLoop;
    return ESP_OK;
}

inline esp_err_t esp_event_handler_instance_register_with(esp_event_loop_handle_t loop, esp_event_base_t base,
    int32_t id, esp_event_handler_t handler, void* arg, esp_event_handler_instance_t*) {
    loop->handlers.push_back({ base, id, handler, arg });
    return ESP_OK;
}

inline esp_err_t esp_event_post_to(
    esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id, const void* data, size_t size, TickType_t) {
    ++fake_posts;
    if (fake_post_result != ESP_OK)
        return fake_post_result;
    // Like esp_event, handlers receive a copy of the data.
    std::vector<unsigned char> copy(size);
    if (size != 0)
        std::memcpy(copy.data(), data, size);
    for (const auto& entry : loop->handlers)
        if (entry.base == base && (entry.id == ESP_EVENT_ANY_ID || entry.id == id))
            entry.handler(entry.arg, base, id, size != 0 ? copy.data() : nullptr);
    return ESP_OK;
}
