// logger_impl.h
// Included only from chatgpt.ino (single TU).
// Logging and network helpers: setBusyPinState, getEmailTriggerReason,
// isCriticalDebugLog, sendVirtualMessageInternal, sendVirtualMessage,
// manageSerialOutput, convertAsciiDecimalString, manageWiFiConnection.
// DO NOT add #pragma once - this is an impl-include file, not a shared header.
void setBusyPinState(bool state) {
    // New policy: we do not drive a BUSY pin on the ESP32; we ask the Nano to pause/resume via XOFF/XON.
    if (state == isPrinterBusy) return; // No change
    isPrinterBusy = state;
    // Rate-limit the debug log: the busy state can oscillate rapidly and would spam the monitor.
    static unsigned long lastBusyLogMs = 0;
    bool logBusy = (millis() - lastBusyLogMs >= 60000UL);
    if (logBusy) lastBusyLogMs = millis();
    if (state) {
        nanoBridgeSendXoff(); // Pause parallel Nano/host
        nanoBridgeForceBusy(true); // Force physical BUSY on the Nano for deterministic braking
    if (enableDebugLogs && logBusy) sendVirtualMessage(F("BUSY=ON -> XOFF"), DEBUG);
    } else {
        nanoBridgeSendXon();  // Resume parallel Nano/host
        nanoBridgeForceBusy(false); // Release the physical BUSY line
    if (enableDebugLogs && logBusy) sendVirtualMessage(F("BUSY=OFF -> XON"), DEBUG);
    }
}

String getEmailTriggerReason(EmailTriggerSource source) {
    switch (source) {
        case TRIGGER_MANUAL_COMMAND: return String(F("Comando Manual"));
        case TRIGGER_AUTO_INACTIVITY_ENABLED: return String(F("Inatividade"));
        case TRIGGER_AUTO_INACTIVITY_DISABLED_LONG_TIMEOUT: return String(F("Inatividade (longa)"));
        case TRIGGER_AUTO_LINE_LIMIT: return String(F("Limite linhas"));
        case TRIGGER_AUTO_BUFFER_FULL: return String(F("Buffer cheio"));
        case TRIGGER_AUTO_FORM_FEED: return String(F("Form Feed"));
        case TRIGGER_NONE:
        default: return String(F("Desconhecida"));
    }
}

// Determine if a DEBUG message is critical and should be shown even when enableDebugLogs=false
static bool isCriticalDebugLog(const String &rawMsg) {
    // By default we do not allow any DEBUG messages when enableDebugLogs=false.
    // We keep this function for future tuning (e.g., allow very specific XON/XOFF cases),
    // but critical messages already use INFO/WARN/ERROR levels in this firmware.
    (void)rawMsg; // Avoid unused parameter warning
    return false;
}

// Determine if a log message should bypass suppression during active reception
// Goal: make critical RS232 events visible in real-time (micro-pauses, line limits, XON/XOFF)
// Lightweight approach: keyword detection on already-formatted messages (after prefixes) to avoid changing signatures
static bool shouldBypassSuppressionFor(const String &formattedMsg, int level) {
    // Allow bypass for explicit control events (INFO/DEBUG with specific keywords)
    if (level == DEBUG) {
        // Micro-pauses and chunking triggers
        // Make detection robust against hyphen/emoji variations and capitalization
        // any message that contains "Micro" and "pausa" (regardless of hyphen) will pass
        bool hasMicroWord = (formattedMsg.indexOf(F("Micro")) >= 0) || (formattedMsg.indexOf(F("micro")) >= 0);
        bool hasPausaWord = (formattedMsg.indexOf(F("pausa")) >= 0) || (formattedMsg.indexOf(F("Pausa")) >= 0);
        if (hasMicroWord && hasPausaWord) return true;                //  Qualquer telemetria de micro-pausa
        if (formattedMsg.indexOf(F("Micro-pausa")) >= 0) return true; //  H?fen normal
        if (formattedMsg.indexOf(F("Micro-pausa")) >= 0) return true; //  H?fen n?o-quebr?vel (U+2011)
        if (formattedMsg.indexOf(F("retoma detetada")) >= 0) return true; //  Retoma ap?s primeira pausa
        if (formattedMsg.indexOf(F("pausa #")) >= 0) return true;         //  Contagem de pausas
        if (formattedMsg.indexOf(F("Limite de linhas")) >= 0) return true;         //  Disparo de limite de linhas
        if (formattedMsg.indexOf(F("Buffer Quase Cheio")) >= 0) return true;       //  Quase cheio
    // Short prefixes used in this optimization
    if (formattedMsg.startsWith(F("TRG:"))) return true; //  Short triggers
    if (formattedMsg.startsWith(F("ST:"))) return true;  //  Short states
    if (formattedMsg.startsWith(F("EML:"))) return true; //  Compact email telemetry
    if (formattedMsg.indexOf(F("DEBUG_EMAIL_PROCESS")) >= 0) return true;      //  Email process state (legacy)
    }
    // Logs informativos curtos
    if (formattedMsg.startsWith(F("WAIT:"))) return true; // ? Esperas curtas
    //  Important informational events related to forced cuts/resumes
    if (level == INFO) {
        if (formattedMsg.indexOf(F("Corte for?ado")) >= 0) return true;             //  Corte por timeout
        if (formattedMsg.indexOf(F("Emissor parou")) >= 0) return true;             // ? Emissor parou
    }
    // XON/XOFF status (useful in real time)
    if (formattedMsg.indexOf(F("XOFF")) >= 0 || formattedMsg.indexOf(F("XON")) >= 0) {
        return true;
    }
    // Diagnostic logs for COUNT/colN/streaming — must appear even during data reception
    if (level == WARN) {
        if (formattedMsg.startsWith(F("[COUNT]"))) return true;
        if (formattedMsg.startsWith(F("[colN]")))  return true;
        if (formattedMsg.startsWith(F("[STREAM]"))) return true;
        if (formattedMsg.startsWith(F("[RECIP")))  return true;
        if (formattedMsg.indexOf(F("[UNIKON]")) >= 0) return true; // Always show Unikon poll events
    }
    return false; //  Remaining messages follow the standard suppression rule
}

// ---------------------------------------------------------------------------
// Boot-log deferred write queue
// logCompactBufferedEvent() allocates char line[256] on the stack, which
// overflows loopTask (8KB) when called from deep inside sendVirtualMessageInternal.
// We enqueue into this static ring buffer (BSS, not stack) and drain from loop().
// ---------------------------------------------------------------------------
struct _BlqEntry { char tag[10]; char msg[200]; };
static _BlqEntry _blq[8];
static volatile uint8_t _blqHead = 0;
static volatile uint8_t _blqTail = 0;

// Called from loop() — stack is shallow here, safe to call logCompactBufferedEvent.
static void bootLogDrainQueue() {
    while (_blqHead != _blqTail) {
        logCompactBufferedEvent(_blq[_blqHead].tag, _blq[_blqHead].msg);
        _blqHead = (_blqHead + 1) & 7;
    }
}

// ---------------------------------------------------------------------------
// System-command response capture (web serial monitor)
// While active, COMMAND_RESPONSE/INFO/ERROR output emitted by sendVirtualMessage
// is buffered so the web /api/system-command endpoint can return the real reply
// instead of a canned placeholder.
// ---------------------------------------------------------------------------
static volatile bool g_sysCmdCaptureActive = false;
static String g_sysCmdCapture;
static portMUX_TYPE g_sysCmdCaptureMux = portMUX_INITIALIZER_UNLOCKED;
static const size_t SYS_CMD_CAPTURE_MAX = 1024;

void beginSystemCommandCapture() {
    portENTER_CRITICAL(&g_sysCmdCaptureMux);
    g_sysCmdCapture = String();
    g_sysCmdCaptureActive = true;
    portEXIT_CRITICAL(&g_sysCmdCaptureMux);
}

String endSystemCommandCapture() {
    portENTER_CRITICAL(&g_sysCmdCaptureMux);
    g_sysCmdCaptureActive = false;
    String out = g_sysCmdCapture;
    g_sysCmdCapture = String();
    portEXIT_CRITICAL(&g_sysCmdCaptureMux);
    return out;
}

static void sendVirtualMessageInternal(const String &msg, int level, bool alreadyFormatted, bool bypassDedup) {
     if (g_sysCmdCaptureActive) {
         if (level == COMMAND_RESPONSE || level == INFO || level == ERROR) {
             portENTER_CRITICAL(&g_sysCmdCaptureMux);
             if (g_sysCmdCapture.length() < SYS_CMD_CAPTURE_MAX) {
                 if (g_sysCmdCapture.length() > 0) g_sysCmdCapture += '\n';
                 g_sysCmdCapture += msg;
             }
             portEXIT_CRITICAL(&g_sysCmdCaptureMux);
         }
     }

     // Fragmentation mitigation: limit message size to reduce heap churn
     const size_t MAX_LOG_MESSAGE_SIZE = 512;
     
     // Log shearing when heap is low to save memory
     // System typically starts with ~170KB heap, so thresholds adjusted accordingly
     const size_t HEAP_SHED_THRESHOLD = 110 * 1024; // 110 KB - start dropping non-essential logs
     const size_t HEAP_CRITICAL_THRESHOLD = 100 * 1024; // 100 KB - CRITICAL: drop almost everything
     size_t freeHeapAtLog = ESP.getFreeHeap();
     
     // 60s covers Ethernet init: WiFi-enabled path delays 8s then runs DHCP (up to 15s+6s PHY wait).
     bool startupBootWindow = millis() < 60000UL;

     // CRITICAL heap: only CRITICAL_EVENT and ERROR allowed
     if (!startupBootWindow && freeHeapAtLog <= HEAP_CRITICAL_THRESHOLD) {
         if (level != CRITICAL_EVENT && level != ERROR) {
             return; //  Drop immediately to protect system
         }
     }
     // Low heap: drop non-essential logs
     else if (!startupBootWindow && freeHeapAtLog <= HEAP_SHED_THRESHOLD) {
         //  Under memory pressure, allow only critical/error/essential warning levels
          // - DEBUG: always discarded
          // - INFO: discarded
          // - COMMAND_RESPONSE: discarded (UI may re-emit later via command or endpoint)
          // - TELNET_EVENT: discarded
          // - WARN/ERROR/CRITICAL_EVENT: allowed
          if (level == DEBUG || level == INFO || level == COMMAND_RESPONSE || level == TELNET_EVENT) {
              return; //  Cut to protect heap
          }
      }
      //  Local helper: sanitizes to basic ASCII (removes emojis and unicode > 0x7E; keeps CR/LF and tab)
     auto sanitizeAscii = [](const String &in) -> String {
         String out; 
         size_t reserveSize = min(in.length(), (size_t)MAX_LOG_MESSAGE_SIZE);
         out.reserve(reserveSize + 1); // Pre-allocate to reduce fragmentation
         for (size_t i = 0; i < in.length() && out.length() < MAX_LOG_MESSAGE_SIZE; ++i) {
             char c = in.charAt(i);
             // Mant?m CR/LF/TAB
             if (c == '\r' || c == '\n' || c == '\t') { out += c; continue; }
             // Convert to visible ASCII, remove the rest
             if (c >= 0x20 && c <= 0x7E) { out += c; }
         }
         return out;
     };
     //  Local helper: compacts common messages into short tokens
     auto compactMessageContent = [](const String &in) -> String {
         // Avoid large allocations: compare simple prefixes
         // Use const references instead of String() to reduce temporary allocations
         if (in.startsWith(F("XOFF"))) return F("XOFF");
         if (in.startsWith(F("XON"))) return F("XON");
         if (in == F("Web monitor pronto")) return F("WEB:READY");
         if (in == F("Boot OK")) return F("BOOT:OK");
         if (in == F("Reaplicacao OK")) return F("REAPPLY:OK");
         if (in.indexOf(F("Telnet ON")) >= 0) return F("TELNET:ON");
         if (in.indexOf(F("Telnet OFF")) >= 0) return F("TELNET:OFF");
         if (in.indexOf(F("RS232 reconfig")) >= 0) {
             if (in.indexOf(F(" RX=")) >= 0 || in.indexOf(F(" TX=")) >= 0) return in;
             return F("RS232:RECFG");
         }
         if (in.indexOf(F("RS232 OFF")) >= 0) return F("RS232:OFF");
         if (in.indexOf(F("STA ON:")) >= 0) return F("WIFI:STA:ON");
         if (in.indexOf(F("AP ON:")) >= 0) return F("WIFI:AP:ON");
         if (in.indexOf(F("AP OFF")) >= 0) return F("WIFI:AP:OFF");
         // Truncate if too long to prevent fragmentation
         if (in.length() > MAX_LOG_MESSAGE_SIZE) {
             String truncated;
             truncated.reserve(MAX_LOG_MESSAGE_SIZE + 4);
             truncated = in.substring(0, MAX_LOG_MESSAGE_SIZE - 3);
             truncated += F("...");
             return truncated;
         }
         return in;
     };
     if (level == DEBUG && !enableDebugLogs) {
          //  Allow critical DEBUG even with debug logs disabled
         if (!isCriticalDebugLog(msg)) {
                 return;
         }
     }

  String fullFormattedMessageWithoutEnd;
  // Pre-reserve memory to reduce fragmentation
  fullFormattedMessageWithoutEnd.reserve(MAX_LOG_MESSAGE_SIZE);
  
  if (alreadyFormatted) {
      // Message already contains any prefix/decoration necessary - use as is
      fullFormattedMessageWithoutEnd = msg;
      if (fullFormattedMessageWithoutEnd.length() > MAX_LOG_MESSAGE_SIZE) {
          fullFormattedMessageWithoutEnd = fullFormattedMessageWithoutEnd.substring(0, MAX_LOG_MESSAGE_SIZE - 3);
          fullFormattedMessageWithoutEnd += F("...");
      }
  } else {
       //  Original prefix added " @@[" to wrap messages on the web monitor
    // Current requirement: remove @@[] wrapping to display simple messages (ASCII)
      // We keep prefix variable but without special envelope to minimize other changes
      String formattedMessagePrefix = String(); // Now empty (no " @@[")

      if (!compactLogMode) {
          switch (static_cast<LogLevel>(level)) {
              case INFO:  formattedMessagePrefix += F("[INFO] "); break;
              case WARN:  formattedMessagePrefix += F("[WARN] "); break;
              case ERROR: formattedMessagePrefix += F("[ERROR] "); break;
              case DEBUG: formattedMessagePrefix += F("[DEBUG] "); break;
              case CRITICAL_EVENT: formattedMessagePrefix += F("[CRITICO] "); break;
              case COMMAND_RESPONSE: formattedMessagePrefix += F("[CMD] "); break;
              case TELNET_EVENT: formattedMessagePrefix += F("[TELNET] "); break;
          }
      }

      //  If compact Telnet mode is active and client authenticated, generate minimal prefix
#if ENABLE_TELNET==1
      if ((compactLogMode || telnetCompactMode) && telnetClient && telnetClient.connected() && telnetAuthenticated) { // Compacting also in global mode
#else
      if (compactLogMode) { // Global compact mode (Telnet disabled)
#endif
          //  Mapa r?pido de prefixos curtos
          const char* shortTag = ""; //  Inicial
          switch(static_cast<LogLevel>(level)){
              case INFO: shortTag = "I"; break;
              case WARN: shortTag = "W"; break;
              case ERROR: shortTag = "E"; break;
              case DEBUG: shortTag = enableDebugLogs?"D":""; break;
              case CRITICAL_EVENT: shortTag = "C"; break;
              case COMMAND_RESPONSE: shortTag = "CMD"; break;
              case TELNET_EVENT: shortTag = "T"; break;
          }
          if(shortTag[0] != '\0') { //  S? adiciona se tag n?o vazia
              String content = compactLogMode ? compactMessageContent(msg) : msg;
              content = sanitizeAscii(content);
              fullFormattedMessageWithoutEnd.reserve(strlen(shortTag) + content.length() + 2);
              fullFormattedMessageWithoutEnd = String(shortTag) + F("|") + content; //  Formato curto ex.: W|Mensagem
          } else {
              String content = sanitizeAscii(compactLogMode ? compactMessageContent(msg) : msg);
              fullFormattedMessageWithoutEnd = content; //  Sem tag
          }
      } else {
          //  Modo normal: manter formato expandido
          String content = compactLogMode ? compactMessageContent(msg) : msg;
          content = sanitizeAscii(content);
          if (compactLogMode) {
              // Prefixos curtos tamb?m em outras sa?das
              const char* shortTag = "";
              switch(static_cast<LogLevel>(level)){
                  case INFO: shortTag = "I"; break;
                  case WARN: shortTag = "W"; break;
                  case ERROR: shortTag = "E"; break;
                  case DEBUG: shortTag = enableDebugLogs?"D":""; break;
                  case CRITICAL_EVENT: shortTag = "C"; break;
                  case COMMAND_RESPONSE: shortTag = "CMD"; break;
                  case TELNET_EVENT: shortTag = "T"; break;
              }
              if(shortTag[0] != '\0') {
                  fullFormattedMessageWithoutEnd.reserve(strlen(shortTag) + content.length() + 2);
                  fullFormattedMessageWithoutEnd = String(shortTag) + F("|") + content;
              } else {
                  fullFormattedMessageWithoutEnd = content;
              }
          } else {
              fullFormattedMessageWithoutEnd = formattedMessagePrefix + content; 
          }
      }
  }

  unsigned long currentMillis = millis();
  
  // Apply rate limiting for CRITICAL_EVENT
  if (!bypassDedup) {
      if (level == CRITICAL_EVENT) {
          if (fullFormattedMessageWithoutEnd == lastLoggedSystemMessageContent && (currentMillis - lastLoggedSystemMessageTime < CRITICAL_EVENT_RATE_LIMIT_MS)) {
              return; // Suppress repeated critical event within the rate limit
          }
      } else { 
          // For other levels (INFO, WARN, ERROR, DEBUG, COMMAND_RESPONSE, TELNET_EVENT),
          // avoid immediate repetition of the *same* message, but allow new messages.
          if (fullFormattedMessageWithoutEnd == lastLoggedSystemMessageContent && level != DEBUG) { // Allow repeating DEBUG to see flow
              return; 
          }
      }
  }

    // Condition to suppress messages: active data reception (LPT/RS232) to avoid mixing with lines
    // Rules: suppress ANY level while there is data activity; re-emit later
    //  Exception: some critical messages (micro-pause, line limit, XON/XOFF) must appear in real-time
        bool isDataActiveForSuppression = true;
        {
            bool empty = false;
            bool hasData = true;
            // Non-blocking: if we cannot safely read the buffer, assume active.
            if (bufferDadosTryIsEmpty(empty, 0)) hasData = !empty;
            isDataActiveForSuppression = (currentMillis - lastReceptionTime < DATA_INACTIVITY_FOR_SUPPRESSION_MS)
                || hasData
                || isReceivingData
                || rs232_sending
                || rs232FirstResponseGuardActive; // Avoid pollution during first response/tx
        }
        // Boot window: 'lastReceptionTime' starts at 0, so 'millis()-0 < 1000ms' is TRUE for the
        // first second, causing all boot messages to be silently discarded.
        // Bypass suppression for the first 30 s so the full boot log is always visible.
        if (startupBootWindow) {
            isDataActiveForSuppression = false;
        }
        bool bypassSuppression = shouldBypassSuppressionFor(fullFormattedMessageWithoutEnd, level);
        //  Never suppress command responses — they must go out immediately
        if (level == COMMAND_RESPONSE) {
            bypassSuppression = true;
        }

    if (isDataActiveForSuppression && !bypassSuppression) {
      // Ignore empty messages when suppressing (avoids lines "[INFO]" without content)
      if (!alreadyFormatted) {
          String tmp = msg; String t = tmp; t.trim();
          if (t.length() == 0) {
              return; // Nothing to save/show
          }
      }
      // Save the message for later emission (flush after reception end)
      // We want to always keep the last 16 messages.
      // If the buffer is full, remove the oldest entry (shift)
      // before inserting the new one, guaranteeing overwrite behavior (rolling).
      // DISABLED: Suppression buffer disabled due to crashes
      // if (suppressedSystemMessagesBuffer.isFull()) {
      //     suppressedSystemMessagesBuffer.shift(); // ? Safe: remove oldest (FIFO)
      // }
      // suppressedSystemMessagesBuffer.push(fullFormattedMessageWithoutEnd);
      //  WORKAROUND: Just discard suppressed messages for now
      // TODO: Implement thread-safe message queue
      // Update the last logged message (even if suppressed) for rate limiting
      lastLoggedSystemMessageContent = fullFormattedMessageWithoutEnd;
      lastLoggedSystemMessageTime = currentMillis;
      return; // Do not display now (neither in serialMonitorBuffer nor in printerOutputBuffer)
  }

  // Only now (not suppressed) we add to the serial monitor buffer
  const char* logLevelStr; // PHASE 2: Use const char* to avoid String allocation
  switch (level) {
      case INFO: logLevelStr = "INFO"; break;
      case WARN: logLevelStr = "WARN"; break;
      case ERROR: logLevelStr = "ERROR"; break;
      case DEBUG: logLevelStr = "DEBUG"; break;
      case CRITICAL_EVENT: logLevelStr = "CRITICAL_EVENT"; break;
      case COMMAND_RESPONSE: logLevelStr = "COMMAND_RESPONSE"; break;
      case TELNET_EVENT: logLevelStr = "TELNET_EVENT"; break;
  }

  SerialMessage serialMsg; // Structure for serial monitor message buffer
  // Use NTP epoch (seconds) when available so web serial monitor shows wall-clock time.
  // Fall back to millis()/1000 (elapsed seconds since boot) before NTP sync.
  // Store a "display epoch": UTC epoch shifted by the local TZ offset so that the
  // browser can format with getUTCHours/Minutes/Seconds and get the correct local time
  // regardless of the browser's own timezone setting.
  // localtime_r() uses the TZ applied by configTzTime() at boot (e.g. WET0WEST,M3.5.0/1,M10.5.0).
  {
      time_t _t = time(NULL);
      if (_t > 1000000000UL) {
          struct tm _li, _gm;
          localtime_r(&_t, &_li); // Local time fields using TZ set by configTzTime()
          gmtime_r(&_t, &_gm);   // UTC time fields
          // Compute TZ offset without tm_gmtoff (newlib does not provide it).
          // Both structs represent the same instant; difference of h/m/s fields is the offset.
          // Clamp accounts for day-boundary crossings (e.g. 23:00 UTC = 00:00 in UTC+1).
          long _off = (long)(_li.tm_hour - _gm.tm_hour) * 3600L
                    + (long)(_li.tm_min  - _gm.tm_min)  * 60L
                    + (long)(_li.tm_sec  - _gm.tm_sec);
          if (_off < -43200L) _off += 86400L;
          if (_off >  43200L) _off -= 86400L;
          serialMsg.timestamp = (unsigned long)((long)_t + _off);
      } else {
          serialMsg.timestamp = millis() / 1000; // Before NTP sync: elapsed seconds since boot
      }
  }
  serialMsg.setText(fullFormattedMessageWithoutEnd); // PHASE 2: Use setText (fixed buffers)
  serialMsg.setType(logLevelStr); // PHASE 2: Use setType (fixed buffers)

  // OPTIMIZATION (Point 5): Push to PSRAM buffer (Ring Buffer 5000)
  pushLogToPsram(serialMsg);

  // Legacy buffer (RAM, short) - kept for compatibility
  if (!serialMonitorBuffer.isFull()) {
      serialMonitorBuffer.push(serialMsg);
  } else {
      // Improvement: ensure the legacy buffer also rolls (rolling) instead of discarding
      serialMonitorBuffer.shift(); // Remove oldest
      serialMonitorBuffer.push(serialMsg);
  }

  // Native USB CDC boards can miss early buffered logs while the host terminal is still
  // reattaching after reset. Also, if printerOutputBuffer allocation failed, the buffered
  // path is unavailable. In both cases, fall back to direct Serial output.
  bool serialDirectFallback = (currentStartupState != STARTUP_COMPLETE) || (printerOutputBuffer.capacity() == 0);
  if (serialDirectFallback) {
      Serial.println(fullFormattedMessageWithoutEnd);
      lastSerialOutputWasNewline = true;
  } else {
      // Unified buffered output path for normal runtime.
      if (!lastSerialOutputWasNewline) {
          printerOutputBuffer.push('\n');
      }

      for (char c : fullFormattedMessageWithoutEnd) {
          printerOutputBuffer.push(c);
      }
      printerOutputBuffer.push('\n');

      // manageSerialOutput() will update the newline state when the buffered data is sent.
      lastSerialOutputWasNewline = true;
  }

  // Update the last logged message for rate limiting and duplicate prevention
  lastLoggedSystemMessageContent = fullFormattedMessageWithoutEnd;
  lastLoggedSystemMessageTime = currentMillis;

  // When BOOT-LOG is enabled, enqueue this message for deferred SD write.
  // Do NOT call logCompactBufferedEvent() here — it allocates char line[256] on the stack,
  // and sendVirtualMessageInternal already has a large frame, causing a stack overflow
  // on the loopTask 8KB stack. We enqueue into a static ring buffer and drain from loop().
  if (level == INFO || level == WARN || level == ERROR || level == CRITICAL_EVENT) {
      if (configManager.getBootLogEnabled()) {
          const char* t = (level == WARN)           ? "FW_WARN"
                        : (level == ERROR)          ? "FW_ERROR"
                        : (level == CRITICAL_EVENT) ? "FW_CRIT"
                        :                             "FW_INFO";
          uint8_t next = (_blqTail + 1) & 7;
          if (next != _blqHead) { // not full — enqueue
              strncpy(_blq[_blqTail].tag, t, 9); _blq[_blqTail].tag[9] = '\0';
              strncpy(_blq[_blqTail].msg, fullFormattedMessageWithoutEnd.c_str(), 199);
              _blq[_blqTail].msg[199] = '\0';
              _blqTail = next;
          }
      }
  }
}

// Thread-safe wrapper for logs
void sendVirtualMessage(const String &msg, int level, bool alreadyFormatted, bool bypassDedup) {
    // Short timeout (100ms) to avoid blocking time-critical tasks.
    if (logLock(pdMS_TO_TICKS(100))) {
        sendVirtualMessageInternal(msg, level, alreadyFormatted, bypassDedup);
        logUnlock();
    } else {
        // If lock fails, silently drop message to avoid heap fragmentation
        // DO NOT call Serial.println here - causes allocations while lock contention
        (void)msg; // Suppress unused warning
    }
}

// Compatibility overload: accept integer log levels from modules that don't see LogLevel
// and forward to the typed version. This keeps older declarations working.
// Removed redundant overload; main function already accepts 'int level'

void manageSerialOutput() {
    // Atomic flag protection for the output buffer consumer.
    if (!logLock(pdMS_TO_TICKS(10))) {
        return; // try again on next loop - lock busy
    }
    bool locked = true;

    int bytesToSend = 128; // 128 bytes per call: fast enough to drain a heartbeat message in one loop()
                          // iteration without monopolizing the CPU (takes ~1ms at 115200 baud,
                          // effectively instant on USB CDC). Was 8, causing severe output delay.
    static uint8_t lastSentByte = 0; //  Stores the last byte actually sent (for logical CRLF)
    static bool atLineStart = true; //  Whether next byte is a logical line start
    while (!printerOutputBuffer.isEmpty() && bytesToSend-- > 0) { //  While data remains and quota lasts
        uint8_t rawByte = printerOutputBuffer.shift(); //  Take next raw byte

        //  Left trimming: at line start ignore successive spaces and tabs
        if (atLineStart) { //  Line start
            if (rawByte == ' ' || rawByte == '\t') { //  Space or tab — discard
                lastSentByte = rawByte; //  Update reference but do not send
                continue; //  Take next byte
            } else if (rawByte == '\r') { // Isolated CR at start — discard (handled when LF arrives)
                lastSentByte = rawByte; //  Store
                continue; //  Next
            } else if (rawByte == '\n') { // Empty line — still emit normal CRLF below
                // falls into the newline block below
            } else {
                atLineStart = false; //  First useful character found
            }
        }

        if (rawByte == '\n') { //  Detected LF marking end of line
            if (lastSentByte != '\r') { //  No CR immediately before
                Serial.write('\r'); // Emit CR to Serial
#if ENABLE_TELNET==1
                if (telnetClient && telnetClient.connected() && telnetAuthenticated) { //  Valid Telnet client
                    telnetClient.write('\r'); // Emit CR to Telnet
                }
#endif
            }
            Serial.write('\n'); // Emit final LF
#if ENABLE_TELNET==1
            if (telnetClient && telnetClient.connected() && telnetAuthenticated) { //  Also send LF to Telnet
                telnetClient.write('\n'); // LF
            }
#endif
            lastSentByte = '\n'; //  Update last byte
            lastSerialOutputWasNewline = true; // Line complete
            atLineStart = true; //  Next line restarts trimming
            continue; //  Next byte
        }

        //  Normal byte (not LF) - send as is
        Serial.write(rawByte); // Serial out
#if ENABLE_TELNET==1
        if (telnetClient && telnetClient.connected() && telnetAuthenticated) { //  Telnet ready
            telnetClient.write(rawByte); // Telnet out
        }
#endif

        if (rawByte == '\r') { //  Isolated CR (we do not know yet if LF follows)
            lastSerialOutputWasNewline = false; //  Not a complete break yet
        } else if (rawByte == '\n') { //  (Would be caught above, redundant for safety)
            lastSerialOutputWasNewline = true; // Line ended
        } else {
            lastSerialOutputWasNewline = false; //  Still inside the line
        }
        lastSentByte = rawByte; //  Memoriza
    }
    
    // Release mutex
    if (locked) logUnlock();
}

String convertAsciiDecimalString(const String &input) {
    String result = "";
    String currentNumStr = "";
    for (int i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        if (c >= '0' && c <= '9') {
            currentNumStr += c;
        } else if (c == ',') {
            if (currentNumStr.length() > 0) {
                result += (char)currentNumStr.toInt();
                currentNumStr = "";
            }
        }
    }
    if (currentNumStr.length() > 0) {
        result += (char)currentNumStr.toInt();
    }
    return result;
}


void manageWiFiConnection() {
    unsigned long currentMillis = millis();

    switch (currentWifiConnectionState) {
        case WIFI_IDLE:
            break;

        case WIFI_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                String ipMsg = String(F("WIFI_OK:"));
                ipMsg += WiFi.localIP().toString();
                sendVirtualMessage(ipMsg, INFO);
                currentWifiConnectionState = WIFI_CONNECTED;
                lastWifiAttemptTime = currentMillis;
                if (configManager.getTelnetEnabled()) startTelnetServer();
            } else if (currentMillis - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT) {
                String wifiErrorMsg = String(F("Falha ao conectar ao WiFi. Status: "));
                wifiErrorMsg += String(WiFi.status());
                if (WiFi.status() == WL_NO_SSID_AVAIL) {
                    wifiErrorMsg += String(F(" (SSID n?o encontrado)"));
                } else if (WiFi.status() == WL_CONNECT_FAILED) {
                    wifiErrorMsg += String(F(" (Falha de conex?o, verifique password)"));
                }
                sendVirtualMessage(String(F("WIFI_FAIL")), ERROR);
                currentWifiConnectionState = WIFI_FAILED;
                lastWifiAttemptTime = currentMillis;
            } else {
                if (currentMillis - lastWifiAttemptTime > 1000) {
                    sendVirtualMessage(String(F("Connecting to WiFi...")), INFO);
                    lastWifiAttemptTime = currentMillis;
                }
            }
            break;

        case WIFI_CONNECTED:
            if (currentMillis - lastWifiAttemptTime > WIFI_RETRY_INTERVAL) {
                if (WiFi.status() != WL_CONNECTED) {
                    sendVirtualMessage(String(F("WiFi disconnected. Trying to reconnect...")), WARN);
                    currentWifiConnectionState = WIFI_CONNECTING;
                    
                    // CRITICAL: Ensure WiFi mode before reconnect to avoid TCP/IP crash
                    if (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) {
                        WiFi.mode(WIFI_STA);
                        delay(500);
                    }
                    
                    wifiReconnectCount++; // 📊 Increment reconnection counter
                    WiFi.begin(ssid.c_str(), password.c_str());
                    wifiConnectStartTime = currentMillis;
                }
                lastWifiAttemptTime = currentMillis;
            }
            break;

        case WIFI_FAILED:
            if (currentMillis - lastWifiAttemptTime > WIFI_RETRY_INTERVAL) {
                sendVirtualMessage(String(F("Reconnecting to WiFi after failure...")), INFO);
                wifiReconnectCount++; // 📊 Increment reconnection counter
                WiFi.begin(ssid.c_str(), password.c_str()); 
                wifiConnectStartTime = currentMillis;
                currentWifiConnectionState = WIFI_CONNECTING;
            }
            break;
    }
}