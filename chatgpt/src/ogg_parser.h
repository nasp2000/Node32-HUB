#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ============================================================================
// ogg_parser.h — first-party OGG container demuxer + Opus packet helpers.
//
// Both new RADIO decoders ride on OGG: Vorbis packets go straight into
// stb_vorbis (which de-pages internally), while Opus packets must be
// extracted from the OGG pages before libopus (esp_opus_dec) can decode
// them. This header is the single shared OGG implementation (MIT,
// first-party, no Arduino dependencies) so the host native tests compile
// the exact production code.
//
//   ogg::PageHeader  — parsed OggS page header (27 bytes + segment table).
//   ogg::parseHeader — validate + measure one page (pure, no state).
//   ogg::Reader      — incremental packet reassembler: feed() raw bytes,
//                      one complete packet per onPacket() call, continued
//                      packets across pages reassembled internally.
//   opus::Head       — parsed OpusHead identification packet.
//   opus::parseHead  — validate + decode OpusHead (RFC 7845 section 5.1).
//   opus::frameCount — TOC-driven frame count of one Opus packet
//                      (RFC 6716 section 3.1).
// ============================================================================

namespace ogg {

// OggS page header (27 fixed bytes) + segment table.
struct PageHeader {
    bool     valid;      // capture pattern + version + sizes sane
    uint8_t  type;       // header_type_flag: 0x01 continued, 0x02 BOS, 0x04 EOS
    uint64_t granule;    // granule position (Vorbis/Opus sample count)
    uint32_t serial;     // bitstream serial number
    uint32_t seq;        // page sequence number
    uint8_t  nseg;       // segment count
    uint16_t headerLen;  // 27 + nseg
    uint32_t bodyLen;    // sum of the lacing values
};

// Parse one OggS page header at buf (needs >= 27 bytes for the fixed part,
// >= 27 + nseg for the full header). Pure: never reads past the header.
inline PageHeader parseHeader(const uint8_t* buf, size_t len) {
    PageHeader h;
    h.valid = false;
    h.type = 0;
    h.granule = 0;
    h.serial = 0;
    h.seq = 0;
    h.nseg = 0;
    h.headerLen = 0;
    h.bodyLen = 0;
    if (!buf || len < 27) return h;
    if (buf[0] != 'O' || buf[1] != 'g' || buf[2] != 'g' || buf[3] != 'S') return h;
    if (buf[4] != 0) return h;  // stream_structure_version must be 0
    h.type = buf[5];
    h.granule = ((uint64_t)buf[6]) | ((uint64_t)buf[7] << 8) |
                 ((uint64_t)buf[8] << 16) | ((uint64_t)buf[9] << 24) |
                 ((uint64_t)buf[10] << 32) | ((uint64_t)buf[11] << 40) |
                 ((uint64_t)buf[12] << 48) | ((uint64_t)buf[13] << 56);
    h.serial = ((uint32_t)buf[14]) | ((uint32_t)buf[15] << 8) |
               ((uint32_t)buf[16] << 16) | ((uint32_t)buf[17] << 24);
    h.seq = ((uint32_t)buf[18]) | ((uint32_t)buf[19] << 8) |
            ((uint32_t)buf[20] << 16) | ((uint32_t)buf[21] << 24);
    // Bytes 22..25 are the CRC (not verified here — framing only).
    h.nseg = buf[26];
    if (len < (size_t)(27 + h.nseg)) return h;
    h.headerLen = (uint16_t)(27 + h.nseg);
    uint32_t body = 0;
    for (uint8_t i = 0; i < h.nseg; i++) body += buf[27 + i];
    h.bodyLen = body;
    h.valid = true;
    return h;
}

// Quick framing check: does buf start with an OggS capture pattern?
inline bool isOgg(const uint8_t* buf, size_t len) {
    if (!buf || len < 4) return false;
    return buf[0] == 'O' && buf[1] == 'g' && buf[2] == 'g' && buf[3] == 'S';
}

// Incremental OGG packet reassembler.
//
// Feed raw stream bytes; each complete packet is delivered via onPacket()
// (data pointer is only valid for the duration of the call — copy it).
// Packet index counts from the first BOS page (0 = id header, 1 = comment
// header, >= 2 = audio for both Vorbis and Opus). Packets from foreign
// serials (chained streams) reset the reassembly state. A lacing value of
// 255 continues the packet into the next segment; any smaller value ends it.
// A page ending on a 255 lacing keeps the packet open across the page
// boundary (the next page must carry the continued-packet flag).
//
// The pending assembly buffer is a fixed member (no heap): Opus audio
// packets are at most ~1500 bytes and Vorbis/Opus id headers fit easily.
// Oversized packets (tags with big covers) are truncated to the buffer and
// flagged via onPacket(truncated=true) so the caller can skip them.
class Reader {
 public:
    // onPacket(data, len, packetIndex, granule, truncated)
    template <typename F>
    size_t feed(const uint8_t* data, size_t len, F onPacket) {
        size_t pos = 0;
        while (pos < len) {
            // Ensure the fixed 27-byte header is buffered.
            if (_avail - _used < 27) {
                size_t want = 27 - (_avail - _used);
                size_t take = len - pos;
                if (take > want) take = want;
                append(data + pos, take);
                pos += take;
                if (_avail - _used < 27) return pos;  // wait for more
            }
            const uint8_t* cur = _buf + _used;
            if (cur[0] != 'O' || cur[1] != 'g' || cur[2] != 'g' || cur[3] != 'S') {
                _used++;  // resync: drop one byte of non-OGG prefix/garbage
                compact();
                continue;
            }
            // Full header (fixed + segment table) must be buffered before
            // parsing, otherwise a valid split header would look corrupt.
            size_t need = (size_t)27 + cur[26];
            if (_avail - _used < need) {
                size_t want = need - (_avail - _used);
                size_t take = len - pos;
                if (take > want) take = want;
                append(data + pos, take);
                pos += take;
                if (_avail - _used < need) return pos;  // wait for more
                cur = _buf + _used;
            }
            PageHeader h = parseHeader(cur, _avail - _used);
            if (!h.valid) {
                _used++;  // bad version: skip and resync
                compact();
                continue;
            }
            size_t pageTotal = (size_t)h.headerLen + h.bodyLen;
            if (_avail - _used < pageTotal) {
                // Incomplete page buffered: top up from the input.
                size_t want = pageTotal - (_avail - _used);
                size_t take = len - pos;
                if (take > want) take = want;
                append(data + pos, take);
                pos += take;
                if (_avail - _used < pageTotal) return pos;  // wait for more
                cur = _buf + _used;
            }
            if (h.serial != _serial) {
                // New (or first) logical stream: reset reassembly.
                _serial = h.serial;
                _pktIdx = 0;
                _pendLen = 0;
                _open = false;
                _seq = h.seq;
            }
            // Walk the lacing values, slicing packets out of the page body.
            const uint8_t* lace = cur + 27;
            const uint8_t* body = cur + h.headerLen;
            size_t bodyOff = 0;
            for (uint8_t s = 0; s < h.nseg; s++) {
                uint8_t seg = lace[s];
                // Guard against a truncated buffer (should not happen after
                // the pageTotal check, but never over-read).
                size_t segAvail = (h.bodyLen > bodyOff) ? (h.bodyLen - bodyOff) : 0;
                size_t segTake = seg;
                if (segTake > segAvail) segTake = segAvail;
                bool continued = (seg == 255);
                if (_pendLen + segTake > sizeof(_pend)) {
                    // Oversized packet (e.g. OpusTags with embedded cover):
                    // deliver what fits, flag truncated, drop the rest of
                    // this packet but keep framing in sync.
                    size_t room = sizeof(_pend) - _pendLen;
                    if (room > 0) {
                        memcpy(_pend + _pendLen, body + bodyOff, room);
                        _pendLen += room;
                    }
                    _truncated = true;
                } else if (segTake > 0) {
                    memcpy(_pend + _pendLen, body + bodyOff, segTake);
                    _pendLen += segTake;
                }
                bodyOff += segTake;
                if (!continued) {
                    onPacket(_pend, _pendLen, _pktIdx, h.granule, _truncated);
                    _pktIdx++;
                    _pendLen = 0;
                    _truncated = false;
                    _open = false;
                } else {
                    _open = true;
                }
            }
            // A page ending on a 255 lacing leaves the packet open across the
            // page boundary; otherwise nothing may stay open here.
            _used += pageTotal;
            _seq = h.seq + 1;
            compact();
        }
        return pos;
    }

    void reset() {
        _used = 0;
        _avail = 0;
        _serial = 0;
        _seq = 0;
        _pktIdx = 0;
        _pendLen = 0;
        _open = false;
        _truncated = false;
    }

    uint32_t serial() const { return _serial; }
    uint32_t packetIndex() const { return _pktIdx; }

 private:
    void append(const uint8_t* data, size_t len) {
        if (len == 0) return;
        if (_avail + len > sizeof(_buf)) {
            // Buffer full of undecodable bytes: drop the oldest to make
            // room (keeps streams with garbage prefixes moving forward).
            compact();
            if (_avail + len > sizeof(_buf)) {
                size_t drop = _avail + len - sizeof(_buf);
                if (drop > _avail - _used) drop = _avail - _used;
                _used += drop;
                compact();
            }
        }
        size_t room = sizeof(_buf) - _avail;
        if (len > room) len = room;
        memcpy(_buf + _avail, data, len);
        _avail += len;
    }
    void compact() {
        if (_used == 0) return;
        if (_used >= _avail) {
            _used = 0;
            _avail = 0;
            return;
        }
        memmove(_buf, _buf + _used, _avail - _used);
        _avail -= _used;
        _used = 0;
    }

    static const size_t kBufSize = 8192;
    uint8_t _buf[kBufSize];
    size_t _used = 0;
    size_t _avail = 0;
    uint8_t _pend[2048];
    size_t _pendLen = 0;
    uint32_t _serial = 0;
    uint32_t _seq = 0;
    uint32_t _pktIdx = 0;
    bool _open = false;
    bool _truncated = false;
};

}  // namespace ogg

namespace opus {

// OpusHead identification header (RFC 7845 section 5.1).
struct Head {
    bool valid;
    uint8_t channels;    // 1 = mono, 2 = stereo
    uint16_t preSkip;    // samples to skip at the start (at 48 kHz)
    uint32_t sampleRate;  // input sample rate (informational; decode is 48 kHz)
};

// Validate + decode an OpusHead packet (packet index 0 of an OGG Opus
// stream). Needs >= 19 bytes: magic "OpusHead", version, channels,
// pre-skip, sample rate. Version must be 0..15 per RFC 7845.
inline Head parseHead(const uint8_t* pkt, size_t len) {
    Head h;
    h.valid = false;
    h.channels = 0;
    h.preSkip = 0;
    h.sampleRate = 0;
    if (!pkt || len < 19) return h;
    if (memcmp(pkt, "OpusHead", 8) != 0) return h;
    uint8_t ver = pkt[8];
    if (ver > 15) return h;
    uint8_t ch = pkt[9];
    if (ch < 1 || ch > 2) return h;
    h.channels = ch;
    h.preSkip = (uint16_t)pkt[10] | ((uint16_t)pkt[11] << 8);
    h.sampleRate = (uint32_t)pkt[12] | ((uint32_t)pkt[13] << 8) |
                   ((uint32_t)pkt[14] << 16) | ((uint32_t)pkt[15] << 24);
    h.valid = true;
    return h;
}

// Frame count of one Opus packet from its TOC byte (RFC 6716 section 3.1):
// code 0 = 1 frame, 1 = 2 same-size, 2 = 2 different-size, 3 = count in the
// next byte (M). Returns -1 when the packet is too short to parse.
inline int frameCount(const uint8_t* pkt, size_t len) {
    if (!pkt || len < 1) return -1;
    uint8_t code = pkt[0] & 0x03;
    if (code == 0) return 1;
    if (code == 1 || code == 2) return 2;
    if (len < 2) return -1;
    uint8_t m = pkt[1];
    if (m == 0 || m > 120) return -1;  // M is a frame count bound
    return m;
}

}  // namespace opus
