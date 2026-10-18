#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "ConfigManager.h"
#include "feature_flags.h"
#include "psram_helpers.h"
#include "psram_queue.h"
#include "runtime_environment.h"
#include "sdcard.h"
#include "structures.h"

extern bool isHeaderLine(const String &line);

#if ENABLE_ETHERNET==1
#include <Ethernet.h>
#endif

extern ConfigManager configManager;
extern PsramCircularBuffer<uint8_t> bufferDados;
extern volatile bool bufferDadosBusy;
extern portMUX_TYPE bufferDadosSpinlock;
extern volatile unsigned long bufferDadosSeq;
extern volatile StorageMode currentStorageMode;
extern volatile bool isReceivingData;
extern unsigned long lastReceptionTime;
extern const char* getStorageModeString();
extern SerialMessage* psramLogBuffer;
extern size_t psramLogHead;
extern size_t psramLogCount;
#if ENABLE_RS232==1
extern PsramQueue<String> rs232SendQueue;
extern bool rs232_enabled;
extern volatile bool rs232_paused_by_controller;
extern bool rs232_blockQueueForEmail;
extern int rs232_baudrate_index;
extern bool rs232_invert_pins;
extern int rs232_baudrate;
extern const int RS232_BAUDRATES[];
extern const int RS232_BAUDRATES_COUNT;
#else
#ifndef RS232_STUBS_DEFINED
#define RS232_STUBS_DEFINED
static const bool rs232_enabled = false;
static const bool rs232_invert_pins = false;
static const int rs232_baudrate_index = 0;
static const int rs232_baudrate = 9600;
#endif // RS232_STUBS_DEFINED
#endif // ENABLE_RS232
extern void processLine(const String& line, unsigned long currentMillis);
#if ENABLE_RS232==1
extern bool enqueueRS232Command(const String& command, const String& templateName, const String& scheduledId, const String& type);
#else
inline bool enqueueRS232Command(const String&, const String&, const String&, const String&) { return false; }
// Stub RS232 baudrate list when ENABLE_RS232==0
static const int RS232_BAUDRATES_COUNT = 0;
static const int RS232_BAUDRATES[1] = {9600};
#endif

static const size_t SHARED_WEB_LOG_BUFFER_SIZE = 2259U;

bool sharedWebBufferDadosLock(TickType_t timeoutTicks, bool& didLockOut);
void sharedWebBufferDadosUnlock(bool didLock);

struct SharedEthernetConfigInput {
    String mode;
    String ip;
    String gw;
    String dns;
    String mac;
};

struct SharedSdcardConfigInput {
    bool enabled = true;
    bool structured = true;
    bool emailSend = true;
    bool emailFromSd = true;
    String flushInterval = "30m";
    String format = "csv";
    int maxSizeMb = 4;
    String extraEmails;
    bool autoCleanup = true;
    int cleanupThreshold = 70;
    int keepDays = 0; // 0 = infinity (keep until the % threshold)
};

struct SharedUsbServerConfigInput {
    bool enabled = true;
    uint16_t port = 3240;
    uint16_t recvTimeoutMs = 1200;
    uint16_t inWaitTimeoutMs = 220;
};

String sharedWebJsonEscape(const String& input);
String stripControlChars(const String& input);
String normalizeEmailToken(const String& tokenRaw);
String normalizeEmailDestinationList(const String& rawList, bool& anyInvalid, bool& allInvalid);
bool sharedWebValidIPv4(const String& value);
bool sharedWebValidMac(String mac);
void sharedWebAppendUInt64(String& out, uint64_t value);
String sharedWebBuildEthernetStatusJson();
String sharedWebBuildUsbServerStatusJson();
String sharedWebBuildUsbServerConfigJson();
String sharedWebBuildUsbServerDevicesJson();
bool sharedWebApplyUsbServerConfig(const SharedUsbServerConfigInput& input, String& responseBody);
bool sharedWebApplyEthernetConfig(const SharedEthernetConfigInput& input, String& responseBody);
String sharedWebBuildSdStatusJson();
String sharedWebBuildSdcardConfigJson();
bool sharedWebApplySdcardConfig(const SharedSdcardConfigInput& input, String& responseBody);
String sharedWebApplySdAction(const String& action, bool& ok);
String sharedWebBuildMonitorStatusJson();
bool sharedWebBuildReceivedText(int limit, int maxBytesWindow, bool includePartial, String& output);
bool sharedWebClearReceivedBuffer(String& responseBody);
void sharedWebWriteJsonEscaped(Print& out, const char* input);
void sharedWebWriteJsonEscaped(Print& out, const String& input);
void sharedWebWriteSystemCommandResponse(Print& out, const String& command);
// Executes a system command, capturing the COMMAND_RESPONSE/INFO/ERROR output
// produced during the call. Returns the captured text (may be multi-line) or an
// empty String when the command produced no capturable output.
void beginSystemCommandCapture();
String endSystemCommandCapture();
String sharedRunSystemCommandCapture(const String& command);
#if ENABLE_RS232==1
void sharedWebWriteRs232CommandResponse(Print& out, const String& command, const String& templateName);
#else
inline void sharedWebWriteRs232CommandResponse(Print& out, const String&, const String&) {
    out.print(F("{\"success\":false,\"response\":\"RS232 desativado\"}"));
}
#endif
#if ENABLE_MODBUS==1
void sharedWebWriteModbusCommandResponse(Print& out, const String& command);
#else
inline void sharedWebWriteModbusCommandResponse(Print& out, const String&) {
    out.print(F("{\"success\":false,\"response\":\"Modbus desativado\"}"));
}
#endif
void sharedWebStreamSerialMonitorJson(
    Print& out,
    int limitMsgs,
    bool hasSince,
    size_t sinceValue,
    bool skipCmds,
    bool hasLastSeq,
    unsigned long lastSeqValue,
    int maxBytesWindow,
    int limitLines,
    bool debug
);