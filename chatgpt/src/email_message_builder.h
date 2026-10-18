#pragma once

#include <Arduino.h>

String buildEmailTitlePrefix();
String buildEmailInfoTable(const String &line1,
                           const String &ruleCol1,
                           const String &ruleCol2,
                           const String &ruleCol3,
                           const String &freqLabel,
                           const String &sourceLine = "",
                           const String &highlightVal = "",
                           const String &eventTimestamp = String());
String buildEmailSignature();