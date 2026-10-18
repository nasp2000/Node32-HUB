// recipient_matching_impl.h
// Included only from chatgpt.ino (single TU).
// Recipient throttling, rule matching, hysteresis, count/presence logic.
// Includes: findLastCompleteLineInBuffer, isRecipientThrottled, markRecipientNotified,
// collectMatchedRulesFromContent, checkPresenceWatchdog, findRecipientsInLowerText, etc.
// DO NOT add #pragma once - this is an impl-include file, not a shared header.

#include "recipient_matching_utils.h"
#include <map>

// Recipient tracking maps (slot index 1-50)
static std::map<int, unsigned long> recipientLastNotificationTime;
static std::map<int, double> recipientLastSeenValue;
static std::map<int, int> recipientMatchCount;
static std::map<int, unsigned long> recipientPresenceLastSeen;
static std::map<int, int> recipientCwCount;
static std::map<int, unsigned long> recipientCwWindowStart;
static std::map<int, bool> recipientHystFired;
static std::map<int, bool> recipientPendingNotification;
static std::map<int, bool> recipientCountTriggered;
static std::map<int, bool> recipientCwTriggered;

// Forward declarations for functions defined later in this file
static bool shouldBlockRecipientTrigger(const String &ruleName, const String &ruleNameLower);
static String getFreqLabelForRule(const String &ruleName);
// Helper: Find the last complete line in a buffer (returns index of last element of complete line, or -1)
// A complete line ends with '\n' or '\r'. This ensures we never cut data mid-line.
static int findLastCompleteLineInBuffer(const std::vector<TimestampedData, PsramAllocatorStd<TimestampedData>>* buffer) {
    if (!buffer || buffer->empty()) return -1;
    
    // Scan backwards from the end to find the last newline character
    for (int i = buffer->size() - 1; i >= 0; i--) {
        const TimestampedData& item = (*buffer)[i];
        size_t len = item.dataLength();
        if (len > 0) {
            // Access last character from vector directly
            char lastChar = item.data[len - 1];
            if (lastChar == '\n' || lastChar == '\r') {
                return i; // Found complete line at index i
            }
        }
    }
    
    return -1; // No complete line found
}

// forceSwapSdcardBuffersForEmailday — moved to src/runtime_storage.cpp

// safeFlushBeforeCriticalCommand — moved to src/runtime_storage.cpp

// Helper: find recipients matched in a text line (expects text already lowercased)
// Uses the same logic as EMAIL_PREPARE_CONTENT and processSdEmailQueue: word-boundary tokens + full numeric rules.

// Helper: Check if recipient CAN receive notification (WITHOUT updating timestamp)
// Returns TRUE if should BLOCK (throttled), FALSE if can send
static bool isRecipientThrottled(const String &recipientName, const String &recipientNameLower) {
    int slotIdx = configManager.findRecipientSlotIndexByName(recipientNameLower);
    if (slotIdx <= 0) {
        return false; // Not found in slots, no throttling
    }
    
    int freqSec = configManager.getRecipientSlotFrequency(slotIdx);
    if (freqSec < 180) freqSec = 180; // Minimum 3 minutes between alerts
    
    if (recipientThrottleMutex == NULL) return false;
    
    if (xSemaphoreTakeRecursive(recipientThrottleMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        auto it = recipientLastNotificationTime.find(slotIdx);
        if (it == recipientLastNotificationTime.end()) {
            return false; // No timestamp, allow first
        } else {
            return true; // Timestamp exists, assume throttled for safety
        }
    }
    
    unsigned long now = millis();
    unsigned long lastTime = recipientLastNotificationTime[slotIdx];
    unsigned long elapsed = now - lastTime;
    unsigned long freqMs = (unsigned long)freqSec * 1000UL;
    bool throttled = (lastTime > 0 && elapsed < freqMs);
    
    // Log the check (without updating timestamp)
    String debugMsg = String(F("🔍 Throttle CHECK (read-only): ")) + recipientName + 
                     String(F(" | slot=")) + String(slotIdx) +
                     String(F(" | freq=")) + String(freqSec) + String(F("s")) +
                     String(F(" | elapsed=")) + String(elapsed/1000.0, 1) + String(F("s")) +
                     String(F(" | decision=")) + (throttled ? F("BLOCK") : F("ALLOW"));
    sendVirtualMessage(debugMsg, DEBUG);
    
    xSemaphoreGiveRecursive(recipientThrottleMutex);
    return throttled;
}

// Helper: Mark recipient as notified (update timestamp)
// Call this AFTER email is successfully queued/sent
static void markRecipientNotified(const String &recipientName, const String &recipientNameLower) {
    int slotIdx = configManager.findRecipientSlotIndexByName(recipientNameLower);
    if (slotIdx <= 0) return; // Not in slots
    
    if (recipientThrottleMutex == NULL) return;
    
    if (xSemaphoreTakeRecursive(recipientThrottleMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        sendVirtualMessage(String(F("⚠️ Failed to mark notified (mutex timeout): ")) + recipientName, WARN);
        return;
    }
    
    unsigned long now = millis();
    recipientLastNotificationTime[slotIdx] = now;
    
    sendVirtualMessage(String(F("✅ Recipient timestamp UPDATED: ")) + recipientName + 
                         String(F(" at ")) + String(now), DEBUG);
    
    xSemaphoreGiveRecursive(recipientThrottleMutex);
}

static int getRecipientOpForSlotIndex(int slotIdx) {
    if (slotIdx < 1 || slotIdx > 50) return -1;
    JsonArray slots = configManager.getRecipientSlots();
    if (slotIdx > (int)slots.size()) return -1;
    JsonObject slot = slots[slotIdx - 1];
    return opStrToInt(slot["op"] | String(""));
}

static bool isRecipientPendingNotification(const String &recipientNameLower) {
    int slotIdx = configManager.findRecipientSlotIndexByName(recipientNameLower);
    if (slotIdx <= 0 || recipientThrottleMutex == NULL) return false;
    if (xSemaphoreTakeRecursive(recipientThrottleMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return false;
    bool pending = recipientPendingNotification.count(slotIdx) && recipientPendingNotification[slotIdx];
    xSemaphoreGiveRecursive(recipientThrottleMutex);
    return pending;
}

static void setRecipientPendingNotification(const String &recipientName, const String &recipientNameLower, bool pending) {
    int slotIdx = configManager.findRecipientSlotIndexByName(recipientNameLower);
    if (slotIdx <= 0 || recipientThrottleMutex == NULL) return;
    if (xSemaphoreTakeRecursive(recipientThrottleMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        sendVirtualMessage(String(F("⚠️ Failed to change pending state: ")) + recipientName, WARN);
        return;
    }
    if (pending) recipientPendingNotification[slotIdx] = true;
    else recipientPendingNotification.erase(slotIdx);
    xSemaphoreGiveRecursive(recipientThrottleMutex);
}

static void finalizeRecipientNotificationResult(const String &recipientName, const String &recipientNameLower, bool success) {
    int slotIdx = configManager.findRecipientSlotIndexByName(recipientNameLower);
    if (slotIdx <= 0 || recipientThrottleMutex == NULL) return;

    int op = getRecipientOpForSlotIndex(slotIdx);
    if (xSemaphoreTakeRecursive(recipientThrottleMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        sendVirtualMessage(String(F("⚠️ Failed to finalize recipient state: ")) + recipientName, WARN);
        return;
    }

    recipientPendingNotification.erase(slotIdx);
    if (success) {
        recipientLastNotificationTime[slotIdx] = millis();
        if (op == 9) {
            recipientMatchCount[slotIdx] = 0;
            recipientCountTriggered.erase(slotIdx);
        } else if (op == 11) {
            recipientCwCount[slotIdx] = 0;
            recipientCwWindowStart.erase(slotIdx);
            recipientCwTriggered.erase(slotIdx);
        }
    }

    xSemaphoreGiveRecursive(recipientThrottleMutex);
}

void resetRecipientThrottleSlot(int slotIdx) {
    if (slotIdx <= 0 || recipientThrottleMutex == NULL) return;
    if (xSemaphoreTakeRecursive(recipientThrottleMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
    recipientLastNotificationTime.erase(slotIdx);
    xSemaphoreGiveRecursive(recipientThrottleMutex);
    Serial.printf("[RECIP_POST] Throttle timer reset for slot %d\n", slotIdx);
}

#if ENABLE_SDCARD==1
static void addDataToSdcardBuffer(const String &source, const String &data);

void logRs232TestPayloadToSd() {
    addDataToSdcardBuffer("RS232", String(F("RS232_TEST\r\n")));
}
#else
void logRs232TestPayloadToSd() {} // SD disabled — no-op
#endif

void finalizeMatchedRulesResult(const String &matchedRules, bool success) {
    int start = 0;
    while (start <= (int)matchedRules.length()) {
        int sep = matchedRules.indexOf('|', start);
        if (sep < 0) sep = matchedRules.length();
        String ruleName = matchedRules.substring(start, sep);
        ruleName.trim();
        if (ruleName.length() > 0) {
            String ruleNameLower = ruleName;
            ruleNameLower.toLowerCase();
            finalizeRecipientNotificationResult(ruleName, ruleNameLower, success);
        }
        if (sep >= (int)matchedRules.length()) break;
        start = sep + 1;
    }
}

void markMatchedRulesPending(const String &matchedRules) {
    int start = 0;
    while (start <= (int)matchedRules.length()) {
        int sep = matchedRules.indexOf('|', start);
        if (sep < 0) sep = matchedRules.length();
        String ruleName = matchedRules.substring(start, sep);
        ruleName.trim();
        if (ruleName.length() > 0) {
            String ruleNameLower = ruleName;
            ruleNameLower.toLowerCase();
            setRecipientPendingNotification(ruleName, ruleNameLower, true);
        }
        if (sep >= (int)matchedRules.length()) break;
        start = sep + 1;
    }
}





// Collect all recipient rules represented in a streaming email body.
// This is used to keep pending/success state tied to the exact rules behind a queued email.
String collectMatchedRulesFromContent(const String &content) {
    if (content.length() == 0) return String();

    String matchedRules;
    auto appendRuleOnce = [&](const String &ruleName) {
        int start = 0;
        while (start <= (int)matchedRules.length()) {
            int sep = matchedRules.indexOf('|', start);
            if (sep < 0) sep = matchedRules.length();
            String existing = matchedRules.substring(start, sep);
            existing.trim();
            if (existing == ruleName) return;
            if (sep >= (int)matchedRules.length()) break;
            start = sep + 1;
        }
        if (matchedRules.length() > 0) matchedRules += "|";
        matchedRules += ruleName;
    };
    
    String contentLower = content;
    contentLower.toLowerCase();
    
    // Word-boundary check helper
    auto isWordChar = [](char c)->bool{ return (c>='a' && c<='z') || (c>='0' && c<='9') || (c=='_'); };
    auto containsWholeToken = [&](const String &textLower, const String &tokenLower)->bool {
        if (tokenLower.length() == 0) return false;
        int startPos = 0;
        int iterations = 0;
        const int MAX_ITERATIONS = 1000;
        while (true) {
            if (++iterations > MAX_ITERATIONS) return false;
            if (iterations % 100 == 0) esp_task_wdt_reset();
            int idx = textLower.indexOf(tokenLower, startPos);
            if (idx < 0) return false;
            bool leftOk = (idx == 0) || !isWordChar(textLower.charAt(idx - 1));
            int end = idx + tokenLower.length();
            bool rightOk = (end >= textLower.length()) || !isWordChar(textLower.charAt(end));
            if (leftOk && rightOk) return true;
            startPos = idx + 1;
        }
    };
    
    // Check recipient slots for matches
    JsonArray numericRules = configManager.getRecipientSlots();
    for (JsonObject r : numericRules) {
        bool enabled = r.containsKey("enabled") ? r["enabled"].as<bool>() : true;
        if (!enabled) continue;

        String ruleName = r.containsKey("name") ? r["name"].as<String>() : String();
        int op = opStrToInt(r["op"] | String(""));
        if (ruleName.length() == 0 || op < 0) continue;

        // I (igual/equal): plain keyword (no ", "): word-boundary match.
        // CSV format "key, threshold": fall through to numeric equality below.
        if (op == 3 && ruleName.indexOf(", ") < 1) {
            String ruleNameLower3 = ruleName; ruleNameLower3.toLowerCase();
            if (containsWholeToken(contentLower, ruleNameLower3)) appendRuleOnce(ruleName);
            continue;
        }

        bool matchOk = false;

        // Detect CSV key-value format: "key, threshold" or "key, min..max"
        int csvSep = ruleName.indexOf(", ");
        if (csvSep >= 1) {
            String csvKey = ruleName.substring(0, csvSep);
            csvKey.trim();
            String thresholdStr = ruleName.substring(csvSep + 2);
            thresholdStr.trim();
            double low = 0.0, high = 0.0;
            bool isInterval = false;
            int dotdot = thresholdStr.indexOf("..");
            if (dotdot > 0 && (op == 6 || op == 7)) {
                low = thresholdStr.substring(0, dotdot).toFloat();
                high = thresholdStr.substring(dotdot + 2).toFloat();
                isInterval = true;
            } else {
                low = thresholdStr.toFloat();
            }
            String csvKeyLower = csvKey;
            csvKeyLower.toLowerCase();
            String contentLow = content;
            contentLow.toLowerCase();
            int keyIdx = contentLow.indexOf(csvKeyLower);
            if (op == 10) continue; // P: presence watchdog fires from loop; email content never triggers it
            if (keyIdx >= 0) {
                int after = keyIdx + csvKeyLower.length();
                while (after < content.length() && content[after] == ' ') after++;
                if (after < content.length() && content[after] == ',') {
                    after++;
                    while (after < content.length() && content[after] == ' ') after++;
                    int vs = after;
                    if (vs < content.length() && content[vs] == '-') after++;
                    int ve = after;
                    bool seenDotV = false;
                    while (ve < content.length() && (isDigit(content[ve]) || (!seenDotV && content[ve] == '.'))) {
                        if (content[ve] == '.') seenDotV = true;
                        ve++;
                    }
                    if (ve > vs) {
                        double val = content.substring(vs, ve).toFloat();
                        if (op == 0) matchOk = (val < low);
                        else if (op == 1) matchOk = (val > low);
                        else if (op == 2) matchOk = (val != low);
                        else if (op == 3) matchOk = (fabs(val - low) < 0.005); // I: numeric equality
                        else if (op == 4) matchOk = (val >= low);
                        else if (op == 5) matchOk = (val <= low);
                        else if (op == 6) matchOk = (isInterval ? (val >= low && val <= high) : (val == low));
                        else if (op == 7) matchOk = (isInterval ? (val < low || val > high) : (val != low));
                    }
                }
                // V (delta), C (count), Cw (count-window), Hy (hysteresis): mark notified on key presence;
                // actual threshold validation was done when the alert was originally queued.
                if (!matchOk && (op == 8 || op == 9 || op == 11 || op == 12)) matchOk = true;
            } else if (csvKeyLower == "any") {
                int lineStart = 0;
                while (!matchOk && lineStart < (int)content.length()) {
                    int lineEnd = content.indexOf('\n', lineStart);
                    if (lineEnd < 0) lineEnd = content.length();
                    String line = content.substring(lineStart, lineEnd);
                    line.trim();
                    int tbPos = line.indexOf('\t');
                    if (tbPos > 0 && tbPos < 25) {
                        if (line.length() >= 10 && line[4] == '-' && line[7] == '-') line = line.substring(tbPos + 1);
                        else if (line.length() >= 8 && line[2] == ':' && line[5] == ':') line = line.substring(tbPos + 1);
                    }
                    String lineLow = line; lineLow.toLowerCase();
                    int pos = 0;
                    while (!matchOk && pos < (int)lineLow.length()) {
                        while (pos < (int)lineLow.length() && (lineLow[pos] == ' ' || lineLow[pos] == '\t' || lineLow[pos] == ',')) pos++;
                        if (pos >= (int)lineLow.length()) break;
                        int tokStart = pos;
                        if (lineLow[pos] == '-') pos++;
                        bool hasDigit = false;
                        while (pos < (int)lineLow.length() && (isDigit(lineLow[pos]) || lineLow[pos] == '.')) {
                            if (isDigit(lineLow[pos])) hasDigit = true;
                            pos++;
                        }
                        if (hasDigit) {
                            if (pos < (int)lineLow.length() && lineLow[pos] == ':') {
                                while (pos < (int)lineLow.length() && lineLow[pos] != ' ' && lineLow[pos] != '\t' && lineLow[pos] != ',') pos++;
                                continue;
                            }
                            double val = lineLow.substring(tokStart, pos).toFloat();
                            if      (op == 6) matchOk = (val >= low && val <= high);
                            else if (op == 7) matchOk = (val < low || val > high);
                            else if (op == 8 || op == 9 || op == 11 || op == 12) matchOk = true;
                        } else {
                            while (pos < (int)lineLow.length() && lineLow[pos] != ' ' && lineLow[pos] != '\t' && lineLow[pos] != ',') pos++;
                        }
                    }
                    lineStart = lineEnd + 1;
                }
            } else if (csvKeyLower.startsWith("col") && csvKeyLower.length() > 3 && isDigit(csvKeyLower.charAt(3))) {
                // colN positional rule: "col2" never appears literally in RS232 data so keyIdx == -1.
                // For state-based ops the match was already validated at trigger time; for comparison ops
                // scan each line in the accumulated content and check the Nth numeric column.
                int targetCol = csvKeyLower.substring(3).toInt();
                if (op == 8 || op == 9 || op == 11 || op == 12) {
                    if (targetCol > 0) matchOk = true;
                } else if (targetCol > 0) {
                    int lineStart = 0;
                    while (!matchOk && lineStart < (int)content.length()) {
                        int lineEnd = content.indexOf('\n', lineStart);
                        if (lineEnd < 0) lineEnd = content.length();
                        String line = content.substring(lineStart, lineEnd);
                        line.trim();
                        // Remove timestamp prefix (YYYY-MM-DD HH:MM:SS\t or HH:MM:SS\t)
                        int tbPos = line.indexOf('\t');
                        if (tbPos > 0 && tbPos < 25) {
                            if (line.length() >= 10 && line[4] == '-' && line[7] == '-')
                                line = line.substring(tbPos + 1);
                            else if (line.length() >= 8 && line[2] == ':' && line[5] == ':')
                                line = line.substring(tbPos + 1);
                        }
                        String lineLow = line; lineLow.toLowerCase();
                        int currentCol = 0, pos = 0;
                        while (pos < (int)lineLow.length() && currentCol < targetCol) {
                            while (pos < (int)lineLow.length() && (lineLow[pos] == ' ' || lineLow[pos] == '\t' || lineLow[pos] == ',')) pos++;
                            if (pos >= (int)lineLow.length()) break;
                            int tokStart = pos;
                            if (lineLow[pos] == '-') pos++;
                            bool hasDigit = false;
                            while (pos < (int)lineLow.length() && (isDigit(lineLow[pos]) || lineLow[pos] == '.')) {
                                if (isDigit(lineLow[pos])) hasDigit = true;
                                pos++;
                            }
                            if (hasDigit) {
                                // Skip time-component tokens (digit run immediately followed by ':')
                                if (pos < (int)lineLow.length() && lineLow[pos] == ':') {
                                    while (pos < (int)lineLow.length() && lineLow[pos] != ' ' && lineLow[pos] != '\t' && lineLow[pos] != ',') pos++;
                                    continue;
                                }
                                currentCol++;
                                if (currentCol == targetCol) {
                                    double val = lineLow.substring(tokStart, pos).toFloat();
                                    if      (op == 0) matchOk = (val <  low);
                                    else if (op == 1) matchOk = (val >  low);
                                    else if (op == 2) matchOk = (val != low);
                                    else if (op == 3) matchOk = (fabs(val - low) < 0.005);
                                    else if (op == 4) matchOk = (val >= low);
                                    else if (op == 5) matchOk = (val <= low);
                                    else if (op == 6) matchOk = (isInterval ? (val >= low && val <= high) : (val == low));
                                    else if (op == 7) matchOk = (isInterval ? (val < low || val > high) : (val != low));
                                }
                            } else {
                                while (pos < (int)lineLow.length() && lineLow[pos] != ' ' && lineLow[pos] != '\t' && lineLow[pos] != ',') pos++;
                            }
                        }
                        lineStart = lineEnd + 1;
                    }
                }
            }
        } else {
            // Bare format: no CSV separator. Handles R/DR (6/7) and single-threshold ops (0-5).
            String ruleNameLow = ruleName; ruleNameLow.toLowerCase();
            if (op == 6 || op == 7) {
                // Range: find ".." separator, extract prefix, min, max and shape from minStr.
                int dotdot2 = -1;
                for (int k = 0; k < (int)ruleNameLow.length() - 1; k++) {
                    if (ruleNameLow.charAt(k) == '.' && ruleNameLow.charAt(k+1) == '.') { dotdot2 = k; break; }
                }
                if (dotdot2 >= 0) {
                    String leftPart2 = ruleNameLow.substring(0, dotdot2); leftPart2.trim();
                    String rightPart2 = ruleNameLow.substring(dotdot2 + 2); rightPart2.trim();
                    int thStart2 = 0;
                    while (thStart2 < (int)leftPart2.length() && !isDigit(leftPart2.charAt(thStart2))) thStart2++;
                    String prefix2 = (thStart2 > 0) ? leftPart2.substring(0, thStart2) : String(""); prefix2.trim();
                    String minStr2  = leftPart2.substring(thStart2); minStr2.trim();
                    double minVal2  = parseFormattedNumber(minStr2);
                    double maxVal2  = parseFormattedNumber(rightPart2);
                    String shape2   = getNumberShape(minStr2);
                    if (shape2.length() > 0) {
                        int searchStart2 = 0;
                        bool prefixOk = true;
                        if (prefix2.length() > 0) {
                            int pIdx2 = contentLower.indexOf(prefix2);
                            if (pIdx2 >= 0) searchStart2 = pIdx2 + prefix2.length();
                            else prefixOk = false; // Prefix not in content.
                        }
                        if (prefixOk) {
                            for (int i = searchStart2;
                                 i <= (int)contentLower.length() - (int)shape2.length(); i++) {
                                String outStr2; double outVal2;
                                if (!tryMatchShape(contentLower, i, shape2, outStr2, outVal2)) continue;
                                bool inRange = (outVal2 >= minVal2 && outVal2 <= maxVal2);
                                if (op == 7) inRange = !inRange;
                                if (inRange) { matchOk = true; break; }
                            }
                        }
                    }
                }
            } else {
                // Single-threshold ops (0,1,2,4,5): shape-strict matching.
                // Prefix = non-digit chars before first digit; threshold defines exact digit format.
                int threshStart = 0;
                while (threshStart < (int)ruleNameLow.length() &&
                       !isDigit(ruleNameLow.charAt(threshStart))) threshStart++;
                String prefix = ruleNameLow.substring(0, threshStart); prefix.trim();
                String thresholdStr = ruleNameLow.substring(threshStart); thresholdStr.trim();
                if (thresholdStr.length() == 0) continue; // Nothing parseable.
                String shape     = getNumberShape(thresholdStr);
                double threshold = parseFormattedNumber(thresholdStr);
                int searchStart  = 0;
                if (prefix.length() > 0) {
                    int pIdx = contentLower.indexOf(prefix);
                    if (pIdx < 0) continue; // Key prefix not found in content.
                    searchStart = pIdx + prefix.length();
                }
                for (int i = searchStart; i <= (int)contentLower.length() - (int)shape.length(); i++) {
                    String outStr; double outVal;
                    if (!tryMatchShape(contentLower, i, shape, outStr, outVal)) continue;
                    if      (op == 0 && outVal <  threshold) matchOk = true;
                    else if (op == 1 && outVal >  threshold) matchOk = true;
                    else if (op == 2 && outVal != threshold) matchOk = true;
                    else if (op == 3 && fabs(outVal - threshold) < 0.005) matchOk = true; // I: numeric equality
                    else if (op == 4 && outVal >= threshold) matchOk = true;
                    else if (op == 5 && outVal <= threshold) matchOk = true;
                    // Cw/Hy: fire on numeric presence (mirrors findRecipientsInLowerText behaviour)
                    else if (op == 11 || op == 12) matchOk = true;
                    if (matchOk) break;
                }
            }
        }
        
        if (matchOk) appendRuleOnce(ruleName);
    }

    return matchedRules;
}

// Periodic watchdog for Presence (P) operator slots.
// Fires an alert email when a keyword has NOT been seen for longer than its configured timeout.
// Called every 5 seconds from loop(). Uses the freq field for repeat-alert throttling.
static void checkPresenceWatchdog() {
    static unsigned long lastPresenceCheck = 0;
    unsigned long now = millis();
    if (now - lastPresenceCheck < 5000) return; // Run at most every 5 seconds
    lastPresenceCheck = now;

    if (!isEmailConfiguredAndWifiConnected()) return;
    if (!configManager.getSdcardEnabled() || !configManager.getSdcardEmailSend()) return;

    JsonArray numericRules = configManager.getRecipientSlots();
    for (JsonObject r : numericRules) {
        bool enabled = r.containsKey("enabled") ? r["enabled"].as<bool>() : true;
        if (!enabled) continue;

        int op = opStrToInt(r["op"] | String(""));
        if (op != 10) continue; // Only P (presence watchdog) slots

        String ruleName = r["name"] | String("");
        String emails = r["emails"] | String("");
        if (ruleName.length() == 0 || emails.length() == 0) continue;

        // Name format: "keyword, timeout_seconds" (e.g. "HEARTBEAT, 60")
        int csvSep = ruleName.indexOf(", ");
        if (csvSep < 1) continue;

        String csvKey = ruleName.substring(0, csvSep);
        csvKey.trim();
        String threshStr = ruleName.substring(csvSep + 2);
        threshStr.trim();
        double timeoutSec = threshStr.toFloat();
        if (timeoutSec < 10.0) timeoutSec = 60.0; // Enforce minimum 10s; default 60s

        unsigned long timeoutMs = (unsigned long)(timeoutSec * 1000.0);
        String ruleNameLower = ruleName;
        ruleNameLower.toLowerCase();
        int sIdx = configManager.findRecipientSlotIndexByName(ruleNameLower);
        if (sIdx <= 0) continue;

        // Read last seen timestamp under mutex
        unsigned long lastSeen = 0;
        if (recipientThrottleMutex != NULL &&
            xSemaphoreTakeRecursive(recipientThrottleMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            auto it = recipientPresenceLastSeen.find(sIdx);
            if (it != recipientPresenceLastSeen.end()) lastSeen = it->second;
            xSemaphoreGiveRecursive(recipientThrottleMutex);
        }

        // Check if keyword has been absent for longer than timeout
        bool silent = (lastSeen == 0) ? (now >= timeoutMs) : ((now - lastSeen) >= timeoutMs);
        if (!silent) continue;

        // Throttle + pending check: uses freq slider as re-alert interval and avoids duplicate queued watchdog alerts
        if (shouldBlockRecipientTrigger(ruleName, ruleNameLower)) continue;

        unsigned long silentSec = (lastSeen == 0) ? (now / 1000) : ((now - lastSeen) / 1000);
        sendVirtualMessage(
            String(F("⏰ WATCHDOG P: '")) + csvKey + String(F("' silent ")) +
            String(silentSec) + String(F("s (timeout: ")) + String((unsigned long)timeoutSec) +
            String(F("s). Firing absence alert!")), WARN);

        // Build and enqueue the absence alert email
        String alertBody = buildEmailInfoTable(
            String(F("'")) + csvKey + String(F("' sem sinal ha ")) + String(silentSec) +
            String(F("s (timeout configurado: ")) + String((unsigned long)timeoutSec) + String(F("s)")),
            "", "", "",
            getFreqLabelForRule(ruleName),
            "", "");
#if ENABLE_PAGE_RECIPIENTS == 1
        enqueueStreamingEmail(alertBody, ruleName);
#else
        // Pack without recipients page: do NOT send rule-based emails (per user request).
        sendVirtualMessage(String(F("📭 Recipient rule '")) + ruleName + String(F("' matched but recipients page disabled — email skipped.")), DEBUG);
#endif
    }
}

// Helper function to check if a recipient should be throttled based on notification frequency
static bool shouldThrottleRecipient(const String &recipientName, const String &recipientNameLower) {
    int slotIdx = configManager.findRecipientSlotIndexByName(recipientNameLower);
    if (slotIdx <= 0) {
        sendVirtualMessage(String(F("📭 Recipient not in slots (no throttle): ")) + recipientName, DEBUG);
        return false; // Not found in slots, no throttling
    }
    
    int freqSec = configManager.getRecipientSlotFrequency(slotIdx);
    if (freqSec < 180) freqSec = 180; // Minimum 3 minutes between alerts
    
    if (recipientThrottleMutex == NULL) return false; // Mutex not initialized
    
    // CRITICAL: 2s timeout (was 100ms) to prevent race condition under high load
    // If mutex acquisition fails, BLOCK by default (assume recent activity)
    if (xSemaphoreTakeRecursive(recipientThrottleMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        // Failed to acquire mutex after 2s - system under extreme load
        // CONSERVATIVE FALLBACK: Check if entry exists in map
        auto it = recipientLastNotificationTime.find(slotIdx);
        if (it == recipientLastNotificationTime.end()) {
            // No timestamp exists - allow first notification
            sendVirtualMessage(String(F("⚠️ Mutex timeout, allowing FIRST notification: ")) + recipientName, WARN);
            return false;
        } else {
            // Timestamp exists - assume recent activity, BLOCK notification
            sendVirtualMessage(String(F("🔒 Mutex timeout, BLOCKING notification (safe): ")) + recipientName, WARN);
            return true;
        }
    }
    
    unsigned long now = millis();
    unsigned long lastTime = recipientLastNotificationTime[slotIdx];
    unsigned long elapsed = now - lastTime;
    unsigned long freqMs = (unsigned long)freqSec * 1000UL;
    bool shouldThrottle = (lastTime > 0 && elapsed < freqMs);
    
    // DETAILED DEBUG LOG (always log for debugging throttling issues)
    String debugMsg = String(F("🔍 Throttle check: ")) + recipientName + 
                     String(F(" | slot=")) + String(slotIdx) +
                     String(F(" | freq=")) + String(freqSec) + String(F("s")) +
                     String(F(" | last=")) + String(lastTime) +
                     String(F(" | now=")) + String(now) +
                     String(F(" | elapsed=")) + String(elapsed/1000.0, 1) + String(F("s")) +
                     String(F(" | decision=")) + (shouldThrottle ? F("BLOCK") : F("ALLOW"));
    sendVirtualMessage(debugMsg, DEBUG);
    
    if (shouldThrottle) {
        xSemaphoreGiveRecursive(recipientThrottleMutex);
        return true; // Throttle this notification
    }
    
    // NOTE: Do NOT update the timestamp here.
    // shouldThrottleRecipient is called at queue-fill time (inside findRecipientsInLowerText),
    // which runs up to 30s BEFORE processRamEmailQueue actually sends the email.
    // Writing recipientLastNotificationTime here would cause isRecipientThrottled to see
    // elapsed=~30s < freqMin=180s at send time and block the email permanently.
    // Timestamp MUST only be written after the email is sent, via markRecipientNotified().
    sendVirtualMessage(String(F("✅ Recipient ALLOWED (queued for send): ")) + recipientName, DEBUG);
    xSemaphoreGiveRecursive(recipientThrottleMutex);
    return false; // Allow notification
}

static bool shouldBlockRecipientTrigger(const String &recipientName, const String &recipientNameLower) {
    if (isRecipientPendingNotification(recipientNameLower)) {
        sendVirtualMessage(String(F("⏳ Recipient pending delivery, blocking new trigger: ")) + recipientName, DEBUG);
        return true;
    }
    return shouldThrottleRecipient(recipientName, recipientNameLower);
}

static bool evaluateCountRuleMatch(int slotIdx, int thresholdCount) {
    if (slotIdx <= 0) return false;
    if (thresholdCount < 1) thresholdCount = 1;
    if (recipientCountTriggered.count(slotIdx) && recipientCountTriggered[slotIdx]) return true;
    recipientMatchCount[slotIdx]++;
    int currentCount = recipientMatchCount[slotIdx];
    // Log COUNT progress at WARN level so user can see it without debug mode
    safeEnviarMensagemVirtual("[COUNT] slot=%d count=%d/%d", WARN, slotIdx, currentCount, thresholdCount);
    if (currentCount >= thresholdCount) {
        recipientCountTriggered[slotIdx] = true;
        return true;
    }
    return false;
}

static bool evaluateCountWindowRuleMatch(int slotIdx, int thresholdCount, unsigned long windowMs) {
    if (slotIdx <= 0) return false;
    if (thresholdCount < 1) thresholdCount = 1;
    if (windowMs < 1) windowMs = 30000UL;
    if (recipientCwTriggered.count(slotIdx) && recipientCwTriggered[slotIdx]) return true;

    unsigned long nowMs = millis();
    auto wIt = recipientCwWindowStart.find(slotIdx);
    if (wIt == recipientCwWindowStart.end() || (nowMs - wIt->second) >= windowMs) {
        recipientCwWindowStart[slotIdx] = nowMs;
        recipientCwCount[slotIdx] = 0;
    }
    recipientCwCount[slotIdx]++;
    if (recipientCwCount[slotIdx] >= thresholdCount) {
        recipientCwTriggered[slotIdx] = true;
        return true;
    }
    return false;
}

static bool evaluateHysteresisRuleMatch(int slotIdx, double value, double highThreshold, double lowThreshold) {
    if (slotIdx <= 0) return false;
    bool fired = recipientHystFired.count(slotIdx) && recipientHystFired[slotIdx];
    if (fired) {
        if (value <= lowThreshold) recipientHystFired[slotIdx] = false;
        return false;
    }
    if (value >= highThreshold) {
        recipientHystFired[slotIdx] = true;
        return true;
    }
    return false;
}

// outCapturedValues: optional map populated with the numeric value found for each matched numeric rule.
// Key = ruleName, Value = numeric string as found in the text (e.g. "018.79").
// Only populated for numeric/interval rules; token-equality rules leave no entry.
static void findRecipientsInLowerText(const String &textLower, std::vector<std::pair<String,String>> &outRecipients, bool sdActiveForRecipients, std::map<String,String>* outCapturedValues = nullptr) {
    outRecipients.clear();
    auto isWordChar = [](char c)->bool{ return (c>='a' && c<='z') || (c>='0' && c<='9') || (c=='_'); };
    auto containsWholeToken = [&](const String &text, const String &token)->bool{
        if (token.length() == 0) return false;
        int startPos = 0;
        int iterations = 0; // Iteration counter
        const int MAX_ITERATIONS = 1000; // Safe limit
        while (true) {
            if (++iterations > MAX_ITERATIONS) return false; // Timeout protection
            if (iterations % 100 == 0) esp_task_wdt_reset(); // Reset watchdog
            int idx = text.indexOf(token, startPos);
            if (idx < 0) return false;
            bool leftOk = (idx == 0) || !isWordChar(text.charAt(idx - 1));
            int end = idx + token.length();
            bool rightOk = (end >= text.length()) || !isWordChar(text.charAt(end));
            if (leftOk && rightOk) return true;
            startPos = idx + 1;
        }
    };

    // In SD streaming mode, the SMTP sender must NOT receive a copy.
    // IMPORTANT: do not drop an entire rule if the sender appears in the list;
    // instead, remove only the sender address and keep the remaining recipients.
    String senderForFilter = configManager.getSmtpUsername();
    senderForFilter.trim();
    senderForFilter.toLowerCase();

    auto filterSenderFromEmailList = [&](const String &emailsRaw) -> String {
        if (!sdActiveForRecipients || senderForFilter.length() == 0) return emailsRaw;

        String out;
        int p = 0;
        while (p < (int)emailsRaw.length()) {
            int q1 = emailsRaw.indexOf(';', p);
            int q2 = emailsRaw.indexOf(',', p);
            int q = -1;
            if (q1 < 0) q = q2;
            else if (q2 < 0) q = q1;
            else q = min(q1, q2);
            if (q < 0) q = emailsRaw.length();

            String token = emailsRaw.substring(p, q);
            token.trim();
            p = q + 1;
            if (token.length() == 0) continue;

            // Extract email part from formats like: "Name <user@host>"
            String emailOnly = token;
            int lt = token.lastIndexOf('<');
            int gt = token.lastIndexOf('>');
            if (lt >= 0 && gt > lt) {
                emailOnly = token.substring(lt + 1, gt);
                emailOnly.trim();
            } else {
                if (emailOnly.startsWith("<") && emailOnly.endsWith(">") && emailOnly.length() > 2) {
                    emailOnly = emailOnly.substring(1, emailOnly.length() - 1);
                    emailOnly.trim();
                }
            }

            String lower = emailOnly;
            lower.toLowerCase();
            if (lower == senderForFilter) continue;

            if (out.length() > 0) out += ", ";
            out += emailOnly;
        }
        out.trim();
        return out;
    };

    // All recipient slots: I (equal token), R/DR (range), and numeric threshold operators.
    // op==3 (I): keyword-only slots are handled in this loop below.
    JsonArray numericRules = configManager.getRecipientSlots();
    for (JsonObject r : numericRules) {
        bool enabled = r.containsKey("enabled") ? r["enabled"].as<bool>() : true;
        if (!enabled) {
            Serial.printf("[RECIP_SKIP_DISABLED] slot name='%s' skipped (enabled=false)\n",
                          r.containsKey("name") ? r["name"].as<String>().c_str() : "?");
            continue;
        }

        String ruleName = r.containsKey("name") ? r["name"].as<String>() : String();
        int op = opStrToInt(r["op"] | String(""));
        String emails = r.containsKey("emails") ? r["emails"].as<String>() : String();
        if (ruleName.length() == 0 || emails.length() == 0 || op < 0) continue;

        // I (igual/equal):
        // - Plain keyword (no ", " separator): word-boundary token match (RS232 text rules).
        // - CSV format "key, threshold": fall through to numeric equality below (sensor rules).
        if (op == 3 && ruleName.indexOf(", ") < 1) {
            String ruleNameLower3 = ruleName; ruleNameLower3.toLowerCase();
            if (containsWholeToken(textLower, ruleNameLower3)) {
                if (shouldBlockRecipientTrigger(ruleName, ruleNameLower3)) continue;
                outRecipients.push_back({ruleName, emails});
            }
            continue;
        }
        // CSV format "key, threshold" with op==3 falls through to numeric matching below.

        // Intervals (R=6, DR=7): shape-strict range matching.
        // Supports plain "min..max", "prefix min..max", and multi-group "ddd.dd dd.ddd..ddd.dd dd.ddd".
        // ".." is safe to search for: a decimal number never contains two consecutive dots.
        if (op == 6 || op == 7) {
            int dotdot = -1;
            for (int k = 0; k < (int)ruleName.length() - 1; k++) {
                if (ruleName.charAt(k) == '.' && ruleName.charAt(k+1) == '.') { dotdot = k; break; }
            }
            if (dotdot < 0) continue; // No ".." separator found.

            String leftPart = ruleName.substring(0, dotdot); leftPart.trim();
            String rightPart = ruleName.substring(dotdot + 2); rightPart.trim();

            bool isCsvFormat = false;
            String csvKey = "";
            String prefix = "";
            String minStr = "";

            int csvSep = leftPart.indexOf(',');
            if (csvSep > 0) {
                csvKey = leftPart.substring(0, csvSep);
                csvKey.trim();
                minStr = leftPart.substring(csvSep + 1);
                minStr.trim();
                isCsvFormat = true;
            } else {
                int threshStart = 0;
                while (threshStart < (int)leftPart.length() && !isDigit(leftPart.charAt(threshStart))) threshStart++;
                prefix = (threshStart > 0) ? leftPart.substring(0, threshStart) : String("");
                prefix.trim();
                minStr = leftPart.substring(threshStart); minStr.trim();
            }

            double minVal = parseFormattedNumber(minStr);
            double maxVal = parseFormattedNumber(rightPart);

            String numStr; double foundNum; bool foundNumOk = false;

            if (isCsvFormat) {
                String csvKeyLower = csvKey; csvKeyLower.toLowerCase();
                int keyIdx = textLower.indexOf(csvKeyLower);
                if (keyIdx >= 0) {
                    int after = keyIdx + csvKeyLower.length();
                    while (after < (int)textLower.length() && textLower.charAt(after) == ' ') after++;
                    if (after < (int)textLower.length() && textLower.charAt(after) == ',') {
                        after++;
                        while (after < (int)textLower.length() && textLower.charAt(after) == ' ') after++;
                        int vs = after;
                        if (after < (int)textLower.length() && textLower.charAt(after) == '-') after++;
                        int ve = after;
                        bool seenDot = false;
                        while (ve < (int)textLower.length() &&
                               (isDigit(textLower.charAt(ve)) || (!seenDot && textLower.charAt(ve) == '.'))) {
                            if (textLower.charAt(ve) == '.') seenDot = true;
                            ve++;
                        }
                        if (ve > vs) {
                            numStr = textLower.substring(vs, ve);
                            foundNum = numStr.toFloat();
                            foundNumOk = true;
                        }
                    }
                } else if (csvKeyLower.startsWith("col") && csvKeyLower.length() > 3 && isDigit(csvKeyLower.charAt(3))) {
                    int targetCol = csvKeyLower.substring(3).toInt();
                    int currentCol = 0;
                    int pos = 0;
                    while (pos < (int)textLower.length() && currentCol < targetCol) {
                        while (pos < (int)textLower.length() && (textLower.charAt(pos) == ' ' || textLower.charAt(pos) == '\t' || textLower.charAt(pos) == ',')) pos++;
                        if (pos >= (int)textLower.length()) break;
                        int startPos = pos;
                        if (textLower.charAt(pos) == '-') pos++;
                        bool hasDigit = false;
                        while (pos < (int)textLower.length() && (isDigit(textLower.charAt(pos)) || textLower.charAt(pos) == '.')) {
                            if (isDigit(textLower.charAt(pos))) hasDigit = true;
                            pos++;
                        }
                        if (hasDigit) {
                            if (pos < (int)textLower.length() && textLower.charAt(pos) == ':') {
                                while (pos < (int)textLower.length() &&
                                       textLower.charAt(pos) != ' ' && textLower.charAt(pos) != '\t' && textLower.charAt(pos) != ',') pos++;
                                continue;
                            }
                            currentCol++;
                            if (currentCol == targetCol) {
                                numStr = textLower.substring(startPos, pos);
                                foundNum = numStr.toFloat();
                                foundNumOk = true;
                                break;
                            }
                        } else {
                            while (pos < (int)textLower.length() && textLower.charAt(pos) != ' ' && textLower.charAt(pos) != '\t' && textLower.charAt(pos) != ',') pos++;
                        }
                    }
                } else if (csvKeyLower == "any") {
                    int pos = 0;
                    while (pos < (int)textLower.length()) {
                        while (pos < (int)textLower.length() && (textLower.charAt(pos) == ' ' || textLower.charAt(pos) == '\t' || textLower.charAt(pos) == ',')) pos++;
                        if (pos >= (int)textLower.length()) break;
                        int startPos = pos;
                        if (textLower.charAt(pos) == '-') pos++;
                        bool hasDigit = false;
                        while (pos < (int)textLower.length() && (isDigit(textLower.charAt(pos)) || textLower.charAt(pos) == '.')) {
                            if (isDigit(textLower.charAt(pos))) hasDigit = true;
                            pos++;
                        }
                        if (hasDigit) {
                            if (pos < (int)textLower.length() && textLower.charAt(pos) == ':') {
                                while (pos < (int)textLower.length() &&
                                       textLower.charAt(pos) != ' ' && textLower.charAt(pos) != '\t' && textLower.charAt(pos) != ',') pos++;
                                continue;
                            }
                            double candidate = textLower.substring(startPos, pos).toFloat();
                            bool candidateInRange = (candidate >= minVal && candidate <= maxVal);
                            bool candidateMatched = (op == 7) ? !candidateInRange : candidateInRange;
                            if (candidateMatched) {
                                numStr = textLower.substring(startPos, pos);
                                foundNum = candidate;
                                foundNumOk = true;
                                break;
                            }
                        } else {
                            while (pos < (int)textLower.length() && textLower.charAt(pos) != ' ' && textLower.charAt(pos) != '\t' && textLower.charAt(pos) != ',') pos++;
                        }
                    }
                }
            } else {
                String shape  = getNumberShape(minStr);
                if (shape.length() == 0) continue; // Unparseable threshold.

                int searchStart = 0;
                if (prefix.length() > 0) {
                    String prefixLower = prefix; prefixLower.toLowerCase();
                    int pIdx = textLower.indexOf(prefixLower);
                    if (pIdx < 0) continue; // Prefix not found in line.
                    searchStart = pIdx + prefixLower.length();
                }

                for (int i = searchStart; i <= (int)textLower.length() - (int)shape.length(); i++) {
                    if (tryMatchShape(textLower, i, shape, numStr, foundNum)) { foundNumOk = true; break; }
                }
            }

            if (foundNumOk) {
                bool matched = (foundNum >= minVal && foundNum <= maxVal);
                if (op == 7) matched = !matched; // DR: fire when outside range.
                if (matched) {
                    // Alert emails are always delivered; sender filter is NOT applied here.
                    String ruleNameLower = ruleName; ruleNameLower.toLowerCase();
                    if (shouldBlockRecipientTrigger(ruleName, ruleNameLower)) continue;
                    if (outCapturedValues) (*outCapturedValues)[ruleName] = numStr;
                    outRecipients.push_back({ruleName, emails});
                }
            }
            continue;
        }

        // Ops other than R/DR: CSV key-value or bare shape-strict matching.
        String ruleNameLower = ruleName; ruleNameLower.toLowerCase();
        bool matched = false;
        String capturedStr;

        int csvSep2 = ruleName.indexOf(", ");
        if (csvSep2 >= 1) {
            // CSV format: "key, threshold" — any number format accepted after the key.
            String csvKey2 = ruleName.substring(0, csvSep2); csvKey2.trim();
            String threshStr2 = ruleName.substring(csvSep2 + 2); threshStr2.trim();
            double thresh2 = threshStr2.toFloat();
            String csvKey2Lower = csvKey2; csvKey2Lower.toLowerCase();
            int keyIdx2 = textLower.indexOf(csvKey2Lower);
            if (op == 10) { /* P: presence watchdog; skip here */ }
            else if (keyIdx2 >= 0) {
                // COUNT (op=9) and PRESENCE (op=8): key found in the line is enough to act.
                // The numeric value after the key is irrelevant for these two operators,
                // so skip value extraction entirely — avoids breaking on space-separated data.
                if (op == 8 || op == 9) {
                    String rn = ruleName; rn.toLowerCase();
                    int sIdx = configManager.findRecipientSlotIndexByName(rn);
                    if (op == 8) matched = true;
                    else         matched = evaluateCountRuleMatch(sIdx, (int)thresh2);
                } else {
                    // Numeric comparison ops: extract the value that follows the key.
                    // Accept both "key, value" (comma-space) and "key value" (space-only) formats.
                    int after2 = keyIdx2 + csvKey2Lower.length();
                    while (after2 < (int)textLower.length() && textLower.charAt(after2) == ' ') after2++;
                    // Optional comma separator — skip it if present
                    if (after2 < (int)textLower.length() && textLower.charAt(after2) == ',') after2++;
                    while (after2 < (int)textLower.length() && textLower.charAt(after2) == ' ') after2++;
                    int vs2 = after2;
                    if (after2 < (int)textLower.length() && textLower.charAt(after2) == '-') after2++;
                    int ve2 = after2;
                    bool seenDotV2 = false;
                    while (ve2 < (int)textLower.length() &&
                           (isDigit(textLower.charAt(ve2)) ||
                            (!seenDotV2 && textLower.charAt(ve2) == '.'))) {
                        if (textLower.charAt(ve2) == '.') seenDotV2 = true;
                        ve2++;
                    }
                    if (ve2 > vs2) {
                        double val2 = textLower.substring(vs2, ve2).toFloat();
                        capturedStr = textLower.substring(vs2, ve2);
                        String rn = ruleName; rn.toLowerCase();
                        int sIdx = configManager.findRecipientSlotIndexByName(rn);
                        if      (op == 0 && val2 <  thresh2) matched = true;
                        else if (op == 1 && val2 >  thresh2) matched = true;
                        else if (op == 2 && val2 != thresh2) matched = true;
                        else if (op == 3 && fabs(val2 - thresh2) < 0.005) matched = true; // I: numeric equality
                        else if (op == 4 && val2 >= thresh2) matched = true;
                        else if (op == 5 && val2 <= thresh2) matched = true;
                        else if (op == 11) {
                            int slashPos = threshStr2.indexOf('/');
                            int cw_n = (slashPos > 0) ? threshStr2.substring(0, slashPos).toInt() : max(1, (int)thresh2);
                            unsigned long cw_w = (unsigned long)((slashPos > 0) ? threshStr2.substring(slashPos + 1).toFloat() * 1000.0 : 30000UL);
                            matched = evaluateCountWindowRuleMatch(sIdx, cw_n, cw_w);
                        } else if (op == 12) {
                            double highThreshold = 0.0, lowThreshold = 0.0;
                            if (parseHysteresisThresholds(threshStr2, highThreshold, lowThreshold)) {
                                matched = evaluateHysteresisRuleMatch(sIdx, val2, highThreshold, lowThreshold);
                            }
                        }
                    }
                }
            } else if (csvKey2Lower.startsWith("col") && isDigit(csvKey2Lower.charAt(3))) {
                // Feature: "colN, threshold" extracts the N-th number (1-based) from an space/tab/comma separated line
                int targetCol = csvKey2Lower.substring(3).toInt();
                int currentCol = 0;
                int pos = 0;
                while (pos < (int)textLower.length() && currentCol < targetCol) {
                    while (pos < (int)textLower.length() && (textLower.charAt(pos) == ' ' || textLower.charAt(pos) == '\t' || textLower.charAt(pos) == ',')) pos++;
                    if (pos >= (int)textLower.length()) break;
                    
                    int startPos = pos;
                    if (textLower.charAt(pos) == '-') pos++;
                    bool hasDigit = false;
                    while (pos < (int)textLower.length() && (isDigit(textLower.charAt(pos)) || textLower.charAt(pos) == '.')) {
                        if (isDigit(textLower.charAt(pos))) hasDigit = true;
                        pos++;
                    }
                    if (hasDigit) {
                        // Skip tokens that are part of a time field (e.g. "17" or "57" in "17:57:57").
                        // A numeric token immediately followed by ':' is a time component — not a data column.
                        if (pos < (int)textLower.length() && textLower.charAt(pos) == ':') {
                            while (pos < (int)textLower.length() &&
                                   textLower.charAt(pos) != ' ' && textLower.charAt(pos) != '\t' && textLower.charAt(pos) != ',')
                                pos++;
                            continue; // Do not count as a data column
                        }
                        currentCol++;
                        if (currentCol == targetCol) {
                            capturedStr = textLower.substring(startPos, pos);
                            double val2 = capturedStr.toFloat();
                            String rn = ruleName; rn.toLowerCase();
                            int sIdx = configManager.findRecipientSlotIndexByName(rn);
                            if      (op == 0 && val2 <  thresh2) matched = true;
                            else if (op == 1 && val2 >  thresh2) matched = true;
                            else if (op == 2 && val2 != thresh2) matched = true;
                            else if (op == 3 && fabs(val2 - thresh2) < 0.005) matched = true; // I: numeric equality
                            else if (op == 4 && val2 >= thresh2) matched = true;
                            else if (op == 5 && val2 <= thresh2) matched = true;
                            else if (op == 8)                     matched = true;
                            else if (op == 9)                     matched = evaluateCountRuleMatch(sIdx, (int)thresh2);
                            else if (op == 11) {
                                int slashPos = threshStr2.indexOf('/');
                                int cw_n = (slashPos > 0) ? threshStr2.substring(0, slashPos).toInt() : max(1, (int)thresh2);
                                unsigned long cw_w = (unsigned long)((slashPos > 0) ? threshStr2.substring(slashPos + 1).toFloat() * 1000.0 : 30000UL);
                                matched = evaluateCountWindowRuleMatch(sIdx, cw_n, cw_w);
                            } else if (op == 12) {
                                double highThreshold = 0.0, lowThreshold = 0.0;
                                if (parseHysteresisThresholds(threshStr2, highThreshold, lowThreshold)) {
                                    matched = evaluateHysteresisRuleMatch(sIdx, val2, highThreshold, lowThreshold);
                                }
                            }
                            // Diagnostic log: show what colN found and whether it matched (rate-limited)
                            static unsigned long _lastColNLog = 0;
                            if (millis() - _lastColNLog > 5000) {
                                safeEnviarMensagemVirtual("[colN] rule='%s' col%d=%.3f thresh=%.3f op=%d → %s",
                                    WARN, ruleName.c_str(), targetCol, val2, thresh2, op, matched ? "MATCH" : "no match");
                                _lastColNLog = millis();
                            }
                            break;
                        }
                    } else {
                        while (pos < (int)textLower.length() && textLower.charAt(pos) != ' ' && textLower.charAt(pos) != '\t' && textLower.charAt(pos) != ',') pos++;
                    }
                }
                // Log when the target column was not found in the data (e.g. not enough columns)
                if (currentCol < targetCol) {
                    static unsigned long _lastColNMissLog = 0;
                    if (millis() - _lastColNMissLog > 10000) {
                        safeEnviarMensagemVirtual("[colN] rule='%s' col%d NOT FOUND in: %.35s",
                            WARN, ruleName.c_str(), targetCol, textLower.c_str());
                        _lastColNMissLog = millis();
                    }
                }
            } else if (csvKey2Lower == "any") {
                // Feature: "any, threshold" checks all numbers in the line regardless of shape
                int pos = 0;
                while (pos < (int)textLower.length() && !matched) {
                    while (pos < (int)textLower.length() && (textLower.charAt(pos) == ' ' || textLower.charAt(pos) == '\t' || textLower.charAt(pos) == ',')) pos++;
                    if (pos >= (int)textLower.length()) break;
                    
                    int startPos = pos;
                    if (textLower.charAt(pos) == '-') pos++;
                    bool hasDigit = false;
                    while (pos < (int)textLower.length() && (isDigit(textLower.charAt(pos)) || textLower.charAt(pos) == '.')) {
                        if (isDigit(textLower.charAt(pos))) hasDigit = true;
                        pos++;
                    }
                    if (hasDigit) {
                        // Skip tokens that are part of a time field (e.g. "17" or "57" in "17:57:57").
                        // A numeric token immediately followed by ':' is a time component — not a data value.
                        if (pos < (int)textLower.length() && textLower.charAt(pos) == ':') {
                            while (pos < (int)textLower.length() &&
                                   textLower.charAt(pos) != ' ' && textLower.charAt(pos) != '\t' && textLower.charAt(pos) != ',')
                                pos++;
                            continue; // Do not evaluate as a data number
                        }
                        String numStr = textLower.substring(startPos, pos);
                        double val2 = numStr.toFloat();
                        bool pass = false;
                        String rn = ruleName; rn.toLowerCase();
                        int sIdx = configManager.findRecipientSlotIndexByName(rn);
                        if      (op == 0 && val2 <  thresh2) pass = true;
                        else if (op == 1 && val2 >  thresh2) pass = true;
                        else if (op == 2 && val2 != thresh2) pass = true;
                        else if (op == 3 && fabs(val2 - thresh2) < 0.005) pass = true;
                        else if (op == 4 && val2 >= thresh2) pass = true;
                        else if (op == 5 && val2 <= thresh2) pass = true;
                        else if (op == 8)                     pass = true;
                        else if (op == 9)                     pass = evaluateCountRuleMatch(sIdx, (int)thresh2);
                        else if (op == 11) {
                            int slashPos = threshStr2.indexOf('/');
                            int cw_n = (slashPos > 0) ? threshStr2.substring(0, slashPos).toInt() : max(1, (int)thresh2);
                            unsigned long cw_w = (unsigned long)((slashPos > 0) ? threshStr2.substring(slashPos + 1).toFloat() * 1000.0 : 30000UL);
                            pass = evaluateCountWindowRuleMatch(sIdx, cw_n, cw_w);
                        } else if (op == 12) {
                            double highThreshold = 0.0, lowThreshold = 0.0;
                            if (parseHysteresisThresholds(threshStr2, highThreshold, lowThreshold)) {
                                pass = evaluateHysteresisRuleMatch(sIdx, val2, highThreshold, lowThreshold);
                            }
                        }

                        if (pass) {
                            matched = true;
                            capturedStr = numStr;
                        }
                    } else {
                        while (pos < (int)textLower.length() && textLower.charAt(pos) != ' ' && textLower.charAt(pos) != '\t' && textLower.charAt(pos) != ',') pos++;
                    }
                }
            }
        } else {
            // Bare format: shape-strict matching.
            // Extract optional letter prefix and numeric threshold from rule name.
            int threshStart2 = 0;
            while (threshStart2 < (int)ruleNameLower.length() &&
                   !isDigit(ruleNameLower.charAt(threshStart2))) threshStart2++;
            String prefix2 = ruleNameLower.substring(0, threshStart2); prefix2.trim();
            String thresholdStr2 = ruleNameLower.substring(threshStart2); thresholdStr2.trim();
            if (thresholdStr2.length() > 0) {
                String shape2     = getNumberShape(thresholdStr2);
                double threshold2 = parseFormattedNumber(thresholdStr2);
                int searchStart2  = 0;
                if (prefix2.length() > 0) {
                    int pIdx2 = textLower.indexOf(prefix2);
                    if (pIdx2 >= 0) searchStart2 = pIdx2 + prefix2.length();
                    else thresholdStr2 = ""; // Prefix not found — skip.
                }
                if (thresholdStr2.length() > 0) {
                    for (int i2 = searchStart2;
                         i2 <= (int)textLower.length() - (int)shape2.length(); i2++) {
                        String outStr2; double outVal2;
                        if (!tryMatchShape(textLower, i2, shape2, outStr2, outVal2)) continue;
                        if      (op == 0 && outVal2 <  threshold2) matched = true;
                        else if (op == 1 && outVal2 >  threshold2) matched = true;
                        else if (op == 2 && outVal2 != threshold2) matched = true;
                        else if (op == 3 && fabs(outVal2 - threshold2) < 0.005) matched = true; // I: numeric equality
                        else if (op == 4 && outVal2 >= threshold2) matched = true;
                        else if (op == 5 && outVal2 <= threshold2) matched = true;

                        if (matched) { capturedStr = outStr2; break; }
                    }
                }
            }
        }

        if (matched) {
            // Alert emails are always delivered; sender filter is NOT applied here.
            if (shouldBlockRecipientTrigger(ruleName, ruleNameLower)) continue;
            if (outCapturedValues && capturedStr.length() > 0)
                (*outCapturedValues)[ruleName] = capturedStr;
            outRecipients.push_back({ruleName, emails});
        }
    }

    // Remove exact duplicate rule entries (same rule name appearing twice).
    // NOTE: dedup is intentionally by rule name, NOT by email address.
    // Email deduplication must be done by each caller after this function returns,
    // because deduping by email here silently drops rules like colN/any that share
    // the same recipient address as a named-key rule on the same line.
    std::vector<std::pair<String,String>> unique;
    for (auto &p : outRecipients) {
        bool seen = false;
        for (auto &q : unique) if (q.first == p.first) { seen = true; break; }
        if (!seen) unique.push_back(p);
    }
    outRecipients.swap(unique);
}