// sd_status_impl.h -- SD card init, detection, mounting, loop, unmount, and state getters.
// Included only from sdcard.cpp (single TU, all static variables shared).
// Do not include this file from any other translation unit.

// =============================================================
// 🔌 SD CARD AUTO-DETECTION & HOT-PLUG SUPPORT
// =============================================================
// Functions for intelligent boot behavior and hot-plug detection.
// sdcardDetect(): Quick card presence check without mounting.
// sdcardStartAutoMount(): Enable periodic hot-plug scanning.
// sdcardStopAutoMount(): Disable hot-plug scanning.
// sdcardIsAutoMountEnabled(): Query auto-mount state.

/**
 * @brief Quick SD card presence check without mounting
 * @return true if card physically present, false otherwise
 * 
 * This function performs a fast detection without full mount:
 * - Checks physical detect pin if available (fastest)
 * - Or attempts quick SD_MMC initialization with timeout
 * 
 * Use before sdcardBegin() to avoid slow mount attempts when no card present.
 * NOTE: ESP-IDF errors during detection are normal and expected when no card present.
 */
bool sdcardDetect() {
    // Method 1: Physical detect pin (fastest, <1ms)
    #if defined(SDCARD_DETECT_PIN) && (SDCARD_DETECT_PIN >= 0)
        pinMode(SDCARD_DETECT_PIN, INPUT_PULLUP);
        // Most adapters: LOW = card inserted, HIGH = no card (pull-up)
        if (digitalRead(SDCARD_DETECT_PIN) == HIGH) {
            return false; // No card detected
        }
        return true; // Card detected
    #endif
    
    // Method 2: Quick SD_MMC probe (slower, ~100-200ms when no card)
    // If already mounted, card must be present
    if (_sdMounted) return true;
    
    // CRITICAL: Never probe with SD_MMC.begin()+end() here.
    // Doing begin()+end() followed by another begin() in sdcardBegin() allocates two sets of
    // DMA buffers in PSRAM (CONFIG_SPIRAM_USE_MALLOC=1). The second allocation lands adjacent
    // to the first at a fixed PSRAM address. When the SDMMC DMA engine writes to the freed
    // buffer, it corrupts the adjacent heap block header → CORRUPT HEAP at 0x3c33ff01.
    // Simply return _sdMounted state; sdcardBegin() will attempt mount and handle failures.
    return _sdMounted;
}

/**
 * @brief Start hot-plug auto-mount detection
 * 
 * Enables periodic checking integrated in sdcardLoop().
 * No memory allocation - uses existing loop cycle.
 * Safe to call multiple times.
 */
void sdcardStartAutoMount() {
    if (_autoMountEnabled) {
        Serial.println(F("SD: Auto-mount already enabled"));
        return;
    }
    
    _autoMountEnabled = true;
    Serial.println(F("SD: Auto-mount enabled (check every 30s, integrated in sdcardLoop)"));
    sendVirtualMessage(F("📡 SD auto-mount enabled (30s interval)"), INFO);
}

/**
 * @brief Stop hot-plug auto-mount detection
 * 
 * Disables periodic checking in sdcardLoop().
 * Safe to call even if auto-mount not enabled.
 */
void sdcardStopAutoMount() {
    if (!_autoMountEnabled) {
        Serial.println(F("SD: Auto-mount already disabled"));
        return;
    }
    
    _autoMountEnabled = false;
    Serial.println(F("SD: Auto-mount disabled"));
    sendVirtualMessage(F("📡 SD auto-mount disabled"), INFO);
}

/**
 * @brief Query auto-mount enabled state
 * @return true if auto-mount is enabled, false otherwise
 */
bool sdcardIsAutoMountEnabled() {
    return _autoMountEnabled;
}

// =============================================================
// 🚀 SD CARD INITIALIZATION & MOUNTING
// =============================================================

bool sdcardBegin() {
    static const bool kSdBootVerbose = false;
    // Initialize recursive mutex with priority inheritance (prevents xTaskPriorityDisinherit assert)
    // Recursive mutexes prevent deadlocks when same task reacquires
    if (sdMutex == NULL) {
        sdMutex = xSemaphoreCreateRecursiveMutex();
        // Mutexes are created in "unlocked" state, no need to give
    }
    
    // 🔒 CRITICAL: ALWAYS reset tracking variables to ensure consistent state
    // This prevents corruption from uninitialized global variables or startup race conditions
    sdMutexTracking.canary_before = 0xDEADBEEF;
    sdMutexTracking.ownerTask = NULL;
    sdMutexTracking.depth = 0;
    sdMutexTracking.canary_after = 0xCAFEBABE;
    sdMutexOwnerTask = NULL;
    sdMutexDepth = 0;

    if (_sdMounted) return true; // ♻️ Already mounted
    
    // 🔎 If a physical detect pin is defined, check before starting
    #if defined(SDCARD_DETECT_PIN) && (SDCARD_DETECT_PIN >= 0)
        pinMode(SDCARD_DETECT_PIN, INPUT_PULLUP);
        if (digitalRead(SDCARD_DETECT_PIN) == HIGH) {
            // Pull-up and HIGH = no card (many adapters use LOW when inserted)
            Serial.println(F("SD: physical detector reports: no card (skip init)."));
            _sdStatus = -2; _sdMounted = false; _sdTotal = 0; _sdUsed = 0;
            return false;
        }
    #endif

    // 🚀 Start SDMMC in 1-bit or 4-bit mode (configurable via SDCARD_USE_1BIT_MODE)
    // 🛠️ Pinout: CLK=39, CMD=38, D0=40 (+D1-D3=41-43 for 4-bit)
    Serial.print(F("SD: Inicializando SDMMC ("));
    Serial.print(SDCARD_USE_1BIT_MODE ? F("1-bit") : F("4-bit"));
    Serial.println(F(" mode)..."));
    if (kSdBootVerbose) {
        Serial.print(F("SD: Pinos - CLK=")); Serial.print(SDCARD_CLK_PIN);
        Serial.print(F(" CMD=")); Serial.print(SDCARD_CMD_PIN);
        Serial.print(F(" D0=")); Serial.print(SDCARD_D0_PIN);
        if (!SDCARD_USE_1BIT_MODE) {
            Serial.print(F(" D1=")); Serial.print(SDCARD_D1_PIN);
            Serial.print(F(" D2=")); Serial.print(SDCARD_D2_PIN);
            Serial.print(F(" D3=")); Serial.println(SDCARD_D3_PIN);
        } else {
            Serial.println();
        }
    }

    bool success = false;
    const char* mountPoint = "/sdcard";

#if SDCARD_USE_SPI_MODE == 1
    // SPI-based SD init. Default example: Waveshare 8DI8DO onboard slot
    // (CS=21, MISO=45, MOSI=47, SCLK=48). Touch7 override lives in board_pins.h
    // (slot SPI 11/12/13, CS = CH422G EXIO4 external, SDCARD_SPI_CS_PIN=-1).
    // Uses SPI3 (HSPI) — Ethernet W5500 already occupies SPI2 (FSPI).
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // The Board owns CH422G on I2C0: reuse its handle, never create a second
    // expander (a duplicate instance races LCD/touch init). Mount is only
    // attempted after the panel is up (see boot order in rs232_config_impl.h);
    // before that this returns false and auto-mount retries later.
    extern bool touch7SdAssertCs();
    if (!touch7SdAssertCs()) {
        Serial.println(F("SD: Touch7 expander not ready, deferring TF mount."));
        _sdStatus = -1; _sdMounted = false; _sdTotal = 0; _sdUsed = 0;
        return false;
    }
#endif
    static SPIClass sdSPI(HSPI);
    sdSPI.begin(SDCARD_SPI_SCLK_PIN, SDCARD_SPI_MISO_PIN, SDCARD_SPI_MOSI_PIN, SDCARD_SPI_CS_PIN);
    Serial.print(F("SD: Init via SPI (CS="));
    Serial.print(SDCARD_SPI_CS_PIN);
    Serial.println(F(")..."));
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // Dummy CS (-1 => 255): the card stays selected via EXIO4 LOW asserted
    // above. The invalid-pin log spam from the Arduino SD backend is expected
    // (same pattern as the Waveshare TF demo); keep the mount attempt.
#ifndef TOUCH7_SD_SPI_FREQ
#define TOUCH7_SD_SPI_FREQ 8000000
#endif
    success = SD.begin(SDCARD_SPI_CS_PIN, sdSPI, TOUCH7_SD_SPI_FREQ, mountPoint);
#else
    success = SD.begin(SDCARD_SPI_CS_PIN, sdSPI, 8000000, mountPoint);
#endif
    if (!success) {
        Serial.println(F(" ❌ SD SPI init failed."));
        _sdStatus = -1; _sdMounted = false; _sdTotal = 0; _sdUsed = 0;
        return false;
    }
#else
    // SDMMC native interface (flexible GPIO matrix — any GPIO valid on ESP32-S3).
    // SD_MMC.setPins() must be called BEFORE begin().

    // SDCARD_DAT3_HIGHPIN (e.g. GPIO21 on Waveshare 8DI8DO): drive HIGH so the SD
    // card's DAT3/CS pin is not pulled low at power-on. If DAT3 is low during
    // card power-up, the card enters SPI mode and will not respond to SDMMC commands.
    #if defined(SDCARD_DAT3_HIGHPIN) && (SDCARD_DAT3_HIGHPIN >= 0)
        pinMode(SDCARD_DAT3_HIGHPIN, OUTPUT);
        digitalWrite(SDCARD_DAT3_HIGHPIN, HIGH);
        if (kSdBootVerbose) {
            Serial.print(F("SD: DAT3/CS pin GPIO"));
            Serial.print(SDCARD_DAT3_HIGHPIN);
            Serial.println(F(" set HIGH (SD mode, not SPI mode)"));
        }
        delay(5); // Brief delay after driving HIGH before SDMMC init
    #endif

    SD_MMC.setPins(SDCARD_CLK_PIN, SDCARD_CMD_PIN, SDCARD_D0_PIN);
    // In 4-bit mode, also pass D1-D3. SD_MMC.setPins() has an overload for this.
    // Without this, SD_MMC.begin(mountPoint, false) (4-bit) silently uses wrong pins.
#if SDCARD_USE_1BIT_MODE == false || !SDCARD_USE_1BIT_MODE
    if (!SDCARD_USE_1BIT_MODE) {
        SD_MMC.setPins(SDCARD_CLK_PIN, SDCARD_CMD_PIN, SDCARD_D0_PIN,
                       SDCARD_D1_PIN, SDCARD_D2_PIN, SDCARD_D3_PIN);
    }
#endif

    // Enable internal pull-ups on CMD and DATA lines.
    // Dev boards (e.g. N16R8) have PCB pull-ups on SD traces; industrial boards
    // without an onboard SD slot (e.g. Waveshare 8DI8DO) do not.
    // Internal pull-ups (~45kΩ) are weak but cover short traces.
    // For reliable operation with an external SD module, add 10K-22K external pull-ups
    // from CMD and D0 to 3.3V on the wiring harness.
    gpio_set_pull_mode((gpio_num_t)SDCARD_CMD_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)SDCARD_D0_PIN,  GPIO_PULLUP_ONLY);
    if (!SDCARD_USE_1BIT_MODE) {
        gpio_set_pull_mode((gpio_num_t)SDCARD_D1_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)SDCARD_D2_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)SDCARD_D3_PIN, GPIO_PULLUP_ONLY);
    }

        // Stack watermark logs are intentionally disabled in short boot mode.

    // Progressive speed fallback for SDMMC 1-bit: 25MHz → 20MHz → 10MHz → 400kHz.
    // For 4-bit: 40MHz → 25MHz first, then same fallback chain.
    // All messages go to sendVirtualMessage so they appear in the web monitor
    // (critical when Serial is not visible, e.g. ESP32-S3 native USB without CDC enabled).
    if (kSdBootVerbose) {
        char _sdmmc_msg[64];
        snprintf(_sdmmc_msg, sizeof(_sdmmc_msg), "SD: SDMMC %s CLK=%d CMD=%d D0=%d",
            SDCARD_USE_1BIT_MODE ? "1-bit" : "4-bit",
            SDCARD_CLK_PIN, SDCARD_CMD_PIN, SDCARD_D0_PIN);
        sendVirtualMessage(String(_sdmmc_msg), INFO);
    }

    if (!SDCARD_USE_1BIT_MODE) {
        if (kSdBootVerbose) sendVirtualMessage(F("SD: trying 40MHz (4-bit)..."), INFO);
        success = SD_MMC.begin(mountPoint, SDCARD_USE_1BIT_MODE, false, 40000);
        if (!success) {
            if (kSdBootVerbose) sendVirtualMessage(F("SD: 40MHz failed, trying 25MHz..."), WARN);
            SD_MMC.end();
            delay(100);
            success = SD_MMC.begin(mountPoint, SDCARD_USE_1BIT_MODE, false, 25000);
        }
    } else {
        if (kSdBootVerbose) sendVirtualMessage(F("SD: trying 25MHz (1-bit)..."), INFO);
        success = SD_MMC.begin(mountPoint, SDCARD_USE_1BIT_MODE, false, 25000);
    }
    if (!success) {
        if (kSdBootVerbose) sendVirtualMessage(F("SD: 25MHz failed, trying 20MHz..."), WARN);
        SD_MMC.end();
        delay(100);
        success = SD_MMC.begin(mountPoint, SDCARD_USE_1BIT_MODE, false, 20000);
    }
    if (!success) {
        if (kSdBootVerbose) sendVirtualMessage(F("SD: 20MHz failed, trying 10MHz..."), WARN);
        SD_MMC.end();
        delay(100);
        success = SD_MMC.begin(mountPoint, SDCARD_USE_1BIT_MODE, false, 10000);
    }
    if (!success) {
        // Ultra-slow fallback (400kHz = SD card init frequency).
        // Helps when internal pull-ups (~45kΩ) are too weak for higher speeds.
        // External 10k-22kΩ pull-ups on CMD and D0 would fix this at higher speeds.
        if (kSdBootVerbose) sendVirtualMessage(F("SD: 10MHz failed, trying 400kHz (pull-up issue?)..."), WARN);
        SD_MMC.end();
        delay(200);
        success = SD_MMC.begin(mountPoint, SDCARD_USE_1BIT_MODE, false, 400);
    }
    if (!success) {
        sendVirtualMessage(F("SD: all speeds failed — check wiring/pull-ups"), ERROR);
        _sdStatus = -1;
        _sdMounted = false;
        _sdTotal = 0;
        _sdUsed = 0;
        return false;
    }
    sendVirtualMessage(F("SD: mounted"), INFO);
#endif

    // Stack watermark logs are intentionally disabled in short boot mode.

    // Successful mount
    _sdStatus = 0;
    _sdMounted = true;
    Serial.println(F(" ✅ SDMMC montado com sucesso!"));
    
    // Load total write counters from NVS
    Preferences prefs;
    if (prefs.begin("sdcard", false)) {
        _sdWriteCountTotal = prefs.getULong("write_total", 0);
        _sdBytesWrittenTotal = prefs.getULong64("bytes_total", 0);
        prefs.end();
    }
    vTaskDelay(pdMS_TO_TICKS(20)); // Allow NVS cleanup after prefs.end()
    
        // Read metrics (SD_MMC API)

    // Get total card size only. Skip usedBytes() at boot: it calls f_getfree() which traverses
    // the entire FAT table (thousands of reads on a 32GB card). With CONFIG_SPIRAM_USE_MALLOC=1,
    // the internal FatFS cache buffer (>= 16KB) is allocated in PSRAM where DMA cannot write
    // reliably, causing heap corruption at the same fixed PSRAM address every boot.
    // _sdUsed will be populated after the first 30s interval by sdcardUpdateMetrics().
    _sdTotal = SD_MMC.cardSize();
    vTaskDelay(pdMS_TO_TICKS(10)); // Allow SD_MMC internal cleanup after cardSize()
    _sdUsed = 0; // Populated later - unsafe to call usedBytes() at boot with PSRAM

        Serial.print(F("SD: total bytes="));
        Serial.println(_sdTotal);
    
    // CRITICAL: Final delay before returning to allow SD_MMC DMA buffer cleanup.
    // Without this, the next PSRAM allocation (e.g. LittleFS structs) can land adjacent
    // to the SD_MMC internal buffers, which then get zeroed → CORRUPT HEAP: 0xabba1234 → 0x00000000.
    vTaskDelay(pdMS_TO_TICKS(50));
    return true;
}

int sdcardStatus() {
    return _sdStatus;
}

// Split metric refresh to keep heavy usedBytes() out of the fast periodic loop.
static unsigned long _sdLastTotalRefreshMs = 0;
static unsigned long _sdLastUsedRefreshMs = 0;

static void sdcardRefreshTotalBytesIfNeeded(bool force = false) {
    if (!_sdMounted) return;

    const unsigned long now = millis();
    const unsigned long TOTAL_REFRESH_MS = 300000UL; // 5 min
    if (!force && _sdLastTotalRefreshMs != 0 && (now - _sdLastTotalRefreshMs) < TOTAL_REFRESH_MS) return;

    // FTP holds the SD exclusively during its cycle (HISTORIC #57): skip the
    // refresh rather than interleave with the SDMMC host while it walks/streams.
    if (!sdMutexLock(pdMS_TO_TICKS(2000))) return;
    _sdTotal = SD_MMC.cardSize();
    sdMutexUnlock();
    _sdLastTotalRefreshMs = now;
}

static void sdcardRefreshUsedBytesIfNeeded(bool force = false) {
    if (!_sdMounted) return;

    const unsigned long now = millis();
    const unsigned long USED_REFRESH_MS = 180000UL; // 3 min
    if (!force && _sdLastUsedRefreshMs != 0 && (now - _sdLastUsedRefreshMs) < USED_REFRESH_MS) return;

    // See sdcardRefreshTotalBytesIfNeeded: never touch the SD while FTP runs.
    if (!sdMutexLock(pdMS_TO_TICKS(2000))) return;
    _sdUsed = SD_MMC.usedBytes();
    sdMutexUnlock();
    _sdLastUsedRefreshMs = now;
}

void sdcardLoop() {
    static unsigned long lastMetricsUpdate = 0;
    static unsigned long lastAutoMountCheck = 0;
    const unsigned long METRICS_UPDATE_INTERVAL_MS = 30000; // 30 seconds (throttled helpers decide real refresh)
    const unsigned long AUTO_MOUNT_CHECK_INTERVAL_MS = 30000; // 30 seconds (hot-plug check)
    
    unsigned long now = millis();
    
    // Refresh metrics only in background to avoid blocking HTTP handlers.
    // Real I/O frequency is still throttled by the helper intervals.
    if (_sdMounted && (now - lastMetricsUpdate >= METRICS_UPDATE_INTERVAL_MS)) {
        sdcardRefreshTotalBytesIfNeeded(false);
        sdcardRefreshUsedBytesIfNeeded(false);
        lastMetricsUpdate = now;
    }
    
    // 🔌 Hot-plug detection: check for card insertion every 30s (if enabled and not mounted)
    // TOUCH7: the slot is SPI-only (no SDMMC card can ever appear), so the old
    // SDMMC auto-mount was disabled (it coincided with the ~136s reboot cycle,
    // proven 15-09-2026). With the SPI backend (SDCARD_USE_SPI_MODE==1) cards
    // CAN appear, so auto-mount is allowed again only in that mode.
#if !(defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1 && SDCARD_USE_SPI_MODE!=1)
    if (_autoMountEnabled && !_sdMounted && (now - lastAutoMountCheck >= AUTO_MOUNT_CHECK_INTERVAL_MS)) {
        lastAutoMountCheck = now;
        
        if (sdcardDetect()) {
            Serial.println(F("SD: ✅ Card inserted detected, auto-mounting..."));
            
            // Attempt mount (non-blocking, integrated in existing flow)
            if (sdcardBegin()) {
                Serial.println(F("SD: ✅ Auto-mount successful"));
                sendVirtualMessage(F("✅ SD card auto-mounted"), INFO);
                _autoMountEnabled = false; // Auto-disable after successful mount
            } else {
                Serial.println(F("SD: ⚠️ Auto-mount failed, will retry in 30s"));
            }
        }
    }
#endif // !(TARGET_BOARD_S3_TOUCH7)
}

bool sdcardIsMounted(){ return _sdMounted; }


// 🔧 Non-blocking mount: flag and background mount task
static volatile bool _mountInProgress = false;

// Configurable: stack size (bytes) and option to use static stack (BSS) instead of heap
#ifndef SDCARD_MOUNT_STACK_BYTES
#define SDCARD_MOUNT_STACK_BYTES (8 * 1024) // 8 KB by default (dynamic stack)
#endif
#ifndef SDCARD_MOUNT_USE_STATIC_STACK
#define SDCARD_MOUNT_USE_STATIC_STACK 0 // 0 = use dynamic stack (allocated on heap) and freed when the task ends
#endif

#if SDCARD_MOUNT_USE_STATIC_STACK == 1
// Static reserve (BSS) for the mount task stack (width in StackType_t)
static StackType_t sdcardMountStack[SDCARD_MOUNT_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t sdcardMountTcb; // Static TCB
#else
// Task buffers for the dynamic path: allocate once and reuse.
// Stack tries PSRAM first to save internal heap; fallback to internal RAM.
static StackType_t* sdcardMountStackDyn = nullptr;
static StaticTask_t* sdcardMountTcbDyn = nullptr;
#endif

static void sdcard_mount_task(void *arg){
    (void)arg;
    Serial.println(F("SD: montagem em background iniciada"));

    // Monitor stack: record the minimum watermark for overflow detection
    #if configUSE_TRACE_FACILITY == 1 || configUSE_STATS_FORMATTING_FUNCTIONS == 1
      UBaseType_t water = uxTaskGetStackHighWaterMark(NULL);
      Serial.print(F("SD: stack high water mark (words): "));
      Serial.println(water);
      Serial.print(F("SD: stack free bytes aprox: "));
      Serial.println((unsigned long)water * (unsigned long)sizeof(StackType_t));
    #endif

    bool ok = sdcardBegin();
    if(ok) Serial.println(F("SD: montagem em background OK"));
    else Serial.println(F("SD: montagem em background FALHOU"));

    // Re-report stack watermark after init (check for extreme consumption)
    #if configUSE_TRACE_FACILITY == 1 || configUSE_STATS_FORMATTING_FUNCTIONS == 1
      UBaseType_t water2 = uxTaskGetStackHighWaterMark(NULL);
      Serial.print(F("SD: stack high water mark after init (words): "));
      Serial.println(water2);
    #endif

    _mountInProgress = false;
    // Indicate the task is about to end — with dynamic stack, memory is freed by the RTOS
    Serial.println(F("SD: mount finished - task ending (stack will be freed if dynamic)"));
    vTaskDelete(NULL);
}

void sdcardBeginAsync(){
    if(_sdMounted) return; // already mounted
    if(_mountInProgress) return; // already in progress
    _mountInProgress = true;
    // Create mount task (enlarged stack to avoid RTOS stack overflow)
    // SdFat may use a lot of stack during init; we raise to 16KB to avoid stack canary triggers
    TaskHandle_t mountTaskHandle = NULL;
    const size_t SDCARD_MOUNT_STACK = SDCARD_MOUNT_STACK_BYTES; // bytes

#if SDCARD_MOUNT_USE_STATIC_STACK == 1
    // Create static task (stack in BSS, not on heap)
    uint32_t stackWords = (uint32_t)(sizeof(sdcardMountStack) / sizeof(sdcardMountStack[0]));
    mountTaskHandle = xTaskCreateStaticPinnedToCore(sdcard_mount_task, "sd_mount", stackWords, NULL, 1, sdcardMountStack, &sdcardMountTcb, 1);
    if (mountTaskHandle == NULL) {
        Serial.println(F("SD: falha a criar tarefa de montagem (static)"));
        _mountInProgress = false;
    } else {
        Serial.print(F("SD: tarefa de montagem criada (static stack bytes=")); Serial.print(SDCARD_MOUNT_STACK); Serial.println(F(")"));
    }
#else
    // Dynamic path: use static task with reused buffers to avoid
    // new internal-heap allocations on each mount.
    if (sdcardMountTcbDyn == nullptr) {
        sdcardMountTcbDyn = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (sdcardMountStackDyn == nullptr) {
        sdcardMountStackDyn = (StackType_t*)heap_caps_malloc(SDCARD_MOUNT_STACK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!sdcardMountStackDyn) {
            sdcardMountStackDyn = (StackType_t*)heap_caps_malloc(SDCARD_MOUNT_STACK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }

    if (sdcardMountTcbDyn == nullptr || sdcardMountStackDyn == nullptr) {
        Serial.println(F("SD: falha a alocar buffers da task de montagem"));
        _mountInProgress = false;
        return;
    }

    uint32_t stackWords = (uint32_t)(SDCARD_MOUNT_STACK / sizeof(StackType_t));
    mountTaskHandle = xTaskCreateStaticPinnedToCore(sdcard_mount_task, "sd_mount", stackWords, NULL, 1, sdcardMountStackDyn, sdcardMountTcbDyn, 1);
    if (mountTaskHandle == NULL) {
        Serial.println(F("SD: falha a criar tarefa de montagem (dynamic-static)"));
        _mountInProgress = false;
    } else {
        Serial.print(F("SD: tarefa de montagem criada (dynamic-static stack bytes=")); Serial.print(SDCARD_MOUNT_STACK); Serial.println(F(")"));
    }
#endif
}

bool sdcardMountInProgress(){ return _mountInProgress; }

bool sdcardUnmount(){
    if(!_sdMounted) return true; // ✅ Already unmounted
    
    // 💾 Force persistence before unmounting (if there were writes since last save)
    if (_sdWriteCountTotal > 0 || _sdBytesWrittenTotal > 0) {
        Preferences prefs;
        if (prefs.begin("sdcard", false)) {
            prefs.putULong("write_total", _sdWriteCountTotal);
            prefs.putULong64("bytes_total", _sdBytesWrittenTotal);
            prefs.end();
        }
    }
    
    // ⚠️ Arduino SD does not expose direct unmount — we simulate it by clearing state
    _sdMounted = false; _sdStatus = -1; _sdTotal = 0; _sdUsed = 0;
    _sdLastTotalRefreshMs = 0;
    _sdLastUsedRefreshMs = 0;
    // Note: to guarantee flush we could open/close root, but we keep it light
    return true;
}

uint64_t sdcardTotalBytes(){
    // Cached value only: never trigger SD I/O from request handlers.
    return _sdTotal;
}

uint64_t sdcardUsedBytes(){
    // Cached value only: never trigger SD I/O from request handlers.
    return _sdUsed;
}
unsigned long sdcardGetWriteCount() { return _sdWriteCount; } // 📝 Session operation counter
unsigned long sdcardGetWriteCountTotal() { return _sdWriteCountTotal; } // 📊 Total operation counter
uint64_t sdcardGetBytesWritten() { return _sdBytesWritten; } // 📝 Session bytes written
uint64_t sdcardGetBytesWrittenTotal() { return _sdBytesWrittenTotal; } // 📊 Total bytes written

// 🔄 Reset total counter (for card replacement)
bool sdcardResetWriteCounter() {
    _sdWriteCountTotal = 0; // 📊 Zero operation counter in RAM
    _sdWriteCount = 0; // 📝 Zero session operations
    _sdBytesWrittenTotal = 0; // 📊 Zero byte counter in RAM
    _sdBytesWritten = 0; // 📝 Zero session bytes
    _lastNvsSaveTime = millis(); // ✅ Update timestamp to avoid immediate save
    
    // 💾 Persist reset in NVS
    Preferences prefs;
    if (prefs.begin("sdcard", false)) {
        prefs.putULong("write_total", 0);
        prefs.putULong64("bytes_total", 0);
        prefs.end();
        return true; // ✅ Success
    }
    return false; // ❌ Failure
}

// Count folders and files recursively on SD card.
// Recursive walk WITHOUT locking — called by sdcardCountFoldersAndFiles while
// it holds the exclusivity mutex for the whole walk (FTP cycle, HISTORIC #57).
static void sdcardCountUnlocked(int &folderCount, int &fileCount, const char* path) {
    File root = SD_MMC.open(path);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }

    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            folderCount++;
            String subPath = String(path);
            if (!subPath.endsWith("/")) subPath += "/";
            subPath += entry.name();
            sdcardCountUnlocked(folderCount, fileCount, subPath.c_str());
        } else {
            fileCount++;
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

// Used for status page statistics (similar to PSRAM VFS).
// Results are cached externally via TTL — this function scans live.
void sdcardCountFoldersAndFiles(int &folderCount, int &fileCount, const char* path) {
    folderCount = 0;
    fileCount = 0;
    if (!_sdMounted) return;

    // FTP holds the SD exclusively during its cycle (HISTORIC #57): skip the
    // walk (zero counts) rather than interleave with the SDMMC host.
    if (!sdMutexLock(pdMS_TO_TICKS(2000))) return;
    sdcardCountUnlocked(folderCount, fileCount, path);
    sdMutexUnlock();
}

