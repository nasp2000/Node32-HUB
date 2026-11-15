// Native tests for the `interval` parameter of POST /api/scheduled-command-slot
// (BACKLOG CMD-6).
//
// The handler read the cadence with getParamInt() → String::toInt(), so:
//   * "12x" was stored as 12 (a typo became a schedule), and
//   * a missing interval collapsed to 0, which only failed later because of a
//     `interval <= 0` guard — the request was never actually understood.
//
// The rule now lives in commands_params.h: Missing / Invalid / Ok, and an
// enabled row is refused unless the value is a clean positive decimal, while a
// disabled row is parked with 0 (the field is irrelevant while it is off).
//
// Part A covers the parser and the accept/reject decision (real code, both
// directions), part B pins that the handler uses it and keeps its wire contract.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/commands_params.h"

#include <cstdio>
#include <string>

#ifndef SRC_DIR_PATH
#define SRC_DIR_PATH "../../chatgpt/src"
#endif

namespace {

int stateOf(const char *raw, int *value = nullptr) {
    int local = -1;
    int *out = value ? value : &local;
    return (int)parseIntervalParam(raw, out);
}

const int kMissing = (int)IntervalParam::Missing;
const int kInvalid = (int)IntervalParam::Invalid;
const int kOk = (int)IntervalParam::Ok;

std::string readSrc(const char *rel) {
    std::string path = std::string(SRC_DIR_PATH) + "/" + rel;
    FILE *f = fopen(path.c_str(), "rb");
    REQUIRE_MESSAGE(f != nullptr, "cannot read " << path);
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

std::string slice(const std::string &text, const char *from, const char *to) {
    const size_t start = text.find(from);
    REQUIRE_MESSAGE(start != std::string::npos, "marker not found: " << from);
    const size_t end = text.find(to, start + 1);
    REQUIRE_MESSAGE(end != std::string::npos, "end marker not found: " << to);
    return text.substr(start, end - start);
}

} // namespace

// ── Part A: what counts as an interval ──────────────────────────────────────

TEST_CASE("absent values are Missing, never a silent 0") {
    int value = 99;
    CHECK(stateOf(nullptr, &value) == kMissing);
    CHECK(value == 0);
    value = 99;
    CHECK(stateOf("", &value) == kMissing);
    CHECK(value == 0);
    // The parser must also be safe without an out pointer.
    CHECK(stateOf("") == kMissing);
    CHECK(stateOf(nullptr) == kMissing);
}

TEST_CASE("anything that is not a clean positive decimal is Invalid") {
    for (const char *bad : {"0", "00", "-3", "+5", " 5", "5 ", "12x", "x12", "1e3",
                            "5 s", "5.0", "0x10", "9999999999"}) {
        INFO("value: " << bad);
        int value = 42;
        CHECK(stateOf(bad, &value) == kInvalid);
        CHECK(value == 0); // never leaks a half-parsed number into the config
    }
}

TEST_CASE("a positive decimal is Ok and returns the exact value") {
    int value = 0;
    CHECK(stateOf("1", &value) == kOk);
    CHECK(value == 1);
    CHECK(stateOf("5", &value) == kOk);
    CHECK(value == 5);
    CHECK(stateOf("3600", &value) == kOk);
    CHECK(value == 3600);
    CHECK(stateOf("999999999", &value) == kOk); // 9 digits: int-safe
    CHECK(value == 999999999);
}

// ── Part A: the save decision, i.e. "enabled with/without interval" ─────────

TEST_CASE("an enabled row without a usable interval is refused") {
    for (int st : {kMissing, kInvalid}) {
        const IntervalDecision d = decideIntervalForSave(true, (IntervalParam)st, 0);
        INFO("state: " << st);
        CHECK(d.accept == false);
        CHECK(d.interval == 0);
    }
}

TEST_CASE("an enabled row with a valid interval is accepted with that value") {
    const IntervalDecision d = decideIntervalForSave(true, IntervalParam::Ok, 7);
    CHECK(d.accept == true);
    CHECK(d.interval == 7);
}

TEST_CASE("a disabled row is parked with 0 whatever the input says") {
    CHECK(decideIntervalForSave(false, IntervalParam::Missing, 0).accept == true);
    CHECK(decideIntervalForSave(false, IntervalParam::Missing, 0).interval == 0);
    CHECK(decideIntervalForSave(false, IntervalParam::Invalid, 0).accept == true);
    CHECK(decideIntervalForSave(false, IntervalParam::Invalid, 0).interval == 0);
    // A disabled row still keeps a valid value the client sent (round-trip).
    CHECK(decideIntervalForSave(false, IntervalParam::Ok, 90).interval == 90);
}

// ── Part B: the handler follows the rule and keeps its wire contract ────────

TEST_CASE("POST /api/scheduled-command-slot validates through commands_params.h") {
    const std::string handler = readSrc("webpages/handlers_unified/commands_handler.cpp");
    const std::string post = slice(handler,
                                   "void handleScheduledCommandSlotPost(",
                                   "#endif // ENABLE_SCHEDULER");

    // Strict parse, no String::toInt() shortcut left for this parameter.
    CHECK(post.find("parseIntervalParam(") != std::string::npos);
    CHECK(post.find("decideIntervalForSave(") != std::string::npos);
    CHECK(post.find("getParamInt(src, \"interval\"") == std::string::npos);
    CHECK(post.find("int interval = getParamInt(") == std::string::npos);
    // The value stored comes from the decision, not from a raw parse.
    CHECK(post.find("int interval = intervalDecision.interval;") != std::string::npos);

    // Refusal path: 400 "interval" and nothing written before it.
    const size_t refuse = post.find("if (!intervalDecision.accept)");
    REQUIRE(refuse != std::string::npos);
    const size_t intervalErr = post.find("\\\"msg\\\":\\\"interval\\\"", refuse);
    REQUIRE(intervalErr != std::string::npos);
    const size_t setSlot = post.find("setSchedCmdSlot(");
    REQUIRE(setSlot != std::string::npos);
    CHECK(intervalErr < setSlot);

    // The previous contract is preserved: an enabled row with no command still
    // answers "cmd" first (the interval check comes second).
    const size_t cmdErr = post.find("\\\"msg\\\":\\\"cmd\\\"");
    REQUIRE(cmdErr != std::string::npos);
    CHECK(cmdErr < refuse);
}
