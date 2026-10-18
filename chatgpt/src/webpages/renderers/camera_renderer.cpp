#include "camera_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_camera_asset.h"
#include "page_render_common.h"

size_t streamCameraPageToPrint(Print& out) {
#if ENABLE_CAMERA==1
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x93\xB7 C\xC3\xA2mara");
    out.print(FPSTR(CAMERA_PAGE));
    n += strlen_P(CAMERA_PAGE);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
#else
    (void)out;
    return 0;
#endif
}
