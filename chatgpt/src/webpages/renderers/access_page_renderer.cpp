#include "../../languages/lang_active.h"
#include "access_page_renderer.h"

static const char ACCESS_TABS_CSS[] PROGMEM = R"CSS(
<style>
.acc-tab-bar{display:flex;flex-wrap:nowrap;gap:6px;margin:16px 0 0 0}
.acc-tab-btn{flex:1 1 0;min-width:0;border:2px solid #1e40af;border-radius:8px;background:#0f172a;color:#93c5fd;padding:8px 6px;font-size:12px;cursor:pointer;transition:all .2s ease;text-align:center;white-space:nowrap}
.acc-tab-btn.active{background:#1d4ed8;color:#fff;border-color:#3b82f6;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.acc-panel{display:none}
.acc-panel.active{display:block}
.acc-panel-inner{border:2px solid #1e40af;border-radius:8px;padding:14px;background:#0f172a;margin-top:10px}
.acc-panel-inner h2{margin:0 0 6px 0;font-size:15px;color:#60a5fa}
.acc-desc{font-size:12px;color:#94a3b8;margin-bottom:12px}
.acc-hint{font-size:11px;color:#94a3b8;display:block;margin-bottom:4px}
.acc-subtle{font-size:11px;color:#94a3b8}
.acc-field{display:block;margin-bottom:10px}
.acc-field span{display:block;font-size:13px;margin-bottom:4px}
.acc-field input,.acc-field select{width:100%;padding:8px 10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px;box-sizing:border-box}
.acc-check{display:flex;align-items:center;gap:8px;font-size:13px;margin-bottom:6px}
.acc-err{font-size:12px;color:#ef4444;margin-top:4px}
.acc-user-section{margin-bottom:16px;padding:12px;background:rgba(0,0,0,.2);border-radius:6px}
.acc-user-section h3{margin:0 0 6px 0;font-size:14px;color:#60a5fa}
.acc-routes-hint{margin-top:8px;padding:8px 10px;background:#0c2340;border:1px solid #1e40af;border-radius:6px;font-size:11px;color:#93c5fd}
@media(max-width:480px){.acc-tab-bar{gap:5px}.acc-tab-btn{min-width:60px;padding:8px 6px;font-size:11px}}
</style>)CSS";

size_t streamAccessPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, L(activeLang->pageTitleAccess).c_str());
    out.print(FPSTR(ACCESS_TABS_CSS));
    n += strlen_P(ACCESS_TABS_CSS);
    out.print(FPSTR(PAGE_ACCESS));
    n += strlen_P(PAGE_ACCESS);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}
