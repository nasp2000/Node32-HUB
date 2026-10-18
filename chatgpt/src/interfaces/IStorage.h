#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <vector>

struct FileInfo {
    String name;
    String path;
    size_t size;
    bool isDirectory;
    uint32_t lastModified;
};

enum class StorageMedium : uint8_t {
    LittleFS = 0,
    SDCard = 1,
    PSRAM = 2
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    virtual bool begin() = 0;
    virtual void end() = 0;

    virtual bool exists(const String& path) const = 0;
    virtual bool remove(const String& path) = 0;
    virtual bool rename(const String& from, const String& to) = 0;
    virtual bool mkdir(const String& path) = 0;
    virtual bool rmdir(const String& path) = 0;

    virtual bool list(const String& path, std::vector<FileInfo>& out) = 0;
    virtual size_t totalSpace() const = 0;
    virtual size_t usedSpace() const = 0;
    virtual size_t freeSpace() const = 0;

    virtual String readFile(const String& path) = 0;
    virtual bool writeFile(const String& path, const String& content) = 0;
    virtual bool appendFile(const String& path, const String& content) = 0;

    virtual StorageMedium medium() const = 0;
    virtual bool isReady() const = 0;
};

class ILittleFsManager : public IFileSystem {
public:
    virtual ~ILittleFsManager() = default;
    virtual bool format() = 0;
};

class ISdCardManager : public IFileSystem {
public:
    virtual ~ISdCardManager() = default;

    virtual bool mount(int ssPin = -1, int sckPin = -1, int misoPin = -1, int mosiPin = -1) = 0;
    virtual void unmount() = 0;
    virtual bool isMounted() const = 0;

    virtual bool getCardInfo(uint64_t& totalBytes, uint64_t& usedBytes) = 0;
    virtual String getCardType() const = 0;

    virtual bool startLogging(const String& filePath) = 0;
    virtual bool stopLogging() = 0;

    virtual bool setRotationPolicy(const String& policy) = 0;
    virtual bool rotateNow() = 0;
};

class IStorageManager {
public:
    virtual ~IStorageManager() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    virtual IFileSystem* getPrimary() = 0;
    virtual IFileSystem* getSecondary() = 0;

    virtual bool isWritable() const = 0;
    virtual bool ensurePath(const String& path) = 0;
};
