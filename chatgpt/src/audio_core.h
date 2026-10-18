#pragma once

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// audio_core.h — first-party audio stack base classes.
//
// Header-only equivalents for the byte-stream, sample-out and decode-loop
// contracts that the runtime and the decoders rely on:
//
//   AudioSource   — byte-stream input (HTTP ring, SD file)
//   AudioOutput   — 16-bit stereo PCM sink (EqAudioOutput subclasses it)
//   AudioDecoder  — decode loop contract (begin/loop/stop/isRunning/desync)
//
// No metadata/status callback plumbing (AudioStatus) is provided because the
// Node32-HUB runtime never used it — only the byte-stream, sample-out and
// decode-loop contracts below.
// ============================================================================

// Byte-stream input (HTTP ring, SD file).
class AudioSource {
public:
    AudioSource() {}
    virtual ~AudioSource() {}
    virtual bool open(const char* filename) { (void)filename; return false; }
    virtual uint32_t read(void* data, uint32_t len) { (void)data; (void)len; return 0; }
    virtual uint32_t readNonBlock(void* data, uint32_t len) { return read(data, len); }
    virtual bool seek(int32_t pos, int dir) { (void)pos; (void)dir; return false; }
    virtual bool close() { return false; }
    virtual bool isOpen() { return false; }
    virtual uint32_t getSize() { return 0; }
    virtual uint32_t getPos() { return 0; }
    virtual bool loop() { return true; }
    // Consume the latest ICY StreamTitle parsed from this source (if any).
    // Returns true when a new title was available and copied into buf.
    // Sources without ICY metadata always return false.
    virtual bool readIcyMetadata(char* buf, size_t max) { (void)buf; (void)max; return false; }
};

// 16-bit stereo PCM sink. EqAudioOutput subclasses it and relies on the
// protected helpers/fields below.
class AudioOutput {
public:
    AudioOutput() : hertz(44100), bps(16), channels(2), gainF2P6(0) {}
    virtual ~AudioOutput() {}
    virtual bool SetRate(int hz) { hertz = hz; return true; }
    virtual bool SetBitsPerSample(int bits) { bps = bits; return true; }
    virtual bool SetChannels(int chan) { channels = chan; return true; }
    virtual bool SetGain(float f) {
        if (f > 4.0f) f = 4.0f;
        if (f < 0.0f) f = 0.0f;
        gainF2P6 = (uint8_t)(f * (1 << 6)); // fixed point 2.6
        return true;
    }
    virtual bool begin() { return false; }
    typedef enum { LEFTCHANNEL = 0, RIGHTCHANNEL = 1 } SampleIndex;
    virtual bool ConsumeSample(int16_t sample[2]) { (void)sample; return false; }
    virtual uint16_t ConsumeSamples(int16_t* samples, uint16_t count) {
        for (uint16_t i = 0; i < count; i++) {
            if (!ConsumeSample(samples)) return i;
            samples += 2;
        }
        return count;
    }
    virtual bool stop() { return false; }
    virtual void flush() {}
    virtual bool loop() { return true; }

protected:
    // Mono -> stereo (duplicate channel) and 8-bit unsigned -> 16-bit signed.
    void MakeSampleStereo16(int16_t sample[2]) {
        if (channels == 1)
            sample[RIGHTCHANNEL] = sample[LEFTCHANNEL];
        if (bps == 8) {
            sample[LEFTCHANNEL] = (((int16_t)(sample[LEFTCHANNEL] & 0xff)) - 128) << 8;
            sample[RIGHTCHANNEL] = (((int16_t)(sample[RIGHTCHANNEL] & 0xff)) - 128) << 8;
        }
    }

    // Apply the fixed-point gain with clamping (saturating at 16-bit bounds).
    inline int16_t Amplify(int16_t s) {
        int32_t v = ((int32_t)s * gainF2P6) >> 6;
        if (v < -32767) return -32767;
        else if (v > 32767) return 32767;
        else return (int16_t)v;
    }

protected:
    uint16_t hertz;
    uint8_t bps;
    uint8_t channels;
    uint8_t gainF2P6;
};

// Decode loop contract; decoders expose begin/loop/stop/isRunning/desync and
// drive the output through AudioOutput.
class AudioDecoder {
public:
    AudioDecoder() {
        running = false;
        file = nullptr;
        output = nullptr;
        lastSample[0] = 0;
        lastSample[1] = 0;
    }
    virtual ~AudioDecoder() {}
    virtual bool begin(AudioSource* source, AudioOutput* output) {
        (void)source; (void)output; return false;
    }
    virtual bool loop() { return false; }
    virtual bool stop() { return false; }
    virtual bool isRunning() { return running; }
    virtual void desync() {}

protected:
    bool running;
    AudioSource* file;
    AudioOutput* output;
    int16_t lastSample[2];
};
