// ota_security.cpp — RSA-2048+SHA-256 OTA verification implementation
#include "ota_security.h"
#include "ota_pubkey.h"

#include <string.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <esp_ota_ops.h>

// Streaming verification state (single OTA at a time)
static struct {
    size_t totalSize;         // Content-Length
    size_t cursor;            // bytes fed so far
    size_t hashStop;          // totalSize - 256 (don't hash the signature)
    size_t bufStart;          // totalSize - 260 (start buffering magic + sig)
    uint8_t buf[260];         // last 260 bytes: [4 magic][256 sig]
    mbedtls_sha256_context sha256;
    bool active;
} g_otaCtx;

void otaVerifyInit(size_t totalSize) {
    memset(&g_otaCtx, 0, sizeof(g_otaCtx));
    g_otaCtx.totalSize = totalSize;
    g_otaCtx.hashStop = (totalSize > 256) ? totalSize - 256 : 0;
    g_otaCtx.bufStart = (totalSize > 260) ? totalSize - 260 : 0;
    g_otaCtx.active = true;
    mbedtls_sha256_init(&g_otaCtx.sha256);
    mbedtls_sha256_starts(&g_otaCtx.sha256, 0);
}

void otaVerifyFeed(const uint8_t* data, size_t len) {
    if (!g_otaCtx.active || len == 0) return;

    size_t startAbs = g_otaCtx.cursor;
    size_t endAbs = startAbs + len;

    // Hash bytes [0, hashStop) — everything except the last 256 (signature)
    size_t hashStart = startAbs;
    size_t hashEnd = (endAbs < g_otaCtx.hashStop) ? endAbs : g_otaCtx.hashStop;
    if (hashEnd > hashStart) {
        mbedtls_sha256_update(&g_otaCtx.sha256,
                              data + (hashStart - startAbs),
                              hashEnd - hashStart);
    }

    // Buffer last 260 bytes [bufStart, totalSize) — chip magic + signature
    if (endAbs > g_otaCtx.bufStart && startAbs < g_otaCtx.totalSize) {
        size_t chunkStart = (startAbs > g_otaCtx.bufStart) ? startAbs : g_otaCtx.bufStart;
        size_t chunkEnd = (endAbs < g_otaCtx.totalSize) ? endAbs : g_otaCtx.totalSize;
        if (chunkEnd > chunkStart) {
            size_t bufOffset = chunkStart - g_otaCtx.bufStart;
            size_t srcOffset = chunkStart - startAbs;
            memcpy(g_otaCtx.buf + bufOffset,
                   data + srcOffset,
                   chunkEnd - chunkStart);
        }
    }

    g_otaCtx.cursor = endAbs;
}

bool otaVerifyEnd() {
    if (!g_otaCtx.active) return false;
    if (g_otaCtx.cursor != g_otaCtx.totalSize) return false;

    // Finalize SHA-256 hash
    uint8_t hash[32];
    mbedtls_sha256_finish(&g_otaCtx.sha256, hash);
    mbedtls_sha256_free(&g_otaCtx.sha256);
    g_otaCtx.active = false;

    // Check chip magic (first 4 bytes of buffer)
    if (memcmp(g_otaCtx.buf, OTA_CHIP_MAGIC, 4) != 0) {
        return false;
    }

    // RSA-2048 PKCS#1 v1.5 verify
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_parse_public_key(&pk, OTA_PUBKEY_DER, OTA_PUBKEY_DER_LEN);
    if (ret != 0) {
        mbedtls_pk_free(&pk);
        return false;
    }

    // signature is bytes [4..259] of buffer (256 bytes)
    const uint8_t* signature = g_otaCtx.buf + 4;
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, signature, 256);

    mbedtls_pk_free(&pk);
    return (ret == 0);
}

void otaMarkBootValid() {
    esp_ota_mark_app_valid_cancel_rollback();
}
