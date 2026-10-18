#pragma once
// Host stand-in for arduino-esp32's <FS.h> (legacy fs::FS base used by
// littlefs_manager.h's getLittleFS()). Only the type is needed; ConfigManager
// never calls getLittleFS().
namespace fs {
class FS {
public:
    virtual ~FS() {}
    // Minimal surface so real littlefs_manager-style callers can compile.
    bool begin() { return true; }
    bool mounted() { return true; }
};
} // namespace fs