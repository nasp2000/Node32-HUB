// setup_impl.h
// Included only from chatgpt.ino (single translation unit).
#include "heap_trace.h"
#include "ota_security.h"
#include "CommandRegistry.h"
#include <esp_log.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#if SCREEN_TYPE == 3
#include "touch7_log.h" // T7LOG: ets_printf breadcrumbs, always on UART0
#endif
#if ENABLE_FTP_CLIENT==1
#include "ftp_client.h" // ftpClientInit() at boot (hourly monitor task)
#endif

// ── IDF log → Web Serial Monitor bridge ─────────────────────────────────
// ESP_LOGx emitted by libraries (USB Host, esp_video, ...) otherwise only
// reach UART0 (Serial.setDebugOutput redirects them there); the /monitor page
// never sees them, which makes library-level failures invisible remotely.
// Forward ERROR lines (the ones that explain failures) to sendVirtualMessage,
// re-emit through the previous vprintf so UART keeps working, and guard against
// recursion (sendVirtualMessage may itself trigger an ESP_LOGx).
static vprintf_like_t s_prevEspLogVprintf = nullptr;
extern void sendVirtualMessage(const String &msg, int msgType, bool alreadyFormatted, bool bypassDedup);
static int espLogToWebMonitor(const char *fmt, va_list args) {
    static volatile bool busy = false;
    if (!busy && fmt && fmt[0] == 'E') {
        busy = true;
        char buf[256];
        va_list copy;
        va_copy(copy, args);
        int n = vsnprintf(buf, sizeof(buf), fmt, copy);
        va_end(copy);
        if (n > 0) {
            size_t len = strnlen(buf, sizeof(buf));
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
            if (len) sendVirtualMessage(String(F("[IDF] ")) + buf, ERROR, false, true);
        }
        busy = false;
    }
    if (s_prevEspLogVprintf) return s_prevEspLogVprintf(fmt, args);
    return vprintf(fmt, args);
}

void setup(){ // setup function added (missing caused undefined reference at link)
    Serial.begin(115200); // Initialize Serial first so all boot messages are visible
    heapTraceReset();
    heapTraceMark("boot");
#if SCREEN_TYPE == 3
    T7LOG("setup() entry");
#endif
    // Redirect all ESP-IDF/ROM log output to the same USB CDC serial port.
    // Without this, IDF messages go to UART0 (GPIO43) while Arduino Serial goes to USB CDC,
    // so users see only one or the other depending on which port they connect to.
    // With setDebugOutput(true), everything converges on a single USB CDC COM port.
    // NOTE: very early ROM messages (ESP-ROM:... / rst:0xc ...) are hardwired to UART0
    // by the ROM itself — they cannot be redirected and will always appear on GPIO43/44.
#if SCREEN_TYPE == 3
    // TouchMeteo-7: keep ESP_LOG on UART0 (do NOT redirect to CDC) so the
    // UART monitor shows IDF/panel logs during bring-up. Serial prints still
    // go to USB CDC. See touch7_log.h.
    T7LOG("setup(): ESP_LOG stays on UART0");
#else
    Serial.setDebugOutput(true);
#endif

    commandRegistry.begin();

#if __has_include(<HWCDC.h>) && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT==1
    // Waveshare 8DI8DO: direct USB (no UART-to-USB bridge chip).
    // Serial = HWCDC (USB-C port). Must connect to this port in Serial Monitor.
    // DO NOT use a USB-to-UART adapter on RS232/RXD/TXD header pins:
    //   those pins (GPIO43/44) are UART0 and only carry the early ROM boot log.
    //
    // setTxTimeoutMs(0): prevent Serial.print() blocking for 200 ms per call
    // when no USB host is connected (e.g. powered via PoE or 12 V external supply).
    // Without this, setup() takes ~30 s on a first boot with no PC attached.
    Serial.setTxTimeoutMs(0);
    // Brief wait for the host to re-enumerate the native USB CDC device after reset.
    // On this Waveshare board, Windows often needs longer than 200 ms, otherwise
    // the early firmware banner and startup logs are sent before the terminal reattaches.
    delay(1500);
#else
    delay(100); // Brief settle for UART hardware bridge
#endif

    // IMPORTANT (Waveshare FAQ): Serial Monitor MUST connect to the USB-C port.
    // If you see only "ESP-ROM:... rst:0xc..." messages -> wrong port (UART0/RS232 header).
    // Steps to find the correct port:
    //   1. Disconnect any USB-UART adapter from the RS232 header
    //   2. Connect USB-C cable from board to PC
    //   3. Arduino IDE -> Tools -> Port -> "USB Serial Device (COMxx)"
    //   4. Close and reopen Serial Monitor after each board reset

    // Boot banner: printed immediately and repeated by the heartbeat so the user
    // can identify the correct COM port and confirm the firmware is running.
    Serial.println(F("\r\n========================================"));
    Serial.println(F("  Node32-HUB booting..."));
    Serial.println(F("  Serial Monitor OK - USB CDC port"));
    Serial.println(F("  (connect via USB-C, NOT RS232 header)"));
    Serial.println(F("========================================"));
    Serial.flush(); // Ensure the banner is transmitted before the rest of setup runs
    // S3-CNC WDT forensics (2026-10-10): report previous boot's USB bring-up
    // stage (RTC slow mem, survives WDT reset). Prints BEFORE any crash window,
    // so the next boot log self-reports where bring-up died (BootMark at the
    // end of setup preserves a known stage). Safe: printf+flush only.
    // Guarded: usb_host_manager.h is only included in chatgpt.ino when
    // ENABLE_USB_HOST==1, and packs without USB host would not compile.
#if ENABLE_USB_HOST == 1
    Serial.printf("CRASH_STAGE: %s\n", usbHostBeginStage());
#endif
    Serial.flush();

    (void)setOnboardLedState(false); // Force the board onboard LED to remain off by default.

    // Allocate RS232 line accumulator buffers in PSRAM (critical).
    // These are used by loop_impl.h for both SD and RAM (monitor/email) paths,
    // so they must be allocated even when ENABLE_SDCARD=0.
    sdcardLineAccumulator_RS232 = (char*)memoryPressureSafeMalloc(SDCARD_LINE_ACCUMULATOR_SIZE, MALLOC_CAP_SPIRAM, "sdcardLineAccumulator_RS232");
    sdcardLineAccumulator_LPT   = (char*)memoryPressureSafeMalloc(SDCARD_LINE_ACCUMULATOR_SIZE, MALLOC_CAP_SPIRAM, "sdcardLineAccumulator_LPT");
    ramLineAccumulator_RS232    = (char*)memoryPressureSafeMalloc(SDCARD_LINE_ACCUMULATOR_SIZE, MALLOC_CAP_SPIRAM, "ramLineAccumulator_RS232");

    // CRITICAL: Watchdog timeout 120s (sufficient for EMAILDAY + provides safety)
    // Loop stability achieved via strategic yields (not logging delays)
    esp_task_wdt_deinit(); // Disable default watchdog before reconfiguring
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 120000,  // 120 seconds
        // idle_core_mask = 0: do NOT monitor idle tasks.
        // Core 0 runs sdCardWriteTask (priority 3-5), emailTask, and other background tasks
        // that can legitimately starve the idle task for extended periods. Monitoring idle tasks
        // here would cause false-positive TWDT resets (CRASH_TASK_WDT) even when the system
        // is healthy. The main loop task is still protected via esp_task_wdt_add(NULL) below.
        .idle_core_mask = 0,
        .trigger_panic = true  // Panic on timeout (keeps debug behavior)
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL); // Adicionar task setup ao watchdog
    
    // Check NVS partition size (128KB custom vs 20KB default)
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    if (it) {
        const esp_partition_t* nvs = esp_partition_get(it);
        Serial.printf("NVS: %lu KB%s\n", nvs->size / 1024,
                      (nvs->size < 0x20000) ? " [WARNING: small NVS, use custom partition]" : "");
        esp_partition_iterator_release(it);
    }
    
    // Disable Bluetooth permanently to free its reserved internal RAM.
    // Must be called early, before any BT stack initialization.
    // IMPORTANT: ESP32-S3 has BLE only (no Classic BT). Using ESP_BT_MODE_BTDM on S3
    // returns ESP_ERR_INVALID_ARG and frees nothing. Use ESP_BT_MODE_BLE instead.
    // Typical gain: ~100-120 KB of internal heap.
    // P4 has no BT controller on-die (C6 companion handles BT via SDIO) — skip entirely.
    #if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
    esp_err_t btRes = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (btRes != ESP_OK) {
        Serial.printf("[BT] WARNING: esp_bt_controller_mem_release failed (err=0x%x). BT memory NOT freed!\n", (int)btRes);
    }
    #endif
    
    // Email and log mutexes removed - using atomic flags (emailBusy, logBusy) instead to avoid FreeRTOS priority inheritance bugs

    // PSRAM log buffer allocation (Point 5)
    if (psramFound()) {
        size_t sizeBytes = LOG_BUFFER_SIZE * sizeof(SerialMessage);
        // Usamos raw malloc para buffer. NOTA: SerialMessage n?o deve ter classes com construtores (usa char arrays)
        psramLogBuffer = (SerialMessage*) heap_caps_malloc(sizeBytes, MALLOC_CAP_SPIRAM);
        if (psramLogBuffer) {
             memset(psramLogBuffer, 0, sizeBytes);
             psramLogHead = 0;
             psramLogCount = 0;
        } else {
             safeEnviarMensagemVirtual("LOG: Failed to allocate PSRAM Buffer!", ERROR);
        }
    }

    // POINT 7: Initialize buffers in PSRAM (bufferDados) — critical.
    // Ensure memory is allocated now (preferably in PSRAM)
    if (!bufferDados.begin()) {
         memoryPressurePanic("bufferDados (128KB RS232 circular buffer) allocation failed");
    }

    // Initialize printer output buffer in PSRAM (2KB) — critical.
    if (!printerOutputBuffer.begin()) {
         memoryPressurePanic("printerOutputBuffer (2KB) allocation failed");
    }

    // Initialize LPT email buffer (separate 128KB in PSRAM) — critical.
    if (!bufferDadosLPT.begin()) {
         memoryPressurePanic("bufferDadosLPT (128KB LPT buffer) allocation failed");
    }

    // OPTIMIZATION (PSRAM): Allocate reception buffer (Raw Rx BufferRx) — critical.
    // Allocate 128KB in PSRAM to absorb large bursts from the Nano without pausing (XOFF).
    if (psramFound()) {
        bufferRx = (uint8_t*)memoryPressureSafeCalloc(BUFFER_RX_SIZE, 1, MALLOC_CAP_SPIRAM, "bufferRx (PSRAM)");
    } else {
        bufferRx = (uint8_t*)calloc(BUFFER_RX_SIZE, 1);
        if (!bufferRx) {
            memoryPressurePanic("bufferRx (128KB DRAM fallback) allocation failed");
        }
    }

#if ENABLE_SDCARD==1
    // Pre-allocate SD buffers in PSRAM as much as possible to avoid reallocations (128KB)
    // IMPROVEMENT 3: Double buffering A/B (4 buffers of 128KB = 512KB PSRAM)
    if (psramFound()) {
        SDCARD_BUFFER_THRESHOLD_FLUSH = 98304; // 96KB — dynamic growth via PSRAM allocator
    } else {
        SDCARD_BUFFER_THRESHOLD_FLUSH = 4096; // 4KB em RAM interna
        size_t RAM_BUF_SIZE = 6144; // 6KB reserva
        sdcardLogBuffer_RS232_A.reserve(RAM_BUF_SIZE);
        sdcardLogBuffer_RS232_B.reserve(RAM_BUF_SIZE);
        sdcardLogBuffer_LPT_A.reserve(RAM_BUF_SIZE);
        sdcardLogBuffer_LPT_B.reserve(RAM_BUF_SIZE);
        safeEnviarMensagemVirtual("PSRAM missing: Reduced SD buffers (4KB limit / 6KB reserved) in internal RAM", WARN);
    }

    rs232LiveMutex = xSemaphoreCreateRecursiveMutex();
    if (!rs232LiveMutex || !rs232LiveBuffer.begin()) {
        safeEnviarMensagemVirtual("RS232 live buffer unavailable", WARN);
    }
#endif // ENABLE_SDCARD (pre-alloc SD buffers + rs232LiveMutex)

    // Volatile .chart rings (PSRAM-first serving for all .chart APIs).
    // Non-fatal when PSRAM is missing (APIs fall back to files); stubbed for
    // builds without the data pipeline.
    chartRingsBegin();
    heapTraceMark("psram_buffers");   // PSRAM sinks + chart rings (should be ~0 KB internal)

#if ENABLE_SMTP==1
    smtpInitStaticBase64Buffers();
#endif

#if ENABLE_SDCARD==1
    // Improvement 3: init SD buffer swap mutex and create a dedicated write task
    // CRITICAL: Must be RECURSIVE because sdcardSwapLock implements manual recursion
    sdcardBufferSwapMutex = xSemaphoreCreateRecursiveMutex();
    
    // NOTE: sdMutex (for SD file operations) is created in sdcard.cpp
    // No need to create separate mutex here - we reuse the existing one
    
    if (sdcardBufferSwapMutex) {
        sdCardWriteTaskTCB = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!sdCardWriteTaskTCB) {
            memoryPressurePanic("sdCardWriteTaskTCB (StaticTask_t) allocation failed");
        }
        // ESP32-S3 with OPI PSRAM does not have the cache-coherency bug present on original ESP32.
        // The email tasks already use PSRAM stacks safely on this platform.
        // Using PSRAM for this stack frees 16KB of internal heap.
        sdCardWriteTaskStack = (StackType_t*)memoryPressureSafeMalloc(SDCARD_WRITE_TASK_STACK_SIZE, MALLOC_CAP_SPIRAM, "sdCardWriteTaskStack (PSRAM)");
        // PSRAM unavailable: this will panic — SD cannot operate without stack memory.

        if (sdCardWriteTaskTCB && sdCardWriteTaskStack) {
            sdCardWriteTaskHandle = xTaskCreateStaticPinnedToCore(
                sdCardWriteTask,        // Task function
                "SDWrite",              // Name
                SDCARD_WRITE_TASK_STACK_SIZE, // Stack size in bytes
                nullptr,                // Parameter
                1,                      // Low priority
                sdCardWriteTaskStack,   // Stack array
                sdCardWriteTaskTCB,     // TCB
                0                       // Core 0 (same core as SD)
            );
        } else {
            sdCardWriteTaskHandle = NULL;
        }
        if (sdCardWriteTaskHandle != NULL) {
            safeEnviarMensagemVirtual("SD Write task: OK (double buffering)", INFO);
        } else {
            safeEnviarMensagemVirtual("SD Write task: FAILED", ERROR);
        }
    }
#endif // ENABLE_SDCARD (buffer swap mutex + write task)

    // Initialize PSRAM Virtual Filesystem (fallback when SD unavailable)
    if (psramFound()) {
        if (psramVFSBegin()) {
            psramVFSInitialized = true;
            safeEnviarMensagemVirtual("PSRAM VFS: 5MB circular buffer initialized (volatile storage)", INFO);
        } else {
            psramVFSInitialized = false;
            safeEnviarMensagemVirtual("PSRAM VFS: initialization failed", WARN);
        }
    } else {
        psramVFSInitialized = false;
        safeEnviarMensagemVirtual("PSRAM VFS: disabled (no PSRAM available)", INFO);
    }

    // bufferDadosMutex removed - using bufferDadosBusy atomic flag instead (avoid FreeRTOS priority inheritance bugs)
    // Atomic flag initialized to false at declaration (line 448)

    // Protect CircularBuffer<String> that can be accessed from multiple tasks.
    // CRITICAL: Must be RECURSIVE because pushDeferredDirectLog can be called from nested contexts
    deferredDirectLogsMutex = xSemaphoreCreateRecursiveMutex();
    if (!deferredDirectLogsMutex) {
        safeEnviarMensagemVirtual("Failed to create direct log mutex", WARN);
    }

    // CRITICAL: Must be RECURSIVE because rs232QueueLock implements manual recursion
    rs232SendQueueMutex = xSemaphoreCreateRecursiveMutex();
    if (!rs232SendQueueMutex) {
        safeEnviarMensagemVirtual("Failed to create RS232 send queue mutex", WARN);
    }

    // Optionally we could log, but keep silent to avoid polluting serial in production.

    // CRITICAL: Mount LittleFS/FFat before any module attempts to load config from storage
    // This must happen early, before buzzer, unikon, uda1334a, and other modules call begin()
#if ENABLE_FILESYSTEM==1
#if SCREEN_TYPE == 3
    T7LOG("setup(): LittleFS mount...");
#endif
    if (littlefsBegin()) {
        safeEnviarMensagemVirtual("LittleFS: Mounted successfully", INFO);
#if SCREEN_TYPE == 3
        T7LOG("setup(): LittleFS mounted");
#endif
    } else {
        safeEnviarMensagemVirtual("ERROR: LittleFS mount failed - configuration persistence unavailable", ERROR);
#if SCREEN_TYPE == 3
        T7LOG("setup(): LittleFS MOUNT FAILED");
#endif
    }
#endif
    heapTraceMark("littlefs");

    // PHASE 1: Pre-allocate savedNetworks vector to avoid fragmentation
    savedNetworks.reserve(MAX_SAVED_NETWORKS); // Reserve slots for 10 networks

#if SCREEN_TYPE == 3
    T7LOG("setup(): calling applyRS232Config");
#endif
    applyRS232Config(); // Execute consolidated initialization sequence (includes setupBasicPrinterPins)
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    // S3-CNC WDT trial markers (2026-10-11): bracket the post-Boot-OK tail so the
    // next serial capture shows exactly which begin wedges. Literals + flush only.
    Serial.printf("STAGE: tail-start\n"); Serial.flush();
#endif

#if ENABLE_PROXIMITY==1
    proximityBegin();
#endif
    heapTraceMark("proximity");
#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1
    tca9554Begin();
    tca9554SetInvert((uint8_t)(configManager.getSection("do")["invert"] | 0)); // apply saved inversion mask
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    Serial.printf("STAGE: post-tca\n"); Serial.flush();
#endif
#if defined(BOARD_HAS_DS3231) && BOARD_HAS_DS3231 == 1
    rtcBegin(); // detect DS3231, apply saved time to system clock before NTP
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    Serial.printf("STAGE: post-rtc\n"); Serial.flush();
#endif
#if ENABLE_BUZZER == 1
    buzzerBegin(); // Init GPIO, load config from LittleFS
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    Serial.printf("STAGE: post-buzzer\n"); Serial.flush();
#endif
#if ENABLE_UDA1334A == 1
    uda1334aConfig.begin(); // Load config from LittleFS
    radioAudioRuntimeBegin();
    // DAC init moved to radioAudioRuntimeLoop(): i2s_driver_install()
    // allocates DMA buffers in PSRAM which corrupts FatFS. Must run AFTER
    // the SD card has been fully accessed (file explorer, library, etc.).
#endif
#if ENABLE_UNIKON == 1
    unikonBegin(); // Open UART, load config from LittleFS, start idle polling
#endif
#if ENABLE_BRICON == 1
    briconBegin(); // Load config, enforce module exclusion, request RS232 profile
#endif
#if ENABLE_IPG_LASER == 1
    ipgLaserBegin();
#endif
#if ENABLE_CAN == 1
    canRuntimeInit(); // Allocate PSRAM buffer, start TWAI driver and RX task
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    Serial.printf("STAGE: post-periph\n"); Serial.flush();
#endif
#if ENABLE_USB_HOST == 1
    // Forward IDF ERROR logs to the Web Monitor BEFORE the USB host starts,
    // so USB enumeration failures (normally UART-only) become visible on
    // /monitor. See espLogToWebMonitor() above.
    // S3-CNC WDT trial (2026-10-10): bridge DISABLED. Suspect: heap-String in
    // global log hook recursing on first IDF ERROR inside usb_host_install
    // (empty S3 port) -> double-fault -> silent TG1WDT. Logs fall back to UART
    // as pre-v1.115. Revert: uncomment the line below.
    // s_prevEspLogVprintf = esp_log_set_vprintf(&espLogToWebMonitor);
    // OTG policy: other packs start the host at boot (plan B, 2026-09-14).
    // S3 CNC boots with OTG OFF: boot-time begin wedged boards with empty
    // port (silent TG1WDT, hunted Oct 2026). The GRBL page auto-starts the
    // host on every visit via /api/grbl/transport (session-only, never
    // persisted), which is the proven-safe lazy path (v1.06-era behavior).
    // BootMark first: records "boot" only when RTC holds no known stage,
    // preserving a previous crash point for post-mortem reads.
    usbHostBeginStageBootMark();
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    Serial.printf("STAGE: post-bootmark\n"); Serial.flush();
#endif
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    // S3 CNC: host stays down here — page visit brings it up on demand.
    // Revert: delete this guard to restore host-at-boot.
#else
    usbHostBegin(); // Start USB Host stack and CDC-ACM class driver
#endif
#endif
#if ENABLE_GRBL == 1
    grblSenderInit(); // Initialise GRBL character-counting flow-control sender
#endif
#if ENABLE_FTP_CLIENT == 1
    // Start the hourly FTP monitor task at boot. It used to be created lazily
    // on the first FTP page visit, so any reboot silently killed automatic
    // uploads until someone opened /ftp. Idempotent; the 30 s tick runs a
    // cycle only when enabled + auto_upload + interval elapsed + network up.
    ftpClientInit();
#endif
    heapTraceMark("modules");   // usb/grbl/ftp + the small per-module begins above
#if ENABLE_USBIP_SERVER == 1
    {
        JsonVariant usbServer = configManager.getSection("usb_server");
        UsbIpRuntimeConfig usbCfg;
        usbCfg.enabled = usbServer["enabled"] | true;
        usbCfg.listen_port = (uint16_t)(usbServer["port"] | 3240);
        usbCfg.recv_timeout_ms = (uint16_t)(usbServer["recv_timeout_ms"] | 1200);
        usbCfg.in_wait_timeout_ms = (uint16_t)(usbServer["in_wait_timeout_ms"] | 220);
        usbipServerSetRuntimeConfig(usbCfg);
    }
    usbipServerBegin(); // Start USB/IP TCP server (bring-up scaffold)
#endif

    // Mark boot as valid so bootloader does not roll back on next reboot.
    // If this code does not run (e.g. crash during setup), the bootloader
    // reverts to the previous OTA partition.
    otaMarkBootValid();
}