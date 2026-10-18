// rs232_misc_impl.h
// Included only from chatgpt.ino (single translation unit).

// pushSampleLineToWebMonitor removed ? no-op stub to avoid breaking references
void pushSampleLineToWebMonitor(const String &line) {
    // Intentionally empty: sample pushing to web monitor disabled.
    (void)line;
}

// UTF-8 decoder implementation
// Character set conversion functions (decodeUtf8Byte, unicodeToLatin1, unicodeToCP850)
// moved to src/charset_conversion.cpp for better modularity

// Printer emulation functions (processGenericCommand, processEpsonCommand, processIBMCommand)
// moved to src/printer_emulation.cpp for better modularity

void handleDecodedCodepoint(uint32_t codepoint) {
    // NEW BEHAVIOR: LPT data goes to BOTH bufferDadosLPT (email) AND bufferDados (web visualization).
    // RS232 data goes to bufferDados only (SD + email + web visualization).
    // LPT data: direct insertion without timestamp (per user request).
    if (codepoint == 0 || codepoint == 0xFFFD) { // Invalid or incomplete codepoint
        return;
    }

    bool storedInEmailBuffer = false;
    bool droppedForCapacity = false;
    size_t emailBufferSize = 0;
    size_t emailBufferCapacity = 0;

    // Determine data source
    bool isLPTData = lastByteFromDb25; // LPT/DB25 origin flag

    // Track if we're at the start of a new line (for inline timestamp injection)
    static bool atLineStart = true;
    
    // Inject inline timestamp at the start of each line if enabled
    // Only for RS232 data (LPT does not get timestamps per user request)
    bool shouldAddTimestamp = !isLPTData && atLineStart && configManager.getEmailIncludeTimestamp();
    if (shouldAddTimestamp) {
        time_t nowTs = time(nullptr);
        if (nowTs > 100000) { // Valid NTP timestamp
            struct tm timeinfo;
            localtime_r(&nowTs, &timeinfo);
            char tsBuf[24]; // YYYY-MM-DD HH:MM:SS\t + null
            int tsLen = strftime(tsBuf, sizeof(tsBuf) - 1, "%Y-%m-%d %H:%M:%S", &timeinfo);
            if (tsLen > 0) {
                tsBuf[tsLen] = '\t'; // Add tab separator
                tsBuf[tsLen + 1] = '\0';
                
                // Write timestamp bytes directly to bufferDados
                bool didLock = false;
                if (bufferDadosLock(pdMS_TO_TICKS(5), didLock)) {
                    size_t cap = bufferDados.capacity();
                    size_t sz = bufferDados.size();
                    size_t free = (cap > sz) ? (cap - sz) : 0;
                    if (free >= (size_t)(tsLen + 1)) { // +1 for tab
                        for (int i = 0; i <= tsLen; i++) { // Include tab
                            bufferDados.push((uint8_t)tsBuf[i]);
                        }
                        bufferDadosSeq = bufferDadosSeq + 1;
                    }
                    bufferDadosUnlock(didLock);
                }
            }
        }
        atLineStart = false; // No longer at line start
    }

    // Encode UTF-8 bytes once to avoid duplication
    uint8_t utf8Bytes[4];
    size_t utf8ByteCount = 0;
    
    if (codepoint < 0x80) {
        utf8Bytes[utf8ByteCount++] = (uint8_t)codepoint;
    } else if (codepoint < 0x800) {
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0xC0 | (codepoint >> 6));
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0xE0 | (codepoint >> 12));
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x110000) {
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0xF0 | (codepoint >> 18));
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8Bytes[utf8ByteCount++] = (uint8_t)(0x80 | (codepoint & 0x3F));
    } else {
        // Invalid codepoint, push '?'
        utf8Bytes[utf8ByteCount++] = 0x3F;
    }

    // LPT data: write to BOTH bufferDadosLPT (email) and bufferDados (web visualization)
    // RS232 data: write to bufferDados only
    if (isLPTData) {
        // Write to LPT email buffer
        bool didLockLPT = false;
        if (bufferDadosLPTLock(pdMS_TO_TICKS(5), didLockLPT)) {
            emailBufferCapacity = bufferDadosLPT.capacity();
            emailBufferSize = bufferDadosLPT.size();
            const size_t freeSlots = (emailBufferCapacity > emailBufferSize) ? (emailBufferCapacity - emailBufferSize) : 0;
            if (emailBufferCapacity > 0 && freeSlots >= utf8ByteCount) {
                for (size_t i = 0; i < utf8ByteCount; i++) {
                    bufferDadosLPT.push(utf8Bytes[i]);
                }
                storedInEmailBuffer = true;
            } else {
                droppedForCapacity = (emailBufferCapacity > 0);
            }
            emailBufferSize = bufferDadosLPT.size();
            bufferDadosLPTUnlock(didLockLPT);
        } else {
            bufferDadosLPTPushMutexFailCount = bufferDadosLPTPushMutexFailCount + 1;
        }

        // ALSO write to main visualization buffer (bufferDados) for web interface
        bool didLockMain = false;
        if (bufferDadosLock(pdMS_TO_TICKS(5), didLockMain)) {
            size_t mainCap = bufferDados.capacity();
            size_t mainSize = bufferDados.size();
            const size_t mainFree = (mainCap > mainSize) ? (mainCap - mainSize) : 0;
            if (mainCap > 0 && mainFree >= utf8ByteCount) {
                for (size_t i = 0; i < utf8ByteCount; i++) {
                    bufferDados.push(utf8Bytes[i]);
                }
                bufferDadosSeq = bufferDadosSeq + 1; // Update sequence for web polling
            }
            bufferDadosUnlock(didLockMain);
        } else {
            bufferDadosPushMutexFailCount = bufferDadosPushMutexFailCount + 1;
        }
    } else {
        // RS232 data: write to bufferDados only (main buffer for web + SD)
        bool didLock = false;
        if (bufferDadosLock(pdMS_TO_TICKS(5), didLock)) {
            emailBufferCapacity = bufferDados.capacity();
            emailBufferSize = bufferDados.size();
            const size_t freeSlots = (emailBufferCapacity > emailBufferSize) ? (emailBufferCapacity - emailBufferSize) : 0;
            if (emailBufferCapacity > 0 && freeSlots >= utf8ByteCount) {
                for (size_t i = 0; i < utf8ByteCount; i++) {
                    bufferDados.push(utf8Bytes[i]);
                }
                bufferDadosSeq = bufferDadosSeq + 1; // Update sequence for web polling
                storedInEmailBuffer = true;
            } else {
                droppedForCapacity = (emailBufferCapacity > 0);
            }
            emailBufferSize = bufferDados.size();
            bufferDadosUnlock(didLock);
        } else {
            bufferDadosPushMutexFailCount = bufferDadosPushMutexFailCount + 1;
        }
    }

    // Line counting logic for email buffer (CRLF deduplication specific to DB25)
    // LPT data uses bufferDadosLPTLineCount, RS232 keeps using bufferDadosLineCount
    // Goal: DB25 may send CR+LF; we count only one line per pair.
    // Rule:
    //   - If source is DB25 (lastByteFromDb25=true):
    //       On '\r': count 1 line and set db25SkipNextLF=true to ignore the immediately following LF.
    //       On '\n' with db25SkipNextLF=true: ignore this break (do not increment), clear the flag.
    //       On '\n' without db25SkipNextLF: count 1 line (case where LF-only comes from the emitter).
    //   - If source is NOT DB25 (RS232): do not touch here — RS232 already has its own upstream counting FSM.
    if (storedInEmailBuffer && (codepoint == '\n' || codepoint == '\r')) {
        bool countedLine = false; // Indicates if we incremented in this iteration
        bool isLPTData = lastByteFromDb25; // Cache origin flag
        
        if (isLPTData) {
            // LPT/DB25 line counting with CRLF deduplication
            if (codepoint == '\r') {
                // DB25: CR counts as a new line and we arm to ignore the next LF
                db25SkipNextLF = true; // Ignore next LF to avoid double counting
                bufferDadosLPTLineCount++; // Use LPT-specific counter
                countedLine = true;
            } else if (codepoint == '\n') {
                if (db25SkipNextLF) {
                    // This LF is part of the already-counted CRLF — ignore
                    db25SkipNextLF = false; //  Clear after consuming the paired LF
                    countedLine = false;    //  Do not increment
                } else {
                    // Lone LF from DB25: count
                    bufferDadosLPTLineCount++; // Use LPT-specific counter
                    countedLine = true;
                }
            }
        } else {
            // RS232: do not change counting here (already handled in the RS232 pipeline);
            // but keep the activity semantics and sample-line cleanup below.
        }

        //  Fire on line limit only when we actually incremented
        if (countedLine) {
            int currentLineCount = isLPTData ? bufferDadosLPTLineCount : bufferDadosLineCount;
            if (currentLineCount >= currentMaxEmailLines) {
                autoEmailTriggeredByNewline = true;
                //  Store trigger source (DB25 vs RS232)
                lastTriggerFromDb25 = isLPTData;
                //  LPT: do NOT assert BUSY immediately here — apply hysteresis via FSM
                // RS232 keeps its own policy (chunk/micro-pause)
                safeEnviarMensagemVirtual("TRG: limite linhas=%u (fonte:%s)", DEBUG, (unsigned)currentLineCount, isLPTData ? "LPT" : "RS232");
            }
            // RS232 chunk/micro-pause mode disabled; BUSY/XOFF is driven only by buffer high-water.
        }

        //  Update activity timestamp and clear the sample line (both \n/\r)
        if (currentReceivedLine.length() > 0) {
            lastTransmissionActivityMillis = millis();
            currentReceivedLine = "";
        } else {
            lastTransmissionActivityMillis = millis();
        }
    }

    // RESTORED: Only replace null bytes with space for display on the screen
    uint32_t charForDisplay = codepoint;
    if (charForDisplay == 0x00) { 
        charForDisplay = 0x20; // Substitui null por espa?o
    }

    // Accumulate characters for the sample line (display only, does not change bufferDados)
    if (charForDisplay != '\n' && charForDisplay != '\r') {
        currentReceivedLine += (char)charForDisplay;
        // limit length to avoid excessive memory use
        if (currentReceivedLine.length() > 200) currentReceivedLine = currentReceivedLine.substring(0,200);
    }

    // For the printer output buffer (Serial/Telnet), process
    // the command based on the selected emulation mode
    uint8_t processedByte = 0;
    bool shouldOutput = false;
    
    switch (printerEmulationMode) { // Choose emulation mode
        case PRINTER_MODE_GENERIC: //  Generic mode
            shouldOutput = processGenericCommand(charForDisplay, processedByte); //  Process generic command
            break;
        case PRINTER_MODE_EPSON: // Epson mode
            shouldOutput = processEpsonCommand(charForDisplay, processedByte); //  Process Epson command
            break;
        case PRINTER_MODE_IBM: // IBM mode
            shouldOutput = processIBMCommand(charForDisplay, processedByte); //  Process IBM command
            break;
    }
    
    // If the command should be output, convert to the selected charset
    if (shouldOutput) {
        if (charsetMode == 0) { // AUTO / UTF-8 Passthrough
            // Push UTF-8 bytes directly to the printer output buffer
            if (charForDisplay < 0x80) {
                printerOutputBuffer.push((uint8_t)charForDisplay);
            } else if (charForDisplay < 0x800) {
                printerOutputBuffer.push((uint8_t)(0xC0 | (charForDisplay >> 6)));
                printerOutputBuffer.push((uint8_t)(0x80 | (charForDisplay & 0x3F)));
            } else if (charForDisplay < 0x10000) {
                printerOutputBuffer.push((uint8_t)(0xE0 | (charForDisplay >> 12)));
                printerOutputBuffer.push((uint8_t)(0x80 | ((charForDisplay >> 6) & 0x3F)));
                printerOutputBuffer.push((uint8_t)(0x80 | (charForDisplay & 0x3F)));
            } else if (charForDisplay < 0x110000) {
                printerOutputBuffer.push((uint8_t)(0xF0 | (charForDisplay >> 18)));
                printerOutputBuffer.push((uint8_t)(0x80 | ((charForDisplay >> 12) & 0x3F)));
                printerOutputBuffer.push((uint8_t)(0x80 | ((charForDisplay >> 6) & 0x3F)));
                printerOutputBuffer.push((uint8_t)(0x80 | (charForDisplay & 0x3F)));
            } else {
                printerOutputBuffer.push(0x3F); // Replacement character
            }
        } else if (charsetMode == 1) { // Sa?da Latin-1
            printerOutputBuffer.push(unicodeToLatin1(charForDisplay));
        } else if (charsetMode == 2) { // Sa?da CP850
            printerOutputBuffer.push(unicodeToCP850(charForDisplay));
        }
    }

    lastReceptionTime = millis(); // Update timestamp of last data reception
    lastDataReceivedForInactivityAlert = millis(); // Update for inactivity alert (NEW data from LPT/DB25)
    inactivityAlertSent = false; // Reset flag to allow new alerts after activity resumes
    for (int _i = 0; _i < ConfigManager::ALERT_SLOT_COUNT; ++_i) inactivityAlertSentSlot[_i] = false;

    // DIAGNOSTIC: Log inactivity timestamp update (rate-limited to avoid spam)
    static unsigned long lastInactivityUpdateLogLPT = 0;
    if (millis() - lastInactivityUpdateLogLPT > 15000) { // Log every 15s for better visibility
        char logBuf[100];
        snprintf(logBuf, sizeof(logBuf), "✅ [INACTIVITY] Timer RESET by LPT/DB25 data (ts=%lu)", lastDataReceivedForInactivityAlert);
        sendVirtualMessage(String(logBuf), INFO);
        lastInactivityUpdateLogLPT = millis();
    }
    
    bufferClearedOnInactivity = false;

    // L?gica de busy e envio de email baseada no bufferDados
    if (droppedForCapacity) {
        sendVirtualMessage(String(F("? ERRO CR?TICO: Buffer de dados de email cheio, byte descartado! ?")), CRITICAL_EVENT);
        if (!emulatorShouldBeBusy) { // Only set busy if not already busy
            setBusyPinState(true);
            emulatorShouldBeBusy = true;
            pauseInternalBufferTransfer = true;
            isBusyCounting = true;
            busyBytesCounter = 0;
            sendVirtualMessage(String(F("BUSY activated: byte discard")), CRITICAL_EVENT);
            if (enableDebugLogs) {
                safeEnviarMensagemVirtual("DEBUG_BUSY: bufferRx (write/read): %u/%u", DEBUG, (unsigned)writePos, (unsigned)readPos);
            }
        }
    // If the RS232 emitter supports XON/XOFF, pause immediately to avoid further losses
        if (isFlowControlEngaged()) {
            //  Enable chunk mode, as we are coordinating an XOFF pause
            if (!rs232ChunkPauseActive) rs232ChunkPauseActive = true;
            //  Send XOFF only once if not already paused
            if (!rs232_paused_by_controller) {
                sendRs232Xoff();         // Lock RS232 emitter via software flow control
                rs232PauseStartMillis = millis(); // Store timestamp for cut windows/force
            }
        }
        //  Mirror BUSY to the Nano: send XOFF so it propagates upstream
        nanoBridgeSendXoff();
        // ALWAYS set shouldSendEmailImmediately if the buffer is full, regardless of autoSendEmail
        safeEnviarMensagemVirtual("Buffer de email cheio! Envio imediato sinalizado.", WARN);
        shouldSendEmailImmediately = true; 
        //  Store trigger source (buffer full)
        lastTriggerFromDb25 = lastByteFromDb25;
        return; // Return without adding the byte to bufferDados if already full
    }

    // This condition should also trigger email sending.
    {
        if (emailBufferSize >= BUSY_ACTIVATION_THRESHOLD_BYTES) {
        if (!emulatorShouldBeBusy) { // Only set busy if not busy yet
            setBusyPinState(true);
            emulatorShouldBeBusy = true;
            isBusyCounting = true;
            busyBytesCounter = 0;
            safeEnviarMensagemVirtual("Email buffer reached %u bytes. BUSY activated.", WARN, (unsigned)BUSY_ACTIVATION_THRESHOLD_BYTES);
            if (enableDebugLogs) {
                safeEnviarMensagemVirtual("DEBUG_BUSY: bufferRx (write/read): %u/%u", DEBUG, (unsigned)writePos, (unsigned)readPos);
            }
        }
        //  RS232: also mirror BUSY on the RS232 channel with XOFF when flow control is engaged
        if (isFlowControlEngaged()) {
            //  Signal chunk mode for the FSM to manage cut/resume
            if (!rs232ChunkPauseActive) rs232ChunkPauseActive = true;
            //  Pause the emitter only if not already paused
            if (!rs232_paused_by_controller) {
                sendRs232Xoff();          // Pause the RS232 emitter
                rs232PauseStartMillis = millis(); //  Timestamp for force timeouts
            }
        }
        //  Mirror BUSY to the Nano: send XOFF so it propagates upstream
        nanoBridgeSendXoff();
        // Set shouldSendEmailImmediately when the limit is reached
        shouldSendEmailImmediately = true;
        }
    }

    if (isBusyCounting) {
        busyBytesCounter = busyBytesCounter + 1;
    }
    
    // Reset line start flag after newline for inline timestamp injection
    // (atLineStart is declared as static at function entry)
    if (codepoint == '\n') {
        atLineStart = true; // Next write will be at line start
    }
    
    // The wasLastByteNewline variable is no longer used to control newlines for system messages.
    // System-message newline logic is now driven by lastSerialOutputWasNewline.
    // This line may be removed if 'wasLastByteNewline' is not used anywhere else.
    // For now, keep it, but with a note.
    // wasLastByteNewline = (codepoint == '\n' || codepoint == '\r'); 

    lastPrinterDataTime = millis();
}


void printUtf8(uint8_t c) {
    // This function is used only for Serial.write in some contexts,
    // but the main output logic now lives in handleDecodedCodepoint and manageSerialOutput.
    // This function should not be called directly for main serial output.
    if (c < 0x80) {
        Serial.write(c);
    } else {
        Serial.write(0xC0 | (c >> 6));
        Serial.write(0x80 | (c & 0x3F));
    }
    // Do not update lastSerialOutputWasNewline here, as this function is auxiliary and not the main output point.
}

// handleStrobeInterrupt() (legacy) no longer exists - UART bridge replaces the STROBE ISR

// Telnet server: see src/telnet_server.cpp

// ======================================
#if ENABLE_RS232==1
// SISTEMA RS232 - ENVIO DE COMANDOS
// ======================================

bool enviarComandoRS232(const String &comando) {
    // AUTO-ACTIVATE: If RS232 is disabled, enable it automatically
    if (!rs232_enabled) {
    // Silenced on the monitor: only enables RS232 with no visual response
        
        // Enable RS232
        rs232_enabled = true;
        
        // RS232 pins: Waveshare header RXD=GPIO44, TXD=GPIO43. invert_pins swaps RX/TX.
        if (rs232_invert_pins) {
            rs232_rx_pin = RS232_DEFAULT_TX_PIN;
            rs232_tx_pin = RS232_DEFAULT_RX_PIN;
        } else {
            rs232_rx_pin = RS232_DEFAULT_RX_PIN;
            rs232_tx_pin = RS232_DEFAULT_TX_PIN;
        }
        
        // Inicializar RS232
        setRS232BaudrateByIndex(rs232_baudrate_index);
        // Apply full configuration (baud/framing/pins) - do not hardcode SERIAL_8N1
        applyRS232Config();
        lastRs232ConfigMillis = millis();
        
        // Persist active RS232 config (enabled state, baudrate, pin inversion).
        configManager.setRs232Enabled(rs232_enabled);
        configManager.setRs232BaudrateIndex(rs232_baudrate_index);
        configManager.setRs232InvertPins(rs232_invert_pins);
        
    // Silenced on monitor: activation details omitted
        
        // Small delay to stabilize the connection and drain RX
        const unsigned long RS232_ENABLE_SETTLE_MS = 200; // Stabilization after begin()
        unsigned long _enStart = millis();
        while (millis() - _enStart < RS232_ENABLE_SETTLE_MS) {
            if (_rs232PortInitialized) {
                while (RS232.available()) { (void)RS232.read(); lastReceptionTime = millis(); }
            }
            delay(1);
        }
    }
    
    if (comando.isEmpty()) {
    // Silenced on the monitor: failure is not logged
        return false;
    }
    
    // Prepare command for sending
    String comandoParaEnviar = comando;
    
    // Add terminator if missing
    if (!comandoParaEnviar.endsWith("\r\n") && !comandoParaEnviar.endsWith("\n") && !comandoParaEnviar.endsWith("\r")) {
        comandoParaEnviar += "\r\n";
    }
    
    // Robust send: in blocks, watching XON/XOFF and UART buffer capacity
    //  Local pacing parameters
    const size_t RS232_TX_CHUNK_BYTES = 64;         // Max size of each write
    const unsigned long RS232_PAUSE_TIMEOUT_MS = 15000UL; //  Max wait time for XON
    const unsigned long RS232_INACTIVITY_GRACE_MS = 1500UL; //  Grace window after TX

    //  Honor flow control (global or imposed by template in RAM)
    const bool useFlow = (configManager.getRs232UseXonXoff() || rs232_template_use_xon_xoff);

    // Block sends using availableForWrite to avoid filling the buffer and allow reaction to XOFF
    const char* data = comandoParaEnviar.c_str();
    size_t totalLen = comandoParaEnviar.length();
    size_t idx = 0;

    // Enable first-response guard: protects against premature XOFF on the first block after send
    rs232FirstResponseGuardActive = true;
    rs232FirstResponseGuardUntil = millis() + max((unsigned long)RS232_FIRST_RESPONSE_GUARD_MS, (unsigned long)rs232_read_timeout_ms/2);
    rs232LinesSinceLastTx = 0;
    //  Provisionally raise the line minimum for the first block (avoids cutting send 1?)
    rs232FirstResponseMinLines = max(RS232_FIRST_RESPONSE_MIN_LINES, 120);

    while (idx < totalLen) {
        //  If remote XOFF was received, wait for XON with safety timeout
        if (useFlow && rs232_paused_by_controller) {
            unsigned long waitStart = millis();
            while (rs232_paused_by_controller) {
                if ((millis() - waitStart) > RS232_PAUSE_TIMEOUT_MS) {
                    sendVirtualMessage(F("RS232: XOFF pause exceeded timeout (abandoning send)."), WARN);
                    return false; // Abort to avoid blocking the loop
                }
                // Reset watchdog during long XOFF wait (can be up to 15s)
                esp_task_wdt_reset();
                delay(1); //  Yield CPU and let RX process XON
            }
        }

        if (!_rs232PortInitialized) {
            // Port was closed during send - abort gracefully
            return false;
        }

        int avail = RS232.availableForWrite();
        if (avail <= 0) { delay(1); continue; } //  Wait for space in the UART buffer

        size_t toWrite = totalLen - idx;
        if (toWrite > (size_t)avail) toWrite = (size_t)avail; //  Limit to available space
        if (toWrite > RS232_TX_CHUNK_BYTES) toWrite = RS232_TX_CHUNK_BYTES; //  Limit per chunk

        size_t written = RS232.write((const uint8_t*)data + idx, toWrite);
        if (written == 0) { delay(1); continue; } //  Retry
        idx += written;

        //  Update activity and grace window after each block
        rs232LastTxMillis = millis();
        lastReceptionTime = rs232LastTxMillis;
        rs232InactivityGraceUntil = rs232LastTxMillis + RS232_INACTIVITY_GRACE_MS;
    }

    //  Send complete
    //  We do not deactivate the guard here — it only ends when RX detects enough lines or time expires
    // Silenced on monitor: do not log RS232 sends
    // Trigger temporary UI screen to inform user that RS232 command was sent
    triggerScreenCommandSent(comando, "RS232"); //  Pass real command

    return true;
}

// Software flow control helpers (XON/XOFF)
void sendRs232Xoff() {
    // 0x13 is XOFF (DC3)
    if (!isFlowControlEngaged()) return; // Only engage when allowed (config/template) or recent auto-detection
    if (rs232_paused_by_controller) return; // already paused
    if (!rs232_enabled) return;
    RS232.write((uint8_t)0x13);
    RS232.flush();
    rs232_paused_by_controller = true;
    sendVirtualMessage(String(F("RS232: XOFF")), INFO);
}

void sendRs232Xon() {
    // 0x11 is XON (DC1)
    if (!isFlowControlEngaged()) return; // Only engage when allowed (config/template) or recent auto-detection
    if (!rs232_paused_by_controller) return; // not paused
    if (!rs232_enabled) return;
    RS232.write((uint8_t)0x11);
    RS232.flush();
    rs232_paused_by_controller = false;
    sendVirtualMessage(String(F("RS232: XON")), INFO);
}

// ======================================
// XON/XOFF RAW (without changing local flags)
// ======================================
void sendRs232XoffRaw() {
    if (!rs232_enabled) return;
    RS232.write((uint8_t)0x13);
    RS232.flush();
    // ? Tamb?m bloquear a fila localmente ? estamos a pausar o emissor e n?o queremos novos envios at? XON
    rs232_paused_by_controller = true;
}

void sendRs232XonRaw() {
    if (!rs232_enabled) return;
    RS232.write((uint8_t)0x11);
    RS232.flush();
    // ? Retomar fila local
    rs232_paused_by_controller = false;
}

// ================================
// Heuristic for XON/XOFF engagement
// ================================
bool isFlowControlEngaged() {
    // 1) Explicit configuration (global) or active template
    if (configManager.getRs232UseXonXoff() || rs232_template_use_xon_xoff) return true;
    // 2) Immediate auto-detection: if allowed and we have seen XON/XOFF from the remote, cooperate immediately
    if (rs232AutoFlowDetectEnabled && rs232RemoteSendsXonXoff) return true;
    return false;
}
#endif // ENABLE_RS232

// ======================================
// SISTEMA DE COMANDOS AGENDADOS RS232
// ======================================

unsigned long getIntervalInMillis(int interval, const String &unit) {
    if (unit == "seconds") {
        return interval * 1000UL;
    } else if (unit == "minutes") {
        return interval * 60UL * 1000UL;
    } else if (unit == "hours") {
        return interval * 60UL * 60UL * 1000UL;
    }
    return interval * 60UL * 1000UL; // Default: minutes
}

bool shouldExecuteCommand(const ScheduledCommand &cmd, unsigned long currentTime) {
#if ENABLE_SCHEDULER==1
    if (!cmd.enabled) return false;
    
    // Check time interval
    unsigned long intervalMs = getIntervalInMillis(cmd.interval, cmd.unit);
    if (currentTime - cmd.lastExecuted < intervalMs) {
        return false;
    }
    
    // Check weekday
    if (cmd.days == "everyday") {
        return true;
    }
    
    // For verification of specific days we need an RTC
    // For now, assume "everyday" to simplify
    // TODO: Implement specific day checks when RTC is configured
    
    return true;
#else
    (void)cmd;
    (void)currentTime;
    return false;
#endif
}

void processScheduledCommands() { //  Legacy scheduled_commands removed - stub kept for compatibility
    //  Empty function: scheduling logic now uses sched_cmd/sched_dt slots in other blocks
    return; //  No action
}

void clearPrinterDataBufferAndNotifyUI() {
    //  Safeguard: do not clear if we are receiving data
    if (isReceivingData) { //  Protect integrity of receiving data
    sendVirtualMessage(String(F("Clear postponed: reception active.")), WARN); // Feedback to UI/log
        bufferClearedOnInactivity = false; //  Do not mark as cleared
        return; //  Exit without clearing
    }

    //  Detetar trabalho pendente que dependa do buffer ou fluxo associado
    bool rs232Pending = rs232QueueHasPending();
    bool hasPendingQueues = rs232Pending || (!printerOutputBuffer.isEmpty()); //  RS232/printer-output queue
    bool busyFlow = rs232_sending || emulatorShouldBeBusy; //  Busy flow (RS232 sending or logical BUSY)

    //  Safeguard: do not clear if there is pending work to handle
    if (hasPendingQueues || busyFlow) { //  Avoid clearing with pending work
    sendVirtualMessage(String(F("Clear postponed: pending work.")), WARN); // Feedback
        bufferClearedOnInactivity = false; //  Do not mark as clean
        return; //  Exit without clearing
    }

    //  Safeguard: if there is still data in the buffer, consider it pending and do not clear.
    {
        bool empty = false;
        if (!bufferDadosTryIsEmpty(empty, pdMS_TO_TICKS(20))) {
            sendVirtualMessage(String(F("Clear postponed: buffer lock busy.")), WARN);
            bufferClearedOnInactivity = false;
            return;
        }
        if (!empty) {
            sendVirtualMessage(String(F("Clear postponed: pending data.")), WARN);
            bufferClearedOnInactivity = false;
            return;
        }
    }

    //  Perform the safe clearing (buffer is empty and no pending work)
    if (!bufferDadosTryClear(pdMS_TO_TICKS(50))) {
        sendVirtualMessage(String(F("Clear failed: buffer lock busy.")), WARN);
        bufferClearedOnInactivity = false;
        return;
    }
    bufferDadosLineCount = 0; //  Reset line count
    printerOutputBuffer.clear(); //  Clear output buffer associated with screen
    sendVirtualMessage(String(F("BUFFER_CLEARED_BY_EMULADOR")), COMMAND_RESPONSE); // Notify canonical event
    sendVirtualMessage(String(F("Printer data buffer cleared (automatic or by command).")), DEBUG); // Detailed log
    bufferClearedOnInactivity = true; // Mark as cleared by routine/action
}

// enqueueRS232Command moved to src/runtime_rs232_queue.cpp (Phase 5 modularization).
// Declaration is in src/runtime_rs232_queue.h (included below).

// Function to check TCP/IP stack health before starting the web server
bool checkTcpIpHealth() {
    // FIX: Verify WiFi.mode is configured correctly (including pure AP)
    wifi_mode_t currentMode = WiFi.getMode();
    bool ethernetConnected = false;
    IPAddress ethernetIp(0, 0, 0, 0);
#if ENABLE_ETHERNET==1
    ethernetIp = ethernetLocalIP();
    ethernetConnected = (ethernetIp != IPAddress(0, 0, 0, 0));
#endif

    if (currentMode != WIFI_STA && currentMode != WIFI_AP_STA && currentMode != WIFI_AP) {
        sendVirtualMessage(String(F("TCP/IP Health: Incorrect WiFi mode: ")) + String(currentMode), ERROR);
        sendVirtualMessage(F("ABORTING to avoid 'Invalid mbox' crash"), ERROR);
        return false;
    }
    
    // FIX: Check connectivity based on WiFi mode
    if (currentMode == WIFI_AP) {
        // Pure AP mode - check if AP is active
        IPAddress apIP = WiFi.softAPIP();
        if (apIP == IPAddress(0,0,0,0)) {
            sendVirtualMessage(F("TCP/IP Health: Access Point has no valid IP"), ERROR);
            return false;
        }
    sendVirtualMessage(String(F("TCP/IP Health: AP OK - IP: ")) + apIP.toString(), INFO);
        
        // Delay to allow TCP/IP stack stabilization
        delay(500);
        return true;
        
    } else if (currentMode == WIFI_AP_STA) {
        // AP+STA mode - check both
        IPAddress apIP = WiFi.softAPIP();
        if (apIP == IPAddress(0,0,0,0)) {
            sendVirtualMessage(F("? TCP/IP Health: Access Point sem IP v?lido"), ERROR);
            return false;
        }
        
        // For AP+STA, allow STA to be disconnected
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress staIP = WiFi.localIP();
            sendVirtualMessage(String(F("TCP/IP Health: AP+STA OK - AP: ")) + apIP.toString() + String(F(", STA: ")) + staIP.toString(), INFO);
        } else {
            sendVirtualMessage(String(F("TCP/IP Health: AP OK (STA disconnected) - AP: ")) + apIP.toString(), INFO);
        }
        
        // Delay to allow TCP/IP stack stabilization
        delay(500);
        return true;
        
    } else {
        // STA mode only - check STA connectivity or accept Ethernet as active transport.
        if (WiFi.status() != WL_CONNECTED) {
            if (ethernetConnected) {
                sendVirtualMessage(String(F("TCP/IP Health: Ethernet OK - IP: ")) + ethernetIp.toString(), INFO);
                delay(500);
                return true;
            }
            sendVirtualMessage(F("TCP/IP Health: WiFi STA not connected"), ERROR);
            return false;
        }
        
        // Check we have a valid STA IP
        IPAddress localIP = WiFi.localIP();
        if (localIP == IPAddress(0, 0, 0, 0)) {
            sendVirtualMessage(F("TCP/IP Health: STA IP invalid"), ERROR);
            return false;
        }
        
        // Check whether Gateway is available
        IPAddress gateway = WiFi.gatewayIP();
        if (gateway == IPAddress(0, 0, 0, 0)) {
            sendVirtualMessage(F("TCP/IP Health: Gateway not found (but continuing)"), WARN);
        }
        
        // Delay to allow TCP/IP stack stabilization
        delay(1000);
        
        // Check again after delay
        if (WiFi.status() != WL_CONNECTED) {
            sendVirtualMessage(F("TCP/IP Health: WiFi disconnected during check"), ERROR);
            return false;
        }
        
        // Check whether RSSI is reasonable
        int rssi = WiFi.RSSI();
        if (rssi < -90) {
            sendVirtualMessage(String(F("TCP/IP Health: RSSI muito baixo: ")) + String(rssi), WARN);
            sendVirtualMessage(F("Connection may be unstable but continuing"), WARN);
        }
        
    sendVirtualMessage(String(F("TCP/IP Health: STA OK - IP: ")) + localIP.toString() + String(F(", RSSI: ")) + String(rssi) + String(F(", TCP/IP Stack: Initialized")), INFO);
        return true;
    }
}







