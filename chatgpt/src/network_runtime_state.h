#pragma once

#include <Arduino.h>

extern String ssid;
extern String password;
extern bool apActive;
extern bool forceAPMode;
extern bool apLockedUntilReboot;
extern bool manualNetworkSwitch;
extern String currentConnectedSSID;
extern unsigned long lastAPAttemptTime;
extern int apFailureCount;