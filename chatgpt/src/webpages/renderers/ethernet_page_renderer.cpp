#include "ethernet_page_renderer.h"

#include "page_render_common.h"
#include "../assets/page_ethernet_asset.h"

size_t streamEthernetPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "🌐 Ethernet");
    out.print(FPSTR(PAGE_ETHERNET));
    n += strlen_P(PAGE_ETHERNET);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}