#ifndef SMTP_HELPERS_H
#define SMTP_HELPERS_H

#include <Arduino.h>
#include <FS.h>
#include <WiFiClientSecure.h>

#include "feature_flags.h"

String makeMimeBoundary();
void appendBase64Wrapped(String &out, const String &in);
bool isAscii7(const String &s);
void appendWithCRLF(String &out, const String &in);
void buildMultipartMixedPlainText(const String &bodyText,
                                  const String &attachFileName,
                                  const String &attachContent,
                                  String &outCT,
                                  String &outBody);

void smtpInitStaticBase64Buffers();

// Attachment descriptor: name is the MIME basename; path, when non-empty,
// overrides the folderPath + "/" + name resolution (Flash fallback files).
struct SmtpStreamFile {
    String name;
    size_t size;
    String path;
};
bool smtpRawSend(Client* client, const String &data);
bool smtpRawSendC(Client* client, const char *data);
bool smtpRawSendF(Client* client, const __FlashStringHelper *data);
bool smtpRawWrite(Client* client, const uint8_t *buf, size_t len);
void smtpDrainAvailable(Client* client, unsigned long maxMs = 250);
bool smtpReadLine(Client* client, char *out, size_t outLen, unsigned long timeoutMs);
int smtpParseReplyCode(const char *line);
int smtpWaitForCode(Client* client, int expectedCode, bool multiline, unsigned long timeoutMs, char *lastLineOut, size_t lastLineLen);
int smtpWaitFor2xx(Client* client, unsigned long timeoutMs, char *lastLineOut, size_t lastLineLen);
bool smtpStreamPsramBase64Internal(Client* client, const String& folderPath, const String& filename, size_t startOffset, size_t bytesToRead);
bool smtpStreamFileBase64Internal(Client* client, File &file, size_t startOffset, size_t bytesToRead, bool useStaticBuffers);
bool smtpStreamFileAsBase64(Client* client, File &file, size_t fileSize);
bool smtpStreamPsramFileAsBase64(Client* client, const String& folderPath, const String& fileName, size_t fileSize);
bool smtpStreamStringAsBase64(Client* client, const String &content);
bool smtpStreamFileRangeAsBase64(Client* client, File &file, size_t startOffset, size_t endOffset);
// Flash (LittleFS) variants: path-based instead of File so the actual reads
// go through emailFlashRead() — a PSRAM-stacked email task must never hold a
// LittleFS File (IDF cache-disable assert, HISTORIC/HomeE32 (8)). Same MIME/
// base64 framing as the File variants; SD keeps the File API untouched.
bool smtpStreamFlashAsBase64(Client* client, const String& flashPath, size_t fileSize);
bool smtpStreamFlashRangeAsBase64(Client* client, const String& flashPath, size_t startOffset, size_t endOffset);

#endif // SMTP_HELPERS_H
