#pragma once
#include <map>
#include <string>
#include "Arduino.h"

// Minimal in-memory stand-in for ESP32 LittleFS used to compile the real
// LittleFsStore.cpp / ConfigManager.cpp natively. Backed by a
// std::map<string,string> so the test can inspect file contents and inject
// open/rename failures.
//
// File derives from Stream so the real ArduinoJson library (v7, header-only,
// enabled for the real-ConfigManager tests) can serializeJson()/deserializeJson()
// through it exactly like the board does.

class File : public Stream {
public:
    File() : _writeBuf(nullptr), _readBuf(nullptr), _ok(false), _pos(0) {}
    File(std::string* writeBuf, const std::string* readBuf, bool ok)
        : _writeBuf(writeBuf), _readBuf(readBuf), _ok(ok), _pos(0) {}

    explicit operator bool() const { return _ok; }
    bool operator!() const { return !_ok; }

    size_t write(uint8_t c) override {
        if (!_ok || !_writeBuf) return 0;
        _writeBuf->push_back((char)c);
        _readBuf = _writeBuf;
        return 1;
    }
    size_t write(const uint8_t* buf, size_t n) override {
        if (!_ok || !_writeBuf) return 0;
        _writeBuf->append((const char*)buf, n);
        _readBuf = _writeBuf;
        return n;
    }

    int available() override {
        if (!_readBuf) return 0;
        return (int)(_readBuf->size() - _pos);
    }
    int read() override {
        if (!_readBuf || _pos >= _readBuf->size()) return -1;
        return (unsigned char)((*_readBuf)[_pos++]);
    }
    int peek() override {
        if (!_readBuf || _pos >= _readBuf->size()) return -1;
        return (unsigned char)((*_readBuf)[_pos]);
    }

    size_t size() const { return _readBuf ? _readBuf->size() : 0; }
    size_t position() const { return _pos; }

    size_t print(const char* s) {
        if (!_ok || !_writeBuf || !s) return 0;
        return write((const uint8_t*)s, strlen(s));
    }
    size_t print(const String& s) { return print(s.c_str()); }

    void close() { _ok = false; }

    String readString() const {
        if (!_readBuf) return String();
        return String(_readBuf->c_str());
    }

private:
    std::string* _writeBuf;
    const std::string* _readBuf;
    bool _ok;
    size_t _pos;
};

class LittleFSClass {
public:
    std::map<std::string, std::string> files;

    // Failure injection hooks
    bool failOpen = false;
    bool failRename = false;

    bool exists(const String& p) const {
        return files.count(p.c_str()) != 0;
    }

    File open(const String& p, const char* mode) {
        if (failOpen) return File();
        std::string key(p.c_str());
        if (mode && mode[0] == 'w') {
            files[key].clear(); // truncate like "w" mode
            return File(&files[key], nullptr, true);
        }
        auto it = files.find(key);
        if (it == files.end()) return File();
        return File(nullptr, &it->second, true);
    }

    bool remove(const String& p) {
        return files.erase(p.c_str()) > 0;
    }

    bool rename(const String& oldPath, const String& newPath) {
        if (failRename) return false;
        auto it = files.find(oldPath.c_str());
        if (it == files.end()) return false;
        files[newPath.c_str()] = it->second;
        files.erase(it);
        return true;
    }

    bool mkdir(const char*) { return true; }
};

extern LittleFSClass LittleFS;