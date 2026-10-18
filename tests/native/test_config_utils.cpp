#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Arduino.h"
#include "../../chatgpt/src/config_utils.h"

TEST_CASE("normalizeOp: single-char operators") {
    CHECK(normalizeOp("I") == "I");
    CHECK(normalizeOp("D") == "D");
    CHECK(normalizeOp("G") == "G");
    CHECK(normalizeOp("L") == "L");
    CHECK(normalizeOp("R") == "R");
    CHECK(normalizeOp("V") == "V");
    CHECK(normalizeOp("C") == "C");
    CHECK(normalizeOp("P") == "P");
    CHECK(normalizeOp("+") == "+");
    CHECK(normalizeOp("-") == "-");
}

TEST_CASE("normalizeOp: multi-char operators") {
    CHECK(normalizeOp("DF") == "DF");
    CHECK(normalizeOp("DR") == "DF");  // DR maps to DF
    CHECK(normalizeOp("Cw") == "Cw");
    CHECK(normalizeOp("Hy") == "Hy");
}

TEST_CASE("normalizeOp: case insensitivity") {
    CHECK(normalizeOp("i") == "I");
    CHECK(normalizeOp("d") == "D");
    CHECK(normalizeOp("g") == "G");
    CHECK(normalizeOp("l") == "L");
    CHECK(normalizeOp("r") == "R");
    CHECK(normalizeOp("v") == "V");
    CHECK(normalizeOp("c") == "C");
    CHECK(normalizeOp("p") == "P");
    CHECK(normalizeOp("cw") == "Cw");
    CHECK(normalizeOp("CW") == "Cw");
    CHECK(normalizeOp("hy") == "Hy");
    CHECK(normalizeOp("HY") == "Hy");
    CHECK(normalizeOp("df") == "DF");
    CHECK(normalizeOp("Df") == "DF");
    CHECK(normalizeOp("dr") == "DF");
}

TEST_CASE("normalizeOp: unknown operators fallback to I") {
    CHECK(normalizeOp("") == "I");
    CHECK(normalizeOp("X") == "I");
    CHECK(normalizeOp("ZZ") == "I");
    CHECK(normalizeOp("!@#") == "I");
    CHECK(normalizeOp("abc") == "I");
}

TEST_CASE("normalizeOp: plus and minus preserved exactly") {
    CHECK(normalizeOp("+") == "+");
    CHECK(normalizeOp("-") == "-");
    // uppercase version of + and - is same (no-op)
    CHECK(normalizeOp("+") == "+");
    CHECK(normalizeOp("-") == "-");
}

TEST_CASE("fnv1aHash: empty string is offset basis") {
    CHECK(fnv1aHash("") == 2166136261u);
}

TEST_CASE("fnv1aHash: determinism") {
    uint32_t h1 = fnv1aHash("the quick brown fox");
    uint32_t h2 = fnv1aHash("the quick brown fox");
    CHECK(h1 == h2);
}

TEST_CASE("fnv1aHash: different inputs differ") {
    CHECK(fnv1aHash("abc") != fnv1aHash("abd"));
    CHECK(fnv1aHash("abc") != fnv1aHash("ABC"));
    CHECK(fnv1aHash("a") != fnv1aHash("aa"));
}

TEST_CASE("fnv1aHash: special characters") {
    CHECK(fnv1aHash("cfg_web_pwd") != 0u);
    CHECK(fnv1aHash("cfg_tlnt_pwd") != 0u);
    CHECK(fnv1aHash("smtp.gmail.com") != 0u);
    CHECK(fnv1aHash("192.168.1.100") != 0u);
}

TEST_CASE("fnv1aHash: long strings no collision") {
    String a; while ((int)a.length() < 1000) a += "a";
    String b; while ((int)b.length() < 1000) b += "b";
    CHECK(fnv1aHash(a) != fnv1aHash(b));
}

TEST_CASE("shortKey: known mappings") {
    CHECK(shortKey("server") == "s");
    CHECK(shortKey("port") == "p");
    CHECK(shortKey("user") == "u");
    CHECK(shortKey("password") == "pw");
    CHECK(shortKey("ssid") == "id");
    CHECK(shortKey("encryption") == "enc");
    CHECK(shortKey("enabled") == "en");
    CHECK(shortKey("interval") == "i");
    CHECK(shortKey("command") == "c");
    CHECK(shortKey("template") == "t");
    CHECK(shortKey("recipients") == "rcp");
    CHECK(shortKey("from_name") == "fn");
    CHECK(shortKey("from_email") == "fe");
    CHECK(shortKey("secure") == "sec");
    CHECK(shortKey("baud_rate") == "b");
    CHECK(shortKey("data_bits") == "db");
    CHECK(shortKey("stop_bits") == "sb");
    CHECK(shortKey("parity") == "par");
    CHECK(shortKey("flow_control") == "fc");
    CHECK(shortKey("days") == "d");
    CHECK(shortKey("unit") == "un");
    CHECK(shortKey("subject") == "sub");
    CHECK(shortKey("body") == "bd");
    CHECK(shortKey("include_timestamp") == "its");
    CHECK(shortKey("max_lines") == "ml");
    CHECK(shortKey("inactivity_timeout") == "it");
}

TEST_CASE("shortKey: pass alias maps to pw") {
    CHECK(shortKey("pass") == "pw");
}

TEST_CASE("shortKey: unknown key passes through") {
    CHECK(shortKey("unknown_key") == "unknown_key");
    CHECK(shortKey("custom_field") == "custom_field");
    CHECK(shortKey("") == "");
}

TEST_CASE("longKey: known reverse mappings") {
    CHECK(longKey("s") == "server");
    CHECK(longKey("p") == "port");
    CHECK(longKey("u") == "user");
    CHECK(longKey("pw") == "password");
    CHECK(longKey("id") == "ssid");
    CHECK(longKey("enc") == "encryption");
    CHECK(longKey("en") == "enabled");
    CHECK(longKey("i") == "interval");
    CHECK(longKey("c") == "command");
    CHECK(longKey("t") == "template");
    CHECK(longKey("rcp") == "recipients");
    CHECK(longKey("fn") == "from_name");
    CHECK(longKey("fe") == "from_email");
    CHECK(longKey("sec") == "secure");
    CHECK(longKey("b") == "baud_rate");
    CHECK(longKey("db") == "data_bits");
    CHECK(longKey("sb") == "stop_bits");
    CHECK(longKey("par") == "parity");
    CHECK(longKey("fc") == "flow_control");
    CHECK(longKey("d") == "days");
    CHECK(longKey("un") == "unit");
    CHECK(longKey("sub") == "subject");
    CHECK(longKey("bd") == "body");
    CHECK(longKey("its") == "include_timestamp");
    CHECK(longKey("ml") == "max_lines");
    CHECK(longKey("it") == "inactivity_timeout");
}

TEST_CASE("longKey: unknown short key passes through") {
    CHECK(longKey("zz") == "zz");
    CHECK(longKey("") == "");
}

TEST_CASE("key mapping: round-trip short->long->short") {
    const char* allShorts[] = {
        "s", "p", "u", "pw", "id", "enc", "en", "i", "c", "t",
        "rcp", "fn", "fe", "sec", "b", "db", "sb", "par", "fc",
        "d", "un", "sub", "bd", "its", "ml", "it"
    };
    for (const char* sk : allShorts) {
        String expanded = longKey(sk);
        String recompressed = shortKey(expanded);
        CHECK(recompressed == sk);
    }
}

TEST_CASE("key mapping: round-trip long->short->long") {
    const char* allLongs[] = {
        "server", "port", "user", "password", "ssid", "encryption",
        "enabled", "interval", "command", "template", "recipients",
        "from_name", "from_email", "secure", "baud_rate", "data_bits",
        "stop_bits", "parity", "flow_control", "days", "unit",
        "subject", "body", "include_timestamp", "max_lines", "inactivity_timeout"
    };
    for (const char* lk : allLongs) {
        String compressed = shortKey(lk);
        String reexpanded = longKey(compressed);
        CHECK(reexpanded == lk);
    }
}

TEST_CASE("clampSchedCmdActive: bounds") {
    CHECK(clampSchedCmdActive(1) == 1);
    CHECK(clampSchedCmdActive(10) == 10);
    CHECK(clampSchedCmdActive(5) == 5);
    CHECK(clampSchedCmdActive(0) == 1);    // below min
    CHECK(clampSchedCmdActive(-1) == 1);   // negative
    CHECK(clampSchedCmdActive(11) == 10);  // above max
    CHECK(clampSchedCmdActive(100) == 10);
}

TEST_CASE("clampSchedDtActive: bounds") {
    CHECK(clampSchedDtActive(1) == 1);
    CHECK(clampSchedDtActive(8) == 8);
    CHECK(clampSchedDtActive(4) == 4);
    CHECK(clampSchedDtActive(0) == 1);
    CHECK(clampSchedDtActive(-5) == 1);
    CHECK(clampSchedDtActive(9) == 8);
    CHECK(clampSchedDtActive(20) == 8);
}
