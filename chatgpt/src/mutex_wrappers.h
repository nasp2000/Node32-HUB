#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// RAII lock guard for regular (non-recursive) FreeRTOS mutex.
// Takes the mutex on construction, gives it on destruction.
// NOT safe for ISR context (use portMAX_DELAY = block forever).
class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY)
        : mMutex(mutex), mAcquired(false) {
        if (mMutex) {
            mAcquired = (xSemaphoreTake(mMutex, timeout) == pdTRUE);
        }
    }

    ~LockGuard() {
        if (mAcquired && mMutex) {
            xSemaphoreGive(mMutex);
        }
    }

    bool acquired() const { return mAcquired; }

private:
    SemaphoreHandle_t mMutex;
    bool mAcquired;
};

// RAII lock guard for recursive FreeRTOS mutex.
class RecursiveLockGuard {
public:
    explicit RecursiveLockGuard(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY)
        : mMutex(mutex), mAcquired(false) {
        if (mMutex) {
            mAcquired = (xSemaphoreTakeRecursive(mMutex, timeout) == pdTRUE);
        }
    }

    ~RecursiveLockGuard() {
        if (mAcquired && mMutex) {
            xSemaphoreGiveRecursive(mMutex);
        }
    }

    bool acquired() const { return mAcquired; }

private:
    SemaphoreHandle_t mMutex;
    bool mAcquired;
};

// ISR-safe lock guard for regular mutex (non-blocking, returns false if busy).
class LockGuardISR {
public:
    explicit LockGuardISR(SemaphoreHandle_t mutex)
        : mMutex(mutex), mAcquired(false) {
        if (mMutex) {
            BaseType_t higher = pdFALSE;
            mAcquired = (xSemaphoreTakeFromISR(mMutex, &higher) == pdTRUE);
            (void)higher;
        }
    }

    ~LockGuardISR() {
        if (mAcquired && mMutex) {
            BaseType_t higher = pdFALSE;
            xSemaphoreGiveFromISR(mMutex, &higher);
            (void)higher;
        }
    }

    bool acquired() const { return mAcquired; }

private:
    SemaphoreHandle_t mMutex;
    bool mAcquired;
};
