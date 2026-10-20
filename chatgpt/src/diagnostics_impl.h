// diagnostics_impl.h
// Included only from chatgpt.ino (single translation unit).
// Runtime temporary OTA enable window (millis-based UTC since boot). When zero, OTA uploads are refused.
unsigned long otaEnabledUntil = 0;

// Enable OTA for a short window (ms). Returns new expiry timestamp.
unsigned long otaEnableFor(unsigned long ms) {
    otaEnabledUntil = millis() + ms;
    return otaEnabledUntil;
}

// Existing global variables in chatgpt_ultimo.txt (kept and used).
// Data buffer optimized for PSRAM
// POINT 7: Moved to PSRAM and increased from 8KB to 128KB
const int BUFFER_DATA_SIZE = 131072; // Buffer increased to 128KB (PSRAM).
PsramCircularBuffer<uint8_t> bufferDados(BUFFER_DATA_SIZE, true); // PSRAM buffer with deferred allocation (RS232 only).

// LPT-specific buffer: data from DB25/LPT goes here (backup saved to SD before email send)
PsramCircularBuffer<uint8_t> bufferDadosLPT(BUFFER_DATA_SIZE, true); // 128KB PSRAM for LPT data.
int bufferDadosLPTLineCount = 0; // Line counter for LPT buffer (separate from RS232).

// Deferred direct logs mutex (with tracking)
SemaphoreHandle_t deferredDirectLogsMutex = NULL;
volatile unsigned long deferredDirectLogsMutexFailCount = 0;
TaskHandle_t deferredDirectLogsMutexOwnerTask = NULL;
volatile uint32_t deferredDirectLogsMutexBadUnlockCount = 0;
uint32_t deferredDirectLogsMutexDepth = 0;

// RS232 template dirty flag
bool rs232TemplateDirty = false;

// ===== LEGACY VARIABLES FOR ACCESS POINT (compatibility) =====
unsigned long AP_ACTIVATION_DELAY = 10000;     // Delay to activate AP after disconnect.
unsigned long AP_RETRY_DELAY = 30000;          // Delay between AP attempts.
int MAX_AP_FAILURES = 5;                       // Maximum failures before pausing.
unsigned long AP_FAILURE_PAUSE = 300000;       // Pause after many failures (5 min).
unsigned long lastAPAttemptTime = 0;           // Last AP attempt time.
int apFailureCount = 0;                        // AP failure counter.
bool manualNetworkSwitch = false;              // Flag for manual network switch.

// Non-RS232 globals used by shared code (defined regardless of RS232 flag)
volatile unsigned long bufferDadosSeq = 0; // Sequence number of bytes inserted into bufferDados
String lastDataSourceForEmail = "RS232"; // Last data source for SD Card logging (RS232 or LPT)
static bool globalEmailAutoSendEnabled = false;  // Master flag for email auto-send
static bool globalSdcardEmailSendEnabled = true; // Master flag for SD card email send

#if ENABLE_RS232==1
// RS232 (MAX3232) - configuration and global variables.
bool rs232_enabled = true; // If RS232 communication is enabled.
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)
int rs232_baudrate = 9600; // Transmission rate (baud).
#else
int rs232_baudrate = 19200; // Transmission rate (baud).
#endif
bool rs232_invert_pins = false; // Whether RX/TX pins should be inverted.
// Default pins for RS232.
// N16R8 devkit: UART2 mapped to GPIO16 (RX) / GPIO17 (TX).
// Waveshare 8DI8DO POE board: RS232 uses GPIO44 (RX) / GPIO43 (TX) — overridden in board_pins.h.
// invert_pins swaps RX/TX (useful when cable is wired backwards).
#ifndef RS232_DEFAULT_RX_PIN
#define RS232_DEFAULT_RX_PIN 16
#endif
#ifndef RS232_DEFAULT_TX_PIN
#define RS232_DEFAULT_TX_PIN 17
#endif
int rs232_rx_pin = RS232_DEFAULT_RX_PIN; // Receive pin.
int rs232_tx_pin = RS232_DEFAULT_TX_PIN; // Transmit pin.
HardwareSerial RS232(2); // UART2.
// New RS232 parameters (framing/flow/timeout)
int rs232_data_bits = 8;     // 5,6,7,8
String rs232_parity = "none"; // "none","even","odd"
int rs232_stop_bits = 1;     // 1,2
int rs232_read_timeout_ms = 1000; // Read timeout in ms
// RS232 reception mode: RAW passthrough (true) does not interpret lines as commands
bool rs232_passthrough_raw = true; // default: RAW to avoid encapsulating received messages
bool rs232_mirror_to_usb = true;  // mirrors RS232 bytes to Serial USB when in RAW (ON: allows monitor debug)

// ===== RS232 SEND QUEUE & LOCK =====
// Ensure we never send two commands at the same time and that
// we do not send while receiving data (DB25 or RS232).
volatile bool rs232_sending = false; // true while executing the send operation (writing)
PsramQueue<String> rs232SendQueue(32); // PSRAM queue (32 slots, saves ~3KB DRAM)

// RS232 queue mutex primitives and enqueueRS232Command are defined in
// src/runtime_rs232_queue.cpp and declared in src/runtime_rs232_queue.h.
// Included below after the global variables section.
// When the firmware receives XOFF from the remote, local TX must pause.
volatile bool rs232_paused_by_controller = false;
bool rs232_blockQueueForEmail = false;            // Blocks RS232 TX queue while email work is pending
bool rs232LastWasCR = false;                      // CRLF de-dup helper for RS232 counting
bool rs232SkipNextLF = false;                     // When CR counts as newline, ignore the immediate LF
// Passive detection of XON/XOFF sent by the remote (when we are receiver or during our TX)
volatile bool rs232RemoteSendsXonXoff = false;  // ? Visto pelo menos um XON/XOFF do remoto
volatile unsigned long rs232LastRemoteXonMs = 0;
volatile unsigned long rs232LastRemoteXoffMs = 0;
volatile unsigned long rs232RemoteXonCount = 0;
volatile unsigned long rs232RemoteXoffCount = 0;
// Auto-detect: if the remote uses XON/XOFF, cooperate even if config/template say no
bool rs232AutoFlowDetectEnabled = true;         // Active by default (runtime only)

// Forward declaration
bool isFlowControlEngaged();
// RS232 backpressure is driven by the shared RAM buffer high-water mark.
int rs232LineCount = 0;
int rs232LinesSinceLastTx = 0;
bool rs232ChunkPauseActive = false;               // Deprecated: legacy chunk mode (kept for compatibility)
bool rs232ForceLines66NextEmail = false;          // Deprecated: legacy one-shot override (kept for compatibility)
bool rs232RawPauseWasSent = false;                // If a raw XOFF was sent, ensure we resume with raw XON
bool rs232PausedByHighWater = false;              // True after we pause RS232 due to buffer high-water
unsigned long rs232PauseStartMillis = 0;
const unsigned long RS232_QUEUE_PROCESS_INTERVAL_MS = 10; // process queue every 10ms (responsive when idle)
// Cold-start gate: RS232 TX queue is blocked for this many ms after boot to let the system stabilise.
// Commands enqueued during this window are discarded.
const unsigned long RS232_COLD_START_DELAY_MS = 45000UL; // 45 seconds
unsigned long lastRs232QueueProcess = 0;
// RS232 micro-pause logic disabled (kept as inert state for compatibility).
const int RS232_MICROPAUSE_LINE_THRESHOLD = 120;
const int RS232_MICROPAUSE_JOIN_LINES     = 60;
const unsigned long RS232_MICROPAUSE_MIN_MS = 500;
bool rs232MicroPauseSkipFirstBreak = false;
uint8_t rs232BreaksSinceMicroPauseArm = 0;
bool rs232HoldXonUntilEmailDone = false;
int rs232CumulativeLines = 0;
int rs232CumulativeAtResume = 0;
int rs232CumulativeAtArm = 0;
bool rs232MicroPauseFirstBreakInfoShown = false;

// RS232 first response protection
// Objective: avoid the first reception block of a response being interrupted too early by XOFF at 66 LFs.
// Rule: after sending an RS232 command, during a short period (time) OR until a few initial lines, we do not fire automatic XOFF.
bool rs232FirstResponseGuardActive = false;         // Guard active after a successful send
unsigned long rs232FirstResponseGuardUntil = 0;     // Timestamp until which the guard is active (deadline)
const int RS232_FIRST_RESPONSE_MIN_LINES = 10;      // Base minimum (default) initial lines to allow without XOFF
// Dynamic version to allow widening the window on the first block after each command
int rs232FirstResponseMinLines = RS232_FIRST_RESPONSE_MIN_LINES; // Dynamic minimum lines for the guard
const unsigned long RS232_FIRST_RESPONSE_GUARD_MS = 800; // Maximum guard time window (ms)

// Runtime override: when a template requests XON/XOFF, this flag is set temporarily
bool rs232_template_use_xon_xoff = false;
// Timestamp of the last RS232 configuration application (to enforce a minimum wait before the first send)
unsigned long lastRs232ConfigMillis = 0;
// RS232 port initialization flag (used across multiple functions; non-static so command_executor.cpp can access it via extern)
// Must be volatile: read by Core 0 (AsyncWebServer) and written by Core 1 (loop), no mutex.
volatile bool _rs232PortInitialized = false;
// Deferred RS232 apply flag: set by web handlers (async task) so applyRS232Config() runs safely on the main loop only.
volatile bool _pendingRS232Apply = false;

// List of predefined baud rates for RS232.
extern const int RS232_BAUDRATES[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
extern const int RS232_BAUDRATES_COUNT = sizeof(RS232_BAUDRATES) / sizeof(RS232_BAUDRATES[0]); // Size of the list.

// Selected index (can be changed via JSON or external command).
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)
int rs232_baudrate_index = 3; // 9600 as default.
#else
int rs232_baudrate_index = 4; // 19200 as default.
#endif
#endif // ENABLE_RS232

// PRINTER_EMULATION_MODES moved to src/printer_emulation.cpp

String ssid, password; // SSID and password for Wi-Fi network

bool apActive = false;                          // Whether the Access Point is active.
bool forceAPMode = false;                      // Force AP mode via command.
bool apLockedUntilReboot = false;              // After first stable STA connection, do not re-enable AP until reboot.
String currentConnectedSSID = "";              // SSID of the currently connected network.
String lastStaReconnectLoggedIP = "";          // Last IP reported in a reconnect log.
String lastStaReconnectLoggedSSID = "";        // Last SSID reported in a reconnect log.
unsigned long lastStaReconnectLogMs = 0;       // Last reconnect log timestamp.

void captureSdBufferDiagnosticsSnapshot(SdBufferDiagnosticsSnapshot& snapshot) {
    snapshot.rs232ActiveSize = 0;
    snapshot.rs232ActiveCapacity = 0;
    snapshot.lptActiveSize = 0;
    snapshot.lptActiveCapacity = 0;
    snapshot.rs232Preview = "";
    snapshot.lptPreview = "";

#if ENABLE_SDCARD==1
    if (sdcardSwapLock(pdMS_TO_TICKS(10))) {
        auto* rs232Buf = sdcardActiveBuffer_RS232.load(std::memory_order_acquire);
        if (rs232Buf) {
            snapshot.rs232ActiveSize = (int)rs232Buf->size();
            snapshot.rs232ActiveCapacity = (int)rs232Buf->capacity();
            if (!rs232Buf->empty()) {
                size_t previewCount = min((size_t)20, rs232Buf->size());
                size_t startPos = rs232Buf->size() - previewCount;
                for (size_t i = startPos; i < rs232Buf->size(); ++i) {
                    String sample = (*rs232Buf)[i].dataAsString();
                    for (size_t k = 0; k < sample.length(); ++k) {
                        char c = sample[k];
                        if (c == '"') snapshot.rs232Preview += F("\\\"");
                        else if (c == '\\') snapshot.rs232Preview += F("\\\\");
                        else if (c == '\n') snapshot.rs232Preview += F("\\n");
                        else if (c == '\r') snapshot.rs232Preview += F("\\r");
                        else if (c == '\t') snapshot.rs232Preview += F("\\t");
                        else if (c >= 32 && c < 127) snapshot.rs232Preview += c;
                        else {
                            char hexBuf[8];
                            snprintf(hexBuf, sizeof(hexBuf), "\\x%02X", (uint8_t)c);
                            snapshot.rs232Preview += hexBuf;
                        }
                    }
                    if (i < rs232Buf->size() - 1) snapshot.rs232Preview += F(" | ");
                }
            }
        }

        auto* lptBuf = sdcardActiveBuffer_LPT.load(std::memory_order_acquire);
        if (lptBuf) {
            snapshot.lptActiveSize = (int)lptBuf->size();
            snapshot.lptActiveCapacity = (int)lptBuf->capacity();
            if (!lptBuf->empty()) {
                size_t previewCount = min((size_t)20, lptBuf->size());
                size_t startPos = lptBuf->size() - previewCount;
                for (size_t i = startPos; i < lptBuf->size(); ++i) {
                    String sample = (*lptBuf)[i].dataAsString();
                    for (size_t k = 0; k < sample.length(); ++k) {
                        char c = sample[k];
                        if (c == '"') snapshot.lptPreview += F("\\\"");
                        else if (c == '\\') snapshot.lptPreview += F("\\\\");
                        else if (c == '\n') snapshot.lptPreview += F("\\n");
                        else if (c == '\r') snapshot.lptPreview += F("\\r");
                        else if (c == '\t') snapshot.lptPreview += F("\\t");
                        else if (c >= 32 && c < 127) snapshot.lptPreview += c;
                        else {
                            char hexBuf[8];
                            snprintf(hexBuf, sizeof(hexBuf), "\\x%02X", (uint8_t)c);
                            snapshot.lptPreview += hexBuf;
                        }
                    }
                    if (i < lptBuf->size() - 1) snapshot.lptPreview += F(" | ");
                }
            }
        }
        sdcardSwapUnlock();
    }
#endif // ENABLE_SDCARD
}

static void logStaReconnectEvent(const char *reason, bool forceLog = false) {
    if (WiFi.status() != WL_CONNECTED) return;

    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0,0,0,0)) return;

    String ipString = ip.toString();
    String ssidString = WiFi.SSID();
    unsigned long nowMs = millis();
    bool sameTarget = (ipString == lastStaReconnectLoggedIP && ssidString == lastStaReconnectLoggedSSID);
    bool throttleActive = sameTarget && ((nowMs - lastStaReconnectLogMs) < 15000UL);
    if (!forceLog && throttleActive) return;

    lastStaReconnectLoggedIP = ipString;
    lastStaReconnectLoggedSSID = ssidString;
    lastStaReconnectLogMs = nowMs;

    String message = String(F("STA reconnect"));
    if (reason && reason[0] != '\0') {
        message += String(F(" [")) + String(reason) + String(F("]"));
    }
    message += String(F(": ")) + ssidString + String(F(" @ ")) + ipString;
    sendVirtualMessage(message, INFO);
    logCompactBufferedEvent("WIFI_RECONNECT", message.c_str());
}

void logManualNetworkSwitchReconnect() {
    logStaReconnectEvent("manual-switch", true);
}

void logHybridStaReconnectEvent(const char* reason, bool forceLog) {
    logStaReconnectEvent(reason, forceLog);
}

bool autoSendEmail = false; // Default OFF — auto-send email only when explicitly configured (per-pack config or page save).
bool emailNoRecipientFlag = false; // Flag to mark cycle aborted due to missing recipients (avoids repeated WARNs)

// Configuration to drop emails with little content.
int minEmailContentLength = 3; // Default: 3 characters minimum.

// Global compact mode for all outputs (Serial/Telnet/Web); uses short prefixes and sanitized content.
bool compactLogMode = true;

// ═══════════════════════════════════════════════════════════════════════════
// OBJECT POOLS :: ESPHome-style Memory Reuse System
// ═══════════════════════════════════════════════════════════════════════════
// Eliminates repeated heap allocations for JSON docs and string buffers.
// Benefits: 10-15KB heap savings + eliminates fragmentation + faster allocations.
// Usage: Use POOLED_JSON_256/512/1024/2048 macros in handlers for automatic acquire/release.

// JSON document pools (capacity, max pool size)
JsonPool256  jsonPool256(6);   // 6×256B  = 1.5KB pool (small responses)
JsonPool512  jsonPool512(6);   // 6×512B  = 3KB pool (medium responses)
JsonPool1024 jsonPool1024(4);  // 4×1024B = 4KB pool (large responses)
JsonPool2048 jsonPool2048(3);  // 3×2048B = 6KB pool (very large responses)

// String buffer pools (capacity, max pool size)
StringBufferPool stringPool256(256, 8);    // 8×256B  = 2KB pool (small strings)
StringBufferPool stringPool1024(1024, 6);  // 6×1024B = 6KB pool (medium strings)
StringBufferPool stringPool4096(4096, 2);  // 2×4096B = 8KB pool (large strings)

// Async task scheduler for non-blocking operations
AsyncTaskScheduler asyncScheduler;

// OTA (Over-The-Air Updates) - On-Demand Activation Strategy
// ===========================================================
// OPTIMIZATION: OTA services (ArduinoOTA) consume 5-8KB heap even when not in use.
// Strategy: Only activate OTA when explicitly requested via /ota endpoint or OTA_START command.
// Implementation: Add lazy initialization in webpage_impl.h handler for /ota route.
// Benefits: Saves 5-8KB heap until OTA actually needed (most devices never use OTA).
// 
// To implement:
// 1. Add ArduinoOTA.begin() call in /ota endpoint handler (webpage_impl.h)
// 2. Add ArduinoOTA.handle() in loop() only when otaActive==true
// 3. Add 30-minute timeout to auto-disable OTA if not used
// 
#if ENABLE_OTA==1
bool otaActive = false;                    // OTA currently active flag
unsigned long otaActivatedTime = 0;        // When OTA was last activated
const unsigned long OTA_TIMEOUT = 1800000; // 30 minutes timeout (auto-disable)
// Note: ArduinoOTA initialization deferred to /ota endpoint access
#endif

// RAM-only: name of the last applied RS232 template (not automatically persisted)
String lastAppliedRs232TemplateName = "";

// REMOVED: duplicate nomeParaEmail and scheduledCommands; now use ConfigManager directly.

// Circular buffer for serial monitor messages (struct defined in webpage.h).

// OPTIMIZATION (Point 5): Log buffer in PSRAM (~256KB)
// We removed CircularBuffer from internal Heap and use a raw array in PSRAM
// 256KB / sizeof(SerialMessage) = 262144 / 116 = 2259 entries
#define LOG_BUFFER_SIZE 2259
SerialMessage* psramLogBuffer = nullptr; // Dynamically allocated in setup()
size_t psramLogHead = 0;      // Next write position (ring buffer index)
size_t psramLogCount = 0;     // How many valid messages we hold

// We keep this definition for compatibility if someone still references it,
// but it will not be actively used for storage.
// serialMonitorBuffer now declared as CircularBuffer<uint8_t, 8192> above (line ~2936) 

// Wrapper helper for PSRAM logging
void pushLogToPsram(const SerialMessage& msg) {
    if (!psramLogBuffer) return; // If not allocated, ignore
    
    // HOOK: Persistent debug writer (if enabled)
    debugWriterLog(msg);
    
    // Write to the Head position
    psramLogBuffer[psramLogHead] = msg;
    
    // Update Head
    psramLogHead = (psramLogHead + 1) % LOG_BUFFER_SIZE;
    
    // Update Count (saturates at SIZE)
    if (psramLogCount < LOG_BUFFER_SIZE) {
        psramLogCount++;
    }
}

// NOTE: line sampling for web monitor removed (UTF-8 Sample)


// CharsetMode: 0 = UTF-8 Passthrough (for Serial/Telnet), 1 = Latin-1 Output, 2 = CP850 Output.
int charsetMode = 0; 

// Startup time for uptime calculations.
unsigned long bootTime = 0;

// Printer emulation mode.
int printerEmulationMode = PRINTER_MODE_GENERIC; // Starts in generic mode.

bool escActive = false; // Whether an escape sequence is active.
bool escStarActive = false; // Whether an escape sequence with '*' is active.
bool expectingCharsetCode = false; // Whether we are expecting a charset code.
int  escStarStage = 0; // Stage of the '*' escape sequence.
int  escStarMode  = 0; // Mode of the '*' escape sequence.
int  escStarCount = 0; // Count for the '*' escape sequence.
// POINT 2: Image buffer in PSRAM to relieve internal heap
std::vector<uint8_t, PsramAllocatorStd<uint8_t>> escImageData; // Vector to store image data (PSRAM).

unsigned long lastReceptionTime = 0; // Timestamp of the last printer data reception.
// Timestamp of last RS232 TX and grace window to avoid immediate inactivity trigger
unsigned long rs232LastTxMillis = 0;            // Last RS232 TX
unsigned long rs232InactivityGraceUntil = 0;    // Do not trigger inactivity before this time

// NTP periodic sync tracking
unsigned long lastNtpSyncMillis = 0; // ?ltimo tempo em que configTime()/atualizarNTPTimezone() foi chamado

int bufferDadosLineCount = 0; // Line count in the data buffer.

// MAX_LINES_6_LPI and MAX_LINES_8_LPI defined as constexpr in commands.h
const int ISQ20_EMAIL_LINES = 90; // Special case: line budget per email when ISQ20 is active
int currentMaxEmailLines = MAX_LINES_6_LPI; // Current line limit for email output.
// New: line budget to use specifically when the trigger comes from RS232
// (no flow control case); historically we used 66 — now we want 40.
const int RS232_DEFAULT_EMAIL_LINES = 90; // Default line budget per email when sent via RS232

// Flow origin flags
bool lastByteFromDb25 = false;           // Marks whether the last processed byte came from DB25 (Nano)
bool lastTriggerFromDb25 = false;        // Marks whether the last email trigger (lines/buffer) originated from DB25
// CRLF state for DB25: avoid double-counting when CR is followed by LF
bool db25SkipNextLF = false;             // After seeing a DB25 CR, ignore the immediately following LF

const int AVERAGE_LINE_LENGTH_BYTES = 80; // Average length of a line in bytes.

String serialCommandInput = ""; // Serial command input buffer.
bool serialCommandComplete = false; // Whether a serial command was fully received.
unsigned long lastSerialReadTime = 0; // Time of the last serial read.

// Placeholders for compatibility with removed excerpts (sampling)
unsigned long lastSampleTimer = 0; // previously used for periodic sample task (now inactive)
String currentReceivedLine = ""; // line being received (kept for compatibility)
unsigned long lastTransmissionActivityMillis = 0; // timestamp of the last transmission activity

// Flag to control whether the last Serial output ended with a newline.
static bool lastSerialOutputWasNewline = true; 

// Buffer for suppressed system messages.
// DISABLED: CircularBuffer<String> without a strict lock can crash (LoadProhibited @ 0x107).
// Root cause: rlogiacco/CircularBuffer library is not thread-safe with complex types
// Temporary workaround: disable message suppression entirely until proper fix
// CircularBuffer<String, 8> suppressedSystemMessagesBuffer; // ? DISABLED - causes LoadProhibited crashes

// Buffer + helpers for direct Serial logs suppression while data is being received.
// The queued messages are flushed ~500ms after reception stops.
const unsigned long DIRECT_LOG_FLUSH_DELAY_MS = 500; // Delay after reception stops before flushing
PsramQueue<String> deferredDirectSerialLogs(8); // PSRAM queue (8 slots, saves ~1KB DRAM)
static unsigned long lastDirectLogsEnqueueMillis = 0; // Last enqueue timestamp

// Data-activity check for direct-log suppression.
bool isDataActiveForDirectLogs() {
    unsigned long now = millis();
    bool bufferHasData = true;
    {
        bool empty = false;
        // Non-blocking: if we cannot safely read the buffer, assume active.
        if (bufferDadosTryIsEmpty(empty, 0)) bufferHasData = !empty;
    }
    return (now - lastReceptionTime) < DIRECT_LOG_FLUSH_DELAY_MS || bufferHasData || isReceivingData;
}

// Enqueue into a circular buffer with drop-oldest policy.
static void pushDeferredDirectLog(const String &line) {
    if (deferredDirectLogsMutex) {
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        if (deferredDirectLogsMutexOwnerTask == self) {
            deferredDirectLogsMutexDepth++;
        } else {
            // Use TakeRecursive for recursive mutex
            if (xSemaphoreTakeRecursive(deferredDirectLogsMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
                deferredDirectLogsMutexFailCount = deferredDirectLogsMutexFailCount + 1;
                return;
            }
            deferredDirectLogsMutexOwnerTask = self;
            deferredDirectLogsMutexDepth = 1;
        }
    }
    if (deferredDirectSerialLogs.isFull()) {
        // Avoid overflow: drop the oldest entry.
        (void)deferredDirectSerialLogs.shift(); // PsramQueue uses shift() not pop()
    }
    deferredDirectSerialLogs.push(line);
    lastDirectLogsEnqueueMillis = millis();
    if (deferredDirectLogsMutex) {
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        if (deferredDirectLogsMutexOwnerTask != self || deferredDirectLogsMutexDepth == 0) {
            deferredDirectLogsMutexBadUnlockCount = deferredDirectLogsMutexBadUnlockCount + 1;
            return;
        }
        deferredDirectLogsMutexDepth--;
        if (deferredDirectLogsMutexDepth == 0) {
            deferredDirectLogsMutexOwnerTask = NULL;
            // Use GiveRecursive for recursive mutex
            xSemaphoreGiveRecursive(deferredDirectLogsMutex);
        }
    }
}

// Mirror direct logs to Telnet when a client is authenticated.
#if ENABLE_TELNET==1
static inline bool isDirectLogTelnetMirrorReady() {
    return telnetClient && telnetClient.connected() && telnetAuthenticated;
}

static inline void mirrorDirectLogToTelnet(const __FlashStringHelper *msg, bool newline) {
    if (!isDirectLogTelnetMirrorReady()) return;
    if (newline) {
        telnetClient.println(msg);
    } else {
        telnetClient.print(msg);
    }
}

static inline void mirrorDirectLogToTelnet(const char *msg, bool newline) {
    if (!isDirectLogTelnetMirrorReady()) return;
    if (newline) {
        telnetClient.println(msg);
    } else {
        telnetClient.print(msg);
    }
}

static inline void mirrorDirectLogToTelnet(const String &msg, bool newline) {
    if (!isDirectLogTelnetMirrorReady()) return;
    if (newline) {
        telnetClient.println(msg);
    } else {
        telnetClient.print(msg);
    }
}
#else
static inline void mirrorDirectLogToTelnet(const __FlashStringHelper *msg, bool newline) { (void)msg; (void)newline; }
static inline void mirrorDirectLogToTelnet(const char *msg, bool newline) { (void)msg; (void)newline; }
static inline void mirrorDirectLogToTelnet(const String &msg, bool newline) { (void)msg; (void)newline; }
#endif

// Direct log wrappers that honor suppression.
void directLogPrint(const __FlashStringHelper *msg) {
    if (isDataActiveForDirectLogs()) {
        String s; s.reserve(32); s += msg;
        pushDeferredDirectLog(s);
    } else {
        Serial.print(msg);
        mirrorDirectLogToTelnet(msg, false);
    }
}

void directLogPrint(const char *msg) {
    if (isDataActiveForDirectLogs()) {
        pushDeferredDirectLog(String(msg));
    } else {
        Serial.print(msg);
        mirrorDirectLogToTelnet(msg, false);
    }
}

void directLogPrint(const String &msg) {
    if (isDataActiveForDirectLogs()) {
        pushDeferredDirectLog(msg);
    } else {
        Serial.print(msg);
        mirrorDirectLogToTelnet(msg, false);
    }
}

void directLogPrintln(const __FlashStringHelper *msg) {
    if (isDataActiveForDirectLogs()) {
        String s; s.reserve(34); s += msg; s += '\n';
        pushDeferredDirectLog(s);
    } else {
        Serial.println(msg);
        mirrorDirectLogToTelnet(msg, true);
    }
}

void directLogPrintln(const char *msg) {
    if (isDataActiveForDirectLogs()) {
        String s(msg); s += '\n';
        pushDeferredDirectLog(s);
    } else {
        Serial.println(msg);
        mirrorDirectLogToTelnet(msg, true);
    }
}

void directLogPrintln(const String &msg) {
    if (isDataActiveForDirectLogs()) {
        String s(msg); s += '\n';
        pushDeferredDirectLog(s);
    } else {
        Serial.println(msg);
        mirrorDirectLogToTelnet(msg, true);
    }
}

// Flush deferred direct logs when the system is idle enough.
static void flushDeferredDirectLogsIfIdle() {
    // Criteria: no activity and >= DIRECT_LOG_FLUSH_DELAY_MS since last reception.
    unsigned long now = millis();
    bool bufferEmpty = true;
    {
        bool empty = false;
        // If we cannot safely check, do not flush yet.
        if (!bufferDadosTryIsEmpty(empty, 0)) return;
        bufferEmpty = empty;
    }

    bool idleForFlush = !isReceivingData && bufferEmpty && (now - lastReceptionTime) >= DIRECT_LOG_FLUSH_DELAY_MS;
    if (!idleForFlush) return;

    if (deferredDirectLogsMutex) {
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        if (deferredDirectLogsMutexOwnerTask == self) {
            deferredDirectLogsMutexDepth++;
        } else {
            // Use TakeRecursive for recursive mutex
            if (xSemaphoreTakeRecursive(deferredDirectLogsMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
                deferredDirectLogsMutexFailCount = deferredDirectLogsMutexFailCount + 1;
                return;
            }
            deferredDirectLogsMutexOwnerTask = self;
            deferredDirectLogsMutexDepth = 1;
        }
    }
    if (deferredDirectSerialLogs.isEmpty()) {
        if (deferredDirectLogsMutex) {
            TaskHandle_t self = xTaskGetCurrentTaskHandle();
            if (deferredDirectLogsMutexOwnerTask != self || deferredDirectLogsMutexDepth == 0) {
                deferredDirectLogsMutexBadUnlockCount = deferredDirectLogsMutexBadUnlockCount + 1;
                return;
            }
            deferredDirectLogsMutexDepth--;
            if (deferredDirectLogsMutexDepth == 0) {
                deferredDirectLogsMutexOwnerTask = NULL;
                // Use GiveRecursive for recursive mutex
                xSemaphoreGiveRecursive(deferredDirectLogsMutex);
            }
        }
        return;
    }

    // Emit queued lines without building a single large String (saves heap).
    while (!deferredDirectSerialLogs.isEmpty()) {
        String line = deferredDirectSerialLogs.first();
        deferredDirectSerialLogs.shift();
        Serial.print(line);
        mirrorDirectLogToTelnet(line, false);
    }

    if (deferredDirectLogsMutex) {
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        if (deferredDirectLogsMutexOwnerTask != self || deferredDirectLogsMutexDepth == 0) {
            deferredDirectLogsMutexBadUnlockCount = deferredDirectLogsMutexBadUnlockCount + 1;
            return;
        }
        deferredDirectLogsMutexDepth--;
        if (deferredDirectLogsMutexDepth == 0) {
            deferredDirectLogsMutexOwnerTask = NULL;
            // Use GiveRecursive for recursive mutex
            xSemaphoreGiveRecursive(deferredDirectLogsMutex);
        }
    }
}
// Inactivity time to consider the data transmission finished for purposes of
// Suppress system messages: wait 1000ms (1s) since last byte received
// Requirement: system messages should be freed when >1s without new bytes in bufferRx
const unsigned long DATA_INACTIVITY_FOR_SUPPRESSION_MS = 1000; 

// (autoEmailTriggeredByNewline moved to the top)

// Inject line into bufferDados with prefix and optional timestamp (used by RS232 line accumulator)
static void snifferInjectLine(const char* line, size_t len) {
    // Check if timestamps should be included (applies to web buffer visualization)
    bool includeTimestamp = configManager.getEmailIncludeTimestamp();
    
    // Generate timestamp if enabled and NTP is synced
    char tsBuf[24] = ""; // YYYY-MM-DD HH:MM:SS format
    if (includeTimestamp) {
        time_t nowTs = time(nullptr);
        if (nowTs > 100000) { // NTP synchronized
            struct tm timeinfo;
            localtime_r(&nowTs, &timeinfo);
            snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
    }
    
    // Push timestamp to buffer if available
    if (tsBuf[0] != '\0') {
        for (const char* p = tsBuf; *p; ++p) { (void)bufferDadosSafePush((uint8_t)*p); }
        (void)bufferDadosSafePush('\t'); // Tab separator between timestamp and data
    }
    
    // Push raw line data to buffer
    for (size_t i = 0; i < len; ++i) { (void)bufferDadosSafePush((uint8_t)line[i]); }
    (void)bufferDadosSafePush('\r');
    (void)bufferDadosSafePush('\n');
    bufferDadosLineCount++;
    if (bufferDadosLineCount >= currentMaxEmailLines) { autoEmailTriggeredByNewline = true; }
    lastDataReceivedForInactivityAlert = millis(); // Update for inactivity alert (NEW data from Arduino Bridge)
    inactivityAlertSent = false; // Reset flag to allow new alerts after activity resumes
    for (int _i = 0; _i < ConfigManager::ALERT_SLOT_COUNT; ++_i) inactivityAlertSentSlot[_i] = false;

    // DIAGNOSTIC: Log inactivity timestamp update (rate-limited to avoid spam)
    static unsigned long lastInactivityUpdateLogBridge = 0;
    if (millis() - lastInactivityUpdateLogBridge > 15000) { // Log every 15s for better visibility
        char logBuf[100];
        snprintf(logBuf, sizeof(logBuf), "✅ [INACTIVITY] Timer RESET by Arduino Bridge data (ts=%lu)", lastDataReceivedForInactivityAlert);
        sendVirtualMessage(String(logBuf), INFO);
        lastInactivityUpdateLogBridge = millis();
    }
}

#if ENABLE_SNIFFER==1
// Sniffer callback: inject the line into the global buffer with optional timestamps
static void snifferInjectLine(uint8_t channel, const char* line, size_t len) {
    (void)channel; // Suppress unused parameter warning
    
    // Check if timestamps should be included (applies to web buffer visualization)
    bool includeTimestamp = configManager.getEmailIncludeTimestamp();
    
    // Generate timestamp if enabled and NTP is synced
    char tsBuf[24] = ""; // YYYY-MM-DD HH:MM:SS format
    if (includeTimestamp) {
        time_t nowTs = time(nullptr);
        if (nowTs > 100000) { // NTP synchronized
            struct tm timeinfo;
            localtime_r(&nowTs, &timeinfo);
            snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
    }
    
    // Push timestamp to buffer if available
    if (tsBuf[0] != '\0') {
        for (const char* p = tsBuf; *p; ++p) { (void)bufferDadosSafePush((uint8_t)*p); }
        (void)bufferDadosSafePush('\t'); // Tab separator between timestamp and data
    }
    
    // Push raw line data to buffer
    for (size_t i = 0; i < len; ++i) { (void)bufferDadosSafePush((uint8_t)line[i]); }
    (void)bufferDadosSafePush('\r');
    (void)bufferDadosSafePush('\n');
    bufferDadosLineCount++;
    if (bufferDadosLineCount >= currentMaxEmailLines) { autoEmailTriggeredByNewline = true; }

    // Queue for the 30-second LittleFS periodic flush (sniffer mode only)
    snifferQueueForFlush(channel, line, len);
}
#endif
// (shouldSendEmailImmediately and formFeedTriggered moved to the top)


bool emulatorShouldBeBusy = false; // Whether the emulator should be in BUSY state.
bool holdBusyDuringEmail = false;  // Hold BUSY for the entire email cycle (DB25 only)

// Timestamp of last reception activity (DB25 or RS232)
unsigned long lastReceiveActivityMillis = 0;

// CHANGED: Reduced to 2KB to save RAM (sufficient for output streaming)
PsramCircularBuffer<uint8_t> printerOutputBuffer(2048, true);

unsigned long lastPrinterDataTime = 0; // Time of last printer data.
const unsigned long SYSTEM_MESSAGE_INACTIVITY_TIMEOUT_MS = 500; // Time to show messages after printer inactivity.

// --------------------- Idle heap maintenance ---------------------
// Objective: perform safe actions during idle periods to
// reduce heap fragmentation and close inactive clients (SMTP/Telnet).
// This routine is conservative: it only closes open clients and tries to allocate
// a large block and free it to coalesce free blocks if there is enough memory.
// It does not restart or turn off WiFi.
const unsigned long IDLE_HEAP_MAINTENANCE_INTERVAL_MS = 30000UL; // Run at most every 30s
// Trigger: only start maintenance when free heap falls below this threshold
const size_t IDLE_HEAP_TRIGGER_BELOW = 130UL * 1024UL; // 130 KB
const unsigned long IDLE_HEAP_MARGIN_RESERVE = 4 * 1024UL;         // Leave 4KB margin
static unsigned long _lastIdleHeapMaintenance = 0;
// Adaptive mode: if heap drops further, allow maintenance with a shorter interval
const unsigned long IDLE_HEAP_EMERGENCY_INTERVAL_MS = 5000UL; // Up to every 5s in emergency
const size_t IDLE_HEAP_EMERGENCY_TRIGGER_BELOW = 90UL * 1024UL; // 90 KB
// Only close Telnet client automatically if inactive for this period
const unsigned long TELNET_IDLE_CLOSE_MS = 180000UL; // 180s

// Safe maintenance/defrag function: close inactive clients and try to
// allocate+free a large block to improve allocator coalescence.

// Heap baseline metrics (for diagnostics and alerts)
static size_t heapBaselineAtBoot = 0;        // Free heap just after boot
static size_t heapBaselineAfterWiFi = 0;     // Free heap after Wi-Fi connects
static size_t heapMinimumObserved = SIZE_MAX; // Minimum heap observed so far
static bool heapBaselinesCaptured = false;   // Flag: baselines captured
static unsigned long heapLastDegradationWarning = 0; // Last time we alerted about heap degradation
const unsigned long HEAP_DEGRADATION_WARNING_INTERVAL = 300000UL; // Alert at most once every 5 minutes

static void idleHeapMaintenanceIfIdle(unsigned long now) {
    // Basic criteria: do not interfere if we are receiving data or sending email
    if (isReceivingData) return;
    {
        bool empty = false;
        if (!bufferDadosTryIsEmpty(empty, 0)) return;
        if (!empty) return;
    }
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    if (isEmailSendingActive) return; // do not touch while sending emails
#endif

    // Limit frequency adaptively: if heap is very low, use an emergency interval
    size_t freeHeap = ESP.getFreeHeap();
    unsigned long intervalToUse = IDLE_HEAP_MAINTENANCE_INTERVAL_MS;
    if (freeHeap < IDLE_HEAP_EMERGENCY_TRIGGER_BELOW) intervalToUse = IDLE_HEAP_EMERGENCY_INTERVAL_MS;
    if (now - _lastIdleHeapMaintenance < intervalToUse) return;
    _lastIdleHeapMaintenance = now;

    // Update observed minimum heap
    if (freeHeap < heapMinimumObserved) {
        heapMinimumObserved = freeHeap;
    }

    // Check for significant degradation (alert only if baselines captured)
    if (heapBaselinesCaptured && heapBaselineAfterWiFi > 0) {
        float degradationPercent = ((float)(heapBaselineAfterWiFi - freeHeap) / (float)heapBaselineAfterWiFi) * 100.0f;
        // If we lost more than 40% of the heap baseline AND enough time has passed since the last alert
        if (degradationPercent > 40.0f && (now - heapLastDegradationWarning) > HEAP_DEGRADATION_WARNING_INTERVAL) {
            heapLastDegradationWarning = now;
            sendVirtualMessage(buildMessage("HEAP_DEGRAD: %.1f%% lost (baseline: %u KB, current: %u KB, min: %u KB)",
                                              degradationPercent,
                                              (unsigned)(heapBaselineAfterWiFi / 1024),
                                              (unsigned)(freeHeap / 1024),
                                              (unsigned)(heapMinimumObserved / 1024)), WARN);
            // Show alert on OLED as temporary display
            #if ENABLE_LCD_SCREEN==1
            screenManager.showHeapAlert();
            #endif
        }
    }

    // Only start maintenance if heap is below configured threshold
    if (freeHeap >= IDLE_HEAP_TRIGGER_BELOW) {
        // Outside the threshold that requires maintenance
        return;
    }

#if ENABLE_RS232==1
    // Do not run if there are temporary RS232 parameters pending in RAM
    // Exception: if the template in RAM equals the one persisted in NVS
    if (rs232TemplateDirty) {
        // Get the name and persisted fields
        String storedName = configManager.getRs232CurrentTemplate();
        bool sameAsStored = false;
        // If the name matches, also compare the relevant fields in memory
        if (storedName.length() && (storedName == lastAppliedRs232TemplateName)) {
            if (
                rs232_baudrate_index == configManager.getRs232BaudrateIndex() &&
                rs232_data_bits == configManager.getRs232DataBits() &&
                rs232_parity == configManager.getRs232Parity() &&
                rs232_stop_bits == configManager.getRs232StopBits() &&
                rs232_invert_pins == configManager.getRs232InvertPins() &&
                rs232_read_timeout_ms == configManager.getRs232ReadTimeoutMs() &&
                rs232_template_use_xon_xoff == configManager.getRs232UseXonXoff()
            ) {
                sameAsStored = true;
            }
        }

        if (!sameAsStored) {
            sendVirtualMessage(String(F("HEAP_MAINT: RS232 templates pending; deferred.")), DEBUG);
            return;
        } else {
            // Template in RAM matches NVS — safe to allow maintenance.
            sendVirtualMessage(String(F("HEAP_MAINT: RS232 template in RAM equals NVS; maintenance allowed.")), DEBUG);
            // Note: we do not change the rs232TemplateDirty flag here; the deferred persistence routine
            // will keep handling any needed write later.
        }
    }
#endif // ENABLE_RS232

sendVirtualMessage(String(F("HEAP_MAINT: Starting heap maintenance. Free=")) + String(freeHeap), DEBUG);

    // Before closing clients, revalidate criteria to ensure nothing started happening
    {
        bool empty = false;
        if (!bufferDadosTryIsEmpty(empty, 0)) {
            sendVirtualMessage(String(F("HEAP_MAINT: Buffer lock busy; aborting.")), DEBUG);
            return;
        }
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
        if (isReceivingData || !empty || isEmailSendingActive) {
        sendVirtualMessage(String(F("HEAP_MAINT: Activity detected during revalidation; maintenance aborted.")), DEBUG);
#else
        if (isReceivingData || !empty) {
        sendVirtualMessage(String(F("HEAP_MAINT: Activity detected during revalidation; maintenance aborted.")), DEBUG);
#endif
        return;
        }
    }

    // ? Fecha clientes de rede que possam manter buffers alocados
    // ? Fecha SMTP se aberto
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    smtpTeardown();
#endif
    // ? Fecha Telnet SE e SOMENTE SE estiver inactivo h? mais de TELNET_IDLE_CLOSE_MS
    #ifdef TELNET_CLIENT_VARIABLE_EXISTS
    if (telnetClient && telnetClient.connected()) {
        unsigned long idle = millis() - lastTelnetActivityMillis;
        if (idle > TELNET_IDLE_CLOSE_MS) {
            telnetClient.stop();
            sendVirtualMessage(String(F("HEAP_MAINT: Telnet client closed by inactivity.")), DEBUG);
        } else {
            sendVirtualMessage(String(F("HEAP_MAINT: Telnet active recently (")) + String(idle) + String(F(" ms)); not closed.")), DEBUG);
        }
    }
    #else
    // Generic fallback: if a global telnetClient variable exists, the preprocessor does nothing
    #endif

    // Conservative "defrag" technique: allocate a large block progressively and allow abort
    size_t trySize = freeHeap > IDLE_HEAP_MARGIN_RESERVE ? (freeHeap - IDLE_HEAP_MARGIN_RESERVE) / 2 : 0;
    const size_t MAX_TRY = 64UL * 1024UL; // 64KB max to try
    if (trySize > MAX_TRY) trySize = MAX_TRY;

    // Allocate in small (chunks) pieces to allow rechecks and abort
    const size_t CHUNK = 4UL * 1024UL; // 4KB por chunk
    if (trySize >= 512) {
        size_t remaining = trySize;
        bool aborted = false;
        void **blocks = (void**)malloc(((trySize + CHUNK - 1) / CHUNK) * sizeof(void*));
        if (!blocks) {
            // if we cannot allocate the pointer array, try a single block as fallback
            void *blk = malloc(trySize);
            if (blk) {
                memset(blk, 0, trySize);
                free(blk);
                sendVirtualMessage(String(F("HEAP_MAINT: Temporary fallback allocation of ")) + String(trySize) + String(F(" bytes done and released.")), DEBUG);
            } else {
                sendVirtualMessage(String(F("HEAP_MAINT: Failed to allocate block of ")) + String(trySize) + String(F(" bytes (ignored).")), DEBUG);
            }
        } else {
            size_t idx = 0;
            while (remaining > 0) {
                size_t thisChunk = remaining > CHUNK ? CHUNK : remaining;
                // Before each chunk, check whether activity started
                {
                    bool empty = false;
                    if (!bufferDadosTryIsEmpty(empty, 0)) {
                        sendVirtualMessage(String(F("HEAP_MAINT: Buffer lock busy; aborting.")), DEBUG);
                        aborted = true;
                        break;
                    }
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
                    if (isReceivingData || !empty || isEmailSendingActive) {
                    sendVirtualMessage(String(F("HEAP_MAINT: Activity detected during allocation; aborting maintenance.")), DEBUG);
#else
                    if (isReceivingData || !empty) {
                    sendVirtualMessage(String(F("HEAP_MAINT: Activity detected during allocation; aborting maintenance.")), DEBUG);
#endif
                    aborted = true;
                    break;
                    }
                }

                void *blk = malloc(thisChunk);
                if (!blk) {
                    // chunk failure - log and abort
                    sendVirtualMessage(String(F("HEAP_MAINT: Failed to allocate chunk of ")) + String(thisChunk) + String(F(" bytes; aborting.")), DEBUG);
                    aborted = true;
                    break;
                }
                // tocar a mem?ria
                memset(blk, 0, thisChunk);
                blocks[idx++] = blk;
                remaining -= thisChunk;
                // small yield to allow interrupts and stack/heap housekeeping
                delay(1);
                yield();
            }

            // free all allocated blocks
            for (size_t i = 0; i < idx; ++i) {
                free(blocks[i]);
            }
            free(blocks);

            if (!aborted) {
                sendVirtualMessage(String(F("HEAP_MAINT: Temporary allocation of ")) + String(trySize) + String(F(" bytes done in chunks and released.")), DEBUG);
            }
        }
    } else {
        sendVirtualMessage(String(F("HEAP_MAINT: Calculated size too small (")) + String(trySize) + String(F("). Ignored.")), DEBUG);
    }

    // Give the system time to clean pending TLS/Network stack resources
    delay(10);
    yield();

    sendVirtualMessage(String(F("HEAP_MAINT: End of maintenance. Free=")) + String(ESP.getFreeHeap()), DEBUG);
}

// Capture heap baseline (called after boot and after WiFi connects)
// Allows tracking of degradation and proactive alerts
static void captureHeapBaseline(const char* stage) {
    size_t currentHeap = ESP.getFreeHeap();
    
    if (strcmp(stage, "boot") == 0) {
        heapBaselineAtBoot = currentHeap;
        heapMinimumObserved = currentHeap;  // ? Inicializar com valor boot
        sendVirtualMessage(buildMessage("HEAP_BASELINE [boot]: %u KB",
                                          (unsigned)(heapBaselineAtBoot / 1024)), INFO);
    } else if (strcmp(stage, "wifi") == 0) {
        heapBaselineAfterWiFi = currentHeap;
        heapBaselinesCaptured = true;  // Baselines ready for comparison
        sendVirtualMessage(buildMessage("HEAP_BASELINE [WiFi]: %u KB (boot: %u KB, loss: %d KB)",
                                          (unsigned)(heapBaselineAfterWiFi / 1024),
                                          (unsigned)(heapBaselineAtBoot / 1024),
                                          (int)((heapBaselineAtBoot - heapBaselineAfterWiFi) / 1024)), INFO);
    }
}


static String lastLoggedSystemMessageContent = ""; // Last logged system message content
static unsigned long lastLoggedSystemMessageTime = 0; // Time of the last logged system message

static unsigned long lastBusyLogTime = 0; // Time of the last BUSY log
const unsigned long BUSY_LOG_INTERVAL = 15000; // Intervalo de log de BUSY (reduzido spam)

bool pauseInternalBufferTransfer = false; // Pause internal buffer transfer

volatile unsigned int busyBytesCounter = 0; // Counter of bytes in BUSY state
volatile bool isBusyCounting = false; // Whether counting bytes in BUSY state.

int autoEmailEnabledInactivityTimeout = 10000; // Timeout when auto-email active (ms).
int autoEmailDisabledInactivityTimeout = 300000; // Timeout when auto-email inactive (ms).

unsigned long autoBufferClearInactivityTimeout = 0; // Timeout to clear the buffer automatically.

static bool bufferClearedOnInactivity = true; // ? Flag para indicar se o buffer foi limpo por inatividade.

const unsigned long CRITICAL_EVENT_RATE_LIMIT_MS = 60000; // Rate limit for critical events

static String lastSentWifiStatusInfo = ""; // Last Wi-Fi status info sent

// Define the Wi-Fi connection states.
enum WifiConnectionState {
    WIFI_IDLE, // Idle.
    WIFI_CONNECTING, // Connecting.
    WIFI_CONNECTED, // ? Conectado.
    WIFI_FAILED // ? Falhou.
};
WifiConnectionState currentWifiConnectionState = WIFI_IDLE; // Current Wi-Fi connection state.
unsigned long wifiConnectStartTime = 0; // Wi-Fi connection start time.
const unsigned long WIFI_CONNECT_TIMEOUT = 30000; // Wi-Fi connection timeout.
const unsigned long WIFI_RETRY_INTERVAL = 5000; // Wi-Fi retry interval.
unsigned long lastWifiAttemptTime = 0; // Time of last Wi-Fi connection attempt.

// 📊 WiFi reconnection counter (incremented on each WiFi.begin() after disconnect)
unsigned long wifiReconnectCount = 0;

// ===== JOB N?O BLOQUEANTE DE CONEX?O WIFI (evita travar UI) =====
volatile bool wifiConnectJobPending = false; // Sinaliza job ativo
int wifiConnectJobPhase = 0;                 // 0=prep,1=begin,2=aguarda,3=finalizado
unsigned long wifiConnectJobStart = 0;       // Mark start
String wifiJobSsid = "";                    // SSID alvo
String wifiJobPass = "";                    // Password alvo

bool enableDebugLogs = false; // Disabled for better performance - was true.

// Startup state management to avoid memory deadlocks.
enum StartupState {
    STARTUP_TELNET_PENDING, // ? Telnet pendente.
    STARTUP_WIFI_STATUS_PENDING, // ? Status do Wi-Fi pendente.
    STARTUP_WEBSERVER_PENDING, // ? Servidor web pendente.
    STARTUP_COMPLETE // Initialization complete.
};
StartupState currentStartupState = STARTUP_TELNET_PENDING; // Current startup state.
unsigned long lastStartupAction = 0; // Last startup action
bool startupMessagesShown = false; // Whether startup messages have been shown
const unsigned long STARTUP_DELAY_MS = 500; // 500ms delay between startup phases
const unsigned long WEBSERVER_DELAY_MS = 5000; // 5 second delay before starting the web server

static String ultimoConteudoPreparado; // Last content prepared.

// UTF-8 decoder state variables moved to src/charset_conversion.cpp

// Static variables to control debug logging for manageEmailSending
static int lastLoggedEmailState = -1; // Last logged email state
static bool lastLoggedTriggerAttempt = false; // Last logged trigger attempt

static bool stateMachineHoldsSmtpMutex = false; // Indicates whether the automatic state machine holds the SMTP mutex
static bool stateMachineHoldsPriorityEmail = false; // Whether the state machine holds the P3 priority lock (mutual exclusion with EMAILDAY)

// Cooldown window for inactivity trigger debounce
// Avoid spam of "Email triggered by: Inactivity" when discarding minimal tails or re-evaluating oftenpido
unsigned long lastInactivityTriggerAt = 0;              // Last time an inactivity trigger was accepted
const unsigned long INACTIVITY_TRIGGER_COOLDOWN_MS = 3000; // Minimum window between inactivity triggers

// Temporary suppression of DEBUG logs during email sending
// Goal: avoid building many temporary Strings for DEBUG logs while the send cycle runs
// We save the previous state to restore at the end; scope: from EMAIL_START_SENDING until returning to EMAIL_IDLE
bool emailLogSuppressionActive = false;          // Indicates if suppression is active
bool savedEnableDebugLogsBeforeEmail = false;    // Stores the value of enableDebugLogs before suppression

// Variables for manual email sending via command
String manualEmailContentBuffer = "";
bool manualEmailSendRequested = false;

// Overrides for one-shot "Email Test" button in the UI
// When active, they apply only to the next send cycle and are cleared at process end
// emailTestForceBodyOnlyOnce now declared at line 966 with other email test variables
String emailTestSubjectOverrideOnce = "";        // ? Substitui o assunto apenas para o pr?ximo email

// === UNIFIED CONFIGURATION ===
// Inst?ncia global do ConfigManager - substitui vari?veis dispersas.
ConfigManager configManager;

bool isChartsPageEnabledRuntime() {
    return configManager.getChartsPageEnabled();
}

// === ENVIRONMENTAL SENSOR ===
#if ENABLE_AHT20_BMP280==1
SensorAHT20_BMP280 environmentalSensor;

// Sensor configuration manager (web-based config + automatic readings)
// Global instance defined in sensor_config.cpp
#endif

// Function prototypes for auxiliary helpers (VERY IMPORTANT: KEEP THESE HERE!)
void setBusyPinState(bool state); // Define the BUSY pin state
String getEmailTriggerReason(EmailTriggerSource source); // Get the reason for the email trigger
bool hasNetworkConnectivity(); // Check if WiFi STA or Ethernet currently has a valid IP
String getCurrentNetworkIPString(); // Current preferred IP (WiFi first, Ethernet fallback)
bool isEmailConfiguredAndNetworkConnected(); // Check if email is configured and any active network is available
bool isEmailConfiguredAndWifiConnected(); // Check if email is configured and Wi-Fi connected
static String buildRecipientEmailSubject(const String &ruleName, const String &capturedValue); // Recipient email subject helper
#if ENABLE_SMTP && ENABLE_SDCARD
bool enviarEmailComAnexosSD(const String &folderPath, const String &subject, const String &bodyText, size_t maxBytesPerEmail, const String &shiftEmails = ""); // Send email with SD attachments (exception: always sends even with SD active)
#endif
// References to db25.h removed due to deprecated MCP path
void manageSerialOutput(); // Manage serial output
String convertAsciiDecimalString(const String &input); // Convert an ASCII decimal string
void manageWiFiConnection(); // Manage Wi-Fi connection
void saveAllConfigs(); // Save all configurations
void loadAllConfigs(); // Load all configurations
void syncGlobalVariables(); // NEW: Synchronize global variables with ConfigManager (includes RS232 by default).
void syncGlobalVariables(bool includeRS232); // Overload: allows ignoring RS232 on light reapply
SpiRamJsonDocument get_all_configs(); // NEW: Prototype for function to get unified JSON (renamed).
void enviarTodasConfiguracoesParaUI(); // Sends all configurations to the user interface.
void sendWifiStatusAndRssi(LogLevel level, unsigned long currentMillis); // Send Wi‑Fi status and RSSI.
bool checkTcpIpHealth(); // NEW: Checks TCP/IP stack health.
void manageEmailSending(); // Manages email sending.
// Amostragem de transmiss?o removida: formerly exibirAmostraTransmissao
void processLine(const String &line, unsigned long currentMillis); // Processes a command line.
void clearPrinterDataBufferAndNotifyUI(); // New prototype to clear the printer data buffer and notify UI.
// Removed legacy forward declarations processScheduledCommands/processScheduledDateTimeCommands (now replaced by slots)
#if ENABLE_SCHEDULER==1
bool shouldExecuteCommand(const ScheduledCommand &cmd, unsigned long currentTime); // Check whether a command should execute.
#endif
bool enviarComandoRS232(const String &comando); // New function to send a command via RS232.
void applyRS232Config(); // Apply RS232 configs immediately (baud/framing/pins)
// Sampler removed
void pushSampleLineToWebMonitor(const String &line);

unsigned long getIntervalInMillis(int interval, const String &unit); // Convert interval to milliseconds.

// Global wrapper for HelpEmitFn (avoids lambda captures and matches the typedef signature)
// Intention: allow passing a function pointer (HelpEmitFn) to help routines
// Reuses sendVirtualMessage with COMMAND_RESPONSE level to standardize output
static void helpEmitWrapper(const String &line) { sendVirtualMessage(line, COMMAND_RESPONSE); }

static bool onboardLedState = false;

bool isOnboardLedSupported() {
#if defined(RGB_BUILTIN) || defined(LED_BUILTIN)
    return true;
#else
    return false;
#endif
}

bool isOnboardLedOn() {
    return onboardLedState;
}

bool setOnboardLedState(bool on) {
#if defined(RGB_BUILTIN)
    pinMode(RGB_BUILTIN, OUTPUT);
    const uint8_t rgbPin = static_cast<uint8_t>(RGB_BUILTIN);
    if (on) {
        rgbLedWrite(rgbPin, 16, 16, 16);
    } else {
        rgbLedWrite(rgbPin, 0, 0, 0);
    }
    onboardLedState = on;
    return true;
#elif defined(LED_BUILTIN)
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
    onboardLedState = on;
    return true;
#else
    (void)on;
    onboardLedState = false;
    return false;
#endif
}

// Flags to track whether the header has been written (avoid duplication)
static int sdcardLineCount_MTF1 = 0; // MTF1 line counter to reinsert header every 20 lines
static int sdcardLineCount_AWSE = 0; // AWSE line counter to reinsert header every 20 lines

// Helper function: Detect if a line is a header that should be discarded
// Checks for common header patterns that may be sent by Amada equipment
// Can be enabled/disabled via configuration (default: enabled for safety)
bool isHeaderLine(const String &line) {
    // Check if filtering is disabled in configuration
    if (!configManager.getAmadaFilterHeaders()) return false; // Filtering disabled - accept all lines
    
    if (line.length() == 0) return true; // Empty lines are not data
    
    // Skip common separators and markers
    if (line.startsWith("---")) return true;
    if (line.startsWith("===")) return true;
    if (line.startsWith("___")) return true;
    
    // Skip lines that start with "Timestamp" (our generated headers)
    if (line.startsWith("Timestamp")) return true;
    
    // Skip lines that start with field names (potential headers from Amada)
    // Check if line starts with known field names followed by tab/space/delimiter
    if (line.startsWith("Prg\t") || line.startsWith("Prg ") || 
        (line.startsWith("Prg") && line.indexOf("Is1") > 0)) return true;
    
    // Skip lines containing multiple field names in sequence (header signature)
    // If we find 3+ known field names in a single line with delimiters, it's likely a header
    int fieldMatches = 0;
    if (line.indexOf("Prg") >= 0) fieldMatches++;
    if (line.indexOf("Is1_kA") >= 0 || line.indexOf("Is1") >= 0) fieldMatches++;
    if (line.indexOf("Us1_V") >= 0 || line.indexOf("Us1") >= 0) fieldMatches++;
    if (line.indexOf("Ps1_kW") >= 0 || line.indexOf("Ps1") >= 0) fieldMatches++;
    if (line.indexOf("tw1_ms") >= 0 || line.indexOf("tw1") >= 0) fieldMatches++;
    if (line.indexOf("Count") >= 0) fieldMatches++;
    if (line.indexOf("Axis") >= 0) fieldMatches++;
    
    // If 3 or more field names are present, consider it a header
    if (fieldMatches >= 3) return true;
    
    return false;
}

// Helper function: Convert raw email content to ISQ20 tabular format with headers
// Returns empty String if not ISQ20 data or processing fails
// Used to populate emailTablesDataCache for attachments
static String convertToISQ20TabularFormat(const String &rawContent, bool isLPT) {
    if (rawContent.length() == 0) return String();
    
    // Detect ISQ20 format by scanning content
    int mtf1Count = 0, awseCount = 0;
    size_t scanPos = 0;
    int linesScanned = 0;
    
    while (scanPos < rawContent.length() && linesScanned < 100) {
        int nextEol = rawContent.indexOf('\n', scanPos);
        if (nextEol == -1) nextEol = rawContent.length();
        String line = rawContent.substring(scanPos, nextEol);
        line.trim();
        if (line.length() > 0) {
            if (looksLikeISQ20_MTF1(line)) mtf1Count++;
            if (looksLikeISQ20_AWSE(line)) awseCount++;
            linesScanned++;
        }
        if (nextEol >= (int)rawContent.length()) break;
        scanPos = nextEol + 1;
    }
    
    // Decide which format to use
    int threshold = (linesScanned < 5) ? 0 : 2;
    bool useMtf1 = (configManager.getAmadaISQ20MTF1Enabled() || (configManager.getStructuresAutoDetectEnabled() && mtf1Count > (awseCount + threshold)));
    bool useAwse = !useMtf1 && (configManager.getAmadaISQ20AWSEEnabled() || (configManager.getStructuresAutoDetectEnabled() && awseCount > 0));
    
    if (!useMtf1 && !useAwse) {
        return String(); // ? Not ISQ20 data
    }
    
    // Always use tabular format (CSV format option removed)
    bool useRecipientFormat = false;
    
     // Determine timestamp string for this batch if enabled
    String timestampStr = "";
    if (!isLPT && configManager.getEmailIncludeTimestamp()) {
        time_t now = time(nullptr);
        if (now > 100000) { // Valid time (not 1970)
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char buf[16];
            strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
            timestampStr = String(buf);
        }
    }

    // Build output according to format preference
    String output;
    output.reserve(rawContent.length() * 2); // Reserve space for formatting overhead
    
    if (!useRecipientFormat) {
        // Tabular format with headers every 20 lines
        // Add initial header
        if (useMtf1) {
            output += buildISQ20_MTF1_TabularHeader();
            output += "\n";
        } else {
            output += buildISQ20_AWSE_TabularHeader();
            output += "\n";
        }
    }
    
    // Process each line
    int lineCount = 0;
    scanPos = 0;
    while (scanPos < rawContent.length()) {
        int nextEol = rawContent.indexOf('\n', scanPos);
        if (nextEol == -1) nextEol = rawContent.length();
        String line = rawContent.substring(scanPos, nextEol);
        line.trim();
        
        // Skip header lines that may come from Amada equipment
        if (!isHeaderLine(line)) {
            String rowData;
            bool validRow = false;
            
            if (useRecipientFormat) {
                // CSV format: key, value, key, value (no headers)
                if (useMtf1) {
                    validRow = buildISQ20_MTF1_CSVRow(line, rowData);
                } else {
                    validRow = buildISQ20_AWSE_CSVRow(line, rowData);
                }
            } else {
                // Tabular format: tab-separated with periodic headers
                if (useMtf1) {
                    validRow = buildISQ20_MTF1_TabularRow(line, rowData, timestampStr);
                } else {
                    validRow = buildISQ20_AWSE_TabularRow(line, rowData, timestampStr);
                }
            }
            
            if (validRow) {
                // Insert header every 20 lines (only in tabular mode)
                int _hdrEvery = configManager.getAmadaHeaderEvery();
                if (!useRecipientFormat && lineCount > 0 && _hdrEvery > 0 && lineCount % _hdrEvery == 0) {
                    if (useMtf1) {
                        output += buildISQ20_MTF1_TabularHeader();
                    } else {
                        output += buildISQ20_AWSE_TabularHeader();
                    }
                    output += "\n";
                }
                
                output += rowData;
                output += "\n";
                lineCount++;
            }
        }
        
        if (nextEol >= (int)rawContent.length()) break;
        scanPos = nextEol + 1;
    }
    
    return output;
}

// ? Estrutura para rastrear job de email pendente (email via SD streaming)
struct PendingEmailJob {
    String filepath;          // Full path of the file on SD
    size_t start_offset;      // Byte offset of section start
    size_t end_offset;        // Byte offset of section end
    String timestamp_start;   // Timestamp of reception start
    String source;            // Source: "RS232" or "LPT"
    bool processed;           // ? J? processado?
};

// Global queue of pending jobs (maximum 10 concurrent emails)
// Circular FIFO behavior: when full, remove the oldest and add the new one
// Ensure the most recent data is always processed (new over old)
static std::vector<PendingEmailJob> emailJobQueue;
static const size_t MAX_EMAIL_JOBS = 10; // 10 concurrent pending emails

// ============================================================================
// RAM EMAIL QUEUE (recipient matching without SD card)
// ============================================================================
// When SD is inactive/unmounted, recipient matching still creates separate emails.
// Structure to track RAM-based email jobs (data stored in buffer, not SD).
struct PendingRamEmailJob {
    String lineContent;       // Line content (copy from buffer)
    String recipients;        // Recipients matched in this line
    String matchedRules;      // "|" separated rule names that matched (not "," — avoids collision with CSV key,threshold format)
    String capturedValue;     // Numeric value that triggered the rule (e.g. "018.79")
    String timestamp;         // Reception timestamp
    String source;            // Source: "RS232" or "LPT"
    bool processed;           // Already processed?
};

// Global queue for RAM-based email jobs (separate from SD queue)
static std::vector<PendingRamEmailJob> ramEmailJobQueue;
static const size_t MAX_RAM_EMAIL_JOBS = 20; // Allow more RAM jobs (lighterweight)
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
extern SemaphoreHandle_t ramEmailQueueMutex;
extern SemaphoreHandle_t emailQueueMutex;
#endif

// Callback invoked by sdcard.cpp when RS232 data is written to SD
// Checks whether to send email via SD streaming (only if the feature flag is active)
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
void onSdcardDataWritten(const String &filepath, size_t startOffset, size_t endOffset, const String &source) {
    // 🔍 DEBUG: Always log callback entry (INFO level - critical for debugging)
    sendVirtualMessage(buildMessage("📥 CALLBACK: file=%s, range=[%u-%u] (%u bytes), source=%s", 
                                       filepath.c_str(), (unsigned)startOffset, (unsigned)endOffset, 
                                       (unsigned)(endOffset - startOffset), source.c_str()), INFO);
    
    // Check if feature is enabled (SD UI flag) and if source is RS232 or LPT
    if (!configManager.getSdcardEmailSend()) {
        sendVirtualMessage(F("❌ Email SD: Feature DISABLED (sdcardEmailSend=false)"), INFO);
        return;
    }
    if (!source.equalsIgnoreCase("RS232") && !source.equalsIgnoreCase("LPT")) {
        sendVirtualMessage(buildMessage("❌ Email SD: Invalid source '%s' (not RS232/LPT)", source.c_str()), INFO);
        return; // Feature OFF or source is not RS232/LPT — fallback to traditional method
    }
    
    // Ignore very small jobs (e.g., newline or fragments)
    if (endOffset <= startOffset || (endOffset - startOffset) < 10) {
        sendVirtualMessage(buildMessage("⚠️ Email SD: Job ignored (size=%u bytes < 10)", 
                                          (unsigned)(endOffset - startOffset)), INFO);
        return;
    }

    sendVirtualMessage(F("✅ Email SD: Feature ACTIVE, starting recipient scan..."), INFO);

    // NEW: Verify if there are recipients BEFORE enqueuing
    // Avoid filling the queue with "junk" (data with no associated email)
    bool hasRecipients = false;
    File file = SD.open(filepath.c_str());
    if (file) {
        if (file.seek(startOffset)) {
            // Ler linha a linha at? encontrar um destinat?rio ou acabar o chunk
            String lineBuffer = "";
            lineBuffer.reserve(256);
            size_t curPos = startOffset;
            
            while (curPos < endOffset && !hasRecipients) {
                int c = file.read();
                if (c < 0) break;
                curPos++;
                
                if (c == '\n') {
                    String lineLower = lineBuffer;
                    lineLower.toLowerCase();
                    // Skip file header and empty lines to avoid false positives.
                    // The header "--- Node32-HUB v2.21 - 2025-09-15 ..." contains "-09" which
                    // is parsed as col2 = -9.0 and would match any colN <= 25.0 rule.
                    if (lineLower.length() > 0 && !lineLower.startsWith("---")) {
                        std::vector<std::pair<String, String>> found;
                        findRecipientsInLowerText(lineLower, found, true); // sdActive=true
                        if (!found.empty()) {
                            hasRecipients = true;
                        }
                    }
                    lineBuffer = "";
                } else if (c != '\r') {
                    if (lineBuffer.length() < 1024) lineBuffer += (char)c;
                }
            }
            // Check last line if not finished yet
            if (!hasRecipients && lineBuffer.length() > 0) {
                String lineLower = lineBuffer;
                lineLower.toLowerCase();
                // Same header guard: skip "---" lines in trailing (no-newline) check.
                if (!lineLower.startsWith("---")) {
                    std::vector<std::pair<String, String>> found;
                    findRecipientsInLowerText(lineLower, found, true);
                    if (!found.empty()) {
                        hasRecipients = true;
                        sendVirtualMessage(buildMessage("🎯 Email SD: Recipients FOUND in line: '%s'", 
                                                           lineBuffer.c_str()), DEBUG);
                    }
                }
            }
        }
        file.close();
    }

    if (!hasRecipients) {
        sendVirtualMessage(buildMessage("❌ Email SD: Job IGNORED (no recipients matched in %u bytes)", 
                                          (unsigned)(endOffset - startOffset)), INFO);
        return;
    }
    
    sendVirtualMessage(F("✅ Email SD: Recipients found, adding job to queue..."), INFO);
    // Check if job limit has been exceeded (protected by mutex)
    if (emailQueueMutex && xSemaphoreTakeRecursive(emailQueueMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        sendVirtualMessage(F("⚠️ Email SD: mutex timeout, ignoring job"), WARN);
        return;
    }
    
    // FIFO circular: if full (10 jobs), remove the oldest unprocessed job
    if (emailJobQueue.size() >= MAX_EMAIL_JOBS) {
        // Find first unprocessed job
        bool removed = false;
        for (auto it = emailJobQueue.begin(); it != emailJobQueue.end(); ++it) {
            if (!it->processed) {
                sendVirtualMessage(buildMessage("Queue full: removing oldest unprocessed job (%s)", 
                                                  it->timestamp_start.c_str()), WARN);
                emailJobQueue.erase(it);
                removed = true;
                break;
            }
        }
        
        // If all processed, remove the first (oldest)
        if (!removed && !emailJobQueue.empty()) {
            sendVirtualMessage(buildMessage("Queue full: removing processed job (%s)", 
                                              emailJobQueue[0].timestamp_start.c_str()), DEBUG);
            emailJobQueue.erase(emailJobQueue.begin());
        }
    }
    
    // Current timestamp (for identification)
    char timestamp[32];
    time_t now = time(nullptr);
    struct tm info;
    localtime_r(&now, &info);
    snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d", info.tm_hour, info.tm_min, info.tm_sec);
    
    // Create pending job
    PendingEmailJob job;
    job.filepath = filepath;
    job.start_offset = startOffset;
    job.end_offset = endOffset;
    job.timestamp_start = String(timestamp);
    job.source = source;
    job.processed = false;
    
    emailJobQueue.push_back(job);
    
    if (emailQueueMutex) xSemaphoreGiveRecursive(emailQueueMutex); // Release mutex
    
    sendVirtualMessage(buildMessage("✅ Email SD: JOB ADDED to queue [%u jobs]: %s [%u-%u] (%u bytes)",
                                      (unsigned)emailJobQueue.size(),
                                      filepath.c_str(), (unsigned)startOffset, (unsigned)endOffset,
                                      (unsigned)(endOffset - startOffset)), INFO);
}
#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL

// Send email with part of the SD file (direct SMTP streaming, autonomous)
// Uses smtpSendEmailStreamingSDPartial with its own TLS connection; does not accumulate MIME in RAM
#if ENABLE_SMTP==1
bool sendEmailPartialFileSd(const String &filePath, size_t startOffset, size_t endOffset,
                            const String &recipients, const String &subject, const String &bodyText) {
    // Basic checks
    if (!isEmailConfiguredAndWifiConnected()) {
        sendVirtualMessage(F("Email SD: invalid config or WiFi OFF"), ERROR);
        return false;
    }
    
    if (endOffset <= startOffset) {
        sendVirtualMessage(F("Email SD: invalid offsets"), ERROR);
        return false;
    }
    
    size_t dataSize = endOffset - startOffset;
    
    // Prepare SMTP config
    String server = configManager.getSmtpServer();
    int port = configManager.getSmtpPort();
    String user = configManager.getSmtpUsername();
    String pass = configManager.getSmtpPassword();
    bool tlsInsecure = configManager.getSmtpTlsInsecure();
    
    sendVirtualMessage(buildMessage("Email SD STREAM: %s [%u-%u] (%u bytes)", 
                                      filePath.c_str(), (unsigned)startOffset, (unsigned)endOffset, (unsigned)dataSize), INFO);
    
    vTaskDelay(pdMS_TO_TICKS(50)); // Yield for watchdog
    
    // AUTONOMOUS DIRECT STREAMING: connect and send only the matching lines
    // Instead of sending the raw SD block, create a temporary file with lines that match the rules
    String tmpPath = filePath + String(".emailtmp");
    File src = SD.open(filePath.c_str());
    if (!src) {
        sendVirtualMessage(String(F("❌ Email SD: failed to open source file: ")) + filePath, ERROR);
        return false;
    }
    if (!src.seek(startOffset)) {
        src.close();
        sendVirtualMessage(F("❌ Email SD: failed to seek in source file"), ERROR);
        return false;
    }

    // Open a temp file to write the filtered lines
    File tmp = SD.open(tmpPath.c_str(), FILE_WRITE);
    if (!tmp) {
        src.close();
        sendVirtualMessage(F("❌ Email SD: failed to create temporary file"), ERROR);
        return false;
    }

    // Helpers for local matching (case-insensitive)
    auto lowerCopy = [&](const String &s) {
        String out = s;
        out.toLowerCase();
        return out;
    };
    auto lineContainsToken = [&](const String &lineLower, const String &tokenLower) -> bool {
        // Word-boundary token match (simple): token must appear as substring separated by non-word chars
        size_t pos = 0; size_t L = tokenLower.length();
        if (L == 0) return false;
        while ((int)(pos = lineLower.indexOf(tokenLower, pos)) != -1) {
            bool leftOk = (pos == 0) || !((lineLower.charAt(pos-1) >= 'a' && lineLower.charAt(pos-1) <= 'z') || (lineLower.charAt(pos-1) >= '0' && lineLower.charAt(pos-1) <= '9') || lineLower.charAt(pos-1) == '_');
            bool rightOk = (pos + L >= lineLower.length()) || !((lineLower.charAt(pos+L) >= 'a' && lineLower.charAt(pos+L) <= 'z') || (lineLower.charAt(pos+L) >= '0' && lineLower.charAt(pos+L) <= '9') || lineLower.charAt(pos+L) == '_');
            if (leftOk && rightOk) return true;
            pos += 1;
        }
        return false;
    };

    // L? linha a linha dentro do intervalo e escreve no tmp se corresponder a alguma regra
    const size_t LINE_BUF = 1024;
    char linebuf[LINE_BUF]; size_t linepos = 0;
    size_t curPos = startOffset;
    bool hasAny = false;

    while (curPos < endOffset) {
        int c = src.read();
        if (c < 0) break; // EOF
        curPos++;
        if (c == '\r') continue; // ignorar CR, esperar LF
        if (c == '\n' || curPos >= endOffset) {
            // Termina linha
            linebuf[linepos] = '\0';
            String line = String(linebuf);
            line.trim();
            if (line.length() > 0) {
                String lineLower = lowerCopy(line);
                std::vector<std::pair<String,String>> tmpFound;
                findRecipientsInLowerText(lineLower, tmpFound, configManager.getSdcardEnabled());
                if (!tmpFound.empty()) {
                    // Write line to the temp file
                    tmp.print(line.c_str());
                    tmp.print('\n');
                    hasAny = true;
                }
            }
            // Reset buffer
            linepos = 0;
            continue;
        }
        // adicionar char se espa?o
        if (linepos < LINE_BUF - 1) linebuf[linepos++] = (char)c;
    }

    src.close();
    tmp.flush();
    tmp.close();

    bool success = false;

    // Keep the inline email body compact: the contextual header is enough here.
    // Matched raw lines are no longer appended to avoid legacy formatting in alerts.
    String finalBody = bodyText;
    if (hasAny) {
        SD.remove(tmpPath.c_str());
    }
    // Send plain-text email (no attachment), with emailBusy protection
    {
        unsigned long _lkStart = millis();
        while (emailBusy) {
            if (millis() - _lkStart > 10000) {
                sendVirtualMessage(F("Email SD inline: busy timeout"), WARN);
                return false;
            }
            delay(50);
        }
        emailBusy    = true;
        isEmailSending = true;
        success = smtpSendEmail(user, recipients, subject, finalBody, server, port, user, pass, tlsInsecure, false);
        isEmailSending = false;
        emailBusy    = false;
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // Yield for watchdog
    
    if (success) {
        sendVirtualMessage(F("✅ Email SD STREAM sent successfully!"), INFO);
    } else {
        sendVirtualMessage(F("❌ Email SD STREAM: failed to send"), ERROR);
    }
    
    return success;
}
#endif // ENABLE_SMTP

// ============================================================================
// Panic context — saved to NVS before abort(), read back on next boot
// ============================================================================
#include <Preferences.h>

static const char* PANIC_NVS_NS = "panic";
static const char* PANIC_NVS_KEY = "ctx";

struct PanicContext {
    uint32_t magic;
    uint32_t freeHeap;
    uint32_t maxAllocHeap;
    uint32_t uptimeMs;
    uint32_t pressureLevel;
    char msg[64];
};

static constexpr uint32_t PANIC_MAGIC = 0x504E4331; // "PNC1"

// The function pointer called by memoryPressurePanic() before abort.
void (*gMemoryPressurePanicHook)(const char* msg) = nullptr;

static void panicContextSave(const char* msg) {
    Preferences prefs;
    if (!prefs.begin(PANIC_NVS_NS, false)) return;
    PanicContext ctx;
    ctx.magic        = PANIC_MAGIC;
    ctx.freeHeap     = (uint32_t)ESP.getFreeHeap();
    ctx.maxAllocHeap = (uint32_t)ESP.getMaxAllocHeap();
    ctx.uptimeMs     = millis();
    ctx.pressureLevel = (uint32_t)memoryPressureCurrentLevel();
    size_t len = strlen(msg);
    if (len >= sizeof(ctx.msg)) len = sizeof(ctx.msg) - 1;
    memcpy(ctx.msg, msg, len);
    ctx.msg[len] = '\0';
    prefs.putBytes(PANIC_NVS_KEY, &ctx, sizeof(ctx));
    prefs.end();
    Serial.println("*** Panic context saved to NVS ***");
}

// Returns true and fills ctx if valid panic context exists.
// Clears the NVS key after reading (one-shot).
static bool panicContextLoad(PanicContext& ctx) {
    Preferences prefs;
    if (!prefs.begin(PANIC_NVS_NS, true)) return false;
    size_t sz = prefs.getBytesLength(PANIC_NVS_KEY);
    if (sz != sizeof(PanicContext)) { prefs.end(); return false; }
    prefs.getBytes(PANIC_NVS_KEY, &ctx, sizeof(ctx));
    prefs.end();
    if (ctx.magic != PANIC_MAGIC) return false;
    prefs.begin(PANIC_NVS_NS, false);
    prefs.remove(PANIC_NVS_KEY);
    prefs.end();
    return true;
}

static void panicContextReport() {
    PanicContext ctx;
    if (!panicContextLoad(ctx)) return;
    char line[256];
    snprintf(line, sizeof(line),
             "*** PREVIOUS PANIC: %s | heap=%u max_alloc=%u uptime=%lums pressure=%u ***",
             ctx.msg,
             (unsigned)ctx.freeHeap,
             (unsigned)ctx.maxAllocHeap,
             (unsigned long)ctx.uptimeMs,
             (unsigned)ctx.pressureLevel);
    Serial.println(line);
    logSystemEvent(String(F("PANIC_REPORT")), String(line));
}

static void panicHookInit() {
    gMemoryPressurePanicHook = panicContextSave;
}