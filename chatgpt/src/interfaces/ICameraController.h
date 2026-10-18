#pragma once
#include <Arduino.h>
#include <stdint.h>

struct CameraConfig {
    uint16_t frameWidth = 1600;
    uint16_t frameHeight = 1200;
    uint8_t jpegQuality = 12;
    uint8_t brightness = 0;
    int8_t contrast = 0;
    int8_t saturation = 0;
    bool verticalFlip = false;
    bool horizontalMirror = false;
    bool nightMode = false;
    uint8_t frameRate = 15;
    bool autoWhiteBalance = true;
    bool autoExposure = true;
};

struct CameraFrame {
    uint8_t* data;
    size_t len;
    uint32_t timestamp;
    bool isJpeg;
    void (*freeFn)(const uint8_t*);
};

struct CameraRecordingStatus {
    bool isRecording;
    uint32_t startTime;
    uint32_t elapsedMs;
    size_t bytesWritten;
    String fileName;
};

class ICameraController {
public:
    virtual ~ICameraController() = default;

    virtual bool init(const CameraConfig& config) = 0;
    virtual void loop() = 0;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual bool configure(const CameraConfig& config) = 0;
    virtual CameraConfig getConfig() const = 0;

    virtual bool capture(CameraFrame& frame) = 0;
    virtual void freeFrame(CameraFrame& frame) = 0;

    virtual bool startStreaming(void (*cb)(const CameraFrame&)) = 0;
    virtual void stopStreaming() = 0;
    virtual bool isStreaming() const = 0;

    virtual bool startRecording(const String& path) = 0;
    virtual void stopRecording() = 0;
    virtual CameraRecordingStatus getRecordingStatus() const = 0;

    virtual bool detectMotion(float sensitivity = 0.1) = 0;
    virtual void setMotionCallback(bool (*cb)()) = 0;

    virtual bool isReady() const = 0;
    virtual String getModel() const = 0;
};
