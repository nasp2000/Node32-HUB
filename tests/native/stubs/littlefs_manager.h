#pragma once
#include "Arduino.h"

// Native stand-in for littlefs_manager.h. LittleFsStore.cpp only calls
// littlefsBegin()/littlefsMounted(); the real mount logic is not testable
// on the host, so these return success unconditionally.
inline bool littlefsBegin() { return true; }
inline bool littlefsMounted() { return true; }
// Daily storage stats: no-op on the host (NVS not present in native tests).
#include <cstdint>
inline void storNoteLfsWrite(uint64_t) {}
