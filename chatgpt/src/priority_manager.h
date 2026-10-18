// Priority Manager - Coordinates heavy operations honoring priorities
// P1: RS232/LPT reception (highest - never blocks)
// P2: SD card writes (chunked, yields between chunks)
// P3: Email send (checks before starting, yields periodically)
// P4: Web/Downloads (lowest - one at a time, checks before starting)

#ifndef PRIORITY_MANAGER_H
#define PRIORITY_MANAGER_H

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Operation states
enum class OperationState {
    IDLE = 0,
    ACTIVE = 1,
    WAITING = 2
};

// Priority levels
enum class Priority {
    P1_RS232_LPT = 1,    // Highest - data reception (never blocks)
    P2_SD_WRITE = 2,     // SD card writes (chunked, cooperative)
    P3_EMAIL = 3,        // Email operations (yields periodically)
    P4_WEB_DOWNLOAD = 4  // Lowest - web/downloads (one at a time)
};

class PriorityManager {
private:
    // Atomic flags for current operation states
    std::atomic<bool> sdWriteActive{false};
    std::atomic<bool> emailActive{false};
    std::atomic<bool> downloadActive{false};
    std::atomic<bool> emaildayActive{false};
    
    // Mutex for download exclusivity (only one download at a time)
    SemaphoreHandle_t downloadMutex = NULL;
    
    // Tracking
    std::atomic<unsigned long> lastSdWriteYield{0};
    std::atomic<unsigned long> lastEmailYield{0};
    std::atomic<unsigned long> downloadStartTime{0};
    std::atomic<size_t> downloadBytesServed{0};
    
    // Singleton
    static PriorityManager* instance;
    PriorityManager();
    
public:
    static PriorityManager& getInstance();
    
    // Initialize system
    void begin();
    
    // SD Write operations (P2)
    bool canStartSDWrite();
    void startSDWrite();
    void endSDWrite();
    bool isSDWriteActive() const { return sdWriteActive.load(); }
    void yieldDuringSDWrite(); // Called between chunks
    
    // Email operations (P3)
    bool canStartEmail();
    void startEmail();
    void endEmail();
    bool isEmailActive() const { return emailActive.load(); }
    void yieldDuringEmail(); // Called periodically during send
    
    // EMAILDAY operations (P3 - special)
    bool canStartEmailday();
    void startEmailday();
    void endEmailday();
    bool isEmaildayActive() const { return emaildayActive.load(); }
    
    // Download operations (P4 - exclusive)
    bool canStartDownload();
    bool tryAcquireDownload(unsigned long timeoutMs = 5000);
    void releaseDownload();
    bool isDownloadActive() const { return downloadActive.load(); }
    void updateDownloadProgress(size_t bytes);
    void checkDownloadTimeout(); // Auto-release if stuck (call periodically)
    
    // Query system state
    String getStatusString() const;
    bool isBusy() const; // Any operation active
    
    // Emergency: force reset all locks (use with caution)
    void emergencyReset();
};

// Global instance access
inline PriorityManager& priorityManager() {
    return PriorityManager::getInstance();
}

#endif // PRIORITY_MANAGER_H
