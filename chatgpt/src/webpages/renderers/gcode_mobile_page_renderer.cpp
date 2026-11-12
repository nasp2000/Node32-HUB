#include "gcode_mobile_page_renderer.h"

#include "../assets/page_gcode_mobile_asset.h"
#include "page_render_common.h"

size_t streamGcodeMobilePageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "Enviar G-code");
    out.print(FPSTR(PAGE_GCODE_MOBILE));
    n += strlen_P(PAGE_GCODE_MOBILE);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
