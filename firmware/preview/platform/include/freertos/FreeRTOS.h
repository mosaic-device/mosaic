#pragma once
// Host FreeRTOS: tasks are threads and ticks are milliseconds. Priorities and stack
// sizes are accepted and ignored; the host scheduler is preemptive, so code that is
// correct under FreeRTOS's preemption is correct here.
#include <cstdint>
#include <mutex>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdFAIL pdFALSE
#define pdPASS pdTRUE
#define configTICK_RATE_HZ 1000
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#define portMAX_DELAY (static_cast<TickType_t>(0xffffffffu))
#define tskNO_AFFINITY 0x7FFFFFFF

// A critical section is a mutex: it excludes the other tasks, as the device's
// spinlock does across cores.
struct portMUX_TYPE {
    std::mutex lock;
};
#define portMUX_INITIALIZER_UNLOCKED                                                                                   \
    { }
#define portENTER_CRITICAL(mux) (mux)->lock.lock()
#define portEXIT_CRITICAL(mux) (mux)->lock.unlock()
