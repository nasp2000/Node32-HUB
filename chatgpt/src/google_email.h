// =============================================================
// 📧 google_email.h — Google Apps Script Email System
// =============================================================
// Lightweight email system using Google Apps Script as intermediary.
// Reduces ESP32 heap usage from 60-80KB (TLS+SMTP) to 8-15KB (HTTP only).
// 
// Features:
// - Chunked upload (50KB chunks) for large files
// - Session-based upload (start → chunks → finish)
// - Automatic retry logic
// - Fallback to RAM email if Google Script unavailable
// - Thread-safe with mutex protection

#ifndef GOOGLE_EMAIL_H
#define GOOGLE_EMAIL_H

#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <SD.h>

#include "config_manager_runtime.h"

// =============================================================
// 📊 CONSTANTS
// =============================================================

#define GOOGLE_EMAIL_CHUNK_SIZE (50 * 1024)  // 50KB chunks (heap-friendly)
#define GOOGLE_EMAIL_MAX_RETRIES 3           // Retry failed operations
#define GOOGLE_EMAIL_TIMEOUT_MS 30000        // 30s HTTP timeout
#define GOOGLE_EMAIL_SESSION_MAX_AGE 1800000 // 30min session expiry

// =============================================================
// 📦 STRUCTURES
// =============================================================

struct GoogleEmailSession {
    String sessionId;
    String filename;
    unsigned long createdAt;
    int chunksUploaded;
    bool active;
};

// =============================================================
// 🔐 MUTEX & STATE
// =============================================================

extern SemaphoreHandle_t googleEmailMutex;
extern GoogleEmailSession activeSession;

bool googleEmailLock(TickType_t timeoutTicks);
void googleEmailUnlock();

// =============================================================
// 🚀 CORE API
// =============================================================

// Initialize Google Email system (call in setup)
bool googleEmailInit();

// Check if Google Email is configured and available
bool googleEmailIsAvailable();

// Send simple email without attachments (uses JSON body)
bool googleEmailSendSimple(const String &to, const String &subject, const String &body);

// Send email with single file attachment (chunked upload from SD)
bool googleEmailSendWithFile(const String &to, const String &subject, const String &body,
                              const String &filepath, const String &attachmentName);

// Send email with partial file attachment (chunked upload with byte range)
bool googleEmailSendWithFilePartial(const String &to, const String &subject, const String &body,
                                     const String &filepath, size_t startOffset, size_t endOffset,
                                     const String &attachmentName);

// Send EMAILDAY (folder with multiple files, batched into multiple emails)
// shiftEmails: extra recipients for SHIFTDAY events (appended to emaildayExtraEmails)
bool googleEmailSendEmailDay(const String &folderPath, const String &subject, 
                              const String &bodyText, size_t maxBytesPerEmail,
                              const String &shiftEmails = "");

// =============================================================
// 🔧 SESSION MANAGEMENT (Internal)
// =============================================================

// Start upload session (returns sessionId or empty on failure)
String googleEmailStartSession(const String &filename, const String &to, 
                                const String &subject, const String &body);

// Upload single chunk (returns true on success)
bool googleEmailUploadChunk(const String &sessionId, int chunkNum, 
                             const uint8_t* data, size_t dataLen);

// Finish session and trigger email send
bool googleEmailFinishSession(const String &sessionId);

// Cancel session (cleanup on error)
void googleEmailCancelSession(const String &sessionId);

// =============================================================
// 🛠️ HELPERS
// =============================================================

// Get Google Script URL from config
String googleEmailGetScriptUrl();

// Perform HTTP POST with retry logic
bool googleEmailHttpPost(const String &url, const String &payload, String &response, int maxRetries = GOOGLE_EMAIL_MAX_RETRIES);

// Base64 encode buffer (heap-efficient, processes in chunks)
String googleEmailBase64Encode(const uint8_t* data, size_t length);

// Build recipient list (handles multiple emails separated by comma/semicolon)
String googleEmailBuildRecipients(const String &primary, const String &extra);

#endif // GOOGLE_EMAIL_H
