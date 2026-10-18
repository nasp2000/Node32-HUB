#pragma once

#include <Arduino.h>

void triggerScreenConfigSaved();
void triggerScreenCommandSent(const String& command = "", const String& type = "RS232");
void triggerScreenNVSWarning(int usagePercent);