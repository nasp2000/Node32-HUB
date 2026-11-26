#include "tdk_lambda_page_renderer.h"

#include "../../feature_flags.h"
#include "../assets/page_tdk_lambda_asset.h"
#include "page_render_common.h"

size_t streamTdkLambdaPageToPrint(Print& out) {
#if ENABLE_TDK_LAMBDA == 1
    size_t n = 0;
    n += streamPageShellStart(out, "\xE2\x9A\xA1 TDK-Lambda / Genesys");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(PAGE_TDK_LAMBDA));
    n += strlen_P(PAGE_TDK_LAMBDA);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
#else
    (void)out;
    return 0;
#endif
}
