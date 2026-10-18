#include "grbl_psram_gcode.h"
#include "feature_flags.h"

#if ENABLE_GRBL == 1

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>
#include "runtime_storage.h"
#include "psram_vfs.h"

static char* s_psramGcodeData = nullptr;
static size_t s_psramGcodeSize = 0;
static char s_psramGcodeName[128] = {0};

// Returns true when the VFS is the active backing store for the G-code blob.
// On S3 (ENABLE_SDCARD=0, VFS primary storage) and P4 with VFS initialized,
// the G-code blob is stored within the VFS budget (PSRAM_VFS_BUFFER_SIZE).
static inline bool usingVfs() {
    return psramVFSInitialized;
}

bool grblPsramGcodeStore(const char* name, const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;

    // Free previous allocation (VFS or direct)
    if (usingVfs() && !s_psramGcodeData) {
        char* old = psramVFSReleaseGcodeBlob();
        if (old) heap_caps_free(old);
    } else if (s_psramGcodeData) {
        heap_caps_free(s_psramGcodeData);
        s_psramGcodeData = nullptr;
    }
    s_psramGcodeSize = 0;

    // Store in VFS when active — data is tracked within the VFS budget
    if (usingVfs()) {
        if (!psramVFSSetGcodeBlob(data, size)) return false;
        s_psramGcodeSize = size;
    } else {
        // Direct PSRAM allocation (fallback when VFS unavailable)
        if (psramFound()) {
            s_psramGcodeData = (char*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        }
        if (!s_psramGcodeData) {
            s_psramGcodeData = (char*)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!s_psramGcodeData) return false;
        memcpy(s_psramGcodeData, data, size);
        s_psramGcodeSize = size;
    }

    if (name) {
        strncpy(s_psramGcodeName, name, sizeof(s_psramGcodeName) - 1);
        s_psramGcodeName[sizeof(s_psramGcodeName) - 1] = '\0';
    } else {
        s_psramGcodeName[0] = '\0';
    }
    return true;
}

void grblPsramGcodeClear() {
    if (usingVfs() && !s_psramGcodeData) {
        char* old = psramVFSReleaseGcodeBlob();
        if (old) heap_caps_free(old);
    } else if (s_psramGcodeData) {
        heap_caps_free(s_psramGcodeData);
        s_psramGcodeData = nullptr;
    }
    s_psramGcodeSize = 0;
    s_psramGcodeName[0] = '\0';
}

const char* grblPsramGcodeGetName() {
    return s_psramGcodeName;
}

size_t grblPsramGcodeGetSize() {
    if (usingVfs() && !s_psramGcodeData) {
        size_t sz;
        psramVFSGetGcodeBlobData(&sz);
        return sz;
    }
    return s_psramGcodeSize;
}

const char* grblPsramGcodeGetData() {
    if (usingVfs() && !s_psramGcodeData) {
        return psramVFSGetGcodeBlobData(nullptr);
    }
    return s_psramGcodeData;
}

char* grblPsramGcodeRelease() {
    // When VFS-backed: release from VFS (zero-copy)
    if (usingVfs() && !s_psramGcodeData) {
        s_psramGcodeSize = 0;
        s_psramGcodeName[0] = '\0';
        return psramVFSReleaseGcodeBlob();
    }
    // When direct allocation: return internal pointer
    char* ptr = s_psramGcodeData;
    s_psramGcodeData = nullptr;
    s_psramGcodeSize = 0;
    s_psramGcodeName[0] = '\0';
    return ptr;
}

#endif // ENABLE_GRBL == 1
