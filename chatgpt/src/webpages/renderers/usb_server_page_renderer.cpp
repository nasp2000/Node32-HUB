#include "usb_server_page_renderer.h"

#include "page_render_common.h"
#include "../assets/page_usb_server_asset.h"

size_t streamUsbServerPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "🧩 USB Server");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(PAGE_USB_SERVER));
    n += strlen_P(PAGE_USB_SERVER);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
