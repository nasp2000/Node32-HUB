#pragma once

#include "audio_core.h"

// ============================================================================
// AudioSourceBuffer — linear RAM buffer in front of any AudioSource.
//
// Wraps any AudioSource behind a linear RAM buffer: the first read() blocks
// until the whole buffer is filled (prebuffer), subsequent reads are served
// from the buffer, and the tail is topped up opportunistically. Used by the
// runtime for the SD/cache path (a pre-allocated PSRAM buffer is passed in).
// ============================================================================

class AudioSourceBuffer : public AudioSource {
public:
    AudioSourceBuffer(AudioSource* in, uint32_t bufferBytes);
    AudioSourceBuffer(AudioSource* in, void* buffer, uint32_t bufferBytes); // pre-allocated buffer
    virtual ~AudioSourceBuffer() override;

    virtual uint32_t read(void* data, uint32_t len) override;
    virtual bool seek(int32_t pos, int dir) override;
    virtual bool close() override;
    virtual bool isOpen() override;
    virtual uint32_t getSize() override;
    virtual uint32_t getPos() override;
    virtual bool loop() override;

    virtual uint32_t getFillLevel();

private:
    virtual void fill();

private:
    AudioSource* src;
    uint32_t buffSize;
    uint8_t* buffer;
    bool deallocateBuffer;
    uint32_t writePtr;
    uint32_t readPtr;
    uint32_t length;
    bool filled;
};
