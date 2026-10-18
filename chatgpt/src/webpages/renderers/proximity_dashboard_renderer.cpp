#include "proximity_dashboard_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_proximity_dashboard_asset.h"
#include "page_render_common.h"

#if ENABLE_PROXIMITY==1
size_t streamProximityDashboardPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x93\x8A Charts and OEE");
    // Dashboard renders live data locally in the browser — ALWAYS exempt from
    // the inactivity watchdog (charts_exempt only governs static pages).
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(PROXIMITY_DASHBOARD_PAGE));
    n += strlen_P(PROXIMITY_DASHBOARD_PAGE);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
#endif
