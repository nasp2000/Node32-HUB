// =============================================================
// 📧 sd_email_streaming.h — Email via SD streaming (RS232 only, heap-friendly)
// =============================================================
// 🧠 When SDCard ON + email_from_sd=true: RS232 records to SD and sends email via streaming
// ✅ LPT keeps the traditional method (RAM → Email)
// ✅ SDCard OFF: both use the traditional method

#ifndef SD_EMAIL_STREAMING_H
#define SD_EMAIL_STREAMING_H

#pragma once

#include <Arduino.h>
#include <vector>
#include "ConfigManager.h"

// 📦 Structure defined in chatgpt.ino (extern here)
extern std::vector<PendingEmailJob> emailJobQueue;

// 📧 Callback invoked by sdcard.cpp when RS232 data is written to SD
void onSdcardDataWritten(const String &filepath, size_t startOffset, size_t endOffset, const String &source);

// 📧 Sends email with part of the SD file (Base64 streaming)
bool sendEmailPartialFileSd(const String &filePath, size_t startOffset, size_t endOffset,
                             const String &recipients, const String &subject, const String &bodyText);

// 📊 Processes pending SD email jobs (called periodically in the loop)
void processSdEmailQueue();

#endif // SD_EMAIL_STREAMING_H
