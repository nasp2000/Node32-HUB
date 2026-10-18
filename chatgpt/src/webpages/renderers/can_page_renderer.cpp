// can_page_renderer.cpp
// Streams the CAN bus page using the standard shell wrapper.

#include "../../languages/lang_active.h"
#include "can_page_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_can_asset.h"
#include "page_render_common.h"

static const char CAN_TABS_SCRIPT[] PROGMEM = R"JSTABS(<style>
.can-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.can-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.can-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.can-tab-panel{display:none}
.can-tab-panel.active{display:block}
@media(max-width:480px){.can-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr))}.can-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
</style><script>(function(){function on(btns,secs,idx){var i;for(i=0;i<btns.length;i++){if(i===idx)btns[i].classList.add('active');else btns[i].classList.remove('active');if(i===idx)secs[i].classList.add('active');else secs[i].classList.remove('active');}}function setup(){var wrap=document.querySelector('.wrap');if(!wrap)return;var labels=[L(activeLang->tabLabelState),L(activeLang->tabLabelMonitor),L(activeLang->tabLabelSend),L(activeLang->tabLabelConfig)];var nodes=wrap.querySelectorAll("div[style*='border:2px solid #3b82f6']");var tabs=[];var i;for(i=0;i<nodes.length;i++){if(nodes[i].querySelector('h2'))tabs.push(nodes[i]);}if(tabs.length<2)return;var count=tabs.length<labels.length?tabs.length:labels.length;var bar=document.createElement('div');bar.className='can-tab-bar';var btns=[];var secs=[];for(i=0;i<count;i++){tabs[i].classList.add('can-tab-panel');secs.push(tabs[i]);var b=document.createElement('button');b.type='button';b.className='can-tab-btn';b.textContent=labels[i];(function(ix){b.onclick=function(){on(btns,secs,ix);};})(i);btns.push(b);bar.appendChild(b);}wrap.insertBefore(bar,tabs[0]);on(btns,secs,0);}setup();})();</script>)JSTABS";

size_t streamCanPageToPrint(Print& out) {
#if ENABLE_CAN==1
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x94\x8C CAN Bus (TWAI)");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(CAN_TABS_SCRIPT));
    n += strlen_P(CAN_TABS_SCRIPT);
    out.print(FPSTR(PAGE_CAN));
    n += strlen_P(PAGE_CAN);
    n += streamAutoJsFooter(out);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamPageShellEnd(out);
    return n;
#else
    (void)out;
    return 0;
#endif
}
