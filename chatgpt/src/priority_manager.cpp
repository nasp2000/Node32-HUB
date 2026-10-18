// Priority Manager Implementation
#include "priority_manager.h"

PriorityManager* PriorityManager::instance = nullptr;

PriorityManager::PriorityManager() {
}

PriorityManager& PriorityManager::getInstance() {
    if (instance == nullptr) {
        instance = new PriorityManager();
    }
    return *instance;
}

void PriorityManager::begin() {
    // Create download mutex (exclusive access)
    if (downloadMutex == NULL) {
        downloadMutex = xSemaphoreCreateBinary();
        xSemaphoreGive(downloadMutex); // Start unlocked
    }

    // Reset all states
    sdWriteActive = false;
    emailActive = false;
    downloadActive = false;
    emaildayActive = false;

    Serial.println("[PRIORITY] Priority Manager initialized");
}

// ========== SD WRITE OPERATIONS (P2) ==========

bool PriorityManager::canStartSDWrite() {
    // SD writes can start anytime (P2); yields frequently to not block RS232/LPT
    return true;
}

void PriorityManager::startSDWrite() {
    sdWriteActive = true;
    lastSdWriteYield = millis();
}

void PriorityManager::endSDWrite() {
    sdWriteActive = false;
}

void PriorityManager::yieldDuringSDWrite() {
    unsigned long now = millis();
    // Yield every 10ms to allow RS232/LPT processing
    if (now - lastSdWriteYield.load() >= 10) {
        vTaskDelay(1);
        lastSdWriteYield = now;
    }
}

// ========== EMAIL OPERATIONS (P3) ==========

bool PriorityManager::canStartEmail() {
    // No String allocations here — called very frequently (every 5s from queue processor).
    // Email blocked if another email or a download is active.
    if (emailActive.load()) return false;
    if (downloadActive.load()) return false;
    return true;
}

void PriorityManager::startEmail() {
    // No Serial print — called from hot-path; log is done by the caller if needed.
    emailActive = true;
    lastEmailYield = millis();
}

void PriorityManager::endEmail() {
    emailActive = false;
}

void PriorityManager::yieldDuringEmail() {
    unsigned long now = millis();
    // Yield every 50ms to allow RS232/LPT and SD writes
    if (now - lastEmailYield.load() >= 50) {
        vTaskDelay(5);
        lastEmailYield = now;
    }
}

// ========== EMAILDAY OPERATIONS (P3 - special) ==========

bool PriorityManager::canStartEmailday() {
    // No String allocations — same reasoning as canStartEmail.
    if (emailActive.load() || emaildayActive.load()) return false;
    if (downloadActive.load()) return false;
    return true;
}

void PriorityManager::startEmailday() {
    emaildayActive = true;
    emailActive = true; // EMAILDAY is a subtype of email
    lastEmailYield = millis();
}

void PriorityManager::endEmailday() {
    emaildayActive = false;
    emailActive = false;
}

// ========== DOWNLOAD OPERATIONS (P4 - exclusive) ==========

bool PriorityManager::canStartDownload() {
    // Only one download at a time; must wait for any active email (P3).
    if (downloadActive.load()) return false;
    if (emailActive.load() || emaildayActive.load()) return false;
    return true;
}

bool PriorityManager::tryAcquireDownload(unsigned long timeoutMs) {
    if (downloadMutex == NULL) return false;
    if (!canStartDownload()) return false;

    TickType_t ticks = timeoutMs == 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    if (xSemaphoreTake(downloadMutex, ticks) != pdTRUE) return false;

    downloadActive = true;
    downloadStartTime = millis();
    downloadBytesServed = 0;
    return true;
}

void PriorityManager::releaseDownload() {
    if (downloadMutex == NULL) return;

    downloadActive = false;

    // snprintf into a stack buffer — no heap allocation
    char msg[96];
    snprintf(msg, sizeof(msg), "[PRIORITY] Download done (%u bytes, %lu ms)",
             (unsigned)downloadBytesServed.load(),
             (unsigned long)(millis() - downloadStartTime.load()));
    Serial.println(msg);

    xSemaphoreGive(downloadMutex);
}

void PriorityManager::updateDownloadProgress(size_t bytes) {
    downloadBytesServed.fetch_add(bytes);
}

void PriorityManager::checkDownloadTimeout() {
    const unsigned long DOWNLOAD_TIMEOUT_MS = 5UL * 60UL * 1000UL;
    if (downloadActive.load()) {
        unsigned long elapsed = millis() - downloadStartTime.load();
        if (elapsed > DOWNLOAD_TIMEOUT_MS) {
            char msg[72];
            snprintf(msg, sizeof(msg), "[PRIORITY] Download timeout (%lu ms) - force release", elapsed);
            Serial.println(msg);
            releaseDownload();
        }
    }
}

// ========== QUERY STATE ==========

// Uses a static stack buffer — no heap allocation, safe to call from any context.
String PriorityManager::getStatusString() const {
    char buf[48];
    snprintf(buf, sizeof(buf), "P:[%s%s%s%s]",
             sdWriteActive.load()  ? "SD " : "",
             emaildayActive.load() ? "EMAILDAY " : (emailActive.load() ? "EMAIL " : ""),
             downloadActive.load() ? "DL " : "",
             isBusy()              ? "" : "IDLE");
    return String(buf); // Single allocation, only when caller explicitly requests status
}

bool PriorityManager::isBusy() const {
    return sdWriteActive.load() || emailActive.load() || downloadActive.load();
}

// ========== EMERGENCY ==========

void PriorityManager::emergencyReset() {
    Serial.println("[PRIORITY] EMERGENCY RESET - clearing all locks");

    sdWriteActive = false;
    emailActive = false;
    downloadActive = false;
    emaildayActive = false;

    if (downloadMutex != NULL) {
        xSemaphoreGive(downloadMutex);
    }
}
