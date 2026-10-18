#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cctype>

#ifndef GRBL_MAX_LINE_LEN
  #define GRBL_MAX_LINE_LEN 96
#endif

static inline size_t stripLine(const char* src, char* dst, size_t dstSize) {
    size_t len = 0;
    for (size_t i = 0; src[i] && len < dstSize - 1; i++) {
        if (src[i] == ';') break;
        if (src[i] == '\r' || src[i] == '\n') break;
        dst[len++] = toupper((unsigned char)src[i]);
    }
    while (len > 0 && dst[len - 1] == ' ') len--;
    dst[len] = '\0';
    return len;
}

static inline bool isCriticalPersistentCommand(const char* line, size_t len) {
    if (!line || len == 0) return false;
    if (line[0] == '$') {
        if (len >= 2 && (line[1] == '$' || line[1] == '#' || line[1] == 'I' || line[1] == 'N')) return true;
        if (len >= 5 && strncmp(line, "$RST=", 5) == 0) return true;
        size_t i = 1;
        bool hasDigits = false;
        while (i < len && isdigit((unsigned char)line[i])) {
            hasDigits = true;
            i++;
        }
        if (hasDigits && i < len && line[i] == '=') return true;
    }
    if (len >= 3 &&
        (strncmp(line, "G10", 3) == 0 ||
         strncmp(line, "G28", 3) == 0 ||
         strncmp(line, "G30", 3) == 0)) {
        return true;
    }
    if (len >= 3 && strncmp(line, "G5", 2) == 0) {
        char d = line[2];
        if (d >= '4' && d <= '9') return true;
    }
    return false;
}

static inline bool isRepeatedRealtimeStatusQuery(const char* line, size_t len) {
    if (!line || len == 0) return false;
    if (line[0] != '?') return false;
    for (size_t i = 1; i < len; i++) {
        if (line[i] != '?') return false;
    }
    return true;
}

static inline size_t preprocessGcodeInPsram(char* buf, size_t len) {
    if (!buf || len == 0) return len;
    size_t readPos = 0;
    size_t writePos = 0;
    int motionMode = 0;
    bool isAbsolute = true;
    while (readPos < len) {
        size_t lineStart = readPos;
        while (readPos < len && buf[readPos] != '\n') readPos++;
        size_t lineEnd = readPos;
        if (readPos < len) readPos++;
        size_t rawLen = lineEnd - lineStart;
        if (rawLen > 0 && buf[lineStart + rawLen - 1] == '\r') rawLen--;
        {
            size_t cleanEnd = 0;
            uint8_t parenDepth = 0;
            for (size_t i = 0; i < rawLen; i++) {
                char c = buf[lineStart + i];
                if (c == '(') { parenDepth++; continue; }
                if (parenDepth > 0) {
                    if (c == ')') parenDepth--;
                    continue;
                }
                buf[lineStart + (cleanEnd++)] = (char)toupper((unsigned char)c);
            }
            rawLen = cleanEnd;
        }
        {
            size_t commentPos = 0;
            bool found = false;
            for (size_t i = 0; i < rawLen; i++) {
                if (buf[lineStart + i] == ';') { commentPos = i; found = true; break; }
            }
            if (found) rawLen = commentPos;
        }
        size_t trimStart = 0;
        while (trimStart < rawLen && (buf[lineStart + trimStart] == ' ' || buf[lineStart + trimStart] == '\t')) trimStart++;
        size_t trimEnd = rawLen;
        while (trimEnd > trimStart && (buf[lineStart + trimEnd - 1] == ' ' || buf[lineStart + trimEnd - 1] == '\t')) trimEnd--;
        rawLen = trimEnd - trimStart;
        lineStart += trimStart;
        if (rawLen > 0) {
            size_t compactLen = 0;
            bool lastWasSpace = false;
            for (size_t i = 0; i < rawLen; i++) {
                char c = buf[lineStart + i];
                if (c == ' ' || c == '\t') {
                    if (!lastWasSpace && compactLen > 0) buf[lineStart + compactLen++] = ' ';
                    lastWasSpace = true;
                } else {
                    buf[lineStart + compactLen++] = c;
                    lastWasSpace = false;
                }
            }
            rawLen = compactLen;
        }
        if (rawLen == 0) continue;
        {
            char stripped[GRBL_MAX_LINE_LEN + 1];
            size_t slen = 0;
            size_t i = 0;
            while (i < rawLen) {
                while (i < rawLen && buf[lineStart + i] == ' ') i++;
                if (i >= rawLen) break;
                size_t ws = i;
                while (i < rawLen && buf[lineStart + i] != ' ') i++;
                size_t wlen = i - ws;
                bool skipWord = false;
                if (buf[lineStart + ws] == 'G') {
                    size_t di = ws + 1;
                    int gNum = -1;
                    if (di < ws + wlen && buf[lineStart + di] >= '0' && buf[lineStart + di] <= '9') {
                        gNum = 0;
                        while (di < ws + wlen && buf[lineStart + di] >= '0' && buf[lineStart + di] <= '9') {
                            gNum = gNum * 10 + (buf[lineStart + di] - '0');
                            di++;
                        }
                    }
                    if (gNum >= 0) {
                        if (gNum >= 0 && gNum <= 3) {
                            if (gNum == motionMode) skipWord = true;
                            else motionMode = gNum;
                        } else if (gNum == 90) {
                            if (isAbsolute) skipWord = true;
                            else isAbsolute = true;
                        } else if (gNum == 91) {
                            if (!isAbsolute) skipWord = true;
                            else isAbsolute = false;
                        }
                    }
                }
                if (skipWord) continue;
                if (slen > 0) stripped[slen++] = ' ';
                memcpy(stripped + slen, buf + lineStart + ws, wlen);
                slen += wlen;
            }
            if (slen == 0) continue;
            memcpy(buf + writePos, stripped, slen);
            buf[writePos + slen] = '\n';
            writePos += slen + 1;
        }
    }
    return writePos;
}
