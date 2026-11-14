#include "cam_mobile_page_renderer.h"

#include "../assets/page_cam_mobile_asset.h"
#include "../assets/mobile_common_asset.h"
#include "../assets/mobile_system_asset.h"
#include "page_render_common.h"
#include "../../feature_flags.h"   // PACK_SYSTEM_NAME

size_t streamCamMobilePageToPrint(Print& out) {
#if ENABLE_CAMERA!=1
    (void)out;
    return 0;
#else
    size_t n = 0;
    n += streamPageShellStart(out, "Camera");
    out.print(F("<script>window.PACK_NAME=\""));
    out.print(PACK_SYSTEM_NAME);
    out.print(F("\";</script>"));
    n += 24 + strlen(PACK_SYSTEM_NAME);
    out.print(FPSTR(MOBILE_COMMON_CSS));
    n += strlen_P(MOBILE_COMMON_CSS);
    out.print(FPSTR(PAGE_CAM_MOBILE));
    n += strlen_P(PAGE_CAM_MOBILE);
    out.print(FPSTR(MOBILE_SYSTEM_HTML));
    n += strlen_P(MOBILE_SYSTEM_HTML);
    out.print(FPSTR(PAGE_CAM_MOBILE_TAIL));
    n += strlen_P(PAGE_CAM_MOBILE_TAIL);
    out.print(FPSTR(MOBILE_COMMON_JS));
    n += strlen_P(MOBILE_COMMON_JS);
    out.print(FPSTR(MOBILE_SYSTEM_JS));
    n += strlen_P(MOBILE_SYSTEM_JS);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
#endif  // ENABLE_CAMERA
}
