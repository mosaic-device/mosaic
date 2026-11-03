#include "esp_event.h"
#include "esp_netif.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

ESP_EVENT_DEFINE_BASE(IP_EVENT);

struct mosaic_event_handler {
    esp_event_base_t base;
    int32_t id;
    esp_event_handler_t handler;
    void* arg;
};

struct mosaic_event_loop {
    struct Posted {
        esp_event_base_t base;
        int32_t id;
        std::vector<unsigned char> data;
    };
    size_t capacity;
    std::mutex lock;
    std::condition_variable changed;
    std::deque<Posted> queue;
    std::vector<std::shared_ptr<mosaic_event_handler>> handlers;
};

namespace {
esp_event_loop_handle_t g_default = nullptr;
std::mutex g_default_lock;

bool matches(const mosaic_event_handler& handler, esp_event_base_t base, int32_t id) {
    return (handler.base == ESP_EVENT_ANY_BASE || handler.base == base)
        && (handler.id == ESP_EVENT_ANY_ID || handler.id == id);
}

void run(esp_event_loop_handle_t loop) {
    while (true) {
        mosaic_event_loop::Posted event;
        std::vector<std::shared_ptr<mosaic_event_handler>> handlers;
        {
            std::unique_lock lock(loop->lock);
            loop->changed.wait(lock, [loop] { return !loop->queue.empty(); });
            event = std::move(loop->queue.front());
            loop->queue.pop_front();
            handlers = loop->handlers;
        }
        loop->changed.notify_all();
        for (const auto& handler : handlers)
            if (matches(*handler, event.base, event.id))
                handler->handler(handler->arg, event.base, event.id, event.data.empty() ? nullptr : event.data.data());
    }
}

esp_event_loop_handle_t default_loop() {
    const std::lock_guard lock(g_default_lock);
    return g_default;
}
} // namespace

esp_err_t esp_event_loop_create(const esp_event_loop_args_t* args, esp_event_loop_handle_t* loop) {
    if (!args || !loop || args->queue_size <= 0)
        return ESP_ERR_INVALID_ARG;
    auto* created = new mosaic_event_loop { };
    created->capacity = static_cast<size_t>(args->queue_size);
    std::thread(run, created).detach();
    *loop = created;
    return ESP_OK;
}

esp_err_t esp_event_loop_create_default() {
    const std::lock_guard lock(g_default_lock);
    if (g_default)
        return ESP_ERR_INVALID_STATE;
    // The device's default loop queue length.
    const esp_event_loop_args_t args {
        .queue_size = 32, .task_name = "sys_evt", .task_priority = 20, .task_stack_size = 2304, .task_core_id = 0
    };
    return esp_event_loop_create(&args, &g_default);
}

esp_err_t esp_event_handler_instance_register_with(esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id,
    esp_event_handler_t handler, void* arg, esp_event_handler_instance_t* instance) {
    if (!loop || !handler || (base == ESP_EVENT_ANY_BASE && id != ESP_EVENT_ANY_ID))
        return ESP_ERR_INVALID_ARG;
    auto registered = std::make_shared<mosaic_event_handler>(mosaic_event_handler { base, id, handler, arg });
    {
        const std::lock_guard lock(loop->lock);
        loop->handlers.push_back(registered);
    }
    if (instance)
        *instance = registered.get();
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister_with(
    esp_event_loop_handle_t loop, esp_event_base_t, int32_t, esp_event_handler_instance_t instance) {
    if (!loop || !instance)
        return ESP_ERR_INVALID_ARG;
    const std::lock_guard lock(loop->lock);
    std::erase_if(loop->handlers, [instance](const auto& handler) { return handler.get() == instance; });
    return ESP_OK;
}

esp_err_t esp_event_post_to(esp_event_loop_handle_t loop, esp_event_base_t base, int32_t id, const void* data,
    size_t size, TickType_t ticks_to_wait) {
    if (!loop || base == ESP_EVENT_ANY_BASE || id == ESP_EVENT_ANY_ID)
        return ESP_ERR_INVALID_ARG;
    std::unique_lock lock(loop->lock);
    const auto room = [loop] { return loop->queue.size() < loop->capacity; };
    if (ticks_to_wait == portMAX_DELAY)
        loop->changed.wait(lock, room);
    else if (!loop->changed.wait_for(lock, std::chrono::milliseconds(ticks_to_wait), room))
        return ESP_ERR_TIMEOUT;
    const auto* bytes = static_cast<const unsigned char*>(data);
    loop->queue.push_back(
        { base, id, bytes ? std::vector<unsigned char>(bytes, bytes + size) : std::vector<unsigned char> { } });
    lock.unlock();
    loop->changed.notify_all();
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(
    esp_event_base_t base, int32_t id, esp_event_handler_t handler, void* arg, esp_event_handler_instance_t* instance) {
    const auto loop = default_loop();
    return loop ? esp_event_handler_instance_register_with(loop, base, id, handler, arg, instance)
                : ESP_ERR_INVALID_STATE;
}

esp_err_t esp_event_handler_instance_unregister(
    esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance) {
    const auto loop = default_loop();
    return loop ? esp_event_handler_instance_unregister_with(loop, base, id, instance) : ESP_ERR_INVALID_STATE;
}

esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void* data, size_t size, TickType_t ticks_to_wait) {
    const auto loop = default_loop();
    return loop ? esp_event_post_to(loop, base, id, data, size, ticks_to_wait) : ESP_ERR_INVALID_STATE;
}

esp_err_t esp_netif_init() {
    return ESP_OK;
}
