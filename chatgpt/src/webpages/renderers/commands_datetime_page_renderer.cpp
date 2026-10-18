#include "commands_datetime_page_renderer.h"

#include "../../feature_flags.h"
#include "../assets/page_commands_datetime_asset.h"
#include "page_render_common.h"

size_t streamCommandsByDatetimePageToPrint(Print& out) {
    static const char PAGE_COMMANDS_DATETIME_SCRIPT_CLOSE[] = "</script>";
    static const char PAGE_COMMANDS_DATETIME_PAGE_CLOSE[] = "</div></main>";
    out.print(FPSTR(PAGE_COMMANDS_DATETIME_HTML));
#if ENABLE_RS232==1
    out.print(F("var _rs232Enabled=true;"));
#else
    out.print(F("var _rs232Enabled=false;"));
#endif
#if ENABLE_MODBUS==1
    out.print(F("var _modbusEnabled=true;"));
#else
    out.print(F("var _modbusEnabled=false;"));
#endif
    out.print(FPSTR(PAGE_COMMANDS_DATETIME_JS));
    out.print(PAGE_COMMANDS_DATETIME_SCRIPT_CLOSE);
    out.print(PAGE_COMMANDS_DATETIME_PAGE_CLOSE);
    size_t n = strlen_P(PAGE_COMMANDS_DATETIME_HTML)
        + strlen_P(PAGE_COMMANDS_DATETIME_JS)
        + strlen(PAGE_COMMANDS_DATETIME_SCRIPT_CLOSE)
        + strlen(PAGE_COMMANDS_DATETIME_PAGE_CLOSE);
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
