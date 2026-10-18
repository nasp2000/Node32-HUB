#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Test-double for ILogger — captures log entries for verification.
// Does NOT inherit from the production interface (avoids Arduino dependency).
class MockLogger {
public:
    struct Entry {
        std::string level;
        std::string module;
        std::string message;
    };

private:
    std::vector<Entry> entries_;
    bool enabled_ = true;

public:
    void setEnabled(bool e) { enabled_ = e; }
    bool isEnabled() const { return enabled_; }

    void log(const std::string& level, const std::string& module, const std::string& message) {
        if (!enabled_) return;
        Entry e;
        e.level = level;
        e.module = module;
        e.message = message;
        entries_.push_back(e);
    }

    // Convenience wrappers
    void debug(const std::string& module, const std::string& msg) { log("DEBUG", module, msg); }
    void info(const std::string& module, const std::string& msg)  { log("INFO", module, msg); }
    void warn(const std::string& module, const std::string& msg)  { log("WARN", module, msg); }
    void error(const std::string& module, const std::string& msg) { log("ERROR", module, msg); }

    // Test helpers
    size_t entryCount() const { return entries_.size(); }
    const Entry& lastEntry() const {
        static Entry empty;
        return entries_.empty() ? empty : entries_.back();
    }
    const Entry& entryAt(size_t i) const {
        static Entry empty;
        return i < entries_.size() ? entries_[i] : empty;
    }
    void clear() { entries_.clear(); }
};
