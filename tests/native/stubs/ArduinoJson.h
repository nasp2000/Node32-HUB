#pragma once

#define ARDUINOJSON_VERSION_MAJOR 6

#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <functional>
#include <type_traits>
#include "Arduino.h"

// ============================================================================
// Enriched ArduinoJson stand-in for native tests.
//
// Provides a real (if minimal) JSON DOM so that production persistence logic
// can be exercised natively:
//   - deserializeJson()   real recursive-descent parser
//   - serializeJson()/serializeJsonPretty()  DOM -> string
//   - containsKey() / operator[] / as<T>() / operator|  (ArduinoJson 6 API)
//   - JsonObject / JsonArray iteration
//
// The value storage is a tree of JsonVal nodes; key order is preserved so a
// serialize(deserialize(x)) round-trip is stable.
// ============================================================================

class JsonDocument;

// --- error code (subset used by the firmware) ---
struct DeserializationError {
    enum Code { Ok = 0, InvalidInput = 1, NoMemory = 2, EmptyInput = 3 };
    Code code;
    bool operator==(DeserializationError other) const { return code == other.code; }
    bool operator!=(DeserializationError other) const { return code != other.code; }
    bool operator==(Code c) const { return code == c; }
    bool operator!=(Code c) const { return code != c; }
    // ArduinoJson idiom: `if (!err)` means "no error".
    bool operator!() const { return code == Ok; }
    operator bool() const { return code != Ok; }
};
static const DeserializationError deserializeOk{DeserializationError::Ok};

// --- JSON value node ---
struct JsonVal {
    enum Type { NUL, BOOL, INT, FLOAT, STR, ARR, OBJ };
    Type type = NUL;
    bool   b = false;
    long long i = 0;
    double d = 0;
    std::string s;
    std::vector<JsonVal> arr;
    // preserves key insertion order
    std::vector<std::pair<std::string, JsonVal>> obj;

    bool isNull() const { return type == NUL; }
};

// Forward declarations for the variant class hierarchy.
class JsonVariant;
class JsonVariantConst;
class JsonObjectConst;
class JsonObject;
class JsonArrayConst;
class JsonArray;
class JsonPairConst;

// --- forward parse/serialize ---
DeserializationError deserializeJson(JsonDocument& doc, const std::string& json);
DeserializationError deserializeJson(JsonDocument& doc, const String& json);

// --- JsonVariant: reference into a JsonVal tree ---
class JsonVariant {
public:
    JsonVal* _v = nullptr;

    JsonVariant() {}
    explicit JsonVariant(JsonVal* v) : _v(v) {}

    bool isNull() const { return !_v || _v->isNull(); }
    bool isBool() const { return _v && _v->type == JsonVal::BOOL; }
    bool isInt() const { return _v && (_v->type == JsonVal::INT); }
    bool isFloat() const { return _v && (_v->type == JsonVal::FLOAT); }
    bool isString() const { return _v && _v->type == JsonVal::STR; }
    bool isArray() const { return _v && _v->type == JsonVal::ARR; }
    bool isObject() const { return _v && _v->type == JsonVal::OBJ; }

    template<typename T> T as() const;
    template<typename T> bool is() const;
    template<typename T> T to();

    void set(const char* s) { if (_v) { _v->type = JsonVal::STR; _v->s = s ? s : ""; } }
    void set(const String& s) { set(s.c_str()); }
    void set(bool b) { if (_v) { _v->type = JsonVal::BOOL; _v->b = b; } }
    void set(int i) { if (_v) { _v->type = JsonVal::INT; _v->i = i; } }
    void set(unsigned int i) { if (_v) { _v->type = JsonVal::INT; _v->i = i; } }
    void set(float f) { if (_v) { _v->type = JsonVal::FLOAT; _v->d = f; } }
    void set(double d) { if (_v) { _v->type = JsonVal::FLOAT; _v->d = d; } }
    void set(std::nullptr_t) { if (_v) { *_v = JsonVal(); } }
    void set(const JsonVariantConst& src);

    // ArduinoJson 6 assignment into a node
    JsonVariant& operator=(const char* s) { set(s); return *this; }
    JsonVariant& operator=(const String& s) { set(s); return *this; }
    JsonVariant& operator=(bool b) { set(b); return *this; }
    JsonVariant& operator=(int i) { set(i); return *this; }
    JsonVariant& operator=(unsigned int i) { set(i); return *this; }
    JsonVariant& operator=(float f) { set(f); return *this; }
    JsonVariant& operator=(double d) { set(d); return *this; }
    JsonVariant& operator=(std::nullptr_t) { set(nullptr); return *this; }
    JsonVariant& operator=(const JsonVariantConst& src) { set(src); return *this; }

    bool asBool() const {
        if (!_v) return false;
        switch (_v->type) {
            case JsonVal::BOOL: return _v->b;
            case JsonVal::INT:  return _v->i != 0;
            case JsonVal::FLOAT:return _v->d != 0.0;
            case JsonVal::STR:  return _v->s == "true" || _v->s == "1";
            default: return false;
        }
    }
    long long asInt64() const {
        if (!_v) return 0;
        switch (_v->type) {
            case JsonVal::BOOL: return _v->b ? 1 : 0;
            case JsonVal::INT:  return _v->i;
            case JsonVal::FLOAT:return (long long)_v->d;
            case JsonVal::STR:  return atoll(_v->s.c_str());
            default: return 0;
        }
    }
    double asDouble() const {
        if (!_v) return 0.0;
        switch (_v->type) {
            case JsonVal::BOOL: return _v->b ? 1.0 : 0.0;
            case JsonVal::INT:  return (double)_v->i;
            case JsonVal::FLOAT:return _v->d;
            case JsonVal::STR:  return atof(_v->s.c_str());
            default: return 0.0;
        }
    }
    const char* asCStr() const {
        static std::string cache;
        if (!_v) return "";
        if (_v->type == JsonVal::STR) return _v->s.c_str();
        return "";
    }
    String asString() const {
        if (!_v) return String();
        if (_v->type == JsonVal::STR) return String(_v->s.c_str());
        return String();
    }

    // implicit conversions (ArduinoJson style); template covers small integer
    // types (uint8_t, etc.) that have no dedicated operator.
    template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
    operator T() const { return (T)asDouble(); }
    operator bool() const { return asBool(); }
    operator int() const { return (int)asInt64(); }
    operator unsigned int() const { return (unsigned int)asInt64(); }
    operator long() const { return (long)asInt64(); }
    operator unsigned long() const { return (unsigned long)asInt64(); }
    operator float() const { return (float)asDouble(); }
    operator double() const { return asDouble(); }
    operator const char*() const { return asCStr(); }

    // default-value operator: `json["k"] | fallback`
    template<typename T>
    JsonVariant operator|(const T& fallback) {
        if (isNull()) { static JsonVal v; v = JsonVal(); v.type = JsonVal::INT; v.i = (long long)fallback; _v = &v; }
        return *this;
    }
    JsonVariant operator|(const char* fallback) {
        if (isNull()) { static JsonVal v; v.type = JsonVal::STR; v.s = fallback ? fallback : ""; _v = &v; }
        return *this;
    }
    JsonVariant operator|(const String& fallback) {
        if (isNull()) { static JsonVal v; v.type = JsonVal::STR; v.s = fallback.c_str(); _v = &v; }
        return *this;
    }
    JsonVariant operator|(float fallback) {
        if (isNull()) { static JsonVal v; v.type = JsonVal::FLOAT; v.d = fallback; _v = &v; }
        return *this;
    }
    JsonVariant operator|(double fallback) {
        if (isNull()) { static JsonVal v; v.type = JsonVal::FLOAT; v.d = fallback; _v = &v; }
        return *this;
    }
    JsonVariant operator|(int fallback) {
        if (isNull()) { static JsonVal v; v.type = JsonVal::INT; v.i = fallback; _v = &v; }
        return *this;
    }
    JsonVariant operator|(bool fallback) {
        if (isNull()) { static JsonVal v; v.type = JsonVal::BOOL; v.b = fallback; _v = &v; }
        return *this;
    }
    JsonVariant operator|(unsigned int fallback) {
        if (isNull()) { static JsonVal v; v = JsonVal(); v.type = JsonVal::INT; v.i = (long long)fallback; _v = &v; }
        return *this;
    }

    // navigation into nested object/array (ArduinoJson 6 JsonVariant::operator[])
    JsonVariant operator[](const char* key) {
        if (!_v || _v->type != JsonVal::OBJ) return JsonVariant();
        for (auto& kv : _v->obj)
            if (kv.first == key) return JsonVariant(&kv.second);
        JsonVal nv; _v->obj.push_back({std::string(key ? key : ""), nv});
        return JsonVariant(&_v->obj.back().second);
    }
    JsonVariant operator[](const String& key) { return (*this)[key.c_str()]; }
    JsonVariant operator[](int index) {
        if (!_v || _v->type != JsonVal::ARR || index < 0 ||
            (size_t)index >= _v->arr.size()) return JsonVariant();
        return JsonVariant(&_v->arr[(size_t)index]);
    }
};

// explicit as<T>() specializations
template<> inline bool JsonVariant::as<bool>() const { return asBool(); }
template<> inline int JsonVariant::as<int>() const { return (int)asInt64(); }
template<> inline unsigned int JsonVariant::as<unsigned int>() const { return (unsigned int)asInt64(); }
template<> inline long JsonVariant::as<long>() const { return (long)asInt64(); }
template<> inline unsigned long JsonVariant::as<unsigned long>() const { return (unsigned long)asInt64(); }
template<> inline float JsonVariant::as<float>() const { return (float)asDouble(); }
template<> inline double JsonVariant::as<double>() const { return asDouble(); }
template<> inline const char* JsonVariant::as<const char*>() const { return asCStr(); }
template<> inline String JsonVariant::as<String>() const { return asString(); }

// --- JsonDocument: owns the tree, exposes ArduinoJson 6 accessors ---
class JsonDocument {
public:
    JsonVal _root;

    JsonDocument() {}
    explicit JsonDocument(size_t /*capacity*/) {}

    bool containsKey(const char* key) const {
        for (const auto& kv : _root.obj)
            if (kv.first == key) return true;
        return false;
    }
    bool containsKey(const String& key) const { return containsKey(key.c_str()); }

    JsonVariant operator[](const char* key) {
        if (_root.type != JsonVal::OBJ) {
            _root.type = JsonVal::OBJ; _root.obj.clear();
        }
        for (auto& kv : _root.obj)
            if (kv.first == key) return JsonVariant(&kv.second);
        _root.obj.push_back({std::string(key ? key : ""), JsonVal()});
        return JsonVariant(&_root.obj.back().second);
    }
    JsonVariant operator[](const String& key) { return (*this)[key.c_str()]; }

    bool isNull() const { return _root.isNull(); }
    bool isArray() const { return _root.type == JsonVal::ARR; }
    bool isObject() const { return _root.type == JsonVal::OBJ; }
    template<typename T> bool is() const { return isArray() || isObject(); }
    size_t memoryUsage() const { return _root.obj.size() * 32 + _root.arr.size() * 32; }

    // iteration helpers (defined after JsonArray/JsonObject are complete)
    JsonArray asArray();
    JsonObject asObject();
    JsonArray toArray();
    JsonObject toObject();
    template<typename T> T asT();
    template<typename T> T toT();
    template<typename T> T to();
    template<typename T> T as();

    // ArduinoJson 6 convenience used across the firmware (declared here,
    // defined after JsonVariant is complete).
    JsonArray createNestedArray(const char* key);
    JsonObject createNestedObject(const char* key);
};

// --- JsonVariantConst: const view into a JsonVal tree (ArduinoJson 6 style) ---
class JsonVariantConst {
public:
    const JsonVal* _v = nullptr;
    std::string _raw;   // legacy raw-string payload (LittleFsStore tests)

    JsonVariantConst() {}
    explicit JsonVariantConst(const JsonVal* v) : _v(v) {}
    explicit JsonVariantConst(const char* raw) : _raw(raw ? raw : "") {}
    explicit JsonVariantConst(const std::string& raw) : _raw(raw) {}

    bool isNull() const { return !_v || _v->isNull(); }
    bool isObject() const { return _v && _v->type == JsonVal::OBJ; }
    bool isArray() const { return _v && _v->type == JsonVal::ARR; }
    bool isString() const { return _v && _v->type == JsonVal::STR; }

    template<typename T> bool is() const { return false; }
    template<typename T> T as() const { return T(); }
    template<typename T> T to() const { return T(); }

    bool asBool() const {
        if (!_v) return false;
        switch (_v->type) {
            case JsonVal::BOOL: return _v->b;
            case JsonVal::INT:  return _v->i != 0;
            case JsonVal::FLOAT:return _v->d != 0.0;
            case JsonVal::STR:  return _v->s == "true" || _v->s == "1";
            default: return false;
        }
    }
    long long asInt64() const {
        if (!_v) return 0;
        switch (_v->type) {
            case JsonVal::BOOL: return _v->b ? 1 : 0;
            case JsonVal::INT:  return _v->i;
            case JsonVal::FLOAT:return (long long)_v->d;
            case JsonVal::STR:  return atoll(_v->s.c_str());
            default: return 0;
        }
    }
    double asDouble() const {
        if (!_v) return 0.0;
        switch (_v->type) {
            case JsonVal::BOOL: return _v->b ? 1.0 : 0.0;
            case JsonVal::INT:  return (double)_v->i;
            case JsonVal::FLOAT:return _v->d;
            case JsonVal::STR:  return atof(_v->s.c_str());
            default: return 0.0;
        }
    }
    const char* asCStr() const {
        static std::string cache;
        if (!_v) return "";
        if (_v->type == JsonVal::STR) return _v->s.c_str();
        return "";
    }
    String asString() const {
        if (!_v) return String();
        if (_v->type == JsonVal::STR) return String(_v->s.c_str());
        return String();
    }

    // implicit conversions (ArduinoJson style); template covers small integer
    // types (uint8_t, etc.) that have no dedicated operator.
    template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
    operator T() const { return (T)asDouble(); }
    operator bool() const { return asBool(); }
    operator int() const { return (int)asInt64(); }
    operator unsigned int() const { return (unsigned int)asInt64(); }
    operator long() const { return (long)asInt64(); }
    operator unsigned long() const { return (unsigned long)asInt64(); }
    operator float() const { return (float)asDouble(); }
    operator double() const { return asDouble(); }
    operator const char*() const { return asCStr(); }

    // navigation into nested object/array (ArduinoJson 6 JsonVariantConst::operator[])
    JsonVariantConst operator[](const char* key) const {
        if (!_v || _v->type != JsonVal::OBJ) return JsonVariantConst();
        for (const auto& kv : _v->obj)
            if (kv.first == key) return JsonVariantConst(&kv.second);
        return JsonVariantConst();
    }
    JsonVariantConst operator[](const String& key) const { return (*this)[key.c_str()]; }
    JsonVariantConst operator[](int index) const {
        if (!_v || _v->type != JsonVal::ARR || index < 0 ||
            (size_t)index >= _v->arr.size()) return JsonVariantConst();
        return JsonVariantConst(&_v->arr[(size_t)index]);
    }
};

// --- JsonPairConst: (key, value) yielded when iterating a JsonObjectConst ---
class JsonPairConst {
public:
    const std::string* _key = nullptr;
    const JsonVal* _value = nullptr;
    JsonPairConst() {}
    JsonPairConst(const std::string* k, const JsonVal* v) : _key(k), _value(v) {}
    String key() const { return _key ? String(_key->c_str()) : String(); }
    JsonVariantConst value() const { return JsonVariantConst(_value); }
};

// --- JsonObject / JsonArray wrappers over JsonVal::obj / ::arr ---
class JsonObjectConst {
public:
    const JsonVal* _v = nullptr;
    explicit JsonObjectConst(const JsonVal* v) : _v(v) {}

    class iterator {
    public:
        std::vector<std::pair<std::string, JsonVal>>::const_iterator _it;
        iterator(const std::vector<std::pair<std::string, JsonVal>>::const_iterator& it) : _it(it) {}
        iterator& operator++() { ++_it; return *this; }
        bool operator!=(const iterator& other) const { return _it != other._it; }
        JsonPairConst operator*() const { return JsonPairConst(&_it->first, &_it->second); }
    };
    iterator begin() const { return iterator(_v ? _v->obj.begin() : emptyObj().begin()); }
    iterator end() const { return iterator(_v ? _v->obj.end() : emptyObj().end()); }
private:
    static const std::vector<std::pair<std::string, JsonVal>>& emptyObj() {
        static const std::vector<std::pair<std::string, JsonVal>> e;
        return e;
    }
};

class JsonObject : public JsonObjectConst {
public:
    JsonVal* _mutable = nullptr;
    explicit JsonObject(JsonVal* v) : JsonObjectConst(v), _mutable(v) {}
    JsonObject() : JsonObjectConst(nullptr), _mutable(nullptr) {}

    JsonVariant operator[](const char* key) {
        if (!_mutable) return JsonVariant();
        for (auto& kv : _mutable->obj)
            if (kv.first == key) return JsonVariant(&kv.second);
        _mutable->obj.push_back({std::string(key ? key : ""), JsonVal()});
        return JsonVariant(&_mutable->obj.back().second);
    }
    JsonVariant operator[](const String& key) { return (*this)[key.c_str()]; }

    // ArduinoJson 6: JsonObject is-a JsonVariant (implicit conversion)
    operator JsonVariant() const { return JsonVariant(_mutable); }
};

class JsonArrayConst {
public:
    const JsonVal* _v = nullptr;
    explicit JsonArrayConst(const JsonVal* v) : _v(v) {}

    class iterator {
    public:
        std::vector<JsonVal>::const_iterator _it;
        iterator(const std::vector<JsonVal>::const_iterator& it) : _it(it) {}
        iterator& operator++() { ++_it; return *this; }
        bool operator!=(const iterator& other) const { return _it != other._it; }
        JsonVariantConst operator*() const { return JsonVariantConst(&(*_it)); }
    };
    iterator begin() const { return iterator(_v ? _v->arr.begin() : emptyArr().begin()); }
    iterator end() const { return iterator(_v ? _v->arr.end() : emptyArr().end()); }
private:
    static const std::vector<JsonVal>& emptyArr() {
        static const std::vector<JsonVal> e;
        return e;
    }
};

class JsonArray : public JsonArrayConst {
public:
    JsonVal* _mutable = nullptr;
    explicit JsonArray(JsonVal* v) : JsonArrayConst(v), _mutable(v) {}
    JsonArray() : JsonArrayConst(nullptr), _mutable(nullptr) {}

    // Mutable iteration: `for (JsonVariant v : arr)` on a JsonArray.
    class mutable_iterator {
    public:
        std::vector<JsonVal>::iterator _it;
        mutable_iterator(const std::vector<JsonVal>::iterator& it) : _it(it) {}
        mutable_iterator& operator++() { ++_it; return *this; }
        bool operator!=(const mutable_iterator& other) const { return _it != other._it; }
        JsonVariant operator*() const { return JsonVariant(&(*_it)); }
    };
    mutable_iterator begin() {
        return mutable_iterator(_mutable ? _mutable->arr.begin() : emptyMutableArr().begin());
    }
    mutable_iterator end() {
        return mutable_iterator(_mutable ? _mutable->arr.end() : emptyMutableArr().end());
    }
private:
    static std::vector<JsonVal>& emptyMutableArr() {
        static std::vector<JsonVal> e;
        return e;
    }
public:
    JsonObject createNestedObject() {
        if (!_mutable) return JsonObject();
        _mutable->arr.push_back(JsonVal());
        _mutable->arr.back().type = JsonVal::OBJ;
        return JsonObject(&_mutable->arr.back());
    }
    JsonArray createNestedArray() {
        if (!_mutable) return JsonArray();
        _mutable->arr.push_back(JsonVal());
        _mutable->arr.back().type = JsonVal::ARR;
        return JsonArray(&_mutable->arr.back());
    }
    void add(const JsonVal& node) {
        if (_mutable) _mutable->arr.push_back(node);
    }
    void add(const JsonVariantConst& v) {
        if (_mutable) {
            if (v._v) _mutable->arr.push_back(*v._v);
            else { JsonVal n; n.type = JsonVal::STR; n.s = v._raw; _mutable->arr.push_back(n); }
        }
    }
    void add(const JsonVariant& v) {
        if (_mutable && v._v) _mutable->arr.push_back(*v._v);
    }
    void add(const char* s) {
        if (_mutable) { JsonVal n; n.type = JsonVal::STR; n.s = s ? s : ""; _mutable->arr.push_back(n); }
    }
    void add(const String& s) { add(s.c_str()); }
    void add(bool b) {
        if (_mutable) { JsonVal n; n.type = JsonVal::BOOL; n.b = b; _mutable->arr.push_back(n); }
    }
    void add(int i) {
        if (_mutable) { JsonVal n; n.type = JsonVal::INT; n.i = i; _mutable->arr.push_back(n); }
    }
    void add(float f) {
        if (_mutable) { JsonVal n; n.type = JsonVal::FLOAT; n.d = f; _mutable->arr.push_back(n); }
    }
    void add(double d) {
        if (_mutable) { JsonVal n; n.type = JsonVal::FLOAT; n.d = d; _mutable->arr.push_back(n); }
    }

    // ArduinoJson 6: JsonArray is-a JsonVariant (implicit conversion)
    operator JsonVariant() const { return JsonVariant(_mutable); }
};

// --- serialize ---
inline size_t serializeJson(const JsonVal& v, std::string& out) {
    out.clear();
    switch (v.type) {
        case JsonVal::NUL: out += "null"; break;
        case JsonVal::BOOL: out += v.b ? "true" : "false"; break;
        case JsonVal::INT: out += std::to_string(v.i); break;
        case JsonVal::FLOAT: {
            char buf[32];
            // print with up to 6 significant decimals, trimming trailing zeros
            snprintf(buf, sizeof(buf), "%g", v.d);
            out += buf;
            break;
        }
        case JsonVal::STR: {
            out += '"';
            for (char c : v.s) {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else if ((unsigned char)c < 0x20) {
                    char h[8]; snprintf(h, sizeof(h), "\\u%04X", (unsigned char)c); out += h;
                } else out += c;
            }
            out += '"';
            break;
        }
        case JsonVal::ARR: {
            out += '[';
            for (size_t i = 0; i < v.arr.size(); i++) {
                if (i) out += ',';
                std::string tmp; serializeJson(v.arr[i], tmp); out += tmp;
            }
            out += ']';
            break;
        }
        case JsonVal::OBJ: {
            out += '{';
            for (size_t i = 0; i < v.obj.size(); i++) {
                if (i) out += ',';
                out += '"'; out += v.obj[i].first; out += '"';
                out += ':';
                std::string tmp; serializeJson(v.obj[i].second, tmp); out += tmp;
            }
            out += '}';
            break;
        }
    }
    return out.size();
}

inline size_t serializeJsonPretty(const JsonVal& v, std::string& out) {
    out.clear();
    // pretty printer with 2-space indent
    std::string ind;
    std::function<void(const JsonVal&, int)> dump = [&](const JsonVal& n, int depth) {
        std::string pad(depth * 2, ' ');
        switch (n.type) {
            case JsonVal::NUL: out += "null"; break;
            case JsonVal::BOOL: out += n.b ? "true" : "false"; break;
            case JsonVal::INT: out += std::to_string(n.i); break;
            case JsonVal::FLOAT: {
                char buf[32]; snprintf(buf, sizeof(buf), "%g", n.d); out += buf;
                break;
            }
            case JsonVal::STR: {
                std::string tmp; JsonVal c = n; serializeJson(c, tmp); out += tmp;
                break;
            }
            case JsonVal::ARR: {
                if (n.arr.empty()) { out += "[]"; break; }
                out += "[\n";
                for (size_t i = 0; i < n.arr.size(); i++) {
                    out += std::string((depth + 1) * 2, ' ');
                    dump(n.arr[i], depth + 1);
                    if (i + 1 < n.arr.size()) out += ',';
                    out += '\n';
                }
                out += pad; out += ']';
                break;
            }
            case JsonVal::OBJ: {
                if (n.obj.empty()) { out += "{}"; break; }
                out += "{\n";
                for (size_t i = 0; i < n.obj.size(); i++) {
                    out += std::string((depth + 1) * 2, ' ');
                    out += '"'; out += n.obj[i].first; out += '"';
                    out += ": ";
                    dump(n.obj[i].second, depth + 1);
                    if (i + 1 < n.obj.size()) out += ',';
                    out += '\n';
                }
                out += pad; out += '}';
                break;
            }
        }
    };
    dump(v, 0);
    return out.size();
}

// --- parse (recursive descent) ---
namespace {
struct JsonParser {
    const std::string& s;
    size_t p = 0;
    bool err = false;

    JsonParser(const std::string& str) : s(str) {}

    void skipWs() { while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++; }

    bool ok() const { return p < s.size(); }

    JsonVal parseValue() {
        skipWs();
        if (!ok()) { err = true; return JsonVal(); }
        char c = s[p];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') { JsonVal v; v.type = JsonVal::STR; v.s = parseString(); return v; }
        if (c == 't' || c == 'f') {
            JsonVal v; v.type = JsonVal::BOOL;
            v.b = (s.compare(p, 4, "true") == 0);
            p += v.b ? 4 : 5;
            return v;
        }
        if (c == 'n') { p += 4; return JsonVal(); }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        err = true;   // unexpected character
        return JsonVal();
    }

    JsonVal parseObject() {
        JsonVal v; v.type = JsonVal::OBJ;
        p++; // '{'
        skipWs();
        if (ok() && s[p] == '}') { p++; return v; }
        bool closed = false;
        while (ok() && !err) {
            skipWs();
            if (!ok() || s[p] != '"') { err = true; break; }  // key must be quoted
            std::string key = parseString();
            skipWs();
            if (!ok() || s[p] != ':') { err = true; break; }  // key must be followed by ':'
            p++;
            v.obj.push_back({key, parseValue()});
            if (err) break;
            skipWs();
            if (ok() && s[p] == ',') { p++; continue; }
            if (ok() && s[p] == '}') { p++; closed = true; break; }
            err = true;   // expected ',' or '}'
            break;
        }
        if (!err && !closed) err = true;   // unterminated object
        return v;
    }

    JsonVal parseArray() {
        JsonVal v; v.type = JsonVal::ARR;
        p++; // '['
        skipWs();
        if (ok() && s[p] == ']') { p++; return v; }
        bool closed = false;
        while (ok() && !err) {
            v.arr.push_back(parseValue());
            if (err) break;
            skipWs();
            if (ok() && s[p] == ',') { p++; continue; }
            if (ok() && s[p] == ']') { p++; closed = true; break; }
            err = true;   // expected ',' or ']'
            break;
        }
        if (!err && !closed) err = true;   // unterminated array
        return v;
    }

    std::string parseString() {
        std::string out;
        if (!ok() || s[p] != '"') { err = true; return out; }
        p++; // opening quote
        while (ok() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                char esc = s[p + 1];
                switch (esc) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (p + 5 < s.size()) {
                            unsigned int code = 0;
                            sscanf(s.c_str() + p + 2, "%4x", &code);
                            // only ASCII subset is needed; encode >=0x80 as raw bytes for pt-BR
                            if (code < 0x80) out += (char)code;
                            else out += '?';
                            p += 4;
                        }
                        break;
                    }
                    default: out += esc; break;
                }
                p += 2;
            } else {
                out += s[p];
                p++;
            }
        }
        if (ok() && s[p] == '"') p++; // closing quote
        return out;
    }

    JsonVal parseNumber() {
        JsonVal v;
        char* end = nullptr;
        const char* start = s.c_str() + p;
        // detect float vs int by scanning for . or e/E
        const char* q = start;
        bool isFloat = false;
        while (*q && (isdigit(*q) || *q == '-' || *q == '+' || *q == '.' || *q == 'e' || *q == 'E')) {
            if (*q == '.' || *q == 'e' || *q == 'E') isFloat = true;
            q++;
        }
        size_t consumed = (size_t)(q - start);
        if (isFloat) {
            v.type = JsonVal::FLOAT;
            v.d = strtod(start, &end);
        } else {
            v.type = JsonVal::INT;
            v.i = strtoll(start, &end, 10);
        }
        p += consumed;
        return v;
    }
};
} // namespace

inline DeserializationError deserializeJson(JsonDocument& doc, const std::string& json) {
    if (json.find_first_not_of(" \t\r\n") == std::string::npos)
        return DeserializationError{DeserializationError::EmptyInput};
    JsonParser parser(json);
    JsonVal root = parser.parseValue();
    if (parser.err)
        return DeserializationError{DeserializationError::InvalidInput};
    if (!parser.err && !json.empty()) {
        std::string rest = json.substr(parser.p);
        if (rest.find_first_not_of(" \t\r\n") != std::string::npos)
            return DeserializationError{DeserializationError::InvalidInput};  // trailing garbage
    }
    doc._root = root;
    return deserializeOk;
}
inline DeserializationError deserializeJson(JsonDocument& doc, const String& json) {
    return deserializeJson(doc, std::string(json.c_str()));
}

// --- conversions for doc.asArray() / doc.asObject() / doc.to<> / doc.as<> ---
// asT<T>: array semantics (used by doc.as<JsonArray>(), doc.asArray(), doc.toArray())
template<typename T>
inline T JsonDocument::asT() {
    if (_root.type != JsonVal::ARR) {
        _root.type = JsonVal::ARR; _root.arr.clear();
    }
    return T(&_root);
}
// toT<T>: object semantics (used by doc.to<JsonObject>(), doc.asObject(), doc.toObject())
template<typename T>
inline T JsonDocument::toT() {
    if (_root.type != JsonVal::OBJ) {
        _root.type = JsonVal::OBJ; _root.obj.clear();
    }
    return T(&_root);
}
inline JsonArray JsonDocument::asArray() { return asT<JsonArray>(); }
inline JsonObject JsonDocument::asObject() { return toT<JsonObject>(); }
inline JsonArray JsonDocument::toArray() { return asT<JsonArray>(); }
inline JsonObject JsonDocument::toObject() { return toT<JsonObject>(); }

// ArduinoJson 6: doc.to<JsonArray>()/doc.as<JsonArray>() use array semantics,
// doc.to<JsonObject>()/doc.as<JsonObject>() use object semantics.
template<> inline JsonArray  JsonDocument::to<JsonArray>()  { return asT<JsonArray>(); }
template<> inline JsonObject JsonDocument::to<JsonObject>() { return toT<JsonObject>(); }
template<> inline JsonArray  JsonDocument::as<JsonArray>()  { return asT<JsonArray>(); }
template<> inline JsonObject JsonDocument::as<JsonObject>() { return toT<JsonObject>(); }

// --- SpiRamJsonDocument / DynamicJsonDocument aliases ---
class SpiRamJsonDocument : public JsonDocument {
public:
    SpiRamJsonDocument() {}
    explicit SpiRamJsonDocument(size_t capacity) : JsonDocument(capacity) {}
};
class DynamicJsonDocument : public JsonDocument {
public:
    DynamicJsonDocument() {}
    explicit DynamicJsonDocument(size_t capacity) : JsonDocument(capacity) {}
};

// --- serialize a DOM JsonDocument into a String or any Print-like object ---
inline size_t serializeJson(const JsonDocument& doc, String& out) {
    std::string tmp;
    serializeJson(doc._root, tmp);
    out = String(tmp.c_str());
    return tmp.size();
}
template <typename TPrint>
inline size_t serializeJson(const JsonDocument& doc, TPrint& out) {
    std::string tmp;
    serializeJson(doc._root, tmp);
    return out.print(tmp.c_str());
}
template <typename TPrint>
inline size_t serializeJsonPretty(const JsonDocument& doc, TPrint& out) {
    std::string tmp;
    serializeJsonPretty(doc._root, tmp);
    return out.print(tmp.c_str());
}

// --- serialize a JsonVariantConst (DOM node if present, else legacy raw) ---
inline size_t serializeJson(JsonVariantConst v, String& out) {
    if (v._v) { std::string tmp; serializeJson(*v._v, tmp); out = String(tmp.c_str()); return tmp.size(); }
    out = String(v._raw.c_str());
    return v._raw.size();
}
template <typename TPrint>
inline size_t serializeJsonPretty(JsonVariantConst v, TPrint& out) {
    if (v._v) { std::string tmp; serializeJsonPretty(*v._v, tmp); return out.print(tmp.c_str()); }
    return out.print(v._raw.c_str());
}

// --- JsonVariantConst type checks / conversions (ArduinoJson 6 API) ---
template<> inline bool JsonVariantConst::is<JsonObjectConst>() const { return isObject(); }
template<> inline bool JsonVariantConst::is<JsonArrayConst>() const { return isArray(); }
template<> inline bool JsonVariantConst::is<const char*>() const { return isString(); }
template<> inline bool JsonVariantConst::is<bool>() const { return !isNull(); }
template<> inline JsonObjectConst JsonVariantConst::as<JsonObjectConst>() const { return JsonObjectConst(_v); }
template<> inline JsonArrayConst JsonVariantConst::as<JsonArrayConst>() const { return JsonArrayConst(_v); }
template<> inline const char* JsonVariantConst::as<const char*>() const { return asCStr(); }
template<> inline bool JsonVariantConst::as<bool>() const { return asBool(); }
template<> inline int JsonVariantConst::as<int>() const { return (int)asInt64(); }
template<> inline unsigned int JsonVariantConst::as<unsigned int>() const { return (unsigned int)asInt64(); }
template<> inline long JsonVariantConst::as<long>() const { return (long)asInt64(); }
template<> inline float JsonVariantConst::as<float>() const { return (float)asDouble(); }
template<> inline double JsonVariantConst::as<double>() const { return asDouble(); }
template<> inline String JsonVariantConst::as<String>() const { return asString(); }

// --- JsonVariant::is<T>() / as<T>() / to<T>() container specializations ---
template<> inline bool JsonVariant::is<JsonObjectConst>() const { return isObject(); }
template<> inline bool JsonVariant::is<JsonArrayConst>() const { return isArray(); }
template<> inline bool JsonVariant::is<const char*>() const { return isString(); }
template<> inline bool JsonVariant::is<JsonObject>() const { return isObject(); }
template<> inline bool JsonVariant::is<JsonArray>() const { return isArray(); }
template<> inline JsonObjectConst JsonVariant::as<JsonObjectConst>() const { return JsonObjectConst(_v); }
template<> inline JsonArrayConst JsonVariant::as<JsonArrayConst>() const { return JsonArrayConst(_v); }
template<> inline JsonObject JsonVariant::as<JsonObject>() const { return JsonObject(_v); }
template<> inline JsonArray JsonVariant::as<JsonArray>() const { return JsonArray(_v); }
template<> inline JsonObject JsonVariant::to<JsonObject>() {
    if (!_v) { static JsonVal v; _v = &v; }
    if (_v->type != JsonVal::OBJ) { _v->type = JsonVal::OBJ; _v->obj.clear(); }
    return JsonObject(_v);
}
template<> inline JsonArray JsonVariant::to<JsonArray>() {
    if (!_v) { static JsonVal v; _v = &v; }
    if (_v->type != JsonVal::ARR) { _v->type = JsonVal::ARR; _v->arr.clear(); }
    return JsonArray(_v);
}

// --- JsonVariant::set(const JsonVariantConst&) deep copy ---
inline void JsonVariant::set(const JsonVariantConst& src) {
    if (!_v) return;
    if (src._v) { *_v = *src._v; }
    else { _v->type = JsonVal::STR; _v->s = src._raw; }
}

// --- JsonDocument nested-container helpers (defined last: need the
// JsonVariant::to<T>() specializations above) ---
inline JsonArray JsonDocument::createNestedArray(const char* key) { return (*this)[key].to<JsonArray>(); }
inline JsonObject JsonDocument::createNestedObject(const char* key) { return (*this)[key].to<JsonObject>(); }
