#pragma once

#include <pgmspace.h>

// Shared building blocks for the phone pages (any pack).
// CSS is scoped to `.mp` (mobile panel) so the host page keeps its own root.
extern const char MOBILE_COMMON_CSS[];
// window.MU — helpers (t/tf/get/post/fmt/msg/arm) used by every mobile page.
extern const char MOBILE_COMMON_JS[];
