// email_recipients.h
// One envelope, one address, once — recipient list normalization.
//
// The SMTP envelope is assembled by concatenating the primary destination with
// the extra/command-specific list ("dest + ", " + extra"), so the same mailbox
// can end up twice and with a stray space after the comma:
//     "nasp2000@gmail.com, nasp2000@gmail.com,joana2escura@gmail.com"
// The persisted pipeline event then showed a `to=` that looked broken and the
// transport issued a second RCPT TO for the same address.
//
// Normalize ONCE, before the envelope is built and before the event is logged:
// split on ',' or ';', trim, strip <>, drop empties and non-addresses, dedupe
// case-insensitively (first spelling wins), rejoin with ','.
//
// Header-only and String-only so it is host-testable:
// tests/native/test_email_recipients.cpp.
#pragma once

#include <Arduino.h>

// Copy of `s` split into addresses. Returns the normalized list ("" for none).
static inline String emailNormalizeRecipientList(const String &raw) {
    String out;
    out.reserve(raw.length() + 1);

    const int n = (int)raw.length();
    int start = 0;
    while (start <= n) {
        int sep = -1;
        const int sepComma = raw.indexOf(',', (unsigned int)start);
        const int sepSemi = raw.indexOf(';', (unsigned int)start);
        if (sepComma < 0) sep = sepSemi;
        else if (sepSemi < 0) sep = sepComma;
        else sep = (sepComma < sepSemi) ? sepComma : sepSemi;

        String one = (sep < 0) ? raw.substring(start) : raw.substring(start, sep);
        one.trim();
        // Bare <addr@host> is valid in a header but never in RCPT TO.
        if (one.length() > 1 && one.charAt(0) == '<' && one.charAt((int)one.length() - 1) == '>') {
            one = one.substring(1, (int)one.length() - 1);
            one.trim();
        }

        if (one.length() > 0 && one.indexOf('@') > 0) {
            String low = one;
            low.toLowerCase();

            bool dup = false;
            const int outLen = (int)out.length();
            int p = 0;
            while (p < outLen) {
                int c = out.indexOf(',', (unsigned int)p);
                String have = (c < 0) ? out.substring(p) : out.substring(p, c);
                String haveLow = have;
                haveLow.toLowerCase();
                if (haveLow == low) { dup = true; break; }
                if (c < 0) break;
                p = c + 1;
            }

            if (!dup) {
                if (out.length() > 0) out += ',';
                out += one;
            }
        }

        if (sep < 0) break;
        start = sep + 1;
    }

    return out;
}
