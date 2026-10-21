// chatgpt_includes.h
// Single-inclusion header that replaces the conditional include block in
// chatgpt.ino.  Keeps the .ino file lean and the include order easy to audit.
#pragma once

// ── Email trigger flags (defined here before smtp_core_impl.h needs them) ──
bool rs232MicroPauseArmed = false;
bool autoEmailTriggeredByNewline = false;
bool shouldSendEmailImmediately = false;
bool formFeedTriggered = false;
bool lastSentEmailWasLPT = false;
String lptEmailContentForSave = "";

// SMTP/NTP config globals (extern in commands.h, email_runtime_config_state.h)
String smtpServer;
String smtpPort;
String smtpUser;
String smtpPass;
String emailDest;
String ntpServer = "pool.ntp.org";
String timezone = "WET0WEST,M3.5.0/1,M10.5.0";

// Memory pressure guard state (extern in memory_pressure_guard.h)
MemoryPressureGuardState gMemoryPressureState = {};

// Recipient throttle mutex (unconditional — rs232_config_impl.h uses it)
SemaphoreHandle_t recipientThrottleMutex = NULL;

// RS232 send queue mutex (extern in runtime_rs232_queue.h)
SemaphoreHandle_t rs232SendQueueMutex = NULL;
volatile unsigned long rs232SendQueueMutexFailCount = 0;
TaskHandle_t rs232SendQueueMutexOwnerTask = NULL;
volatile uint32_t rs232SendQueueMutexBadUnlockCount = 0;
uint32_t rs232SendQueueMutexDepth = 0;

// RAM line accumulator for RS232 (web monitor display)
static char* ramLineAccumulator_RS232 = nullptr;
static size_t ramLineAccumulator_RS232_len = 0;
static unsigned long ramLineStartTimestamp_RS232 = 0;

// WiFi scan cache
struct CachedWiFiScanResult {
    String ssid;
    int32_t rssi;
    wifi_auth_mode_t encryption;
    CachedWiFiScanResult() : ssid(), rssi(0), encryption(WIFI_AUTH_OPEN) {}
};
bool wifiScanInProgress = false;
unsigned long lastScanTime = 0;
int cachedScanCount = 0;
CachedWiFiScanResult* wifiScanCache = nullptr;
const unsigned long WIFI_SCAN_CACHE_DURATION = 60000UL;
unsigned long lastFallbackScanAttempt = 0;
int lastTriedAltNetworkIndex = -1;

// Buffer protection globals needed by rx_buffers_impl.h AND diagnostics_impl.h
volatile bool bufferDadosBusy = false;
portMUX_TYPE bufferDadosSpinlock = portMUX_INITIALIZER_UNLOCKED;
volatile unsigned long bufferDadosMutexFailCount = 0;
volatile unsigned long bufferDadosPushMutexFailCount = 0;
volatile bool bufferDadosLPTBusy = false;
portMUX_TYPE bufferDadosLPTSpinlock = portMUX_INITIALIZER_UNLOCKED;
volatile unsigned long bufferDadosLPTMutexFailCount = 0;
volatile unsigned long bufferDadosLPTPushMutexFailCount = 0;

// bufferDados/bufferDadosLPT externs — defined in diagnostics_impl.h, but
// rx_buffers_impl.h references them so declare here first.
extern PsramCircularBuffer<uint8_t> bufferDados;
extern PsramCircularBuffer<uint8_t> bufferDadosLPT;

// ── rx_buffers_impl.h: buffer helper functions (needs buffer vars above, and
// must be before diagnostics_impl.h which uses these helpers) ────────────
#include "rx_buffers_impl.h"

// smtpLock/smtpUnlock stubs for non-SMTP builds — needed by smtp_core_impl.h
#if ENABLE_SMTP==0
bool smtpLock(TickType_t) { return false; }
void smtpUnlock() {}
#endif

// ── Core implementation headers (always included) ──────────────────────────
// smtp_core_impl.h declares shared globals: bufferRx, writePos, readPos,
// FIRMWARE_VERSION, isReceivingData, isPrinterBusy, etc. — needed by ALL
// downstream code even when SMTP/Google Email are disabled.
// Email-specific sections inside it are guarded by #if ENABLE_SMTP==1.
#include "smtp_core_impl.h"

// recipient_matching_impl.h must be BEFORE diagnostics_impl.h when email is
// enabled — diagnostics_impl.h references findRecipientsInLowerText inside
// #if ENABLE_SMTP==1 blocks.
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
#include "recipient_matching_impl.h"
#endif

// diagnostics_impl.h declares shared globals: printerOutputBuffer, plus
// email-related functions guarded by #if ENABLE_SMTP==1 inside the file.
#include "diagnostics_impl.h"

// ── Email-specific includes ────────────────────────────────────────────────
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
#include "email_queues_impl.h"
#endif

#if ENABLE_SDCARD==1
#include "data_sd_pipeline_impl.h"
#elif ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
// stub: logDataToSdcard called from manage_email_impl.h (SD+Email runtime path)
static void logDataToSdcard(const String&, const String&, unsigned long, bool) {}
// stub: pipeline excluded without SDCARD — chart rings unavailable,
// .chart APIs serve files only (same stub as non-email builds below).
static void chartRingsBegin() {}
#endif

#else
// Stubs for non-email packs: recipient_matching_impl.h and
// data_sd_pipeline_impl.h are not included (email-only files).
static void checkPresenceWatchdog() {}
void logRs232TestPayloadToSd() {}
// Stub for builds without data_sd_pipeline_impl.h: chart rings unavailable,
// .chart APIs serve files only.
static void chartRingsBegin() {}
// Stub for non-email packs (data_sd_pipeline_impl.h not included): Modbus
// poll log callback is a no-op here — poll values stay visible on
// /modbus-live via the RX ring buffer (snifferInjectLine).
static void addDataToSdcardDirectRaw(const char*, const char*, size_t, unsigned long) {}
bool emailProcessInitiatedThisCycle = false;
#if ENABLE_SDCARD==1
static void flushSdcardBuffers() {}
static size_t logDataToSdcardTimestamped(const String&, std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>&) { return 0; }
#endif

// SMTP stubs (ENABLE_SMTP==0) — smtpLock/smtpUnlock defined above
TaskHandle_t emailTestTaskHandle = NULL;
volatile bool emailTestInProgress = false;
volatile bool emailDayInProgress = false;
String emailDayCmdExtra = "";
SemaphoreHandle_t emailQueueMutex = NULL;
SemaphoreHandle_t ramEmailQueueMutex = NULL;
SemaphoreHandle_t emailQueueProcessorMutex = NULL;
void runtimeEmailInitBackgroundTasks() {}
void enqueueStreamingEmail(const String &, const String &) {}
void processEmailQueue() {}
size_t runtimeEmailGetStreamQueueCount() { return 0; }
unsigned long runtimeEmailGetLastStreamEmailSentTime() { return 0; }
bool startEmailDayAsync(const String &, const String &, const String &) { return false; }
#endif // ENABLE_SMTP || ENABLE_GOOGLE_EMAIL

// ── Remaining shared includes (always included) ────────────────────────────
#include "sensors/sensor_helpers_impl.h"
#include "heap_trace.h"
#include "setup_impl.h"
// rs232_config_impl.h is always included: applyRS232Config() contains the full
// boot sequence (config load, NTP, WiFi, screen, web server). RS232-specific
// sections inside that file are guarded with #if ENABLE_RS232==1.
#include "rs232_config_impl.h"

// Functions specific to each emulation mode
bool processEpsonCommand(uint8_t* data, int length);
bool processIBMCommand(uint8_t* data, int length);
void handlePrinterModeSpecificByte(uint8_t byte);

// Functions for charset handling - moved to src/charset_conversion.h
void handleDecodedCodepoint(uint32_t codepoint);
// Printer emulation functions moved to src/printer_emulation.h

void printUtf8(uint8_t c);
// handleStrobeInterrupt() (legacy MCP) removed during transition to Nano UART bridge
// Stubs for RS232 when RS232 is disabled in build
#if ENABLE_RS232==0
inline bool enviarComandoRS232(const String&) { return false; }
inline void sendRs232Xoff() {}
inline void sendRs232Xon() {}
inline void sendRs232XoffRaw() {}
inline void sendRs232XonRaw() {}
static volatile bool rs232_sending = false;
static bool rs232FirstResponseGuardActive = false;
static int rs232LineCount = 0;
static bool rs232ChunkPauseActive = false;
static bool rs232ForceLines66NextEmail = false;
static int rs232FirstResponseMinLines = 0;
#define RS232_FIRST_RESPONSE_MIN_LINES 0
static bool rs232HoldXonUntilEmailDone = false;
static volatile bool rs232_paused_by_controller = false;
static bool rs232LastWasCR = false;
static unsigned long rs232FirstResponseGuardUntil = 0;
static volatile bool rs232RemoteSendsXonXoff = false;
static volatile unsigned long rs232RemoteXonCount = 0;
static volatile unsigned long rs232RemoteXoffCount = 0;
static volatile unsigned long rs232LastRemoteXonMs = 0;
static volatile unsigned long rs232LastRemoteXoffMs = 0;
static int rs232_read_timeout_ms = 800;
static unsigned long rs232PauseStartMillis = 0;
static bool rs232_blockQueueForEmail = false;
static inline bool isFlowControlEngaged() { return false; }
static inline bool rs232QueueHasPending(unsigned* = nullptr) { return false; }
#endif

#include "wifi_config_impl.h"

#include "loop_impl.h"

#include "logger_impl.h"

// printf-style log helper (declared in coordinator_hooks.h, defined here after sendVirtualMessage is available)
void safeEnviarMensagemVirtual(const char *fmt, int level, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, level);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sendVirtualMessage(String(buf), level, false, false);
}

#include "config_sync_impl.h"

#if ENABLE_SMTP==1
#include "manage_email_impl.h"
#else
void manageEmailSending() {}
#endif

#include "process_line_impl.h"
#include "rs232_misc_impl.h"
