#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class GrblState : uint8_t {
    Idle = 0,
    Running = 1,
    Hold = 2,
    Door = 3,
    Check = 4,
    Home = 5,
    Sleep = 6,
    Alarm = 7,
    Jog = 8,
    Unknown = 9
};

enum class GrblSendMode : uint8_t {
    Manual = 0,
    Stream = 1,
    Paused = 2
};

struct GrblStatus {
    GrblState state;
    float posX, posY, posZ;
    float wcoX, wcoY, wcoZ;
    float feedRate;
    float spindleSpeed;
    uint8_t overrides;
    uint32_t lineNumber;
    uint32 linesSent;
    uint32 linesCompleted;
    uint32 errorCount;
    bool hasAlarm;
    String alarmMessage;
    uint32_t lastResponseMs;
};

class IGrblSender {
public:
    virtual ~IGrblSender() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    virtual bool connect(unsigned long baud) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual bool sendCommand(const String& cmd) = 0;
    virtual bool sendGcode(const String& line) = 0;

    virtual bool streamFile(const String& path) = 0;
    virtual void pauseStream() = 0;
    virtual void resumeStream() = 0;
    virtual void stopStream() = 0;
    virtual bool isStreaming() const = 0;
    virtual float streamProgress() const = 0;

    virtual bool uploadToPsram(const String& gcode) = 0;
    virtual bool uploadFromSd(const String& path) = 0;
    virtual void clearPsram() = 0;
    virtual size_t psramGcodeSize() const = 0;

    virtual GrblStatus getStatus() const = 0;
    virtual String getLastResponse() const = 0;
    virtual void reset() = 0;

    virtual void setStatusCallback(void (*cb)(const GrblStatus&)) = 0;
    virtual void setResponseCallback(void (*cb)(const String& line)) = 0;
};
