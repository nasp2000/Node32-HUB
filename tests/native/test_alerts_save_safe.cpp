#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <map>
#include <string>
#include <vector>

// Standalone reimplementation of the 2026-08 alerts.json corruption fix in
// ConfigManager.cpp. Production maps:
//   - copyJsonCleanControlChars()  (saveSection("alerts") sanitizes the WHOLE
//     inactivity_alerts object before serialize, not just emails in the handler)
//   - writeJsonAtomic()            (re-reads the .tmp and deserializeJson()s it
//     BEFORE renaming, so a truncated/raw-control tmp can never replace a good file)
//   - alerts load block            (a corrupt alerts.json is healed immediately
//     with the in-memory defaults instead of surviving until the next save)
// The JSON parser/serializer below deliberately mirrors ArduinoJson's behavior:
// control bytes < 0x20 are INVALID inside a string on read, and on write only
// \b \f \n \r \t and NUL are escaped (every other byte < 0x20 goes out raw).

namespace {

// Minimal JSON value tree mirroring the alert document shape.
struct JVal {
    enum Type { NUL, BOOL, INT, STR, OBJ } type = NUL;
    bool b = false;
    long long i = 0;
    std::string s;
    std::vector<std::pair<std::string, JVal>> obj;

    static JVal nullV() { return JVal{}; }
    static JVal boolV(bool v) { JVal j; j.type = BOOL; j.b = v; return j; }
    static JVal intV(long long v) { JVal j; j.type = INT; j.i = v; return j; }
    static JVal strV(std::string v) { JVal j; j.type = STR; j.s = std::move(v); return j; }
    static JVal objV() { JVal j; j.type = OBJ; return j; }
};

// ---- strict JSON parser (mirrors ArduinoJson deserializeJson) --------------

struct ParseResult {
    bool ok = false;
    JVal root;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : s_(src) {}

    ParseResult parse() {
        ParseResult r;
        skipWs();
        JVal v;
        if (!parseValue(v)) return r;
        skipWs();
        r.ok = (pos_ == s_.size());
        r.root = std::move(v);
        return r;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    bool raw(char c) { return pos_ < s_.size() && s_[pos_] == c; }

    void skipWs() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool parseValue(JVal& out) {
        skipWs();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') { std::string str; if (!parseString(str)) return false; out = JVal::strV(std::move(str)); return true; }
        if (c == 't') { if (s_.substr(pos_, 4) != "true") return false; pos_ += 4; out = JVal::boolV(true); return true; }
        if (c == 'f') { if (s_.substr(pos_, 5) != "false") return false; pos_ += 5; out = JVal::boolV(false); return true; }
        if (c == 'n') { if (s_.substr(pos_, 4) != "null") return false; pos_ += 4; out = JVal::nullV(); return true; }
        return parseNumber(out);
    }

    bool parseObject(JVal& out) {
        JVal o = JVal::objV();
        pos_++;  // '{'
        skipWs();
        if (raw('}')) { pos_++; out = std::move(o); return true; }
        while (true) {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') return false;
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (!raw(':')) return false;
            pos_++;
            JVal v;
            if (!parseValue(v)) return false;
            o.obj.push_back({std::move(key), std::move(v)});
            skipWs();
            if (raw(',')) { pos_++; continue; }
            if (raw('}')) { pos_++; break; }
            return false;
        }
        out = std::move(o);
        return true;
    }

    bool parseArray(JVal& out) {
        JVal a;
        a.type = JVal::OBJ;   // arrays reuse OBJ storage; items live under "%item"
        a.s = "ARRAY";
        pos_++;  // '['
        skipWs();
        if (raw(']')) { pos_++; out = std::move(a); return true; }
        while (true) {
            skipWs();
            JVal v;
            if (!parseValue(v)) return false;
            a.obj.push_back({"%item", std::move(v)});
            skipWs();
            if (raw(',')) { pos_++; continue; }
            if (raw(']')) { pos_++; break; }
            return false;
        }
        out = std::move(a);
        return true;
    }

    bool parseString(std::string& out) {
        pos_++;  // opening '"'
        while (pos_ < s_.size()) {
            unsigned char ch = (unsigned char)next();
            if (ch == '"') return true;
            if (ch == '\\') {
                if (pos_ >= s_.size()) return false;
                unsigned char e = (unsigned char)next();
                switch (e) {
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) return false;
                        unsigned val = 0;
                        for (int k = 0; k < 4; ++k) {
                            unsigned char h = (unsigned char)s_[pos_++];
                            if (h >= '0' && h <= '9') val = (val << 4) | (h - '0');
                            else if (h >= 'a' && h <= 'f') val = (val << 4) | (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') val = (val << 4) | (h - 'A' + 10);
                            else return false;
                        }
                        out += (char)(unsigned char)(val & 0xFF);
                        break;
                    }
                    default: return false;  // unknown escape
                }
            } else if (ch < 0x20) {
                return false;  // raw control byte inside a string -> parse error
            } else {
                out += (char)ch;
            }
        }
        return false;  // unterminated string
    }

    bool parseNumber(JVal& out) {
        size_t start = pos_;
        while (pos_ < s_.size() &&
               (s_[pos_] == '-' || s_[pos_] == '+' ||
                (s_[pos_] >= '0' && s_[pos_] <= '9') ||
                s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
        }
        if (pos_ == start) return false;
        out = JVal::intV(0);  // exact numeric value is irrelevant here
        return true;
    }

    int next() { return pos_ < s_.size() ? s_[pos_++] : -1; }
};

bool parseOk(const std::string& src) {
    return JsonParser(src).parse().ok;
}

// ---- serializer mirroring ArduinoJson TextFormatter ------------------------
// Escapes \b \f \n \r \t " \ and NUL; every other byte (< 0x20 included) is
// written RAW — exactly the behavior that turned stray control bytes into a
// corrupt file.

std::string escapeChar(char c) {
    switch (c) {
        case '\b': return "\\b";
        case '\f': return "\\f";
        case '\n': return "\\n";
        case '\r': return "\\r";
        case '\t': return "\\t";
        case '"':  return "\\\"";
        case '\\': return "\\\\";
        case '\0': return "\\u0000";
        default:   return std::string(1, c);
    }
}

std::string serializeAlerts(const JVal& root) {
    if (root.type == JVal::OBJ) {
        bool isArray = (root.s == "ARRAY");
        std::string out = isArray ? "[" : "{";
        bool first = true;
        for (const auto& kv : root.obj) {
            if (!first) out += ",";
            first = false;
            if (!isArray) {
                out += "\"";
                out += kv.first;
                out += "\":";
            }
            out += serializeAlerts(kv.second);
        }
        out += isArray ? "]" : "}";
        return out;
    }
    if (root.type == JVal::STR) {
        std::string out = "\"";
        for (char c : root.s) out += escapeChar(c);
        out += "\"";
        return out;
    }
    if (root.type == JVal::BOOL) return root.b ? "true" : "false";
    if (root.type == JVal::INT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", root.i);
        return buf;
    }
    return "null";
}

// ---- mirror of Configuration copyJsonCleanControlChars() -------------------

bool isLegalControl(char c) { return c == '\n' || c == '\r' || c == '\t'; }

void sanitizeJson(JVal& v) {
    if (v.type == JVal::STR) {
        std::string clean;
        for (char c : v.s) {
            unsigned char uc = (unsigned char)c;
            if (uc >= 0x20 || isLegalControl(c)) clean += c;
        }
        v.s = std::move(clean);
    } else if (v.type == JVal::OBJ) {
        for (auto& kv : v.obj) sanitizeJson(kv.second);
    }
}

// ---- mirror of writeJsonAtomic() with the new re-parse verification --------

struct VirtualFs {
    std::map<std::string, std::string> files;
    bool failOpen = false;
    bool failRename = false;

    bool exists(const std::string& p) const { return files.count(p) != 0; }
    std::string read(const std::string& p) const {
        auto it = files.find(p);
        return it == files.end() ? std::string() : it->second;
    }
};

std::string tmpOf(const std::string& finalPath) { return finalPath + ".tmp"; }

// Mirrors the NEW production writeJsonAtomic(): write tmp -> bytes>0 ->
// re-parse tmp as JSON -> only then remove final + rename.
bool atomicWriteVerified(VirtualFs& fs, const std::string& finalPath,
                         const std::string& content, bool failOpen = false,
                         bool failRename = false) {
    const std::string tmpPath = tmpOf(finalPath);
    if (failOpen) return false;
    fs.files[tmpPath] = content;
    if (content.empty()) {
        fs.files.erase(tmpPath);
        return false;
    }
    // JSON verification of the .tmp BEFORE it can replace anything.
    if (!parseOk(content)) {
        fs.files.erase(tmpPath);
        return false;
    }
    if (failRename) {
        fs.files.erase(tmpPath);
        return false;
    }
    fs.files.erase(finalPath);
    fs.files[finalPath] = content;
    fs.files.erase(tmpPath);
    return true;
}

// ---- mirror of the alerts load block: heal a corrupt file with defaults ----

std::string defaultAlertsJson() {
    JVal slots;
    slots.type = JVal::OBJ;
    slots.s = "ARRAY";
    for (int i = 0; i < 6; ++i) {
        JVal slot = JVal::objV();
        slot.obj.push_back({"enabled", JVal::boolV(false)});
        slot.obj.push_back({"timeout_minutes", JVal::intV(20)});
        slot.obj.push_back({"max_per_day", JVal::intV(12)});
        slot.obj.push_back({"emails", JVal::strV("")});
        slots.obj.push_back({"%item", std::move(slot)});
    }
    JVal root = JVal::objV();
    root.obj.push_back({"sensors_reset_timer", JVal::boolV(false)});
    root.obj.push_back({"slots", std::move(slots)});
    return serializeAlerts(root);
}

// Mirrors loadFromLittleFS alerts block: a corrupt file gets rewritten from
// memory; a valid (or missing) file is left untouched.
bool healOnLoad(VirtualFs& fs, const std::string& path, bool& healed) {
    healed = false;
    if (!fs.exists(path)) return true;
    if (!parseOk(fs.read(path))) {
        atomicWriteVerified(fs, path, defaultAlertsJson());
        healed = true;
    }
    return parseOk(fs.read(path));
}

// Mirrors recoverOrRemoveTmpPath() for alerts.json: a .tmp is only promoted
// over a missing final if it parses as JSON; otherwise it is discarded. When a
// final exists, a stale .tmp is always removed (old behavior for other stems).
void recoverTmpForAlerts(VirtualFs& fs, const std::string& finalPath) {
    const std::string tmpPath = tmpOf(finalPath);
    if (!fs.exists(tmpPath)) return;
    if (fs.exists(finalPath)) {
        fs.files.erase(tmpPath);
        return;
    }
    if (parseOk(fs.read(tmpPath))) {
        fs.files[finalPath] = fs.read(tmpPath);
    }
    fs.files.erase(tmpPath);
}

// Alerts tree whose first (enabled) slot holds the given emails.
JVal alertTreeWithEmails(const std::string& emails) {
    JVal slots;
    slots.type = JVal::OBJ;
    slots.s = "ARRAY";
    JVal slot = JVal::objV();
    slot.obj.push_back({"enabled", JVal::boolV(true)});
    slot.obj.push_back({"timeout_minutes", JVal::intV(20)});
    slot.obj.push_back({"max_per_day", JVal::intV(12)});
    slot.obj.push_back({"emails", JVal::strV(emails)});
    slots.obj.push_back({"%item", std::move(slot)});

    JVal root = JVal::objV();
    root.obj.push_back({"sensors_reset_timer", JVal::boolV(true)});
    root.obj.push_back({"slots", std::move(slots)});
    return root;
}

} // namespace

// ---- sanitize: control bytes stripped from EVERY string field --------------

TEST_CASE("sanitize strips non-legal control bytes from slot emails (nested)") {
    // 0x01 and 0x1A are control bytes ArduinoJson emits RAW (only \b\f\n\r\t
    // and NUL are escaped), which makes the next deserializeJson() fail.
    std::string dirty = "a@b.com";
    dirty += (char)0x01;
    dirty += ",c@d.com";
    dirty += (char)0x1A;

    JVal tree = alertTreeWithEmails(dirty);
    sanitizeJson(tree);
    std::string serialized = serializeAlerts(tree);
    CHECK(serialized.find((char)0x01) == std::string::npos);
    CHECK(serialized.find((char)0x1A) == std::string::npos);
    CHECK(parseOk(serialized));
    CHECK(serialized.find("a@b.com,c@d.com") != std::string::npos);
}

TEST_CASE("sanitize keeps TAB/LF/CR - ArduinoJson escapes them so JSON stays valid") {
    std::string keep = "x@y.com";
    keep += '\t';
    keep += ",y@z.com";
    keep += '\n';

    JVal tree = alertTreeWithEmails(keep);
    sanitizeJson(tree);
    std::string serialized = serializeAlerts(tree);
    CHECK(serialized.find("\\t") != std::string::npos);
    CHECK(serialized.find("\\n") != std::string::npos);
    CHECK(parseOk(serialized));
}

TEST_CASE("sanitize is recursive - strips any string field, preserves scalars") {
    JVal root = JVal::objV();
    root.obj.push_back({"sensors_reset_timer", JVal::boolV(false)});
    std::string dirty = "label";
    dirty += (char)0x0B;   // vertical tab: raw-out in ArduinoJson, would corrupt
    root.obj.push_back({"extra_note", JVal::strV(dirty)});

    sanitizeJson(root);
    std::string serialized = serializeAlerts(root);
    CHECK(serialized.find((char)0x0B) == std::string::npos);
    CHECK(serialized.find("true") == std::string::npos);          // bool untouched
    CHECK(parseOk(serialized));
}

// ---- atomic write: re-parse verification before rename ----------------------

TEST_CASE("atomicWriteVerified refuses to replace good file with truncated tmp") {
    VirtualFs fs;
    fs.files["/config/alerts.json"] = "{\"slots\":[]}";
    CHECK_FALSE(atomicWriteVerified(fs, "/config/alerts.json", "{\"slots\":[{\"enabled\":"));
    CHECK(fs.read("/config/alerts.json") == "{\"slots\":[]}");   // original intact
    CHECK_FALSE(fs.exists("/config/alerts.json.tmp"));           // tmp discarded
}

TEST_CASE("atomicWriteVerified refuses raw control byte that would corrupt next read") {
    VirtualFs fs;
    fs.files["/config/alerts.json"] = "{\"slots\":[]}";
    std::string bad = "{\"slots\":[{\"emails\":\"a@b.com,";
    bad += (char)0x01;
    bad += "\"}]}";
    CHECK_FALSE(atomicWriteVerified(fs, "/config/alerts.json", bad));
    CHECK(fs.read("/config/alerts.json") == "{\"slots\":[]}");
    CHECK_FALSE(fs.exists("/config/alerts.json.tmp"));
}

TEST_CASE("atomicWriteVerified replaces final on valid content and cleans tmp") {
    VirtualFs fs;
    fs.files["/config/alerts.json"] = "old";
    CHECK(atomicWriteVerified(fs, "/config/alerts.json", "{\"k\":1}"));
    CHECK(fs.read("/config/alerts.json") == "{\"k\":1}");
    CHECK_FALSE(fs.exists("/config/alerts.json.tmp"));
}

// ---- heal-on-load: corrupt alerts.json is rewritten immediately -------------

TEST_CASE("healOnLoad rewrites a corrupt alerts.json with clean defaults") {
    VirtualFs fs;
    std::string garbage = "{\"slots\":[{\"emails\":\"a@b.com";
    garbage += (char)0x03;
    garbage += "\"}]}";
    fs.files["/config/alerts.json"] = garbage;

    bool healed = false;
    CHECK(healOnLoad(fs, "/config/alerts.json", healed));
    CHECK(healed);
    CHECK(parseOk(fs.read("/config/alerts.json")));
    CHECK(fs.read("/config/alerts.json").find((char)0x03) == std::string::npos);
    CHECK_FALSE(fs.exists("/config/alerts.json.tmp"));
}

TEST_CASE("healOnLoad leaves a valid alerts.json untouched") {
    VirtualFs fs;
    fs.files["/config/alerts.json"] = "{\"slots\":[],\"sensors_reset_timer\":true}";
    bool healed = true;   // sentinel: must be set to false
    CHECK(healOnLoad(fs, "/config/alerts.json", healed));
    CHECK_FALSE(healed);
    CHECK(fs.read("/config/alerts.json") == "{\"slots\":[],\"sensors_reset_timer\":true}");
}

TEST_CASE("healOnLoad does nothing when the file is absent (defaults used in memory)") {
    VirtualFs fs;
    bool healed = true;
    CHECK(healOnLoad(fs, "/config/alerts.json", healed));
    CHECK_FALSE(healed);
    CHECK_FALSE(fs.exists("/config/alerts.json"));
}

// ---- boot tmp recovery: only a parseable .tmp may be promoted --------------

TEST_CASE("recoverTmpForAlerts discards a truncated .tmp, never promotes it") {
    VirtualFs fs;
    fs.files["/config/alerts.json.tmp"] = "{\"slots\":[{\"enabled\":";
    recoverTmpForAlerts(fs, "/config/alerts.json");
    CHECK_FALSE(fs.exists("/config/alerts.json"));        // no promotion
    CHECK_FALSE(fs.exists("/config/alerts.json.tmp"));    // tmp cleaned
}

TEST_CASE("recoverTmpForAlerts promotes a parseable .tmp when final is missing") {
    VirtualFs fs;
    fs.files["/config/alerts.json.tmp"] = "{\"slots\":[],\"sensors_reset_timer\":true}";
    recoverTmpForAlerts(fs, "/config/alerts.json");
    CHECK(fs.read("/config/alerts.json") == "{\"slots\":[],\"sensors_reset_timer\":true}");
    CHECK_FALSE(fs.exists("/config/alerts.json.tmp"));
}

TEST_CASE("recoverTmpForAlerts removes a stale .tmp when a valid final exists") {
    VirtualFs fs;
    fs.files["/config/alerts.json"] = "{\"slots\":[],\"sensors_reset_timer\":true}";
    fs.files["/config/alerts.json.tmp"] = "{\"slots\":[]}";
    recoverTmpForAlerts(fs, "/config/alerts.json");
    CHECK(fs.read("/config/alerts.json") == "{\"slots\":[],\"sensors_reset_timer\":true}");
    CHECK_FALSE(fs.exists("/config/alerts.json.tmp"));
}
