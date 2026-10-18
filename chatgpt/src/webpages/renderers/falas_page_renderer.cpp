#include "../../languages/lang_active.h"
#include "falas_page_renderer.h"
#include "../../feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "../assets/page_falas_asset.h"
#include "page_render_common.h"
#endif

size_t streamFalasPageToPrint(Print& out) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    size_t n = 0;
    n += streamPageShellStart(out, L(activeLang->pageTitleFalas).c_str());
    out.print(FPSTR(FALAS_PAGE));
    n += strlen_P(FALAS_PAGE);
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
