#include "email_page_renderer.h"

#include "../assets/page_email_asset.h"
#include "page_render_common.h"

size_t streamEmailPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "&#9993;&#65039; E-mail");
    out.print(FPSTR(PAGE_EMAIL_BODY));
    n += strlen_P(PAGE_EMAIL_BODY);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}