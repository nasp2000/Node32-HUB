// runtime_rs232_queue.h — RS232 send queue helpers and enqueue API.
// Extracted from chatgpt.ino to reduce its size.
// Provides: queue lock/unlock/push/pop primitives + enqueueRS232Command.
// Only compiled when ENABLE_RS232==1; stubs live in chatgpt.ino otherwise.
#pragma once

#include "feature_flags.h"

#if ENABLE_RS232==1

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "psram_queue.h"

// ---------------------------------------------------------------------------
// Global variables defined in chatgpt.ino, accessible via extern.
// ---------------------------------------------------------------------------
extern SemaphoreHandle_t        rs232SendQueueMutex;
extern volatile unsigned long   rs232SendQueueMutexFailCount;
extern TaskHandle_t             rs232SendQueueMutexOwnerTask;
extern volatile uint32_t        rs232SendQueueMutexBadUnlockCount;
extern uint32_t                 rs232SendQueueMutexDepth;
extern PsramQueue<String>       rs232SendQueue;

// ---------------------------------------------------------------------------
// Queue mutex primitives (definitions in runtime_rs232_queue.cpp).
// ---------------------------------------------------------------------------
bool rs232QueueLock(TickType_t timeoutTicks);
void rs232QueueUnlock();
bool rs232QueueHasPending(unsigned *pendingCountOut = nullptr);
bool rs232QueueTryPop(String &out);
bool rs232QueueTryPush(const String &payload);

// ---------------------------------------------------------------------------
// Enqueue a command for later RS232 transmission.
// templateName: optional RS232 template to activate before sending.
// scheduledId : optional scheduler slot identifier (e.g. "CMD_1", "DT_2").
// type        : "rs232" or "system".
// ---------------------------------------------------------------------------
bool enqueueRS232Command(const String &command,
                         const String &templateName,
                         const String &scheduledId,
                         const String &type);

#endif // ENABLE_RS232==1
