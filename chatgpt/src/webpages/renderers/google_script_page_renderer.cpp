#include "google_script_page_renderer.h"

#include "page_render_common.h"
#include "../assets/page_google_script_asset.h"

size_t streamGoogleScriptPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "&#127760; Google App Script");
    out.print(FPSTR(PAGE_GOOGLE_SCRIPT));
    n += strlen_P(PAGE_GOOGLE_SCRIPT);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
