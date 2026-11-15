#include "rambox_mobile_page_renderer.h"

#include "../assets/page_rambox_mobile_asset.h"
#include "../assets/mobile_common_asset.h"
#include "../assets/mobile_system_asset.h"
#include "page_render_common.h"
#include "../../feature_flags.h"   // PACK_SYSTEM_NAME

size_t streamRamboxMobilePageToPrint(Print& out) {
#if (ENABLE_UNIKON != 1) && (ENABLE_BRICON != 1)
    (void)out;
    return 0;
#else
    size_t n = 0;
    n += streamMobileShellStart(out, "Rambox32");
    out.print(F("<script>window.PACK_NAME=\""));
    out.print(PACK_SYSTEM_NAME);
    out.print(F("\";</script>"));
    n += 24 + strlen(PACK_SYSTEM_NAME);
    out.print(FPSTR(MOBILE_COMMON_CSS));
    n += strlen_P(MOBILE_COMMON_CSS);
    out.print(FPSTR(PAGE_RAMBOX_MOBILE));
    n += strlen_P(PAGE_RAMBOX_MOBILE);
    out.print(FPSTR(MOBILE_SYSTEM_HTML));
    n += strlen_P(MOBILE_SYSTEM_HTML);
    out.print(FPSTR(PAGE_RAMBOX_MOBILE_TAIL));
    n += strlen_P(PAGE_RAMBOX_MOBILE_TAIL);
    out.print(FPSTR(MOBILE_COMMON_JS));
    n += strlen_P(MOBILE_COMMON_JS);
    out.print(FPSTR(MOBILE_SYSTEM_JS));
    n += strlen_P(MOBILE_SYSTEM_JS);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
#endif
}
