#include "recipients_page_renderer.h"

#include "../../ConfigManager.h"
#include "page_render_common.h"
#include "../assets/page_recipients_asset.h"

extern ConfigManager configManager;

namespace {
size_t printCheckedAttr(Print& out, bool enabled) {
    if (!enabled) {
        return 0;
    }

    const char checked[] = "checked ";
    out.print(checked);
    return sizeof(checked) - 1;
}

}

size_t streamRecipientsPageToPrint(Print& out) {
    size_t n = 0;

    out.print(FPSTR(PAGE_RECIPIENTS_HEAD));
    n += strlen_P(PAGE_RECIPIENTS_HEAD);
    n += printCheckedAttr(out, configManager.getEmailDontCopySender());

    out.print(FPSTR(PAGE_RECIPIENTS_AFTER_DONT_COPY));
    n += strlen_P(PAGE_RECIPIENTS_AFTER_DONT_COPY);

    // Inject day-commands section with per-command email values
    out.print(FPSTR(PAGE_RECIPIENTS_DAY_CMDS_A));
    n += strlen_P(PAGE_RECIPIENTS_DAY_CMDS_A);
    { String v = configManager.getEmaildayExtraEmails(); out.print(v); n += v.length(); }
    out.print(FPSTR(PAGE_RECIPIENTS_DAY_CMDS_B));
    n += strlen_P(PAGE_RECIPIENTS_DAY_CMDS_B);
    { String v = configManager.getLogdayExtraEmails(); out.print(v); n += v.length(); }
    out.print(FPSTR(PAGE_RECIPIENTS_DAY_CMDS_C));
    n += strlen_P(PAGE_RECIPIENTS_DAY_CMDS_C);
    { String v = configManager.getSensordayExtraEmails(); out.print(v); n += v.length(); }
    out.print(FPSTR(PAGE_RECIPIENTS_DAY_CMDS_D));
    n += strlen_P(PAGE_RECIPIENTS_DAY_CMDS_D);

    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsTag(out, false);

    out.print(F("<script>"));
    n += 8;

    out.print(FPSTR(PAGE_RECIPIENTS_JS));
    n += strlen_P(PAGE_RECIPIENTS_JS);

    out.print(FPSTR(PAGE_RECIPIENTS_FOOTER));
    n += strlen_P(PAGE_RECIPIENTS_FOOTER);
    return n;
}
