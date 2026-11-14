// access_limits.h — single source of truth for the three numeric Access fields.
//
// The API validator (handleAccessConfigPost, config_handler.cpp) and the page
// controls must accept the SAME range: the UI used to be narrower than the API
// (idle select 5..60 vs API 1..120; abs select 15..120 vs API 5..240), so a
// value the server accepted could not be picked in the browser and a value
// stored by a script did not match any option (BACKLOG ACC-5).
//
// Direction chosen: widen the UI to the API limits. The API ranges are the
// documented contract (PAGES/access.md, hardware scripts and the error payload
// already report them) and narrowing them would reject values that are already
// persisted. The page therefore uses free numeric inputs with these bounds.
//
// The native test test_access_limits.cpp reads the page markup and compares it
// with this table, so the two cannot drift again without failing the suite.
#pragma once

struct AccessIntFieldLimit {
    const char* key;      // POST field name
    int minVal;           // accepted range, inclusive
    int maxVal;
    int defaultValue;     // what GET defaults to before the first save
};

static const AccessIntFieldLimit kAccessIntFields[] = {
    { "web_max_sessions",     1,   5,  3 },
    { "web_idle_timeout_min", 1, 120, 10 },
    { "web_abs_timeout_min",  5, 240, 20 },
};
static const int kAccessIntFieldCount =
    (int)(sizeof(kAccessIntFields) / sizeof(kAccessIntFields[0]));
