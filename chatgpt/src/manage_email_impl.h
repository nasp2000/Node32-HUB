// manage_email_impl.h
// Included only from chatgpt.ino (single TU).
// Email sending state machine: manageEmailSending().
// DO NOT add #pragma once - this is an impl-include file, not a shared header.

// Build attachment filename with current timestamp: HHhMMmSSs_YYYY-MM-DD.txt
static String buildAttachmentFilename() {
    time_t now = time(nullptr);
    struct tm info;
    localtime_r(&now, &info);
    char fname[40];
    strftime(fname, sizeof(fname), "%Hh%Mm%Ss_%Y-%m-%d.txt", &info);
    return String(fname);
}

void manageEmailSending() {
    unsigned long currentMillis = millis();
    //  Detect whether ISQ20 structures are active (used to decide line budgets)
    bool isq20Active = (configManager.getStructuresAutoDetectEnabled() || configManager.getAmadaISQ20MTF1Enabled() || configManager.getAmadaISQ20AWSEEnabled());
    String searchContent;

    //  Telemetria opcional curta: remoto XON/XOFF
    static unsigned long _lastXonxoffReport = 0;
    if (enableDebugLogs && (millis() - _lastXonxoffReport) > 5000UL) {
        if (rs232RemoteSendsXonXoff) {
            safeEnviarMensagemVirtual("DBG XONXOFF on=%u off=%u dtOn=%u dtOff=%u", DEBUG,
                (unsigned)rs232RemoteXonCount, (unsigned)rs232RemoteXoffCount,
                (unsigned)(millis() - rs232LastRemoteXonMs), (unsigned)(millis() - rs232LastRemoteXoffMs));
        }
        _lastXonxoffReport = millis();
    }

    // Log curto de entrada de manageEmailSending, apenas se o estado mudou
    if (currentEmailSendingState != lastLoggedEmailState) {
        safeEnviarMensagemVirtual("ST: enter=%d init=%s", DEBUG,
            (int)currentEmailSendingState, (emailProcessInitiatedThisCycle ? "true" : "false"));
        lastLoggedEmailState = currentEmailSendingState;
    }


    switch (currentEmailSendingState) {
    case EMAIL_IDLE: {
            // ? S? resetar flag global se n?o houver tarefas em background ativas (EMAILDAY/TESTE)
            if (!emailDayInProgress && !emailTestInProgress) {
                isEmailSendingActive = false; 
            } else {
                // If manual sending is in progress, do not process automatic triggers now
                break;
            }
        //  Ensure queue unlock if some previous cycle did not clear it
        rs232_blockQueueForEmail = false;

            bool canSendEmail = isEmailConfiguredAndWifiConnected();
            bool triggerEmail = false;
            EmailTriggerSource tempTriggerSource = TRIGGER_NONE;
            
            //  SD CARD + EMAIL STREAMING: Disable automatic triggers if SD is active AND Email is active (streaming mode)
            // Revised logic to ensure separation of processes:
            // 1. SD ON + Email ON (Streaming): RAM triggers (Line Limit/Inactivity) OFF. SD handles the partial send.
            // 2. SD ON + Email OFF (Log Only): Triggers RAM OFF. Apenas grava SD.
            // 3. SD OFF + Email OFF: Triggers RAM ON (Modo Auto Email cl?ssico).
            // 4. SD OFF + Email ON (Wait, UI doesn't allow this? Assuming user meant Email Switch in SD box controls streaming)

            // Let's use the ConfigManager flags
            bool sdCardActive = configManager.getSdcardEnabled();
            bool sdEmailActive = configManager.getSdcardEmailSend(); // "Email" checkbox inside the SD box

            //  If SD is ON (with or without email streaming), the "Automatic RAM Email" must stay inactive to avoid interference.
            // User note: "When on the sdcard config page... Enable ON + Email OFF... There are no partial or automatic emails." 

            // Therefore, if SD is ACTIVE, we disable RAM triggers (for Auto Email)
            // EXCEPTION: DB25/LPT may need triggers to "cut" pages even for SD
            // The user says "we must review the logics and separate".
            // If SD is ON, processing must go via SD logic (2s chunks or full buffer for SD write).
            // If streaming (Email ON), sdcard.cpp handles it.
            // If there is NO streaming (Email OFF), just record.
            
            // Conclusion: if sdCardActive == true, we do NOT generate AUTOMATIC EMAIL triggers here.
            // Only if sdCardActive == false does this block fire triggers.
            
            if (sdCardActive) {
                //  SD Active: Recording mode (with or without streaming).
                // Ignore RAM triggers for "Auto Email".
                // The main loop will keep calling logDataToSdcard() to record data.
                // Email streaming is done via SD callback or internal monitoring.
                // If EMAILDAY arrives, it is handled by the dedicated task.
                
                // Keep processing only for LPT if needed (but the user says SD mode separates everything)
                // Vamos assumir que SD ON desliga totalmente o Auto Email de RAM.
                triggerEmail = false; 
                
                //  If the buffer is full, we NEED to drain it to SD, otherwise it blocks.
                // The main loop already calls logDataToSdcard() when there is data.
                // logDataToSdcard() consome bufferDados e escreve no SD.
                // If that works, bufferDados stays empty and we do not need triggerEmail here.
                
                 // Security check intentionally omitted: do not read bufferDados here without a lock.
                break; // Exit switch(EMAIL_IDLE) and do not process triggers
            }

            //Below follows the normal logic for when SD is OFF (RAM Auto Email Mode)

            // Priority 1: Immediate-send conditions (Buffer Full/Threshold, Line Limit, Form Feed)
            // RULE: the "line limit" fires immediately ALWAYS (complete 66/90 pages),
            // regardless of Auto Email being active and even with the monitor open.
            // The monitor only adjusts inactivity times ("inactive" profile).
            bool allowLineLimitImmediate = true; // Never blocked by the monitor

            // Full/threshold buffer can ALWAYS fire immediately; Form Feed also stays immediate
            bool immediateByBufferOrFF = shouldSendEmailImmediately || formFeedTriggered;
            //  Line limit only fires if allowed (monitor closed)
            bool immediateByLineLimit = allowLineLimitImmediate && autoEmailTriggeredByNewline;
            //  DB25: record last line-limit fire instant (to defer partial flush by inactivity right after a complete page)
            static unsigned long lastDb25LineLimitAt = 0; //  0 means "never"
            //  DB25: inactivity for PARTIAL cut must be observed twice in a row to avoid 88/89 mid-flow
            // Rule: when the page has not yet reached 66/90, ignore the first inactivity timeout; only accept on the second consecutive one
            // Automatic reset when: data arrives again, a complete page is reached, or the buffer goes empty
            static unsigned long db25PartialInactivityFirstObservedAt = 0; // 0 = not observed/clean

            //  Immediate triggers do NOT depend on WiFi/config: we need to cut the chunk anyway
            //  EXCEPTION: Line limit stops being "immediate" and instead arms a micro-pause and uses the next branch
            if (immediateByBufferOrFF) {
                triggerEmail = true;
                if (shouldSendEmailImmediately) tempTriggerSource = TRIGGER_AUTO_BUFFER_FULL;
                else if (formFeedTriggered) tempTriggerSource = TRIGGER_AUTO_FORM_FEED;
            }
            //  Immediate line limit (with DB25 hysteresis):
            else if (immediateByLineLimit) {
                static unsigned long db25LineLimitFirstReachedAt = 0; //  0 = nunca
                if (lastByteFromDb25) {
                    unsigned required = (unsigned)(currentMaxEmailLines + DB25_LINE_HYST);
                    if ((unsigned)bufferDadosLineCount < required) {
                        if (db25LineLimitFirstReachedAt == 0) db25LineLimitFirstReachedAt = currentMillis;
                        unsigned dt = (unsigned)(currentMillis - db25LineLimitFirstReachedAt);
                        if (dt < DB25_LINE_HYST_TIMEOUT_MS) {
                            // Defer trigger until at least +hysteresis lines (or short timeout)
                            triggerEmail = false;
                            tempTriggerSource = TRIGGER_NONE;
                        } else {
                            //  Timeout: accept even without +hysteresis to avoid blocking exact-66 jobs
                            triggerEmail = true;
                            tempTriggerSource = TRIGGER_AUTO_LINE_LIMIT;
                            db25LineLimitFirstReachedAt = 0;
                        }
                    } else {
                        // We already have +hysteresis — accept
                        triggerEmail = true;
                        tempTriggerSource = TRIGGER_AUTO_LINE_LIMIT;
                        db25LineLimitFirstReachedAt = 0;
                    }
                    lastDb25LineLimitAt = currentMillis;
                } else {
                    // RS232 keeps previous behavior
                    triggerEmail = true;
                    tempTriggerSource = TRIGGER_AUTO_LINE_LIMIT;
                }
            }

            // Priority 3: Inactivity (timeouts are the priority RULE)
            // Inactivity: do not require WiFi up to start the cycle — WiFi is checked further ahead
            //    This allows firing emails with sparse inputs even with WiFi momentarily offline
            else {
                size_t bufSize = 0;
                bool haveBufSize = bufferDadosTryGetSize(bufSize, 0);
                if (!haveBufSize) {
                    // If we cannot safely read the buffer, skip inactivity evaluation this cycle.
                } else if (!emulatorShouldBeBusy && bufSize > 0) {
                //  Removed monitor override: the "autoSendEmail" state is now used directly,
                //    even with the /monitor page open. So the inactivity trigger stays active.
                bool effectiveAutoSendEmail = autoSendEmail; // Ignore autoEmailMonitorOverrideActive
                unsigned long currentInactivityTimeout = effectiveAutoSendEmail ? autoEmailEnabledInactivityTimeout : autoEmailDisabledInactivityTimeout; // Timeout according to configuration
                // Suppression: if we just transmitted an RS232 command, grant a grace window
                bool graceActive = (currentMillis < rs232InactivityGraceUntil);
                //  Required policy: timeouts (ENABLED/DISABLED) take priority and allow sending even with few lines.
                // Otherwise (timeout not yet reached), keep buffering until at least 40 lines (or other immediate triggers).
                if (!graceActive && (currentMillis - lastReceptionTime > currentInactivityTimeout)) {
                    triggerEmail = true;
                    tempTriggerSource = effectiveAutoSendEmail ? TRIGGER_AUTO_INACTIVITY_ENABLED : TRIGGER_AUTO_INACTIVITY_DISABLED_LONG_TIMEOUT; //  Fonte conforme estado efetivo
                }

                // Reset the double-observation marker when not in inactivity or when conditions cease to be "partial DB25"
                // Avoid accumulating state between flows and ensure new traffic cancels previous observation
                if ((currentMillis - lastReceptionTime) <= currentInactivityTimeout
                    || bufferDadosLineCount == 0
                    || bufferDadosLineCount >= currentMaxEmailLines
                    || !lastByteFromDb25) {
                    db25PartialInactivityFirstObservedAt = 0; //  Reset
                }
                }
            }

            // Short log of the final trigger result, only if a trigger fired or the fire state changed to true
            if (triggerEmail && triggerEmail != lastLoggedTriggerAttempt) {
                safeEnviarMensagemVirtual("TRG: final trig=%s init=%s src=%s", DEBUG,
                    (triggerEmail ? "true" : "false"), (emailProcessInitiatedThisCycle ? "true" : "false"), getEmailTriggerReason(tempTriggerSource).c_str());
                lastLoggedTriggerAttempt = triggerEmail; // Atualiza o estado do trigger logado
            } else if (!triggerEmail && lastLoggedTriggerAttempt) {
                // Reset sem logar "false" repetidamente
                lastLoggedTriggerAttempt = false;
            }


            // Debounce for inactivity triggers: honor global cooldown and suppress repeats
            bool isInactivityTrigger = (tempTriggerSource == TRIGGER_AUTO_INACTIVITY_ENABLED || tempTriggerSource == TRIGGER_AUTO_INACTIVITY_DISABLED_LONG_TIMEOUT);
            if (triggerEmail && isInactivityTrigger) {
                if ((currentMillis - lastInactivityTriggerAt) < INACTIVITY_TRIGGER_COOLDOWN_MS) {
                    //  Inside cooldown — do not fire again
                    safeEnviarMensagemVirtual("TRG: inatividade suprimida (cooldown)", DEBUG);
                    triggerEmail = false; // cancels this cycle
                }
            }

            //  DB25: defer inactivity trigger when the page is not yet complete if a complete page just went out
            //   Goal: guarantee 66/90 during flow; use inactivity only for the FINAL remainder.
            //   Rule: if there was a DB25 line-limit page a short time ago (< inactivity timeout),
            //            and we still only have a partial page (<66/90), then defer this inactivity trigger.
            //            For a small job (never had a complete page) or if the timeout already passed since the last page,
            //            inactivity flush of the remainder is allowed.
            if (triggerEmail && isInactivityTrigger && lastByteFromDb25) {
                int db25Page = currentMaxEmailLines; // 66/90 according to current configuration
                if (bufferDadosLineCount > 0 && bufferDadosLineCount < db25Page) {
                    //  If there was a complete DB25 page "recently" (< timeout), defer partial flush to avoid 88/89
                    unsigned long _db25InactTimeout = autoSendEmail ? (unsigned long)autoEmailEnabledInactivityTimeout : (unsigned long)autoEmailDisabledInactivityTimeout; // Timeout according to current configuration
                    bool hadRecentFullPage = (lastDb25LineLimitAt > 0) && ((currentMillis - lastDb25LineLimitAt) < _db25InactTimeout);
                    if (hadRecentFullPage) {
                        safeEnviarMensagemVirtual("TRG: DB25 inactivity postponed (partial %u<%u) recent full page", DEBUG,
                            (unsigned)bufferDadosLineCount, (unsigned)db25Page);
                        triggerEmail = false; //  Defer this trigger — let the next line-limit break handle it
                    } else {
                        //  "Double timeout" rule for DB25: only accept partial flush after two consecutive windows
                        if (db25PartialInactivityFirstObservedAt == 0) {
                            db25PartialInactivityFirstObservedAt = currentMillis; // First observation
                            safeEnviarMensagemVirtual("TRG: inatividade DB25 observada (1/2) parcial %u<%u", DEBUG,
                                (unsigned)bufferDadosLineCount, (unsigned)db25Page);
                            triggerEmail = false; //  Ignore this 1st window
                        } else if ((currentMillis - db25PartialInactivityFirstObservedAt) < _db25InactTimeout) {
                            // Still inside the second window — keep deferring
                            triggerEmail = false;
                        } else {
                            // Second window complete — accept flush of the final remainder
                            safeEnviarMensagemVirtual("TRG: inatividade DB25 (2/2) parcial aceito %u<%u", DEBUG,
                                (unsigned)bufferDadosLineCount, (unsigned)db25Page);
                            db25PartialInactivityFirstObservedAt = 0; //  Reset for next job
                        }
                    }
                }
            }

            if (triggerEmail && !emailProcessInitiatedThisCycle) {
                // Safety: if bufferDados is not allocated (capacity==0) then we cannot do RAM email ? log and skip
                if (bufferDados.capacity() == 0) {
                    safeEnviarMensagemVirtual("ERR: bufferDados capacity==0 - cannot start RAM Email; ignoring trigger", ERROR);
                    triggerEmail = false; // cancel trigger
                }
            }
            if (triggerEmail && !emailProcessInitiatedThisCycle) {
                //  Accept trigger — SD and Email will process per their flags in EMAIL_PREPARE_CONTENT
                // We do not block here; we let logic proceed (SD always records, Email has its own checks)
                {
                    // Trigger accepted — mark as started
                    emailProcessInitiatedThisCycle = true;
                    safeEnviarMensagemVirtual("ST: init=1", DEBUG);
                    lastEmailTriggerSource = tempTriggerSource;
                // Align the trigger source (DB25 vs RS232) also for non-LINE triggers (BUF/FF/INACTIVITY)
                //  Reason: previously we only marked in handleDecodedCodepoint() when the trigger was a line limit.
                //    So an inactivity trigger could inherit a stale value and wrongly apply 55 (ISQ20) to DB25.
                // Now we record the source at trigger time: DB25 → 66/90; RS232 → 42/55 per policy.
                lastTriggerFromDb25 = lastByteFromDb25; // Source of reception when trigger was accepted

                safeEnviarMensagemVirtual("TRG_SRC: db25=%u", DEBUG, (unsigned)(lastTriggerFromDb25 ? 1 : 0));

                // Raise the flag when the email sending process starts
                isEmailSendingActive = true; 

                // Concise trigger message
                safeEnviarMensagemVirtual("Email: %s. A aguardar.", CRITICAL_EVENT, getEmailTriggerReason(lastEmailTriggerSource).c_str());

                //  If the reason was inactivity, record instant for cooldown
                if (isInactivityTrigger) { lastInactivityTriggerAt = currentMillis; }

                // Light debug without creating long temporaries
                {
                    size_t bufSize = 0;
                    if (!bufferDadosTryGetSize(bufSize, 0)) bufSize = 0;
                    safeEnviarMensagemVirtual("TRG: disparado buf=%u", DEBUG, (unsigned)bufSize);
                }

                // Reset fire flags immediately after firing
                // Even if the line limit had been "gated" before, we clear the flag here to avoid re-firing
                autoEmailTriggeredByNewline = false;
                shouldSendEmailImmediately = false; // Reset buffer-full flag
                formFeedTriggered = false;

                    if (enableDebugLogs) {
                        safeEnviarMensagemVirtual("DEBUG_BUSY: bufferRx (write/read): %u/%u", DEBUG, (unsigned)writePos, (unsigned)readPos);
                    }

                currentEmailSendingState = EMAIL_WAITING_FOR_SENDER_STOP;
                rs232_blockQueueForEmail = false;
                safeEnviarMensagemVirtual("ST: -> WAIT_SENDER src=%s", DEBUG, getEmailTriggerReason(lastEmailTriggerSource).c_str());
                //  Detailed telemetry removed (DEBUG_WAIT_ENTER) to reduce noise
                lastEmailActionTime = currentMillis;
                }
            } else if (triggerEmail && emailProcessInitiatedThisCycle) {
                safeEnviarMensagemVirtual("TRG: ignorado init=1", DEBUG);
            }
            break;
        }

        case EMAIL_WAITING_FOR_SENDER_STOP: {
            // Wait conditions before preparing the email content.
            // - DB25: after BUSY/XOFF, wait for a short silence window and then drain residual bytes.
            // - Inactivity trigger: wait for the configured sender timeout.
            // - Other triggers (line limit / buffer full / form-feed): prepare immediately.

            if (lastTriggerFromDb25) {
                const unsigned long DB25_SENDER_STOP_GAP_MS = 15UL;
                if ((currentMillis - lastReceptionTime) < DB25_SENDER_STOP_GAP_MS) {
                    if (enableDebugLogs) safeEnviarMensagemVirtual("WAIT: DB25 sender stopping...", DEBUG);
                    break;
                }

                // Drain bufferRx -> bufferDados once to capture bytes that arrived after BUSY.
                while (readPos != writePos) {
                    noInterrupts();
                    if (readPos >= BUFFER_RX_SIZE || writePos >= BUFFER_RX_SIZE) {
                        readPos = 0;
                        writePos = 0;
                        interrupts();
                        sendVirtualMessage(String(F("Buffer positions corrupted, reset performed (drain inline)")), ERROR);
                        break;
                    }
                    uint8_t raw = bufferRx[readPos];
                    lastByteFromDb25 = true;
                    readPos = (readPos + 1) % BUFFER_RX_SIZE;
                    interrupts();
                    uint32_t decodedCodepoint = decodeUtf8Byte(raw);
                    if (decodedCodepoint != 0) {
                        handleDecodedCodepoint(decodedCodepoint);
                    }
                }
            }

            const unsigned long SENDER_STOP_TIMEOUT_MS = (rs232_read_timeout_ms > 0) ? (unsigned long)rs232_read_timeout_ms : 800UL;
            rs232_paused_by_controller = true;
            rs232LastWasCR = false;
            if (rs232FirstResponseGuardActive) {
                rs232FirstResponseGuardUntil = max(rs232FirstResponseGuardUntil, millis() + (SENDER_STOP_TIMEOUT_MS / 2));
            }

            const bool isInactivityTrigger = (lastEmailTriggerSource == TRIGGER_AUTO_INACTIVITY_ENABLED
                || lastEmailTriggerSource == TRIGGER_AUTO_INACTIVITY_DISABLED_LONG_TIMEOUT);

            auto abortToIdle = [&](bool markInactivity) {
                isEmailSendingActive = false;
                emailProcessInitiatedThisCycle = false;
                currentEmailSendingState = EMAIL_IDLE;
                rs232_blockQueueForEmail = false;

                // Release any forced BUSY/XOFF held for DB25-triggered emails.
                holdBusyDuringEmail = false;
                emulatorShouldBeBusy = false;
                pauseInternalBufferTransfer = false;
                setBusyPinState(false);

                // Resume senders. Flow control is driven by buffer high-water logic.
                if (isFlowControlEngaged()) {
                    sendRs232Xon();
                }
                nanoBridgeSendXon();
                rs232_paused_by_controller = false;

                if (markInactivity) {
                    lastInactivityTriggerAt = millis();
                }
            };

            if (isInactivityTrigger) {
                if ((readPos != writePos) || ((currentMillis - lastReceptionTime) < SENDER_STOP_TIMEOUT_MS)) {
                    break;
                }
            }

            // Drain one page from appropriate buffer (LPT uses bufferDadosLPT, RS232 uses bufferDados).
            bool isLPTEmail = lastTriggerFromDb25; // Determine data source
            bool bufferLocked = false;
            
            if (isLPTEmail) {
                if (!bufferDadosLPTLock(pdMS_TO_TICKS(200), bufferLocked)) {
                    safeEnviarMensagemVirtual("ERR: timeout acquiring bufferDadosLPT lock; aborting email prepare", ERROR);
                    abortToIdle(isInactivityTrigger);
                    break;
                }
            } else {
                if (!bufferDadosLock(pdMS_TO_TICKS(200), bufferLocked)) {
                    safeEnviarMensagemVirtual("ERR: timeout acquiring bufferDados lock; aborting email prepare", ERROR);
                    abortToIdle(isInactivityTrigger);
                    break;
                }
            }

            //  ISQ20 Adjustment: When ISQ20 is active, we need MORE raw lines from buffer
            // because headers are inserted every 20 lines, consuming space
            // For 90 final lines, we need ~90 + (90/20 * header_lines) ? 90 + 20 = 110 raw lines
            bool isq20Active = (configManager.getStructuresAutoDetectEnabled() || 
                              configManager.getAmadaISQ20MTF1Enabled() || 
                              configManager.getAmadaISQ20AWSEEnabled());
            
            int linesBudget = currentMaxEmailLines;
            if (isq20Active && configManager.getSdcardStructured()) {
                // ? Add ~30% more lines to account for headers (safety margin)
                linesBudget = (currentMaxEmailLines * 13) / 10; // 90 * 1.3 = 117 lines
            }
            
            // Use appropriate line counter based on source
            int currentLineCount = isLPTEmail ? bufferDadosLPTLineCount : bufferDadosLineCount;
            if (isInactivityTrigger) {
                linesBudget = min(linesBudget, max(1, currentLineCount));
            }

            std::vector<uint8_t, PsramAllocatorStd<uint8_t>> rawBytes;
            rawBytes.reserve((size_t)linesBudget * (size_t)AVERAGE_LINE_LENGTH_BYTES);

            unsigned consumedLines = 0;
            bool justSawCR = false;
            bool needToCompleteLastLine = false;
            
            // Drain from appropriate buffer
            if (isLPTEmail) {
                while (!bufferDadosLPT.isEmpty()) {
                    uint8_t b = bufferDadosLPT.shift();
                    rawBytes.push_back(b);
                    
                    if (b == '\r') {
                        consumedLines++;
                        justSawCR = true;
                        needToCompleteLastLine = false;
                    } else if (b == '\n') {
                        if (!justSawCR) consumedLines++;
                        justSawCR = false;
                        needToCompleteLastLine = false;
                    } else {
                        justSawCR = false;
                        needToCompleteLastLine = true;
                    }
                    
                    if (consumedLines >= (unsigned)linesBudget && !needToCompleteLastLine) {
                        break;
                    }
                }
            } else {
                while (!bufferDados.isEmpty()) {
                    uint8_t b = bufferDados.shift();
                    rawBytes.push_back(b);
                    
                    if (b == '\r') {
                        consumedLines++;
                        justSawCR = true;
                        needToCompleteLastLine = false;
                    } else if (b == '\n') {
                        if (!justSawCR) consumedLines++;
                        justSawCR = false;
                        needToCompleteLastLine = false;
                    } else {
                        justSawCR = false;
                        needToCompleteLastLine = true;
                    }
                    
                    if (consumedLines >= (unsigned)linesBudget && !needToCompleteLastLine) {
                        break;
                    }
                }
            }

            // Update appropriate line counter
            if (isLPTEmail) {
                if (bufferDadosLPTLineCount >= (int)consumedLines) bufferDadosLPTLineCount -= (int)consumedLines;
                else bufferDadosLPTLineCount = 0;
                autoEmailTriggeredByNewline = (bufferDadosLPTLineCount >= currentMaxEmailLines);
                bufferClearedOnInactivity = bufferDadosLPT.isEmpty();
                bufferDadosLPTUnlock(bufferLocked);
            } else {
                if (bufferDadosLineCount >= (int)consumedLines) bufferDadosLineCount -= (int)consumedLines;
                else bufferDadosLineCount = 0;
                autoEmailTriggeredByNewline = (bufferDadosLineCount >= currentMaxEmailLines);
                bufferClearedOnInactivity = bufferDados.isEmpty();
                bufferDadosUnlock(bufferLocked);
            }

            String rawEmailContent;
            rawEmailContent.reserve(rawBytes.size() + 8);
            for (size_t i = 0; i < rawBytes.size(); ++i) {
                rawEmailContent += (char)rawBytes[i];
            }

            // Store LPT content and save backup to SD BEFORE sending (preventive backup)
            // Note: PSRAM not used for backups (volatile - data lost on reboot)
            if (isLPTEmail) {
                lptEmailContentForSave = rawEmailContent;
                lastSentEmailWasLPT = true;
                
                // Save backup to SD BEFORE email send (if send fails, backup is already safe)
                if (lptEmailContentForSave.length() > 0) {
                    bool savedCopy = saveLptCopyToSD(lptEmailContentForSave);
                    if (!savedCopy && sdcardIsMounted()) {
                        sendVirtualMessage(F("⚠️ LPT: Failed to save SD backup before send"), WARN);
                    }
                }
            } else {
                lptEmailContentForSave = "";
                lastSentEmailWasLPT = false;
            }

            if (isInactivityTrigger) {
                bool onlyWhitespace = true;
                for (int i = 0; i < rawEmailContent.length(); ++i) {
                    char c = rawEmailContent.charAt(i);
                    if (!(c == '\r' || c == '\n' || c == '\t' || c == ' ' || c == '\f')) {
                        onlyWhitespace = false;
                        break;
                    }
                }
                if (onlyWhitespace && rawEmailContent.length() <= 4) {
                    abortToIdle(true);
                    break;
                }
            }

            const bool useAttach = configManager.getEmailAttachTxt();
            emailRawDataCache = rawEmailContent;
            
            //  ISQ20: Convert raw content to tabular format for attachment
            // (reuse isq20Active from line budget calculation above)
            if (isq20Active && configManager.getSdcardStructured() && useAttach) {
                // ? Process raw email content to ISQ20 tabular format with headers
                emailTablesDataCache = convertToISQ20TabularFormat(rawEmailContent, isLPTEmail);
                
                //  Debug: Log if conversion produced output
                if (emailTablesDataCache.length() > 0) {
                    char debugMsg[128];
                    snprintf(debugMsg, sizeof(debugMsg), "ISQ20: Converted %u raw chars to %u tabular chars", 
                            (unsigned)rawEmailContent.length(), 
                            (unsigned)emailTablesDataCache.length());
                    safeEnviarMensagemVirtual(debugMsg, DEBUG);
                } else {
                    //  Conversion failed - fallback to raw
                    safeEnviarMensagemVirtual("ISQ20: Conversion failed, using raw content", WARN);
                    emailTablesDataCache = String();
                }
            } else {
                emailTablesDataCache = String();
            }
            
            emailContentToSend = useAttach ? String() : rawEmailContent;
            ultimoConteudoPreparado = emailContentToSend;

            int effectiveLen = useAttach ? emailRawDataCache.length() : emailContentToSend.length();
            if (effectiveLen < minEmailContentLength) {
                abortToIdle(isInactivityTrigger);
                break;
            }

            currentEmailSendingState = EMAIL_START_SENDING;
            rs232_blockQueueForEmail = false;
            emailCycleCount = 1;
            emailAttemptCount = 0;
            lastEmailActionTime = currentMillis;
            safeEnviarMensagemVirtual("ST: -> EMAIL_START_SENDING (prep)", DEBUG);
            break;
        }

        case EMAIL_START_SENDING: {
            // State entry log
            safeEnviarMensagemVirtual("ST: EMAIL_START_SENDING", DEBUG);
            //  Reinforcement: if the source was DB25, keep BUSY/XOFF active during the whole email cycle
            // This covers all paths entering this state (including previous preparations)
            if (lastTriggerFromDb25) {
                if (!holdBusyDuringEmail || !isPrinterBusy) {
                    holdBusyDuringEmail = true;
                    emulatorShouldBeBusy = true;
                    pauseInternalBufferTransfer = true; // avoid extra drains while sending
                    setBusyPinState(true); // translates to XOFF — Nano asserts physical BUSY
                    db25SkipNextLF = false; //  CRLF dedup state reset to start a clean page
                    safeEnviarMensagemVirtual("EMAIL_START: DB25 BUSY/XOFF assegurado", DEBUG);
                }
            }
            // Preventive cleanup: ensure no pending TLS connection from previous cycle
#if ENABLE_SMTP
            smtpTeardown();
#endif
            //  DEBUG suppression: from this point on email sending will build many Strings
            // To reduce heap spikes/fragmentation, temporarily disable DEBUG logs
            if(!emailLogSuppressionActive){
                emailLogSuppressionActive = true;                 //  Mark suppression active
                savedEnableDebugLogsBeforeEmail = enableDebugLogs; //  Save previous state
                enableDebugLogs = false;                           //  Force DEBUG OFF
            }
            // Note: sendVirtualMessage already suppresses system messages during active reception/RS232,
            //         so logs will not contaminate bufferDados nor the sample line during the cycle.

            if (emailCycleCount > EMAIL_NUM_CYCLES) {
                sendVirtualMessage(String(F("EMAIL: All attempts failed")), ERROR);
                currentEmailSendingState = EMAIL_FINISHED_FAILURE;
                rs232_blockQueueForEmail = false; //  Ensure unlock
                safeEnviarMensagemVirtual("ST: -> EMAIL_FINISHED_FAILURE (ciclos)", DEBUG);
                break;
            }
            if (emailCycleCount > 1 && currentMillis - lastEmailActionTime < EMAIL_DELAY_BETWEEN_CYCLES) {
                return;
            }
            //  Attempt increment moved to EMAIL_PREPARE_CONTENT after validating recipients
            //  Here we only transition to analysis; counters stay unchanged in this state

            // Reorder: now validate recipients BEFORE attempting WiFi/SMTP
            //  Cycle/attempt increments already done above — we only advance to WiFi if recipients exist
            // We keep minimum configuration check (no primary recipient still allowed if there are rules) only after building the attempt
            // Direct transition to EMAIL_PREPARE_CONTENT (pre-scan recipients)
            currentEmailSendingState = EMAIL_PREPARE_CONTENT; //  Next step: detect recipients, only then WiFi
            safeEnviarMensagemVirtual("ST: -> EMAIL_PREPARE_CONTENT (preTransport)", DEBUG);
            break;
        }

        case EMAIL_CHECK_TRANSPORT: {
            // State entry log
            safeEnviarMensagemVirtual("ST: EMAIL_CHECK_TRANSPORT", DEBUG);

            if (!emailTransportCanSendEmail()) {
                sendVirtualMessage(String(F("EMAIL_TRANSPORTE: ")) + emailTransportUnavailableReason() + String(F(". A tentar recuperar WiFi...")), WARN);
                if (currentWifiConnectionState == WIFI_IDLE || currentWifiConnectionState == WIFI_FAILED) {
                    // CRITICAL: Ensure WiFi mode before reconnect for email
                    if (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) {
                        WiFi.mode(WIFI_STA);
                        delay(500);
                    }
                    
                    WiFi.begin(ssid.c_str(), password.c_str());
                    wifiConnectStartTime = currentMillis;
                    currentWifiConnectionState = WIFI_CONNECTING;
                }
                if (currentWifiConnectionState != WIFI_CONNECTED) {
                    return;
                }
            }
            sendVirtualMessage(String(F("Email transport OK via ")) + emailTransportActiveLabel(), INFO);
            safeEnviarMensagemVirtual("DBG: heap=%u", DEBUG, (unsigned)ESP.getFreeHeap());
            // Pre-SMTP checks: circuit-breaker and minimum heap
#if ENABLE_SMTP
            if(emailBreakerUntil && currentMillis < emailBreakerUntil){
                //  Breaker ativo ? log curto
                unsigned long waitMs = emailBreakerUntil - currentMillis;
                safeEnviarMensagemVirtual("EMAIL_BREAKER: wait=%lus", WARN, (unsigned)(waitMs/1000));
                return; // ? Aguarda
            }
            size_t freeHeapNow = ESP.getFreeHeap();
            if (freeHeapNow < EMAIL_MIN_HEAP_TO_CONNECT){
                safeEnviarMensagemVirtual("EMAIL_HEAP_BAIXO: %u", WARN, (unsigned)freeHeapNow);
                return; // ? Aguarda acumular heap
            }
#endif
            currentEmailSendingState = EMAIL_CONNECTING_SMTP; //  Destinat?rios j? validados previamente
            safeEnviarMensagemVirtual("ST: -> EMAIL_CONNECTING_SMTP", DEBUG);
            break;
        }

        case EMAIL_PREPARE_CONTENT: {
            safeEnviarMensagemVirtual("ST: EMAIL_PREPARE_CONTENT", DEBUG);
            
            //  Decide whether to process data (LOGIC PER dados.txt)
            bool sdEnabled = configManager.getSdcardEnabled();  //  SD Card ativo?
            bool emailSdEnabled = configManager.getSdcardEmailSend();  //  Email envio ativo (na p?gina SD)?
            bool autoSendEnabled = configManager.getEmailAutoSend();  //  Auto-send ativo (na p?gina Email)?
            
            //  STORE GLOBAL FLAGS for use in other states (preserved between EMAIL_PREPARE_CONTENT and EMAIL_SENDING_*)
            globalEmailAutoSendEnabled = autoSendEnabled;    //  Armazena flag global
            globalSdcardEmailSendEnabled = emailSdEnabled;   //  Armazena flag global
            
            //  RULE PER dados.txt:
            // SD=YES, Email=NO → Record SD only (no email)
            // SD=YES, Email=YES → Record SD + Send email (sender blocked)
            // SD=NO → the "Email" field of the SD page is IGNORED, send follows normal behavior
            //     (respects getEmailDontCopySender configuration, sends to recipients according to rules)
            
            //  If SD=YES, Email=NO → Record SD only, NO email
            if (sdEnabled && !emailSdEnabled) {
                safeEnviarMensagemVirtual("ST: SD Only mode - SD=YES, Email=NO", INFO);
                //  Data was already recorded to SD by logDataToSdcard(), just mark cycle as complete
                (void)bufferDadosTryClear(pdMS_TO_TICKS(500));
                bufferDadosLineCount = 0;
                bufferClearedOnInactivity = true;
                isPrinterBusy = false;
                if (emailMatchedRulesToFinalize.length() > 0) finalizeMatchedRulesResult(emailMatchedRulesToFinalize, false);
                emailMatchedRulesToFinalize = "";
                currentEmailSendingState = EMAIL_IDLE;  //  Back to IDLE (without showing "Email Enviado" on OLED)
                rs232_blockQueueForEmail = false;
                safeEnviarMensagemVirtual("ST: -> EMAIL_IDLE (SD only, sem email)", DEBUG);
                return;  // Exit without sending email
            }
            
            // If SD=YES, Email=YES - CHECK FOR MATCHED RECIPIENTS
            //  CRITICAL FIX: Only bypass to background task if there are recipients!
            //  If there are no recipients, continue with normal email (no sender filter)
            if (sdEnabled && emailSdEnabled) {
                // 🔍 PRE-SCAN: Check for matched recipients in the current buffer
                String tempSearchContent;
                bool useAttach = configManager.getEmailAttachTxt();
                if (useAttach && emailRawDataCache.length()) {
                    tempSearchContent = emailRawDataCache;
                } else {
                    tempSearchContent = emailContentToSend;
                }
                
                std::vector<std::pair<String, String>> tempRecipients;
                String tempLower = tempSearchContent;
                tempLower.toLowerCase();
                findRecipientsInLowerText(tempLower, tempRecipients, true); // sdActive=true
                
                if (!tempRecipients.empty()) {
                    // ✅ MATCHED RECIPIENTS FOUND → Write to SD and delegate to background task
                    safeEnviarMensagemVirtual("ST: SD+Email mode WITH MATCH (%u recipients) - task background", INFO,
                                             (unsigned)tempRecipients.size());
                    
                    // ✅ CRITICAL FIX: Write data to SD BEFORE returning!
                    // Data is in emailContentToSend (populated by startEmailSending)
                    // emailRawDataCache is only populated later for attachments
                    
                    // 🔍 DEBUG: Log buffer states (INFO level to bypass suppression)
                    safeEnviarMensagemVirtual("🔍 Buffer states: useAttach=%d, rawCache=%u, contentToSend=%u, tempSearch=%u", INFO,
                                             useAttach ? 1 : 0, 
                                             (unsigned)emailRawDataCache.length(), 
                                             (unsigned)emailContentToSend.length(), 
                                             (unsigned)tempSearchContent.length());
                    
                    String dataToWrite;
                    if (useAttach && emailRawDataCache.length() > 0) {
                        dataToWrite = emailRawDataCache; // Use raw cache if available
                        safeEnviarMensagemVirtual("📋 Using emailRawDataCache for SD write", INFO);
                    } else if (emailContentToSend.length() > 0) {
                        dataToWrite = emailContentToSend; // Use content (from streaming queue)
                        safeEnviarMensagemVirtual("📋 Using emailContentToSend for SD write", INFO);
                    } else {
                        dataToWrite = tempSearchContent; // Last resort: use temp search buffer
                        safeEnviarMensagemVirtual("📋 Using tempSearchContent for SD write", INFO);
                    }
                    
                    if (dataToWrite.length() > 0) {
                        safeEnviarMensagemVirtual("✍️ Writing matched data to SD before background task... (%u bytes)", INFO, (unsigned)dataToWrite.length());
                        
                        // ✅ CRITICAL: Write DIRECTLY to SD file, bypassing buffers AND structure filtering
                        // forceRaw=true ensures data is written as-is without MTF1/AWSE processing or line filtering
                        // This guarantees callback invocation (no hasDataLines=false scenario)
                        logDataToSdcard(lastDataSourceForEmail, dataToWrite, 0, true); // timestamp=0, forceRaw=true
                        
                        // Note: logDataToSdcard() is void, so we can't check if it failed
                        // If it fails (mutex timeout), it logs "⚠️ SD WRITE FAILED" internally
                        safeEnviarMensagemVirtual("✅ SD write call completed (check for failure warnings above)", INFO);
                        
                        // ⚠️ NO FORCE FLUSH NEEDED! 
                        // logDataToSdcard() writes DIRECTLY to file via sdcardWriteData()
                        // It does NOT use circular buffers, so forceSwap is irrelevant
                        // The callback is invoked by sdcardWriteData() immediately after write
                    } else {
                        safeEnviarMensagemVirtual("⚠️ All email buffers empty, nothing to write to SD!", WARN);
                    }
                    
                    //  Task emailTaskFunction() will process emails with matched operators (via onSdcardDataWritten)
                    (void)bufferDadosTryClear(pdMS_TO_TICKS(500));
                    bufferDadosLineCount = 0;
                    bufferClearedOnInactivity = true;
                    isPrinterBusy = false;
                    emailMatchedRulesToFinalize = "";
                    currentEmailSendingState = EMAIL_IDLE;
                    rs232_blockQueueForEmail = false;
                    safeEnviarMensagemVirtual("ST: -> EMAIL_IDLE (task background ativa)", DEBUG);
                    return;  // ? Sair - task background trata os emails
                } else {
                    // ❌ NO MATCHED RECIPIENTS → Continue with normal email
                    safeEnviarMensagemVirtual("ST: SD+Email mode NO MATCH - normal email (all recipients)", INFO);
                    // Continue to the code below (normal email)
                }
            }
            
            //  REMAINING SCENARIO:
            // SD=NO → Continue to prepare RAM email (no SD, normal behavior)
            // SD=YES + Email=YES BUT no match → Continue with normal email

            // ============================================================================
            // NEW: RECIPIENT MATCHING WITHOUT SD (RAM MODE)
            // ============================================================================
            // When SD is inactive, check if there are recipient matches in buffer.
            // If matches found, create separate email jobs (one per matched line) and
            // enqueue to ramEmailJobQueue for background processing.
            // This ensures recipient matching works even without SD card.
            
            if (!sdEnabled) {
                safeEnviarMensagemVirtual("ST: SD INACTIVE - checking RAM recipient matching...", DEBUG);
                
                // Use cached content OR emailContentToSend
                bool useAttach = configManager.getEmailAttachTxt();
                String sourceContent = (useAttach && emailRawDataCache.length()) ? emailRawDataCache : emailContentToSend;
                
                if (sourceContent.length() > 0) {
                    // Process line by line to find recipient matches
                    std::vector<PendingRamEmailJob> matchedJobs;
                    int startIdx = 0;
                    
                    while (startIdx < sourceContent.length()) {
                        int endIdx = sourceContent.indexOf('\n', startIdx);
                        if (endIdx == -1) endIdx = sourceContent.length();
                        
                        String line = sourceContent.substring(startIdx, endIdx);
                        line.trim();
                        
                        if (line.length() > 0) {
                            // Search for recipients in this line
                            String lineLower = line;
                            lineLower.toLowerCase();
                            std::vector<std::pair<String, String>> lineRecipients;
                            std::map<String,String> capturedByRule;
                            findRecipientsInLowerText(lineLower, lineRecipients, false, &capturedByRule); // sdActive=false
                            
                            if (!lineRecipients.empty()) {
                                // Create job for this line
                                PendingRamEmailJob job;
                                job.lineContent = line;
                                job.source = lastDataSourceForEmail; // RS232 or LPT
                                job.processed = false;
                                // Store the value captured during rule matching; fallback to smart scan
                                for (const auto &rec : lineRecipients) {
                                    if (capturedByRule.count(rec.first)) { job.capturedValue = capturedByRule[rec.first]; break; }
                                }
                                if (job.capturedValue.length() == 0) job.capturedValue = extractSmartNumber(line);
                                
                                // Build unique recipient list
                                std::vector<String> uniqueEmails;
                                std::vector<String> uniqueRules;
                                for (const auto &rec : lineRecipients) {
                                    // Check email uniqueness
                                    bool emailSeen = false;
                                    for (const auto &e : uniqueEmails) {
                                        if (e == rec.second) {
                                            emailSeen = true;
                                            break;
                                        }
                                    }
                                    if (!emailSeen) uniqueEmails.push_back(rec.second);
                                    
                                    // Check rule uniqueness
                                    bool ruleSeen = false;
                                    for (const auto &r : uniqueRules) {
                                        if (r == rec.first) {
                                            ruleSeen = true;
                                            break;
                                        }
                                    }
                                    if (!ruleSeen) uniqueRules.push_back(rec.first);
                                }
                                
                                // Build recipients string (comma-separated emails)
                                job.recipients = "";
                                for (size_t i = 0; i < uniqueEmails.size(); i++) {
                                    if (i > 0) job.recipients += ", ";
                                    job.recipients += uniqueEmails[i];
                                }
                                
                                // Build matched rules string ("|" separated rule names — avoids collision with CSV rule names)
                                job.matchedRules = "";
                                for (size_t i = 0; i < uniqueRules.size(); i++) {
                                    if (i > 0) job.matchedRules += "|";
                                    job.matchedRules += uniqueRules[i];
                                }
                                
                                // Timestamp (HH:MM:SS format)
                                time_t now = time(nullptr);
                                struct tm info;
                                localtime_r(&now, &info);
                                char timestamp[16];
                                snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d", 
                                        info.tm_hour, info.tm_min, info.tm_sec);
                                job.timestamp = String(timestamp);
                                
                                matchedJobs.push_back(job);
                                
                                safeEnviarMensagemVirtual("📋 RAM Match: [%.30s...] -> %s (rules: %s)", DEBUG,
                                                         line.c_str(), job.recipients.c_str(), job.matchedRules.c_str());
                            }
                        }
                        
                        startIdx = endIdx + 1;
                    }
                    
                    // If matches found, enqueue jobs and clear buffer
                    if (!matchedJobs.empty()) {
                        safeEnviarMensagemVirtual("✅ RAM Mode: Found %u matched line(s), enqueueing jobs...", INFO, 
                                                 (unsigned)matchedJobs.size());
                        
                        // Acquire mutex and enqueue jobs
                        if (ramEmailQueueMutex && xSemaphoreTakeRecursive(ramEmailQueueMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                            // Enforce FIFO circular behavior (limit to MAX_RAM_EMAIL_JOBS)
                            for (const auto &newJob : matchedJobs) {
                                if (ramEmailJobQueue.size() >= MAX_RAM_EMAIL_JOBS) {
                                    // Remove oldest unprocessed
                                    bool removed = false;
                                    for (auto it = ramEmailJobQueue.begin(); it != ramEmailJobQueue.end(); ++it) {
                                        if (!it->processed) {
                                            safeEnviarMensagemVirtual("⚠️ RAM queue full, removing oldest job", WARN);
                                            ramEmailJobQueue.erase(it);
                                            removed = true;
                                            break;
                                        }
                                    }
                                    // If all processed, remove first
                                    if (!removed && !ramEmailJobQueue.empty()) {
                                        ramEmailJobQueue.erase(ramEmailJobQueue.begin());
                                    }
                                }
                                ramEmailJobQueue.push_back(newJob);
                            }
                            xSemaphoreGiveRecursive(ramEmailQueueMutex);
                            
                            safeEnviarMensagemVirtual("✅ Enqueued %u RAM email jobs (queue size: %u)", INFO,
                                                     (unsigned)matchedJobs.size(), 
                                                     (unsigned)ramEmailJobQueue.size());
                        } else {
                            safeEnviarMensagemVirtual("⚠️ RAM queue mutex timeout, continuing with normal email", WARN);
                        }
                        
                        // Clear buffer and return to IDLE (background task will process)
                        (void)bufferDadosTryClear(pdMS_TO_TICKS(500));
                        bufferDadosLineCount = 0;
                        bufferClearedOnInactivity = true;
                        isPrinterBusy = false;
                        emailMatchedRulesToFinalize = "";
                        currentEmailSendingState = EMAIL_IDLE;
                        rs232_blockQueueForEmail = false;
                        safeEnviarMensagemVirtual("ST: -> EMAIL_IDLE (RAM match task background active)", DEBUG);
                        return; // Exit - background task handles the emails
                    } else {
                        safeEnviarMensagemVirtual("ST: RAM mode - no matches found, continuing with normal email", DEBUG);
                        // No matches - continue to normal email flow below
                    }
                }
            }
            
            // ============================================================================
            // FALLBACK: Normal email flow (no SD, no RAM matches, or SD without matches)
            // ============================================================================

            
            // Pre-analysis of recipients BEFORE opening WiFi connection (reordered) and SMTP
            foundRecipientsForEmail.clear();
            //  Quando anexo .txt est? ativo, preferir procurar destinat?rios no chunk BRUTO (emailRawDataCache)
            {
                bool useAttach = configManager.getEmailAttachTxt(); //  L? flag attach
                String src = (useAttach && emailRawDataCache.length()) ? emailRawDataCache : emailContentToSend; //  Fonte da pesquisa
                searchContent = src; //  Copia
                searchContent.toLowerCase(); //  Normaliza
                
                // ? USAR L?GICA UNIFICADA (mesma do SD)
                // Supports tokens, complete numeric rules (R/DR) and prefixes (e.g. "001 020..040")
                bool sdActiveForRecipients = configManager.getSdcardEnabled();
                
                // Process LINE BY LINE to ensure number extraction logic
                // works correctly for each data line, as on SD.
                // If we pass the whole block, findRecipientsInLowerText only parses the first number it finds.
                int startIdx = 0;
                while (startIdx < searchContent.length()) {
                    int endIdx = searchContent.indexOf('\n', startIdx);
                    if (endIdx == -1) endIdx = searchContent.length();
                    
                    String line = searchContent.substring(startIdx, endIdx);
                    line.trim();
                    if (line.length() > 0) {
                        std::vector<std::pair<String, String>> lineRecs;
                        findRecipientsInLowerText(line, lineRecs, sdActiveForRecipients);
                        // Convert pairs to tuples and accumulate results
                        for (const auto &p : lineRecs) {
                            foundRecipientsForEmail.push_back(std::make_tuple(p.first, p.second, String()));
                        }
                    }
                    startIdx = endIdx + 1;
                }
                
                // Remove duplicates (based on emails - second element)
                std::vector<std::tuple<String,String,String>> unique;
                for (auto &p : foundRecipientsForEmail) {
                    bool seen = false;
                    for (auto &q : unique) if (std::get<1>(q) == std::get<1>(p)) { seen = true; break; }
                    if (!seen) unique.push_back(p);
                }
                foundRecipientsForEmail = unique;
            }
            
            // If there is no match from rules/operators, fall back to a default destination.
            // Priority: configured main destination (emailDest) -> SMTP user (sender address).
            // This preserves the legacy behavior: an inactivity-triggered email still goes somewhere.
            if (foundRecipientsForEmail.empty()) {
                String fallbackDest = emailDest;
                fallbackDest.trim();
                if (fallbackDest.length() == 0) {
                    fallbackDest = smtpUser;
                    fallbackDest.trim();
                }

                if (fallbackDest.indexOf('@') > 0) {
                    emailDest = fallbackDest;
                    emailNoRecipientFlag = false;
                    safeEnviarMensagemVirtual("EMAIL_NO_MATCH: fallback dst=%s", INFO, emailDest.c_str());
                } else {
                    sendVirtualMessage(String(F("EMAIL_DROPPED: no recipients matched and no fallback destination configured.")), WARN);
                    emailNoRecipientFlag = true;
                    emailContentToSend = String();
                    ultimoConteudoPreparado = String();
                    foundRecipientsForEmail.clear();
                    isPrinterBusy = false;
                    currentEmailSendingState = EMAIL_FINISHED_FAILURE;
                    rs232_blockQueueForEmail = false;
                    safeEnviarMensagemVirtual("ST: -> EMAIL_FINISHED_FAILURE (no rcpt)", DEBUG);
                    break;
                }
            }
            
            // Recipients exist (primary or rules) — now manage counters and record attempt
            emailAttemptCount++; //  Increment attempt only when recipients exist
            if (emailAttemptCount > EMAIL_ATTEMPTS_PER_CYCLE) { //  Exhausted cycle attempts
                emailCycleCount++; //  Advance cycle
                emailAttemptCount = 1; //  Restart attempts for the new cycle
                lastEmailActionTime = currentMillis; //  Mark time to apply delay between cycles
                if (emailCycleCount > EMAIL_NUM_CYCLES) { //  Exhausted all cycles
                    sendVirtualMessage(String(F("EMAIL: All attempts failed")), ERROR); //  Final failure log
                    currentEmailSendingState = EMAIL_FINISHED_FAILURE; //  End state
                    safeEnviarMensagemVirtual("ST: -> EMAIL_FINISHED_FAILURE (ciclos)", DEBUG);
                    break; //  Exit
                }
                String cycleWaitMsg = String(F("Primeiro ciclo de envio de email falhou. A aguardar "));
                cycleWaitMsg += String(EMAIL_DELAY_BETWEEN_CYCLES / 1000); //  Wait seconds
                cycleWaitMsg += String(F(" segundos para o ciclo "));
                cycleWaitMsg += String(emailCycleCount); //  New cycle
                cycleWaitMsg += String(F("..."));
                sendVirtualMessage(cycleWaitMsg, WARN); //  Inter-cycle wait log
                //  Stay in this state until natural timeout re-queues — do not proceed to WiFi now
                break; // Wait for next loop to retry
            }
            if (emailAttemptCount > 1 && currentMillis - lastEmailActionTime < EMAIL_DELAY_BETWEEN_ATTEMPTS) { //  Delay between attempts within the same cycle
                //  Do not advance if still inside the minimum interval — keep state
                break; // Wait
            }
            //  Outside delays — proceed to build attempt message (compact)
            char bufSending[56];
            snprintf(bufSending, sizeof(bufSending), "Email: ciclo %u, tent. %u/%u...", (unsigned)emailCycleCount, (unsigned)emailAttemptCount, (unsigned)EMAIL_ATTEMPTS_PER_CYCLE);
            sendVirtualMessage(String(bufSending), INFO); //  Shorter log
            lastEmailActionTime = currentMillis; //  Mark timer for intervals
            String heapDebugSend = String(F("Free Heap (EMAIL_PREPARE_CONTENT - after validation): "));
            heapDebugSend += String(ESP.getFreeHeap()); //  Current heap
            heapDebugSend += String(F(" bytes"));
            sendVirtualMessage(heapDebugSend, DEBUG); //  Heap log
            // Final check of minimal SMTP config (allows empty emailDest if only rules)
            if (smtpServer.isEmpty() || String(smtpPort).isEmpty() || smtpUser.isEmpty() || smtpPass.isEmpty()) { //  smtpPort handled as String for consistency
                sendVirtualMessage(String(F("EMAIL_CFG_ERROR: Incomplete email settings. Aborting.")), ERROR); // Configuration failure
                currentEmailSendingState = EMAIL_FINISHED_FAILURE; //  End flow
                safeEnviarMensagemVirtual("ST: -> EMAIL_FINISHED_FAILURE (cfg)", DEBUG);
                break; //  Abort early
            }
            currentEmailSendingState = EMAIL_CHECK_TRANSPORT; //  Next step: ensure email transport
            safeEnviarMensagemVirtual("ST: -> EMAIL_CHECK_TRANSPORT", DEBUG);
            break;
        }

        case EMAIL_CONNECTING_SMTP: {
            // State entry log
            safeEnviarMensagemVirtual("ST: EMAIL_CONNECTING_SMTP", DEBUG);

            // Acquire SMTP mutex (protects against concurrent operations with EMAILDAY)
            if (!stateMachineHoldsSmtpMutex) {
                if (!smtpLock(pdMS_TO_TICKS(100))) {
                    // Mutex busy (probably by EMAILDAY) - wait for next iteration
                    return; 
                }
                stateMachineHoldsSmtpMutex = true;
                safeEnviarMensagemVirtual("SMTP mutex acquired by state machine", DEBUG);
            }

            // Acquire P3 priority lock (blocks EMAILDAY from starting while state machine is sending)
            if (!stateMachineHoldsPriorityEmail) {
                if (!priorityManager().canStartEmail()) {
                    // Another email operation (e.g., EMAILDAY) is active; release SMTP mutex and retry next loop
                    if (stateMachineHoldsSmtpMutex) { smtpUnlock(); stateMachineHoldsSmtpMutex = false; }
                    return;
                }
                priorityManager().startEmail();
                stateMachineHoldsPriorityEmail = true;
            }

            // Ensure allocation of SMTP objects
            ensureSmtpAllocated();
            
            // CRITICAL: Verify allocation succeeded
            if (!smtp || !smtpClient) {
                safeEnviarMensagemVirtual("ST: SMTP allocation failed (heap: %u)", ERROR, (unsigned)ESP.getFreeHeap());
                // Release mutex and transition to error state
                if (stateMachineHoldsSmtpMutex) {
                    smtpUnlock();
                    stateMachineHoldsSmtpMutex = false;
                }
                currentEmailSendingState = EMAIL_FINISHED_FAILURE;
                return;
            }

            // Prepare TLS according to configuration through the generic transport seam.
            // Nota: Em ESP32 core 3.x, WiFiClientSecure (NetworkClientSecure) n?o exp?e setHostname();
            // o SNI/hostname ? derivado automaticamente do par?metro host em smtp.connect().
            if (!smtpClientPrepare(configManager.getSmtpTlsInsecure(), 30000, 60)) {
                if (stateMachineHoldsSmtpMutex) {
                    smtpUnlock();
                    stateMachineHoldsSmtpMutex = false;
                }
                currentEmailSendingState = EMAIL_FINISHED_FAILURE;
                return;
            }
            // ADAPTED: Using smtp.connect() with empty callback as before
            if (!smtp->connect(smtpServer.c_str(), String(smtpPort).toInt(), [](SMTPStatus status) {})) { // Convert String to int
                sendVirtualMessage(F("SMTP: connection failed"), ERROR);
#if ENABLE_SMTP
                smtpTeardown(); //  Aggressive cleanup after failure
                releaseSmtpResources(); //  Free resources
                if (stateMachineHoldsSmtpMutex) { smtpUnlock(); stateMachineHoldsSmtpMutex = false; }
                emailConsecutiveFailures++;
                if(emailConsecutiveFailures >= EMAIL_FAILS_BEFORE_BREAKER){
                    emailBreakerUntil = millis() + EMAIL_BREAKER_COOLDOWN_MS;
                    safeEnviarMensagemVirtual("EMAIL_BREAKER: pause %lus", WARN, (unsigned)(EMAIL_BREAKER_COOLDOWN_MS/1000));
                }
#endif
                currentEmailSendingState = EMAIL_START_SENDING;
                sendVirtualMessage(F("DBG: EMAIL START (SMTP fail)"), DEBUG);
                break;
            }
            sendVirtualMessage(F("SMTP connected"), INFO);
            safeEnviarMensagemVirtual("DBG: heap=%u", DEBUG, (unsigned)ESP.getFreeHeap());
            // Reset failure counter after success in critical step
#if ENABLE_SMTP
            emailConsecutiveFailures = 0;
#endif
            currentEmailSendingState = EMAIL_AUTHENTICATING_SMTP;
            sendVirtualMessage(F("DBG: EMAIL AUTH"), DEBUG);
            break;
        }

        case EMAIL_AUTHENTICATING_SMTP: {
            // State entry log
            sendVirtualMessage(F("DBG: EMAIL AUTH start"), DEBUG);

            // ADAPTED: Using smtp.authenticate() with readymail_auth_password
            if (!smtp->authenticate(smtpUser.c_str(), smtpPass.c_str(), readymail_auth_password)) {
                sendVirtualMessage(F("SMTP: auth failed"), ERROR);
#if ENABLE_SMTP
                smtpTeardown();
                releaseSmtpResources(); //  Limpar recursos
                if (stateMachineHoldsSmtpMutex) { smtpUnlock(); stateMachineHoldsSmtpMutex = false; }
                emailConsecutiveFailures++;
                if(emailConsecutiveFailures >= EMAIL_FAILS_BEFORE_BREAKER){
                    emailBreakerUntil = millis() + EMAIL_BREAKER_COOLDOWN_MS;
                    safeEnviarMensagemVirtual("EMAIL_BREAKER: pause %lus", WARN, (unsigned)(EMAIL_BREAKER_COOLDOWN_MS/1000));
                }
#endif
                currentEmailSendingState = EMAIL_START_SENDING;
                sendVirtualMessage(F("DBG: EMAIL START (auth fail)"), DEBUG);
                break;
            }
            sendVirtualMessage(F("SMTP: auth OK"), INFO);
            safeEnviarMensagemVirtual("DBG: heap=%u", DEBUG, (unsigned)ESP.getFreeHeap());
            // ? Reset contador de falhas ap?s sucesso
#if ENABLE_SMTP
            emailConsecutiveFailures = 0;
#endif

            foundRecipientsForEmail.clear();
            //  For final matching, prefer raw data again if the .txt attachment is active
            {
                bool useAttach = configManager.getEmailAttachTxt(); //  Flag attach
                String src = (useAttach && emailRawDataCache.length()) ? emailRawDataCache : emailContentToSend; //  Escolhe fonte
                searchContent = src; //  Copia
                searchContent.toLowerCase(); //  Lowercase
            }
            //  "Equal" recipient match logic is now by complete word/phrase (case-insensitive), not substring
            // Use the centralized function supporting all logics, including "Exact Format" with multiple decimals (e.g. ddd.dd dd.ddd)
            String bodyLower = searchContent;
            bodyLower.toLowerCase();

            std::vector<std::pair<String,String>> foundRecipients;
            std::map<String,String> capturedValues;
            
            // False/True for sdActive? Here the state processes regardless of log blocking (pass false to avoid blocking internal send)
            findRecipientsInLowerText(bodyLower, foundRecipients, configManager.getSdcardEnabled(), &capturedValues);
            
            JsonArray numericRules = configManager.getRecipientSlots();
            for (const auto& pair : foundRecipients) {
                String ruleName = pair.first;
                String emails = pair.second;
                
                int op = 3; // Default 'I' if not found
                for (JsonObject r : numericRules) {
                    if ((r["name"] | String("")) == ruleName) {
                        op = opStrToInt(r["op"] | String(""));
                        break;
                    }
                }
                
                String valStr = "";
                if (capturedValues.find(ruleName) != capturedValues.end()) {
                    String rawStr = capturedValues[ruleName];
                    const char* opLabel = "";
                    if      (op==0) opLabel = "L";   // <
                    else if (op==1) opLabel = "H";   // >
                    else if (op==2) opLabel = "D";   // !=
                    else if (op==4) opLabel = "H=";  // >=
                    else if (op==5) opLabel = "L=";  // <=
                    else if (op==6) opLabel = "R";   // range
                    else if (op==7) opLabel = "DR";  // out of range
                    
                    if (strlen(opLabel) > 0) {
                        valStr = String(opLabel) + String(F(" ")) + rawStr;
                    } else {
                        valStr = rawStr;
                    }
                    foundNumericLabels.push_back(ruleName);
                }
                
                foundRecipientsForEmail.push_back(std::make_tuple(ruleName, emails, valStr));
                sendVirtualMessage(String(F("Recipient found: '")) + ruleName + String(F("' (")) + emails + String(F(") value=")) + valStr, INFO);
            }

            //  NEW LOGIC: the "dont_copy_sender" flag NO longer removes the sender from rule-derived lists
            // Motivation: user may repeat the same email (e.g., owner@example.com) in rules (I, D, +, etc.)
            // and expects to receive those sends even when they enabled the 'do not send automatic copy' option.
            //  This way the flag only prevents the sender from being included as the PRIMARY recipient (email destination)
            //  but does not interfere with destinations explicitly defined in the rules/numeric rules.
            // We keep implicit deduplication later when there are multiple segmented sends.
            //  (Previous filtering block deliberately removed — see git history for reference.)

            currentEmailSendingState = EMAIL_SENDING_MAIN_MESSAGE;
            safeEnviarMensagemVirtual("ST: -> EMAIL_SENDING_MAIN_MESSAGE", DEBUG);
            break;
        }

        case EMAIL_SENDING_MAIN_MESSAGE: {
            // State entry log
            safeEnviarMensagemVirtual("ST: EMAIL_SENDING_MAIN_MESSAGE", DEBUG);

            // ADAPTED: Using smtp.getMessage() to configure the email
            auto &mainMessage = smtp->getMessage();
            mainMessage.clear(); // Clear previous messages and headers

            // Configurar From
            String fromHeader = L(activeLang->emailFromName) + String(F(" <"));
            fromHeader += smtpUser;
            fromHeader += String(F(">"));
            mainMessage.headers.add(rfc822_from, fromHeader.c_str());

            String debugEmailDest = String(F("DEBUG_EMAIL: emailDest configurado: '"));
            debugEmailDest += emailDest;
            debugEmailDest += String(F("'"));
            sendVirtualMessage(debugEmailDest, DEBUG);

            // Recipient policy:
            // - SD enabled: do not include the SMTP sender as an automatic copy.
            // - SD disabled (RAM mode): include sender as an automatic copy only when dont_copy_sender is OFF.
            bool sdCardActive = configManager.getSdcardEnabled();
            bool copySenderEnabled = (!sdCardActive && !configManager.getEmailDontCopySender());

            auto addRecipientListToToHeader = [&](auto &msg, const String &listRaw, std::vector<String> &addedLower) -> bool {
                std::vector<String> parsedEmails;
                parsedEmails.reserve(4);
                runtimeEmailCollectUniqueRecipients(listRaw, addedLower, parsedEmails);
                for (const auto &emailOnly : parsedEmails) {
                    String toHeader = String(F("<"));
                    toHeader += emailOnly;
                    toHeader += String(F(">"));
                    msg.headers.add(rfc822_to, toHeader.c_str());
                }
                return !parsedEmails.empty();
            };

            bool mainRecipientAdded = false;
            std::vector<String> mainAddedLower;
            mainAddedLower.reserve(4);
            std::vector<String> mainAddedEmails;
            mainAddedEmails.reserve(4);

            auto addRecipientListToVectors = [&](const String &listRaw, std::vector<String> &addedLower, std::vector<String> &addedEmails) {
                runtimeEmailCollectUniqueRecipients(listRaw, addedLower, addedEmails);
            };
            
            if (!sdCardActive) {
                // ? Sem SD: adicionar remetente/destinat?rio principal (emailDest) normalmente
                mainRecipientAdded = addRecipientListToToHeader(mainMessage, emailDest, mainAddedLower);
                addRecipientListToVectors(emailDest, mainAddedLower, mainAddedEmails);

                // Optional automatic copy to sender (RAM mode only)
                if (copySenderEnabled) {
                    String senderEmail = smtpUser;
                    senderEmail.trim();
                    if (senderEmail.indexOf('@') > 0) {
                        String senderLower = senderEmail;
                        senderLower.toLowerCase();
                        bool already = false;
                        for (const auto &s : mainAddedLower) {
                            if (s == senderLower) { already = true; break; }
                        }
                        if (!already) {
                            String toHeader = String(F("<"));
                            toHeader += senderEmail;
                            toHeader += String(F(">"));
                            mainMessage.headers.add(rfc822_to, toHeader.c_str());
                            mainAddedLower.push_back(senderLower);
                            mainAddedEmails.push_back(senderEmail);
                            mainRecipientAdded = true;
                            sendVirtualMessage(String(F("INFO_EMAIL: Sender copy enabled -> added sender to main email.")), DEBUG);
                        }
                    }
                }

                if (mainRecipientAdded) {
                    String rlog = runtimeEmailBuildRecipientsLog(mainAddedEmails);
                    sendVirtualMessage(String(F("EMAIL_TO (main): ")) + rlog, INFO);
                }

                if (!mainRecipientAdded) {
                    if(!emailNoRecipientFlag){ sendVirtualMessage(String(F("DEBUG_EMAIL: Nenhum destinatario principal valido adicionado ao email.")), WARN); } // Evita duplicar apos early-abort
                }
            } else {
                // SD card active: do NOT add a sender, only recipients (rules)
                safeEnviarMensagemVirtual("SD_ACTIVE: Pulando remetente principal, usando apenas destinat%s (regras)", DEBUG, "?rios");
            }

            // Configurar Subject
            String mainSubject;
            if (!foundNumericLabels.empty()) {
                // Deduplicate labels while preserving order
                std::vector<String> uniqueLabels;
                for (const auto &lbl : foundNumericLabels) {
                    bool exists = false;
                    for (const auto &u : uniqueLabels) {
                        if (u.equalsIgnoreCase(lbl)) { exists = true; break; }
                    }
                    if (!exists) uniqueLabels.push_back(lbl);
                }

                // Join unique labels with comma
                mainSubject = String(F("\xF0\x9F\x93\x8A - ")) + configManager.getSystemName() + L(activeLang->emailRulesSuffix);
                (void)uniqueLabels; // Labels available in body; keep subject concise
            } else {
                mainSubject = String(F("\xF0\x9F\x93\x8A - ")) + configManager.getSystemName() + L(activeLang->emailRulesSuffix);
            }
            // ? Override espec?fico do teste (se definido): substituir o assunto calculado
            if (emailTestSubjectOverrideOnce.length() > 0) {
                mainSubject = emailTestSubjectOverrideOnce; // ? Usa o assunto de teste (nome do emulador + sufixo)
            }
            // Debug: log the subject being set for the main message to help diagnose empty-subject reports
            sendVirtualMessage(String(F("[DEBUG_EMAIL] mainSubject='")) + mainSubject + String(F("'")), DEBUG);
            mainMessage.headers.add(rfc822_subject, mainSubject.c_str());

            // Configurar Body
            // For the test, force a body without attachment (ignore attachment flag in this send)
            bool _testForceBodyOnly = emailTestForceBodyOnlyOnce;
            if (!_testForceBodyOnly && configManager.getEmailAttachTxt()) {
                //  Quando ativo, corpo ? fixo e dados v?o em anexo .txt
                String forcedBodyStr = L(activeLang->emailDataAttached);
                
                const char *forcedBody = forcedBodyStr.c_str(); //  Dynamic body
                String fileName = buildAttachmentFilename(); //  Dynamic time_date.txt name
                String contentType; //  MIME type to apply in the header
                //  IMPORTANT: prefer formatted TABLES in the attachment; fallback to RAW without duplicating heap data
                //  Priority: emailTablesDataCache -> emailRawDataCache -> move from emailContentToSend to emailRawDataCache
                const String* attachDataPtr = nullptr; //  Pointer to attachment source (avoids copy when possible)
                if (emailTablesDataCache.length()) {
                    attachDataPtr = &emailTablesDataCache; // Use tables, no copy
                } else if (emailRawDataCache.length()) {
                    attachDataPtr = &emailRawDataCache; // Raw cache already exists
                } else {
                    // Move the current body to the raw cache for reuse (copy then clear)
                    emailRawDataCache = emailContentToSend; // Copy current content to raw cache
                    emailContentToSend = String();           // Free the body's capacity
                    attachDataPtr = &emailRawDataCache;      // Source becomes the cache
                }
                // Ensures a clean body and reserves the exact space needed for MIME (minimizes reallocations)
                emailContentToSend = String(); //  Free body capacity
                emailContentToSend.reserve(attachDataPtr->length() + 256); //  Approximate reserve for MIME
                buildMultipartMixedPlainText(String(forcedBody), fileName, *attachDataPtr, contentType, emailContentToSend); //  Build MIME in the reused buffer

                //  Override header Content-Type to multipart/mixed with boundary
                mainMessage.headers.addCustom("Content-Type", contentType.c_str()); //  Uses ReadyMail addCustom
                //  Ensure explicit MIME version
                mainMessage.headers.addCustom("MIME-Version", "1.0"); //  Uses ReadyMail addCustom
                //  Avoid global re-encoding (QP) by the SMTP library
                mainMessage.headers.addCustom("Content-Transfer-Encoding", "8bit");
                //  Inject the complete MIME body (includes preamble and parts)
                mainMessage.text.body(emailContentToSend.c_str());
                // Nothing to free here: we avoid copies by using a pointer (attachDataPtr)

                // Debug notes
                sendVirtualMessage(String(F("EMAIL: sending with .txt attachment enabled; file: ")) + fileName, DEBUG);
            } else {
                //  Comportamento antigo: corpo cont?m os dados
                mainMessage.text.body(emailContentToSend.c_str());
            }

            String debugContentLength = String(F("DEBUG_EMAIL: Main email content (length: "));
            debugContentLength += String(emailContentToSend.length());
            debugContentLength += String(F(" bytes)"));
            sendVirtualMessage(debugContentLength, DEBUG);

            // Web monitor sampling removed (was: exibirAmostraTransmissao)

            String totalEmailSize = String(F("TOTAL email size to be sent: "));
            // Calculate after potential MIME reconstruction to reflect the actual current size
            totalEmailSize += String(emailContentToSend.length());
            totalEmailSize += String(F(" bytes"));
            sendVirtualMessage(totalEmailSize, DEBUG);

            int linesInEmailToSend = 0;
            for(char c : emailContentToSend) {
                if (c == '\n') linesInEmailToSend++;
            }
            String linesCountDebug = String(F("Number of lines in the email to send: "));
            linesCountDebug += String(linesInEmailToSend);
            sendVirtualMessage(linesCountDebug, DEBUG);

            String heapBeforeSend = String(F("Free heap (before smtp.send - main): "));
            heapBeforeSend += String(ESP.getFreeHeap());
            heapBeforeSend += String(F(" bytes"));
            sendVirtualMessage(heapBeforeSend, DEBUG);

            // ADAPTED: Using smtp.send() with the configured message
            bool sendSuccess = false;
            if (mainRecipientAdded) {
                taskYIELD(); // Yield CPU explicitly to FreeRTOS scheduler
                delay(100); // Long pause to stabilize WiFi/TLS before blocking operation
                sendSuccess = smtp->send(mainMessage);
                taskYIELD(); // Yield CPU after the blocking operation
                delay(50); // Pause after send
                String sendResultDebug = String(F("DEBUG_EMAIL: Resultado smtp.send() para email principal: "));
                sendResultDebug += (sendSuccess ? String(F("SUCESSO")) : String(F("FALHA")));
                sendVirtualMessage(sendResultDebug, DEBUG);
            } else {
                if(!emailNoRecipientFlag){ sendVirtualMessage(String(F("DEBUG_EMAIL: No valid main recipient configured; skipping main send and proceeding to individual sends (if any).")), WARN); } // Suppress if already aborted early
            }

            String heapAfterSend = String(F("Heap livre (depois de smtp.send - main): "));
            heapAfterSend += String(ESP.getFreeHeap());
            heapAfterSend += String(F(" bytes"));
            sendVirtualMessage(heapAfterSend, DEBUG);

            // If there are no individual sends, we can free the main MIME body now to reduce heap pressure
            if (foundRecipientsForEmail.empty()) {
                // Free the String's capacity for the body
                emailContentToSend = String(); // Force immediate capacity release
                String heapAfterSwap = String(F("Free heap (after freeing main body): "));
                heapAfterSwap += String(ESP.getFreeHeap());
                heapAfterSwap += String(F(" bytes"));
                sendVirtualMessage(heapAfterSwap, DEBUG);
            }


            // If main message was sent successfully or skipped due to no main recipients, continue to individual messages
            if (sendSuccess || (!mainRecipientAdded && foundRecipientsForEmail.size() > 0) || (sendSuccess==false && foundRecipientsForEmail.size() > 0)) {
                // IMPORTANT: ReadyMail may keep internal references to the last message body.
                // We rebuild `emailContentToSend` for individual emails, so clear the internal message first.
                // This must happen before we reuse/free `emailContentToSend`.
                if (!foundRecipientsForEmail.empty() && smtp) {
                    smtp->getMessage().clear();
                    // Release the large body buffer early to reduce heap pressure before building per-recipient MIME.
                    emailContentToSend = String();
                }
                // If main send failed but we have individuals, proceed to try individuals
                currentEmailSendingState = EMAIL_SENDING_INDIVIDUAL_MESSAGES;
                safeEnviarMensagemVirtual("ST: -> EMAIL_SENDING_INDIVIDUAL_MESSAGES", DEBUG);
            } else if (!sendSuccess && mainRecipientAdded && foundRecipientsForEmail.size() == 0) {
                // No main recipients were added and no individuals to try -> treat as failure and retry cycle
                sendVirtualMessage(String(F("SMTP send failed for main recipient: Unknown or unreported error.")), ERROR);
                    sendVirtualMessage(String(F("EMAIL_ERR: recipient failure")), ERROR);
#if ENABLE_SMTP
                smtpTeardown();
                emailConsecutiveFailures++;
                if(emailConsecutiveFailures >= EMAIL_FAILS_BEFORE_BREAKER){
                    emailBreakerUntil = millis() + EMAIL_BREAKER_COOLDOWN_MS;
                    sendVirtualMessage(String(F("EMAIL_BREAKER: too many consecutive failures, pausing new attempts for ")) + String(EMAIL_BREAKER_COOLDOWN_MS/1000) + String(F("s")), WARN);
                }
#endif
                currentEmailSendingState = EMAIL_START_SENDING;
                safeEnviarMensagemVirtual("ST: -> EMAIL_START_SENDING (msg fail)", DEBUG);
            } else {
                // No recipients at all
                if(!emailNoRecipientFlag){ sendVirtualMessage(String(F("DEBUG_EMAIL: No recipient configured. Aborting send.")), WARN); } // Suprime duplicado
                currentEmailSendingState = EMAIL_START_SENDING;
                safeEnviarMensagemVirtual("ST: -> EMAIL_START_SENDING (no rcpt)", DEBUG);
            }
            break;
        }

        case EMAIL_SENDING_INDIVIDUAL_MESSAGES: {
            // State entry log
            safeEnviarMensagemVirtual("ST: EMAIL_SENDING_INDIVIDUAL_MESSAGES", DEBUG);

            bool allIndividualSent = true;
            bool sdCardActive = configManager.getSdcardEnabled();
            bool copySenderEnabled = (!sdCardActive && !configManager.getEmailDontCopySender());

            auto addRecipientListToToHeader = [&](auto &msg, const String &listRaw, std::vector<String> &addedLower) -> bool {
                std::vector<String> parsedEmails;
                parsedEmails.reserve(4);
                runtimeEmailCollectUniqueRecipients(listRaw, addedLower, parsedEmails);
                for (const auto &emailOnly : parsedEmails) {
                    String toHeader = String(F("<"));
                    toHeader += emailOnly;
                    toHeader += String(F(">"));
                    msg.headers.add(rfc822_to, toHeader.c_str());
                }
                return !parsedEmails.empty();
            };

            for (const auto& rec : foundRecipientsForEmail) {
                // ADAPTED: Using smtp.getMessage() to configure each individual email
                auto &individualMessage = smtp->getMessage();
                individualMessage.clear(); // Clear previous messages and headers

                // Configurar From
                String fromHeaderInd = L(activeLang->emailFromName) + String(F(" <"));
                fromHeaderInd += smtpUser;
                fromHeaderInd += String(F(">")); 
                individualMessage.headers.add(rfc822_from, fromHeaderInd.c_str());

                // Configure To recipients from rules (supports multiple addresses separated by ',' or ';')
                std::vector<String> indAddedLower;
                indAddedLower.reserve(4);
                std::vector<String> indAddedEmails;
                indAddedEmails.reserve(4);
                bool indRecipientAdded = addRecipientListToToHeader(individualMessage, std::get<1>(rec), indAddedLower);

                // Track exact addresses used for logging
                runtimeEmailCollectUniqueRecipients(std::get<1>(rec), indAddedLower, indAddedEmails);

                // Optional automatic copy to sender (RAM mode only)
                if (copySenderEnabled) {
                    String senderEmail = smtpUser;
                    senderEmail.trim();
                    if (senderEmail.indexOf('@') > 0) {
                        String senderLower = senderEmail;
                        senderLower.toLowerCase();
                        bool already = false;
                        for (const auto &s : indAddedLower) {
                            if (s == senderLower) { already = true; break; }
                        }
                        if (!already) {
                            String toHeader = String(F("<"));
                            toHeader += senderEmail;
                            toHeader += String(F(">"));
                            individualMessage.headers.add(rfc822_to, toHeader.c_str());
                            indAddedLower.push_back(senderLower);
                            indAddedEmails.push_back(senderEmail);
                        }
                    }
                }

                if (indRecipientAdded) {
                    String rlog = runtimeEmailBuildRecipientsLog(indAddedEmails);
                    sendVirtualMessage(String(F("EMAIL_TO (rule ")) + std::get<0>(rec) + String(F("): ")) + rlog, INFO);
                }

                // Build subject using the decorated format: emoji + rule + operator + threshold + value.
                // valStr has format "L 020.94" (opLabel space numericValue); extract only the numeric part.
                // Only use capturedNum if it contains at least one digit (guard against stray '-' or '.').
                String subjectInd;
                {
                    String valStr = std::get<2>(rec);
                    int spIdx = valStr.indexOf(' ');
                    String capturedNum = (spIdx >= 0) ? valStr.substring(spIdx + 1) : valStr;
                    capturedNum.trim();
                    bool hasDigit = false;
                    for (int _i = 0; _i < (int)capturedNum.length(); _i++) {
                        if (isDigit(capturedNum.charAt(_i))) { hasDigit = true; break; }
                    }
                    if (!hasDigit) capturedNum = "";
                    subjectInd = buildRecipientEmailSubject(std::get<0>(rec), capturedNum);
                }
                // ? Override espec?fico do teste: usa o mesmo assunto do principal se definido
                if (emailTestSubjectOverrideOnce.length() > 0) {
                    subjectInd = emailTestSubjectOverrideOnce; // ? Uniformiza assunto no modo teste
                }
                // Debug: log the subject for this individual recipient
                sendVirtualMessage(String(F("[DEBUG_EMAIL] individual subject='")) + subjectInd + String(F("' for recipient='")) + std::get<0>(rec) + String(F("'")), DEBUG);
                individualMessage.headers.add(rfc822_subject, subjectInd.c_str());

            // Configurar Body
                // ? Para o teste, for?ar corpo sem anexo (ignorar flag anexo)
                bool _testForceBodyOnlyInd = emailTestForceBodyOnlyOnce;
                if (!_testForceBodyOnlyInd && configManager.getEmailAttachTxt()) {
                    //  Individual email with .txt attachment; body shows fixed line + rule summary (same as subject).
                    String forcedBodyStr = L(activeLang->emailDataAttached) + String(F("\n")) + subjectInd;
                    
                    const char *forcedBody = forcedBodyStr.c_str(); //  Dynamic body
                    String fileName = buildAttachmentFilename(); //  Dynamic time_date.txt name
                    String contentType; //  MIME type
                    //  Prefer the TABULATED cache; fallback to RAW; last resort: move body to RAW cache without duplicating
                    const String* attachDataPtr = nullptr; //  Pointer to avoid copies
                    if (emailTablesDataCache.length()) {
                        attachDataPtr = &emailTablesDataCache; // Tables
                    } else if (emailRawDataCache.length()) {
                        attachDataPtr = &emailRawDataCache; // Existing raw
                    } else {
                        //  Move current body to raw cache for reuse in remaining individual sends
                        emailRawDataCache = emailContentToSend; //  Copy to raw cache
                        emailContentToSend = String();          //  Free body
                        attachDataPtr = &emailRawDataCache;     //  Becomes the source
                    }
                    emailContentToSend = String(); //  Free body capacity before building MIME
                    emailContentToSend.reserve(attachDataPtr->length() + 256); //  Approximate reserve
                    buildMultipartMixedPlainText(String(forcedBody), fileName, *attachDataPtr, contentType, emailContentToSend); //  MIME in reused buffer
                    individualMessage.headers.addCustom("Content-Type", contentType.c_str()); //  Set multipart Content-Type
                    individualMessage.headers.addCustom("MIME-Version", "1.0"); //  MIME-Version header
                    individualMessage.headers.addCustom("Content-Transfer-Encoding", "8bit"); //  Avoid global re-encoding
                    individualMessage.text.body(emailContentToSend.c_str()); //  Complete MIME body
                    //  No extra temporary buffer; attachDataPtr points to a live String
                    sendVirtualMessage(String(F("EMAIL: individual sending with .txt attachment; file: ")) + fileName, DEBUG); // Log
                } else {
                //  Old behavior: body holds the data
                    individualMessage.text.body(emailContentToSend.c_str());
                }

                String debugContentInd = String(F("DEBUG_EMAIL: Conteudo do email individual (length: "));
                debugContentInd += String(emailContentToSend.length());
                debugContentInd += String(F(" bytes) para '"));
                debugContentInd += std::get<0>(rec);
                debugContentInd += String(F("'"));
                sendVirtualMessage(debugContentInd, DEBUG);

                String bodyPreviewInd = String(F("Corpo do email para '"));
                bodyPreviewInd += std::get<0>(rec);
                bodyPreviewInd += String(F("' (primeiros 200 chars):"));
                sendVirtualMessage(bodyPreviewInd, DEBUG); // Also for DEBUG
                sendVirtualMessage(emailContentToSend.substring(0, min((int)emailContentToSend.length(), 200)), DEBUG); // Also for DEBUG

                String heapBeforeSendInd = String(F("Heap livre (antes de smtp.send - individual): "));
                heapBeforeSendInd += String(ESP.getFreeHeap());
                heapBeforeSendInd += String(F(" bytes"));
                sendVirtualMessage(heapBeforeSendInd, DEBUG);

                bool sendSuccess = false;
                if (!indRecipientAdded) {
                    sendVirtualMessage(String(F("EMAIL: individual skipped (no valid To recipients)")), WARN);
                    sendSuccess = false;
                } else {
            // ADAPTED: Using smtp.send() with the configured message
                    taskYIELD(); //  Yield CPU explicitly to the FreeRTOS scheduler
                    delay(100); //  Long pause to stabilize WiFi/TLS
                    sendSuccess = smtp->send(individualMessage);
                }
                taskYIELD(); // Yield CPU after a blocking operation
                delay(50); //  Pause after send
                String sendResultInd = String(F("DEBUG_EMAIL: Resultado smtp.send() para email individual '"));
                sendResultInd += std::get<0>(rec);
                sendResultInd += String(F("': "));
                sendResultInd += (sendSuccess ? String(F("SUCESSO")) : String(F("FALHA")));
                sendVirtualMessage(sendResultInd, DEBUG);

                String heapAfterSendInd = String(F("Heap livre (depois de smtp.send - individual): "));
                heapAfterSendInd += String(ESP.getFreeHeap());
                heapAfterSendInd += String(F(" bytes"));
                sendVirtualMessage(heapAfterSendInd, DEBUG);


                if (sendSuccess) {
                    String emailSentMsg = String(F("Email para '"));
                    emailSentMsg += std::get<0>(rec);
                    emailSentMsg += String(F("' enviado com sucesso."));
                    sendVirtualMessage(emailSentMsg, INFO);
                    // Log email sent to recipient in SD log file (visible in log.txt)
                    logEmailSent("SMTP", "DATA", subjectInd, std::get<1>(rec));
                    #if ENABLE_LCD_SCREEN==1
                    screenManager.showEmailSent();
                    #endif
                } else {
                    String emailFailedMsg = String(F("Falha envio SMTP para '"));
                    emailFailedMsg += std::get<0>(rec);
                    emailFailedMsg += String(F("': Erro desconhecido ou nao reportado diretamente."));
                    sendVirtualMessage(emailFailedMsg, ERROR);
                    allIndividualSent = false;
#if ENABLE_SMTP
                    smtpTeardown();
                    emailConsecutiveFailures++;
                    if(emailConsecutiveFailures >= EMAIL_FAILS_BEFORE_BREAKER){
                        emailBreakerUntil = millis() + EMAIL_BREAKER_COOLDOWN_MS;
                        safeEnviarMensagemVirtual("EMAIL_BREAKER: pause %lus", WARN, (unsigned)(EMAIL_BREAKER_COOLDOWN_MS/1000));
                    }
#endif
                }
            }

            if (allIndividualSent) {
                currentEmailSendingState = EMAIL_FINISHED_SUCCESS;
                safeEnviarMensagemVirtual("ST: -> EMAIL_FINISHED_SUCCESS (indiv)", DEBUG);
            } else {
                currentEmailSendingState = EMAIL_FINISHED_FAILURE;
                safeEnviarMensagemVirtual("ST: -> EMAIL_FINISHED_FAILURE (indiv)", DEBUG);
            }
            break;
        }

        case EMAIL_FINISHED_SUCCESS: {
            // State entry log
            safeEnviarMensagemVirtual("ST: EMAIL_FINISHED_SUCCESS", DEBUG);

            if (emailMatchedRulesToFinalize.length() > 0) {
                finalizeMatchedRulesResult(emailMatchedRulesToFinalize, true);
                emailMatchedRulesToFinalize = "";
            }

            safeEnviarMensagemVirtual("DBG: heap=%u", DEBUG, (unsigned)ESP.getFreeHeap());

            // Cleanup: avoid calling ReadyMail message clear here.
            // The internal message may hold stale pointers if the body buffer was reused.
            yield();
            emailContentToSend = String(); //  Liberta capacidade do corpo MIME/Texto
            emailRawDataCache = String(); //  Liberta cache de dados brutos
            emailTablesDataCache = String(); //  Liberta cache de dados tabelados (ISQ20)
            manualEmailContentBuffer = String(); //  Liberta buffer de envio manual (se usado)
            ultimoConteudoPreparado = String(); //  Limpa refer?ncia do ?ltimo conte?do
            yield(); //  Yield CPU after releases
            std::vector<std::tuple<String, String, String>>().swap(foundRecipientsForEmail); //  Free vector capacity
            if (smtpClient) {
                smtpClient->stop();
                smtpClient->flush();
                delay(100);
            }
            //  Do NOT rebuild smtpClient here - SMTPClient keeps an internal reference that would become invalid
            releaseSmtpResources(); //  Free dynamic resources
            yield(); //  Yield CPU after releases
            // Clear one-shot test overrides
            emailTestForceBodyOnlyOnce = false; //  Disarm
            emailTestSubjectOverrideOnce = "";  //  Clear
            String heapAfterCleanupOk = String(F("Heap livre (apos cleanup sucesso): "));
            heapAfterCleanupOk += String(ESP.getFreeHeap());
            heapAfterCleanupOk += String(F(" bytes"));
            sendVirtualMessage(heapAfterCleanupOk, DEBUG);

#if ENABLE_SMTP
            // Reset failure circuit after a successful cycle
            emailConsecutiveFailures = 0;
            emailBreakerUntil = 0;
#endif

            // Short message with no BUSY reference to save heap
            char bufEmailSent[40];
            snprintf(bufEmailSent, sizeof(bufEmailSent), "Email OK. Pausa %ums.", (unsigned)BUSY_DEACTIVATION_DELAY_MS);
            sendVirtualMessage(String(bufEmailSent), INFO);

            // Clear LPT cache after successful send (backup was already saved before send)
            if (lastSentEmailWasLPT) {
                lptEmailContentForSave = "";
                lastSentEmailWasLPT = false;
            }

            currentEmailSendingState = EMAIL_DELAY_BUSY_DEACTIVATION;
            if (enableDebugLogs) safeEnviarMensagemVirtual("ST: -> EMAIL_DELAY_BUSY_DEACTIVATION (ok)", DEBUG);
            busyDeactivationStartTime = currentMillis;
            lastEmailTriggerSource = TRIGGER_NONE;
            emailProcessInitiatedThisCycle = false;
            if (stateMachineHoldsSmtpMutex) { smtpUnlock(); stateMachineHoldsSmtpMutex = false; }
            if (stateMachineHoldsPriorityEmail) { priorityManager().endEmail(); stateMachineHoldsPriorityEmail = false; }
            safeEnviarMensagemVirtual("ST: init=0 (ok)", DEBUG);
            //  Cleanup and resume RS232 after cycle
            if (rs232HoldXonUntilEmailDone && isFlowControlEngaged()) {
                //  FULL-DRAIN: ensure counter cleanup and open XON only now
                (void)bufferDadosTryClear(pdMS_TO_TICKS(200));
                bufferDadosLineCount = 0;
                sendRs232Xon();
                rs232HoldXonUntilEmailDone = false;
            } else if (isFlowControlEngaged()) {
                sendRs232Xon();
            }
            rs232ChunkPauseActive = false; rs232ForceLines66NextEmail = false; rs232LineCount = 0;
            //  Ap?s um ciclo de email, regressar par?metros de guarda
            rs232FirstResponseGuardActive = false; rs232FirstResponseMinLines = RS232_FIRST_RESPONSE_MIN_LINES;
            break;
        }

        case EMAIL_FINISHED_FAILURE: {
            // State entry log
            safeEnviarMensagemVirtual("ST: EMAIL_FINISHED_FAILURE", DEBUG);

            if (emailMatchedRulesToFinalize.length() > 0) {
                finalizeMatchedRulesResult(emailMatchedRulesToFinalize, false);
                emailMatchedRulesToFinalize = "";
            }

            sendVirtualMessage(String(F("All email sending attempts failed.")), ERROR);

            safeEnviarMensagemVirtual("DBG: heap=%u", DEBUG, (unsigned)ESP.getFreeHeap());

            // Cleanup: avoid calling ReadyMail message clear here.
            // The internal message may hold stale pointers if the body buffer was reused.
            yield();
            emailContentToSend = String(); //  Liberta capacidade do corpo MIME/Texto
            emailRawDataCache = String(); //  Liberta cache de dados brutos
            emailTablesDataCache = String(); //  Liberta cache de dados tabelados (ISQ20)
            manualEmailContentBuffer = String(); //  Liberta buffer de envio manual (se usado)
            ultimoConteudoPreparado = String(); //  Limpa refer?ncia do ?ltimo conte?do
            yield(); //  Yield CPU after releases
            std::vector<std::tuple<String, String, String>>().swap(foundRecipientsForEmail); //  Free vector capacity
            if (smtpClient) {
                smtpClient->stop();
                smtpClient->flush();
                delay(100);
            }
            //  Do NOT rebuild smtpClient here - SMTPClient keeps an internal reference that would become invalid
            releaseSmtpResources(); //  Free dynamic resources
            yield(); //  Yield CPU after releases
            // Clear one-shot test overrides
            emailTestForceBodyOnlyOnce = false; //  Disarm
            emailTestSubjectOverrideOnce = "";  //  Clear
            String heapAfterCleanupFail = String(F("Heap livre (apos cleanup falha): "));
            heapAfterCleanupFail += String(ESP.getFreeHeap());
            heapAfterCleanupFail += String(F(" bytes"));
            sendVirtualMessage(heapAfterCleanupFail, DEBUG);

#if ENABLE_SMTP
            //  On final failure, reinforce breaker if not yet active to avoid tight loops
            if(emailConsecutiveFailures < EMAIL_FAILS_BEFORE_BREAKER){
                emailConsecutiveFailures = EMAIL_FAILS_BEFORE_BREAKER;
            }
            if(!emailBreakerUntil){
                emailBreakerUntil = millis() + EMAIL_BREAKER_COOLDOWN_MS;
                safeEnviarMensagemVirtual("EMAIL_BREAKER: pause %lus (final)", WARN, (unsigned)(EMAIL_BREAKER_COOLDOWN_MS/1000));
            }
#endif

            char bufEmailFail[64];
            snprintf(bufEmailFail, sizeof(bufEmailFail), "Envio falhou. Pausa %ums.", (unsigned)BUSY_DEACTIVATION_DELAY_MS);
            sendVirtualMessage(String(bufEmailFail), WARN);

            //  Do not clear the buffer on failure: preserve data for next send
            //  If we are holding XON (full-drain), open now to avoid blocking the emitter
            if (rs232HoldXonUntilEmailDone && isFlowControlEngaged()) {
                sendRs232Xon();
                //  Also resume the Nano (propagate XON upstream)
                nanoBridgeSendXon();
                rs232HoldXonUntilEmailDone = false;
            }

            currentEmailSendingState = EMAIL_DELAY_BUSY_DEACTIVATION;
            if (enableDebugLogs) safeEnviarMensagemVirtual("ST: -> EMAIL_DELAY_BUSY_DEACTIVATION (fail)", DEBUG);
            busyDeactivationStartTime = currentMillis;
            lastEmailTriggerSource = TRIGGER_NONE;
            emailProcessInitiatedThisCycle = false;
            if (stateMachineHoldsSmtpMutex) { smtpUnlock(); stateMachineHoldsSmtpMutex = false; }
            if (stateMachineHoldsPriorityEmail) { priorityManager().endEmail(); stateMachineHoldsPriorityEmail = false; }
            safeEnviarMensagemVirtual("ST: init=0 (fail)", DEBUG);
            //  Cleanup and resume RS232 after failure (idempotent)
                if (isFlowControlEngaged()) { sendRs232Xon(); }
                //  Also resume the Nano (propagate XON upstream)
                nanoBridgeSendXon();
            rs232ChunkPauseActive = false; rs232ForceLines66NextEmail = false; rs232LineCount = 0;
            rs232FirstResponseGuardActive = false; rs232FirstResponseMinLines = RS232_FIRST_RESPONSE_MIN_LINES;
            break;
        }

        case EMAIL_DELAY_BUSY_DEACTIVATION: {
            // State entry log
            if (enableDebugLogs) safeEnviarMensagemVirtual("DBG: EMAIL_DELAY_BUSY_DEACTIVATION", DEBUG);

            if (currentMillis - busyDeactivationStartTime >= BUSY_DEACTIVATION_DELAY_MS) {
                // Release holdBusyDuringEmail and lower BUSY
                if (holdBusyDuringEmail || emulatorShouldBeBusy) {
                    setBusyPinState(false);
                    // resume RS232 senders only if flow control active
                    if (isFlowControlEngaged()) {
                        sendRs232Xon();
                        //  Also resume the Nano (propagate XON upstream)
                        nanoBridgeSendXon();
                    }
                    holdBusyDuringEmail = false;
                    emulatorShouldBeBusy = false;
                    pauseInternalBufferTransfer = false;
                    safeEnviarMensagemVirtual("BUSY deactivated after %ums. Transfer resumed.", DEBUG, (unsigned)BUSY_DEACTIVATION_DELAY_MS);
                }
                currentEmailSendingState = EMAIL_IDLE;
                //  Restore DEBUG suppression when returning to IDLE
                if(emailLogSuppressionActive){
                    enableDebugLogs = savedEnableDebugLogsBeforeEmail; //  Restore previous state
                    emailLogSuppressionActive = false;                 // Suppression ends
                }
                // Deactivate the flag when the email process ends
                isEmailSendingActive = false; 
                if (enableDebugLogs) safeEnviarMensagemVirtual("ST: -> EMAIL_IDLE (BUSY)", DEBUG);
                lastReceptionTime = currentMillis;
                emailProcessInitiatedThisCycle = false;
                if (enableDebugLogs) safeEnviarMensagemVirtual("ST: init=0 (BUSY)", DEBUG);
                //  Also ensure cleanup here in case previous states were skipped
                // Nothing to do: without flow control we do not send XON/XOFF
                rs232ChunkPauseActive = false; rs232ForceLines66NextEmail = false; rs232LineCount = 0;
                rs232FirstResponseGuardActive = false; rs232FirstResponseMinLines = RS232_FIRST_RESPONSE_MIN_LINES;
            }
            break;
        }
    }
}