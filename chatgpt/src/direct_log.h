#pragma once

#include <Arduino.h>

void directLogPrint(const __FlashStringHelper *msg);
void directLogPrint(const char *msg);
void directLogPrint(const String &msg);
void directLogPrintln(const __FlashStringHelper *msg);
void directLogPrintln(const char *msg);
void directLogPrintln(const String &msg);