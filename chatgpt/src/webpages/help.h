// ============================================================================
// help.h — Modular help by sections (header-only)
// ============================================================================
// Purpose: keep help in English, emit lines via callback.
// Use PROGMEM literals with F() to save RAM.
// ============================================================================

#pragma once

#include "../feature_flags.h"
#include <Arduino.h>

typedef void (*HelpEmitFn)(const String &line);

#if ENABLE_HELP_TEXTS

// Main menu (HELP)
static inline void help_renderMainMenu(HelpEmitFn emit) {
  emit(String(F("node32-hub help")));
  emit(String(F("general sections")));
  emit(String(F("  HELP-BASIC   - basic commands")));
  emit(String(F("  HELP-CONFIG  - json configuration")));
  emit(String(F("  HELP-MAINT   - maintenance")));
  emit(String(F("  HELP-SYSTEM  - diagnostics")));
  emit(String(F("  HELP-DEBUG   - persistent logs")));

  emit(String(F("compiled-module sections")));
#if ENABLE_RS232==1 || ENABLE_SNIFFER==1
  emit(String(F("  HELP-RS232   - serial port")));
#endif
#if ENABLE_DB25==1
  emit(String(F("  HELP-NANO    - db25 bridge")));
  emit(String(F("  HELP-PRINTER - printer emulation")));
  emit(String(F("  HELP-CHARSET - encoding")));
#endif
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
  emit(String(F("  HELP-EMAIL   - sending and triggers")));
#endif
#if ENABLE_GRBL==1
  emit(String(F("  HELP-GCODESENDER - gcode sender")));
#endif

#if !(ENABLE_RS232==1 || ENABLE_SNIFFER==1 || ENABLE_DB25==1 || ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1 || ENABLE_GRBL==1)
  emit(String(F("  (no optional module compiled)")));
#endif

  emit(String(F("use a section to view details")));
}

static inline void help_emitSectionUnavailable(HelpEmitFn emit, const __FlashStringHelper* sectionName) {
  emit(String(F("section unavailable in this build: ")) + String(sectionName));
}

// HELP-BASIC
static inline void help_renderBasic(HelpEmitFn emit) {
  emit(String(F("basic commands")));
  emit(String(F("  HELP — list sections")));
  emit(String(F("  GET_CHIP_INFO — chip, memory and firmware")));
  emit(String(F("  GET_ALL_CONFIGS — json (passwords masked)")));
  emit(String(F("  GET_FIRMWARE_VERSION")));
  emit(String(F("  VERSION / VER — version and build date")));
  emit(String(F("  GET_DATETIME / DATE / TIME")));
  emit(String(F("  HELLO / GETIP — wifi, ip, rssi and buffers")));
  emit(String(F("  LED ON|OFF | LED? — onboard led control")));
  emit(String(F("  REBOOT")));
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
  emit(String(F("  EMAILDAY [DD-MM-YYYY] — send day files")));
  emit(String(F("    ex: EMAILDAY | EMAILDAY 29-12-2025")));
#endif
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
  emit(String(F("  LOGDAY [DD-MM-YYYY] — send log for date (SD/PSRAM)")));
  emit(String(F("    ex: LOGDAY | LOGDAY 28-02-2026")));
  emit(String(F("  SENSORDAY [DD-MM-YYYY] — send sensor history (SD/PSRAM)")));
  emit(String(F("    ex: SENSORDAY | SENSORDAY 01-03-2026")));
#endif
#if ENABLE_TELNET==1
  emit(String(F("  TELNET ON|OFF | TELNET?")));
#endif
  emit(String(F("  WEBSERVER ON|OFF | WEBSERVER?")));
  emit(String(F("  LOG.COMPACT ON|OFF | LOG.COMPACT?")));
  emit(String(F("  BOOT-LOG ON|OFF | BOOT-LOG? — persist boot to SD/PSRAM")));
  emit(String(F("SYS: prefix for local execution")));
#if ENABLE_RS232==1 || ENABLE_SNIFFER==1
  emit(String(F("  SYS: RS232USB ON|OFF | RS232MODE RAW|CMD")));
#endif
  emit(String(F("  SYS: DEBUG_LOGS_ON | CLEAR_PRINTER_DATA")));
}

// HELP-RS232
static inline void help_renderRS232(HelpEmitFn emit) {
  emit(String(F("rs232/db9 serial port")));
  emit(String(F("  SET_RS232_ENABLE:<0|1>")));
  emit(String(F("  SET_RS232_BAUDRATE:<IDX> (0:1200..7:115200)")));
  emit(String(F("  SET_RS232_INVERT_PINS:<0|1> — 0 RX:16/TX:17 | 1 RX:17/TX:16")));
  // SET_RS232_FRAMING and SET_RS232_READ_TIMEOUT: help text existed but no handler was ever implemented — removed.
  emit(String(F("  RS232MODE RAW|CMD | RS232MODE? | RS232RAW ON|OFF")));
  emit(String(F("  RS232USB ON|OFF | RS232USB?")));
  emit(String(F("  RS232 <TEMPLATE> <CMD> | RS232 <CMD> — use active or DEFAULT")));
  emit(String(F("  RS232LIST — list templates (* = active in ram)")));
}

// HELP-NANO — Arduino Nano bridge (DB25 handshake + reboot)
static inline void help_renderNano(HelpEmitFn emit) {
  emit(String(F("nano db25 bridge")));
  emit(String(F("  centronics timing on nano; uart at 500000 (esp rx=35, tx=33)")));
  emit(String(F("  flow: XOFF=0x13 pause, XON=0x11 resume")));
  emit(String(F("  NANO.REBOOT | NANO_REBOOT | REBOOT_NANO | NANO REBOOT")));
  emit(String(F("  adjustable without reflash: ACK WIDTH, STROBE FILTER, PAUSE MODE")));
  emit(String(F("  FORCE BUSY, XON/XOFF ENABLE, ACK DELAY, BUSY TAIL")));
  emit(String(F("  RESET STATE, PAUSE GRACE BYTES, NOOP")));
  emit(String(F("  use SYS: for local execution, ex: SYS: NANO.ACK 8")));
  emit(String(F("  ex: SYS: NANO.FORCEBUSY ON|OFF")));
}

// HELP-EMAIL (caller provides limits for 6/8 LPI)
static inline void help_renderEmail(HelpEmitFn emit, int limit6Lpi, int limit8Lpi) {
  emit(String(F("email and triggers")));
  emit(String(F("  SET_LINE_LIMIT:<N> — ")) + String(limit6Lpi) + String(F(" @6LPI | ")) + String(limit8Lpi) + String(F(" @8LPI")));
  // SET_INACTIVITY_TIMEOUTS: help text existed but no handler was ever implemented — removed.
  emit(String(F("  ADD_EMAIL_NAME:<name>,<email> | REMOVE_EMAIL_NAME:<name>")));
  emit(String(F("  auto triggers: inactivity, line limit, buffer full, formfeed")));
}

// HELP-PRINTER
static inline void help_renderPrinter(HelpEmitFn emit) {
  emit(String(F("printer emulation")));
  emit(String(F("  SET_PRINTER_MODE:<MODE>  0=Generic | 1=Epson ESC/P | 2=IBM Proprinter")));
  emit(String(F("  esc/p: ESC @ reset, ESC A line spacing, ESC * bitmap")));
  emit(String(F("  ibm: ESC E bold on, ESC F bold off, ESC G double print")));
}

// HELP-CHARSET
static inline void help_renderCharset(HelpEmitFn emit) {
  emit(String(F("encoding and charset")));
  emit(String(F("  SET_CHARSET:<MODE>  0=AUTO(UTF-8) | 1=LATIN1 | 2=CP850")));
  emit(String(F("  use AUTO for current text; LATIN1 for legacy; CP850 for dos")));
}

// HELP-CONFIG
static inline void help_renderConfig(HelpEmitFn emit) {
  emit(String(F("json configuration")));
  emit(String(F("  SET_ALL_CONFIGS:###CONFIG_START###{JSON}###CONFIG_END###")));
  emit(String(F("  accepts multiple fields; masked passwords are ignored")));
  emit(String(F("  main fields: wifi.*, smtp.*, email.*, rs232.*, telnet.*, ntp.*")));
  emit(String(F("  debug.logs_enabled, recipients, sched_cmd and sched_dt")));
}

// HELP-MAINT
static inline void help_renderMaintenance(HelpEmitFn emit) {
  emit(String(F("maintenance")));
  emit(String(F("  CLEAR_PRINTER_DATA | CLEAR_TERMINAL")));
  emit(String(F("  DEBUG_LOGS_ON | DEBUG_LOGS_OFF")));
  emit(String(F("  BOOT-LOG ON|OFF | BOOT-LOG? — log boot messages to SD")));
  emit(String(F("  LED OFF — keep onboard led off when supported")));
  emit(String(F("  REBOOT")));
  emit(String(F("  RETRYSD — retry sd mount (failover psram)")));
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
  emit(String(F("  EMAILDAY — send sd files for today")));
  emit(String(F("  LOGDAY [DD-MM-YYYY] — send log for date (SD/PSRAM)")));
#endif
#if ENABLE_SDCARD==1
  emit(String(F("  SD_SPEED_TEST[:<MB>] — microsd benchmark (default 5 MB)")));
  emit(String(F("  SD_SPEED_TEST:ALL[:<MB>] — benchmark multiple speeds")));
#endif
  emit(String(F("monitoring: GET_DATETIME/DATE/TIME, GET_CHIP_INFO, GET_ALL_CONFIGS, HELLO/GETIP")));
}

// ============================================================================
// Helper messages reused by adjacent command handlers.
// ============================================================================

// RS232LIST — no templates available
static inline void help_emitRs232NoTemplates(HelpEmitFn emit) {
  emit(String(F("(no rs232 templates defined)")));
}

// RS232LIST — list header
static inline void help_emitRs232TemplatesHeader(HelpEmitFn emit) {
  emit(String(F("rs232 templates")));
}

// RS232 — mixed usage <TEMPLATE> <COMMAND> | or just <COMMAND>
static inline void help_emitRs232UsageMixed(HelpEmitFn emit) {
  emit(String(F("usage: RS232 <TEMPLATE> <CMD> | RS232 <CMD> (uses active/DEFAULT)")));
}

// RS232 — usage hint (optional template or standalone command)
static inline void help_emitRs232UsageEither(HelpEmitFn emit) {
  emit(String(F("usage: RS232 <TEMPLATE> <CMD> | RS232 <CMD>")));
}

// RS232 — usage hint (required template plus command)
static inline void help_emitRs232UsageTemplateOnly(HelpEmitFn emit) {
  emit(String(F("usage: RS232 <TEMPLATE> <CMD>")));
}

// TELNET — usage hint
static inline void help_emitTelnetUsage(HelpEmitFn emit) {
  emit(String(F("usage: TELNET ON | TELNET OFF | TELNET?")));
}

// WEBSERVER — usage hint
static inline void help_emitWebserverUsage(HelpEmitFn emit) {
  emit(String(F("usage: WEBSERVER ON | WEBSERVER OFF | WEBSERVER?")));
}

// HELP-SYSTEM — System diagnostic and monitoring commands
static inline void help_renderSystem(HelpEmitFn emit) {
  if (!emit) return;
  emit(String(F("system diagnostics")));
  // CONFIG RESTORE: deprecated — config is now managed by ConfigManager; no restore needed.
  emit(String(F("  HEARTBEAT ON|OFF — enable or disable heartbeat")));
  emit(String(F("  HEARTBEAT 5|10|15|30|60 — set interval in seconds")));
  emit(String(F("  HEARTBEAT? — show current state")));
  emit(String(F("heartbeat shows 5 lines per cycle")));
  emit(String(F("  line 1: loops, uptime, heap and psram")));
  emit(String(F("  line 2: rs232-out queues, logs and print buffer")));
  emit(String(F("  line 3: psram retry buffers (rs232 and lpt)")));
  emit(String(F("  line 4: active sd buffers and last flush")));
  emit(String(F("  line 5: email queue, last send and state")));
  emit(String(F("default: active, interval 10s, persistent")));
}

// HELP-DEBUG — Debug writer commands
static inline void help_renderDebug(HelpEmitFn emit) {
  if (!emit) return;
  emit(String(F("debug writer")));
  emit(String(F("  DEBUG_WRITER ON — enable and log to /DEBUG/YYYY-MM-DD.txt")));
  emit(String(F("  DEBUG_WRITER OFF — disable")));
  emit(String(F("  DEBUG_WRITER STATUS — show buffer, flush and stats")));
  emit(String(F("  DEBUG_WRITER_NOW — force immediate flush to sd")));
  emit(String(F("  DEBUG_WRITER_FLUSH <seconds> — interval 10-3600s, default 300")));
  emit(String(F("features")));
  emit(String(F("  - captures INFO, WARN, ERROR and DEBUG")));
  emit(String(F("  - separate from crash logs (/logs/)")));
  emit(String(F("  - 64KB buffer, auto-flush at 48KB")));
  emit(String(F("  - integrated in Priority Manager (P2 - SD writes)")));
  emit(String(F("  - thread-safe with low overhead")));
  emit(String(F("ex: DEBUG_WRITER ON; DEBUG_WRITER_FLUSH 600")));
}

#if ENABLE_GRBL==1
// HELP-GCODESENDER — G-code sender commands
static inline void help_renderGcodeSender(HelpEmitFn emit) {
  if (!emit) return;
  emit(String(F("gcode sender")));
  emit(String(F("  GRBLLOG ON — enable job trace log to /GRBL_JOBS/ on SD")));
  emit(String(F("  GRBLLOG OFF — disable job trace log")));
  emit(String(F("  GRBLLOG — show current state (ON/OFF)")));
  emit(String(F("trace log fields per line:")));
  emit(String(F("  T=ms;SEQ=n;EV=event;SENT=n;ACK=n;TOT=n;INFLIGHT=n;FIFO=n;ERR=n;POLL=n;DETAIL=text")));
  emit(String(F("final VERIFY event confirms ack == sent")));
  emit(String(F("persisted: survives reboot via config")));
}
#endif

// ============================================================================
// Simple router for HELP/HELP-XXXX commands.
// Backward compat: Portuguese HELP-BASICOS, HELP-MANUTENCAO, HELP-IMPRESSORA,
// HELP-SISTEMA still route to the same sections.
// ============================================================================
static inline bool help_dispatch(const String &upperCmd, HelpEmitFn emit, int limit6Lpi, int limit8Lpi) {
  if (!emit) {
    return upperCmd.startsWith("HELP");
  }

  if (upperCmd.equalsIgnoreCase(String(F("HELP")))) { help_renderMainMenu(emit); return true; }
  if (upperCmd.equalsIgnoreCase(String(F("HELP-BASIC"))) || upperCmd.equalsIgnoreCase(String(F("HELP-BASICOS")))) { help_renderBasic(emit); return true; }

    if (upperCmd.equalsIgnoreCase(String(F("HELP-RS232")))) {
  #if ENABLE_RS232==1 || ENABLE_SNIFFER==1
    help_renderRS232(emit);
  #else
    help_emitSectionUnavailable(emit, F("HELP-RS232"));
  #endif
    return true;
    }

    if (upperCmd.equalsIgnoreCase(String(F("HELP-NANO")))) {
  #if ENABLE_DB25==1
    help_renderNano(emit);
  #else
    help_emitSectionUnavailable(emit, F("HELP-NANO"));
  #endif
    return true;
    }

    if (upperCmd.equalsIgnoreCase(String(F("HELP-EMAIL")))) {
  #if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    help_renderEmail(emit, limit6Lpi, limit8Lpi);
  #else
    help_emitSectionUnavailable(emit, F("HELP-EMAIL"));
  #endif
    return true;
    }

    if (upperCmd.equalsIgnoreCase(String(F("HELP-PRINTER"))) || upperCmd.equalsIgnoreCase(String(F("HELP-IMPRESSORA")))) {
  #if ENABLE_DB25==1
    help_renderPrinter(emit);
  #else
    help_emitSectionUnavailable(emit, F("HELP-PRINTER"));
  #endif
    return true;
    }

    if (upperCmd.equalsIgnoreCase(String(F("HELP-CHARSET")))) {
  #if ENABLE_DB25==1
    help_renderCharset(emit);
  #else
    help_emitSectionUnavailable(emit, F("HELP-CHARSET"));
  #endif
    return true;
    }

  if (upperCmd.equalsIgnoreCase(String(F("HELP-CONFIG")))) { help_renderConfig(emit); return true; }
  if (upperCmd.equalsIgnoreCase(String(F("HELP-MAINT"))) || upperCmd.equalsIgnoreCase(String(F("HELP-MANUTENCAO")))) { help_renderMaintenance(emit); return true; }
  if (upperCmd.equalsIgnoreCase(String(F("HELP-SYSTEM"))) || upperCmd.equalsIgnoreCase(String(F("HELP-SISTEMA")))) { help_renderSystem(emit); return true; }
  if (upperCmd.equalsIgnoreCase(String(F("HELP-DEBUG"))) || upperCmd.equalsIgnoreCase(String(F("HELP_DEBUG"))) || upperCmd.equalsIgnoreCase(String(F("HELP DEBUG")))) { help_renderDebug(emit); return true; }
#if ENABLE_GRBL==1
  if (upperCmd.equalsIgnoreCase(String(F("HELP-GCODESENDER")))) { help_renderGcodeSender(emit); return true; }
#endif
  return false;
}

#else // ENABLE_HELP_TEXTS==0

// Minimal stubs when help is disabled to save flash.
static inline void help_renderMainMenu(HelpEmitFn) {}
static inline void help_emitSectionUnavailable(HelpEmitFn, const __FlashStringHelper*) {}
static inline void help_renderBasic(HelpEmitFn) {}
static inline void help_renderRS232(HelpEmitFn) {}
static inline void help_renderNano(HelpEmitFn) {}
static inline void help_renderEmail(HelpEmitFn, int, int) {}
static inline void help_renderPrinter(HelpEmitFn) {}
static inline void help_renderCharset(HelpEmitFn) {}
static inline void help_renderConfig(HelpEmitFn) {}
static inline void help_renderMaintenance(HelpEmitFn) {}
static inline void help_renderSystem(HelpEmitFn) {}
static inline void help_renderDebug(HelpEmitFn) {}
static inline void help_renderGcodeSender(HelpEmitFn) {}
static inline void help_emitRs232NoTemplates(HelpEmitFn) {}
static inline void help_emitRs232TemplatesHeader(HelpEmitFn) {}
static inline void help_emitRs232UsageMixed(HelpEmitFn) {}
static inline void help_emitRs232UsageEither(HelpEmitFn) {}
static inline void help_emitRs232UsageTemplateOnly(HelpEmitFn) {}
static inline void help_emitTelnetUsage(HelpEmitFn) {}
static inline void help_emitWebserverUsage(HelpEmitFn) {}
static inline bool help_dispatch(const String &, HelpEmitFn, int, int) { return false; }

#endif // ENABLE_HELP_TEXTS
