#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct tskTaskControlBlock {
    std::string name;
    std::mutex lock;
    std::condition_variable changed;
    uint32_t value = 0;
    bool received = false;
};

struct QueueDefinition {
    size_t length;
    size_t item_size;
    std::mutex lock;
    std::condition_variable changed;
    std::deque<std::vector<unsigned char>> items;
};

struct mosaic_semaphore {
    std::mutex lock;
    std::condition_variable changed;
    bool available = true;
};

namespace {
// Thrown by vTaskDelete(nullptr) to unwind the task's thread.
struct TaskDeleted { };

thread_local tskTaskControlBlock* g_current = nullptr;

// Waits until ready() under lock, or the timeout; portMAX_DELAY waits forever.
template <typename Ready>
bool wait(std::unique_lock<std::mutex>& lock, std::condition_variable& changed, TickType_t ticks, Ready ready) {
    if (ticks == portMAX_DELAY) {
        changed.wait(lock, ready);
        return true;
    }
    return changed.wait_for(lock, std::chrono::milliseconds(ticks), ready);
}
} // namespace

BaseType_t xTaskCreate(
    TaskFunction_t function, const char* name, uint32_t, void* argument, UBaseType_t, TaskHandle_t* created) {
    auto* task = new tskTaskControlBlock { };
    task->name = name ? name : "";
    if (created)
        *created = task;
    std::thread([task, function, argument] {
        g_current = task;
        try {
            function(argument);
        } catch (const TaskDeleted&) { }
        // FreeRTOS tasks must not return; one that did would crash the device.
    }).detach();
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
    if (task && task != g_current) {
        std::fprintf(stderr, "vTaskDelete of another task is not supported in the preview\n");
        std::abort();
    }
    throw TaskDeleted { };
}

void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

TaskHandle_t xTaskGetCurrentTaskHandle() {
    if (!g_current)
        g_current = new tskTaskControlBlock { };
    return g_current;
}

BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action) {
    if (!task)
        return pdFAIL;
    {
        const std::lock_guard lock(task->lock);
        switch (action) {
        case eSetBits:
            task->value |= value;
            break;
        case eIncrement:
            ++task->value;
            break;
        case eSetValueWithOverwrite:
            task->value = value;
            break;
        case eSetValueWithoutOverwrite:
            if (task->received)
                return pdFAIL;
            task->value = value;
            break;
        case eNoAction:
            break;
        }
        task->received = true;
    }
    task->changed.notify_all();
    return pdPASS;
}

BaseType_t xTaskNotifyGive(TaskHandle_t task) {
    return xTaskNotify(task, 0, eIncrement);
}

uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks_to_wait) {
    auto* task = xTaskGetCurrentTaskHandle();
    std::unique_lock lock(task->lock);
    wait(lock, task->changed, ticks_to_wait, [task] { return task->value != 0; });
    const uint32_t value = task->value;
    if (value)
        task->value = clear_on_exit ? 0 : value - 1;
    task->received = false;
    return value;
}

BaseType_t xTaskNotifyWait(uint32_t clear_on_entry, uint32_t clear_on_exit, uint32_t* value, TickType_t ticks_to_wait) {
    auto* task = xTaskGetCurrentTaskHandle();
    std::unique_lock lock(task->lock);
    if (!task->received)
        task->value &= ~clear_on_entry;
    const bool received = wait(lock, task->changed, ticks_to_wait, [task] { return task->received; });
    if (value)
        *value = task->value;
    if (received)
        task->value &= ~clear_on_exit;
    task->received = false;
    return received ? pdTRUE : pdFALSE;
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    if (!length || !item_size)
        return nullptr;
    auto* queue = new QueueDefinition { };
    queue->length = length;
    queue->item_size = item_size;
    return queue;
}

void vQueueDelete(QueueHandle_t queue) {
    delete queue;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks_to_wait) {
    std::unique_lock lock(queue->lock);
    if (!wait(lock, queue->changed, ticks_to_wait, [queue] { return queue->items.size() < queue->length; }))
        return pdFAIL;
    const auto* bytes = static_cast<const unsigned char*>(item);
    queue->items.emplace_back(bytes, bytes + queue->item_size);
    lock.unlock();
    queue->changed.notify_all();
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t ticks_to_wait) {
    std::unique_lock lock(queue->lock);
    if (!wait(lock, queue->changed, ticks_to_wait, [queue] { return !queue->items.empty(); }))
        return pdFAIL;
    std::memcpy(item, queue->items.front().data(), queue->item_size);
    queue->items.pop_front();
    lock.unlock();
    queue->changed.notify_all();
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    const std::lock_guard lock(queue->lock);
    return static_cast<UBaseType_t>(queue->items.size());
}

SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new mosaic_semaphore { };
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    delete semaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait) {
    std::unique_lock lock(semaphore->lock);
    if (!wait(lock, semaphore->changed, ticks_to_wait, [semaphore] { return semaphore->available; }))
        return pdFAIL;
    semaphore->available = false;
    return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    {
        const std::lock_guard lock(semaphore->lock);
        if (semaphore->available)
            return pdFAIL;
        semaphore->available = true;
    }
    semaphore->changed.notify_one();
    return pdPASS;
}
