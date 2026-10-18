#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <cctype>

typedef uint8_t byte;
typedef bool boolean;

#define PROGMEM
#define PGM_P const char*
#define PSTR(x) x
#define pgm_read_byte(x) (*(const uint8_t*)(x))
#define pgm_read_word(x) (*(const uint16_t*)(x))
#define pgm_read_dword(x) (*(const uint32_t*)(x))
#define pgm_read_float(x) (*(const float*)(x))
#define strlen_P strlen
#define strcmp_P strcmp
#define strncpy_P strncpy
#define sprintf_P sprintf
#define vsnprintf_P vsnprintf

#define isDigit(c) ((c) >= '0' && (c) <= '9')

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

// F()/FPSTR: on host, PROGMEM strings live in ordinary RAM.
#define F(x) x
#define FPSTR(s) (reinterpret_cast<const char*>(s))
class __FlashStringHelper;

class String {
public:
    String() : _impl() {}
    String(const char* s) : _impl(s ? s : "") {}
    String(const std::string& s) : _impl(s) {}
    String(const String& other) : _impl(other._impl) {}
    const char* c_str() const { return _impl.c_str(); }
    size_t length() const { return _impl.length(); }
    char charAt(int idx) const { return (idx >= 0 && (size_t)idx < _impl.length()) ? _impl[idx] : '\0'; }
    int indexOf(const String& s) const { size_t p = _impl.find(s._impl); return (p != std::string::npos) ? (int)p : -1; }
    int indexOf(const char* s) const { size_t p = _impl.find(s); return (p != std::string::npos) ? (int)p : -1; }
    String substring(int begin, int end) const { return String(_impl.substr(begin, end - begin)); }
    String substring(int begin) const { return String(_impl.substr(begin)); }
    double toDouble() const { return atof(_impl.c_str()); }
    float toFloat() const { return (float)atof(_impl.c_str()); }
    // numeric constructors (int/float with precision, as used by sensor configs)
    String(int v) : _impl(std::to_string(v)) {}
    String(unsigned int v) : _impl(std::to_string(v)) {}
    String(long v) : _impl(std::to_string(v)) {}
    String(unsigned long v) : _impl(std::to_string(v)) {}
    String(float v, unsigned int dec) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%.*f", (int)dec, (double)v);
        _impl = buf;
    }
    String(double v, unsigned int dec) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%.*f", (int)dec, v);
        _impl = buf;
    }
    bool endsWith(const String& suffix) const {
        if (suffix.length() > _impl.length()) return false;
        return _impl.compare(_impl.length() - suffix.length(), suffix.length(), suffix._impl) == 0;
    }
    void remove(int idx) { if (idx >= 0 && (size_t)idx < _impl.length()) _impl.erase(idx); }
    void trim() {
        size_t s = _impl.find_first_not_of(" \t\r\n");
        size_t e = _impl.find_last_not_of(" \t\r\n");
        if (s == std::string::npos) _impl.clear();
        else _impl = _impl.substr(s, e - s + 1);
    }
    String& operator+=(char c) { _impl += c; return *this; }
    String& operator+=(const char* s) { _impl += s; return *this; }
    String& operator+=(const String& s) { _impl += s._impl; return *this; }
    String& operator+=(int v) { _impl += std::to_string(v); return *this; }
    String& operator+=(unsigned int v) { _impl += std::to_string(v); return *this; }
    String& operator+=(long v) { _impl += std::to_string(v); return *this; }
    String& operator+=(unsigned long v) { _impl += std::to_string(v); return *this; }
    String operator+(const String& rhs) const { return String(_impl + rhs._impl); }
    int compareTo(const String& other) const { return _impl.compare(other._impl); }
    bool operator==(const String& rhs) const { return _impl == rhs._impl; }
    bool operator!=(const String& rhs) const { return _impl != rhs._impl; }
    String& operator=(const String& other) { _impl = other._impl; return *this; }
    String& operator=(const char* s) { _impl = s ? s : ""; return *this; }
    char operator[](int idx) const { return charAt(idx); }
    bool equalsIgnoreCase(const String& other) const {
        if (_impl.length() != other._impl.length()) return false;
        for (size_t i = 0; i < _impl.length(); i++) {
            if (toupper((unsigned char)_impl[i]) != toupper((unsigned char)other._impl[i])) return false;
        }
        return true;
    }
    int toInt() const {
        if (_impl.empty()) return 0;
        return (int)strtol(_impl.c_str(), nullptr, 10);
    }
    int toInt(int defaultValue) const {
        if (_impl.empty()) return defaultValue;
        char* end = nullptr;
        long v = strtol(_impl.c_str(), &end, 10);
        return (end == _impl.c_str()) ? defaultValue : (int)v;
    }
    bool isEmpty() const { return _impl.empty(); }
    void reserve(size_t /*capacity*/) {}
    bool concat(const char* s) {
        if (!s) return false;
        _impl += s;
        return true;
    }
    bool concat(const String& s) { _impl += s._impl; return true; }
    void toLowerCase() {
        for (size_t i = 0; i < _impl.length(); i++) {
            char c = _impl[i];
            if (c >= 'A' && c <= 'Z') _impl[i] = (char)(c - 'A' + 'a');
        }
    }
    void toUpperCase() {
        for (size_t i = 0; i < _impl.length(); i++) {
            char c = _impl[i];
            if (c >= 'a' && c <= 'z') _impl[i] = (char)(c - 'a' + 'A');
        }
    }
private:
    std::string _impl;
};

inline String operator+(const char* lhs, const String& rhs) { return String(lhs) + rhs; }
inline String operator+(const String& lhs, const char* rhs) { return lhs + String(rhs); }

inline void delay(unsigned long ms) { (void)ms; }

// millis() for host — sensor_config_base.h scheduling uses it.
inline unsigned long millis() { return 0; }

// Print base — the arduino-esp32 supertype that ArduinoJson's Print writer and
// the Stream reader expect. `write` is the only pure method.
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* buf, size_t n) {
        size_t w = 0;
        for (size_t i = 0; i < n; i++) w += write(buf[i]);
        return w;
    }
    size_t print(const char* s) {
        if (!s) return 0;
        return write((const uint8_t*)s, strlen(s));
    }
    size_t print(const String& s) { return write((const uint8_t*)s.c_str(), s.length()); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { char b[16]; snprintf(b, sizeof(b), "%d", v); return print(b); }
    size_t print(unsigned int v) { char b[16]; snprintf(b, sizeof(b), "%u", v); return print(b); }
    size_t print(long v) { char b[16]; snprintf(b, sizeof(b), "%ld", v); return print(b); }
    size_t print(unsigned long v, int base = 10) {
        char b[24];
        if (base == 16) snprintf(b, sizeof(b), "%lX", v);
        else if (base == 8) snprintf(b, sizeof(b), "%lo", v);
        else snprintf(b, sizeof(b), "%lu", v);
        return print(b);
    }
    size_t print(double v, int digits = 2) {
        char b[40];
        snprintf(b, sizeof(b), "%.*f", digits, v);
        return print(b);
    }
    size_t println() { return write((const uint8_t*)"\n", 1); }
    size_t println(const char* s) { size_t w = print(s); w += println(); return w; }
    size_t println(const String& s) { size_t w = print(s); w += println(); return w; }
    size_t println(int v) { size_t w = print(v); w += println(); return w; }
    size_t println(unsigned long v, int base = 10) { size_t w = print(v, base); w += println(); return w; }
};

class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    size_t readBytes(char* buffer, size_t length) {
        size_t n = 0;
        while (n < length) {
            int c = read();
            if (c < 0) break;
            buffer[n++] = (char)c;
        }
        return n;
    }
};

// Arduino "Printable" base expected by the ArduinoJson Print/Printable adapters.
class Printable {
public:
    virtual ~Printable() {}
    virtual size_t printTo(Print& p) const = 0;
};

// ESP32 core API subset used by ConfigManager diagnostics.
class ESPClass {
public:
    static uint32_t getFreeHeap() { return 1024ul * 1024; }
    static uint32_t getHeapSize() { return 1024ul * 1024; }
};
extern ESPClass _espInstance;
#define ESP _espInstance

// Minimal Serial stub (printf only — used by LittleFsStore.cpp logging).
struct SerialStub {
    void printf(const char*, ...) {}
    size_t print(const char* s) { return s ? strlen(s) : 0; }
    size_t print(const String& s) { return print(s.c_str()); }
    size_t print(int v) { char b[16]; snprintf(b, sizeof(b), "%d", v); return print(b); }
    size_t print(unsigned int v) { char b[16]; snprintf(b, sizeof(b), "%u", v); return print(b); }
    size_t print(long v) { char b[16]; snprintf(b, sizeof(b), "%ld", v); return print(b); }
    size_t print(unsigned long v, int base = 10) {
        char b[24];
        if (base == 16) snprintf(b, sizeof(b), "%lX", v);
        else if (base == 8) snprintf(b, sizeof(b), "%lo", v);
        else snprintf(b, sizeof(b), "%lu", v);
        return print(b);
    }
    size_t println() { return 1; }
    size_t println(const char* s) { size_t w = print(s); return w + println(); }
    size_t println(const String& s) { size_t w = print(s); return w + println(); }
    size_t println(int v) { size_t w = print(v); return w + println(); }
    size_t println(unsigned int v) { size_t w = print(v); return w + println(); }
    size_t println(unsigned long v, int base = 10) { size_t w = print(v, base); return w + println(); }
};
extern SerialStub Serial;
