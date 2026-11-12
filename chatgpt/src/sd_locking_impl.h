// sd_locking_impl.h -- SD card mutex: safe lock/unlock helpers with deadlock detection.
// Included only from sdcard.cpp (single TU, all static variables shared).
// Do not include this file from any other translation unit.

// 🕒 Onset of the current CONTINUOUS hold (0 = free). Maintained by the lock
// path itself, so the watchdog works even when the optional debug label
// (sdMutexHolder, set only by the SD-write pipeline) was never touched — that
// was why a leaked lock on Cam-M survived for hours without a single log line.
static volatile unsigned long sdMutexHoldStartMs = 0;

// Diagnostic label/timestamp owned by the main TU (sd_tasks_impl.h). The lock
// path clears them on full release so a stale label can never describe a
// different owner.
extern volatile const char* sdMutexHolder;
extern volatile unsigned long sdMutexAcquiredAt;

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
        if (sdMutexTracking.depth == 0) sdMutexHoldStartMs = millis();
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
    sdMutexHoldStartMs = millis();
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
        sdMutexHoldStartMs = 0;
        sdMutexAcquiredAt = 0;
        sdMutexHolder = nullptr;
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

// 📊 Snapshot for diagnostics: who holds the mutex, recursion depth and for how
// long. Returns false when nobody owns it (heldMs/owner are then 0/empty).
// ownerName is the FreeRTOS task name (e.g. "CamRecWr", "async_tcp", "loopTask"),
// which is what identifies a leaked lock without a serial console.
bool sdMutexGetSnapshot(uint32_t* depthOut, unsigned long* heldMsOut,
                        char* ownerNameOut, size_t ownerNameCap) {
    taskENTER_CRITICAL(&spinlock);
    TaskHandle_t owner = sdMutexTracking.ownerTask;
    uint32_t depth = sdMutexTracking.depth;
    unsigned long startMs = sdMutexHoldStartMs;
    taskEXIT_CRITICAL(&spinlock);

    const bool held = (owner != NULL && depth > 0);
    if (depthOut) *depthOut = held ? depth : 0;
    if (heldMsOut) *heldMsOut = (held && startMs) ? (unsigned long)(millis() - startMs) : 0UL;
    if (ownerNameOut && ownerNameCap) {
        const char* nm = held ? pcTaskGetName(owner) : nullptr;
        ownerNameOut[0] = 0;
        if (nm && nm[0]) {
            strncpy(ownerNameOut, nm, ownerNameCap - 1);
            ownerNameOut[ownerNameCap - 1] = 0;
        }
    }
    return held;
}

// 🚑 Emergency recovery for a leaked lock: publish a NEW recursive mutex and
// wipe ownership tracking, so a stuck SD no longer needs a reboot or a serial
// console. Used by the loop watchdog (held > SD_MUTEX_RESET_AFTER_MS) and by
// POST /api/sd/mutex-reset. Refuses when the caller is the owner.
//
// The old handle is ABANDONED, never deleted on the spot: tasks may be blocked
// inside xSemaphoreTakeRecursive on it (timeouts up to 30 s), and deleting a
// mutex under waiters is undefined behaviour. Abandoned handles are freed once
// they are older than every lock timeout, when nobody can reference them.
bool sdMutexEmergencyReset(const char* reason) {
    static unsigned long lastResetMs = 0;
    static SemaphoreHandle_t retired[4] = {nullptr, nullptr, nullptr, nullptr};
    static unsigned long retiredAt[4] = {0, 0, 0, 0};
    static uint8_t retiredNext = 0;
    const unsigned long now = millis();
    if (lastResetMs != 0 && (unsigned long)(now - lastResetMs) < 10000UL) return false;

    // Free abandoned handles nobody can still be waiting on any more.
    for (uint8_t i = 0; i < 4; i++) {
        if (retired[i] && (unsigned long)(now - retiredAt[i]) > 60000UL) {
            vSemaphoreDelete(retired[i]);
            retired[i] = nullptr;
            retiredAt[i] = 0;
        }
    }

    uint32_t depth = 0;
    unsigned long heldMs = 0;
    char owner[24];
    const bool held = sdMutexGetSnapshot(&depth, &heldMs, owner, sizeof(owner));

    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    taskENTER_CRITICAL(&spinlock);
    const bool selfOwns = (sdMutexTracking.ownerTask == self);
    taskEXIT_CRITICAL(&spinlock);
    if (selfOwns) return false;

    char msg[240];
    snprintf(msg, sizeof(msg),
             "🚑 [SD] Emergency mutex reset (%s): held_by='%s' depth=%u held=%lums",
             reason ? reason : "?", held ? owner : "none",
             (unsigned)depth, heldMs);
    sendVirtualMessage(msg, held ? ERROR : WARN, false, true);

    SemaphoreHandle_t fresh = xSemaphoreCreateRecursiveMutex();
    if (fresh == NULL) {
        sendVirtualMessage(F("🚑 [SD] Mutex recreate FAILED — SD access disabled"), ERROR, false, true);
        return false;
    }

    taskENTER_CRITICAL(&spinlock);
    SemaphoreHandle_t old = sdMutex;
    sdMutex = fresh;
    sdMutexTracking.canary_before = 0xDEADBEEF;
    sdMutexTracking.canary_after = 0xCAFEBABE;
    sdMutexTracking.ownerTask = NULL;
    sdMutexTracking.depth = 0;
    sdMutexHoldStartMs = 0;
    sdMutexOwnerTask = NULL;
    sdMutexDepth = 0;
    sdMutexHolder = nullptr;
    sdMutexAcquiredAt = 0;
    taskEXIT_CRITICAL(&spinlock);

    if (old) {                        // retire, do not delete under waiters
        retired[retiredNext] = old;
        retiredAt[retiredNext] = now;
        retiredNext = (uint8_t)((retiredNext + 1) & 3);
    }

    lastResetMs = now;
    sendVirtualMessage(F("✅ [SD] Mutex recovered; SD access restored (a reboot is still advisable)"), WARN, true, true);
    return true;
}

