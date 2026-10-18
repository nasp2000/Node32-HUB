#include "../../languages/lang_active.h"
#include "ina226_page_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_ina226_asset.h"
#include "page_render_common.h"

size_t streamIna226PageToPrint(Print& out) {
#if ENABLE_INA226==1
    size_t n = 0;
    n += streamPageShellStart(out, L(activeLang->pageTitleMax31865).c_str());
    out.print(FPSTR(INA226_CONFIG_PAGE));
    n += strlen_P(INA226_CONFIG_PAGE);
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
