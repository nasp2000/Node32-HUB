#include "gcode_mobile_page_renderer.h"

#include "../assets/page_gcode_mobile_asset.h"
#include "../assets/mobile_common_asset.h"
#include "../assets/mobile_system_asset.h"
#include "page_render_common.h"
#include "../../feature_flags.h"   // PACK_SYSTEM_NAME (compile-time, per pack)

size_t streamGcodeMobilePageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "Send G-code");
    // The pack name is a compile-time macro; hand it to the page script so the
    // header can show "GcodeSender-P4" / "-S3" without duplicating the build.
    out.print(F("<script>window.PACK_NAME=\""));
    out.print(PACK_SYSTEM_NAME);
    out.print(F("\";</script>"));
    n += 24 + strlen(PACK_SYSTEM_NAME);
    // Shared phone building blocks, then the pack's own screens, the shared
    // System screen and the pack's footer + script.
    out.print(FPSTR(MOBILE_COMMON_CSS));
    n += strlen_P(MOBILE_COMMON_CSS);
    out.print(FPSTR(PAGE_GCODE_MOBILE));
    n += strlen_P(PAGE_GCODE_MOBILE);
    out.print(FPSTR(MOBILE_SYSTEM_HTML));
    n += strlen_P(MOBILE_SYSTEM_HTML);
    out.print(FPSTR(PAGE_GCODE_MOBILE_TAIL));
    n += strlen_P(PAGE_GCODE_MOBILE_TAIL);
    out.print(FPSTR(MOBILE_COMMON_JS));
    n += strlen_P(MOBILE_COMMON_JS);
    out.print(FPSTR(MOBILE_SYSTEM_JS));
    n += strlen_P(MOBILE_SYSTEM_JS);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
