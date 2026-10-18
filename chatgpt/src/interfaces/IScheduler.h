#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <vector>

enum class ScheduleType : uint8_t {
    OneShot = 0,
    Daily = 1,
    Weekly = 2,
    Monthly = 3,
    Interval = 4
};

enum class ScheduleAction : uint8_t {
    Command = 0,
    Email = 1,
    HttpGet = 2,
    HttpPost = 3,
    GpioToggle = 4,
    Custom = 5
};

struct ScheduleEntry {
    uint32_t id;
    ScheduleType type;
    ScheduleAction action;
    String command;
    String params;
    uint8_t hour;
    uint8_t minute;
    uint8_t dayOfWeek;
    uint8_t dayOfMonth;
    uint32_t intervalMs;
    bool enabled;
    uint32_t lastTriggered;
    uint32_t triggerCount;
};

class IScheduler {
public:
    virtual ~IScheduler() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    virtual uint32_t addEntry(const ScheduleEntry& entry) = 0;
    virtual bool removeEntry(uint32_t id) = 0;
    virtual bool updateEntry(const ScheduleEntry& entry) = 0;
    virtual bool setEnabled(uint32_t id, bool enabled) = 0;

    virtual const ScheduleEntry* getEntry(uint32_t id) const = 0;
    virtual bool getAllEntries(std::vector<ScheduleEntry>& out) const = 0;
    virtual void clearAll() = 0;

    virtual size_t entryCount() const = 0;
    virtual bool isBusy() const = 0;

    virtual void setActionCallback(uint32_t id, void (*cb)(const ScheduleEntry&)) = 0;
    virtual void setDefaultAction(void (*cb)(const String& cmd)) = 0;
};
