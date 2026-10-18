#include "radio_ui_page_renderer.h"
#include "../assets/page_radio_ui_asset.h"
#include "page_render_common.h"

size_t streamRadioUiPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x8E\xB5 Harmonica Box");

    // Radio is a full-screen player with its own 5-button bottom navigation —
    // keep the shared mobile Home/Back bar (auto.js .bottom-nav) off this page.
    static const char kNoBottomNavScript[] PROGMEM =
        "<script>window._noBottomNav=true;window._localPagesNeverExpire=true;</script>";
    out.print(FPSTR(kNoBottomNavScript));
    n += strlen_P(kNoBottomNavScript);

    out.print(FPSTR(PAGE_RADIO_UI));
    n += strlen_P(PAGE_RADIO_UI);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
