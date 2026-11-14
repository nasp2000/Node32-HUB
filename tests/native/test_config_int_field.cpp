// Native tests for strict numeric POST validation (chatgpt/src/config_int_field.h).
//
// BACKLOG ACC-2: handleAccessConfigPost() read numbers with a getInt() lambda
// that returned -1 for absent, non-numeric AND out-of-range values alike, so a
// malformed or out-of-range POST was skipped in silence and the client still
// got {"success":true} while the old value stayed in place.
//
// These cases reproduce that defect (old semantics: nothing applied, no error)
// and prove the fix: the present-but-unusable field is named in a 400 payload
// with its accepted range, the whole request is rejected (nothing written), and
// a fully valid group still applies. The group loop below mirrors
// handleAccessConfigPost() field-for-field.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdlib>
#include <map>
#include <string>

#include "config_int_field.h"

namespace {

struct FieldSpec {
    const char *key;
    int minVal;
    int maxVal;
};

// Same table as the handler.
const FieldSpec kFields[] = {
    { "web_max_sessions",     1,   5 },
    { "web_idle_timeout_min", 1, 120 },
    { "web_abs_timeout_min",  5, 240 },
};
const int kFieldCount = (int)(sizeof(kFields) / sizeof(kFields[0]));

struct AccStore {
    int maxSessions;
    int idle;
    int abs;
};

// OLD handler semantics: getInt(key,min,max) -> -1 on absent/invalid/range,
// caller applies only >= 0.
int oldGetInt(const std::map<std::string, std::string> &post,
              const char *key, int minVal, int maxVal) {
    auto it = post.find(key);
    if (it == post.end() || it->second.empty()) return -1;
    int val = atoi(it->second.c_str());          // String::toInt() truncates too
    return (val >= minVal && val <= maxVal) ? val : -1;
}

void oldApply(const std::map<std::string, std::string> &post, AccStore &st) {
    int v = oldGetInt(post, "web_max_sessions", 1, 5);
    if (v >= 0) st.maxSessions = v;
    v = oldGetInt(post, "web_idle_timeout_min", 1, 120);
    if (v >= 0) st.idle = v;
    v = oldGetInt(post, "web_abs_timeout_min", 5, 240);
    if (v >= 0) st.abs = v;
}

struct Outcome {
    bool applied;          // true -> 200 {"success":true}
    int code;              // 400 when rejected
    std::string body;      // error payload when rejected
};

// NEW handler semantics: validate every posted numeric field first, reject the
// whole request on the first unusable one, otherwise apply all present values.
Outcome applyNew(const std::map<std::string, std::string> &post, AccStore &st) {
    Outcome out{false, 400, ""};
    int value[kFieldCount];
    bool present[kFieldCount];
    for (int i = 0; i < kFieldCount; i++) {
        value[i] = 0;
        present[i] = false;
        if (!post.count(kFields[i].key)) continue;   // absent = leave alone
        const std::string &raw = post.at(kFields[i].key);
        ConfigIntParseResult parsed =
            configIntParse(raw.c_str(), raw.size(), kFields[i].minVal, kFields[i].maxVal);
        if (parsed.status != CONFIG_INT_OK) {
            char buf[256];
            configIntErrorJson(buf, sizeof(buf), kFields[i].key,
                               kFields[i].minVal, kFields[i].maxVal, parsed.status);
            out.body = buf;
            return out;
        }
        value[i] = parsed.value;
        present[i] = true;
    }
    if (present[0]) st.maxSessions = value[0];
    if (present[1]) st.idle = value[1];
    if (present[2]) st.abs = value[2];
    out.applied = true;
    out.code = 200;
    return out;
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("defect: the old getInt drops malformed and out-of-range values silently") {
    AccStore st{3, 10, 20};
    // "9" is outside 1..5 and "abc" is not a number: the old lambda returns -1
    // for both, the caller skips them, and no error is produced at all.
    oldApply({{"web_max_sessions", "9"}}, st);
    CHECK(st.maxSessions == 3);          // silently kept the old value

    oldApply({{"web_idle_timeout_min", "abc"}}, st);
    CHECK(st.idle == 10);                // silently kept the old value

    // String::toInt() truncates "12abc" to 12 — an in-range value is applied
    // even though the payload is not a number.
    oldApply({{"web_max_sessions", "12abc"}}, st);
    CHECK(st.maxSessions == 3);          // 12 is out of range, so still skipped
    oldApply({{"web_max_sessions", "3abc"}}, st);
    CHECK(st.maxSessions == 3);          // applied only because 3 happens to parse
}

TEST_CASE("out-of-range posts are rejected with the field and its range") {
    AccStore st{3, 10, 20};
    Outcome low = applyNew({{"web_max_sessions", "0"}}, st);
    CHECK_FALSE(low.applied);
    CHECK(low.code == 400);
    CHECK(contains(low.body, "\"success\":false"));
    CHECK(contains(low.body, "\"field\":\"web_max_sessions\""));
    CHECK(contains(low.body, "\"min\":1"));
    CHECK(contains(low.body, "\"max\":5"));
    CHECK(contains(low.body, "\"error\":\"out_of_range\""));
    CHECK(contains(low.body, "web_max_sessions"));
    CHECK(st.maxSessions == 3);          // nothing written

    Outcome high = applyNew({{"web_abs_timeout_min", "241"}}, st);
    CHECK_FALSE(high.applied);
    CHECK(contains(high.body, "\"field\":\"web_abs_timeout_min\""));
    CHECK(contains(high.body, "\"max\":240"));
    CHECK(contains(high.body, "\"error\":\"out_of_range\""));
    CHECK(st.abs == 20);
}

TEST_CASE("malformed numbers are rejected instead of being truncated") {
    AccStore st{3, 10, 20};
    for (const char *bad : { "abc", "12abc", "1.5", "3x", "--3", "3 3" }) {
        Outcome o = applyNew({{"web_max_sessions", bad}}, st);
        CAPTURE(bad);
        CHECK_FALSE(o.applied);
        CHECK(o.code == 400);
        CHECK(contains(o.body, "\"field\":\"web_max_sessions\""));
        CHECK(contains(o.body, "\"error\":\"not_a_number\""));
        CHECK(st.maxSessions == 3);
    }
}

TEST_CASE("range boundaries are accepted on both sides") {
    AccStore st{3, 10, 20};
    CHECK(applyNew({{"web_max_sessions", "1"}}, st).applied);
    CHECK(st.maxSessions == 1);
    CHECK(applyNew({{"web_max_sessions", "5"}}, st).applied);
    CHECK(st.maxSessions == 5);
    CHECK(applyNew({{"web_idle_timeout_min", "120"}}, st).applied);
    CHECK(st.idle == 120);
    CHECK(applyNew({{"web_abs_timeout_min", "240"}}, st).applied);
    CHECK(st.abs == 240);
}

TEST_CASE("absent keys stay legal; a present empty value is rejected") {
    AccStore st{3, 10, 20};
    // Partial POST (scripts, other pages): only one field, no error.
    Outcome partial = applyNew({{"web_idle_timeout_min", "15"}}, st);
    CHECK(partial.applied);
    CHECK(st.idle == 15);
    CHECK(st.maxSessions == 3);          // untouched
    CHECK(st.abs == 20);

    // Present but blank is a value the user submitted: reject it explicitly.
    Outcome empty = applyNew({{"web_idle_timeout_min", ""}}, st);
    CHECK_FALSE(empty.applied);
    CHECK(contains(empty.body, "\"error\":\"empty\""));
    CHECK(contains(empty.body, "\"field\":\"web_idle_timeout_min\""));
    CHECK(st.idle == 15);                // unchanged
}

TEST_CASE("whitespace and a leading plus are tolerated, a bare sign is not") {
    AccStore st{3, 10, 20};
    CHECK(applyNew({{"web_max_sessions", " 4 "}}, st).applied);
    CHECK(st.maxSessions == 4);
    CHECK(applyNew({{"web_max_sessions", "+2"}}, st).applied);
    CHECK(st.maxSessions == 2);
    CHECK(applyNew({{"web_max_sessions", "-2"}}, st).applied == false);
    CHECK(contains(applyNew({{"web_max_sessions", "-"}}, st).body, "\"error\":\"not_a_number\""));
    CHECK(contains(applyNew({{"web_max_sessions", "+"}}, st).body, "\"error\":\"not_a_number\""));
    CHECK(st.maxSessions == 2);
}

TEST_CASE("a very long digit string cannot wrap back into the accepted range") {
    std::string huge(60, '9');
    ConfigIntParseResult r = configIntParse(huge.c_str(), huge.size(), 1, 5);
    CHECK(r.status == CONFIG_INT_OUT_OF_RANGE);
    // Leading zeros are still a valid number.
    ConfigIntParseResult z = configIntParse("0000000000000003", 16, 1, 5);
    CHECK(z.status == CONFIG_INT_OK);
    CHECK(z.value == 3);
}

TEST_CASE("one bad field rejects the whole group, nothing is written") {
    AccStore st{3, 10, 20};
    Outcome o = applyNew({{"web_max_sessions", "4"},
                          {"web_idle_timeout_min", "0"},   // invalid
                          {"web_abs_timeout_min", "30"}}, st);
    CHECK_FALSE(o.applied);
    CHECK(contains(o.body, "\"field\":\"web_idle_timeout_min\""));
    CHECK(st.maxSessions == 3);          // valid field must NOT be applied
    CHECK(st.idle == 10);
    CHECK(st.abs == 20);
}

TEST_CASE("a valid group applies every posted field") {
    AccStore st{3, 10, 20};
    Outcome o = applyNew({{"web_max_sessions", "4"},
                          {"web_idle_timeout_min", "15"},
                          {"web_abs_timeout_min", "30"}}, st);
    CHECK(o.applied);
    CHECK(o.code == 200);
    CHECK(o.body.empty());
    CHECK(st.maxSessions == 4);
    CHECK(st.idle == 15);
    CHECK(st.abs == 30);
}

TEST_CASE("the error payload is always NUL-terminated, even when truncated") {
    char small[40];
    int would = configIntErrorJson(small, sizeof(small), "web_max_sessions", 1, 5,
                                   CONFIG_INT_OUT_OF_RANGE);
    CHECK(would > (int)sizeof(small) - 1);   // full body did not fit
    CHECK(small[sizeof(small) - 1] == '\0');
    CHECK(std::string(small).size() == sizeof(small) - 1);
    // The untruncated payload carries every key the page may read.
    char full[256];
    configIntErrorJson(full, sizeof(full), "web_max_sessions", 1, 5, CONFIG_INT_NOT_A_NUMBER);
    std::string body(full);
    CHECK(contains(body, "\"success\":false"));
    CHECK(contains(body, "\"field\":\"web_max_sessions\""));
    CHECK(contains(body, "\"error\":\"not_a_number\""));
    CHECK(contains(body, "must be a whole number"));
}
