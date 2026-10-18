#include "ipg_laser_module.h"

#include "feature_flags.h"
#if ENABLE_IPG_LASER == 1

#include <ctype.h>
#include <string.h>

#include "runtime_rs232_queue.h"
#include "feature_flags.h"

namespace {

static const uint16_t IPG_LASER_CAPTURE_WINDOW_MS = 2500;
static const uint8_t IPG_LASER_MAX_LOG_ENTRIES = 24;
static const uint8_t IPG_LASER_MAX_LINE_LENGTH = 159;

struct InternalLogEntry {
    uint32_t timestampMs;
    char direction[4];
    char text[160];
};

static const IpgLaserCommandInfo kCommandCatalog[] = {
    {"ABN", "Guide", "ABN", "Enable the aiming beam.", "ABN", "Fails if external guide control is enabled."},
    {"ABF", "Guide", "ABF", "Disable the aiming beam.", "ABF", "Fails if external guide control is enabled."},
    {"DEABC", "Guide", "DEABC", "Disable external aiming beam control.", "DEABC", "Required to control guide beam via RS232."},
    {"DEC", "Control", "DEC", "Disable external analog current control.", "DEC", "Also disables dynamic scaling in waveform mode."},
    {"DGM", "Control", "DGM", "Disable gate mode.", "DGM", "Rejected while emission is on."},
    {"DLE", "Control", "DLE", "Disable hardware emission control.", "DLE", "Rejected while emission is on."},
    {"DMOD", "Control", "DMOD", "Disable modulation mode.", "DMOD", "Rejected while emission is on."},
    {"DPM", "Control", "DPM", "Disable pulse mode.", "DPM", "QCW models only."},
    {"EEABC", "Guide", "EEABC", "Enable external aiming beam control.", "EEABC", "Allows hardware guide control input."},
    {"EEC", "Control", "EEC", "Enable external analog current control.", "EEC", "Rejected while emission is on."},
    {"EGM", "Control", "EGM", "Enable gate mode.", "EGM", "Uses external gate input to trigger internal pulses."},
    {"ELE", "Control", "ELE", "Enable hardware emission control.", "ELE", "Rejected while emission is on."},
    {"EMOD", "Control", "EMOD", "Enable modulation mode.", "EMOD", "Rejected while emission is on."},
    {"EMOFF", "Emission", "EMOFF", "Stop laser emission.", "EMOFF", "Immediate stop command."},
    {"EMON", "Emission", "EMON", "Start laser emission.", "EMON", "Requires valid setpoint and remote-ready state."},
    {"EPM", "Control", "EPM", "Enable pulse mode.", "EPM", "QCW models only."},
    {"ESTA", "Readback", "ESTA", "Read extended device status words for diagnostics.", "ESTA: 256;0;...", "Used mainly for remote troubleshooting."},
    {"HELP", "Utility", "HELP [CMD]", "List commands or show details for one command.", "HELP: ...", "Commands are case-insensitive."},
    {"LFP", "Front Panel", "LFP", "Lock the touch-screen front panel.", "LFP", "Touch-screen option only."},
    {"RBAUD", "Readback", "RBAUD", "Read the active RS232 baud-rate index.", "RBAUD: 8", "Index 8 maps to 57600 bps."},
    {"RCE", "Service", "RCE <code>", "Clear a critical error using an IPG service code.", "RCE: Code Accepted", "Service use only."},
    {"RCS", "Readback", "RCS", "Read current setpoint in percent.", "RCS: 56.7", "Percent of maximum diode current."},
    {"RCT", "Readback", "RCT", "Read internal laser temperature in Celsius.", "RCT: 34.5", "Useful for health monitoring."},
    {"REC", "Readback", "REC", "Read the critical error counter.", "REC: 37", "Persistent count."},
    {"RERR", "Service", "RERR", "Reset resettable errors.", "RERR", "Equivalent to hardware error reset."},
    {"RET", "Readback", "RET", "Read elapsed on-time in minutes.", "RET: 1105", "Cumulative runtime."},
    {"RFV", "Readback", "RFV", "Read software revision.", "RFV: 7.28;2.83;ND", "Firmware versions separated by semicolons."},
    {"RMEC", "Readback", "RMEC", "Read stored module error code.", "RMEC: 0", "Zero means normal operation."},
    {"RNC", "Readback", "RNC", "Read minimum allowed current setpoint.", "RNC: 10.0", "Percent of maximum current."},
    {"ROP", "Readback", "ROP", "Read average output power in watts.", "ROP: 99.6", "May also return Off or Low."},
    {"RPP", "Readback", "RPP", "Read peak output power in watts.", "RPP: 730", "May also return Off or Low."},
    {"RPRR", "Readback", "RPRR", "Read pulse repetition rate in Hz.", "RPRR: 10.00", "Pulse mode only."},
    {"RPW", "Readback", "RPW", "Read pulse width in ms.", "RPW: 5.550", "Pulse mode only."},
    {"RSN", "Readback", "RSN", "Read device serial number.", "RSN: 6103081", "Useful for inventory and hostname defaults."},
    {"SBAUD", "Network/Serial", "SBAUD <index>", "Set RS232 baud-rate index.", "SBAUD: 9", "Via RS232 there is no reply because speed changes immediately."},
    {"SDC", "Setpoint", "SDC <percent>", "Set diode current in percent.", "SDC: 34.2", "Rejected if out of range or external control is enabled."},
    {"SPRR", "Setpoint", "SPRR <hz>", "Set pulse repetition rate.", "SPRR: 10", "Can fail with duty cycle too high."},
    {"SPW", "Setpoint", "SPW <ms>", "Set pulse width.", "SPW: 5.5", "Can fail with argument out of range or duty cycle too high."},
    {"STA", "Readback", "STA", "Read 32-bit status word.", "STA: 4100", "Bitfield reports emission, modes and alarms."},
    {"UFP", "Front Panel", "UFP", "Unlock the touch-screen front panel.", "UFP", "Touch-screen option only; fails in REMOTE with key restrictions."},
    {"DDHCP", "Ethernet", "DDHCP", "Disable DHCP.", "DDHCP", "Rejected while emission is on."},
    {"EDHCP", "Ethernet", "EDHCP", "Enable DHCP.", "EDHCP", "Rejected while emission is on."},
    {"RDGW", "Ethernet", "RDGW", "Read default gateway.", "RDGW: 192.168.1.1", "Dot-decimal format."},
    {"RDHCP", "Ethernet", "RDHCP", "Read DHCP status.", "RDHCP: OFF", "Returns ON or OFF."},
    {"RIP", "Ethernet", "RIP", "Read IP address.", "RIP: 192.168.1.230", "Dot-decimal format."},
    {"RLHN", "Ethernet", "RLHN", "Read local host name.", "RLHN: IPG-12004020", "Derived from serial by default."},
    {"RMAC", "Ethernet", "RMAC", "Read MAC address.", "RMAC: A1-B2-C3-D4-E5-F6", "Six hex groups separated by hyphens."},
    {"RMASK", "Ethernet", "RMASK", "Read subnet mask.", "RMASK: 255.255.240.0", "Dot-decimal format."},
    {"RSTIP", "Ethernet", "RSTIP", "Reset TCP/IP settings to IPG defaults.", "RSTIP", "Rejected while emission is on."},
    {"SDGW", "Ethernet", "SDGW <ip>", "Set default gateway.", "SDGW: 192.168.0.1", "Rejected while emission is on."},
    {"SIP", "Ethernet", "SIP <ip>", "Set IP address.", "SIP: 192.168.1.231", "Rejected while emission is on."},
    {"SMAC", "Ethernet", "SMAC <mac>", "Set MAC address.", "SMAC: 12-34-56-78-EF-EF", "Rejected while emission is on."},
    {"SMASK", "Ethernet", "SMASK <mask>", "Set subnet mask.", "SMASK: 255.255.0.0", "Rejected while emission is on."},
    {"SLHN", "Ethernet", "SLHN <host>", "Set local host name.", "SLHN: IPG-12004020", "Rejected while emission is on."},
    {"DWPM", "Waveform", "DWPM", "Disable waveform pulse mode.", "DWPM", "Pulse shaping option only."},
    {"EWPM", "Waveform", "EWPM", "Enable waveform pulse mode.", "EWPM", "Requires pulse/sequence availability and pulse mode."},
    {"PCFG", "Waveform", "PCFG", "Read waveform mode configuration.", "PCFG: [] [Status:Disabled]", "Reports mode and selected ID."},
    {"PRLS", "Waveform", "PRLS", "List stored pulse profiles.", "PRLS: [] [0:New Shape 0]", "Pulse shaping option only."},
    {"PRSEL", "Waveform", "PRSEL <id>", "Select pulse profile ID.", "PRSEL: [2:New Shape 2]", "Also switches to single pulse profile mode."},
    {"SQLS", "Waveform", "SQLS", "List stored pulse sequences.", "SQLS: [] [4:New Program 4]", "Pulse shaping option only."},
    {"SQSEL", "Waveform", "SQSEL <id>", "Select pulse sequence ID.", "SQSEL: [4:New Program 4]", "Also switches to sequence mode."},
};

static InternalLogEntry s_logEntries[IPG_LASER_MAX_LOG_ENTRIES];
static uint8_t s_logStart = 0;
static uint8_t s_logCount = 0;
static bool s_captureActive = false;
static uint32_t s_captureDeadlineMs = 0;
static uint32_t s_lastRxMs = 0;
static char s_lastCommand[64] = {0};
static char s_rxLineBuffer[IPG_LASER_MAX_LINE_LENGTH + 1] = {0};
static uint8_t s_rxLineLength = 0;

static void clearRxLineBuffer() {
    s_rxLineLength = 0;
    s_rxLineBuffer[0] = '\0';
}

static void pushLogEntry(const char* direction, const char* text) {
    if (!direction || !text || !text[0]) return;

    uint8_t slot = 0;
    if (s_logCount < IPG_LASER_MAX_LOG_ENTRIES) {
        slot = (uint8_t)((s_logStart + s_logCount) % IPG_LASER_MAX_LOG_ENTRIES);
        s_logCount++;
    } else {
        slot = s_logStart;
        s_logStart = (uint8_t)((s_logStart + 1) % IPG_LASER_MAX_LOG_ENTRIES);
    }

    InternalLogEntry& entry = s_logEntries[slot];
    entry.timestampMs = millis();
    strncpy(entry.direction, direction, sizeof(entry.direction) - 1);
    entry.direction[sizeof(entry.direction) - 1] = '\0';
    strncpy(entry.text, text, sizeof(entry.text) - 1);
    entry.text[sizeof(entry.text) - 1] = '\0';
}

static void flushRxLineBuffer() {
    if (s_rxLineLength == 0) return;
    s_rxLineBuffer[s_rxLineLength] = '\0';
    pushLogEntry("RX", s_rxLineBuffer);
    s_lastRxMs = millis();
    clearRxLineBuffer();
}

static bool isPrintableLaserByte(char byteValue) {
    return isprint((unsigned char)byteValue) || byteValue == '\t';
}

} // namespace

void ipgLaserBegin() {
    s_captureActive = false;
    s_captureDeadlineMs = 0;
    s_lastRxMs = 0;
    s_lastCommand[0] = '\0';
    s_logStart = 0;
    s_logCount = 0;
    clearRxLineBuffer();
}

void ipgLaserUpdate() {
    if (!s_captureActive) return;
    if ((int32_t)(millis() - s_captureDeadlineMs) < 0) return;
    flushRxLineBuffer();
    s_captureActive = false;
}

void ipgLaserOnRxByte(char byteValue) {
    if (!s_captureActive) return;

    if (byteValue == '\r' || byteValue == '\n') {
        flushRxLineBuffer();
        return;
    }

    if (!isPrintableLaserByte(byteValue)) return;

    if (s_rxLineLength >= IPG_LASER_MAX_LINE_LENGTH) {
        flushRxLineBuffer();
    }

    s_rxLineBuffer[s_rxLineLength++] = byteValue;
    s_rxLineBuffer[s_rxLineLength] = '\0';
}

bool ipgLaserSendCommand(const String& command, const String& templateName) {
    String normalized = command;
    normalized.trim();
    if (normalized.isEmpty()) return false;

    clearRxLineBuffer();
    s_captureActive = true;
    s_captureDeadlineMs = millis() + IPG_LASER_CAPTURE_WINDOW_MS;

    strncpy(s_lastCommand, normalized.c_str(), sizeof(s_lastCommand) - 1);
    s_lastCommand[sizeof(s_lastCommand) - 1] = '\0';
    pushLogEntry("TX", s_lastCommand);

    if (!enqueueRS232Command(normalized, templateName, String("IPG"), String("rs232"))) {
        pushLogEntry("SYS", "Queue full or RS232 unavailable.");
        s_captureActive = false;
        return false;
    }

    return true;
}

void ipgLaserClearLog() {
    s_logStart = 0;
    s_logCount = 0;
    clearRxLineBuffer();
}

IpgLaserStatus ipgLaserGetStatus() {
    IpgLaserStatus status = {};
    status.captureActive = s_captureActive;
    status.captureEndsInMs = s_captureActive ? (uint32_t)max((int32_t)(s_captureDeadlineMs - millis()), (int32_t)0) : 0;
    status.lastRxAgeMs = s_lastRxMs > 0 ? (millis() - s_lastRxMs) : 0;
    status.logCount = s_logCount;
    strncpy(status.lastCommand, s_lastCommand, sizeof(status.lastCommand) - 1);
    return status;
}

uint16_t ipgLaserCopyLog(IpgLaserLogEntry* entries, uint16_t maxEntries) {
    if (!entries || maxEntries == 0) return 0;

    uint16_t count = s_logCount;
    if (count > maxEntries) count = maxEntries;

    uint8_t firstIndex = s_logCount > count ? (uint8_t)((s_logStart + (s_logCount - count)) % IPG_LASER_MAX_LOG_ENTRIES) : s_logStart;
    for (uint16_t index = 0; index < count; ++index) {
        const InternalLogEntry& source = s_logEntries[(firstIndex + index) % IPG_LASER_MAX_LOG_ENTRIES];
        IpgLaserLogEntry& target = entries[index];
        target.ageMs = millis() - source.timestampMs;
        strncpy(target.direction, source.direction, sizeof(target.direction) - 1);
        target.direction[sizeof(target.direction) - 1] = '\0';
        strncpy(target.text, source.text, sizeof(target.text) - 1);
        target.text[sizeof(target.text) - 1] = '\0';
    }

    return count;
}

const IpgLaserCommandInfo* ipgLaserGetCommandCatalog(size_t* count) {
    if (count) {
        *count = sizeof(kCommandCatalog) / sizeof(kCommandCatalog[0]);
    }
    return kCommandCatalog;
}

#endif