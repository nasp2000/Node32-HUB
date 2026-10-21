// sd_tasks_impl.h
// Included only from chatgpt.ino (single translation unit).

// Line accumulator size — used unconditionally by setup_impl.h / loop_impl.h
#define SDCARD_LINE_ACCUMULATOR_SIZE 512

// Forward declarations (defined later in .ino chain)
#if ENABLE_SDCARD==1
static size_t logDataToSdcardTimestamped(const String &source, std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>& dataChunks);
#endif

// SD buffer swap mutex (defined here instead of .ino for correct include ordering)
#if ENABLE_SDCARD==1
static SemaphoreHandle_t sdcardBufferSwapMutex = NULL;
static TaskHandle_t sdcardBufferSwapMutexOwnerTask = NULL;
static volatile uint32_t sdcardBufferSwapMutexBadUnlockCount = 0;
static volatile unsigned long sdcardBufferSwapMutexFailCount = 0;
static uint32_t sdcardBufferSwapMutexDepth = 0;

// SD double-buffer vars (PSRAM double-buffering)
static std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> sdcardLogBuffer_RS232_A;
static std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> sdcardLogBuffer_RS232_B;
static std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> sdcardLogBuffer_LPT_A;
static std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> sdcardLogBuffer_LPT_B;

std::atomic<std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>*> sdcardActiveBuffer_RS232(&sdcardLogBuffer_RS232_A);
std::atomic<std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>*> sdcardFlushBuffer_RS232(&sdcardLogBuffer_RS232_B);
std::atomic<std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>*> sdcardActiveBuffer_LPT(&sdcardLogBuffer_LPT_A);
std::atomic<std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>*> sdcardFlushBuffer_LPT(&sdcardLogBuffer_LPT_B);

static std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> sdcardFreezeBuffer_RS232;
static std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> sdcardFreezeBuffer_LPT;
static size_t sdcardFreezeBytes_RS232 = 0;
static size_t sdcardFreezeBytes_LPT = 0;

static char* sdcardLineAccumulator_RS232 = nullptr;
static size_t sdcardLineAccumulator_RS232_len = 0;
static char* sdcardLineAccumulator_LPT = nullptr;
static size_t sdcardLineAccumulator_LPT_len = 0;
static unsigned long sdcardLineStartTimestamp_RS232 = 0;
static unsigned long sdcardLineStartTimestamp_LPT = 0;

PsramCircularBuffer<Rs232LivePoint> rs232LiveBuffer(RS232_LIVE_BUFFER_CAPACITY, true);
SemaphoreHandle_t rs232LiveMutex = NULL;
#endif

// Inactivity alert tracking — not SD-specific: used by proximity, arduino_bridge, wifi_config etc.
unsigned long lastDataReceivedForInactivityAlert = 0; // Timestamp of last data reception (any source) - EXTERN for arduino_bridge
static int inactivityAlertsToday = 0; // Counter of alerts sent today (legacy global)
static int inactivityAlertLastDay = -1; // Day of the month for the last alert (to reset counter)
bool inactivityAlertSent = false; // Flag to prevent multiple alerts for same inactivity period (EXTERN for arduino_bridge)
int inactivityAlertsTodaySlot[6] = {0,0,0,0,0,0}; // Per-slot daily alert counters (EXTERN)
bool inactivityAlertSentSlot[6] = {false,false,false,false,false,false}; // Per-slot inactivity period flags (EXTERN)

#if ENABLE_SDCARD==1
static void taskInitFlash(void *param);  // forward declaration: defined further below
bool sdcardSwapLock(TickType_t timeoutTicks) {
    if (sdcardBufferSwapMutex == NULL) return false;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (sdcardBufferSwapMutexOwnerTask == self) {
        // Soft recursion: do not take the FreeRTOS mutex again.
        // This prevents deadlocks if a caller re-enters a path that needs the swap lock.
        sdcardBufferSwapMutexDepth++;
        return true;
    }
    // Use TakeRecursive for recursive mutex
    if (xSemaphoreTakeRecursive(sdcardBufferSwapMutex, timeoutTicks) != pdTRUE) {
        sdcardBufferSwapMutexFailCount = sdcardBufferSwapMutexFailCount + 1;
        return false;
    }
    sdcardBufferSwapMutexOwnerTask = self;
    sdcardBufferSwapMutexDepth = 1;
    return true;
}

void sdcardSwapUnlock() {
    if (sdcardBufferSwapMutex == NULL) return;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (sdcardBufferSwapMutexOwnerTask != self || sdcardBufferSwapMutexDepth == 0) {
        sdcardBufferSwapMutexBadUnlockCount = sdcardBufferSwapMutexBadUnlockCount + 1;
        return;
    }
    sdcardBufferSwapMutexDepth--;
    if (sdcardBufferSwapMutexDepth == 0) {
        sdcardBufferSwapMutexOwnerTask = NULL;
        // Use GiveRecursive for recursive mutex
        xSemaphoreGiveRecursive(sdcardBufferSwapMutex);
    }
}

// acquireSDAccess / releaseSDAccess moved to runtime_storage.cpp

// Mutex ownership tracking (sdMutexOwnerTask/sdMutexDepth declared as extern in sdcard.h, defined in sdcard.cpp)
static volatile int sdMutexOwnerCore = -1; // Core ID of owner task

// SD mutex diagnostics (debug tracking of who holds the mutex) - GLOBAL for watchdog access
volatile const char* sdMutexHolder = nullptr; // Current holder identifier
volatile unsigned long sdMutexAcquiredAt = 0; // Timestamp when acquired

// sdMutexTrackAcquire / Release / DumpState (extern declared in sdcard.h)
// Must be in this TU (sd_tasks_impl.h) because they access sdMutexHolder/sdMutexAcquiredAt.
inline void sdMutexTrackAcquire(const char* holder) {
    sdMutexHolder = holder;
    sdMutexAcquiredAt = millis();
}
inline void sdMutexTrackRelease() {
    sdMutexHolder = nullptr;
    sdMutexAcquiredAt = 0;
}
inline void sdMutexDumpState() {
    if (sdMutexHolder) {
        String msg = "SD Mutex held by: ";
        msg += String(const_cast<const char*>(sdMutexHolder));
        msg += " for ";
        msg += String(millis() - sdMutexAcquiredAt);
        msg += "ms";
        sendVirtualMessage(msg, DEBUG);
    }
}

// Task handle and notification flag for asynchronous SD writes
TaskHandle_t sdCardWriteTaskHandle = NULL;
// Stack increased from 8192: logDataToSdcardTimestamped + SD_MMC driver use up to ~10KB peak depth.
// PSRAM-allocated so internal heap is not affected.
#define SDCARD_WRITE_TASK_STACK_SIZE 16384
static StackType_t* sdCardWriteTaskStack = nullptr;
static StaticTask_t* sdCardWriteTaskTCB = nullptr;

// Control timestamps.
unsigned long lastSdcardFlush_RS232 = 0;
unsigned long lastSdcardFlush_LPT = 0;
static unsigned long lastSdcardDataReceived_RS232 = 0; // Last time received RS232 data
static unsigned long sdcardPushMutexFailCount_RS232 = 0; // Counter: mutex take failures during append
static unsigned long sdcardPushPointerNullCount_RS232 = 0; // Counter: null active pointer (diagnostic)
static unsigned long lastSdcardDataReceived_LPT = 0;   // Last time received LPT data
static const unsigned long SDCARD_DEFAULT_WRITE_PAUSE_MS = 200; // Default inactivity window before flushing to SD (200ms)

// Storage mode control (currentStorageMode, checkAndSwitchStorageMode, retrySDStorage, etc.)
// moved to runtime_storage.cpp — declared in runtime_storage.h (included above).

// ============================================================================
static const size_t SDCARD_BUFFER_MAX_SIZE = 131072; // Max 128KB (PSRAM)
static size_t SDCARD_BUFFER_THRESHOLD_FLUSH = 98304; // 96KB - immediate flush (can be adjusted at runtime)

// Retry buffer for SD write failures (bounded to protect heap/PSRAM).
static const size_t SDCARD_RETRY_BUFFER_MAX_BYTES_TOTAL = 128UL * 1024UL; // 128KB max retry buffer

// IMPROVEMENT 1: Atomic byte counters for lock-free size tracking
// SD buffering accounting (bytes, not vector size) to avoid O(n) scans on every push.
std::atomic<size_t> sdcardActiveBytes_RS232(0);
std::atomic<size_t> sdcardActiveBytes_LPT(0);

// Per-source sequence counters provide deterministic ordering and a stable cutoff point for EMAILDAY/REBOOT.
uint32_t sdcardSeq_RS232 = 0;
uint32_t sdcardSeq_LPT = 0;

// When true, automatic SD swaps/flushes are paused. Used to keep EMAILDAY on a stable SD snapshot.
// Exported (non-static) to be accessible in sdcard.cpp

volatile bool sdcardFreezeWrites = false;

// SD retry status (owned/updated by the SD writer task).
volatile size_t sdcardRetryBytes_RS232 = 0;
volatile size_t sdcardRetryBytes_LPT = 0;
volatile uint32_t sdcardRetryFirstSeq_RS232 = 0;
volatile uint32_t sdcardRetryFirstSeq_LPT = 0;

// EMAILDAY cutoff sequence (inclusive). When freeze is enabled, the writer must not write seq > cutoff.
volatile uint32_t sdcardFreezeCutoffSeq_RS232 = 0;
volatile uint32_t sdcardFreezeCutoffSeq_LPT = 0;

// Timestamps for tracking reception pauses (for smart flush logic)
static volatile unsigned long sdcardLastByteTime_RS232 = 0; // Last time a byte was received on RS232
static volatile unsigned long sdcardLastByteTime_LPT = 0;   // Last time a byte was received on LPT
static volatile bool sdcardTimerExpired_RS232 = false;       // True when pauseMs has elapsed since last flush
static volatile bool sdcardTimerExpired_LPT = false;         // True when pauseMs has elapsed since last flush
#endif

// 📊 Write counters for NVS partition (Config storage)
static std::atomic<uint64_t> _nvsWriteCount{0};        // Session write operations count
static std::atomic<uint64_t> _nvsBytesWritten{0};      // Session bytes written
static uint64_t _nvsWriteCountTotal = 0;               // Total persistent write operations
static uint64_t _nvsBytesWrittenTotal = 0;             // Total persistent bytes written

// 📊 Write counters for FATFS/Flash partition (File storage)
static std::atomic<uint64_t> _flashWriteCount{0};      // Session write operations count
static std::atomic<uint64_t> _flashBytesWritten{0};    // Session bytes written
static uint64_t _flashWriteCountTotal = 0;             // Total persistent write operations
static uint64_t _flashBytesWrittenTotal = 0;           // Total persistent bytes written

// 🕒 Last NVS save timestamp for storage stats (save every 12h like microSD)
static unsigned long _storageStatsLastSave = 0;
const unsigned long STORAGE_STATS_SAVE_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL; // 12 hours

// Load storage stats from NVS (called on boot)
void storLoadDaily();
void loadStorageStatsFromNVS() {
    Preferences prefs;
    if (prefs.begin("storage_stats", true)) { // Read-only
        _nvsWriteCountTotal = prefs.getULong64("nvs_wrt_tot", 0);
        _nvsBytesWrittenTotal = prefs.getULong64("nvs_byt_tot", 0);
        _flashWriteCountTotal = prefs.getULong64("fls_wrt_tot", 0);
        _flashBytesWrittenTotal = prefs.getULong64("fls_byt_tot", 0);
        prefs.end();
    }
    storLoadDaily();
}

// Save storage stats to NVS (called periodically and on shutdown)
bool saveStorageStatsToNVS() {
    // 📝 Consolidate session stats into total stats before saving
    _nvsWriteCountTotal += _nvsWriteCount.exchange(0);
    _nvsBytesWrittenTotal += _nvsBytesWritten.exchange(0);
    _flashWriteCountTotal += _flashWriteCount.exchange(0);
    _flashBytesWrittenTotal += _flashBytesWritten.exchange(0);
    storPersistDaily(true);

    Preferences prefs;
    if (prefs.begin("storage_stats", false)) { // Read-write
        size_t w = 0;
        w += prefs.putULong64("nvs_wrt_tot", _nvsWriteCountTotal);
        w += prefs.putULong64("nvs_byt_tot", _nvsBytesWrittenTotal);
        w += prefs.putULong64("fls_wrt_tot", _flashWriteCountTotal);
        w += prefs.putULong64("fls_byt_tot", _flashBytesWrittenTotal);
        prefs.end();
        _storageStatsLastSave = millis();
        // Serial.printf("💾 Storage Stats Saved: NVS=%llu, Flash=%llu (wrote %u bytes)\n", _nvsWriteCountTotal, _flashWriteCountTotal, w);
        return true;
    }
    return false;
}

// Reset storage stats (for testing or maintenance)
bool resetStorageStats() {
    _nvsWriteCount = 0;
    _nvsBytesWritten = 0;
    _nvsWriteCountTotal = 0;
    _nvsBytesWrittenTotal = 0;
    _flashWriteCount = 0;
    _flashBytesWritten = 0;
    _flashWriteCountTotal = 0;
    _flashBytesWrittenTotal = 0;
    
    Preferences prefs;
    if (prefs.begin("storage_stats", false)) {
        prefs.clear(); // Clear all keys in this namespace
        prefs.end();
        _storageStatsLastSave = millis();
        return true;
    }
    return false;
}

// Getter functions for NVS counters (return total = session + persistent)
uint64_t nvsGetWriteCount() { return _nvsWriteCount.load() + _nvsWriteCountTotal; }
uint64_t nvsGetBytesWritten() { return _nvsBytesWritten.load() + _nvsBytesWrittenTotal; }

// ---- Daily storage stats (per-day writes: SD + LittleFS + config saves) ----
// Buckets live in NVS "storage_stats" as day keys (sdYYYYMMDD/sdb…/lfs…/lfsb…/
// cfg…). Only TODAY's keys are ever written; on day rotation the old keys are
// removed (lifetime totals already persist separately, so no history is lost).
// Counters accumulate in PSRAM (never DRAM heap); NVS persists every 30 min +
// on reboot/OTA. Day 0 = clock not synced yet: lifetime counters still
// advance, daily waits for NTP.
struct StorDayBank {
    std::atomic<uint32_t> sdW{0};
    std::atomic<uint64_t> sdB{0};
    std::atomic<uint32_t> lfsW{0};
    std::atomic<uint64_t> lfsB{0};
    std::atomic<uint32_t> cfg{0};
    uint32_t day = 0;        // YYYYMMDD of the open bucket (0 = none)
    uint32_t firstDay = 0;   // first day ever seen (anchors the average)
    unsigned long lastSave = 0;
};
static StorDayBank* storBank() {
    static StorDayBank* p = nullptr;
    if (!p) {
        void* mem = heap_caps_malloc(sizeof(StorDayBank), MALLOC_CAP_SPIRAM);
        if (!mem) mem = heap_caps_malloc(sizeof(StorDayBank), MALLOC_CAP_8BIT);
        if (mem) p = new (mem) StorDayBank();
    }
    return p;
}
#define STOR_DAY_SAVE_MS (12UL * 60UL * 60UL * 1000UL)  // 2x/day; accumulates in PSRAM between writes
#define STOR_CFG_MAX_DAY 2  // config-file writes per day (flash wear cap)

static uint32_t storTodayKey() {
    time_t n = time(nullptr);
    if (n < 1700000000L) return 0;
    struct tm t;
    localtime_r(&n, &t);
    return (uint32_t)(t.tm_year + 1900) * 10000u + (uint32_t)(t.tm_mon + 1) * 100u + (uint32_t)t.tm_mday;
}

static void storDayPersistLocked(Preferences& prefs, StorDayBank* b) {
    if (!b || !b->day) return;
    char k[12];
    snprintf(k, sizeof(k), "sd%u", b->day);
    prefs.putULong(k, b->sdW.load());
    snprintf(k, sizeof(k), "sdb%u", b->day);
    prefs.putULong64(k, b->sdB.load());
    snprintf(k, sizeof(k), "lfs%u", b->day);
    prefs.putULong(k, b->lfsW.load());
    snprintf(k, sizeof(k), "lfsb%u", b->day);
    prefs.putULong64(k, b->lfsB.load());
    snprintf(k, sizeof(k), "cfg%u", b->day);
    prefs.putULong(k, b->cfg.load());
    prefs.putULong("firstday", b->firstDay);
    b->lastSave = millis();
}

static void storDayRotate(uint32_t day, Preferences& prefs, StorDayBank* b) {
    if (b->day && b->day != day) {
        // Drop yesterday's keys (lifetime totals already cover history).
        char k[12];
        snprintf(k, sizeof(k), "sd%u", b->day); prefs.remove(k);
        snprintf(k, sizeof(k), "sdb%u", b->day); prefs.remove(k);
        snprintf(k, sizeof(k), "lfs%u", b->day); prefs.remove(k);
        snprintf(k, sizeof(k), "lfsb%u", b->day); prefs.remove(k);
        snprintf(k, sizeof(k), "cfg%u", b->day); prefs.remove(k);
        b->sdW.store(0); b->sdB.store(0); b->lfsW.store(0); b->lfsB.store(0); b->cfg.store(0);
    }
    if (!b->firstDay) b->firstDay = day;
    b->day = day;
    // Resume a bucket already persisted today (reboot mid-day).
    char k[12];
    snprintf(k, sizeof(k), "sd%u", day); b->sdW.store(prefs.getULong(k, 0));
    snprintf(k, sizeof(k), "sdb%u", day); b->sdB.store(prefs.getULong64(k, 0));
    snprintf(k, sizeof(k), "lfs%u", day); b->lfsW.store(prefs.getULong(k, 0));
    snprintf(k, sizeof(k), "lfsb%u", day); b->lfsB.store(prefs.getULong64(k, 0));
    snprintf(k, sizeof(k), "cfg%u", day); b->cfg.store(prefs.getULong(k, 0));
    uint32_t fd = prefs.getULong("firstday", b->firstDay);
    b->firstDay = fd ? fd : day;
    if (!fd) prefs.putULong("firstday", b->firstDay);
}

// Ensure an open bucket for today (rotates + resumes when needed).
// Returns nullptr when the clock is not synced (daily paused).
static StorDayBank* storBucket() {
    StorDayBank* b = storBank();
    if (!b) return nullptr;
    uint32_t d = storTodayKey();
    if (!d) return nullptr;
    if (d == b->day) return b;
    Preferences prefs;
    if (!prefs.begin("storage_stats", false)) return nullptr;
    storDayRotate(d, prefs, b);
    prefs.end();
    return b;
}

// Persist today's bucket when due (30 min) or forced (reboot/OTA).
void storPersistDaily(bool force) {
    StorDayBank* b = storBank();
    if (!b || !b->day) return;
    if (!force && (millis() - b->lastSave) < STOR_DAY_SAVE_MS) return;
    Preferences prefs;
    if (!prefs.begin("storage_stats", false)) return;
    storDayPersistLocked(prefs, b);
    prefs.end();
}

void storNoteSdWrite(uint64_t bytes) {
    StorDayBank* b = storBucket();
    if (!b) return;
    b->sdW.fetch_add(1);
    b->sdB.fetch_add(bytes);
    storPersistDaily(false);
}

void storNoteLfsWrite(uint64_t bytes) {
    StorDayBank* b = storBucket();
    if (!b) return;
    b->lfsW.fetch_add(1);
    b->lfsB.fetch_add(bytes);
    storPersistDaily(false);
}

// Config-file save gate: max STOR_CFG_MAX_DAY routine saves per day.
// Returns false when the budget is spent (caller keeps dirty for tomorrow).
// Forced saves (reboot/OTA) bypass via storNoteCfgSaveForced().
bool storNoteCfgSave() {
    StorDayBank* b = storBucket();
    if (!b) return true;
    if (b->cfg.load() >= STOR_CFG_MAX_DAY) return false;
    b->cfg.fetch_add(1);
    storPersistDaily(false);
    return true;
}

void storNoteCfgSaveForced() {
    StorDayBank* b = storBucket();
    if (!b) return;
    b->cfg.fetch_add(1);
    storPersistDaily(false);
}

// Load today's bucket at boot (called from loadStorageStatsFromNVS path).
void storLoadDaily() {
    StorDayBank* b = storBank();
    if (!b) return;
    uint32_t d = storTodayKey();
    Preferences prefs;
    if (!prefs.begin("storage_stats", true)) return;
    b->firstDay = prefs.getULong("firstday", 0);
    prefs.end();
    if (!d) return;
    Preferences pw;
    if (!pw.begin("storage_stats", false)) return;
    if (d != b->day) storDayRotate(d, pw, b);
    else {
        char k[12];
        snprintf(k, sizeof(k), "sd%u", d); b->sdW.store(pw.getULong(k, 0));
        snprintf(k, sizeof(k), "sdb%u", d); b->sdB.store(pw.getULong64(k, 0));
        snprintf(k, sizeof(k), "lfs%u", d); b->lfsW.store(pw.getULong(k, 0));
        snprintf(k, sizeof(k), "lfsb%u", d); b->lfsB.store(pw.getULong64(k, 0));
        snprintf(k, sizeof(k), "cfg%u", d); b->cfg.store(pw.getULong(k, 0));
    }
    pw.end();
    b->day = d;
    if (!b->firstDay) {
        b->firstDay = d;
        storPersistDaily(true);
    }
}

uint32_t storDaysTracked() {
    StorDayBank* b = storBank();
    if (!b || !b->firstDay || !b->day || b->day < b->firstDay) return 1;
    return b->day - b->firstDay + 1;
}
uint32_t storSdTodayW() { StorDayBank* b = storBank(); return b ? b->sdW.load() : 0; }
uint64_t storSdTodayB() { StorDayBank* b = storBank(); return b ? b->sdB.load() : 0; }
uint32_t storLfsTodayW() { StorDayBank* b = storBank(); return b ? b->lfsW.load() : 0; }
uint64_t storLfsTodayB() { StorDayBank* b = storBank(); return b ? b->lfsB.load() : 0; }
uint32_t storCfgToday() { StorDayBank* b = storBank(); return b ? b->cfg.load() : 0; }
#if ENABLE_SDCARD==1
unsigned long getSdcardPushMutexFailCountRs232() { return sdcardPushMutexFailCount_RS232; }
unsigned long getSdcardPushPointerNullCountRs232() { return sdcardPushPointerNullCount_RS232; }
#else
unsigned long getSdcardPushMutexFailCountRs232() { return 0; }
unsigned long getSdcardPushPointerNullCountRs232() { return 0; }
#endif
int getInactivityAlertsToday() { return inactivityAlertsToday; }
void nvsIncrementWriteCount(uint64_t bytes) { 
    _nvsWriteCount.fetch_add(1); 
    _nvsBytesWritten.fetch_add(bytes);
}

// Getter functions for Flash/FATFS counters (return total = session + persistent)
uint64_t flashGetWriteCount() { return _flashWriteCount.load() + _flashWriteCountTotal; }
uint64_t flashGetBytesWritten() { return _flashBytesWritten.load() + _flashBytesWrittenTotal; }
void flashIncrementWriteCount(uint64_t bytes) {
    _flashWriteCount.fetch_add(1);
    _flashBytesWritten.fetch_add(bytes);
    storNoteLfsWrite(bytes);
}

static void ensureExpectedNvsNamespaces() {
    static const char* const namespaces[] = {
        "esp32_config",
        "wifi_networks",
        "storage_stats",
        "debug_writer",
        "all_configs",
        "sdcard",
        "nvs_stats",
        "printer_configs"
    };

    Preferences prefs;
    size_t createdCount = 0;

    for (const char* ns : namespaces) {
        if (prefs.begin(ns, false)) {
            prefs.end();
            createdCount++;
        } else {
            Serial.print(F("NVS: failed to open/create namespace: "));
            Serial.println(ns);
        }
    }
}

// Async initialization tasks: static functions without captures/lambdas
#if ENABLE_SDCARD==1
static void taskInitSD(void *param){
    static const bool kBootAsyncVerbose = false;
    if (kBootAsyncVerbose) sendVirtualMessage(F("SD: init async"), INFO, false, false);
    
    // Call sdcardBegin() directly. It returns false quickly if no card is present.
    // CRITICAL: Do not call sdcardDetect() before sdcardBegin().
    // sdcardDetect() previously called SD_MMC.begin()+end(), followed immediately by another
    // SD_MMC.begin() inside sdcardBegin(). This allocated two sets of DMA buffers in PSRAM
    // (CONFIG_SPIRAM_USE_MALLOC=1). The SDMMC DMA engine writing to the freed first buffer
    // corrupted the adjacent heap block header, causing CORRUPT HEAP at 0x3c33ff01.
    bool ok = sdcardBegin();
    if (ok && sdcardIsMounted()) {
        if (kBootAsyncVerbose) sendVirtualMessage(F("SD: mounted"), INFO, false, false);
        // Daily file creation is lazy: the first real data write creates the folder and file.
        // No proactive creation needed — days without data produce no files.
    } else {
        // No card or mount failed - enable hot-plug auto-mount
        sendVirtualMessage(F("SD: auto-mount enabled"), WARN, false, false);
        sdcardStartAutoMount();
    }
    
    // CRITICAL: Delay before starting LittleFS to let SD_MMC DMA buffers and FatFS internal
    // structs be fully freed. Starting LittleFS concurrently causes both to allocate in PSRAM
    // via malloc() at the same time. FatFS memset(FATFS*,0) then zeroes the adjacent heap
    // block header, causing CORRUPT HEAP at 0x3c33ff01.
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Start LittleFS sequentially after SD allocations are fully freed
#if ENABLE_FILESYSTEM==1
    // Stack size increased to 12KB: loadFromLittleFS() uses 32KB SpiRamJsonDocument
    // (PSRAM-allocated but parsing is stack-heavy). 4KB caused stack overflow → CORRUPT HEAP.
    xTaskCreate(taskInitFlash, "flashInit", 12288, nullptr, 1, nullptr);
#endif
    
    // Cleanup task memory
    vTaskDelete(nullptr);
}
#endif

#if ENABLE_FILESYSTEM==1
#include "src/littlefs_manager.h"

#include "psram_helpers.h"
static void taskInitFlash(void *param){
    static const bool kBootAsyncVerbose = false;
    if (kBootAsyncVerbose) sendVirtualMessage(F("Flash: init async"), INFO, false, false);
    bool ok = littlefsBegin();
    if (!(ok && littlefsMounted())) {
        sendVirtualMessage(F("Flash: mount failed (no forced format in boot path)"), WARN, false, false);
    }

    if(ok && littlefsMounted()) {
        if (kBootAsyncVerbose) sendVirtualMessage(F("Flash: mounted"), INFO, false, false);

        if (configManager.lfsWritePending) {
            // The web handler saved config before LittleFS was ready (saveToLittleFS failed).
            // Save the current in-memory state (which has the user's values) to LittleFS now
            // instead of loading the old file, so the user's changes are not overwritten.
            configManager.lfsWritePending = false;
            configManager.save(true);
            if (kBootAsyncVerbose) sendVirtualMessage(F("Flash: pending save flushed"), INFO, false, false);
        } else {
            // Normal boot: load saved config from LittleFS (system_name, charts, NTP, etc.)
            // loadConfig() in setup() tried to read LittleFS before mount, so it used defaults.
            // This call picks up the actual saved config from LittleFS (including system_name).
            configManager.loadFromLittleFS();
        }

        // Sync all global variables with the freshly updated ConfigManager values.
        // This ensures ntpServer, timezone, charsetMode, printerEmulationMode etc. are
        // up to date for /api/configs and for runtime use.
        extern void syncGlobalVariables(bool includeRS232);
        syncGlobalVariables(false);

        // Apply language preference now that LittleFS config is loaded.
        // At boot, setupLanguage() ran before LittleFS was ready and used the default ("pt").
        // This call re-applies the correct saved language (e.g. "en") from LittleFS.
        extern void setupLanguage();
        setupLanguage();

        // Reload proximity config from LittleFS now that it is mounted.
        // proximityBegin() runs in setup() before LittleFS is ready so loadProximityConfig()
        // returns immediately there; this is the deferred load that actually applies saved values.
#if ENABLE_PROXIMITY==1
        extern void loadProximityConfig();
        extern void loadProximityShiftConfig();
        loadProximityConfig();
        loadProximityShiftConfig();
    if (kBootAsyncVerbose) sendVirtualMessage(F("Proximity: config loaded"), INFO, false, false);
#endif

        // Reload buzzer config from LittleFS now that it is mounted.
        // buzzerBegin() runs in setup() before LittleFS is ready; this applies saved values.
#if ENABLE_BUZZER==1
        extern void buzzerReloadConfig();
        buzzerReloadConfig();
    if (kBootAsyncVerbose) sendVirtualMessage(F("Buzzer: config loaded"), INFO, false, false);
#endif

    // Reload Unikon config from LittleFS now that it is mounted.
    // unikonBegin() runs in setup(); this deferred reload guarantees persisted
    // values are applied even if early boot mount/load was not available.
#if ENABLE_UNIKON==1
    extern bool unikonFlushPendingConfig();
    extern void unikonReloadConfig();
    unikonFlushPendingConfig();
    unikonReloadConfig();
    if (kBootAsyncVerbose) sendVirtualMessage(F("Unikon: config loaded"), INFO, false, false);
#endif

        // Monitor stack usage (12KB allocated - 4KB minimum safety margin)
        UBaseType_t stackFree = uxTaskGetStackHighWaterMark(NULL);
        if (stackFree < 1024) { // <4KB margin = danger
            sendVirtualMessage(buildMessage("FLASH_INIT: low stack (%u words)", (unsigned)stackFree), WARN);
        }
    }
    else sendVirtualMessage(F("Flash: not mounted"), WARN, false, false);
    
    // Cleanup task memory
    vTaskDelete(nullptr);
}
#endif

#if ENABLE_SDCARD==1
// IMPROVEMENT 3: Dedicated task for async SD writing (double buffering)
// Runs on Core 0 with priority 3-5 (dynamic boost when urgent)
static void sdCardWriteTask(void *param) {
    // IMPROVEMENT 3: Priority levels for dynamic boost
    const UBaseType_t normalPriority = 3;  // Normal operation
    const UBaseType_t urgentPriority = 5;  // Above email/SMTP when buffer critical
    
    // Retry queues live inside the writer task to keep ordering and avoid extra locking.
    std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> retry_RS232;
    std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> retry_LPT;
    size_t retryBytes_RS232 = 0;
    size_t retryBytes_LPT = 0;
    
    // CRITICAL: Track last processed day to detect day changes (midnight rotation)
    // When day changes, flush ALL buffers to previous day's file BEFORE creating new day's file
    static int lastProcessedDay = -1;  // -1 = not initialized
    static int lastProcessedMonth = -1;
    static int lastProcessedYear = -1;
    static bool dayChangeFlushInProgress = false; // Prevent recursive flushes

    auto retryTotalBytes = [&]() -> size_t { return retryBytes_RS232 + retryBytes_LPT; };

    auto publishRetryStatus = [&]() {
        sdcardRetryBytes_RS232 = retryBytes_RS232;
        sdcardRetryBytes_LPT = retryBytes_LPT;
        sdcardRetryFirstSeq_RS232 = retry_RS232.empty() ? 0 : retry_RS232.front().seq;
        sdcardRetryFirstSeq_LPT = retry_LPT.empty() ? 0 : retry_LPT.front().seq;
    };

    auto appendToRetry = [&](const String &source,
                             std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> &retryVec,
                             size_t &retryBytes,
                             std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> &chunks,
                             size_t startIdx) {
        static unsigned long lastWarnMs = 0;
        for (size_t i = startIdx; i < chunks.size(); ++i) {
            const size_t len = chunks[i].dataLength();
            if (retryTotalBytes() + len > SDCARD_RETRY_BUFFER_MAX_BYTES_TOTAL) {
                if (millis() - lastWarnMs > 5000UL) {
                    sendVirtualMessage(String(F("SD retry buffer full; dropping new data for ")) + source, WARN);
                    lastWarnMs = millis();
                }
                continue;
            }
            retryVec.push_back(std::move(chunks[i]));
            retryBytes += len;
        }
    };

    auto dropWrittenPrefix = [&](std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> &vec,
                                 size_t &bytes,
                                 size_t count) {
        if (count == 0) return;
        if (count >= vec.size()) {
            vec.clear();
            vec.shrink_to_fit(); // CRITICAL: Release PSRAM capacity to prevent ESP_ERR_NO_MEM
            bytes = 0;
            return;
        }
        size_t removed = 0;
        for (size_t i = 0; i < count; ++i) removed += vec[i].dataLength();
        vec.erase(vec.begin(), vec.begin() + (ptrdiff_t)count);
        vec.shrink_to_fit(); // CRITICAL: Release PSRAM capacity after partial erase to prevent ESP_ERR_NO_MEM
        bytes = (bytes > removed) ? (bytes - removed) : 0;
    };

    while (true) {
        // Wait for new flush notifications, but periodically wake up when we have retry data.
        // OPTIMIZED: 50ms wake interval (was 100ms) for faster retry recovery
        // Faster wakeup = retry queue drains quicker = less chance of RS232/LPT buffer overflow
        const TickType_t waitTicks = (retryTotalBytes() > 0) ? pdMS_TO_TICKS(50) : portMAX_DELAY;
        ulTaskNotifyTake(pdTRUE, waitTicks);
        
        // PROTECTION: Check if pointers initialized before use
        if (!sdcardFlushBuffer_RS232 || !sdcardFlushBuffer_LPT) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Wait for initialization
            continue;
        }
        
        // ========================================================================
        // 🔐 CRITICAL: MIDNIGHT DAY CHANGE DETECTION AND FLUSH
        // ========================================================================
        // Before processing any buffers, check if day has changed (00:00:00 crossed)
        // If day changed, flush ALL buffers to PREVIOUS day's file BEFORE creating new file
        // This ensures files contain ONLY data from their respective days
        // ========================================================================
        time_t now = time(nullptr);
        if (now > 1000) { // Only proceed if RTC is valid (NTP synced)
            struct tm currentTime;
            localtime_r(&now, &currentTime);
            int currentDay = currentTime.tm_mday;
            int currentMonth = currentTime.tm_mon + 1;
            int currentYear = currentTime.tm_year + 1900;
            
            // Initialize on first run
            if (lastProcessedDay == -1) {
                lastProcessedDay = currentDay;
                lastProcessedMonth = currentMonth;
                lastProcessedYear = currentYear;
                sendVirtualMessage(buildMessage("📅 SD Writer: Initialized day tracking: %02d-%02d-%04d", 
                                                  currentDay, currentMonth, currentYear), DEBUG);
            }
            
            // Detect day change (compare date components to handle month/year boundaries)
            bool dayChanged = (currentDay != lastProcessedDay || 
                             currentMonth != lastProcessedMonth || 
                             currentYear != lastProcessedYear);
            
            if (dayChanged && !dayChangeFlushInProgress) {
                dayChangeFlushInProgress = true; // Prevent recursive flushes
                
                sendVirtualMessage(buildMessage("🌅 DAY CHANGE DETECTED: %02d-%02d-%04d → %02d-%02d-%04d", 
                                                  lastProcessedDay, lastProcessedMonth, lastProcessedYear,
                                                  currentDay, currentMonth, currentYear), INFO);
                sendVirtualMessage(F("🔒 FLUSHING ALL BUFFERS to previous day's files..."), INFO);
                
                // ✅ PRIORITY MANAGER: Mark this as SD write operation (P2)
                // This operation has high priority but must yield to RS232/LPT (P1)
                priorityManager().startSDWrite();
                
                // Step 1: Force swap ALL activeBuffers → flushBuffers
                // This moves all accumulated data to flush buffers for writing
                auto* activeRS232 = sdcardActiveBuffer_RS232.load(std::memory_order_acquire);
                auto* activeLPT = sdcardActiveBuffer_LPT.load(std::memory_order_acquire);
                auto* flushRS232 = sdcardFlushBuffer_RS232.load(std::memory_order_acquire);
                auto* flushLPT = sdcardFlushBuffer_LPT.load(std::memory_order_acquire);
                
                if (sdcardSwapLock(pdMS_TO_TICKS(5000))) { // 5s timeout for day change
                    // Move RS232 active → flush
                    if (activeRS232 && flushRS232 && !activeRS232->empty()) {
                        sendVirtualMessage(buildMessage("  📦 Moving %u RS232 chunks to flush buffer", 
                                                          (unsigned)activeRS232->size()), DEBUG);
                        // Append to flush buffer (preserves ordering)
                        for (auto& chunk : *activeRS232) {
                            flushRS232->push_back(std::move(chunk));
                            // Yield during large buffer moves to prevent RS232/LPT starvation
                            if (activeRS232->size() > 100) {
                                priorityManager().yieldDuringSDWrite();
                            }
                        }
                        activeRS232->clear();
                        sdcardActiveBytes_RS232.store(0, std::memory_order_release);
                    }
                    
                    // Move LPT active → flush
                    if (activeLPT && flushLPT && !activeLPT->empty()) {
                        sendVirtualMessage(buildMessage("  📦 Moving %u LPT chunks to flush buffer", 
                                                          (unsigned)activeLPT->size()), DEBUG);
                        // Append to flush buffer (preserves ordering)
                        for (auto& chunk : *activeLPT) {
                            flushLPT->push_back(std::move(chunk));
                            // Yield during large buffer moves to prevent RS232/LPT starvation
                            if (activeLPT->size() > 100) {
                                priorityManager().yieldDuringSDWrite();
                            }
                        }
                        activeLPT->clear();
                        sdcardActiveBytes_LPT.store(0, std::memory_order_release);
                    }
                    
                    sdcardSwapUnlock();
                    sendVirtualMessage(F("  ✅ Active buffers swapped to flush buffers"), DEBUG);
                } else {
                    sendVirtualMessage(F("  ⚠️ Day change: Failed to acquire swap lock for buffer move"), WARN);
                }
                
                // Yield after swap operation
                priorityManager().yieldDuringSDWrite();
                
                // Step 2: Process ALL flushBuffers + retry queues to SD card
                // Write RS232 retries
                while (!retry_RS232.empty()) {
                    size_t writtenPrefix = logDataToSdcardTimestamped("RS232", retry_RS232);
                    dropWrittenPrefix(retry_RS232, retryBytes_RS232, writtenPrefix);
                    publishRetryStatus();
                    priorityManager().yieldDuringSDWrite(); // Yield after each retry batch
                    vTaskDelay(pdMS_TO_TICKS(2));
                    if (writtenPrefix == 0) break; // Avoid infinite loop if write fails
                }
                
                // Yield between sources
                priorityManager().yieldDuringSDWrite();
                
                // Write RS232 flush buffer
                if (sdcardSwapLock(pdMS_TO_TICKS(5000))) {
                    if (flushRS232 && !flushRS232->empty()) {
                        std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> bufferToWrite = std::move(*flushRS232);
                        flushRS232->shrink_to_fit();
                        sdcardSwapUnlock();
                        
                        size_t writtenPrefix = logDataToSdcardTimestamped("RS232", bufferToWrite);
                        if (writtenPrefix < bufferToWrite.size()) {
                            appendToRetry(String("RS232"), retry_RS232, retryBytes_RS232, bufferToWrite, writtenPrefix);
                        }
                        publishRetryStatus();
                        priorityManager().yieldDuringSDWrite(); // Yield after flush write
                    } else {
                        sdcardSwapUnlock();
                    }
                }
                
                // Yield between sources
                priorityManager().yieldDuringSDWrite();
                
                // Write LPT retries
                while (!retry_LPT.empty()) {
                    size_t writtenPrefix = logDataToSdcardTimestamped("LPT", retry_LPT);
                    dropWrittenPrefix(retry_LPT, retryBytes_LPT, writtenPrefix);
                    publishRetryStatus();
                    priorityManager().yieldDuringSDWrite(); // Yield after each retry batch
                    vTaskDelay(pdMS_TO_TICKS(2));
                    if (writtenPrefix == 0) break; // Avoid infinite loop if write fails
                }
                
                // Yield between sources
                priorityManager().yieldDuringSDWrite();
                
                // Write LPT flush buffer
                if (sdcardSwapLock(pdMS_TO_TICKS(5000))) {
                    if (flushLPT && !flushLPT->empty()) {
                        std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> bufferToWrite = std::move(*flushLPT);
                        flushLPT->shrink_to_fit();
                        sdcardSwapUnlock();
                        
                        size_t writtenPrefix = logDataToSdcardTimestamped("LPT", bufferToWrite);
                        if (writtenPrefix < bufferToWrite.size()) {
                            appendToRetry(String("LPT"), retry_LPT, retryBytes_LPT, bufferToWrite, writtenPrefix);
                        }
                        publishRetryStatus();
                        priorityManager().yieldDuringSDWrite(); // Yield after flush write
                    } else {
                        sdcardSwapUnlock();
                    }
                }
                
                sendVirtualMessage(F("  ✅ All buffers flushed to previous day's files"), INFO);
                
                // ✅ PRIORITY MANAGER: End SD write operation
                priorityManager().endSDWrite();
                
                // Step 3: Update tracking to new day (allows new files to be created)
                lastProcessedDay = currentDay;
                lastProcessedMonth = currentMonth;
                lastProcessedYear = currentYear;
                
                sendVirtualMessage(buildMessage("🌄 DAY CHANGE COMPLETE: Now writing to %02d-%02d-%04d files", 
                                                  currentDay, currentMonth, currentYear), INFO);
                
                dayChangeFlushInProgress = false; // Re-enable day change detection
                vTaskDelay(pdMS_TO_TICKS(100)); // Brief stabilization
            }
        }
        
        // Retry RS232 first to preserve per-source ordering.
        if (!retry_RS232.empty()) {
            // Debug: Check if mutex is stuck before attempting write
            if (sdMutexHolder != nullptr) {
                unsigned long heldFor = millis() - sdMutexAcquiredAt;
                if (heldFor > 5000) { // If held for >5s, log it
                    sendVirtualMessage(buildMessage("SD Write Task: Mutex already held by '%s' for %lu ms before retry", 
                                                      sdMutexHolder, heldFor), WARN);
                }
            }
            size_t writtenPrefix = logDataToSdcardTimestamped("RS232", retry_RS232);
            dropWrittenPrefix(retry_RS232, retryBytes_RS232, writtenPrefix);
            publishRetryStatus();
            vTaskDelay(pdMS_TO_TICKS(2)); // Increased from 1ms to 2ms for RS232/LPT yield
        }

        // Process flush RS232 (if flush buffer has data)
        // IMPROVEMENT 1: Load atomic pointers (lock-free)
        auto* flushRS232 = sdcardFlushBuffer_RS232.load(std::memory_order_acquire);
        auto* flushLPT = sdcardFlushBuffer_LPT.load(std::memory_order_acquire);
        
        if (sdcardSwapLock(pdMS_TO_TICKS(10))) {
            if (flushRS232 && !flushRS232->empty()) {
                // IMPROVEMENT 2: Move semantics (zero-copy, transfers ownership)
                std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> bufferToWrite = std::move(*flushRS232);
                // Flush buffer now empty but valid (no reallocation)
                if (flushRS232->capacity() > 262144) { // 256KB
                    flushRS232->shrink_to_fit(); // Shrink only if very large
                }
                sdcardSwapUnlock();

                // If there are pending retries, keep strict ordering: append new chunks to the retry queue.
                if (!retry_RS232.empty()) {
                    appendToRetry(String("RS232"), retry_RS232, retryBytes_RS232, bufferToWrite, 0);
                } else {
                    size_t writtenPrefix = logDataToSdcardTimestamped("RS232", bufferToWrite);
                    if (writtenPrefix < bufferToWrite.size()) {
                        appendToRetry(String("RS232"), retry_RS232, retryBytes_RS232, bufferToWrite, writtenPrefix);
                    }
                }
                publishRetryStatus();
                vTaskDelay(pdMS_TO_TICKS(2)); // Increased from 1ms to 2ms for RS232/LPT yield
            } else {
                sdcardSwapUnlock();
            }
        }
        
        // Retry LPT first to preserve per-source ordering.
        if (!retry_LPT.empty()) {
            size_t writtenPrefix = logDataToSdcardTimestamped("LPT", retry_LPT);
            dropWrittenPrefix(retry_LPT, retryBytes_LPT, writtenPrefix);
            publishRetryStatus();
            vTaskDelay(pdMS_TO_TICKS(2)); // Increased from 1ms to 2ms for RS232/LPT yield
        }

        // Process flush LPT (if flush buffer has data)
        if (sdcardSwapLock(pdMS_TO_TICKS(10))) {
            if (flushLPT && !flushLPT->empty()) {
                // IMPROVEMENT 2: Move semantics (zero-copy, transfers ownership)
                std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>> bufferToWrite = std::move(*flushLPT);
                // Flush buffer now empty but valid (no reallocation)
                if (flushLPT->capacity() > 262144) { // 256KB
                    flushLPT->shrink_to_fit(); // Shrink only if very large
                }
                sdcardSwapUnlock();
                
                // If there are pending retries, keep strict ordering: append new chunks to the retry queue.
                if (!retry_LPT.empty()) {
                    appendToRetry(String("LPT"), retry_LPT, retryBytes_LPT, bufferToWrite, 0);
                } else {
                    size_t writtenPrefix = logDataToSdcardTimestamped("LPT", bufferToWrite);
                    if (writtenPrefix < bufferToWrite.size()) {
                        appendToRetry(String("LPT"), retry_LPT, retryBytes_LPT, bufferToWrite, writtenPrefix);
                    }
                }
                publishRetryStatus();
                vTaskDelay(pdMS_TO_TICKS(2)); // Increased from 1ms to 2ms for RS232/LPT yield
            } else {
                sdcardSwapUnlock();
            }
        }
        
        // IMPROVEMENT 3: Dynamic priority boost when SD buffer is urgent
        size_t totalActive = sdcardActiveBytes_RS232.load(std::memory_order_relaxed) + 
                           sdcardActiveBytes_LPT.load(std::memory_order_relaxed);
        // Boost priority when Active > 80% OR Retry > 50KB (was 10KB)
        bool isUrgent = (totalActive > (SDCARD_BUFFER_MAX_SIZE * 8 / 10)) 
                     || (retryTotalBytes() > 51200);
        
        if (isUrgent) {
            vTaskPrioritySet(NULL, urgentPriority);  // BOOST to priority 5
        } else {
            vTaskPrioritySet(NULL, normalPriority);  // Back to priority 3
        }
        
        vTaskDelay(pdMS_TO_TICKS(2)); // Increased from 1ms to 2ms for RS232/LPT yield

        // Periodically log SDWrite stack high-water mark (every ~5 min).
        // Helps detect stack pressure before it becomes a crash.
        static uint32_t hwmLoopCount = 0;
        if (++hwmLoopCount >= 150000) { // ~5min at 2ms/iter
            hwmLoopCount = 0;
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
            // Warn if less than 1024 words (~4KB) remain
            if (hwm < 1024) {
                sendVirtualMessage(buildMessage("⚠️ SDWrite stack low: %u words free", (unsigned)hwm), WARN);
            } else {
                sendVirtualMessage(buildMessage("🔍 SDWrite stack HWM: %u words free", (unsigned)hwm), DEBUG);
            }
        }
    }
}
#endif

#if ENABLE_ETHERNET==1
static void taskInitEthernet(void *param){
    // Mark reinit as running so ethernetLoop() skips SPI link checks until we finish.
    // Without this, the DHCP SPI traffic from this task races with Core 1's linkStatus()
    // reads and produces corrupted results (false LinkOFF) that trigger a second reinit.
    _ethReinitTaskRunning = true;

    // Delay strategy:
    //   WiFi enabled at runtime  : 8000 ms — wait for WiFi/AP init to settle before ETH SPI.
    //   WiFi disabled at runtime : 30000 ms — wait until setup() is fully done (~12 s) plus a
    //     safety margin. Starting at 1.5 s was inside the heavy boot window (heap diagnostics,
    //     NTP sync blocking the main thread, SD/LittleFS tasks on Core 0). Moving to 30 s
    //     ensures the SPI bus and all shared resources are idle before the W5500 is touched.
    uint32_t startupDelayMs = 30000;
#if ENABLE_WIFI==1
    if (!configManager.getWifiDisabled()) {
        startupDelayMs = 8000;
    }
#endif
    if (startupDelayMs > 0) {
        vTaskDelay(pdMS_TO_TICKS(startupDelayMs));
    }
    String _delayMsg = String(F("[ETH_BOOT] init task after delay ")) + String(startupDelayMs) + String(F(" ms"));
    directLogPrintln(_delayMsg);
    ethernetBootLogLine(_delayMsg.c_str());
    sendVirtualMessage(F("ETH: init async"), INFO, false, false);
    ethernetBegin();
    heapTraceMark("ethernet");   // W5500 driver + async TCP plumbing
    int st = ethernetStatus();
    if(st==0){ sendVirtualMessage(F("ETH: pronta"), INFO, false, false); }
    else { sendVirtualMessage(F("ETH: erro"), WARN, false, false); }
    const char* _resultMsg = st == 0 ? "[ETH_BOOT] init task result OK" : "[ETH_BOOT] init task result ERROR";
    directLogPrintln(_resultMsg);
    ethernetBootLogLine(_resultMsg);
    // Flush all buffered [ETH_BOOT] messages to the SD daily log file.
    // Only runs if boot log is enabled (BOOT-LOG ON command + persisted in ConfigManager).
    // ethernetFlushBootLogToSD() waits up to 15s for SD to be mounted before writing.
    if (configManager.getBootLogEnabled()) {
        ethernetFlushBootLogToSD();
    }

    // Clear the reinit guard now that init (including DHCP and SD flush) is complete.
    // ethernetLoop() can resume SPI link checks from this point on.
    _ethReinitTaskRunning = false;

    // Cleanup task memory
    vTaskDelete(nullptr);
}
#endif // ENABLE_ETHERNET

// SD==0 stubs (PSRAM VFS fallback)
#if ENABLE_SDCARD==0
#include "flash_fallback_store.h" // no-SD LittleFS mirror (stub when disabled)
static void addDataToSdcardBufferTimestampedRaw(bool isRS232, const char* data, size_t dataLen, unsigned long) {
    if (psramVFSIsEnabled() && data && dataLen > 0) {
        psramVFSWrite(String(isRS232 ? "RS232" : "LPT"), data, dataLen);
        // Best-effort LittleFS unload (small text only; .bin stays in PSRAM).
        flashFbWrite(isRS232 ? "RS232" : "LPT", data, dataLen);
    }
}
static char* sdcardLineAccumulator_RS232 = nullptr;
static size_t sdcardLineAccumulator_RS232_len = 0;
static char* sdcardLineAccumulator_LPT = nullptr;
static size_t sdcardLineAccumulator_LPT_len = 0;
static unsigned long sdcardLineStartTimestamp_RS232 = 0;
static unsigned long sdcardLineStartTimestamp_LPT = 0;
static unsigned long lastSdcardDataReceived_RS232 = 0;
std::vector<StorageFileInfo> storageListFiles(const String &folderPath) {
    std::vector<StorageFileInfo> files;
    if (psramVFSIsEnabled()) {
        std::vector<String> names = psramVFSListFiles(folderPath);
        for (const String &fname : names) {
            StorageFileInfo info;
            info.name = fname;
            info.size = psramVFSGetFileSize(folderPath, fname);
            info.backend = STORAGE_BACKEND_PSRAM;
            files.push_back(info);
        }
    }
    // Flash fallback mirror: merge persisted files (bulk skipped when VFS hit).
    {
        std::vector<StorageFileInfo> flashFiles;
        flashFbCollectEmailFiles(folderPath, "", flashFiles, files.empty());
        for (auto &fi : flashFiles) {
            bool dup = false;
            for (auto &e : files) {
                if (e.name == fi.name) { dup = true; break; }
            }
            if (!dup) files.push_back(fi);
        }
    }
    return files;
}
#endif // ENABLE_SDCARD==0