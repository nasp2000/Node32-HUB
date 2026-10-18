#pragma once
#include <Arduino.h>

String buildMessage(const char* format, ...);

void safeLog(const char* fmt, int level, ...);
