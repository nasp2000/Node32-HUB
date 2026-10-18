#pragma once

#include <Arduino.h>

#include "grbl_sender.h"
#include "usb_host_manager.h"

struct GrblWebActionResult {
    bool success;
    String message;
};

const char* grblWebStateToString(GrblStreamState state);
String grblWebBuildStatusJson(const String& sessionId, uint32_t lastConsoleEventId = 0);
String grblWebBuildEventsSse(const String& sessionId, uint32_t lastEventId);
String grblWebBuildActionJson(const GrblWebActionResult& result);
String grblWebBuildControlTokenJson(const String& sessionId);

void grblWebPublishConsoleLine(const String& line);
void grblWebPublishProductionEvent(const char* type, const String& message);
void grblDebugLog(const char* fmt, ...);
String grblDebugDump();

uint8_t grblWebNormalizeSlot(int slot);

GrblWebActionResult grblWebSendCommand(uint8_t usbSlot, const String& command, const String& sessionId);
GrblWebActionResult grblWebSetOverride(uint8_t usbSlot, int feedPct, int spinPct, const String& sessionId);
GrblWebActionResult grblWebStartStream(uint8_t usbSlot, const String& filePath, const String& sessionId);
GrblWebActionResult grblWebStartPsramStream(uint8_t usbSlot, const String& sessionId);
String grblWebBuildPsramFileInfoJson();
GrblWebActionResult grblWebStartQueue(uint8_t usbSlot, const String& queueText, const String& sessionId,
                                      const String& queueErrorMode, uint8_t retryMax);
GrblWebActionResult grblWebStopQueue(const String& sessionId);
GrblWebActionResult grblWebPauseStream(const String& sessionId);
GrblWebActionResult grblWebResumeStream(const String& sessionId);
GrblWebActionResult grblWebCancelStream(const String& sessionId);
GrblWebActionResult grblWebSetPollInterval(uint32_t ms, const String& sessionId);
GrblWebActionResult grblWebAutoDetectBaud(uint8_t slot, const String& sessionId);
GrblWebActionResult grblWebAuthorizeWrite(const String& sessionId, const String& token);
