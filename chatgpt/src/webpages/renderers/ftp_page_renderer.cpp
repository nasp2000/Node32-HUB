#include "ftp_page_renderer.h"

#include "page_render_common.h"
#include "../assets/page_ftp_asset.h"

size_t streamFtpPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "&#128229; FTP Upload");
    out.print(FPSTR(PAGE_FTP));
    n += strlen_P(PAGE_FTP);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
