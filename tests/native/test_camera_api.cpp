#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <functional>

// Standalone reimpl of the /api/camera/config POST pipeline from
// camera_handler.cpp (handleCameraConfigPost): form + JSON parsing,
// truthy/intval semantics, per-field clamp table, and the changed-flag
// merge rule. Mirrors production exactly: unknown keys are ignored,
// missing keys leave the field untouched, out-of-range values are clamped
// (never wrapped). JSON mirror matches jsonVal()/jsonScalar(): flat keys
// plus a nested "motion" object, booleans flattened to "1"/"0".

// --- JSON lookup mirror (mirrors jsonVal/jsonScalar lambdas) ------------
static std::string jsonVal(const std::string& json, const std::string& k) {
    auto valueAfter = [&](size_t colonPos) -> std::string {
        long idx = (long)colonPos + 1;
        while (idx < (long)json.size() && (json[(size_t)idx] == ' ' || json[(size_t)idx] == '\t' ||
                                        json[(size_t)idx] == '\n' || json[(size_t)idx] == '\r'))
            idx++;
        if (idx < 0 || (size_t)idx >= json.size()) return std::string();
        char c = json[(size_t)idx];
        if (c == '"') {
            size_t e = json.find('"', (size_t)idx + 1);
            if (e == std::string::npos) return std::string();
            return json.substr((size_t)idx + 1, e - ((size_t)idx + 1));
        }
        if (json.compare((size_t)idx, 4, "true") == 0) return "1";
        if (json.compare((size_t)idx, 5, "false") == 0) return "0";
        size_t e = (size_t)idx;
        while (e < json.size() && strchr("-+.0123456789eE", json[e]) != nullptr) e++;
        if (e == (size_t)idx) return std::string();
        return json.substr((size_t)idx, e - (size_t)idx);
    };
    auto keyColon = [&](const std::string& key, size_t from) -> size_t {
        size_t q = json.find("\"" + key + "\"", from);
        if (q == std::string::npos) return std::string::npos;
        q = json.find(':', q);
        return q;
    };
    if (k.compare(0, 7, "motion_") == 0) {
        size_t p = json.find("\"motion\"");
        if (p == std::string::npos) return std::string();
        std::string sub = k.substr(7);
        size_t q = keyColon(sub, p);
        if (q == std::string::npos) return std::string();
        return valueAfter(q);
    }
    size_t q = keyColon(k, 0);
    if (q == std::string::npos) return std::string();
    return valueAfter(q);
}

// --- form parser (mirrors val/truthy/intval lambdas) -------------------
static std::string formVal(const std::string& body, const std::string& k) {
    std::string p = k + "=";
    size_t s = 0;
    while (s <= body.size()) {
        size_t e = body.find('&', s);
        std::string pair = (e == std::string::npos) ? body.substr(s)
                                                    : body.substr(s, e - s);
        if (pair.compare(0, p.size(), p) == 0) {
            std::string v = pair.substr(p.size());
            for (size_t i = 0; i < v.size(); i++)
                if (v[i] == '+') v[i] = ' ';
            return v;
        }
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return std::string();
}

static bool formTruthy(const std::string& v) {
    std::string t = v;
    for (size_t i = 0; i < t.size(); i++)
        t[i] = (char)tolower(t[i]);
    return t == "1" || t == "true" || t == "on";
}

static int clampI(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// --- config struct mirror (defaults = cameraConfigDefaults) ------------
struct ApiCfg {
    int framesize = 6;  // QVGA (framework framesize_t: 6=QVGA … 24=5MP)
    int quality = 20;
    int fps = 12;
    int brightness = 0, contrast = 0, saturation = 0;
    bool hmirror = false, vflip = false;
    int gainceiling = 0;
    bool awb = true, aec = true, agc = true, lenc = true;
    int special_effect = 0, wb_mode = 0;
    int aec_value = 204;
    bool aec2 = true;
    int agc_gain = 5, ae_level = 0;
    int dashcam_mins = 0, timelapse_secs = 0;
    int auto_mode = 0;
    bool mot_enabled = false;
    int mot_sens = 20, mot_confirm = 3, mot_stop = 10, mot_post = 5;
    int mot_action = 0;
    int mot_cooldown = 5;
    bool mot_lamp_auto = false;
    int mot_roi_x = 0, mot_roi_y = 0, mot_roi_w = 0, mot_roi_h = 0;
    int lamp = 0;
    int source = 0;  // 0=native, 1=USB UVC (P4 only, ignored on S3)
};

// Rule table shared by both form and JSON bodies: a single value lookup
// callback supplies each field's raw string (mirrors the handler's val()).
// smtpAvailable mirrors production's `#if ENABLE_SMTP==1` gate on the
// motion_action upper bound (packs without email clamp 2 -> 1).
static bool applyConfigPostImpl(const std::function<std::string(const std::string&)>& val, ApiCfg& c,
                                bool smtpAvailable = true) {
    bool changed = false;
    if (!val("framesize").empty()) {
        int fs = atoi(val("framesize").c_str());
        if (fs >= 0 && fs <= 24) { c.framesize = fs; changed = true; }
    }
    if (!val("quality").empty())     { c.quality = clampI(atoi(val("quality").c_str()), 0, 63); changed = true; }
    if (!val("fps").empty())         { c.fps = clampI(atoi(val("fps").c_str()), 1, 30); changed = true; }
    if (!val("brightness").empty())  { c.brightness = clampI(atoi(val("brightness").c_str()), -2, 2); changed = true; }
    if (!val("contrast").empty())    { c.contrast = clampI(atoi(val("contrast").c_str()), -2, 2); changed = true; }
    if (!val("saturation").empty())  { c.saturation = clampI(atoi(val("saturation").c_str()), -2, 2); changed = true; }
    if (!val("dashcam_mins").empty())   { c.dashcam_mins = clampI(atoi(val("dashcam_mins").c_str()), 0, 120); changed = true; }
    if (!val("timelapse_secs").empty()) { c.timelapse_secs = clampI(atoi(val("timelapse_secs").c_str()), 0, 3600); changed = true; }
    if (!val("auto_mode").empty())      { c.auto_mode = clampI(atoi(val("auto_mode").c_str()), 0, 4); changed = true; }
    if (!val("hmirror").empty()) { c.hmirror = formTruthy(val("hmirror")); changed = true; }
    if (!val("vflip").empty())   { c.vflip = formTruthy(val("vflip")); changed = true; }
    if (!val("awb").empty())     { c.awb = formTruthy(val("awb")); changed = true; }
    if (!val("aec").empty())     { c.aec = formTruthy(val("aec")); changed = true; }
    if (!val("agc").empty())     { c.agc = formTruthy(val("agc")); changed = true; }
    if (!val("lenc").empty())    { c.lenc = formTruthy(val("lenc")); changed = true; }
    if (!val("gainceiling").empty())  { c.gainceiling = clampI(atoi(val("gainceiling").c_str()), 0, 6); changed = true; }
    if (!val("special_effect").empty()) { c.special_effect = clampI(atoi(val("special_effect").c_str()), 0, 6); changed = true; }
    if (!val("wb_mode").empty())      { c.wb_mode = clampI(atoi(val("wb_mode").c_str()), 0, 4); changed = true; }
    if (!val("aec_value").empty())    { c.aec_value = clampI(atoi(val("aec_value").c_str()), 0, 1200); changed = true; }
    if (!val("aec2").empty())         { c.aec2 = formTruthy(val("aec2")); changed = true; }
    if (!val("agc_gain").empty())     { c.agc_gain = clampI(atoi(val("agc_gain").c_str()), 0, 30); changed = true; }
    if (!val("ae_level").empty())     { c.ae_level = clampI(atoi(val("ae_level").c_str()), -2, 2); changed = true; }
    if (!val("motion_enabled").empty())  { c.mot_enabled = formTruthy(val("motion_enabled")); changed = true; }
    if (!val("motion_sensitivity").empty()) { c.mot_sens = clampI(atoi(val("motion_sensitivity").c_str()), 1, 100); changed = true; }
    if (!val("motion_confirm_frames").empty()) { c.mot_confirm = clampI(atoi(val("motion_confirm_frames").c_str()), 1, 20); changed = true; }
    if (!val("motion_stop_frames").empty())    { c.mot_stop = clampI(atoi(val("motion_stop_frames").c_str()), 2, 30); changed = true; }
    if (!val("motion_post_record_secs").empty()) { c.mot_post = clampI(atoi(val("motion_post_record_secs").c_str()), 0, 30); changed = true; }
    if (!val("motion_action").empty())          { c.mot_action = clampI(atoi(val("motion_action").c_str()), 0, smtpAvailable ? 2 : 1); changed = true; }
    if (!val("motion_cooldown_secs").empty())   { c.mot_cooldown = clampI(atoi(val("motion_cooldown_secs").c_str()), 0, 600); changed = true; }
    if (!val("motion_lamp_auto").empty())       { c.mot_lamp_auto = formTruthy(val("motion_lamp_auto")); changed = true; }
    if (!val("motion_roi_x").empty()) { c.mot_roi_x = clampI(atoi(val("motion_roi_x").c_str()), 0, 39); changed = true; }
    if (!val("motion_roi_y").empty()) { c.mot_roi_y = clampI(atoi(val("motion_roi_y").c_str()), 0, 29); changed = true; }
    if (!val("motion_roi_w").empty()) { c.mot_roi_w = clampI(atoi(val("motion_roi_w").c_str()), 0, 40); changed = true; }
    if (!val("motion_roi_h").empty()) { c.mot_roi_h = clampI(atoi(val("motion_roi_h").c_str()), 0, 30); changed = true; }
    if (!val("lamp").empty())           { c.lamp = clampI(atoi(val("lamp").c_str()), 0, 15); changed = true; }
    if (!val("source").empty())         { c.source = clampI(atoi(val("source").c_str()), 0, 1); changed = true; }
    return changed;
}

static bool applyConfigPost(const std::string& body, ApiCfg& c, bool smtpAvailable = true) {
    auto val = [&](const std::string& k) -> std::string { return formVal(body, k); };
    return applyConfigPostImpl(val, c, smtpAvailable);
}

static bool applyConfigPostJson(const std::string& json, ApiCfg& c, bool smtpAvailable = true) {
    auto val = [&](const std::string& k) -> std::string { return jsonVal(json, k); };
    return applyConfigPostImpl(val, c, smtpAvailable);
}

TEST_CASE("form parser: first/middle/last key extraction") {
    std::string b = "quality=20&fps=12&brightness=-2";
    CHECK(formVal(b, "quality") == "20");
    CHECK(formVal(b, "fps") == "12");
    CHECK(formVal(b, "brightness") == "-2");
    CHECK(formVal(b, "missing") == "");
    CHECK(formVal(b, "") == "");
}

TEST_CASE("form parser: plus decodes to space, prefix keys do not collide") {
    CHECK(formVal("a=1&ab=2", "a") == "1");
    CHECK(formVal("a=1&ab=2", "ab") == "2");
    CHECK(formVal("x=a+b", "x") == "a b");
}

TEST_CASE("truthy variants") {
    CHECK(formTruthy(formVal("hmirror=1", "hmirror")) == true);
    CHECK(formTruthy(formVal("hmirror=true", "hmirror")) == true);
    CHECK(formTruthy(formVal("hmirror=on", "hmirror")) == true);
    CHECK(formTruthy(formVal("hmirror=TRUE", "hmirror")) == true);
    CHECK(formTruthy(formVal("hmirror=0", "hmirror")) == false);
    CHECK(formTruthy(formVal("hmirror=false", "hmirror")) == false);
    CHECK(formTruthy(formVal("hmirror=", "hmirror")) == false);
    CHECK(formTruthy(formVal("", "hmirror")) == false);
}

TEST_CASE("changed flag: empty and unknown-only bodies change nothing") {
    ApiCfg c;
    CHECK(applyConfigPost("", c) == false);
    CHECK(applyConfigPost("foo=1&bar=2", c) == false);
    CHECK(c.quality == 20);
}

TEST_CASE("clamps: quality/fps/picture ranges never wrap") {
    ApiCfg c;
    CHECK(applyConfigPost("quality=200", c) == true);
    CHECK(c.quality == 63);
    CHECK(applyConfigPost("quality=-5", c) == true);
    CHECK(c.quality == 0);
    CHECK(applyConfigPost("fps=99", c) == true);
    CHECK(c.fps == 30);
    CHECK(applyConfigPost("fps=0", c) == true);
    CHECK(c.fps == 1);
    CHECK(applyConfigPost("brightness=99", c) == true);
    CHECK(c.brightness == 2);
    CHECK(applyConfigPost("brightness=-99", c) == true);
    CHECK(c.brightness == -2);
    CHECK(applyConfigPost("ae_level=5", c) == true);
    CHECK(c.ae_level == 2);
}

TEST_CASE("clamps: sensor-mode fields") {
    ApiCfg c;
    applyConfigPost("gainceiling=9", c); CHECK(c.gainceiling == 6);
    applyConfigPost("special_effect=9", c); CHECK(c.special_effect == 6);
    applyConfigPost("wb_mode=9", c); CHECK(c.wb_mode == 4);
    applyConfigPost("aec_value=9999", c); CHECK(c.aec_value == 1200);
    applyConfigPost("agc_gain=99", c); CHECK(c.agc_gain == 30);
}

TEST_CASE("clamps: recording/motion fields, framesize enum guard") {
    ApiCfg c;
    applyConfigPost("dashcam_mins=999", c); CHECK(c.dashcam_mins == 120);
    applyConfigPost("timelapse_secs=9999", c); CHECK(c.timelapse_secs == 3600);
    applyConfigPost("motion_sensitivity=0", c); CHECK(c.mot_sens == 1);
    applyConfigPost("motion_sensitivity=200", c); CHECK(c.mot_sens == 100);
    applyConfigPost("motion_confirm_frames=0", c); CHECK(c.mot_confirm == 1);
    applyConfigPost("motion_confirm_frames=99", c); CHECK(c.mot_confirm == 20);
    applyConfigPost("motion_stop_frames=0", c); CHECK(c.mot_stop == 2);
    applyConfigPost("motion_stop_frames=99", c); CHECK(c.mot_stop == 30);
    applyConfigPost("motion_post_record_secs=99", c); CHECK(c.mot_post == 30);
    applyConfigPost("motion_cooldown_secs=9999", c); CHECK(c.mot_cooldown == 600);
    applyConfigPost("motion_lamp_auto=1", c); CHECK(c.mot_lamp_auto == true);
    applyConfigPost("motion_lamp_auto=0", c); CHECK(c.mot_lamp_auto == false);
    applyConfigPost("motion_roi_x=99", c); CHECK(c.mot_roi_x == 39);
    applyConfigPost("motion_roi_y=99", c); CHECK(c.mot_roi_y == 29);
    applyConfigPost("motion_roi_w=99", c); CHECK(c.mot_roi_w == 40);
    applyConfigPost("motion_roi_h=99", c); CHECK(c.mot_roi_h == 30);
    applyConfigPost("lamp=99", c); CHECK(c.lamp == 15);
    applyConfigPost("source=99", c); CHECK(c.source == 1);
    applyConfigPost("source=0", c); CHECK(c.source == 0);
    // Invalid framesize enum is ignored, valid one applies.
    CHECK(applyConfigPost("framesize=99", c) == false);
    CHECK(c.framesize == 6);
    CHECK(applyConfigPost("framesize=-1", c) == false);
    CHECK(applyConfigPost("framesize=13", c) == true);
    CHECK(c.framesize == 13);
    // QXGA (19) and 5MP (24) both valid now; 25 rejected.
    CHECK(applyConfigPost("framesize=19", c) == true);
    CHECK(c.framesize == 19);
    CHECK(applyConfigPost("framesize=24", c) == true);
    CHECK(c.framesize == 24);
    CHECK(applyConfigPost("framesize=25", c) == false);
    CHECK(c.framesize == 24);
}

TEST_CASE("checkboxes round-trip incl. explicit off") {
    ApiCfg c;
    applyConfigPost("hmirror=1&awb=0&aec2=1", c);
    CHECK(c.hmirror == true);
    CHECK(c.awb == false);
    CHECK(c.aec2 == true);
    applyConfigPost("hmirror=0", c);
    CHECK(c.hmirror == false);
}

TEST_CASE("json: flat scalars reach config, booleans flatten to 1/0") {
    ApiCfg c;
    std::string b = R"({"framesize":12,"quality":35,"fps":25,"hmirror":false,"awb":true,"agc":true,"dashcam_mins":5})";
    CHECK(applyConfigPostJson(b, c) == true);
    CHECK(c.framesize == 12);
    CHECK(c.quality == 35);
    CHECK(c.fps == 25);
    CHECK(c.hmirror == false);
    CHECK(c.awb == true);
    CHECK(c.agc == true);
    CHECK(c.dashcam_mins == 5);
    CHECK(c.brightness == 0);
}

TEST_CASE("json: nested motion object maps to motion_* fields") {
    ApiCfg c;
    std::string b = R"({"motion":{"enabled":true,"sensitivity":50,"confirm_frames":4,"post_record_secs":8}})";
    CHECK(applyConfigPostJson(b, c) == true);
    CHECK(c.mot_enabled == true);
    CHECK(c.mot_sens == 50);
    CHECK(c.mot_confirm == 4);
    CHECK(c.mot_post == 8);
    CHECK(c.mot_stop == 10);
}

TEST_CASE("json: explicit false in nested object, missing keys untouched") {
    ApiCfg c;
    std::string b = R"({"motion":{"enabled":false}})";
    CHECK(applyConfigPostJson(b, c) == true);
    CHECK(c.mot_enabled == false);
    CHECK(c.mot_sens == 20);
    CHECK(c.quality == 20);
}

TEST_CASE("json: clamps still apply, unknown/empty objects change nothing") {
    ApiCfg c;
    CHECK(applyConfigPostJson(R"({"quality":200,"fps":99})", c) == true);
    CHECK(c.quality == 63);
    CHECK(c.fps == 30);
    CHECK(applyConfigPostJson(R"({"unknown":1})", c) == false);
    CHECK(applyConfigPostJson("{}", c) == false);
    CHECK(c.brightness == 0);
}

TEST_CASE("auto_mode: form + json round-trip") {
    ApiCfg c;
    CHECK(applyConfigPost("auto_mode=4", c) == true);
    CHECK(c.auto_mode == 4);            // timelapse
    CHECK(applyConfigPostJson(R"({"auto_mode":2})", c) == true);
    CHECK(c.auto_mode == 2);            // video (dashcam)
    CHECK(applyConfigPost("auto_mode=0", c) == true);
    CHECK(c.auto_mode == 0);            // off
}

TEST_CASE("auto_mode: clamped to 0..4, never wraps") {
    ApiCfg c;
    applyConfigPost("auto_mode=9", c);
    CHECK(c.auto_mode == 4);
    applyConfigPost("auto_mode=-1", c);
    CHECK(c.auto_mode == 0);
    applyConfigPost("auto_mode=3", c);
    CHECK(c.auto_mode == 3);            // motion
}

TEST_CASE("motion_action: form + json round-trip") {
    ApiCfg c;
    CHECK(applyConfigPost("motion_action=1", c) == true);
    CHECK(c.mot_action == 1);           // snap_photo
    CHECK(applyConfigPostJson(R"({"motion":{"action":2}})", c) == true);
    CHECK(c.mot_action == 2);           // email_alert
    CHECK(applyConfigPost("motion_action=0", c) == true);
    CHECK(c.mot_action == 0);           // record_video
}

TEST_CASE("motion_action: clamped to 0..2, never wraps") {
    ApiCfg c;
    applyConfigPost("motion_action=9", c);
    CHECK(c.mot_action == 2);
    applyConfigPost("motion_action=-1", c);
    CHECK(c.mot_action == 0);
}

TEST_CASE("motion_action: no-SMTP build clamps email (2) to snap (1)") {
    ApiCfg c;
    CHECK(applyConfigPost("motion_action=2", c, false) == true);
    CHECK(c.mot_action == 1);           // legacy email_alert folds to snap_photo
    CHECK(applyConfigPost("motion_action=9", c, false) == true);
    CHECK(c.mot_action == 1);
    CHECK(applyConfigPost("motion_action=1", c, false) == true);
    CHECK(c.mot_action == 1);
    CHECK(applyConfigPost("motion_action=0", c, false) == true);
    CHECK(c.mot_action == 0);
}

TEST_CASE("motion_action: no-SMTP build clamps nested JSON the same way") {
    ApiCfg c;
    CHECK(applyConfigPostJson(R"({"motion":{"action":2}})", c, false) == true);
    CHECK(c.mot_action == 1);
}

TEST_CASE("motion_action: json nested object maps alongside other motion fields") {
    ApiCfg c;
    std::string b = R"({"motion":{"enabled":true,"sensitivity":60,"action":1}})";
    CHECK(applyConfigPostJson(b, c) == true);
    CHECK(c.mot_enabled == true);
    CHECK(c.mot_sens == 60);
    CHECK(c.mot_action == 1);
    CHECK(c.mot_stop == 10);            // untouched default
}
