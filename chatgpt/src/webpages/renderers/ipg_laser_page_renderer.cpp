#include "ipg_laser_page_renderer.h"

#include "../../feature_flags.h"
#include "../assets/page_ipg_laser_asset.h"
#include "page_render_common.h"

size_t streamIpgLaserPageToPrint(Print& out) {
#if ENABLE_IPG_LASER == 1
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x94\xA5 IPG Laser RS232");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(PAGE_IPG_LASER));
    n += strlen_P(PAGE_IPG_LASER);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
#else
    (void)out;
    return 0;
#endif
}