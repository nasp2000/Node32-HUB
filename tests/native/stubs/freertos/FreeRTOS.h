#pragma once
#include <stdint.h>

// Minimal host stand-in for the subset of the FreeRTOS API used by
// ConfigManager.h (recursive task mutex). Everything is reduced to no-ops.
typedef uint32_t TickType_t;
typedef long BaseType_t;
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;

#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFUL)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    return (SemaphoreHandle_t)1; // any non-NULL sentinel
}

inline void vSemaphoreDelete(SemaphoreHandle_t /*mutex*/) {}

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
    return (TaskHandle_t)0; // same "task" every call → soft recursion works
}

inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t /*mutex*/, TickType_t /*block*/) {
    return pdTRUE;
}

inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t /*mutex*/) {
    return pdTRUE;
}