#include "audio_decoder_wav.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "wav_parser.h"
#include "psram_helpers.h"  // heap_caps_malloc
#include "commands.h"       // sendVirtualMessage + LogLevel

// One read chunk = 4096 bytes of interleaved PCM (~23 ms @44.1k stereo 16-bit).
static const uint32_t kWavChunkBytes = 4096;

WavDecoder::WavDecoder()
    : AudioDecoder()
    , _chunkBuf(nullptr)
    , _chunkLen(0)
    , _chunkPos(0)
    , _dataLeft(0)
    , _sampleRate(0)
    , _channels(2)
    , _bitsPerSample(16)
    , _eof(false)
    , _lastRate(0)
    , _wavValid(false)
{
}

WavDecoder::~WavDecoder() {
    stop();
}

bool WavDecoder::begin(AudioSource* source, AudioOutput* output) {
    if (!source || !output) return false;

    file = source;
    this->output = output;

    if (!_chunkBuf) {
        if (psramFound())
            _chunkBuf = (uint8_t*)heap_caps_malloc(kWavChunkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_chunkBuf)
            _chunkBuf = (uint8_t*)heap_caps_malloc(kWavChunkBytes, MALLOC_CAP_8BIT);
        if (!_chunkBuf) return false;
    }

    // Read the first chunk — it carries the RIFF header plus initial PCM.
    _chunkLen = source->read(_chunkBuf, kWavChunkBytes);
    if (_chunkLen < 44) {
        sendVirtualMessage(F("[WAV] stream too short for a WAV header"), WARN, false, true);
        return false;
    }

    wav::Format fmt = wav::parse(_chunkBuf, _chunkLen);
    if (!fmt.valid) {
        sendVirtualMessage(F("[WAV] invalid RIFF/WAVE header"), WARN, false, true);
        return false;
    }
    if (fmt.bitsPerSample != 16) {
        char dbg[80];
        snprintf(dbg, sizeof(dbg), "[WAV] %u-bit not supported (16-bit only)", fmt.bitsPerSample);
        sendVirtualMessage(String(dbg), WARN, false, true);
        return false;
    }
    if (fmt.channels > 2) {
        sendVirtualMessage(F("[WAV] >2 channels not supported"), WARN, false, true);
        return false;
    }

    _sampleRate = fmt.sampleRate;
    _channels = (uint8_t)fmt.channels;
    _bitsPerSample = 16;
    _dataLeft = fmt.dataSize;
    _wavValid = true;
    _eof = false;

    // Skip the 44+ bytes consumed by the header.
    _chunkPos = fmt.dataOffset;
    _dataLeft -= (fmt.dataOffset - 44); // header bytes already inside the chunk

    // Configure the output before begin() so the I2S driver knows the format.
    output->SetRate((int)_sampleRate);
    output->SetBitsPerSample(16);
    output->SetChannels(_channels);
    if (!output->begin()) return false;

    _lastRate = (int)_sampleRate;
    running = true;
    lastSample[0] = 0;
    lastSample[1] = 0;

    char dbg[96];
    snprintf(dbg, sizeof(dbg), "[WAV] opened: %u Hz, %u ch, %u data bytes",
             (unsigned)_sampleRate, (unsigned)_channels, (unsigned)_dataLeft);
    sendVirtualMessage(String(dbg), INFO, false, true);
    return true;
}

bool WavDecoder::loop() {
    if (!running || !file || !output || !_chunkBuf) {
        running = false;
        return false;
    }

    // ---- 1) Feed pending PCM with backpressure ---------------------------
    while (_chunkPos + 4 <= _chunkLen) {
        int16_t l, r;
        if (_channels == 1) {
            l = r = (int16_t)((_chunkBuf[_chunkPos]) | (_chunkBuf[_chunkPos + 1] << 8));
            _chunkPos += 2;
        } else {
            l = (int16_t)((_chunkBuf[_chunkPos]) | (_chunkBuf[_chunkPos + 1] << 8));
            r = (int16_t)((_chunkBuf[_chunkPos + 2]) | (_chunkBuf[_chunkPos + 3] << 8));
            _chunkPos += 4;
        }
        int16_t s[2] = { l, r };
        if (!output->ConsumeSample(s)) {
            return true; // DMA full — yield, resume here next call
        }
        lastSample[0] = s[0];
        lastSample[1] = s[1];
    }

    // ---- 2) Refill the next chunk -----------------------------------------
    if (_dataLeft > 0) {
        uint32_t want = kWavChunkBytes;
        if (want > _dataLeft) want = _dataLeft;
        uint32_t got = file->read(_chunkBuf, want);
        _chunkLen = got;
        _chunkPos = 0;
        if (got > 0) _dataLeft -= got;
        if (got == 0) _dataLeft = 0;
        return true;
    }

    // ---- 3) End of data ----------------------------------------------------
    _eof = true;
    running = false;
    return false;
}

bool WavDecoder::isRunning() {
    return running;
}

bool WavDecoder::stop() {
    running = false;
    if (_chunkBuf) {
        heap_caps_free(_chunkBuf);
        _chunkBuf = nullptr;
    }
    _chunkLen = 0;
    _chunkPos = 0;
    _dataLeft = 0;
    _eof = false;
    _wavValid = false;
    return true;
}

void WavDecoder::desync() {
    _chunkLen = 0;
    _chunkPos = 0;
    _dataLeft = 0;
    _eof = false;
    _wavValid = false;
    running = false;
}

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A == 1
