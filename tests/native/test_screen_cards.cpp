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
