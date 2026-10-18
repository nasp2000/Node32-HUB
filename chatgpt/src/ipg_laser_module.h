#pragma once

#include <Arduino.h>
#include "feature_flags.h"

#if ENABLE_IPG_LASER == 1

struct IpgLaserCommandInfo {
    const char* code;
    const char* category;
    const char* usage;
    const char* description;
    const char* response;
    const char* notes;
};

struct IpgLaserLogEntry {
    uint32_t ageMs;
    char direction[4];
    char text[160];
};

struct IpgLaserStatus {
    bool captureActive;
    uint32_t captureEndsInMs;
    uint32_t lastRxAgeMs;
    uint16_t logCount;
    char lastCommand[64];
};

void ipgLaserBegin();
void ipgLaserUpdate();
void ipgLaserOnRxByte(char byteValue);
bool ipgLaserSendCommand(const String& command, const String& templateName);
void ipgLaserClearLog();
IpgLaserStatus ipgLaserGetStatus();
uint16_t ipgLaserCopyLog(IpgLaserLogEntry* entries, uint16_t maxEntries);
const IpgLaserCommandInfo* ipgLaserGetCommandCatalog(size_t* count);

#endif