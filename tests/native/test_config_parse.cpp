#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <string>

// Standalone reimplementation of the JSON config parsing + default-merge
// pattern used by ConfigManager and the sensor config managers. Production
// relies on ArduinoJson deserializeJson(); this models the same failure modes:
// a truncated / corrupt / wrong-typed config file must never crash the boot,
// and every missing or invalid field falls back to its default.

namespace {

// Minimal JSON value tree (object, string, number, bool, null).
struct JVal {
    enum Type { NUL, BOOL, NUM, STR, OBJ } type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::map<std::string, JVal> obj;

    static JVal nullV() { return JVal{}; }
    static JVal boolV(bool v) { JVal j; j.type = BOOL; j.b = v; return j; }
    static JVal numV(double v) { JVal j; j.type = NUM; j.n = v; return j; }
    static JVal strV(std::string v) { JVal j; j.type = STR; j.s = std::move(v); return j; }
    static JVal objV() { JVal j; j.type = OBJ; return j; }

    bool containsKey(const std::string& k) const {
        return type == OBJ && obj.count(k) != 0;
    }
    const JVal& get(const std::string& k) const {
        static const JVal nul;
        auto it = obj.find(k);
        return it == obj.end() ? nul : it->second;
    }
};

// Returns parseOk=false on any malformed input (matches deserializeJson() failure).
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

    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    bool parseValue(JVal& out) {
        skipWs();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parseObject(out);
        if (c == '"') { std::string str; if (!parseString(str)) return false; out = JVal::strV(std::move(str)); return true; }
        if (c == 't') { if (s_.substr(pos_, 4) != "true") return false; pos_ += 4; out = JVal::boolV(true); return true; }
        if (c == 'f') { if (s_.substr(pos_, 5) != "false") return false; pos_ += 5; out = JVal::boolV(false); return true; }
        if (c == 'n') { if (s_.substr(pos_, 4) != "null") return false; pos_ += 4; out = JVal::nullV(); return true; }
        return parseNumber(out);
    }

    bool parseObject(JVal& out) {
        ++pos_; // {
        out = JVal::objV();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (pos_ < s_.size()) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            JVal v;
            if (!parseValue(v)) return false;
            out.obj[key] = std::move(v);
            skipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
        return false;
    }

    bool parseString(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == '"') { ++pos_; return true; }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case '\\': out += '\\'; break;
                    case '"': out += '"'; break;
                    default: return false;
                }
                ++pos_;
            } else {
                out += c;
                ++pos_;
            }
        }
        return false;
    }

    bool parseNumber(JVal& out) {
        size_t start = pos_;
        while (pos_ < s_.size() && (std::isdigit((unsigned char)s_[pos_]) || s_[pos_] == '-' || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+')) ++pos_;
        if (pos_ == start) return false;
        out = JVal::numV(std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr));
        return true;
    }
};

// --- Default-merge logic (mirrors ConfigManager default application) ---
// For every known field: if the object key is missing OR the value has the
// wrong type, the default is applied. Null values are treated as missing.
struct InactivityDefaults {
    bool enabled = false;
    int timeoutMinutes = 20;
    int maxPerDay = 12;
    std::string emails;
    bool sensorsResetTimer = false;
};

InactivityDefaults applyInactivityDefaults(const JVal& cfg) {
    InactivityDefaults d;
    const JVal& inact = cfg.get("inactivity_alert");
    if (inact.containsKey("enabled") && inact.get("enabled").type == JVal::BOOL)
        d.enabled = inact.get("enabled").b;
    if (inact.containsKey("timeout_minutes") && inact.get("timeout_minutes").type == JVal::NUM)
        d.timeoutMinutes = (int)inact.get("timeout_minutes").n;
    if (inact.containsKey("max_per_day") && inact.get("max_per_day").type == JVal::NUM)
        d.maxPerDay = (int)inact.get("max_per_day").n;
    if (inact.containsKey("emails") && inact.get("emails").type == JVal::STR)
        d.emails = inact.get("emails").s;
    if (inact.containsKey("sensors_reset_timer") && inact.get("sensors_reset_timer").type == JVal::BOOL)
        d.sensorsResetTimer = inact.get("sensors_reset_timer").b;
    return d;
}

bool parseConfig(const std::string& json, JVal& out) {
    JsonParser p(json);
    ParseResult r = p.parse();
    if (!r.ok) return false;
    out = r.root;
    return true;
}

} // namespace

TEST_CASE("valid JSON parses and all fields populate") {
    JVal cfg;
    REQUIRE(parseConfig("{\"inactivity_alert\":{\"enabled\":true,\"timeout_minutes\":45,\"max_per_day\":6,\"emails\":\"a@b.c\",\"sensors_reset_timer\":true}}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK(d.enabled);
    CHECK(d.timeoutMinutes == 45);
    CHECK(d.maxPerDay == 6);
    CHECK(d.emails == "a@b.c");
    CHECK(d.sensorsResetTimer);
}

TEST_CASE("empty JSON object uses all defaults") {
    JVal cfg;
    REQUIRE(parseConfig("{}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK_FALSE(d.enabled);
    CHECK(d.timeoutMinutes == 20);
    CHECK(d.maxPerDay == 12);
    CHECK(d.emails.empty());
    CHECK_FALSE(d.sensorsResetTimer);
}

TEST_CASE("missing nested object uses all defaults") {
    JVal cfg;
    REQUIRE(parseConfig("{\"other\":1}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK_FALSE(d.enabled);
    CHECK(d.timeoutMinutes == 20);
}

TEST_CASE("truncated JSON is detected as parse failure") {
    JVal cfg;
    CHECK_FALSE(parseConfig("{\"inactivity_alert\":{\"enabled\":", cfg));
    CHECK_FALSE(parseConfig("{\"inactivity_alert\":", cfg));
    CHECK_FALSE(parseConfig("{", cfg));
    CHECK_FALSE(parseConfig("", cfg));
}

TEST_CASE("corrupt JSON (binary garbage) is detected as parse failure") {
    JVal cfg;
    CHECK_FALSE(parseConfig("\x00\x01\x02not-json\xff", cfg));
    CHECK_FALSE(parseConfig("{[", cfg));
    CHECK_FALSE(parseConfig("{\"k\":", cfg));
}

TEST_CASE("extra unknown fields are ignored without error") {
    JVal cfg;
    REQUIRE(parseConfig("{\"unrelated\":123,\"inactivity_alert\":{\"enabled\":true,\"future_field\":42}}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK(d.enabled);
    CHECK(d.timeoutMinutes == 20); // untouched -> default
}

TEST_CASE("wrong-typed field falls back to default") {
    JVal cfg;
    REQUIRE(parseConfig("{\"inactivity_alert\":{\"enabled\":\"yes\",\"timeout_minutes\":\"abc\",\"max_per_day\":null,\"emails\":42}}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK_FALSE(d.enabled);            // string "yes" is not bool -> default false
    CHECK(d.timeoutMinutes == 20);     // "abc" is not a number -> default
    CHECK(d.maxPerDay == 12);          // null -> default
    CHECK(d.emails.empty());           // number 42 is not a string -> default
}

TEST_CASE("null in optional fields treated as missing") {
    JVal cfg;
    REQUIRE(parseConfig("{\"inactivity_alert\":null}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK_FALSE(d.enabled);
    CHECK(d.timeoutMinutes == 20);
    CHECK(d.emails.empty());
}

TEST_CASE("numeric strings do NOT coerce (type strictness)") {
    JVal cfg;
    REQUIRE(parseConfig("{\"inactivity_alert\":{\"timeout_minutes\":\"45\"}}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK(d.timeoutMinutes == 20); // string "45" is not a number -> default
}

TEST_CASE("bool coercion: \"true\" string is not a real bool") {
    JVal cfg;
    REQUIRE(parseConfig("{\"inactivity_alert\":{\"enabled\":\"true\"}}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK_FALSE(d.enabled);
}

TEST_CASE("trailing garbage after valid JSON is a parse failure") {
    JVal cfg;
    CHECK_FALSE(parseConfig("{} extra", cfg));
    CHECK_FALSE(parseConfig("{\"a\":1}}", cfg));
}

TEST_CASE("deeply nested config parses and inner fields resolve") {
    JVal cfg;
    REQUIRE(parseConfig("{\"system\":{\"inner\":{\"x\":1}},\"inactivity_alert\":{\"enabled\":true,\"max_per_day\":30}}", cfg));
    InactivityDefaults d = applyInactivityDefaults(cfg);
    CHECK(d.enabled);
    CHECK(d.maxPerDay == 30);
    CHECK(d.timeoutMinutes == 20);
}
