// lpt_db25_page_renderer.cpp
// Streams the LPT / DB25 printer configuration page using the standard shell wrapper.

#include "lpt_db25_page_renderer.h"
#include "../assets/page_lpt_db25_asset.h"
#include "page_render_common.h"

size_t streamLptDb25PageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x96\xA8\xEF\xB8\x8F LPT DB25");
    out.print(FPSTR(PAGE_LPT_DB25));
    n += strlen_P(PAGE_LPT_DB25);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
