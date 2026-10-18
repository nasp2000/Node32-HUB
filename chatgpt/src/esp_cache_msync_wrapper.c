/*
 * esp_cache_msync_wrapper.c
 *
 * Wrapper for esp_cache_msync() to fix IDF 5.5.1 bug on ESP32-P4.
 * The HCD driver (hcd_dwc.c) calls esp_cache_msync() with NULL addr
 * during bulk IN transfers, causing a crash (load fault at Addr=0).
 *
 * This wrapper adds a NULL check before calling the real function.
 * Compiled only for esp32p4 environment.
 *
 * NOTE: --wrap=esp_cache_msync rewrites EVERY reference to esp_cache_msync,
 * including this wrapper's own call. We must call the real function through
 * __real_esp_cache_msync, otherwise the wrapper recurses infinitely on the
 * first non-NULL cache operation (hangs/stack-overflows the whole firmware,
 * e.g. SDMMC init stuck on CPU0 at boot, task-wdt on loopTask ~130s in).
 */

#include <stddef.h>
#include <stdint.h>

/* esp_err_t is int32_t, ESP_OK is 0 */
typedef int32_t esp_err_t;
#define ESP_OK 0

/* The real IDF function; --wrap maps __real_esp_cache_msync -> esp_cache_msync */
extern esp_err_t __real_esp_cache_msync(void *addr, size_t size, int flags);

esp_err_t __wrap_esp_cache_msync(void *addr, size_t size, int flags)
{
    if (addr == NULL) {
        return ESP_OK;
    }
    return __real_esp_cache_msync(addr, size, flags);
}
