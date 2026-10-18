#include "feature_flags.h"

#if ENABLE_SMTP==1

#include "smtp_helpers.h"

#include <ctype.h>
#include <cstring>
#include <time.h>
#include <stdarg.h>

#include <esp_task_wdt.h>

#include "arduino_compat.h"
#include "commands.h"
#include "email_flash_rpc.h" // stack-safe LittleFS reads (PSRAM-stacked email tasks)
#include "priority_manager.h"
#include "psram_vfs.h"

#if (defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)) && defined(__has_include)
  #if __has_include(<esp_heap_caps.h>) && __has_include(<esp_spiram.h>)
    #include <esp_heap_caps.h>
    #include <esp_spiram.h>
    #define SMTP_HELPERS_HAVE_PSRAM_API 1
  #else
    #define SMTP_HELPERS_HAVE_PSRAM_API 0
  #endif
#else
  #define SMTP_HELPERS_HAVE_PSRAM_API 0
#endif

String makeMimeBoundary() {
    char boundary[48];
    unsigned long ms = millis();
    time_t now = time(nullptr);
    snprintf(boundary, sizeof(boundary), "----ESP32-MIME-%lu-%ld", ms, (long)now);
    return String(boundary);
}

void appendBase64Wrapped(String &out, const String &in) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = in.length();
    int lineLen = 0;
    for (size_t i = 0; i < len; ) {
        if (i % 96 == 0) yield();
        uint8_t ib[3] = {0, 0, 0};
        int readCount = 0;
        for (; readCount < 3 && i < len; ++readCount, ++i) {
            ib[readCount] = (uint8_t)in[i];
        }
        uint8_t o1 = (ib[0] >> 2) & 0x3F;
        uint8_t o2 = ((ib[0] & 0x03) << 4) | ((ib[1] >> 4) & 0x0F);
        uint8_t o3 = ((ib[1] & 0x0F) << 2) | ((ib[2] >> 6) & 0x03);
        uint8_t o4 = ib[2] & 0x3F;
        char c1 = b64[o1];
        char c2 = b64[o2];
        char c3 = (readCount > 1) ? b64[o3] : '=';
        char c4 = (readCount > 2) ? b64[o4] : '=';
        if (lineLen >= 76) { out += String(F("\r\n")); lineLen = 0; }
        out += c1; if (++lineLen >= 76) { out += String(F("\r\n")); lineLen = 0; }
        out += c2; if (++lineLen >= 76) { out += String(F("\r\n")); lineLen = 0; }
        out += c3; if (++lineLen >= 76) { out += String(F("\r\n")); lineLen = 0; }
        out += c4; if (++lineLen >= 76) { out += String(F("\r\n")); lineLen = 0; }
    }
    if (lineLen != 0) {
        out += String(F("\r\n"));
    }
}

bool isAscii7(const String &s) {
    for (size_t i = 0; i < s.length(); ++i) {
        uint8_t c = (uint8_t)s[i];
        if (c == '\r' || c == '\n' || c == '\t') continue;
        if (c < 0x20) return false;
        if (c > 0x7E) return false;
    }
    return true;
}

void appendWithCRLF(String &out, const String &in) {
    for (size_t i = 0; i < in.length(); ) {
        char c = in.charAt(i);
        if (c == '\r') {
            if (i + 1 < in.length() && in.charAt(i + 1) == '\n') {
                out += "\r\n";
                i += 2;
            } else {
                out += "\r\n";
                i += 1;
            }
        } else if (c == '\n') {
            out += "\r\n";
            i += 1;
        } else {
            out += c;
            i += 1;
        }
    }
    if (!out.endsWith(String(F("\r\n")))) {
        out += String(F("\r\n"));
    }
}

void buildMultipartMixedPlainText(const String &bodyText,
                                  const String &attachFileName,
                                  const String &attachContent,
                                  String &outCT,
                                  String &outBody) {
    outBody.reserve(1024);

    String boundary = makeMimeBoundary();
    outCT = F("multipart/mixed; boundary=\"");
    outCT += boundary;
    outCT += '"';

    size_t approx = bodyText.length() + ((attachContent.length() * 4) / 3) + 512;
    if (approx > 1024) outBody.reserve(approx);

    outBody = F("This is a multi-part message in MIME format.\r\n");

    outBody += F("--"); outBody += boundary; outBody += F("\r\n");
    outBody += F("Content-Type: text/plain; charset=UTF-8\r\n");
    if (isAscii7(bodyText)) {
        outBody += F("Content-Transfer-Encoding: 7bit\r\n\r\n");
        appendWithCRLF(outBody, bodyText);
        sendVirtualMessage(F("EMAIL: body encoded as 7bit ASCII."), DEBUG, false, false);
    } else {
        outBody += F("Content-Transfer-Encoding: base64\r\n\r\n");
        appendBase64Wrapped(outBody, bodyText);
        sendVirtualMessage(F("EMAIL: body encoded as Base64 due to 8-bit content."), DEBUG, false, false);
    }

    outBody += F("--"); outBody += boundary; outBody += F("\r\n");
    outBody += F("Content-Type: text/plain; name=\"");
    outBody += attachFileName; outBody += F("\"\r\n");
    outBody += F("Content-Disposition: attachment; filename=\"");
    outBody += attachFileName; outBody += F("\"\r\n");
    if (isAscii7(attachContent)) {
        outBody += F("Content-Transfer-Encoding: 7bit\r\n\r\n");
        appendWithCRLF(outBody, attachContent);
        sendVirtualMessage(F("EMAIL: attachment encoded as 7bit ASCII."), DEBUG, false, false);
    } else {
        outBody += F("Content-Transfer-Encoding: base64\r\n\r\n");
        appendBase64Wrapped(outBody, attachContent);
        sendVirtualMessage(F("EMAIL: attachment encoded as Base64 due to 8-bit content."), DEBUG, false, false);
    }

    outBody += F("--"); outBody += boundary; outBody += F("--\r\n");
}

#if ENABLE_SMTP==1
namespace {

static String smtpBuildMessage(const char* format, ...) {
    char msgBuffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(msgBuffer, sizeof(msgBuffer), format, args);
    va_end(args);
    return String(msgBuffer);
}

#if SMTP_HELPERS_HAVE_PSRAM_API
static uint8_t* base64StaticReadBuffer = nullptr;
static char* base64StaticOutputBuffer = nullptr;
static const size_t BASE64_STATIC_READ_SIZE = 12288;
static const size_t BASE64_STATIC_OUTPUT_SIZE = 17408;
#endif

} // namespace

void smtpInitStaticBase64Buffers() {
#if SMTP_HELPERS_HAVE_PSRAM_API
    if (!psramFound()) {
        sendVirtualMessage(F("PSRAM not available - Base64 will use heap malloc"), WARN, false, false);
        return;
    }

    if (base64StaticReadBuffer != nullptr && base64StaticOutputBuffer != nullptr) {
        return;
    }

    base64StaticReadBuffer = (uint8_t*) heap_caps_malloc(BASE64_STATIC_READ_SIZE, MALLOC_CAP_SPIRAM);
    base64StaticOutputBuffer = (char*) heap_caps_malloc(BASE64_STATIC_OUTPUT_SIZE, MALLOC_CAP_SPIRAM);
    if (base64StaticReadBuffer != nullptr && base64StaticOutputBuffer != nullptr) {
        sendVirtualMessage(F("Base64 static buffers allocated in PSRAM"), INFO, false, false);
        return;
    }

    sendVirtualMessage(F("Failed to allocate Base64 PSRAM buffers (dynamic fallback)"), WARN, false, false);
    if (base64StaticReadBuffer != nullptr) {
        heap_caps_free(base64StaticReadBuffer);
        base64StaticReadBuffer = nullptr;
    }
    if (base64StaticOutputBuffer != nullptr) {
        heap_caps_free(base64StaticOutputBuffer);
        base64StaticOutputBuffer = nullptr;
    }
#endif
}

bool smtpRawSend(Client* client, const String &data) {
    if (!client || !client->connected()) return false;
    size_t written = client->print(data);
    return (written == data.length());
}

bool smtpRawSendC(Client* client, const char *data) {
    if (!client || !client->connected() || !data) return false;
    size_t len = strlen(data);
    size_t written = client->write((const uint8_t*)data, len);
    return (written == len);
}

bool smtpRawSendF(Client* client, const __FlashStringHelper *data) {
    if (!client || !client->connected()) return false;
    String s = data;
    size_t written = client->print(s);
    return (written == s.length());
}

bool smtpRawWrite(Client* client, const uint8_t *buf, size_t len) {
    if (!client || !client->connected()) return false;
    size_t totalWritten = 0;
    unsigned long deadline = millis() + 10000UL;
    while (totalWritten < len) {
        if (!client->connected()) return false;
        if (millis() > deadline) return false;
        size_t written = client->write(buf + totalWritten, len - totalWritten);
        if (written == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        totalWritten += written;
    }
    return true;
}

void smtpDrainAvailable(Client* client, unsigned long maxMs) {
    if (!client) return;
    unsigned long start = millis();
    while (millis() - start < maxMs) {
        bool readAny = false;
        while (client->available()) {
            client->read();
            readAny = true;
        }
        if (!readAny) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

bool smtpReadLine(Client* client, char *out, size_t outLen, unsigned long timeoutMs) {
    if (!client || !out || outLen == 0) return false;
    size_t idx = 0;
    out[0] = 0;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        esp_task_wdt_reset();
        while (client->available()) {
            char c = (char)client->read();
            if (idx + 1 < outLen) {
                out[idx++] = c;
                out[idx] = 0;
            }
            if (c == '\n') {
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return (idx > 0);
}

int smtpParseReplyCode(const char *line) {
    if (!line) return -1;
    if (!isdigit((unsigned char)line[0]) || !isdigit((unsigned char)line[1]) || !isdigit((unsigned char)line[2])) return -1;
    return (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
}

int smtpWaitForCode(Client* client, int expectedCode, bool multiline, unsigned long timeoutMs, char *lastLineOut, size_t lastLineLen) {
    if (lastLineOut && lastLineLen > 0) lastLineOut[0] = 0;
    if (!client) return -1;

    char line[256];
    int lastCode = -1;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        esp_task_wdt_reset();
        unsigned long remaining = timeoutMs - (millis() - start);
        if (!smtpReadLine(client, line, sizeof(line), (remaining > 250UL) ? 250UL : remaining)) {
            continue;
        }

        lastCode = smtpParseReplyCode(line);
        if (lastLineOut && lastLineLen > 0) {
            strncpy(lastLineOut, line, lastLineLen - 1);
            lastLineOut[lastLineLen - 1] = 0;
        }

        if (lastCode == expectedCode) {
            if (!multiline) return lastCode;
            if (strlen(line) >= 4 && line[3] == ' ') return lastCode;
            continue;
        }

        if (lastCode >= 400 && lastCode <= 599) {
            return lastCode;
        }
    }
    return lastCode;
}

int smtpWaitFor2xx(Client* client, unsigned long timeoutMs, char *lastLineOut, size_t lastLineLen) {
    if (lastLineOut && lastLineLen > 0) lastLineOut[0] = 0;
    if (!client) return -1;

    char line[256];
    int lastCode = -1;
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        esp_task_wdt_reset();
        unsigned long remaining = timeoutMs - (millis() - start);
        if (!smtpReadLine(client, line, sizeof(line), (remaining > 250UL) ? 250UL : remaining)) {
            continue;
        }

        lastCode = smtpParseReplyCode(line);
        if (lastLineOut && lastLineLen > 0) {
            strncpy(lastLineOut, line, lastLineLen - 1);
            lastLineOut[lastLineLen - 1] = 0;
        }

        if (lastCode >= 200 && lastCode < 300) {
            if (strlen(line) >= 4 && line[3] == ' ') return lastCode;
            continue;
        }

        if (lastCode >= 400 && lastCode <= 599) {
            return lastCode;
        }
    }
    return lastCode;
}

bool smtpStreamPsramBase64Internal(Client* client, const String& folderPath, const String& filename, size_t startOffset, size_t bytesToRead) {
    if (!psramVFSIsEnabled()) return false;

    size_t totalSize = psramVFSGetFileSize(folderPath, filename);
    if (totalSize == 0) return false;
    if (bytesToRead == 0 || (startOffset + bytesToRead > totalSize)) {
        bytesToRead = totalSize - startOffset;
    }

    uint8_t* buf = nullptr;
    char* b64buf = nullptr;
    const size_t chunkSize = 576;
    const size_t b64BufSize = 1024;
    buf = (uint8_t*) malloc(chunkSize);
    b64buf = (char*) malloc(b64BufSize);

    if (!buf || !b64buf) {
        sendVirtualMessage(F("STREAM PSRAM: OOM Critical (buffer alloc failed)"), ERROR, false, false);
        if (buf) free(buf);
        if (b64buf) free(b64buf);
        return false;
    }

    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int lineLen = 0;
    size_t totalRead = 0;
    size_t currentOffset = startOffset;
    bool success = true;

    while (totalRead < bytesToRead) {
        esp_task_wdt_reset();
        taskYIELD();
        priorityManager().yieldDuringEmail();

        size_t toRead = min((size_t)chunkSize, bytesToRead - totalRead);
        size_t readCount = psramVFSReadChunk(folderPath, filename, currentOffset, buf, toRead);
        if (readCount == 0) {
            break;
        }

        totalRead += readCount;
        currentOffset += readCount;

        int b64out = 0;
        for (size_t i = 0; i < readCount; i += 3) {
            uint32_t triplet = 0;
            int validBytes = 0;
            for (int j = 0; j < 3 && (i + j) < readCount; j++) {
                triplet = (triplet << 8) | buf[i + j];
                validBytes++;
            }
            if (validBytes < 3) triplet <<= 8 * (3 - validBytes);

            b64buf[b64out++] = b64[(triplet >> 18) & 0x3F];
            b64buf[b64out++] = b64[(triplet >> 12) & 0x3F];
            b64buf[b64out++] = (validBytes > 1) ? b64[(triplet >> 6) & 0x3F] : '=';
            b64buf[b64out++] = (validBytes > 2) ? b64[triplet & 0x3F] : '=';

            lineLen += 4;
            if (lineLen >= 76) {
                b64buf[b64out++] = '\r';
                b64buf[b64out++] = '\n';
                lineLen = 0;
            }
        }

        if (b64out > 0) {
            if (!smtpRawWrite(client, (const uint8_t*)b64buf, b64out)) {
                sendVirtualMessage(F("❌ STREAM PSRAM: Socket write failed"), ERROR, false, false);
                success = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    free(buf);
    free(b64buf);
    return success;
}

bool smtpStreamFileBase64Internal(Client* client, File &file, size_t startOffset, size_t bytesToRead, bool useStaticBuffers) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (startOffset > 0 && !file.seek(startOffset)) {
        sendVirtualMessage(F("STREAM: Failed to seek to offset"), ERROR, false, false);
        return false;
    }

    size_t actualBytesToRead = bytesToRead;
    if (actualBytesToRead == 0) {
        actualBytesToRead = file.size() - startOffset;
    }

    bool usePsram = psramFound();
    uint8_t* buf = nullptr;
    char* b64buf = nullptr;
    size_t chunkSize = 576;
    size_t b64BufSize = 1024;

#if SMTP_HELPERS_HAVE_PSRAM_API
    if (usePsram && useStaticBuffers && base64StaticReadBuffer != nullptr && base64StaticOutputBuffer != nullptr) {
        buf = base64StaticReadBuffer;
        b64buf = base64StaticOutputBuffer;
        chunkSize = BASE64_STATIC_READ_SIZE;
        b64BufSize = BASE64_STATIC_OUTPUT_SIZE;
    } else if (usePsram && !useStaticBuffers) {
        chunkSize = 8192;
        b64BufSize = (chunkSize * 4 / 3) + (chunkSize / 19) + 512;
        buf = (uint8_t*) heap_caps_malloc(chunkSize, MALLOC_CAP_SPIRAM);
        b64buf = (char*) heap_caps_malloc(b64BufSize, MALLOC_CAP_SPIRAM);
    } else
#endif
    {
        buf = (uint8_t*) malloc(chunkSize);
        b64buf = (char*) malloc(b64BufSize);
    }

    if (!buf || !b64buf) {
        sendVirtualMessage(F("STREAM: OOM Critical (buffer allocation failed)"), ERROR, false, false);
#if SMTP_HELPERS_HAVE_PSRAM_API
        if (buf != base64StaticReadBuffer && buf) { if (usePsram && !useStaticBuffers) heap_caps_free(buf); else free(buf); }
        if (b64buf != base64StaticOutputBuffer && b64buf) { if (usePsram && !useStaticBuffers) heap_caps_free(b64buf); else free(b64buf); }
#else
        if (buf) free(buf);
        if (b64buf) free(b64buf);
#endif
        return false;
    }

    int lineLen = 0;
    size_t totalRead = 0;
    bool success = true;

    while (file.available() && totalRead < actualBytesToRead) {
        esp_task_wdt_reset();
        taskYIELD();
        priorityManager().yieldDuringEmail();

        size_t toRead = min((size_t)chunkSize, actualBytesToRead - totalRead);
        size_t readCount = file.read(buf, toRead);
        if (readCount == 0 && file.available()) {
            sendVirtualMessage(F("❌ STREAM: SD read returned 0 bytes unexpectedly"), ERROR, false, false);
            success = false;
            break;
        }

        totalRead += readCount;

        int b64out = 0;
        for (size_t i = 0; i < readCount; i += 3) {
            uint32_t triplet = 0;
            int validBytes = 0;
            for (int j = 0; j < 3 && (i + j) < readCount; j++) {
                triplet = (triplet << 8) | buf[i + j];
                validBytes++;
            }
            if (validBytes < 3) triplet <<= 8 * (3 - validBytes);

            b64buf[b64out++] = b64[(triplet >> 18) & 0x3F];
            b64buf[b64out++] = b64[(triplet >> 12) & 0x3F];
            b64buf[b64out++] = (validBytes > 1) ? b64[(triplet >> 6) & 0x3F] : '=';
            b64buf[b64out++] = (validBytes > 2) ? b64[triplet & 0x3F] : '=';

            lineLen += 4;
            if (lineLen >= 76) {
                b64buf[b64out++] = '\r';
                b64buf[b64out++] = '\n';
                lineLen = 0;
            }
        }

        if (b64out > 0) {
            if (!smtpRawWrite(client, (const uint8_t*)b64buf, b64out)) {
                sendVirtualMessage(F("❌ STREAM: SMTP write failed during Base64 transmission"), ERROR, false, false);
                success = false;
                break;
            }
        }
    }

    if (success && lineLen > 0) {
        if (!smtpRawSendC(client, "\r\n")) {
            success = false;
        }
    }

#if SMTP_HELPERS_HAVE_PSRAM_API
    if (buf != base64StaticReadBuffer && buf) { if (usePsram && !useStaticBuffers) heap_caps_free(buf); else free(buf); }
    if (b64buf != base64StaticOutputBuffer && b64buf) { if (usePsram && !useStaticBuffers) heap_caps_free(b64buf); else free(b64buf); }
#else
    if (buf) free(buf);
    if (b64buf) free(b64buf);
#endif

    return success;
}

bool smtpStreamFileAsBase64(Client* client, File &file, size_t fileSize) {
    return smtpStreamFileBase64Internal(client, file, 0, fileSize, true);
}

bool smtpStreamPsramFileAsBase64(Client* client, const String& folderPath, const String& fileName, size_t fileSize) {
    size_t actualSize = 0;
    const char* fileData = psramVFSGetFileDataPtr(folderPath, fileName, &actualSize);
    if (!fileData || actualSize == 0) {
        sendVirtualMessage(String(F("PSRAM STREAM: file not found or empty: ")) + fileName, ERROR, false, false);
        return false;
    }

    if (actualSize != fileSize) {
        sendVirtualMessage(smtpBuildMessage("PSRAM STREAM: size mismatch %u vs %u for %s",
                                              (unsigned)actualSize,
                                              (unsigned)fileSize,
                                              fileName.c_str()), WARN, false, false);
    }

    uint8_t* buf = nullptr;
    char* b64buf = nullptr;
    const size_t chunkSize = 576;
    const size_t b64BufSize = 1024;
    buf = (uint8_t*) malloc(chunkSize);
    b64buf = (char*) malloc(b64BufSize);

    if (!buf || !b64buf) {
        sendVirtualMessage(F("PSRAM STREAM: OOM (buffer allocation failed)"), ERROR, false, false);
        if (buf) free(buf);
        if (b64buf) free(b64buf);
        return false;
    }

    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int lineLen = 0;
    size_t totalRead = 0;
    bool success = true;

    while (totalRead < actualSize) {
        esp_task_wdt_reset();
        taskYIELD();
        priorityManager().yieldDuringEmail();

        size_t toRead = min((size_t)chunkSize, actualSize - totalRead);
        memcpy(buf, fileData + totalRead, toRead);
        totalRead += toRead;

        int b64out = 0;
        for (size_t i = 0; i < toRead; i += 3) {
            uint32_t triplet = 0;
            int validBytes = 0;
            for (int j = 0; j < 3 && (i + j) < toRead; j++) {
                triplet = (triplet << 8) | buf[i + j];
                validBytes++;
            }
            if (validBytes < 3) triplet <<= 8 * (3 - validBytes);

            b64buf[b64out++] = b64[(triplet >> 18) & 0x3F];
            b64buf[b64out++] = b64[(triplet >> 12) & 0x3F];
            b64buf[b64out++] = (validBytes > 1) ? b64[(triplet >> 6) & 0x3F] : '=';
            b64buf[b64out++] = (validBytes > 2) ? b64[triplet & 0x3F] : '=';

            lineLen += 4;
            if (lineLen >= 76) {
                b64buf[b64out++] = '\r';
                b64buf[b64out++] = '\n';
                lineLen = 0;
            }
        }

        if (b64out > 0) {
            if (!smtpRawWrite(client, (const uint8_t*)b64buf, b64out)) {
                sendVirtualMessage(F("❌ PSRAM STREAM: SMTP write failed during Base64 transmission"), ERROR, false, false);
                success = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    if (success && lineLen > 0) {
        if (!smtpRawSendC(client, "\r\n")) {
            success = false;
        }
    }

    free(buf);
    free(b64buf);
    return success;
}

bool smtpStreamStringAsBase64(Client* client, const String &content) {
    size_t contentLen = content.length();
    if (contentLen == 0) {
        return true;
    }

    uint8_t* buf = nullptr;
    char* b64buf = nullptr;
    const size_t chunkSize = 576;
    const size_t b64BufSize = 1024;
    buf = (uint8_t*) malloc(chunkSize);
    b64buf = (char*) malloc(b64BufSize);

    if (!buf || !b64buf) {
        sendVirtualMessage(F("STREAM: OOM Critical (PSRAM buffer allocation failed)"), ERROR, false, false);
        if (buf) free(buf);
        if (b64buf) free(b64buf);
        return false;
    }

    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int lineLen = 0;
    size_t totalRead = 0;
    bool success = true;
    const char* dataPtr = content.c_str();

    while (totalRead < contentLen) {
        esp_task_wdt_reset();
        taskYIELD();
        priorityManager().yieldDuringEmail();

        size_t toRead = min((size_t)chunkSize, contentLen - totalRead);
        memcpy(buf, dataPtr + totalRead, toRead);
        totalRead += toRead;

        int b64out = 0;
        for (size_t i = 0; i < toRead; i += 3) {
            uint32_t triplet = 0;
            int validBytes = 0;
            for (int j = 0; j < 3 && (i + j) < toRead; j++) {
                triplet = (triplet << 8) | buf[i + j];
                validBytes++;
            }
            if (validBytes < 3) triplet <<= 8 * (3 - validBytes);

            b64buf[b64out++] = b64[(triplet >> 18) & 0x3F];
            b64buf[b64out++] = b64[(triplet >> 12) & 0x3F];
            b64buf[b64out++] = (validBytes > 1) ? b64[(triplet >> 6) & 0x3F] : '=';
            b64buf[b64out++] = (validBytes > 2) ? b64[triplet & 0x3F] : '=';

            lineLen += 4;
            if (lineLen >= 76) {
                b64buf[b64out++] = '\r';
                b64buf[b64out++] = '\n';
                lineLen = 0;
            }
        }

        if (b64out > 0) {
            if (!smtpRawWrite(client, (const uint8_t*)b64buf, b64out)) {
                sendVirtualMessage(F("❌ STREAM: SMTP write failed during PSRAM Base64 transmission"), ERROR, false, false);
                success = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    if (success && lineLen > 0) {
        if (!smtpRawSendC(client, "\r\n")) {
            success = false;
        }
    }

    free(buf);
    free(b64buf);
    return success;
}

bool smtpStreamFileRangeAsBase64(Client* client, File &file, size_t startOffset, size_t endOffset) {
    size_t bytesToRead = endOffset - startOffset;
    return smtpStreamFileBase64Internal(client, file, startOffset, bytesToRead, false);
}

// ---------------------------------------------------------------------------
// Flash (LittleFS) streaming via the email-flash handle API.
// Framing identical to smtpStreamFileBase64Internal (76-col Base64, final
// CRLF); reads are emailFlashRead() so this runs from any task stack —
// direct on an internal stack, loop-served RPC on a PSRAM stack. The 8KB
// chunk bounds one RPC round-trip per 8KB instead of per 576B.
// ---------------------------------------------------------------------------
static bool smtpStreamFlashRangeInternal(Client* client, const String& flashPath,
                                         size_t startOffset, size_t bytesToRead) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int32_t efh = emailFlashOpenRead(flashPath);
    if (efh < 0) {
        sendVirtualMessage(String(F("❌ FLASH STREAM: open failed: ")) + flashPath, ERROR, false, false);
        return false;
    }
    if (startOffset > 0 && !emailFlashSeek(efh, startOffset)) {
        sendVirtualMessage(F("❌ FLASH STREAM: seek failed"), ERROR, false, false);
        emailFlashClose(efh);
        return false;
    }

    const size_t chunkSize = 8192;
    const size_t b64BufSize = 11520; // ceil(8192/3)*4 + CRLF slack
    uint8_t* buf = (uint8_t*)malloc(chunkSize);
    char* b64buf = (char*)malloc(b64BufSize);
    if (!buf || !b64buf) {
        if (buf) free(buf);
        if (b64buf) free(b64buf);
        emailFlashClose(efh);
        sendVirtualMessage(F("❌ FLASH STREAM: OOM"), ERROR, false, false);
        return false;
    }

    int lineLen = 0;
    size_t totalRead = 0;
    bool success = true;

    while (totalRead < bytesToRead) {
        esp_task_wdt_reset();
        taskYIELD();
        priorityManager().yieldDuringEmail();

        size_t toRead = bytesToRead - totalRead;
        if (toRead > chunkSize) toRead = chunkSize;
        size_t readCount = emailFlashRead(efh, buf, toRead);
        if (readCount == 0) {
            // Mirror smtpStreamFileBase64Internal: a 0-byte read is EOF, not an
            // error, as long as something was already read. Nothing at all is
            // a real failure (open/lock/RPC), so keep that loud.
            if (totalRead == 0) {
                sendVirtualMessage(F("❌ FLASH STREAM: read returned 0 bytes unexpectedly"), ERROR, false, false);
                success = false;
            } else {
                sendVirtualMessage(F("⚠️ FLASH STREAM: early EOF (file shorter than expected)"), WARN);
            }
            break;
        }

        totalRead += readCount;

        int b64out = 0;
        for (size_t i = 0; i < readCount; i += 3) {
            uint32_t triplet = 0;
            int validBytes = 0;
            for (int j = 0; j < 3 && (i + j) < readCount; j++) {
                triplet = (triplet << 8) | buf[i + j];
                validBytes++;
            }
            if (validBytes < 3) triplet <<= 8 * (3 - validBytes);

            b64buf[b64out++] = b64[(triplet >> 18) & 0x3F];
            b64buf[b64out++] = b64[(triplet >> 12) & 0x3F];
            b64buf[b64out++] = (validBytes > 1) ? b64[(triplet >> 6) & 0x3F] : '=';
            b64buf[b64out++] = (validBytes > 2) ? b64[triplet & 0x3F] : '=';

            lineLen += 4;
            if (lineLen >= 76) {
                b64buf[b64out++] = '\r';
                b64buf[b64out++] = '\n';
                lineLen = 0;
            }
        }

        if (b64out > 0) {
            if (!smtpRawWrite(client, (const uint8_t*)b64buf, b64out)) {
                sendVirtualMessage(F("❌ FLASH STREAM: SMTP write failed during Base64 transmission"), ERROR, false, false);
                success = false;
                break;
            }
        }
    }

    if (success && lineLen > 0) {
        if (!smtpRawSendC(client, "\r\n")) {
            success = false;
        }
    }

    free(buf);
    free(b64buf);
    emailFlashClose(efh);
    return success;
}

bool smtpStreamFlashAsBase64(Client* client, const String& flashPath, size_t fileSize) {
    return smtpStreamFlashRangeInternal(client, flashPath, 0, fileSize);
}

bool smtpStreamFlashRangeAsBase64(Client* client, const String& flashPath, size_t startOffset, size_t endOffset) {
    if (endOffset <= startOffset) return true;
    return smtpStreamFlashRangeInternal(client, flashPath, startOffset, endOffset - startOffset);
}
#else

void smtpInitStaticBase64Buffers() {}

bool smtpRawSend(Client*, const String &) { return false; }
bool smtpRawSendC(Client*, const char *) { return false; }
bool smtpRawSendF(Client*, const __FlashStringHelper *) { return false; }
bool smtpRawWrite(Client*, const uint8_t *, size_t) { return false; }
void smtpDrainAvailable(Client*, unsigned long) {}
bool smtpReadLine(Client*, char *, size_t, unsigned long) { return false; }
int smtpParseReplyCode(const char *) { return -1; }
int smtpWaitForCode(Client*, int, bool, unsigned long, char *, size_t) { return -1; }
int smtpWaitFor2xx(Client*, unsigned long, char *, size_t) { return -1; }
bool smtpStreamPsramBase64Internal(Client*, const String&, const String&, size_t, size_t) { return false; }
bool smtpStreamFileBase64Internal(Client*, File &, size_t, size_t, bool) { return false; }
bool smtpStreamFileAsBase64(Client*, File &, size_t) { return false; }
bool smtpStreamPsramFileAsBase64(Client*, const String&, const String&, size_t) { return false; }
bool smtpStreamStringAsBase64(Client*, const String &) { return false; }
bool smtpStreamFileRangeAsBase64(Client*, File &, size_t, size_t) { return false; }
bool smtpStreamFlashAsBase64(Client*, const String&, size_t) { return false; }
bool smtpStreamFlashRangeAsBase64(Client*, const String&, size_t, size_t) { return false; }

#endif // PSRAM guard
#endif // ENABLE_SMTP