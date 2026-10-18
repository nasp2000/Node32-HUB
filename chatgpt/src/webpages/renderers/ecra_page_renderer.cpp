#include "../../languages/lang_active.h"
#include "ecra_page_renderer.h"
#include "../../feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "../assets/page_ecra_asset.h"
#include "page_render_common.h"
#endif

size_t streamEcraPageToPrint(Print& out) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    size_t n = 0;
    n += streamPageShellStart(out, L(activeLang->pageTitleEcra).c_str());
    // Local-processing page (screen mirror): ALWAYS exempt from the watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(ECRA_PAGE));
    n += strlen_P(ECRA_PAGE);
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
