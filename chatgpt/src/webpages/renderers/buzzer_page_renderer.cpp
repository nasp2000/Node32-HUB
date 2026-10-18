// buzzer_page_renderer.cpp
// Streams the buzzer config page using the standard shell wrapper.

#include "buzzer_page_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_buzzer_asset.h"
#include "page_render_common.h"

size_t streamBuzzerPageToPrint(Print& out) {
#if ENABLE_BUZZER == 1
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x94\x94 Buzzer");
    out.print(FPSTR(PAGE_BUZZER));
    n += strlen_P(PAGE_BUZZER);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
#else
    (void)out;
    return 0;
#endif
}
