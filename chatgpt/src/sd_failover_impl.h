// sd_failover_impl.h -- SD card automatic failover and recovery API.
// Included only from sdcard.cpp (single TU, all static variables shared).
// Do not include this file from any other translation unit.

// SD Failover API - Automatic fallback when SD becomes unavailable
bool sdcardInFallbackMode() { 
    return _sdFallbackMode; 
}

void sdcardRegisterWriteFailure() {
    _sdConsecutiveFailures++;
    if (_sdConsecutiveFailures >= SD_FAILOVER_THRESHOLD && !_sdFallbackMode) {
        _sdFallbackMode = true;
        Serial.println(F("⚠️ SD FAILOVER: Too many consecutive failures, switching to no-SD mode"));
        sendVirtualMessage(F("⚠️ SD card failures detected - switched to email-only mode"), WARN);
    }
}

void sdcardRegisterWriteSuccess() {
    _sdConsecutiveFailures = 0;  // Reset failure counter on successful write
    if (_sdFallbackMode) {
        _sdFallbackMode = false;
        Serial.println(F("✅ SD RECOVERED: SD writes working again, exiting fallback mode"));
        sendVirtualMessage(F("✅ SD card recovered - resumed normal operation"), INFO);
    }
}

void sdcardCheckRecovery() {
    // Only attempt recovery if in fallback mode
    if (!_sdFallbackMode) return;
    
    // Throttle retry attempts (once per minute)
    unsigned long now = millis();
    if (now - _sdLastRetryAttempt < SD_RETRY_INTERVAL_MS) return;
    _sdLastRetryAttempt = now;
    
    // Attempt remount if not currently mounted
    if (!_sdMounted) {
        Serial.println(F("🔄 SD FAILOVER: Attempting recovery (remount)..."));
        if (sdcardBegin()) {
            Serial.println(F("✅ SD FAILOVER: Remount successful, testing write..."));
            // Success will be confirmed when next write succeeds
        } else {
            Serial.println(F("⚠️ SD FAILOVER: Remount failed, will retry in 60s"));
        }
    } else {
        // SD is mounted but still in fallback - might recover on next successful write
        Serial.println(F("🔄 SD FAILOVER: SD mounted, waiting for successful write to exit fallback"));
    }
}
