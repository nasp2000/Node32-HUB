#include "sdcard_page_renderer.h"

#include "../assets/page_sdcard_asset.h"
#include "page_render_common.h"
#include "sdcard_page_body.h"   // body in render order (SD-2/SD-3, native-tested)

size_t streamSdcardPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "💾 microSD");
    n += streamSdcardPageBody(out);   // sections + commands card + Save block
    const char* initScript = "<script>if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',function(){initSdcardPage();});}else{initSdcardPage();}</script>";
    n += streamAutoJsFooter(out);
    out.print(initScript);
    n += strlen(initScript);
    n += streamPageShellEnd(out);
    return n;
}