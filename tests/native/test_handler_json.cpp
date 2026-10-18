#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <sstream>

// Standalone test for the JSON-building helpers that the unified web handlers
// (handlers_unified/*.cpp) rely on to produce API responses. Mirrors:
//   - jsonEscape()/escapeJsonStr() from grbl_handler.cpp / recipients_handler.cpp
//   - sendSimpleActionJson() payload structure (grbl_handler.cpp)
//   - status-style JSON object construction (uptime/heap/version keys)
// The frontend parses these strings directly — a missing escape or missing key
// breaks the UI.
namespace {

// Mirrors recipients_handler.cpp::escapeJsonStr() — full escaping incl. control
// chars emitted as \uXXXX (grbl_handler.cpp's jsonEscape omits \uXXXX).
std::string jsonEscape(const std::string& raw) {
    std::ostringstream out;
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = (unsigned char)raw[i];
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"':  out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)c);
                    out << buf;
                } else {
                    out << (char)c;
                }
                break;
        }
    }
    return out.str();
}

// Mirrors sendSimpleActionJson() payload assembly (grbl_handler.cpp).
struct ActionJson {
    bool success;
    std::string message;
    std::string filePath;
    bool hasFilePath;
    size_t bytes;
    bool hasBytes;
};

std::string buildActionJson(const ActionJson& a) {
    std::string payload = "{\"success\":";
    payload += a.success ? "true" : "false";
    payload += ",\"message\":\"" + jsonEscape(a.message) + "\"";
    if (a.hasFilePath && a.filePath.length() > 0) {
        payload += ",\"file_path\":\"" + jsonEscape(a.filePath) + "\"";
    }
    if (a.hasBytes && a.bytes > 0) {
        payload += ",\"bytes\":" + std::to_string((unsigned long)a.bytes);
    }
    payload += "}";
    return payload;
}

// Mirrors a status-style JSON object (uptime/heap/version/state keys).
std::string buildStatusJson(unsigned long uptimeSec, unsigned long heapFree,
                            const std::string& version, const std::string& state) {
    std::string j = "{";
    j += "\"uptime\":" + std::to_string(uptimeSec);
    j += ",\"heap_free\":" + std::to_string(heapFree);
    j += ",\"version\":\"" + jsonEscape(version) + "\"";
    j += ",\"state\":\"" + jsonEscape(state) + "\"";
    j += "}";
    return j;
}

bool jsonHasKey(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\":") != std::string::npos;
}

} // namespace

TEST_CASE("jsonEscape escapes quotes and backslashes") {
    CHECK(jsonEscape("say \"hi\"") == "say \\\"hi\\\"");
    CHECK(jsonEscape("a\\b") == "a\\\\b");
    CHECK(jsonEscape("") == "");
    CHECK(jsonEscape("plain text") == "plain text");
}

TEST_CASE("jsonEscape escapes newlines, CR and tabs") {
    CHECK(jsonEscape("line1\nline2") == "line1\\nline2");
    CHECK(jsonEscape("cr\rhere") == "cr\\rhere");
    CHECK(jsonEscape("a\tb") == "a\\tb");
}

TEST_CASE("jsonEscape encodes control chars as \\uXXXX") {
    CHECK(jsonEscape(std::string(1, (char)0x01)) == "\\u0001");
    CHECK(jsonEscape(std::string(1, (char)0x1F)) == "\\u001F");
    CHECK(jsonEscape("A" + std::string(1, (char)0x07) + "B") == "A\\u0007B");
}

TEST_CASE("jsonEscape output is valid JSON string content (no raw control bytes)") {
    std::string evil;
    for (int c = 0; c < 32; ++c) evil += (char)c;   // all control chars
    std::string escaped = jsonEscape(evil);
    // none of the raw control chars survive unescaped
    for (int c = 1; c < 32; ++c) {
        std::string raw(1, (char)c);
        CHECK(escaped.find(raw) == std::string::npos);
    }
    CHECK(escaped.find("\\u") != std::string::npos);
}

TEST_CASE("simple action JSON contains all expected keys on success") {
    ActionJson a;
    a.success = true; a.message = "done"; a.hasFilePath = true; a.filePath = "/dir/f.nc";
    a.hasBytes = true; a.bytes = 4096;
    std::string j = buildActionJson(a);
    CHECK(jsonHasKey(j, "success"));
    CHECK(jsonHasKey(j, "message"));
    CHECK(jsonHasKey(j, "file_path"));
    CHECK(jsonHasKey(j, "bytes"));
    CHECK(j.find("\"success\":true") != std::string::npos);
    CHECK(j.find("\"file_path\":\"/dir/f.nc\"") != std::string::npos);
    CHECK(j.find("\"bytes\":4096") != std::string::npos);
    CHECK(j.back() == '}');
}

TEST_CASE("optional file_path omitted when empty even if requested") {
    ActionJson a;
    a.success = true; a.message = "ok"; a.hasFilePath = true; a.filePath = "";
    a.hasBytes = false; a.bytes = 0;
    std::string j = buildActionJson(a);
    CHECK_FALSE(jsonHasKey(j, "file_path"));
    CHECK_FALSE(jsonHasKey(j, "bytes"));
    CHECK(j.find("\"message\":\"ok\"") != std::string::npos);
}

TEST_CASE("failed action uses success false and carries message") {
    ActionJson a;
    a.success = false; a.message = "invalid input"; a.hasFilePath = false;
    a.hasBytes = false;
    std::string j = buildActionJson(a);
    CHECK(j.find("\"success\":false") != std::string::npos);
    CHECK(j.find("\"message\":\"invalid input\"") != std::string::npos);
}

TEST_CASE("message with quote/newline does not break JSON structure") {
    ActionJson a;
    a.success = true; a.message = "he said \"no\"\nnext line"; a.hasFilePath = false;
    a.hasBytes = false;
    std::string j = buildActionJson(a);
    // structure must still be exactly two keys, message properly quoted
    CHECK(j == "{\"success\":true,\"message\":\"he said \\\"no\\\"\\nnext line\"}");
}

TEST_CASE("status JSON has all expected keys") {
    std::string j = buildStatusJson(12345, 220000, "v1.091", "idle");
    CHECK(jsonHasKey(j, "uptime"));
    CHECK(jsonHasKey(j, "heap_free"));
    CHECK(jsonHasKey(j, "version"));
    CHECK(jsonHasKey(j, "state"));
    CHECK(j.find("\"uptime\":12345") != std::string::npos);
    CHECK(j.find("\"heap_free\":220000") != std::string::npos);
    CHECK(j.find("\"version\":\"v1.091\"") != std::string::npos);
}

TEST_CASE("status JSON handles large and negative-ish numeric values") {
    std::string j = buildStatusJson(4294967295UL, 0, "v1.091", "idle");
    CHECK(j.find("\"uptime\":4294967295") != std::string::npos);
    CHECK(j.find("\"heap_free\":0") != std::string::npos);
}

TEST_CASE("version string with special chars stays escaped and parseable") {
    std::string j = buildStatusJson(1, 100, "v1.9\"beta\"", "run");
    CHECK(j.find("\"version\":\"v1.9\\\"beta\\\"\"") != std::string::npos);
    // the escaped sequence must not contain a bare quote inside the value
    CHECK(j.find("\"v1.9\"beta\"\"") == std::string::npos);
}

TEST_CASE("responses larger than 4KB are built without truncation") {
    // Frontend/streaming expects complete bodies; build a >4KB message payload.
    std::string big;
    for (int i = 0; i < 2000; ++i) big += "data line " + std::to_string(i) + "\n";
    CHECK(big.size() > 4096);

    ActionJson a;
    a.success = true; a.message = big; a.hasFilePath = false; a.hasBytes = false;
    std::string j = buildActionJson(a);
    CHECK(j.size() > big.size());
    CHECK(j.back() == '}');
    CHECK(j.find("data line 1999\\n") != std::string::npos);   // fully preserved
}

TEST_CASE("arbitrary binary-ish input does not break the JSON builder") {
    std::string weird;
    for (int i = 0; i < 256; ++i) weird += (char)i;
    std::string escaped = jsonEscape(weird);
    // every byte above 0x1F and not " \ survives verbatim; no crash
    CHECK(escaped.size() > 0);
    // the escape round-trips printable ASCII exactly
    CHECK(jsonEscape("hello world 123!") == "hello world 123!");
}
