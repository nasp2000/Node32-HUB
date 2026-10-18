#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class LogLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Critical = 4,
    None = 5
};

struct LogEntry {
    uint32_t timestamp;
    LogLevel level;
    String module;
    String message;
};

class ILogOutput {
public:
    virtual ~ILogOutput() = default;
    virtual void begin() = 0;
    virtual void write(LogLevel level, const String& module, const String& message) = 0;
    virtual void flush() = 0;
};

class ILogger {
public:
    virtual ~ILogger() = default;

    virtual void init() = 0;
    virtual void loop() = 0;

    virtual void log(LogLevel level, const String& module, const String& message) = 0;
    virtual void debug(const String& module, const String& message) = 0;
    virtual void info(const String& module, const String& message) = 0;
    virtual void warn(const String& module, const String& message) = 0;
    virtual void error(const String& module, const String& message) = 0;
    virtual void critical(const String& module, const String& message) = 0;

    virtual void setLevel(LogLevel level) = 0;
    virtual LogLevel getLevel() const = 0;

    virtual void addOutput(ILogOutput* output) = 0;
    virtual bool removeOutput(ILogOutput* output) = 0;

    virtual void addVirtualMessage(const String& msg, LogLevel level) = 0;
    virtual void getHistory(std::vector<LogEntry>& out, size_t maxCount = 100) const = 0;
    virtual void clearHistory() = 0;

    virtual size_t outputCount() const = 0;
};

class ISerialLogOutput : public ILogOutput {
public:
    virtual ~ISerialLogOutput() = default;
    virtual void setBaudRate(unsigned long baud) = 0;
};

class ITelnetLogOutput : public ILogOutput {
public:
    virtual ~ITelnetLogOutput() = default;
    virtual size_t clientCount() const = 0;
    virtual void broadcast(const String& msg) = 0;
};
