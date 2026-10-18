#include "audio_source_sd_ring.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "psram_helpers.h"   // psramFound / heap_caps_malloc
#include "sdcard.h"          // sdMutexLockFast / sdMutexUnlockFast

namespace {

// How much of the file is pulled into the ring synchronously at construction
// (and after a seek) so the decoder's begin()/first reads never stall on SD.
const uint32_t kSdPreloadBytes = 256UL * 1024UL;

// Max bytes read from the SD card per mutex acquisition. Keeps each mutex
// hold short so web handlers (library scans, album art) are not starved.
const uint32_t kSdFillChunk = 16UL * 1024UL;

} // namespace

AudioSourceSdRing::AudioSourceSdRing(const char* path, uint32_t ringBytes)
    : _path(path), _ringBytes(ringBytes), _ringOwned(false), _fileOpen(false),
      _size(0), _abort(false), _fileDone(false), _consumed(0), _task(nullptr) {
    // Round the ring up to a power of two (wrap arithmetic is a mask).
    uint32_t cap = 1;
    while (cap < _ringBytes) cap <<= 1;
    _ringBytes = cap;
    if (psramFound()) {
        _ring.init((uint8_t*)heap_caps_malloc(_ringBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), _ringBytes);
    }
    if (!_ring.raw()) {
        _ring.init((uint8_t*)heap_caps_malloc(_ringBytes, MALLOC_CAP_8BIT), _ringBytes);
    }
    if (!_ring.raw()) return;   // out of memory — isOpen() stays false
    _ringOwned = true;

    if (sdMutexLockFast(pdMS_TO_TICKS(4000))) {
        _mFile = SD_MMC.open(path, FILE_READ);
        sdMutexUnlockFast();
    }
    if (!_mFile) return;        // open failed — isOpen() stays false
    _fileOpen = true;
    _size = _mFile.size();

    preloadSync();              // fill the first chunk before any decoder read
    startTask();
}

AudioSourceSdRing::~AudioSourceSdRing() {
    close();
}

int AudioSourceSdRing::sdRead(void* ctx, uint8_t* buf, uint32_t len) {
    AudioSourceSdRing* s = (AudioSourceSdRing*)ctx;
    if (!s->_fileOpen || s->_abort) return -1;
    if (!sdMutexLockFast(pdMS_TO_TICKS(8000))) return 0;   // busy — retry later
    int n = s->_mFile.read(buf, len);
    sdMutexUnlockFast();
    if (n <= 0) return -1;      // real end of file
    return n;
}

void AudioSourceSdRing::preloadSync() {
    if (!_fileOpen || _fileDone) return;
    int eof = 0;
    ringFillFrom(_ring, &AudioSourceSdRing::sdRead, this, kSdFillChunk, kSdPreloadBytes, &eof);
    if (eof) _fileDone = true;
}

uint32_t AudioSourceSdRing::read(void* data, uint32_t len) {
    if (!_ring.raw()) return 0;
    if (_ring.used() == 0) {
        if (_fileDone) return 0;                    // file fully drained
        unsigned long t0 = millis();
        while (_ring.used() == 0 && !_abort && !_fileDone && (millis() - t0) < 250UL) {
            delay(1);
        }
        if (_ring.used() == 0) return 0;
    }
    uint32_t n = _ring.read((uint8_t*)data, len);
    _consumed += n;
    return n;
}

uint32_t AudioSourceSdRing::readNonBlock(void* data, uint32_t len) {
    if (!_ring.raw()) return 0;
    uint32_t n = _ring.read((uint8_t*)data, len);
    _consumed += n;
    return n;
}

bool AudioSourceSdRing::seek(int32_t pos, int dir) {
    if (!_fileOpen) return false;
    // Stop the fill task first: it must never commit bytes into the ring while
    // we reset it and reposition the file (a concurrent commit would splice
    // audio from the wrong offset).
    stopTask();
    if (!sdMutexLockFast(pdMS_TO_TICKS(8000))) {
        startTask();
        return false;
    }
    int32_t cur = (int32_t)_mFile.position();
    int32_t target = (dir == SEEK_SET) ? pos
                   : (dir == SEEK_CUR) ? cur + pos
                                       : (int32_t)_size + pos;
    bool ok = (target >= 0 && target <= (int32_t)_size);
    if (ok) ok = _mFile.seek((uint32_t)target);
    sdMutexUnlockFast();
    if (!ok) {
        startTask();
        return false;
    }

    _ring.reset();
    _consumed = (uint32_t)target;
    _fileDone = false;
    preloadSync();
    startTask();
    return true;
}

bool AudioSourceSdRing::close() {
    stopTask();
    if (_fileOpen) {
        if (sdMutexLockFast(pdMS_TO_TICKS(4000))) {
            _mFile.close();
            sdMutexUnlockFast();
        }
        _fileOpen = false;
    }
    if (_ringOwned && _ring.raw()) {
        heap_caps_free(_ring.raw());
        _ringOwned = false;
        _ring.init(nullptr, 0);
    }
    _abort = true;
    return true;
}

bool AudioSourceSdRing::isOpen() {
    // Open while the file still produces bytes OR the ring still holds them
    // (the decoder only consults isOpen() after a zero read, so the trailing
    // buffered audio always finishes cleanly).
    if (!_fileOpen || _abort) return false;
    return !_fileDone || _ring.used() > 0;
}

uint32_t AudioSourceSdRing::getSize() {
    return _size;
}

uint32_t AudioSourceSdRing::getPos() {
    return _consumed;
}

void AudioSourceSdRing::startTask() {
    _abort = false;
    xTaskCreatePinnedToCore(taskFn, "sdRing", 4096, this, 3, &_task, 1);
}

void AudioSourceSdRing::stopTask() {
    _abort = true;
    if (_task) {
        for (int i = 0; i < 500 && _task; i++) delay(2);   // up to 1s
        if (_task) {
            vTaskDelete(_task);
            _task = nullptr;
        }
    }
}

void AudioSourceSdRing::taskFn(void* arg) {
    AudioSourceSdRing* s = (AudioSourceSdRing*)arg;
    while (true) {
        if (s->_abort) break;
        if (s->_fileDone || !s->_fileOpen) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        int eof = 0;
        uint32_t filled = ringFillFrom(s->_ring, &AudioSourceSdRing::sdRead, s,
                                       kSdFillChunk, 0, &eof);
        if (eof) s->_fileDone = true;
        if (filled == 0) vTaskDelay(pdMS_TO_TICKS(2));   // ring full or SD busy
    }
    s->_task = nullptr;
    vTaskDelete(nullptr);
}

#endif // ENABLE_UDA1334A == 1
#endif // SELECTED_PACK == PACK_RADIO
