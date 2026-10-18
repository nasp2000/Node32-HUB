#include "icy_demux.h"

#include <string.h>

namespace icy_demux {

int parseTitle(const uint8_t* block, size_t len, char* out, size_t outCap) {
    if (!out || outCap == 0) return 0;
    out[0] = '\0';
    if (!block || len == 0) return 0;

    static const char kNeedle[] = "StreamTitle='";
    const size_t kN = sizeof(kNeedle) - 1;
    if (len < kN) return 0;

    size_t i = 0;
    for (; i + kN <= len; i++) {
        if (memcmp(block + i, kNeedle, kN) == 0) break;
    }
    if (i + kN > len) return 0;

    size_t p = i + kN;
    size_t n = 0;
    while (p < len && n + 1 < outCap) {
        uint8_t c = block[p];
        if (c == '\'' || c == ';') break;
        out[n++] = (char)c;
        p++;
    }
    out[n] = '\0';
    return (int)n;
}

Demux::Demux()
    : _metaint(0), _state(S_AUDIO), _audioLeft(0), _metaLen(0), _metaGot(0) {}

void Demux::reset(uint32_t metaint) {
    _metaint = metaint;
    _state = S_AUDIO;
    // An ICY stream starts with `metaint` audio bytes (the interleave puts the
    // first length byte AFTER the first audio run), so we must forward them
    // before expecting a metadata block.
    _audioLeft = metaint;
    _metaLen = 0;
    _metaGot = 0;
}

uint32_t Demux::feed(const uint8_t* in, size_t len,
                     uint8_t* out, size_t outCap,
                     char* title, size_t titleCap,
                     bool* metaSeen) {
    if (metaSeen) *metaSeen = false;
    if (!in || len == 0 || !out || outCap == 0) return 0;

    size_t oi = 0; // output index
    size_t ii = 0; // input index

    while (ii < len) {
        if (_metaint == 0) {
            // No metadata in this stream: everything is audio.
            size_t take = len - ii;
            if (oi + take > outCap) take = outCap - oi;
            if (take == 0) break;
            memcpy(out + oi, in + ii, take);
            oi += take;
            ii += take;
            continue;
        }

        if (_state == S_AUDIO) {
            if (_audioLeft == 0) {
                // Audio interleave consumed (or never started): the next byte
                // is the metadata block length byte.
                _state = S_META_LEN;
                continue;
            }
            size_t take = len - ii;
            if (take > _audioLeft) take = _audioLeft;
            if (oi + take > outCap) take = outCap - oi;
            if (take == 0) break;
            memcpy(out + oi, in + ii, take);
            oi += take;
            ii += take;
            _audioLeft -= (uint32_t)take;
        } else if (_state == S_META_LEN) {
            _metaLen = (uint16_t)in[ii] * 16;
            _metaGot = 0;
            ii++;
            if (_metaLen > 0) {
                _state = S_META_DATA;
            } else {
                // Zero-length metadata block (no song change reported).
                _audioLeft = _metaint;
                _state = S_AUDIO;
            }
        } else { // S_META_DATA
            size_t need = (size_t)_metaLen - _metaGot;
            size_t take = len - ii;
            if (take > need) take = need;
            size_t dst = 0;
            if (_metaGot < sizeof(_metaBuf)) {
                size_t ncap = sizeof(_metaBuf) - _metaGot;
                dst = (take < ncap) ? take : ncap;
                memcpy(_metaBuf + _metaGot, in + ii, dst);
            }
            _metaGot += (uint16_t)take;
            ii += take;
            if (_metaGot >= _metaLen) {
                size_t have = (_metaGot < sizeof(_metaBuf)) ? _metaGot : sizeof(_metaBuf);
                if (title && titleCap > 0) parseTitle(_metaBuf, have, title, titleCap);
                if (metaSeen) *metaSeen = true;
                _audioLeft = _metaint;
                _state = S_AUDIO;
            }
        }
    }
    return (uint32_t)oi;
}

} // namespace icy_demux
