// grbl_sender_stream.cpp
// GRBL streaming pipeline: staging buffer, prepared queue, PSRAM feeder task.
// Extracted from grbl_sender.cpp for parallel compilation.
#include "grbl_sender_internal.h"

#include "feature_flags.h"
#if ENABLE_GRBL == 1

#include <string.h>
#include "feature_flags.h"

// ---------------------------------------------------------------------------
// Stream stage helpers
// ---------------------------------------------------------------------------
void configureStreamStageWatermarksLocked() {
    const size_t cap = s_streamStageCapacity;
    if (cap == 0) {
        s_streamLowWatermarkBytes = GRBL_STREAM_LOW_WATERMARK_BYTES;
        s_streamRefillChunkBytes = GRBL_STREAM_REFILL_CHUNK_BYTES;
        return;
    }
    size_t low = cap / 4U;
    low = clampSizeT(low, (size_t)GRBL_STREAM_LOW_WATERMARK_MIN_BYTES,
                     (size_t)GRBL_STREAM_LOW_WATERMARK_MAX_BYTES);
    if (low >= cap) low = cap / 2U;
    if (low == 0) low = cap / 2U;
    size_t chunk = cap / 16U;
    chunk = clampSizeT(chunk, (size_t)GRBL_STREAM_REFILL_MIN_BYTES,
                       (size_t)GRBL_STREAM_REFILL_MAX_BYTES);
    if (chunk > cap) chunk = cap;
    if (chunk == 0) chunk = cap;
    s_streamLowWatermarkBytes = low;
    s_streamRefillChunkBytes = chunk;
}

void clearPreparedQueue() {
    s_preparedHead = 0;
    s_preparedTail = 0;
    s_preparedCount = 0;
}

bool preparedQueueIsFull() {
    return s_preparedCount >= (uint16_t)GRBL_PREPARED_LINE_QUEUE_CAP;
}

bool preparedQueueIsEmpty() {
    return s_preparedCount == 0;
}

bool preparedQueuePush(const char* line, size_t len) {
    if (!line) return false;
    if (preparedQueueIsFull()) return false;
    if (len >= (size_t)GRBL_MAX_LINE_LEN + 1U) return false;
    strlcpy(s_preparedLines[s_preparedHead], line, (size_t)GRBL_MAX_LINE_LEN + 1U);
    s_preparedLens[s_preparedHead] = (uint16_t)len;
    s_preparedHead = (uint16_t)((s_preparedHead + 1U) % (uint16_t)GRBL_PREPARED_LINE_QUEUE_CAP);
    s_preparedCount++;
    return true;
}

bool preparedQueuePeek(const char** line, size_t* len) {
    if (preparedQueueIsEmpty()) return false;
    if (line) *line = s_preparedLines[s_preparedTail];
    if (len) *len = (size_t)s_preparedLens[s_preparedTail];
    return true;
}

bool preparedQueuePop() {
    if (preparedQueueIsEmpty()) return false;
    s_preparedTail = (uint16_t)((s_preparedTail + 1U) % (uint16_t)GRBL_PREPARED_LINE_QUEUE_CAP);
    s_preparedCount--;
    return true;
}

void resetStreamStageState() {
    s_streamStageFill = 0;
    s_streamStageReadPos = 0;
    s_streamStageEof = false;
    s_streamStageLastProgressMs = millis();
    s_lastFeedPos = 0;
    s_lastFeedProgressMs = millis();
    clearPreparedQueue();
}

size_t streamStageBytesAvailable() {
    if (s_streamStageFill < s_streamStageReadPos) return 0;
    return s_streamStageFill - s_streamStageReadPos;
}

void compactStreamStageIfNeeded() {
    if (!s_streamStageBuf) return;
    if (s_streamStageReadPos == 0) return;
    if (s_streamStageReadPos >= s_streamStageFill) {
        s_streamStageReadPos = 0;
        s_streamStageFill = 0;
        return;
    }
    if (s_streamStageCapacity == 0) return;
    if (s_streamStageReadPos >= (s_streamStageCapacity / 2UL) ||
        s_streamStageFill >= s_streamStageCapacity) {
        const size_t remaining = s_streamStageFill - s_streamStageReadPos;
        memmove(s_streamStageBuf, s_streamStageBuf + s_streamStageReadPos, remaining);
        s_streamStageFill = remaining;
        s_streamStageReadPos = 0;
    }
}

bool peekStreamStageRawLine(char* out, size_t outSize, size_t* consumedBytes, bool* needMoreData,
                             bool* lineTooLong) {
    if (consumedBytes) *consumedBytes = 0;
    if (needMoreData) *needMoreData = false;
    if (lineTooLong) *lineTooLong = false;
    if (!s_streamStageBuf || !out || outSize < 2) return false;

    const size_t available = streamStageBytesAvailable();
    if (available == 0) {
        if (needMoreData && !s_streamStageEof) *needMoreData = true;
        return false;
    }

    const size_t start = s_streamStageReadPos;
    size_t cursor = start;
    while (cursor < s_streamStageFill && s_streamStageBuf[cursor] != '\n') cursor++;

    const bool hasNewline = (cursor < s_streamStageFill && s_streamStageBuf[cursor] == '\n');
    if (!hasNewline && !s_streamStageEof) {
        if (needMoreData) *needMoreData = true;
        return false;
    }

    const size_t end = hasNewline ? cursor : s_streamStageFill;
    size_t outLen = 0;
    bool truncated = false;
    size_t i;
    for (i = start; i < end; i++) {
        const char c = s_streamStageBuf[i];
        if (c == '\r') continue;
        if (outLen < outSize - 1) {
            out[outLen++] = c;
        } else {
            truncated = true;
        }
    }
    out[outLen] = '\0';
    if (lineTooLong && truncated) *lineTooLong = true;

    if (consumedBytes) *consumedBytes = hasNewline ? ((end - start) + 1U) : (end - start);
    return true;
}

void consumeStreamStageBytes(size_t consumedBytes) {
    if (consumedBytes == 0) return;
    s_streamStageReadPos += consumedBytes;
    if (s_streamStageReadPos >= s_streamStageFill) {
        s_streamStageReadPos = 0;
        s_streamStageFill = 0;
        return;
    }
    compactStreamStageIfNeeded();
}

#if ENABLE_SDCARD == 1
size_t fillStreamStageFromFile() {
    if (!s_streamStageBuf || !s_file || s_streamStageEof) return 0;
    if (streamStageBytesAvailable() >= s_streamLowWatermarkBytes) return 0;

    compactStreamStageIfNeeded();

    size_t freeBytes = (s_streamStageCapacity > s_streamStageFill)
        ? (s_streamStageCapacity - s_streamStageFill)
        : 0;
    if (freeBytes == 0) return 0;

    size_t budget = s_streamRefillChunkBytes;
    if (budget > freeBytes) budget = freeBytes;

    size_t totalRead = 0;
    while (budget > 0 && s_file.available()) {
        size_t chunk = budget;
        const size_t n = s_file.read((uint8_t*)(s_streamStageBuf + s_streamStageFill), chunk);
        if (n == 0) break;
        s_streamStageFill += n;
        totalRead += n;
        if (n >= budget) budget = 0;
        else budget -= n;
    }

    if (totalRead > 0) s_streamStageLastProgressMs = millis();
    if (!s_file.available()) s_streamStageEof = true;
    return totalRead;
}

int fillPreparedQueueFromStage() {
    if (!s_streamStageBuf) return 0;

    int preparedNow = 0;
    while (!preparedQueueIsFull()) {
        char raw[GRBL_MAX_LINE_LEN + 2];
        size_t consumedBytes = 0;
        bool needMoreData = false;
        bool lineTooLong = false;
        if (!peekStreamStageRawLine(raw, sizeof(raw), &consumedBytes, &needMoreData, &lineTooLong)) {
            if (needMoreData && fillStreamStageFromFile() > 0) continue;
            break;
        }

        if (lineTooLong) {
            return -1;
        }

        consumeStreamStageBytes(consumedBytes);

        char stripped[GRBL_MAX_LINE_LEN + 1];
        size_t slen = stripLine(raw, stripped, sizeof(stripped));
        if (slen == 0 || stripped[0] == ';') continue;

        // Alarm-resume offset: drop already-executed lines (single-writer:
        // only this feeder touches the counter while streaming).
        if (s_resumeDropArmed && s_resumeDropLines > 0) {
            if (--s_resumeDropLines == 0) s_resumeDropArmed = false;
            continue;
        }

        if (!preparedQueuePush(stripped, slen)) break;
        preparedNow++;
    }

    return preparedNow;
}
#endif

// ---------------------------------------------------------------------------
// PSRAM feeder task — dedicated high-priority task that keeps the GRBL buffer
// 100% full. Runs on Core 0 (same as TinyUSB). PSRAM reads are instant, so
// this task never blocks the CPU — it yields instantly when buffer is full.
// ---------------------------------------------------------------------------

static void psramFeedTask(void* arg) {
    (void)arg;

    while (!s_psramFeedTaskStop) {
        if (s_state != GrblStreamState::STREAMING || !s_streamPsramBuf) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            const bool canSend = (strcasecmp(s_machineStateName, "IDLE") == 0) ||
                                 (strcasecmp(s_machineStateName, "RUN") == 0) ||
                                 (strcasecmp(s_machineStateName, "JOG") == 0);
            xSemaphoreGive(s_mutex);
            if (!canSend) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        }

        while (!s_psramFeedTaskStop) {
            char batchBuf[GRBL_RX_STREAM_INFLIGHT_LIMIT];
            uint8_t batchLineBytes[GRBL_INFLIGHT_FIFO_SIZE];
            uint8_t batchCount = 0;
            uint8_t batchTotalBytes = 0;
            uint8_t batchFifoSafetySlack = 2;
            size_t batchSavedPos;
            bool reserved = false;

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if (s_state != GrblStreamState::STREAMING || !s_streamPsramBuf) {
                xSemaphoreGive(s_mutex);
                break;
            }

            batchSavedPos = s_streamPsramPos;
            const uint8_t fifoFree = (uint8_t)((uint16_t)GRBL_INFLIGHT_FIFO_SIZE - (uint16_t)s_fifoCount);
            const uint8_t maxBatchLines = (fifoFree > batchFifoSafetySlack)
                ? (uint8_t)(fifoFree - batchFifoSafetySlack) : 0U;
            // Use the planner-filling allowance for the WHOLE job (not just the
            // first batch) so GRBL's 18-block planner stays full during dense
            // streams. A strict 127-byte cap leaves a few leftover bytes that the
            // next line often does not fit, deadlocking the feeder while GRBL
            // executes and does not ack. GRBL acks on RX->planner drain, so
            // s_inflight > 127 does not overflow the 128-byte RX ring.
            const uint16_t activeLimit = (uint16_t)GRBL_RX_STREAM_INFLIGHT_LIMIT;
            const uint16_t remainingBudget = (s_inflight >= activeLimit)
                ? 0U : (uint16_t)(activeLimit - (uint16_t)s_inflight);

            while (batchCount < maxBatchLines && batchTotalBytes < remainingBudget
                   && s_streamPsramPos < s_streamPsramLen) {
                const size_t start = s_streamPsramPos;
                size_t end = start;
                while (end < s_streamPsramLen && s_streamPsramBuf[end] != '\n') end++;
                size_t lineLen = end - start;
                if (lineLen > 0 && s_streamPsramBuf[end - 1] == '\r') lineLen--;
                const size_t nextPos = (end < s_streamPsramLen) ? end + 1 : s_streamPsramLen;

                if (lineLen == 0 || s_streamPsramBuf[start] == ';') {
                    s_streamPsramPos = nextPos;
                    continue;
                }

                // Alarm-resume offset: same rule as the staged feeder.
                if (s_resumeDropArmed && s_resumeDropLines > 0) {
                    if (--s_resumeDropLines == 0) s_resumeDropArmed = false;
                    s_streamPsramPos = nextPos;
                    continue;
                }

                if (lineLen > GRBL_MAX_LINE_LEN) lineLen = GRBL_MAX_LINE_LEN;

                // Height-map compensation — copy the source line, rewrite Z in
                // place when a map is active. Deterministic per source line, so
                // re-extracting on a budget spill produces the same result.
                char compLine[GRBL_MAX_LINE_LEN + 2];
                memcpy(compLine, s_streamPsramBuf + start, lineLen);
                compLine[lineLen] = '\0';
                if (s_heightMapEnabled && s_probeGridTotal > 0) {
                    lineLen = grblApplyHeightMapToLine(compLine, lineLen);
                }
                const uint8_t wb = (uint8_t)(lineLen + 1U);

                if ((uint16_t)batchTotalBytes + (uint16_t)wb > remainingBudget)
                    break;
                if ((uint16_t)batchTotalBytes + (uint16_t)wb > (uint16_t)sizeof(batchBuf))
                    break;

                memcpy(batchBuf + batchTotalBytes, compLine, lineLen);
                batchBuf[batchTotalBytes + lineLen] = '\n';
                batchLineBytes[batchCount] = wb;
                batchTotalBytes += wb;
                batchCount++;
                s_streamPsramPos = nextPos;
            }

            if (batchCount > 0) {
                s_inflight += batchTotalBytes;
                for (uint8_t i = 0; i < batchCount; i++)
                    fifoPush(batchLineBytes[i], nullptr);
                s_linesSent += batchCount;
                reserved = true;
            }
            xSemaphoreGive(s_mutex);

            if (!reserved) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
                continue;
            }

            const size_t sent = sendUsbPayloadWithRetry(s_slot, (const uint8_t*)batchBuf, batchTotalBytes);

            if (sent < batchTotalBytes) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_inflight -= batchTotalBytes;
                for (uint8_t i = 0; i < batchCount; i++)
                    fifoPop(nullptr, 0);
                s_linesSent -= batchCount;
                s_streamPsramPos = batchSavedPos;
                if (sent == 0) s_lastUsbTxZeroMs = millis();
                xSemaphoreGive(s_mutex);
            }
        }
    }
    s_psramFeedTask = nullptr;
    vTaskDelete(nullptr);
}

void psramFeedNotify() {
    TaskHandle_t h = s_psramFeedTask;
    if (h != nullptr) {
        xTaskNotifyGive(h);
    }
}

void grblStartPsramFeedTask() {
    if (s_psramFeedTask != nullptr) return;
    s_psramFeedTaskStop = false;
    xTaskCreatePinnedToCore(psramFeedTask, "psram_feed", 4096, nullptr, 18, &s_psramFeedTask, 0);
}

void grblStopPsramFeedTask() {
    s_psramFeedTaskStop = true;
    TaskHandle_t h = s_psramFeedTask;
    if (h != nullptr) {
        vTaskNotifyGiveFromISR(h, nullptr);
    }
}

#endif // ENABLE_GRBL == 1
