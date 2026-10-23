// email_event_fields.h
// Pure formatting of the EMAIL_PIPELINE_OK / EMAIL_PIPELINE_FAIL `details=`
// payload. Kept out of observability.h so it can be host-tested without the
// Arduino/ESP dependencies (tests/native/test_email_event_fields.cpp).
//
// Legacy layout (byte-identical when there is no failure reason, so existing
// log parsers / field dashboards keep working):
//     stage=%s channel=%s files=%d subject=%.72s to=%.72s
// With a transport reason (only on failure, appended):
//     stage=%s channel=%s files=%d subject=%.56s to=%.56s err=%.40s
// The failure variant shortens subject/to so the whole line still fits the
// 224-byte details buffer.
#pragma once

#include <stdio.h>
#include <stddef.h>

// Returns the snprintf() result (may exceed outSize: the caller ignores it).
static inline int emailEventFormatPipelineDetails(char *out, size_t outSize,
                                                  const char *stage,
                                                  const char *channel,
                                                  int attachmentCount,
                                                  const char *subject,
                                                  const char *recipients,
                                                  const char *error) {
    if (!out || outSize == 0) return 0;
    const char *subj = subject ? subject : "";
    const char *to = recipients ? recipients : "";
    const char *ch = channel ? channel : "-";
    const char *st = stage ? stage : "-";

    if (error && error[0]) {
        return snprintf(out, outSize,
                        "stage=%s channel=%s files=%d subject=%.56s to=%.56s err=%.40s",
                        st, ch, attachmentCount, subj, to, error);
    }
    return snprintf(out, outSize,
                    "stage=%s channel=%s files=%d subject=%.72s to=%.72s",
                    st, ch, attachmentCount, subj, to);
}
