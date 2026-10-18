#include "audio_source_http_ring.h"

#include "feature_flags.h"
#if ENABLE_UDA1334A == 1
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RADIO)

#include "psram_helpers.h"   // psramFound / heap_caps_malloc

#include "icy_demux.h"

AudioSourceHttpRing::AudioSourceHttpRing(uint32_t ringBytes)
    : _ring(nullptr), _cap(ringBytes), _mask(0), _rd(0), _wr(0),
      _abort(false), _connected(false), _eofStream(false), _task(nullptr),
      _bytesConsumed(0), _streamed(0), _contentLen(-1), _chunked(false),
      _chState(0), _chunkRemain(0), _linePos(0),
      _icyMetaint(0), _icyNew(false) {
    _icyTitle[0] = '\0';
}

AudioSourceHttpRing::AudioSourceHttpRing(const char* url, uint32_t ringBytes)
    : AudioSourceHttpRing(ringBytes) {
    open(url);
}

AudioSourceHttpRing::~AudioSourceHttpRing() {
    close();
}

void AudioSourceHttpRing::stopTask() {
    _abort = true;
    _client.stop();
    // Wait for the fill task to actually exit before the ring is freed. The
    // task self-clears _task immediately before vTaskDelete, so this blocks
    // until it is safe to touch the buffers/socket again (generous cap as a
    // safety net; _client.stop() unblocks any in-flight connect/read).
    for (int i = 0; i < 500 && _task; i++) delay(2);   // up to 1s
    if (_task) {                       // shouldn't happen — force delete
        vTaskDelete(_task);
        _task = nullptr;
    }
    _connected = false;
}

bool AudioSourceHttpRing::open(const char* url) {
    if (!url || !url[0]) return false;
    if (!_ring) {
        if (_cap == 0) _cap = 1024UL * 1024UL;
        // Keep the ring capacity a power of two so wrap arithmetic is a mask.
        uint32_t c = 1;
        while (c < _cap) c <<= 1;
        _cap = c;
        _mask = _cap - 1;
        if (psramFound()) {
            _ring = (uint8_t*)heap_caps_malloc(_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!_ring) {
            _ring = (uint8_t*)heap_caps_malloc(_cap, MALLOC_CAP_8BIT);
        }
        if (!_ring) return false;
    }

    // Parse scheme/host/port/path. HTTPS is downgraded to plain HTTP: radio
    // servers serve both and the REST of the pipeline never spoke TLS anyway
    // (the previous source used a plain WiFiClient under HTTPClient too).
    String u = url;
    if (u.startsWith("https://")) { u = u.substring(8); _port = 80; }
    else if (u.startsWith("http://")) { u = u.substring(7); _port = 80; }
    else return false;
    int slash = u.indexOf('/');
    String host = (slash >= 0) ? u.substring(0, slash) : u;
    String path = (slash >= 0) ? u.substring(slash) : "/";
    int colon = host.indexOf(':');
    if (colon >= 0) {
        _port = (uint16_t)host.substring(colon + 1).toInt();
        host = host.substring(0, colon);
    }
    _host = host;
    _path = path;
    _url = url;

    _rd = 0; _wr = 0;
    _bytesConsumed = 0;
    _eofStream = false;
    _abort = false;

    bool ok = connectSocket();      // first connect is synchronous (startup)
    startTask();                    // from here on the task owns the socket
    return ok;
}

bool AudioSourceHttpRing::connectSocket() {
    if (_connected && _client.connected()) return true;

    _client.stop();
    _client.setTimeout(1500);
    if (!_client.connect(_host.c_str(), _port)) {
        _connected = false;
        return false;
    }

    _client.print("GET " + _path + " HTTP/1.1\r\nHost: " + _host +
                  "\r\nIcy-MetaData:1\r\nConnection:keep-alive\r\n\r\n");

    _chunked = false;
    _contentLen = -1;
    _icyMetaint = 0;
    unsigned long t0 = millis();
    bool sawHeader = false;
    while (millis() - t0 < 8000 && _client.connected()) {
        if (_client.available() <= 0) { delay(1); continue; }
        String line = _client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) { sawHeader = true; break; } // blank line = end of headers

        String key = line;
        String val;
        int i = line.indexOf(':');
        if (i > 0) {
            key = line.substring(0, i);
            val = line.substring(i + 1);
            key.toLowerCase();
            val.trim();
            val.toLowerCase();
            if (key.indexOf("transfer-encoding") >= 0 && val.indexOf("chunked") >= 0) {
                _chunked = true;
                _contentLen = -1;
            } else if (key.indexOf("content-length") >= 0) {
                _contentLen = val.toInt();
            } else if (key.indexOf("icy-metaint") >= 0) {
                _icyMetaint = val.toInt();
            }
        }
    }
    if (!sawHeader && !_client.connected()) {
        _connected = false;
        return false;
    }

    _icyDemux.reset((uint32_t)((_icyMetaint > 0) ? _icyMetaint : 0));
    _icyNew = false;             // a reconnect repeats the stream from its start

    _client.setTimeout(0);          // all reads non-blocking from here on
    _chState = 0;
    _chunkRemain = 0;
    _linePos = 0;
    _eofStream = false;
    _connected = true;
    return true;
}

void AudioSourceHttpRing::startTask() {
    _abort = false;
    xTaskCreatePinnedToCore(taskFn, "httpRing", 4096, this, 3, &_task, 1);
}

uint32_t AudioSourceHttpRing::readNonBlock(void* data, uint32_t len) {
    if (!_ring || (_rd == _wr)) return 0;
    return read(data, len);
}

// read() only returns 0 when the stream is genuinely finished (finite file
// consumed or the fill task gave up after a long outage) or aborted. During a
// transient empty window (task reconnecting) it waits in small slices so the
// decode loop never mistakes a hiccup for end-of-stream. readNonBlock never
// blocks (the runtime's AudioSourceBuffer calls it to top up).
uint32_t AudioSourceHttpRing::read(void* data, uint32_t len) {
    if (!_ring) return 0;
    if (_rd == _wr) {
        uint32_t waitMs = (_bytesConsumed == 0) ? 10000UL : 250UL;
        unsigned long t0 = millis();
        while (_rd == _wr && !_abort && !_eofStream &&
               (millis() - t0) < waitMs) {
            delay(2);
        }
        if (_rd == _wr) return 0;
    }

    uint32_t avail = (_wr - _rd) & _mask;
    if (!avail) return 0;
    if (len > avail) len = avail;

    uint8_t* dst = (uint8_t*)data;
    uint32_t n = 0;
    while (len) {
        uint32_t idx = _rd & _mask;
        uint32_t span = _cap - idx;
        if (span > len) span = len;
        memcpy(dst, _ring + idx, span);
        _rd += span;
        dst += span;
        len -= span;
        n += span;
    }
    _bytesConsumed += n;
    return n;
}

bool AudioSourceHttpRing::seek(int32_t pos, int dir) {
    (void)pos; (void)dir;
    return false;   // live streams cannot seek
}

bool AudioSourceHttpRing::close() {
    stopTask();
    if (_ring) {
        heap_caps_free(_ring);
        _ring = nullptr;
    }
    _eofStream = true;
    return true;
}

bool AudioSourceHttpRing::isOpen() {
    // "Open" means the stream can still produce data. It stays true during a
    // transient disconnect while the fill task reconnects (with the cushion
    // still full the decoder never even reads), and only goes false when the
    // stream genuinely ended (finite file) or the task gave up after a long
    // outage and the runtime reconnect takes over.
    return _ring != nullptr && !_abort && !_eofStream;
}

uint32_t AudioSourceHttpRing::getSize() {
    return (_contentLen > 0) ? (uint32_t)_contentLen : 0;
}

uint32_t AudioSourceHttpRing::getPos() {
    return _bytesConsumed;
}

bool AudioSourceHttpRing::readIcyMetadata(char* buf, size_t max) {
    if (!_icyNew) return false;
    _icyNew = false;
    if (!buf || max == 0) return true;
    size_t n = strlen(_icyTitle);
    if (n >= max) n = max - 1;
    memcpy(buf, _icyTitle, n);
    buf[n] = '\0';
    return true;
}

int AudioSourceHttpRing::fillFromSocket() {
    if (_abort || !_connected || !_client.connected()) return 0;
    if (_eofStream) return 0;
    if (_client.available() <= 0) return 0;

    uint32_t freeSpace = (_rd - _wr - 1) & _mask;
    if (freeSpace == 0) return 0;

    int placed = 0;

    if (!_chunked) {
        if (_icyMetaint > 0) {
            // ICY stream: interleaved [metadata block][metaint audio] ... The
            // demux strips the metadata and hands us only audio bytes; a fresh
            // StreamTitle is parsed from each block and published to the
            // runtime via readIcyMetadata(). Chunked + ICY does not occur in
            // practice (Icecast/Shoutcast always send a non-chunked endless
            // body), so this path only needs to handle the plain body.
            uint8_t ibuf[512];
            uint8_t obuf[512];
            while (freeSpace > 0 && _client.available() > 0 && !_abort && !_eofStream) {
                int got = _client.read(ibuf, sizeof(ibuf));
                if (got <= 0) break;
                bool metaSeen = false;
                char title[256];
                uint32_t a = _icyDemux.feed(ibuf, (size_t)got, obuf, sizeof(obuf),
                                            title, sizeof(title), &metaSeen);
                if (metaSeen && title[0]) {
                    memcpy(_icyTitle, title, sizeof(_icyTitle));
                    _icyTitle[sizeof(_icyTitle) - 1] = '\0';
                    _icyNew = true;
                }
                uint32_t remain = a;
                uint32_t oi = 0;
                while (remain > 0 && freeSpace > 0) {
                    uint32_t idx = _wr & _mask;
                    uint32_t span = _cap - idx;
                    if (span > remain) span = remain;
                    memcpy(_ring + idx, obuf + oi, span);
                    _wr += span;
                    freeSpace -= span;
                    oi += span;
                    remain -= span;
                }
                placed += (int)(a - remain);
                if (remain > 0) break; // ring full — leave the rest for next fill
            }
            return placed;
        }

        while (freeSpace > 0 && _client.available() > 0 && !_abort && !_eofStream) {
            if (_contentLen >= 0 && _streamed >= (uint64_t)_contentLen) {
                _eofStream = true;
                break;
            }
            uint32_t idx = _wr & _mask;
            uint32_t span = _cap - idx;
            if (span > freeSpace) span = freeSpace;
            if (_contentLen >= 0) {
                uint64_t left = (uint64_t)_contentLen - _streamed;
                if ((uint64_t)span > left) span = (uint32_t)left;
            }
            int got = _client.read(_ring + idx, span);
            if (got <= 0) break;
            _wr += (uint32_t)got;
            freeSpace -= (uint32_t)got;
            _streamed += (uint32_t)got;
            placed += got;
        }
        return placed;
    }

    // Chunked body
    while (freeSpace > 0 && _client.available() > 0 && !_abort && !_eofStream) {
        if (_chState == 0) {
            int b = _client.read();
            if (b < 0) break;
            if (b == '\n') {
                uint32_t sz = 0;
                for (uint8_t k = 0; k < _linePos; k++) {
                    char c = (char)_lineBuf[k];
                    if (c == ';' || c == '\r') continue;
                    uint8_t d;
                    if (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
                    else if (c >= 'a' && c <= 'f') d = (uint8_t)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') d = (uint8_t)(c - 'A' + 10);
                    else continue;
                    sz = (sz << 4) | d;
                }
                _linePos = 0;
                if (sz == 0) { _eofStream = true; break; }
                _chunkRemain = sz;
                _chState = 1;
            } else if (_linePos < sizeof(_lineBuf) - 1) {
                _lineBuf[_linePos++] = (uint8_t)b;
            }
        } else if (_chState == 1) {
            uint32_t n = (_chunkRemain < freeSpace) ? _chunkRemain : freeSpace;
            if (n == 0) { _chState = 2; continue; }
            uint32_t idx = _wr & _mask;
            uint32_t span = _cap - idx;
            if (span > n) span = n;
            int got = _client.read(_ring + idx, span);
            if (got <= 0) break;
            _wr += (uint32_t)got;
            _chunkRemain -= (uint32_t)got;
            freeSpace -= (uint32_t)got;
            placed += got;
            if (_chunkRemain == 0) _chState = 2;
        } else { // consume trailing CRLF of a chunk
            int b = _client.read();
            if (b < 0) break;
            if (b == '\n') _chState = 0;
        }
    }
    return placed;
}

void AudioSourceHttpRing::taskFn(void* arg) {
    AudioSourceHttpRing* s = (AudioSourceHttpRing*)arg;
    // If we cannot restore the link after this long, give up so the runtime's
    // reconnect machinery (backoff + user log) takes over. Short budget when
    // playback never started (nothing consumed yet); long budget once the
    // cushion has been feeding the decoder for a while.
    static const uint32_t kGiveUpMsStarted = 120000UL;
    static const uint32_t kGiveUpMsFresh = 10000UL;
    uint32_t gaveUpConnectedAt = 0;   // when an empty, disconnected stretch began
    while (true) {
        if (s->_abort) break;
        if (!s->_connected || !s->_client.connected()) {
            s->_connected = false;
            if (s->_eofStream) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
            if (s->_bytesConsumed > 0 && s->_rd == s->_wr) {
                if (gaveUpConnectedAt == 0) gaveUpConnectedAt = millis();
                if ((uint32_t)(millis() - gaveUpConnectedAt) >= kGiveUpMsStarted) {
                    s->_eofStream = true;   // let the runtime reconnect own it
                    continue;
                }
            } else if (s->_bytesConsumed == 0 && s->_rd == s->_wr) {
                // Playback never got off the ground — don't hang "started" in
                // silence forever; surface the failure for the runtime to retry.
                if (gaveUpConnectedAt == 0) gaveUpConnectedAt = millis();
                if ((uint32_t)(millis() - gaveUpConnectedAt) >= kGiveUpMsFresh) {
                    s->_eofStream = true;
                    continue;
                }
            } else {
                gaveUpConnectedAt = 0;
            }
            if (s->connectSocket()) { gaveUpConnectedAt = 0; continue; }
            vTaskDelay(pdMS_TO_TICKS(1500));    // backoff before retrying
            continue;
        }
        if (s->_rd != s->_wr) gaveUpConnectedAt = 0;  // still draining cushion
        int got = s->fillFromSocket();
        if (got > 0) { gaveUpConnectedAt = 0; continue; }  // keep draining the socket
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    s->_connected = false;
    s->_client.stop();
    s->_task = nullptr;
    vTaskDelete(nullptr);
}

#endif // PACK_RADIO
#endif // ENABLE_UDA1334A == 1