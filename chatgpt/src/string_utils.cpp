#include "string_utils.h"
#include "commands.h"
#include <stdarg.h>

String buildMessage(const char* format, ...) {
    char msgBuffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(msgBuffer, sizeof(msgBuffer), format, args);
    va_end(args);
    return String(msgBuffer);
}

void safeLog(const char* fmt, int level, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, level);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sendVirtualMessage(String(buf), level, false, false);
}
