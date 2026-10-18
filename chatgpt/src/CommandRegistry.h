#pragma once
#include <Arduino.h>

#define MAX_CMD_REGISTRY 48

enum CmdMatch : uint8_t {
    CMD_EXACT = 0,
    CMD_PREFIX
};

typedef bool (*CommandHandler)(const String& cmdLine, const String& upperCmd);

class CommandRegistry {
public:
    void add(const char* name, CommandHandler handler, CmdMatch mode = CMD_EXACT);
    bool dispatch(const String& cmdLine, const String& upperCmd);
    void begin();
    int count() const { return _count; }
    int capacity() const { return MAX_CMD_REGISTRY; }
private:
    struct Entry {
        const char* name;
        CommandHandler handler;
        CmdMatch mode;
    };
    Entry _entries[MAX_CMD_REGISTRY];
    int _count = 0;
};

extern CommandRegistry commandRegistry;
