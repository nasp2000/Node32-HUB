// password_field_input.h — how POSTed password fields are interpreted.
//
// The Access page renders "keep the current password" as an EMPTY input whose
// placeholder is the localised "(unchanged)" hint. A placeholder is never
// submitted, so the request carries an empty value: empty = keep, any
// non-empty value = new password.
//
// Older code additionally compared web_password_new / viewer_password against
// the Portuguese strings "(não alterado)" / "(nao alterado)". The client is
// English (placeholder "(unchanged)") and never sends those strings, so the
// comparison was dead and implied a wire protocol that does not exist
// (BACKLOG ACC-3). It is gone: both password fields now follow this one rule.
//
// Pure (stddef only) so the contract is native-tested:
// tests/native/test_password_field_input.cpp.
#pragma once

#include <stddef.h>

// True when the posted value is a new password to apply. False (keep the
// stored password) for an absent/null or empty value. A value that merely
// looks like the old sentinel is ordinary text now — the keep signal is the
// empty string, not a magic word.
static inline bool passwordFieldIsNew(const char* value, size_t len) {
    return value != nullptr && len > 0;
}
