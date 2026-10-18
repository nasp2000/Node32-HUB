#pragma once
#include <Arduino.h>
#include "audio_core.h"
#include "icy_demux.h"
#if defined(ESP32)
#include <WiFi.h>
#endif

// AudioSourceHttpRing
// Stream source for radio / UPnP HTTP audio. A background RTOS task keeps a
// deep PSRAM ring buffer filled straight from the socket, so the decode loop
// (running in the main loop) NEVER blocks on the network and short WiFi
// hiccups are absorbed by the cushion instead of stuttering the DAC.
class AudioSourceHttpRing : public AudioSource {
public:
    AudioSourceHttpRing(uint32_t ringBytes);
    AudioSourceHttpRing(const char* url, uint32_t ringBytes);
    virtual ~AudioSourceHttpRing();

    virtual bool open(const char* url) override;
    virtual uint32_t read(void* data, uint32_t len) override;
    virtual uint32_t readNonBlock(void* data, uint32_t len) override;
    virtual bool seek(int32_t pos, int dir) override;
    virtual bool close() override;
    virtual bool isOpen() override;
    virtual uint32_t getSize() override;
    virtual uint32_t getPos() override;
    virtual bool readIcyMetadata(char* buf, size_t max) override;

private:
    WiFiClient _client;
    String _host;
    String _path;
    String _url;
    uint16_t _port;
    uint8_t* _ring;
    uint32_t _cap;
    uint32_t _mask;             // _cap - 1 (capacity is a power of two)
    volatile uint32_t _rd;      // consumer index (read)
    volatile uint32_t _wr;      // producer index (write)
    volatile bool _abort;
    volatile bool _connected;
    volatile bool _eofStream;
    TaskHandle_t _task;
    uint32_t _bytesConsumed;    // read by the decoder (getPos)
    uint64_t _streamed;         // total bytes produced from the socket (EOF for Content-Length)
    int32_t _contentLen;        // -1 = unknown / endless stream
    bool _chunked;              // Transfer-Encoding: chunked
    uint8_t _chState;           // 0 = size line, 1 = chunk data, 2 = CRLF
    uint32_t _chunkRemain;
    uint8_t _lineBuf[64];
    uint8_t _linePos;

    // ICY metadata (Shoutcast/Icecast StreamTitle). Requested with
    // Icy-MetaData:1; metadata blocks are stripped in the fill task and the
    // latest title is published to the runtime via readIcyMetadata().
    icy_demux::Demux _icyDemux;
    int32_t _icyMetaint;        // 0 = stream has no ICY metadata
    char _icyTitle[256];
    volatile bool _icyNew;      // a new title is waiting to be consumed

    bool connectSocket();
    void startTask();
    void stopTask();
    int fillFromSocket();
    static void taskFn(void* arg);
};
