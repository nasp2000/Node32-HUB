#pragma once

#include <Arduino.h>

enum WiFiHybridState {
    HYBRID_BOOT_AP_STA,        // Boot: AP+STA, up to 60s trying to connect
    HYBRID_STA_ONLY,           // STA connected, AP off
    HYBRID_RECOVERY_AP_STA,    // STA lost, AP re-enabled, retrying STA every 5min
    HYBRID_AP_ONLY,            // No STA available, pure AP; retries every 5min
    HYBRID_AP_RETRY_STA        // Brief STA attempt from AP_ONLY: AP off, 10s to connect
};

extern WiFiHybridState currentHybridState;
extern unsigned long hybridBootTime;
extern unsigned long staConnectedTime;
extern unsigned long staDisconnectedStartTime;
extern bool staWasConnected;
extern unsigned long hybridBootTimeout;
extern unsigned long staStableTime;
extern unsigned long staRecoveryTimeout;
extern int staReconnectAttempts;
extern unsigned long lastStaRetryTime;

int getCurrentHybridStateValue();
void startHybridWiFiConnection();
void manageHybridWiFiMode();