#include "../../languages/lang_active.h"
#include "hx711_page_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_hx711_asset.h"
#include "page_render_common.h"

size_t streamHx711PageToPrint(Print& out) {
#if ENABLE_HX711==1
    size_t n = 0;
    n += streamPageShellStart(out, L(activeLang->pageTitleHx711).c_str());
    out.print(FPSTR(HX711_CONFIG_PAGE));
    n += strlen_P(HX711_CONFIG_PAGE);
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
