#pragma once
#include "FreeRTOS.h"

typedef struct tskTaskControlBlock* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef enum {
    eNoAction,
    eSetBits,
    eIncrement,
    eSetValueWithOverwrite,
    eSetValueWithoutOverwrite,
} eNotifyAction;

BaseType_t xTaskCreate(TaskFunction_t function, const char* name, uint32_t stack_depth, void* argument,
    UBaseType_t priority, TaskHandle_t* created);
// Only the calling task (nullptr) can be deleted; its thread ends.
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
// Threads not started by xTaskCreate get a handle too, so they can wait for notifications.
TaskHandle_t xTaskGetCurrentTaskHandle();

// One notification value per task, shared by the counting and value forms, as in FreeRTOS.
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks_to_wait);
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyWait(uint32_t clear_on_entry, uint32_t clear_on_exit, uint32_t* value, TickType_t ticks_to_wait);
