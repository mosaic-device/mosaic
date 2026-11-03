#pragma once
// Host esp_event: each loop dispatches on its own thread, in post order, like the
// device's loop tasks.
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include <cstddef>
#include <cstdint>

typedef const char* esp_event_base_t;
typedef struct mosaic_event_loop* esp_event_loop_handle_t;
typedef void (*esp_event_handler_t)(void* arg, esp_event_base_t base, int32_t id, void* data);
typedef struct mosaic_event_handler* esp_event_handler_instance_t;

#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t const id = #id
#define ESP_EVENT_ANY_BASE nullptr
#define ESP_EVENT_ANY_ID -1

typedef struct {
    int32_t queue_size;
    const char* task_name;
    UBaseType_t task_priority;
    uint32_t task_stack_size;
    BaseType_t task_core_id;
} esp_event_loop_args_t;

esp_err_t esp_event_loop_create(const esp_event_loop_args_t* args, esp_event_loop_handle_t* loop);
esp_err_t esp_event_loop_create_default();
esp_err_t esp_event_handler_instance_register_with(esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id,
    esp_event_handler_t handler, void* arg, esp_event_handler_instance_t* instance);
esp_err_t esp_event_handler_instance_unregister_with(
    esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance);
esp_err_t esp_event_post_to(esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id, const void* data,
    size_t size, TickType_t ticks_to_wait);
// The default loop.
esp_err_t esp_event_handler_instance_register(
    esp_event_base_t base, int32_t id, esp_event_handler_t handler, void* arg, esp_event_handler_instance_t* instance);
esp_err_t esp_event_handler_instance_unregister(
    esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance);
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void* data, size_t size, TickType_t ticks_to_wait);
