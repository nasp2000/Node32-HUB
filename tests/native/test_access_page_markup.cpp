// Native tests for the Access page session UI (BACKLOG ACC-1).
//
// The page must expose the session list and the "terminate all" action that the
// firmware already serves (GET /api/session-history, POST /api/access-kick-all),
// without turning either button into a form submit (the panel lives inside
// #access-form). The rows themselves are tested in the web suite
// (tests/web/test_access_sessions.js).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <Arduino.h>

#include "webpages/assets/page_access_asset.h"

#include <string>

namespace {

std::string page() { return std::string(PAGE_ACCESS); }

size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0, pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) { n++; pos += needle.size(); }
    return n;
}

// The opening tag that carries `id='<id>'` (back to the previous '<').
std::string tagOf(const std::string& html, const std::string& idAttr) {
    const size_t at = html.find(idAttr);
    if (at == std::string::npos) return "";
    const size_t open = html.rfind('<', at);
    const size_t close = html.find('>', at);
    if (open == std::string::npos || close == std::string::npos) return "";
    return html.substr(open, close - open + 1);
}

} // namespace

TEST_CASE("ACC-1: list, refresh and terminate hooks exist once") {
    const std::string html = page();
    CHECK(countOf(html, "id='acc_sessions_list'") == 1);
    CHECK(countOf(html, "id='acc_sessions_refresh'") == 1);
    CHECK(countOf(html, "id='acc_kick_all'") == 1);
    CHECK(countOf(html, "id='acc_sessions_msg'") == 1);   // feedback line
    // status semantics for screen readers
    CHECK(html.find("id='acc_sessions_msg' class='acc-hint' role='status' aria-live='polite'") != std::string::npos);
}

TEST_CASE("the two buttons cannot submit the configuration form") {
    const std::string html = page();
    const std::string kick = tagOf(html, "id='acc_kick_all'");
    const std::string refresh = tagOf(html, "id='acc_sessions_refresh'");
    CHECK(!kick.empty());
    CHECK(!refresh.empty());
    CHECK(kick.find("type='button'") != std::string::npos);
    CHECK(refresh.find("type='button'") != std::string::npos);
    CHECK(kick.find("type='submit'") == std::string::npos);
    CHECK(refresh.find("type='submit'") == std::string::npos);
}

TEST_CASE("the UI calls the registered endpoints exactly once each") {
    const std::string html = page();
    // one fetch per endpoint (the other mention is the comment above accSessionRows)
    CHECK(countOf(html, "fetch('/api/session-history'") == 1);
    CHECK(countOf(html, "fetch('/api/access-kick-all'") == 1);
    // the list is loaded on page load, and also refreshed after a kick
    CHECK(html.find("accLoadSessions();") != std::string::npos);
    CHECK(countOf(html, "accLoadSessions()") >= 3);   // definition + load + after kick
}

TEST_CASE("the terminate action is confirmed before it fires") {
    const std::string html = page();
    const size_t confirm = html.find("function accKickAll()");
    CHECK(confirm != std::string::npos);
    const size_t confirmCall = html.find("customConfirm(", confirm);
    const size_t post = html.find("/api/access-kick-all", confirm);
    CHECK(confirmCall != std::string::npos);
    CHECK(post != std::string::npos);
    CHECK(confirmCall < post);   // ask first, then POST
    // the confirm text is translated (key + English fallback)
    CHECK(html.find("accessKickAllConfirm") != std::string::npos);
}

TEST_CASE("the list lives in the sessions tab, not in another panel") {
    const std::string html = page();
    const size_t sessionsTab = html.find("data-tab='tabAccessSessions'");
    const size_t list = html.find("id='acc_sessions_list'");
    const size_t nextPanel = html.find("data-tab='tabAccessCharts'");
    CHECK(sessionsTab != std::string::npos);
    CHECK(list != std::string::npos);
    CHECK(nextPanel != std::string::npos);
    CHECK(sessionsTab < list);
    CHECK(list < nextPanel);
}

TEST_CASE("labels are translatable with English fallbacks") {
    const std::string html = page();
    CHECK(html.find("data-i18n='accessActiveSessions'") != std::string::npos);
    CHECK(html.find("data-i18n='accessKickAll'") != std::string::npos);
    CHECK(html.find("data-i18n='accessSessionsRefresh'") != std::string::npos);
    CHECK(html.find("data-i18n='accessSessionsListDesc'") != std::string::npos);
    // runtime texts go through _t()
    CHECK(html.find("_t('accessSessionsEmpty'") != std::string::npos);
    CHECK(html.find("_t('accessSessionsLoadFail'") != std::string::npos);
    CHECK(html.find("accessKickAllDone") != std::string::npos);
    CHECK(html.find("accessKickAllFail") != std::string::npos);
}
