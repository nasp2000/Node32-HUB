#include "wifi_page_renderer.h"

#include "page_render_common.h"
#include "../assets/page_wifi_asset.h"

size_t streamWifiPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "📶 Wi-Fi");
    out.print(FPSTR(PAGE_WIFI));
    n += strlen_P(PAGE_WIFI);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}