// =============================================================
// 🔒 sd_security.h — Security and Validation Helpers for SD Card Operations
// =============================================================
// ✅ Purpose: Centralized security validation for file operations
// ✅ Based on ESP3D-WEBUI best practices
// ✅ Prevents: Path traversal, invalid filenames, size attacks

#ifndef SD_SECURITY_H
#define SD_SECURITY_H

#include <Arduino.h>

namespace SDSecurity {
    
    // 🔒 SECURITY: Maximum file sizes (configurable)
    const size_t MAX_UPLOAD_SIZE = 100 * 1024 * 1024; // 100 MB default
    const size_t MAX_FILENAME_LENGTH = 128;
    const size_t MAX_PATH_LENGTH = 256;
    
    // 🔒 SECURITY: Path traversal protection
    // Returns true if path is safe (no "..", no absolute paths outside root)
    inline bool isPathSafe(const String& path) {
        // ❌ Reject paths with ".."
        if (path.indexOf("..") >= 0) return false;
        
        // ❌ Reject paths with backslashes (Windows-style)
        if (path.indexOf('\\') >= 0) return false;
        
        // ❌ Reject empty paths
        if (path.length() == 0) return false;
        
        // ✅ Must start with / for absolute paths
        if (path[0] != '/') return false;
        
        // ❌ Reject paths with double slashes
        if (path.indexOf("//") >= 0) return false;
        
        // ❌ Reject paths that are too long
        if (path.length() > MAX_PATH_LENGTH) return false;
        
        return true;
    }
    
    // 🔒 SECURITY: Filename validation and sanitization
    // Returns sanitized filename or empty string if invalid
    inline String sanitizeFilename(const String& filename) {
        // ❌ Reject empty filenames
        if (filename.length() == 0) return "";
        
        // ❌ Reject filenames that are too long
        if (filename.length() > MAX_FILENAME_LENGTH) return "";
        
        // ❌ Reject filenames with path separators
        if (filename.indexOf('/') >= 0 || filename.indexOf('\\') >= 0) return "";
        
        // ❌ Reject filenames with ".."
        if (filename.indexOf("..") >= 0) return "";
        
        // ❌ Reject filenames starting with dot (hidden files)
        if (filename[0] == '.') return "";
        
        // ❌ Reject filenames with control characters (ASCII < 32)
        for (size_t i = 0; i < filename.length(); i++) {
            char c = filename[i];
            if (c < 32 || c == 127) return ""; // Control chars or DEL
        }
        
        // ❌ Reject reserved Windows names (even on ESP32 for compatibility)
        String upper = filename;
        upper.toUpperCase();
        if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
            upper == "COM1" || upper == "COM2" || upper == "COM3" || upper == "COM4" ||
            upper == "LPT1" || upper == "LPT2" || upper == "LPT3" || upper == "LPT4") {
            return "";
        }
        
        // ✅ Filename is safe
        return filename;
    }
    
    // 🔒 SECURITY: Combined path + filename validation
    // Returns full sanitized path or empty string if invalid
    inline String validateAndJoinPath(const String& basePath, const String& filename) {
        // Validate base path
        if (!isPathSafe(basePath)) return "";
        
        // Sanitize filename
        String cleanFilename = sanitizeFilename(filename);
        if (cleanFilename.length() == 0) return "";
        
        // Build full path
        String fullPath = basePath;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += cleanFilename;
        
        // Final path validation
        if (!isPathSafe(fullPath)) return "";
        
        return fullPath;
    }
    
    // 🔒 SECURITY: File size validation
    inline bool isFileSizeValid(size_t size, size_t maxSize = MAX_UPLOAD_SIZE) {
        return size > 0 && size <= maxSize;
    }
    
    // 🔒 SECURITY: Extension validation (whitelist approach)
    // Returns true if extension is allowed
    inline bool isExtensionAllowed(const String& filename, const String* allowedExts, size_t numExts) {
        // If no whitelist provided, allow all
        if (!allowedExts || numExts == 0) return true;
        
        // Extract extension
        int dotPos = filename.lastIndexOf('.');
        if (dotPos < 0) return false; // No extension
        
        String ext = filename.substring(dotPos + 1);
        ext.toLowerCase();
        
        // Check whitelist
        for (size_t i = 0; i < numExts; i++) {
            String allowedExt = allowedExts[i];
            allowedExt.toLowerCase();
            if (ext == allowedExt) return true;
        }
        
        return false;
    }
    
    // 🔒 SECURITY: Content-Type validation helper
    inline String getContentType(const String& filename) {
        String ext = filename.substring(filename.lastIndexOf('.') + 1);
        ext.toLowerCase();
        
        // Common MIME types
        if (ext == "html" || ext == "htm") return "text/html";
        if (ext == "css") return "text/css";
        if (ext == "js") return "application/javascript";
        if (ext == "json") return "application/json";
        if (ext == "txt") return "text/plain";
        if (ext == "xml") return "text/xml";
        if (ext == "pdf") return "application/pdf";
        if (ext == "zip") return "application/zip";
        if (ext == "gz") return "application/gzip";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "png") return "image/png";
        if (ext == "gif") return "image/gif";
        if (ext == "svg") return "image/svg+xml";
        if (ext == "ico") return "image/x-icon";
        if (ext == "mp3") return "audio/mpeg";
        if (ext == "mp4") return "video/mp4";
        if (ext == "gcode" || ext == "g" || ext == "gco") return "text/plain";
        
        // Default: binary stream
        return "application/octet-stream";
    }
    
    // 🔒 SECURITY: Normalize path (remove redundant slashes, etc.)
    inline String normalizePath(String path) {
        // Ensure starts with /
        if (path.length() == 0 || path[0] != '/') {
            path = "/" + path;
        }
        
        // Remove trailing slash (except for root)
        if (path.length() > 1 && path.endsWith("/")) {
            path = path.substring(0, path.length() - 1);
        }
        
        // Replace multiple slashes with single slash
        while (path.indexOf("//") >= 0) {
            path.replace("//", "/");
        }
        
        return path;
    }
    
} // namespace SDSecurity

#endif // SD_SECURITY_H
