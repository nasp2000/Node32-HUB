// rx_buffers_impl.h
// Included only from chatgpt.ino (single translation unit).
static inline bool bufferDadosLock(TickType_t timeoutTicks, bool &didLockOut) {
    didLockOut = false;
    unsigned long timeoutMs = timeoutTicks * portTICK_PERIOD_MS;
    unsigned long start = millis();
    while (true) {
        taskENTER_CRITICAL(&bufferDadosSpinlock);
        if (!bufferDadosBusy) {
            bufferDadosBusy = true;
            taskEXIT_CRITICAL(&bufferDadosSpinlock);
            didLockOut = true;
            return true;
        }
        taskEXIT_CRITICAL(&bufferDadosSpinlock);
        if (millis() - start > timeoutMs) {
            bufferDadosMutexFailCount = bufferDadosMutexFailCount + 1;
            return false; // timeout
        }
        vTaskDelay(1); // Yield CPU
    }
}

static inline void bufferDadosUnlock(bool didLock) {
    if (didLock) bufferDadosBusy = false; // Release lock
}

static inline bool bufferDadosTryIsEmpty(bool &outEmpty, TickType_t timeoutTicks = 0) {
    bool didLock = false;
    if (!bufferDadosLock(timeoutTicks, didLock)) return false;
    outEmpty = bufferDados.isEmpty();
    bufferDadosUnlock(didLock);
    return true;
}

static inline bool bufferDadosTryGetSize(size_t &outSize, TickType_t timeoutTicks = 0) {
    bool didLock = false;
    if (!bufferDadosLock(timeoutTicks, didLock)) return false;
    outSize = bufferDados.size();
    bufferDadosUnlock(didLock);
    return true;
}

static inline bool bufferDadosTryClear(TickType_t timeoutTicks = pdMS_TO_TICKS(20)) {
    bool didLock = false;
    if (!bufferDadosLock(timeoutTicks, didLock)) return false;
    bufferDados.clear();
    bufferDadosUnlock(didLock);
    return true;
}

// Trim oldest bytes from bufferDados to free at least 'neededBytes' of space.
// MUST be called while the bufferDados mutex is ALREADY held (not re-entrant).
// After evicting enough bytes, keeps shifting until a newline is reached so the
// buffer head is always at the start of a complete line.
static void bufferDadosTrimOldest(size_t neededBytes) {
    if (bufferDados.capacity() == 0) return;
    // Add 10 % of capacity as headroom to reduce frequency of future trims.
    const size_t target = neededBytes + bufferDados.capacity() / 10;
    size_t evicted = 0;
    while (bufferDados.size() > 0) {
        size_t freeNow = bufferDados.capacity() - bufferDados.size();
        bool hasRoom = (freeNow >= neededBytes);
        uint8_t b = bufferDados.shift();
        evicted++;
        // Stop once we have enough room AND we are on a clean line boundary.
        if (hasRoom && b == '\n') break;
        // Hard cap: never evict more than the headroom target.
        if (evicted >= target) break;
    }
    if (evicted > 0) {
        Serial.printf("[BUF-TRIM] evicted %u bytes, free %u/%u\n",
                      evicted,
                      bufferDados.capacity() - bufferDados.size(),
                      bufferDados.capacity());
    }
}

// Helper: push into bufferDados while holding the mutex.
// If the mutex cannot be acquired quickly, drop the byte instead of writing without protection.
static inline bool bufferDadosSafePush(uint8_t b) {
    bool didLock = false;
    if (!bufferDadosLock(pdMS_TO_TICKS(20), didLock)) {
        bufferDadosPushMutexFailCount = bufferDadosPushMutexFailCount + 1;
        return false;
    }
    (void)bufferDados.push(b);
    bufferDadosUnlock(didLock);
    return true;
}

// ===== LPT Buffer Protection Functions (mirror of bufferDados functions) =====
// Forward-declare bufferDadosLPT
extern PsramCircularBuffer<uint8_t> bufferDadosLPT;

static inline bool bufferDadosLPTLock(TickType_t timeoutTicks, bool &didLockOut) {
    didLockOut = false;
    unsigned long timeoutMs = timeoutTicks * portTICK_PERIOD_MS;
    unsigned long start = millis();
    while (true) {
        taskENTER_CRITICAL(&bufferDadosLPTSpinlock);
        if (!bufferDadosLPTBusy) {
            bufferDadosLPTBusy = true;
            taskEXIT_CRITICAL(&bufferDadosLPTSpinlock);
            didLockOut = true;
            return true;
        }
        taskEXIT_CRITICAL(&bufferDadosLPTSpinlock);
        if (millis() - start > timeoutMs) {
            bufferDadosLPTMutexFailCount = bufferDadosLPTMutexFailCount + 1;
            return false;
        }
        vTaskDelay(1);
    }
}

static inline void bufferDadosLPTUnlock(bool didLock) {
    if (didLock) bufferDadosLPTBusy = false;
}

static inline bool bufferDadosLPTTryIsEmpty(bool &outEmpty, TickType_t timeoutTicks = 0) {
    bool didLock = false;
    if (!bufferDadosLPTLock(timeoutTicks, didLock)) return false;
    outEmpty = bufferDadosLPT.isEmpty();
    bufferDadosLPTUnlock(didLock);
    return true;
}

static inline bool bufferDadosLPTTryGetSize(size_t &outSize, TickType_t timeoutTicks = 0) {
    bool didLock = false;
    if (!bufferDadosLPTLock(timeoutTicks, didLock)) return false;
    outSize = bufferDadosLPT.size();
    bufferDadosLPTUnlock(didLock);
    return true;
}

static inline bool bufferDadosLPTTryClear(TickType_t timeoutTicks = pdMS_TO_TICKS(20)) {
    bool didLock = false;
    if (!bufferDadosLPTLock(timeoutTicks, didLock)) return false;
    bufferDadosLPT.clear();
    bufferDadosLPTUnlock(didLock);
    return true;
}

static inline bool bufferDadosLPTSafePush(uint8_t b) {
    bool didLock = false;
    if (!bufferDadosLPTLock(pdMS_TO_TICKS(20), didLock)) {
        bufferDadosLPTPushMutexFailCount = bufferDadosLPTPushMutexFailCount + 1;
        return false;
    }
    (void)bufferDadosLPT.push(b);
    bufferDadosLPTUnlock(didLock);
    return true;
}