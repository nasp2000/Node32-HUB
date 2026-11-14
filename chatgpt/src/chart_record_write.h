// chart_record_write.h — byte layout of one .chart record.
//
// Extracted from chart_history.cpp so the serialization that reaches the file
// can be unit-tested without the ESP FS stack: u32 timestamp, u8 valueCount,
// u8 reserved[3], then `columnCount` float32 values (little-endian on both the
// ESP32-S3 and the host). Any short write aborts and reports failure, so a full
// card never leaves a half record behind.
#pragma once

#include <stddef.h>
#include <stdint.h>

template <class FileT, class RecordT>
inline bool chartWriteRecord(FileT& file, const RecordT& record, uint8_t columnCount) {
    if (columnCount == 0) return false;
    if (file.write((const uint8_t*)&record.timestamp, sizeof(record.timestamp)) != sizeof(record.timestamp)) return false;
    if (file.write((const uint8_t*)&record.valueCount, sizeof(record.valueCount)) != sizeof(record.valueCount)) return false;
    if (file.write((const uint8_t*)record.reserved, sizeof(record.reserved)) != sizeof(record.reserved)) return false;
    const size_t valuesBytes = (size_t)columnCount * sizeof(float);
    return file.write((const uint8_t*)record.values, valuesBytes) == valuesBytes;
}
