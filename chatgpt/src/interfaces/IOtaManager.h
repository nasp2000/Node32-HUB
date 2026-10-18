#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class OtaState : uint8_t {
    Idle = 0,
    Downloading = 1,
    Verifying = 2,
    ReadyToBoot = 3,
    Failed = 4,
    RolledBack = 5
};

enum class OtaSource : uint8_t {
    WiFi = 0,
    Ethernet = 1,
    Local = 2
};

struct OtaStatus {
    OtaState state;
    OtaSource source;
    size_t receivedBytes;
    size_t totalBytes;
    int progressPercent;
    String currentVersion;
    String newVersion;
    String errorMessage;
    bool signatureValid;
};

class IOtaManager {
public:
    virtual ~IOtaManager() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    virtual bool beginUpdate(size_t totalSize, OtaSource source) = 0;
    virtual bool writeChunk(const uint8_t* data, size_t len) = 0;
    virtual bool endUpdate() = 0;
    virtual void abortUpdate() = 0;

    virtual bool verifyFirmware() = 0;
    virtual bool markBootValid() = 0;
    virtual bool rollback() = 0;

    virtual OtaStatus getStatus() const = 0;
    virtual bool isUpdating() const = 0;
    virtual const char* currentVersion() const = 0;

    virtual bool setPubKey(const uint8_t* key, size_t len) = 0;
    virtual bool isSignatureRequired() const = 0;

    virtual bool switchToPartition(int partition) = 0;
    virtual int getBootPartition() const = 0;
    virtual int getUpdatePartition() const = 0;
};
