#include "sdcard_page_renderer.h"

#include "../assets/page_sdcard_asset.h"
#include "page_render_common.h"

size_t streamSdcardPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "💾 microSD");
    out.print(FPSTR(PAGE_SDCARD));
    n += strlen_P(PAGE_SDCARD);
    out.print(FPSTR(PAGE_SDCARD_PART2));
    n += strlen_P(PAGE_SDCARD_PART2);
    const char* initScript = "<script>if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',function(){initSdcardPage();});}else{initSdcardPage();}</script>";
    n += streamAutoJsFooter(out);
    out.print(initScript);
    n += strlen(initScript);
    n += streamPageShellEnd(out);
    return n;
}