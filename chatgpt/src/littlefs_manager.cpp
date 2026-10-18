// Flash Filesystem Manager implementation
// Supports LittleFS, FFat, and SPIFFS based on feature_flags.h configuration
#include "littlefs_manager.h"

#include "feature_flags.h"
#if ENABLE_FILESYSTEM==1

#if FILESYSTEM_PROVIDER == 1
#include <LittleFS.h>
#define FS_INSTANCE LittleFS
const char* FS_NAME = "LittleFS";
#elif FILESYSTEM_PROVIDER == 2
#include <FFat.h>
#include "feature_flags.h"
#define FS_INSTANCE FFat
const char* FS_NAME = "FFat";
#elif FILESYSTEM_PROVIDER == 3
#include <SPIFFS.h>
#define FS_INSTANCE SPIFFS
const char* FS_NAME = "SPIFFS";
#else
#error "Invalid FILESYSTEM_PROVIDER in feature_flags.h"
#endif

#include <esp_partition.h>
#include <esp_task_wdt.h>

#include "lfs_mutex_guard.h"

static volatile bool _fsMounting = false;
static bool _fsMounted = false;
static bool _fsDiagDumped = false;
static bool _fsSubtypeMismatchLockout = false;
static bool _fsBlankRecoveryTried = false;

// ESP-IDF uses subtype 0x82 for SPIFFS data partitions.
// This project expects LittleFS when FILESYSTEM_PROVIDER==1.
static constexpr uint8_t kDataSubtypeSpiffs = 0x82;

static void dumpDataPartitionsOnce() {
    if (_fsDiagDumped) return;
    _fsDiagDumped = true;

    Serial.println("[FS_MANAGER] DATA partition dump (for mount diagnosis):");
    bool foundStorageLabel = false;

    esp_partition_iterator_t it = esp_partition_find(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        nullptr);

    while (it != nullptr) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p) {
            Serial.printf("  - label='%s' subtype=0x%02X offset=0x%06lX size=0x%06lX\n",
                          p->label,
                          (unsigned)p->subtype,
                          (unsigned long)p->address,
                          (unsigned long)p->size);
            if (strcmp(p->label, "storage") == 0) {
                foundStorageLabel = true;
            }
        }
        // esp_partition_next() already releases the current iterator internally.
        // Releasing it again triggers heap corruption/double-free assertions.
        it = esp_partition_next(it);
    }

    if (!foundStorageLabel) {
        Serial.println("[FS_MANAGER] WARNING: partition label 'storage' not found");
    }
}

static bool isStoragePartitionBlank(const esp_partition_t* storagePart) {
    if (!storagePart) return false;

    uint8_t probe[256];
    esp_err_t err = esp_partition_read(storagePart, 0, probe, sizeof(probe));
    if (err != ESP_OK) return false;

    for (size_t i = 0; i < sizeof(probe); ++i) {
        if (probe[i] != 0xFF) return false;
    }
    return true;
}

bool littlefsBegin() {
    // Thread safety against concurrent mounts from taskInitFlash and SensorConfigManager
    if (_fsMounted) return true;
    if (_fsSubtypeMismatchLockout) return false;

    // Simple lock to prevent dual concurrent mounts
    while (_fsMounting) {
        delay(10);
        if (_fsMounted) return true;
    }

    _fsMounting = true;

    // Detect if already mounted by another subsystem (e.g. ConfigManager calling
    // LittleFS.begin() directly). A mounted FS always returns totalBytes > 0.
    if (FS_INSTANCE.totalBytes() > 0) {
        Serial.println("[FS_MANAGER] Filesystem already mounted by external caller - adopting mount");
        _fsMounted = true;
        _fsMounting = false;
        return true;
    }

    Serial.printf("[%s] Mounting filesystem...\n", FS_NAME);
    bool result = false;

#if FILESYSTEM_PROVIDER == 1
    const esp_partition_t* storagePart = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "storage");

    // Fast-fail guard: avoid mounting LittleFS on a SPIFFS-typed "storage" partition.
    // This state cannot be fixed at runtime; user must flash the correct partitions.bin.
    if (storagePart && storagePart->subtype == kDataSubtypeSpiffs) {
        Serial.println("[FS_MANAGER] ERROR: partition 'storage' subtype is SPIFFS (0x82) but FILESYSTEM_PROVIDER=LittleFS.");
        Serial.println("[FS_MANAGER] ACTION: rebuild and flash bootloader+partitions+firmware (mode 2/3). Runtime mount disabled.");
        dumpDataPartitionsOnce();
        _fsSubtypeMismatchLockout = true;
        _fsMounting = false;
        return false;
    }

    // LittleFS: first try explicit partition label from partitions.csv.
    result = FS_INSTANCE.begin(false, "/littlefs", 10, "storage");
    if (!result) {
        Serial.println("[LittleFS] Mount with label 'storage' failed. Runtime auto-format is disabled.");

        // Safe recovery path: only for blank storage (typical right after full erase/flash).
        // Keeps normal boots non-destructive while allowing first-boot initialization.
        if (!_fsBlankRecoveryTried && isStoragePartitionBlank(storagePart)) {
            _fsBlankRecoveryTried = true;
            Serial.println("[LittleFS] Storage partition appears blank (0xFF). Running one-time recovery format...");
            if (littlefsFormat()) {
                result = FS_INSTANCE.begin(false, "/littlefs", 10, "storage");
                if (result) {
                    Serial.println("[LittleFS] Recovery format succeeded and mount is now OK.");
                } else {
                    Serial.println("[LittleFS] Recovery format ran, but mount still failed.");
                }
            } else {
                Serial.println("[LittleFS] Recovery format failed.");
            }
        }

        // Last resort: partition exists, has data, but mount still failed.
        // Data is unreadable anyway — force format once per boot.
        if (!result && storagePart && !isStoragePartitionBlank(storagePart)) {
            _fsBlankRecoveryTried = true;
            Serial.println("[LittleFS] WARNING: partition 'storage' has data but mount failed — forcing format (config will be lost)");
            if (littlefsFormat()) {
                result = FS_INSTANCE.begin(false, "/littlefs", 10, "storage");
                if (result) {
                    Serial.println("[LittleFS] Forced recovery format succeeded.");
                } else {
                    Serial.println("[LittleFS] Forced format ran, but mount still failed.");
                }
            } else {
                Serial.println("[LittleFS] Forced recovery format failed.");
            }
        }
    }

#elif FILESYSTEM_PROVIDER == 2
    Serial.println("[FFat] Attempting non-destructive mount");
    result = FS_INSTANCE.begin(false, "/ffat", 10, "storage");
    if (!result) {
        Serial.println("[FFat] Mount failed (auto-format disabled)");
    }

#elif FILESYSTEM_PROVIDER == 3
    Serial.println("[SPIFFS] Attempting non-destructive mount");
    result = FS_INSTANCE.begin(false, "/spiffs", 10, "storage");
    if (!result) {
        Serial.println("[SPIFFS] Mount failed (auto-format disabled)");
    }
#endif

    if (!result) {
        dumpDataPartitionsOnce();
        Serial.printf("[%s] Mount failed\n", FS_NAME);
        _fsMounting = false;
        return false;
    }

    _fsMounted = true;
    _fsMounting = false;
    Serial.printf("[%s] Mounted successfully\n", FS_NAME);

    uint64_t total = FS_INSTANCE.totalBytes();
    uint64_t used = FS_INSTANCE.usedBytes();
    uint64_t free = total - used;
    Serial.printf("[%s] Total: %.2f MB, Used: %.2f MB, Free: %.2f MB\n",
                  FS_NAME,
                  total / (1024.0 * 1024.0),
                  used / (1024.0 * 1024.0),
                  free / (1024.0 * 1024.0));
    return true;
}

bool littlefsEnd() {
    if (!_fsMounted) return true;

    FS_INSTANCE.end();
    _fsMounted = false;
    Serial.printf("[%s] Unmounted\n", FS_NAME);
    return true;
}

bool littlefsMounted() {
    return _fsMounted;
}

uint64_t littlefsTotalBytes() {
    if (!_fsMounted) return 0;
    return FS_INSTANCE.totalBytes();
}

uint64_t littlefsUsedBytes() {
    if (!_fsMounted) return 0;
    return FS_INSTANCE.usedBytes();
}

uint64_t littlefsFreeBytes() {
    if (!_fsMounted) return 0;
    return littlefsTotalBytes() - littlefsUsedBytes();
}

bool littlefsFormat() {
    // Manual/destructive operation used only by explicit admin/factory-reset paths.
    if (_fsMounted) {
        FS_INSTANCE.end();
        _fsMounted = false;
    }

#if FILESYSTEM_PROVIDER == 1
    // Use formatOnFail=true so LittleFS auto-formats the blank partition.
    // After a full flash erase, the storage partition is all 0xFF — begin()
    // with formatOnFail=false cannot mount it, and there is no standalone
    // format() API.  formatOnFail=true handles both mount and format.
    bool ctxOk = FS_INSTANCE.begin(true, "/littlefs", 10, "storage");
    if (!ctxOk) {
        Serial.println("[LittleFS] format: failed with label 'storage' and formatOnFail, trying default context...");
        ctxOk = FS_INSTANCE.begin(true);
        if (!ctxOk) {
            Serial.println("[LittleFS] format: default context also failed — cannot format");
            return false;
        }
    }
#endif

    bool ok = FS_INSTANCE.format();
    FS_INSTANCE.end();
    _fsMounted = false;
    Serial.printf("[%s] format: %s\n", FS_NAME, ok ? "ok" : "FAILED");
    return ok;
}

fs::FS& getLittleFS() {
    return FS_INSTANCE;
}

// Cross-task LittleFS guard (see littlefs_manager.h). Created on first
// littlefsBegin (setup single-threaded phase, like sdMutex in sdcardBegin);
// lazily re-created if somehow NULL (never blocks creation).
SemaphoreHandle_t lfsMutex = NULL;

// Race-safe lazy creation. Two tasks can hit this simultaneously at boot, and
// a plain `if (NULL) create()` lets both allocate: one instance leaks and the
// two tasks then guard against different mutexes (i.e. no protection at all).
// lfsMutexPublishOnce allocates outside any lock and uses an atomic CAS to
// pick a single winner (covered by tests/native/test_lfs_mutex_guard.cpp).
static SemaphoreHandle_t lfsMutexEnsure() {
    return lfsMutexPublishOnce<SemaphoreHandle_t>(
        &lfsMutex,
        []() -> SemaphoreHandle_t { return xSemaphoreCreateRecursiveMutex(); },
        [](SemaphoreHandle_t h) { vSemaphoreDelete(h); });
}

bool lfsMutexLock(TickType_t timeoutTicks) {
    SemaphoreHandle_t m = lfsMutexEnsure();
    if (m == NULL) return false;
    // WDT-safe wait: block in short slices and feed the task watchdog between
    // attempts, so long contention (e.g. a large LittleFS flush in the loop
    // task) can never trip a TWDT panic from the waiting task.
    const uint32_t timeoutMs = (uint32_t)(timeoutTicks * portTICK_PERIOD_MS);
    return lfsMutexWaitSliced(
        timeoutMs, 200,
        [m](uint32_t ms) { return xSemaphoreTakeRecursive(m, pdMS_TO_TICKS(ms)) == pdTRUE; },
        []() { esp_task_wdt_reset(); });
}

void lfsMutexUnlock() {
    if (lfsMutex != NULL) xSemaphoreGiveRecursive(lfsMutex);
}

#endif // ENABLE_FILESYSTEM
