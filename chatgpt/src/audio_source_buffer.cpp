#include "audio_source_buffer.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

AudioSourceBuffer::AudioSourceBuffer(AudioSource* in, uint32_t bufferBytes)
    : src(in)
    , buffSize(bufferBytes)
    , buffer(nullptr)
    , deallocateBuffer(true)
    , writePtr(0)
    , readPtr(0)
    , length(0)
    , filled(false) {
    if (buffSize) buffer = (uint8_t*)malloc(buffSize);
}

AudioSourceBuffer::AudioSourceBuffer(AudioSource* in, void* buf, uint32_t bufferBytes)
    : src(in)
    , buffSize(bufferBytes)
    , buffer((uint8_t*)buf)
    , deallocateBuffer(false)
    , writePtr(0)
    , readPtr(0)
    , length(0)
    , filled(false) {
}

AudioSourceBuffer::~AudioSourceBuffer() {
    if (deallocateBuffer && buffer) free(buffer);
    buffer = nullptr;
}

bool AudioSourceBuffer::seek(int32_t pos, int dir) {
    if (dir == SEEK_CUR && (int32_t)(readPtr + pos) >= 0 && readPtr + (uint32_t)pos < length) {
        readPtr += pos;
        return true;
    }
    readPtr = 0;
    writePtr = 0;
    length = 0;
    filled = false;
    return src->seek(pos, dir);
}

bool AudioSourceBuffer::close() {
    if (deallocateBuffer && buffer) free(buffer);
    buffer = nullptr;
    return src->close();
}

bool AudioSourceBuffer::isOpen() {
    return src->isOpen();
}

uint32_t AudioSourceBuffer::getSize() {
    return src->getSize();
}

uint32_t AudioSourceBuffer::getPos() {
    return src->getPos();
}

uint32_t AudioSourceBuffer::getFillLevel() {
    return length;
}

uint32_t AudioSourceBuffer::read(void* data, uint32_t len) {
    if (!buffer) return src->read(data, len);

    uint32_t bytes = 0;
    if (!filled) {
        // Prebuffer: block until the whole buffer is filled (or EOF).
        length = src->read(buffer, buffSize);
        writePtr = length % buffSize;
        filled = true;
    }

    // Serve from the buffer first.
    uint8_t* ptr = (uint8_t*)data;
    uint32_t toReadFromBuffer = (len < length) ? len : length;
    if (toReadFromBuffer > 0 && readPtr >= writePtr) {
        uint32_t toReadToEnd = (toReadFromBuffer < (uint32_t)(buffSize - readPtr)) ? toReadFromBuffer : (buffSize - readPtr);
        memcpy(ptr, &buffer[readPtr], toReadToEnd);
        readPtr = (readPtr + toReadToEnd) % buffSize;
        len -= toReadToEnd;
        length -= toReadToEnd;
        ptr += toReadToEnd;
        bytes += toReadToEnd;
        toReadFromBuffer -= toReadToEnd;
    }
    if (toReadFromBuffer > 0) {
        memcpy(ptr, &buffer[readPtr], toReadFromBuffer);
        readPtr = (readPtr + toReadFromBuffer) % buffSize;
        len -= toReadFromBuffer;
        length -= toReadFromBuffer;
        ptr += toReadFromBuffer;
        bytes += toReadFromBuffer;
        toReadFromBuffer -= toReadFromBuffer;
    }

    if (len) {
        // Need more than buffered: read directly from the source, then force a
        // complete refill on the next call.
        bytes += src->read(ptr, len);
        readPtr = 0;
        writePtr = 0;
        length = 0;
        filled = false;
    }

    fill();
    return bytes;
}

void AudioSourceBuffer::fill() {
    if (!buffer) return;
    if (length >= buffSize) return;

    // Opportunistically top up the free region(s) with non-blocking reads.
    if (readPtr > writePtr) {
        if (readPtr == writePtr + 1) return;
        uint32_t bytesAvailMid = readPtr - writePtr - 1;
        int cnt = (int)src->readNonBlock(&buffer[writePtr], bytesAvailMid);
        length += (uint32_t)cnt;
        writePtr = (writePtr + (uint32_t)cnt) % buffSize;
        return;
    }

    if (buffSize > writePtr) {
        uint32_t bytesAvailEnd = buffSize - writePtr;
        int cnt = (int)src->readNonBlock(&buffer[writePtr], bytesAvailEnd);
        length += (uint32_t)cnt;
        writePtr = (writePtr + (uint32_t)cnt) % buffSize;
        if (cnt != (int)bytesAvailEnd) return;
    }

    if (readPtr > 1) {
        uint32_t bytesAvailStart = readPtr - 1;
        int cnt = (int)src->readNonBlock(&buffer[writePtr], bytesAvailStart);
        length += (uint32_t)cnt;
        writePtr = (writePtr + (uint32_t)cnt) % buffSize;
    }
}

bool AudioSourceBuffer::loop() {
    if (!src->loop()) return false;
    fill();
    return true;
}
