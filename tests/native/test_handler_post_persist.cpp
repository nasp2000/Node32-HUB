#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Arduino.h"
#include "ArduinoJson.h"
#include "MockConfigStore.h"

#include <string>
#include <vector>
#include <cstring>

// Handler POST persistence test. Mirrors the persistence-critical slice of
// handleConfigPost() in chatgpt/src/webpages/handlers_unified/config_handler.cpp:
//   - form-urlencoded AND application/json body parsing (getFormValue / bodyValue)
//   - section validation: wifi|email|rs232|system|recipients|scheduler
//   - "********" password masking (never overwrites an existing secret)
//   - recipients via recipients_json JSON array (clear + re-add)
//   - save routing: wifi/email -> full save; others -> saveSection(stem)
//   - config survives a simulated reboot (fresh state loaded from MockConfigStore)
// ConfigState models ConfigManager; the store stands in for LittleFS slot files.
namespace {

const char* FILLED_PASS = "********";

struct Recipient {
    std::string name;
    std::string email;
};

// Mirrors the ConfigManager values touched by POST /api/config.
struct ConfigState {
    std::string wifi_ssid;
    std::string wifi_password;
    std::string smtp_server;
    int         smtp_port = 0;
    std::string smtp_user;
    std::string smtp_pass;
    std::string email_method;
    std::string email_destination;
    bool        email_auto_send = false;
    std::string system_name;
    std::string system_language;
    std::string ntp_timezone;
    std::string ntp_server;
    std::vector<Recipient> recipients;
};

// MockConfigStore keys (mirror ConfigManager slot files).
const char* K_WIFI   = "config/core.json";
const char* K_EMAIL  = "config/core.json";
const char* K_SYSTEM = "config/system.json";
const char* K_RS232  = "config/rs232.json";
const char* K_RECIP  = "config/recipients.json";
const char* K_SCHED  = "config/schedule.json";

// Mirrors ConfigManager::saveSection stem routing + save(true) for wifi/email.
std::string routeStem(const std::string& section) {
    if (section == "wifi" || section == "email") return "core";
    if (section == "system") return "system";
    if (section == "rs232")  return "rs232";
    if (section == "recipients") return "recipients";
    if (section == "scheduler")  return "schedule";
    return "";
}

const char* stemFile(const std::string& stem) {
    if (stem == "core")        return K_WIFI;
    if (stem == "system")      return K_SYSTEM;
    if (stem == "rs232")       return K_RS232;
    if (stem == "recipients")  return K_RECIP;
    if (stem == "schedule")    return K_SCHED;
    return "";
}

bool validSection(const std::string& s) {
    return s == "wifi" || s == "email" || s == "rs232" ||
           s == "system" || s == "recipients" || s == "scheduler";
}

// --- body parsing helpers (mirror config_handler.cpp:30-101) ---

std::string urlDecode(const std::string& in) {
    std::string out;
    auto hex = [](char ch) -> int {
        return (ch >= '0' && ch <= '9') ? (ch - '0') :
               (ch >= 'A' && ch <= 'F') ? (ch - 'A' + 10) :
               (ch >= 'a' && ch <= 'f') ? (ch - 'a' + 10) : 0;
    };
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '+') { out += ' '; continue; }
        if (c == '%' && i + 2 < in.size()) {
            out += (char)(hex(in[i + 1]) * 16 + hex(in[i + 2]));
            i += 2;
        } else { out += c; }
    }
    return out;
}

std::string getFormValue(const std::string& body, const std::string& key) {
    std::string prefix = key + "=";
    size_t start = 0;
    while (start <= body.size()) {
        size_t amp = body.find('&', start);
        if (amp == std::string::npos) amp = body.size();
        std::string pair = body.substr(start, amp - start);
        if (pair.compare(0, prefix.size(), prefix) == 0) {
            return urlDecode(pair.substr(prefix.size()));
        }
        start = amp + 1;
    }
    return std::string();
}

bool bodyHasKey(const std::string& body, const std::string& contentType,
                const std::string& key) {
    if (contentType.compare(0, 16, "application/json") == 0) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            return doc.containsKey(key.c_str());
        }
        return false;
    }
    return body.find(key + "=") != std::string::npos;
}

std::string bodyValue(const std::string& body, const std::string& contentType,
                      const std::string& key) {
    if (contentType.compare(0, 16, "application/json") == 0) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            if (!doc[key.c_str()].isNull()) {
                JsonVariant v = doc[key.c_str()];
                if (v.isString()) return std::string(v.as<const char*>());
                return std::to_string((long long)v.asInt64());
            }
        }
        return std::string();
    }
    return getFormValue(body, key);
}

bool bodyTruthy(const std::string& body, const std::string& contentType,
                const std::string& key) {
    if (contentType.compare(0, 16, "application/json") == 0) {
        SpiRamJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            if (!doc[key.c_str()].isNull()) return doc[key.c_str()].as<bool>();
        }
        return false;
    }
    std::string v = getFormValue(body, key);
    for (auto& c : v) c = (char)tolower((unsigned char)c);
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

// --- persistence (mirror ConfigManager setter + save flow) ---

// Serialize the wifi-relevant state into the core slot file.
std::string serializeCore(const ConfigState& c) {
    SpiRamJsonDocument doc(512);
    doc["wifi_ssid"] = c.wifi_ssid.c_str();
    doc["wifi_password"] = c.wifi_password.c_str();
    doc["smtp_server"] = c.smtp_server.c_str();
    doc["smtp_port"] = c.smtp_port;
    doc["smtp_user"] = c.smtp_user.c_str();
    doc["smtp_pass"] = c.smtp_pass.c_str();
    doc["email_method"] = c.email_method.c_str();
    doc["email_destination"] = c.email_destination.c_str();
    doc["email_auto_send"] = c.email_auto_send;
    String out;
    serializeJson(doc, out);
    return std::string(out.c_str());
}

void deserializeCore(ConfigState& c, const std::string& json) {
    SpiRamJsonDocument doc(512);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    if (!doc["wifi_ssid"].isNull())    c.wifi_ssid    = doc["wifi_ssid"].as<const char*>();
    if (!doc["wifi_password"].isNull()) c.wifi_password = doc["wifi_password"].as<const char*>();
    if (!doc["smtp_server"].isNull())  c.smtp_server  = doc["smtp_server"].as<const char*>();
    if (!doc["smtp_port"].isNull())    c.smtp_port    = doc["smtp_port"].as<int>();
    if (!doc["smtp_user"].isNull())    c.smtp_user    = doc["smtp_user"].as<const char*>();
    if (!doc["smtp_pass"].isNull())    c.smtp_pass    = doc["smtp_pass"].as<const char*>();
    if (!doc["email_method"].isNull()) c.email_method = doc["email_method"].as<const char*>();
    if (!doc["email_destination"].isNull()) c.email_destination = doc["email_destination"].as<const char*>();
    if (!doc["email_auto_send"].isNull())   c.email_auto_send   = doc["email_auto_send"].as<bool>();
}

std::string serializeSystem(const ConfigState& c) {
    SpiRamJsonDocument doc(512);
    doc["system_name"] = c.system_name.c_str();
    doc["system_language"] = c.system_language.c_str();
    doc["ntp_timezone"] = c.ntp_timezone.c_str();
    doc["ntp_server"] = c.ntp_server.c_str();
    String out;
    serializeJson(doc, out);
    return std::string(out.c_str());
}

void deserializeSystem(ConfigState& c, const std::string& json) {
    SpiRamJsonDocument doc(512);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    if (!doc["system_name"].isNull())    c.system_name    = doc["system_name"].as<const char*>();
    if (!doc["system_language"].isNull()) c.system_language = doc["system_language"].as<const char*>();
    if (!doc["ntp_timezone"].isNull())   c.ntp_timezone   = doc["ntp_timezone"].as<const char*>();
    if (!doc["ntp_server"].isNull())     c.ntp_server     = doc["ntp_server"].as<const char*>();
}

std::string serializeRecipients(const ConfigState& c) {
    SpiRamJsonDocument doc(1024);
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& r : c.recipients) {
        JsonObject o = arr.createNestedObject();
        o["name"] = r.name.c_str();
        o["email"] = r.email.c_str();
    }
    String out;
    serializeJson(doc, out);
    return std::string(out.c_str());
}

void deserializeRecipients(ConfigState& c, const std::string& json) {
    SpiRamJsonDocument doc(1024);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    if (!doc.is<JsonArray>()) return;
    c.recipients.clear();
    for (JsonVariant v : doc.as<JsonArray>()) {
        JsonObject o = v.as<JsonObject>();
        Recipient r;
        r.name  = std::string(o["name"]  | "");
        r.email = std::string(o["email"] | "");
        if (r.email.size()) c.recipients.push_back(r);
    }
}

struct PostResult {
    int         status;
    std::string body;
};

// Mirrors handleConfigPost() persistence flow. Returns (status, response body).
PostResult handleConfigPost(ConfigState& c, MockConfigStore& store,
                            const std::string& body, const std::string& contentType) {
    std::string section = bodyValue(body, contentType, "section");
    if (!validSection(section)) {
        return {400, "{\"success\":false,\"message\":\"Missing/invalid section in POST\"}"};
    }

    bool upd = false;
    std::string msg;
    std::string v;

    // WiFi
    v = bodyValue(body, contentType, "ssid");
    if (v.size() && v != FILLED_PASS) { c.wifi_ssid = v; msg += "WiFi SSID "; upd = true; }
    v = bodyValue(body, contentType, "password");
    if (v.size() && v != FILLED_PASS) { c.wifi_password = v; msg += "WiFi Pass "; upd = true; }

    // System
    v = bodyValue(body, contentType, "system_name");
    if (v.size()) { c.system_name = v; msg += "System Name "; upd = true; }
    v = bodyValue(body, contentType, "system_language");
    if (v.size()) { c.system_language = v; msg += "Language "; upd = true; }
    v = bodyValue(body, contentType, "timezone");
    if (v.size()) { c.ntp_timezone = v; msg += "NTP Timezone "; upd = true; }
    v = bodyValue(body, contentType, "ntp_server");
    if (v.size()) { c.ntp_server = v; msg += "NTP Server "; upd = true; }

    // SMTP / Email
    v = bodyValue(body, contentType, "smtp_server");
    if (v.size()) { c.smtp_server = v; msg += "SMTP Server "; upd = true; }
    v = bodyValue(body, contentType, "smtp_port");
    if (v.size()) { c.smtp_port = std::stoi(v); msg += "SMTP Port "; upd = true; }
    v = bodyValue(body, contentType, "smtp_user");
    if (v.size()) { c.smtp_user = v; msg += "SMTP User "; upd = true; }
    v = bodyValue(body, contentType, "smtp_pass");
    if (v.size() && v != FILLED_PASS) { c.smtp_pass = v; msg += "SMTP Pass "; upd = true; }
    v = bodyValue(body, contentType, "email_method");
    if (v.size()) { c.email_method = v; msg += "Email Method "; upd = true; }
    v = bodyValue(body, contentType, "email_destination");
    if (v.size()) { c.email_destination = v; msg += "Email Dest "; upd = true; }
    if (bodyHasKey(body, contentType, "email_auto_send_present") ||
        bodyHasKey(body, contentType, "email_auto_send")) {
        bool newVal = bodyTruthy(body, contentType, "email_auto_send");
        if (newVal != c.email_auto_send) { c.email_auto_send = newVal; msg += "AutoSend "; upd = true; }
    }

    // Recipients (JSON array via recipients_json) — clear + re-add.
    v = bodyValue(body, contentType, "recipients_json");
    if (v.size()) {
        SpiRamJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, v);
        if (!err && doc.is<JsonArray>()) {
            c.recipients.clear();
            for (JsonVariant r : doc.as<JsonArray>()) {
                JsonObject o = r.as<JsonObject>();
                std::string name  = std::string(o["name"]  | "");
                std::string email = std::string(o["email"] | "");
                if (email.size()) { c.recipients.push_back({name, email}); }
            }
            msg += "Recipients "; upd = true;
        }
    }

    if (!upd) {
        return {200, "{\"success\":true,\"message\":\"No changes detected\"}"};
    }

    // Persist to the correct slot file (mirror saveSection routing).
    std::string stem = routeStem(section);
    const char* file = stemFile(stem);
    if (stem == "core") {
        store.setRaw(file, serializeCore(c));
    } else if (stem == "system") {
        store.setRaw(file, serializeSystem(c));
    } else if (stem == "rs232") {
        // rs232 stem persists the (unchanged) core; keep it present for routing.
        store.setRaw(file, "{}");
    } else if (stem == "recipients") {
        store.setRaw(file, serializeRecipients(c));
    } else if (stem == "schedule") {
        store.setRaw(file, "{}");
    }

    return {200, "{\"success\":true,\"message\":\"Config saved\"}"};
}

} // namespace

// ── WiFi ────────────────────────────────────────────────────────────────

TEST_CASE("POST wifi section persists ssid+password and survives reboot") {
    MockConfigStore store;
    ConfigState c;

    PostResult r = handleConfigPost(c, store, "section=wifi&ssid=MyNet&password=secret123",
                                    "application/x-www-form-urlencoded");
    CHECK(r.status == 200);
    CHECK(r.body.find("\"success\":true") != std::string::npos);
    CHECK(store.exists(K_WIFI));

    // Simulated reboot: fresh state loads from the slot file.
    ConfigState fresh;
    std::string raw;
    REQUIRE(store.load(K_WIFI, raw));
    deserializeCore(fresh, raw);
    CHECK(fresh.wifi_ssid == "MyNet");
    CHECK(fresh.wifi_password == "secret123");
}

TEST_CASE("POST password with ******** does NOT overwrite stored secret") {
    MockConfigStore store;
    ConfigState c;
    c.wifi_ssid = "OldNet";
    c.wifi_password = "real-pass";

    PostResult r = handleConfigPost(c, store,
        "section=wifi&ssid=NewNet&password=********",
        "application/x-www-form-urlencoded");
    CHECK(r.status == 200);
    CHECK(c.wifi_ssid == "NewNet");          // changed
    CHECK(c.wifi_password == "real-pass");   // masked field untouched

    std::string raw;
    REQUIRE(store.load(K_WIFI, raw));
    ConfigState fresh;
    deserializeCore(fresh, raw);
    CHECK(fresh.wifi_ssid == "NewNet");
    CHECK(fresh.wifi_password == "real-pass");
}

TEST_CASE("POST with no changes reports No changes detected") {
    MockConfigStore store;
    ConfigState c;
    PostResult r = handleConfigPost(c, store,
        "section=wifi&ssid=********&password=********",
        "application/x-www-form-urlencoded");
    CHECK(r.status == 200);
    CHECK(r.body.find("No changes detected") != std::string::npos);
    CHECK_FALSE(store.exists(K_WIFI));   // nothing persisted
}

TEST_CASE("POST invalid section returns 400 and persists nothing") {
    MockConfigStore store;
    ConfigState c;
    PostResult r = handleConfigPost(c, store, "section=banana&ssid=Net",
                                    "application/x-www-form-urlencoded");
    CHECK(r.status == 400);
    CHECK(store.size() == 0);
}

// ── JSON content type ───────────────────────────────────────────────────

TEST_CASE("POST application/json body is parsed like the form body") {
    MockConfigStore store;
    ConfigState c;
    std::string jsonBody = "{\"section\":\"email\",\"smtp_server\":\"smtp.gmail.com\","
                           "\"smtp_port\":465,\"smtp_user\":\"bot@x.com\",\"email_method\":\"smtp\"}";
    PostResult r = handleConfigPost(c, store, jsonBody, "application/json");
    CHECK(r.status == 200);
    CHECK(r.body.find("\"success\":true") != std::string::npos);

    ConfigState fresh;
    std::string raw;
    REQUIRE(store.load(K_WIFI, raw));
    deserializeCore(fresh, raw);
    CHECK(fresh.smtp_server == "smtp.gmail.com");
    CHECK(fresh.smtp_port == 465);
    CHECK(fresh.smtp_user == "bot@x.com");
    CHECK(fresh.email_method == "smtp");
}

TEST_CASE("JSON body with password mask does not wipe smtp_pass") {
    MockConfigStore store;
    ConfigState c;
    c.smtp_pass = "topsecret";
    std::string jsonBody = "{\"section\":\"email\",\"smtp_server\":\"new.server.com\",\"smtp_pass\":\"********\"}";
    PostResult r = handleConfigPost(c, store, jsonBody, "application/json");
    CHECK(r.status == 200);
    CHECK(c.smtp_pass == "topsecret");
    CHECK(c.smtp_server == "new.server.com");
}

// ── Recipients ──────────────────────────────────────────────────────────

TEST_CASE("POST recipients_json replaces the whole recipient list") {
    MockConfigStore store;
    ConfigState c;
    c.recipients.push_back({"old", "old@x.com"});

    std::string body =
        "section=recipients&recipients_json=" + urlDecode(
            "[{\"name\":\"Ala\",\"email\":\"ala@x.com\"},{\"name\":\"Bob\",\"email\":\"bob@y.com\"}]");
    PostResult r = handleConfigPost(c, store, body, "application/x-www-form-urlencoded");
    CHECK(r.status == 200);

    REQUIRE(c.recipients.size() == 2);
    CHECK(c.recipients[0].name == "Ala");
    CHECK(c.recipients[0].email == "ala@x.com");
    CHECK(c.recipients[1].email == "bob@y.com");

    std::string raw;
    REQUIRE(store.load(K_RECIP, raw));
    ConfigState fresh;
    deserializeRecipients(fresh, raw);
    REQUIRE(fresh.recipients.size() == 2);
    CHECK(fresh.recipients[0].email == "ala@x.com");
    CHECK(fresh.recipients[1].email == "bob@y.com");
}

TEST_CASE("recipients_json with invalid JSON leaves the list untouched") {
    MockConfigStore store;
    ConfigState c;
    c.recipients.push_back({"keep", "keep@x.com"});

    std::string body = "section=recipients&recipients_json={not valid";
    PostResult r = handleConfigPost(c, store, body, "application/x-www-form-urlencoded");
    CHECK(r.status == 200);
    REQUIRE(c.recipients.size() == 1);          // untouched
    CHECK(c.recipients[0].email == "keep@x.com");
    CHECK_FALSE(store.exists(K_RECIP));
}

TEST_CASE("recipient with empty email is dropped") {
    MockConfigStore store;
    ConfigState c;
    std::string body =
        "section=recipients&recipients_json=" + urlDecode(
            "[{\"name\":\"NoMail\",\"email\":\"\"},{\"name\":\"Ok\",\"email\":\"ok@x.com\"}]");
    PostResult r = handleConfigPost(c, store, body, "application/x-www-form-urlencoded");
    CHECK(r.status == 200);
    REQUIRE(c.recipients.size() == 1);
    CHECK(c.recipients[0].email == "ok@x.com");
}

// ── Section routing / stems ─────────────────────────────────────────────

TEST_CASE("system section persists to the system slot, not core") {
    MockConfigStore store;
    ConfigState c;
    PostResult r = handleConfigPost(c, store,
        "section=system&system_name=Hub01&system_language=pt&timezone=America/Sao_Paulo&ntp_server=pool.ntp.org",
        "application/x-www-form-urlencoded");
    CHECK(r.status == 200);
    CHECK(store.exists(K_SYSTEM));
    CHECK_FALSE(store.exists(K_WIFI));   // not persisted to core

    ConfigState fresh;
    std::string raw;
    REQUIRE(store.load(K_SYSTEM, raw));
    deserializeSystem(fresh, raw);
    CHECK(fresh.system_name == "Hub01");
    CHECK(fresh.system_language == "pt");
    CHECK(fresh.ntp_timezone == "America/Sao_Paulo");
    CHECK(fresh.ntp_server == "pool.ntp.org");
}

TEST_CASE("email section saves to core stem with smtp+email fields") {
    MockConfigStore store;
    ConfigState c;
    PostResult r = handleConfigPost(c, store,
        "section=email&email_destination=alerts@x.com&email_auto_send=1&email_method=google",
        "application/x-www-form-urlencoded");
    CHECK(r.status == 200);
    CHECK(store.exists(K_EMAIL));

    ConfigState fresh;
    std::string raw;
    REQUIRE(store.load(K_EMAIL, raw));
    deserializeCore(fresh, raw);
    CHECK(fresh.email_destination == "alerts@x.com");
    CHECK(fresh.email_auto_send == true);
    CHECK(fresh.email_method == "google");
}

TEST_CASE("scheduler section with no changes reports No changes detected") {
    // Real handler: "scheduler" is a valid section but handleConfigPost() has no
    // scheduler-specific setters — a bare POST leaves upd=false (nothing saved).
    MockConfigStore store;
    ConfigState c;
    PostResult r = handleConfigPost(c, store, "section=scheduler",
                                    "application/x-www-form-urlencoded");
    CHECK(r.status == 200);
    CHECK(r.body.find("No changes detected") != std::string::npos);
    CHECK_FALSE(store.exists(K_SCHED));
    CHECK_FALSE(store.exists(K_SYSTEM));
}

// ── Round-trip persistence across multiple sections ─────────────────────

TEST_CASE("wifi then system POSTs persist independently to separate slots") {
    MockConfigStore store;
    ConfigState c;

    handleConfigPost(c, store, "section=wifi&ssid=NetA&password=pwA",
                     "application/x-www-form-urlencoded");
    handleConfigPost(c, store, "section=system&system_name=MachineB",
                     "application/x-www-form-urlencoded");

    CHECK(store.exists(K_WIFI));
    CHECK(store.exists(K_SYSTEM));

    ConfigState fresh;
    std::string raw;
    REQUIRE(store.load(K_WIFI, raw));
    deserializeCore(fresh, raw);
    CHECK(fresh.wifi_ssid == "NetA");

    REQUIRE(store.load(K_SYSTEM, raw));
    deserializeSystem(fresh, raw);
    CHECK(fresh.system_name == "MachineB");
}

TEST_CASE("persisted wifi password with special chars round-trips through real JSON store") {
    MockConfigStore store;
    ConfigState c;
    // URL-encoded password: "p@ss wörd 123" (space, @, non-ASCII ö)
    PostResult r = handleConfigPost(c, store,
        "section=wifi&ssid=Net&password=p%40ss%20w%C3%B6rd%20123",
        "application/x-www-form-urlencoded");
    CHECK(r.status == 200);

    std::string raw;
    REQUIRE(store.load(K_WIFI, raw));
    ConfigState fresh;
    deserializeCore(fresh, raw);
    CHECK(fresh.wifi_ssid == "Net");
    CHECK(fresh.wifi_password == "p@ss wörd 123");
}
