/**
 * commands.h — System command handler
 *
 * Processes system commands received via Serial/Telnet/Web.
 * Kept as a separate header to reduce chatgpt.ino size.
 *
 * Available commands:
 * - REBOOT: restarts the ESP32
 * - EMAILDAY: emails the current day's SD logs (streaming)
 * - DATE/TIME/GET_DATETIME: prints current date/time
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>
#include "feature_flags.h"
#include "structures.h"
#include "email_message_builder.h"
#include "runtime_environment.h"
#include "sensors/sensor_config.h"
#include "telnet_transport.h"
#include <WiFi.h>

extern void safeReboot(const char* reason); // Forward declaration

// sdcard.h is always included: it provides stubs when ENABLE_SDCARD=0.
#include "sdcard.h"
#include "log.h" // LogLevel enum

#if ENABLE_SDCARD==1
#include "debug_writer.h"  // Debug writer functions (debugWriterFlush)
extern bool flushSdcardNowBlocking(const char *reason, unsigned long waitMs);
extern bool flushSdcardCutoffNowBlocking(const char *reason, unsigned long waitMs);
extern bool sdcardPrepareForEmaildayBlocking(unsigned long waitMs);
extern bool forceSwapSdcardBuffersForEmailday(unsigned long waitMs); // Force buffer swap for EMAILDAY (no freeze)
#else
// When ENABLE_SDCARD==0, stub implementations are provided in runtime_storage.cpp.
// Declare as extern to match runtime_storage.h and avoid linkage conflicts.
extern bool flushSdcardNowBlocking(const char* reason, unsigned long waitMs);
extern bool flushSdcardCutoffNowBlocking(const char* reason, unsigned long waitMs);
extern bool sdcardPrepareForEmaildayBlocking(unsigned long waitMs);
extern bool forceSwapSdcardBuffersForEmailday(unsigned long waitMs);
extern bool safeFlushBeforeCriticalCommand(const String& commandName);
#endif

// Storage abstraction functions (unified SD/PSRAM access) - enum StorageMode defined in structures.h
extern volatile StorageMode currentStorageMode;
// 📦 External dependencies (declared in chatgpt.ino)

extern void sendVirtualMessage(const String &msg, int msgType, bool alreadyFormatted, bool bypassDedup);
extern String timezone;
extern String ntpServer;

// 🔧 Run I2C scan wrapper (implementation in `screen.cpp`)
extern void runI2CScan();

#if ENABLE_SDCARD==1
extern bool sdcardIsMounted();
extern String sdcardGetTodayFolder();
#endif

#if ENABLE_TELNET==1
extern UnifiedTelnetClient telnetClient;
extern bool telnetAuthenticated;
#endif

// 📦 Extra dependencies for configuration commands
extern int currentMaxEmailLines;
extern int charsetMode;
extern int printerEmulationMode;
extern bool enableDebugLogs;
#if ENABLE_RS232==1
extern bool rs232_enabled;
extern bool rs232_invert_pins;
extern int rs232_baudrate_index;
extern int rs232_baudrate;
extern int rs232_rx_pin;
extern int rs232_tx_pin;
#else
#ifndef RS232_STUBS_DEFINED
#define RS232_STUBS_DEFINED
static const bool rs232_enabled = false;
static const bool rs232_invert_pins = false;
static const int rs232_baudrate_index = 0;
static const int rs232_baudrate = 9600;
static const int rs232_rx_pin = 16;
static const int rs232_tx_pin = 17;
#endif // RS232_STUBS_DEFINED
#endif // ENABLE_RS232
extern void saveAllConfigs();
extern void enviarTodasConfiguracoesParaUI();
extern bool setOnboardLedState(bool on);
extern bool isOnboardLedSupported();
extern bool isOnboardLedOn();

#if ENABLE_RS232==1
extern HardwareSerial RS232;
extern void setRS232BaudrateByIndex(int index);
#endif

extern Preferences preferences;

#if ENABLE_MODBUS==1
#include "modbus_runtime.h"
#include "modbus_poll.h"
#include "modbus_core.h"
#include "modbus_tcp.h"
#include "modbus_slave.h"
#endif

// Compile-time constants shared between chatgpt.ino TU and command_executor.cpp
constexpr int MAX_LINES_6_LPI = 66;    // Maximum lines for 6 lines per inch
constexpr int MAX_LINES_8_LPI = 90;    // Maximum lines for 8 lines per inch
constexpr unsigned long REBOOT_COOLDOWN_MS = 3600UL * 1000UL; // Minimum interval between REBOOT commands (1 hour)

/**
 * Process a system command received via Serial/Telnet/Web.
 * Returns true if the command was handled, false if unrecognised.
 */
bool processSystemCommand(const String &commandLine, const String &upperCaseCommand);

// Execute a system command (wrapper for processSystemCommand).
void executeCommand(const String &command);

#endif // COMMANDS_H
