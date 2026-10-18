#pragma once
#include <Arduino.h>
#include "audio_core.h"
#include "audio_ring.h"
#if defined(ESP32)
#include <SD_MMC.h>
#endif

// ============================================================================
// AudioSourceSdRing — SD-card source with a PSRAM ring fed by a background
// task, mirroring the HTTP ring used for radio streams.
//
// The decode loop NEVER blocks on the SD card: the first chunk is preloaded
// synchronously at construction (so the decoder's begin()/first reads are
// instant — the wall/exp ratio stays ~1.0 from the very first report), then a
// background task keeps the ring topped up straight from the file. Reads
// therefore stay in PSRAM and are immune to SD mutex contention from web
// handlers (library scans, album art), which used to stall AudioSourceSdMmc
// mid-track.
// ============================================================================

class AudioSourceSdRing : public AudioSource {
public:
    AudioSourceSdRing(const char* path, uint32_t ringBytes);
    virtual ~AudioSourceSdRing() override;

    virtual uint32_t read(void* data, uint32_t len) override;
    virtual uint32_t readNonBlock(void* data, uint32_t len) override;
    virtual bool seek(int32_t pos, int dir) override;
    virtual bool close() override;
    virtual bool isOpen() override;
    virtual uint32_t getSize() override;
    virtual uint32_t getPos() override;

private:
    void startTask();
    void stopTask();
    void preloadSync();

    String _path;
    uint32_t _ringBytes;
    RingBuffer _ring;
    bool _ringOwned;             // we allocated _ring.raw() and must free it
    File _mFile;
    bool _fileOpen;
    uint32_t _size;
    volatile bool _abort;
    volatile bool _fileDone;     // whole file has been read into the ring
    volatile uint32_t _consumed; // bytes served to the decoder (position)
    TaskHandle_t _task;

    static void taskFn(void* arg);
    static int sdRead(void* ctx, uint8_t* buf, uint32_t len);
};
