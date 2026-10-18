// ota_security.h — RSA-2048+SHA-256 signed OTA verification + chip magic + rollback
#pragma once
#include <stdint.h>
#include <stddef.h>

// Chip magic constants: 4 bytes appended before signature to prevent S3↔P4 cross-flash.
static const uint8_t OTA_CHIP_MAGIC_S3[] = {'S','3','_','_'};
static const uint8_t OTA_CHIP_MAGIC_P4[] = {'P','4','_','_'};

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
#define OTA_CHIP_MAGIC OTA_CHIP_MAGIC_P4
#else
#define OTA_CHIP_MAGIC OTA_CHIP_MAGIC_S3
#endif

// Signed firmware format: [firmware bytes][4-byte chip magic][256-byte RSA signature]
// Total overhead: 260 bytes. The 4-byte chip magic IS included in the SHA-256 hash.
// Only the last 256 bytes (RSA signature) are excluded from the hash.

// Initialize streaming OTA verification. totalSize = Content-Length (must include 260 appended bytes).
void otaVerifyInit(size_t totalSize);

// Feed a data chunk. Call for EVERY received byte (firmware + magic + signature).
// Internally hashes everything except the last 256 bytes (RSA signature),
// and buffers the last 260 bytes (chip magic + signature) for final verification.
void otaVerifyFeed(const uint8_t* data, size_t len);

// Finalize verification: check chip magic + RSA signature.
// Returns true if the firmware is authentic and targeted at this chip.
// Must only be called after ALL bytes have been fed.
bool otaVerifyEnd();

// Mark current firmware as healthy (disable rollback on next boot).
// Call once at end of successful setup().
void otaMarkBootValid();
