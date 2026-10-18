#pragma once

#include <Arduino.h>
#include "../../sd_security.h"
#include "../../ConfigManager.h"

extern ConfigManager configManager;

static inline String grblSdToken(const String& raw) {
    String out = raw;
    out.trim();
    if (out.length() == 0) out = "SD_LOG";
    out.replace(' ', '_');
    for (size_t i = 0; i < out.length(); i++) {
        const char c = out[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-')
            out.setCharAt(i, '_');
    }
    while (out.indexOf("__") >= 0) out.replace("__", "_");
    if (out.length() > 24) out = out.substring(0, 24);
    if (out.length() == 0) out = "SD_LOG";
    return out;
}

static inline String grblSdRootDir() {
    return SDSecurity::normalizePath(String("/") + grblSdToken(configManager.getSdcardName()) + "/gcode");
}

static inline String grblSdNormalizePath(const String& rawPath) {
    String path = rawPath;
    path.trim();
    path.replace('\\', '/');
    if (path.length() == 0) return path;
    if (path[0] != '/') path = "/" + path;
    return SDSecurity::normalizePath(path);
}

static inline bool grblSdIsExtAllowed(const String& path) {
    String lower = path;
    lower.toLowerCase();
    return lower.endsWith(".nc") || lower.endsWith(".gcode") ||
           lower.endsWith(".tap") || lower.endsWith(".txt") ||
           lower.endsWith(".gc") || lower.endsWith(".ngc") ||
           lower.endsWith(".cnc") || lower.endsWith(".code") ||
           lower.endsWith(".cg") || lower.endsWith(".cgo") ||
           lower.endsWith(".cgode");
}

static inline bool grblSdIsAllowedPath(const String& path, const String& rootDir) {
    if (!SDSecurity::isPathSafe(path)) return false;
    if (rootDir.length() == 0) return false;
    if (!path.startsWith(rootDir + "/")) return false;
    if (path.length() <= (size_t)(rootDir.length() + 1)) return false;
    return grblSdIsExtAllowed(path);
}

static inline bool grblSdIsAllowedDir(const String& path, const String& rootDir) {
    if (!SDSecurity::isPathSafe(path)) return false;
    if (rootDir.length() == 0) return false;
    if (path == rootDir) return true;
    return path.startsWith(rootDir + "/");
}