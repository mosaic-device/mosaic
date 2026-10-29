#pragma once
#include "FreeRTOS.h"
#include <mutex>
using SemaphoreHandle_t = std::mutex*;
inline bool fake_semaphore_create_fails = false;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return fake_semaphore_create_fails ? nullptr : new std::mutex;
}
inline void vSemaphoreDelete(SemaphoreHandle_t mutex) {
    delete mutex;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t) {
    mutex->lock();
    return 1;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    mutex->unlock();
    return 1;
}
