#ifndef CHART_STREAM_DECIMATE_H
#define CHART_STREAM_DECIMATE_H

#include <cstdint>
#include <cstring>

// Online, single-pass decimation for chart-history streaming.
//
// Replaces the historical two-pass algorithm (count matching records, compute a
// fixed stride, then re-read the whole file keeping every `stride`-th record).
// That approach had to read the SD file twice and its two passes both spent a
// lot of time sleeping in vTaskDelay(1) (each ~10 ms on a 100 Hz tick), so very
// large chart files (RS232/Modbus at 1 Hz = 86400 records/day) could exceed the
// web server's request time budget.
//
// This decimator processes records in a single streaming pass while keeping at
// most `maxPoints` evenly spaced samples from an arbitrarily long chronological
// input, without knowing the total count in advance. Whenever the buffer would
// exceed maxPoints it doubles the stride and halves the already-kept samples —
// the classic online-decimation scheme. Keeping the number of samples bounded
// also keeps the caller's PSRAM allocation fixed regardless of file size.
//
// The caller owns the output buffer. It must follow the returned action:
//   kSkip             discard this record,
//   kKeep             append the record at buffer index (live() - 1),
//   kCompactThenKeep  keep only the even entries (0,2,4,...) of the current
//                     buffer, compact them to the front, then append the record
//                     at index (live() - 1). count = (live() - 1) valid entries
//                     survive the compaction.
//
// Pure / dependency-free so it can be unit-tested on the host.
class ChartStreamDecimator {
public:
    enum Action : uint8_t {
        kSkip = 0,
        kKeep = 1,
        kCompactThenKeep = 2
    };

    explicit ChartStreamDecimator(uint32_t maxPoints = 0) { reset(maxPoints); }

    void reset(uint32_t maxPoints) {
        cap_ = maxPoints;   // 0 => keep every record (no decimation)
        matched_ = 0;
        stride_ = 1;
        live_ = 0;
    }

    uint32_t matched() const { return matched_; }  // records fed in this pass
    uint32_t stride() const { return stride_; }    // interval between kept samples
    uint32_t live() const { return live_; }        // currently kept sample count

    uint8_t feed() {
        matched_++;
        if (cap_ == 0) {
            live_++;
            return kKeep;
        }
        if (matched_ % stride_) return kSkip;
        if (live_ >= cap_) {
            // Buffer full: halve kept samples and double the stride. Called only
            // when the cadence fits the NEW stride so that samples stay evenly
            // spaced and the stride doubling is not wasted.
            const uint32_t half = (live_ + 1) / 2;   // keep even entries 0,2,4,...
            if (half >= live_) return kSkip;         // cannot shrink (cap <= 1)
            if (matched_ % (stride_ * 2)) return kSkip;
            stride_ *= 2;
            live_ = half + 1;                        // this feed replaces slot `half`
            return kCompactThenKeep;
        }
        live_++;
        return kKeep;
    }

private:
    uint32_t cap_;
    uint32_t matched_;
    uint32_t stride_;
    uint32_t live_;
};

// Compact the first `count` fixed-size items of `buf` to the front, keeping only
// the even-indexed items (0,2,4,...). Matches the decimator's halving rule that
// accompanies ChartStreamDecimator::kCompactThenKeep.
inline void chartStreamDecimateCompact(void* buf, uint32_t count, size_t itemSize) {
    uint8_t* b = (uint8_t*)buf;
    uint32_t out = 0;
    for (uint32_t i = 0; i < count; i += 2) {
        if (out != i) memmove(b + out * itemSize, b + i * itemSize, itemSize);
        out++;
    }
}

#endif