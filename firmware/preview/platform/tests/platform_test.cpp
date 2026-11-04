// Checks that the host platform behaves like the IDF and FreeRTOS APIs the firmware uses.
#include "esp_err.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mosaic_platform.hpp"
#include "nvs.h"
#include "nvs_flash.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
namespace platform = mosaic::preview::platform;

void eventually(const std::atomic_bool& done) {
    for (int i = 0; i < 2000 && !done.load(); ++i)
        std::this_thread::sleep_for(1ms);
    assert(done.load());
}

void queues() {
    QueueHandle_t queue = xQueueCreate(2, sizeof(int));
    assert(queue && !xQueueCreate(0, sizeof(int)));
    int value = 1;
    assert(xQueueSend(queue, &value, 0) == pdTRUE);
    value = 2;
    assert(xQueueSend(queue, &value, 0) == pdTRUE);
    // Full: a zero wait fails at once, a bounded wait fails after its timeout.
    value = 3;
    assert(xQueueSend(queue, &value, 0) == pdFALSE);
    const auto before = esp_timer_get_time();
    assert(xQueueSend(queue, &value, pdMS_TO_TICKS(30)) == pdFALSE);
    assert(esp_timer_get_time() - before >= 25'000);
    assert(uxQueueMessagesWaiting(queue) == 2);
    // Items are copied out in order.
    int out = 0;
    assert(xQueueReceive(queue, &out, 0) == pdTRUE && out == 1);
    assert(xQueueReceive(queue, &out, 0) == pdTRUE && out == 2);
    assert(xQueueReceive(queue, &out, pdMS_TO_TICKS(10)) == pdFALSE);
    // A blocked receiver wakes when another task sends.
    std::thread sender([queue] {
        std::this_thread::sleep_for(20ms);
        const int sent = 42;
        xQueueSend(queue, &sent, portMAX_DELAY);
    });
    assert(xQueueReceive(queue, &out, portMAX_DELAY) == pdTRUE && out == 42);
    sender.join();
    vQueueDelete(queue);
}

TaskHandle_t g_waiter = nullptr;
std::atomic<uint32_t> g_taken { 0 };
std::atomic_bool g_task_done { false };
std::atomic_bool g_after_delete { false };

void counting_task(void*) {
    g_waiter = xTaskGetCurrentTaskHandle();
    // Gives before the take are counted, not lost.
    while (!g_task_done.load()) {
        const uint32_t count = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(500));
        if (count)
            g_taken.fetch_add(1);
        if (g_taken.load() == 3)
            g_task_done = true;
    }
    vTaskDelete(nullptr);
    g_after_delete = true;
}

void notifications() {
    // Counting form: three gives are three takes when not cleared on exit.
    TaskHandle_t created = nullptr;
    assert(xTaskCreate(counting_task, "counting", 4096, nullptr, 3, &created) == pdPASS && created);
    for (int i = 0; i < 3; ++i)
        assert(xTaskNotifyGive(created) == pdPASS);
    eventually(g_task_done);
    std::this_thread::sleep_for(20ms);
    assert(g_taken.load() == 3 && g_waiter == created);
    // vTaskDelete(nullptr) ends the task without running the rest of it.
    assert(!g_after_delete.load());

    // Clearing on exit folds several gives into one take, returning the count.
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    assert(self && self == xTaskGetCurrentTaskHandle());
    xTaskNotifyGive(self);
    xTaskNotifyGive(self);
    assert(ulTaskNotifyTake(pdTRUE, 0) == 2);
    assert(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10)) == 0);

    // Value form: the latest overwrite is what the waiter sees.
    xTaskNotify(self, 7, eSetValueWithOverwrite);
    xTaskNotify(self, 9, eSetValueWithOverwrite);
    uint32_t value = 0;
    assert(xTaskNotifyWait(0, UINT32_MAX, &value, 0) == pdTRUE && value == 9);
    assert(xTaskNotifyWait(0, UINT32_MAX, &value, pdMS_TO_TICKS(10)) == pdFALSE);
    // A notification from another thread wakes a waiter, like startup's handshake.
    std::thread notifier([self] {
        std::this_thread::sleep_for(20ms);
        xTaskNotify(self, static_cast<uint32_t>(ESP_ERR_TIMEOUT), eSetValueWithOverwrite);
    });
    assert(xTaskNotifyWait(0, UINT32_MAX, &value, portMAX_DELAY) == pdTRUE && value == ESP_ERR_TIMEOUT);
    notifier.join();
    // Notifying no task fails rather than crashing.
    assert(xTaskNotifyGive(nullptr) == pdFAIL);
}

void mutexes() {
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    assert(xSemaphoreTake(mutex, 0) == pdTRUE);
    assert(xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) == pdFALSE);
    std::atomic_bool taken = false;
    std::thread other([&] {
        taken = xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
        xSemaphoreGive(mutex);
    });
    std::this_thread::sleep_for(20ms);
    assert(!taken.load());
    assert(xSemaphoreGive(mutex) == pdTRUE);
    other.join();
    assert(taken.load() && xSemaphoreGive(mutex) == pdFALSE);
    vSemaphoreDelete(mutex);

    // Critical sections exclude other threads.
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    int counter = 0;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 10'000; ++i) {
                portENTER_CRITICAL(&mux);
                ++counter;
                portEXIT_CRITICAL(&mux);
            }
        });
    for (auto& thread : threads)
        thread.join();
    assert(counter == 40'000);
}

ESP_EVENT_DEFINE_BASE(TEST_EVENT);
ESP_EVENT_DEFINE_BASE(OTHER_EVENT);
std::mutex g_seen_lock;
std::vector<std::string> g_seen;
std::atomic_bool g_release { true };

void record(void* arg, esp_event_base_t base, int32_t id, void* data) {
    while (!g_release.load())
        std::this_thread::sleep_for(1ms);
    const std::lock_guard lock(g_seen_lock);
    g_seen.push_back(std::string(static_cast<const char*>(arg)) + ":" + base + ":" + std::to_string(id) + ":"
        + (data ? std::to_string(*static_cast<int*>(data)) : "-"));
}
std::vector<std::string> seen_after(size_t count) {
    for (int i = 0; i < 2000; ++i) {
        {
            const std::lock_guard lock(g_seen_lock);
            if (g_seen.size() >= count)
                return g_seen;
        }
        std::this_thread::sleep_for(1ms);
    }
    assert(false && "events not delivered");
    return { };
}

void events() {
    const esp_event_loop_args_t args { .queue_size = 2,
        .task_name = "test",
        .task_priority = 4,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY };
    esp_event_loop_handle_t loop = nullptr;
    assert(esp_event_loop_create(&args, &loop) == ESP_OK && loop);
    esp_event_handler_instance_t one = nullptr, any = nullptr;
    assert(esp_event_handler_instance_register_with(loop, TEST_EVENT, 1, record, const_cast<char*>("one"), &one)
        == ESP_OK);
    assert(esp_event_handler_instance_register_with(
               loop, TEST_EVENT, ESP_EVENT_ANY_ID, record, const_cast<char*>("any"), &any)
        == ESP_OK);
    // Payloads are copied at post time, so the poster may reuse its buffer.
    int payload = 5;
    assert(esp_event_post_to(loop, TEST_EVENT, 1, &payload, sizeof(payload), portMAX_DELAY) == ESP_OK);
    payload = 6;
    assert(esp_event_post_to(loop, TEST_EVENT, 2, &payload, sizeof(payload), portMAX_DELAY) == ESP_OK);
    assert(esp_event_post_to(loop, OTHER_EVENT, 1, nullptr, 0, portMAX_DELAY) == ESP_OK);
    assert((seen_after(3)
        == std::vector<std::string> { "one:TEST_EVENT:1:5", "any:TEST_EVENT:1:5", "any:TEST_EVENT:2:6" }));
    // An unregistered handler hears nothing more.
    assert(esp_event_handler_instance_unregister_with(loop, TEST_EVENT, ESP_EVENT_ANY_ID, any) == ESP_OK);
    assert(esp_event_post_to(loop, TEST_EVENT, 1, nullptr, 0, portMAX_DELAY) == ESP_OK);
    assert(seen_after(4).back() == "one:TEST_EVENT:1:-");
    // While handlers are slow, a full queue times out the poster.
    g_release = false;
    assert(esp_event_post_to(loop, TEST_EVENT, 1, nullptr, 0, 0) == ESP_OK); // Taken by the loop, then blocked.
    std::this_thread::sleep_for(20ms);
    assert(esp_event_post_to(loop, TEST_EVENT, 1, nullptr, 0, 0) == ESP_OK);
    assert(esp_event_post_to(loop, TEST_EVENT, 1, nullptr, 0, 0) == ESP_OK);
    assert(esp_event_post_to(loop, TEST_EVENT, 1, nullptr, 0, pdMS_TO_TICKS(20)) == ESP_ERR_TIMEOUT);
    g_release = true;
    seen_after(7);
    assert(esp_event_post_to(loop, TEST_EVENT, ESP_EVENT_ANY_ID, nullptr, 0, 0) == ESP_ERR_INVALID_ARG);

    // The default loop exists once.
    assert(esp_event_handler_instance_register(TEST_EVENT, 1, record, nullptr, nullptr) == ESP_ERR_INVALID_STATE);
    assert(esp_event_loop_create_default() == ESP_OK);
    assert(esp_event_loop_create_default() == ESP_ERR_INVALID_STATE);
    assert(
        esp_event_handler_instance_register(OTHER_EVENT, 3, record, const_cast<char*>("default"), nullptr) == ESP_OK);
    payload = 8;
    assert(esp_event_post(OTHER_EVENT, 3, &payload, sizeof(payload), 0) == ESP_OK);
    assert(seen_after(8).back() == "default:OTHER_EVENT:3:8");
}

struct Blob {
    uint32_t magic;
    char text[12];
};

void nvs_write() {
    nvs_handle_t handle;
    assert(nvs_open("store", NVS_READONLY, &handle) == ESP_ERR_INVALID_STATE);
    assert(nvs_flash_init() == ESP_OK);
    // A namespace that was never written is not found when read.
    assert(nvs_open("store", NVS_READONLY, &handle) == ESP_ERR_NVS_NOT_FOUND);
    assert(nvs_open("a-name-too-long-for-nvs", NVS_READWRITE, &handle) == ESP_ERR_INVALID_ARG);
    assert(nvs_open("store", NVS_READWRITE, &handle) == ESP_OK);
    const Blob blob { 0x4d4f5331, "kept" };
    assert(nvs_set_blob(handle, "blob", &blob, sizeof(blob)) == ESP_OK);
    assert(nvs_set_u8(handle, "flag", 1) == ESP_OK);
    assert(nvs_commit(handle) == ESP_OK);
    // Failing writes leave the stored values alone.
    platform::set_nvs_write_failure(true);
    assert(nvs_set_u8(handle, "flag", 0) == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    assert(nvs_commit(handle) == ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    platform::set_nvs_write_failure(false);
    nvs_close(handle);
    assert(nvs_commit(handle) == ESP_ERR_NVS_INVALID_HANDLE);
    // Read-only handles cannot write.
    assert(nvs_open("store", NVS_READONLY, &handle) == ESP_OK);
    assert(nvs_set_u8(handle, "flag", 0) == ESP_ERR_NVS_READ_ONLY);
    nvs_close(handle);
}

void nvs_read() {
    // A second run sees what the first committed.
    assert(nvs_flash_init() == ESP_OK);
    nvs_handle_t handle;
    assert(nvs_open("store", NVS_READONLY, &handle) == ESP_OK);
    Blob blob { };
    size_t size = 0;
    assert(nvs_get_blob(handle, "blob", nullptr, &size) == ESP_OK && size == sizeof(blob));
    size = sizeof(blob) - 1;
    assert(nvs_get_blob(handle, "blob", &blob, &size) == ESP_ERR_NVS_INVALID_LENGTH);
    size = sizeof(blob) + 8;
    assert(nvs_get_blob(handle, "blob", &blob, &size) == ESP_OK && size == sizeof(blob));
    assert(blob.magic == 0x4d4f5331 && std::string_view(blob.text) == "kept");
    uint8_t flag = 0;
    assert(nvs_get_u8(handle, "flag", &flag) == ESP_OK && flag == 1);
    // Keys are typed, and missing keys are not found.
    assert(nvs_get_u8(handle, "blob", &flag) == ESP_ERR_NVS_NOT_FOUND);
    assert(nvs_get_blob(handle, "missing", &blob, &size) == ESP_ERR_NVS_NOT_FOUND);
    nvs_close(handle);
    platform::erase_nvs();
    assert(nvs_open("store", NVS_READONLY, &handle) == ESP_ERR_NVS_NOT_FOUND);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 3);
    platform::set_nvs_file(argv[2]);
    if (std::string_view(argv[1]) == "read") {
        nvs_read();
        return 0;
    }
    assert(std::strcmp(esp_err_to_name(ESP_ERR_NVS_NOT_FOUND), "ESP_ERR_NVS_NOT_FOUND") == 0);
    const auto start = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(15));
    assert(esp_timer_get_time() - start >= 14'000);
    queues();
    notifications();
    mutexes();
    events();
    nvs_write();
}
