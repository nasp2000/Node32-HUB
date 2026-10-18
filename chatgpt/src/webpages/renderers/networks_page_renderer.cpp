#include "../../languages/lang_active.h"
#include "networks_page_renderer.h"

size_t streamNetworksPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, L(activeLang->pageTitleNetworksOther).c_str());
    out.print(FPSTR(PAGE_NETWORKS));
    n += strlen_P(PAGE_NETWORKS);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
