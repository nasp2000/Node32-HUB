// Native tests for the status-screen helpers (screen/screen_card_logic.h).
//
// Two rules that used to be untestable inside screen.cpp:
//  - fitting text into the panel width (the 0.96" layout showed a 10-character
//    window even though 21 fit, so cards scrolled for no reason);
//  - which card is due on each 6 s turn (it was a random pick, so the primary
//    card could stay hidden for two cycles in a row).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../../chatgpt/src/screen/screen_card_logic.h"
#include "../../chatgpt/src/screen/screen_cards.h"

#include <cstring>

TEST_CASE("screenFitText keeps short text untouched") {
    char out[32];
    CHECK(screenFitText("TDK 600V", out, sizeof(out)) == 8);
    CHECK(std::strcmp(out, "TDK 600V") == 0);
}

TEST_CASE("screenFitText truncates with a marker") {
    char out[8];                     // 7 visible characters + NUL
    CHECK(screenFitText("600V 1.30A CV", out, sizeof(out)) == 7);
    CHECK(std::strcmp(out, "600V 1~") == 0);
}

TEST_CASE("screenFitText handles degenerate widths") {
    char out[4];
    CHECK(screenFitText("abc", out, 0) == 0);
    out[0] = 'x';
    CHECK(screenFitText(nullptr, out, sizeof(out)) == 0);
    CHECK(out[0] == '\0');
    CHECK(screenFitText("ab", out, 1) == 0);     // only room for the NUL
    CHECK(out[0] == '\0');
}

TEST_CASE("screenRotationIndex is deterministic and honours weights") {
    const uint8_t w[3] = {2, 1, 1};   // primary gets two turns out of four
    CHECK(screenRotationIndex(w, 3, 0) == 0);
    CHECK(screenRotationIndex(w, 3, 1) == 0);
    CHECK(screenRotationIndex(w, 3, 2) == 1);
    CHECK(screenRotationIndex(w, 3, 3) == 2);
    CHECK(screenRotationIndex(w, 3, 4) == 0);   // wraps
    CHECK(screenRotationIndex(w, 3, 5) == 0);
}

TEST_CASE("screenRotationIndex treats a zero weight as one and copes with one card") {
    const uint8_t w[2] = {0, 0};
    CHECK(screenRotationIndex(w, 2, 0) == 0);
    CHECK(screenRotationIndex(w, 2, 1) == 1);
    CHECK(screenRotationIndex(w, 1, 99) == 0);
    CHECK(screenRotationIndex(nullptr, 3, 1) == 0);
    CHECK(screenRotationIndex(w, 0, 1) == 0);
}

TEST_CASE("screenColsForWidth derives and clamps the row width") {
    CHECK(screenColsForWidth(128, 6) == 21);   // 0.96"/2.42" at text size 1
    CHECK(screenColsForWidth(64, 6) == 10);
    CHECK(screenColsForWidth(512, 6) == 60);   // clamped: renderer buffer
    CHECK(screenColsForWidth(8, 6) == 2);      // clamped: never 0/1 columns
    CHECK(screenColsForWidth(128, 0) == 0);    // bad profile -> caller must skip
    CHECK(screenColsForWidth(0, 6) == 0);
}

// --- screen_cards.h: the card model behind line 3 ---------------------------
// ScreenManager used to keep two parallel arrays (String moduleLines[16] plus
// uint8_t cardWeight[16]) filled by ~15 #if blocks. The cards are now objects
// with a full form, a compact form, a weight and a stale flag.

TEST_CASE("ScreenCardList adds cards and clamps at the capacity") {
    ScreenCardList cards;
    CHECK(cards.empty());
    CHECK(cards.add("Uptime: ok") == 0);
    CHECK(cards.add("Sched: 2 dest:1 tpl:0") == 1);
    CHECK(cards.count() == 2);
    CHECK(std::strcmp(cards.at(1)->text, "Sched: 2 dest:1 tpl:0") == 0);
    CHECK(cards.at(7) == nullptr);            // out of range, not a crash
    CHECK(cards.add(nullptr) == -1);          // null text is refused

    for (int i = cards.count(); i < SCREEN_CARD_MAX; ++i) CHECK(cards.add("x") == i);
    CHECK(cards.count() == SCREEN_CARD_MAX);
    CHECK(cards.add("overflow") == -1);       // full: dropped, no overflow
}

TEST_CASE("ScreenCardList picks the compact form only when the row is narrow") {
    ScreenCardList cards;
    cards.addCompact("TDK 600V 1.30A CV", "600V 1.30A");
    char out[SCREEN_CARD_TEXT_MAX];

    CHECK(std::strcmp(cards.displayText(0, 10, out, sizeof(out)), "600V 1.30A") == 0);
    CHECK(std::strcmp(cards.displayText(0, 21, out, sizeof(out)), "TDK 600V 1.30A CV") == 0);
    // A compact form that still does not fit is not used: the full text is
    // returned so the renderer can truncate it with '~'.
    CHECK(std::strcmp(cards.displayText(0, 6, out, sizeof(out)), "TDK 600V 1.30A CV") == 0);
}

TEST_CASE("ScreenCardList marks stale data with a leading ~") {
    ScreenCardList cards;
    cards.add("Temp: 24.5C Hum: 40% P: 1001hPa", 1, true);
    cards.addCompact("600V 1.30A", "600V 1.30A", 1, true);
    char out[SCREEN_CARD_TEXT_MAX];

    CHECK(std::strcmp(cards.displayText(0, 21, out, sizeof(out)), "~ Temp: 24.5C Hum: 40% P: 1001hPa") == 0);
    CHECK(std::strcmp(cards.displayText(1, 10, out, sizeof(out)), "~ 600V 1.30A") == 0);
    cards.setStale(0, false);
    CHECK(std::strcmp(cards.displayText(0, 21, out, sizeof(out)), "Temp: 24.5C Hum: 40% P: 1001hPa") == 0);
    // The '~' prefix must never overflow a small buffer: with 4 bytes it still
    // fits ("~ T"), with 3 it is dropped rather than truncating the text to 1.
    cards.setStale(0, true);
    char tiny4[4];
    CHECK(std::strcmp(cards.displayText(0, 10, tiny4, sizeof(tiny4)), "~ T") == 0);
    char tiny3[3];
    CHECK(std::strcmp(cards.displayText(0, 10, tiny3, sizeof(tiny3)), "Te") == 0);
}

TEST_CASE("ScreenCardList weights feed the rotation (0 means one turn)") {
    ScreenCardList cards;
    cards.add("TDK 600V 1.30A CV");
    cards.add("Sched: 0 dest:1 tpl:0", 0);     // 0 is stored as 1
    cards.add("CAN ok", 3);
    uint8_t w[SCREEN_CARD_MAX] = {0};
    cards.weights(w);
    CHECK(w[0] == 1);
    CHECK(w[1] == 1);
    CHECK(w[2] == 3);

    cards.setWeight(0, 2);                     // primary card, two turns
    cards.weights(w);
    CHECK(w[0] == 2);
    // Rotation over 2+1+3 = 6 turns visits the heavy card twice.
    int visits0 = 0, visits2 = 0;
    for (uint32_t t = 0; t < 6; ++t) {
        const int idx = (int)screenRotationIndex(w, 3, t);
        if (idx == 0) ++visits0;
        if (idx == 2) ++visits2;
    }
    CHECK(visits0 == 2);
    CHECK(visits2 == 3);
}

TEST_CASE("ScreenCardList truncates long texts instead of overrunning") {
    ScreenCardList cards;
    char big[200];
    std::memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CHECK(cards.add(big) == 0);
    CHECK(std::strlen(cards.at(0)->text) == SCREEN_CARD_TEXT_MAX - 1);
    char out[SCREEN_CARD_TEXT_MAX];
    CHECK(std::strlen(cards.displayText(0, 21, out, sizeof(out))) == SCREEN_CARD_TEXT_MAX - 1);
}
