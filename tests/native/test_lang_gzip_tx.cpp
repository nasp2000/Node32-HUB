// Native tests for the /api/lang gzip truncation accounting
// (chatgpt/src/lang_gzip_tx.h).
//
// BACKLOG SD-11: a 48 KB pre-compressed response can be cut short under heap
// pressure (declared Content-Length vs bytes the client actually received),
// and the device recorded nothing. These cases reproduce a truncated send and
// prove the counter increments, the heap snapshot is kept, and a completed
// transfer is not counted. The `complete` flag also catches the subtler OOM
// stall where every byte was "sent" to the library's buffer but the response
// never reached RESPONSE_END.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstring>
#include <string>

#include "lang_gzip_tx.h"

namespace {

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("a completed transfer is counted but not reported as truncation") {
    LangGzipTxStats st;
    langGzipTxInit(&st);
    // 47889 B declared, all 47889 handed to TCP, library reached RESPONSE_END.
    CHECK_FALSE(langGzipTxRecord(&st, 47889, 47889, true, 45000, 38000, 47000));
    CHECK(st.served == 1);
    CHECK(st.completed == 1);
    CHECK(st.incomplete == 0);
    CHECK(st.lastDeclared == 0);      // no truncation to remember
    CHECK(st.lastSent == 0);
}

TEST_CASE("defect: a send that stops short increments the counter") {
    LangGzipTxStats st;
    langGzipTxInit(&st);
    // The client got 1000 bytes fewer than the declared Content-Length — the
    // ERR_CONTENT_LENGTH_MISMATCH case.
    CHECK(langGzipTxRecord(&st, 47889, 46889, false, 42000, 36000, 47000));
    CHECK(st.served == 1);
    CHECK(st.incomplete == 1);
    CHECK(st.completed == 0);
    CHECK(st.lastDeclared == 47889);
    CHECK(st.lastSent == 46889);
    CHECK(st.lastFreeHeap == 42000);
    CHECK(st.lastMinFreeHeap == 36000);
    CHECK(st.lastMaxAlloc == 47000);

    char line[256];
    int n = langGzipTxFormat(line, sizeof(line), &st);
    CHECK(n > 0);
    CHECK(n < (int)sizeof(line));
    std::string text(line);
    CHECK(contains(text, "declared=47889"));
    CHECK(contains(text, "sent=46889"));
    CHECK(contains(text, "shortfall=1000"));
    CHECK(contains(text, "free_heap=42000"));
    CHECK(contains(text, "incomplete=1/1"));
}

TEST_CASE("an OOM stall counts even when every byte was marked sent") {
    LangGzipTxStats st;
    langGzipTxInit(&st);
    // The library increments its sent counter while filling the send buffer; if
    // that buffer could not be allocated the response never reaches END, so
    // sent == declared is not proof of delivery.
    CHECK(langGzipTxRecord(&st, 47889, 47889, false, 12000, 9000, 15000));
    CHECK(st.incomplete == 1);
    CHECK(st.lastDeclared == 47889);
    CHECK(st.lastSent == 47889);

    char line[256];
    langGzipTxFormat(line, sizeof(line), &st);
    CHECK(contains(std::string(line), "shortfall=0"));
    CHECK(contains(std::string(line), "incomplete=1/1"));
}

TEST_CASE("the counter accumulates and served == completed + incomplete") {
    LangGzipTxStats st;
    langGzipTxInit(&st);
    langGzipTxRecord(&st, 47889, 47889, true, 50000, 40000, 47000);   // ok
    langGzipTxRecord(&st, 51234, 50000, false, 30000, 20000, 25000);  // cut
    langGzipTxRecord(&st, 47889, 47889, true, 51000, 40000, 47000);   // ok
    CHECK(st.served == 3);
    CHECK(st.completed == 2);
    CHECK(st.incomplete == 1);
    CHECK(st.served == st.completed + st.incomplete);
}

TEST_CASE("only the most recent truncation is remembered") {
    LangGzipTxStats st;
    langGzipTxInit(&st);
    langGzipTxRecord(&st, 100, 90, false, 1000, 900, 800);
    langGzipTxRecord(&st, 200, 150, false, 2000, 1800, 1700);
    CHECK(st.incomplete == 2);
    CHECK(st.lastDeclared == 200);
    CHECK(st.lastSent == 150);
    CHECK(st.lastFreeHeap == 2000);

    // A later clean transfer must not wipe the last failure figures.
    langGzipTxRecord(&st, 300, 300, true, 9000, 8000, 7000);
    CHECK(st.lastDeclared == 200);
    CHECK(st.lastSent == 150);
    CHECK(st.lastFreeHeap == 2000);
}

TEST_CASE("null stats are ignored instead of crashing") {
    CHECK_FALSE(langGzipTxRecord(nullptr, 10, 5, false, 100, 90, 80));
    CHECK(langGzipTxFormat(nullptr, 0, nullptr) == 0);
    LangGzipTxStats st;
    langGzipTxInit(&st);
    char tiny[8];
    langGzipTxRecord(&st, 47889, 40000, false, 1000, 900, 800);
    int n = langGzipTxFormat(tiny, sizeof(tiny), &st);
    CHECK(n > (int)sizeof(tiny) - 1);       // full line did not fit
    CHECK(tiny[sizeof(tiny) - 1] == '\0');  // always NUL-terminated
}

TEST_CASE("reset clears every field") {
    LangGzipTxStats st;
    langGzipTxInit(&st);
    langGzipTxRecord(&st, 47889, 1, false, 1000, 900, 800);
    langGzipTxInit(&st);
    CHECK(st.served == 0);
    CHECK(st.completed == 0);
    CHECK(st.incomplete == 0);
    CHECK(st.lastDeclared == 0);
    CHECK(st.lastSent == 0);
    CHECK(st.lastFreeHeap == 0);
    CHECK(st.lastMinFreeHeap == 0);
    CHECK(st.lastMaxAlloc == 0);
}
