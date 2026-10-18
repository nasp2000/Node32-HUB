#include "max31865_page_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_max31865_asset.h"
#include "page_render_common.h"

size_t streamMax31865PageToPrint(Print& out) {
#if ENABLE_MAX31865==1
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x8C\xA1\xEF\xB8\x8F Sensor RTD MAX31865");
    out.print(FPSTR(MAX31865_CONFIG_PAGE));
    n += strlen_P(MAX31865_CONFIG_PAGE);
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
