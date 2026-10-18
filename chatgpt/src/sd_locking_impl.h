// sd_locking_impl.h -- SD card mutex: safe lock/unlock helpers with deadlock detection.
// Included only from sdcard.cpp (single TU, all static variables shared).
// Do not include this file from any other translation unit.

// 🔒 Core acquire shared by sdMutexLock (web/background tasks) and the
//    delay-free sdMutexLockFast (time-sensitive audio playback reads, where a
//    50ms stall before every f_read would starve the DAC).
static bool sdMutexLockInternal(TickType_t timeoutTicks, bool fast) {
    if (sdMutex == NULL) return false;
    
    // 🚦 Add delay before attempting lock (reduces contention between cores)
    // Increased from 10ms to 50ms for better inter-core synchronization.
    // Skipped for the fast audio path (called on every SD read while playing).
    if (!fast) delay(50);
    
    // 🔒 CRITICAL: Check for soft recursion atomically
    taskENTER_CRITICAL(&spinlock);
    
    // 🛡️ Check for memory corruption via canaries
    if (sdMutexTracking.canary_before != 0xDEADBEEF || sdMutexTracking.canary_after != 0xCAFEBABE) {
        char msg[150];
        snprintf(msg, sizeof(msg), "[SD] MEMORY CORRUPTION: canaries violated! before=%08x after=%08x", 
                 sdMutexTracking.canary_before, sdMutexTracking.canary_after);
        taskEXIT_CRITICAL(&spinlock);
        sendVirtualMessage(msg, ERROR);
        // Reset to safe state
        taskENTER_CRITICAL(&spinlock);
        sdMutexTracking.canary_before = 0xDEADBEEF;
        sdMutexTracking.canary_after = 0xCAFEBABE;
        sdMutexTracking.ownerTask = NULL;
        sdMutexTracking.depth = 0;
    }
    
    // Sync legacy pointers
    sdMutexOwnerTask = sdMutexTracking.ownerTask;
    sdMutexDepth = sdMutexTracking.depth;
    
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    
    // 🛡️ SELF-HEALING: Detect impossible state (NULL owner but depth>0) and fix it
    if (sdMutexTracking.ownerTask == NULL && sdMutexTracking.depth > 0) {
        // This should NEVER happen - corrupted state, force reset
        char msg[200];
        snprintf(msg, sizeof(msg), "[SD] CORRUPTION DETECTED: owner=NULL but depth=%u at acquisition by task %p", 
                 sdMutexTracking.depth, self);
        taskEXIT_CRITICAL(&spinlock);
        sendVirtualMessage(msg, ERROR);
        
        // Log current call stack info (FreeRTOS task name if available)
        const char* taskName = pcTaskGetName(self);
        if (taskName) {
            char nameMsg[150];
            snprintf(nameMsg, sizeof(nameMsg), "[SD] Corrupted depth found in task: %s", taskName);
            sendVirtualMessage(nameMsg, ERROR);
        }
        
        taskENTER_CRITICAL(&spinlock);
        sdMutexTracking.depth = 0;
    }
    
    if (sdMutexTracking.ownerTask == self) {
        // Soft recursion: already own the mutex, just increment depth
        sdMutexTracking.depth++;
        sdMutexOwnerTask = sdMutexTracking.ownerTask;
        sdMutexDepth = sdMutexTracking.depth;
        taskEXIT_CRITICAL(&spinlock);
        return true;
    }
    taskEXIT_CRITICAL(&spinlock);
    
    // Attempt to acquire FreeRTOS mutex
    if (xSemaphoreTakeRecursive(sdMutex, timeoutTicks) != pdTRUE) {
        // 🛡️ EMERGENCY RECOVERY: If timeout AND tracking says unlocked, mutex is stuck
        taskENTER_CRITICAL(&spinlock);
        bool trackingSaysUnlocked = (sdMutexTracking.ownerTask == NULL && sdMutexTracking.depth == 0);
        taskEXIT_CRITICAL(&spinlock);
        
        if (trackingSaysUnlocked) {
            static unsigned long lastDeadlockLog = 0;
            unsigned long now = millis();
            if ((now - lastDeadlockLog) > 30000UL) {
                sendVirtualMessage("[SD] DEADLOCK: FreeRTOS mutex locked but tracking says unlocked!", ERROR);
                lastDeadlockLog = now;
            }
            // Note: Cannot safely destroy mutex here as other tasks may be using it
            // Manual recovery via SDMUTEXRESET command required
        }
        
        return false; // Timeout
    }
    
    // 🔒 CRITICAL: Update tracking atomically after successful acquisition
    taskENTER_CRITICAL(&spinlock);
    sdMutexTracking.ownerTask = self;
    sdMutexTracking.depth = 1;
    sdMutexOwnerTask = sdMutexTracking.ownerTask;
    sdMutexDepth = sdMutexTracking.depth;
    taskEXIT_CRITICAL(&spinlock);
    
    return true;
}

// 🔒 Helper: Safe lock wrapper that tracks ownership
bool sdMutexLock(TickType_t timeoutTicks) {
    return sdMutexLockInternal(timeoutTicks, false);
}

// 🔒 Delay-free variant for the audio playback path (every SD read while a
//    track plays). Same ownership tracking, so a fast lock taken by a task that
//    already holds the slow lock is detected as soft recursion.
bool sdMutexLockFast(TickType_t timeoutTicks) {
    return sdMutexLockInternal(timeoutTicks, true);
}

// 🔒 Core release shared by sdMutexUnlock / sdMutexUnlockFast.
static void sdMutexUnlockInternal(bool fast) {
    if (sdMutex == NULL) return;
    
    // 🔒 CRITICAL: Disable interrupts for atomic read-modify-write of tracking variables
    taskENTER_CRITICAL(&spinlock);
    
    // Sync from struct
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    
    // 🔒 CRITICAL: Only unlock if we own the mutex (prevents xTaskPriorityDisinherit crash)
    if (sdMutexTracking.ownerTask != self || sdMutexTracking.depth == 0) {
        // ⚠️ This task doesn't own the mutex - don't unlock!
        taskEXIT_CRITICAL(&spinlock);
        return;
    }
    
    sdMutexTracking.depth--;
    
    // Only release mutex when fully unwound
    if (sdMutexTracking.depth == 0) {
        sdMutexTracking.ownerTask = NULL;
        sdMutexOwnerTask = NULL;
        sdMutexDepth = 0;
        taskEXIT_CRITICAL(&spinlock);
        
        // Release FreeRTOS mutex AFTER clearing tracking (atomic operation complete)
        xSemaphoreGiveRecursive(sdMutex);
        
        // 🚦 Add delay after unlock (allows other tasks to acquire, reduces race conditions)
        // Increased from 10ms to 50ms for better inter-core synchronization.
        // Skipped for the fast audio path.
        if (!fast) delay(50);
    } else {
        sdMutexOwnerTask = sdMutexTracking.ownerTask;
        sdMutexDepth = sdMutexTracking.depth;
        taskEXIT_CRITICAL(&spinlock);
    }
}

// 🔒 Helper: Safe unlock wrapper that checks ownership before releasing
void sdMutexUnlock() {
    sdMutexUnlockInternal(false);
}

// 🔒 Delay-free unlock paired with sdMutexLockFast.
void sdMutexUnlockFast() {
    sdMutexUnlockInternal(true);
}

