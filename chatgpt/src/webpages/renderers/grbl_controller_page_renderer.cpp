#include "grbl_controller_page_renderer.h"
#include "../../feature_flags.h"

#if ENABLE_GRBL == 1

#include "../assets/page_grbl_controller_asset.h"
#include "page_render_common.h"

size_t streamGrblControllerPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "🛠️ GcodeSender");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog
    // (the Access-page charts_exempt option only governs static pages).
    n += streamNeverExpireFlag(out);
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC) && ENABLE_USB_HOST == 1
    // S3 CNC (boot OTG OFF since 2026-10-11; page auto-starts the host): the
    // ON/OFF toggle must stay VISIBLE (it is the manual override); hide only
    // the P4-only 4-port hub sections (slot dropdown, chip info, USB baud,
    // Reconnect USB). The USB OTG metric still shows the live state.
    // CSS hides them before/without JS so they never flash even if a status
    // poll races.
    static const char kTransportSelectorTrue[] PROGMEM =
        "<script>window.GRBL_TRANSPORT_SELECTOR=true;</script>"
        "<style>#grbl_usb_slot_section,#grbl_usb_baud_section,#btn_usb_restart{display:none!important}</style>";
    out.print(FPSTR(kTransportSelectorTrue));
    n += strlen_P(kTransportSelectorTrue);
#else
    static const char kTransportSelectorFalse[] PROGMEM =
        "<script>window.GRBL_TRANSPORT_SELECTOR=false;</script>";
    out.print(FPSTR(kTransportSelectorFalse));
    n += strlen_P(kTransportSelectorFalse);
#endif
    out.print(FPSTR(PAGE_GRBL_CONTROLLER));
    n += strlen_P(PAGE_GRBL_CONTROLLER);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}

#endif // ENABLE_GRBL
