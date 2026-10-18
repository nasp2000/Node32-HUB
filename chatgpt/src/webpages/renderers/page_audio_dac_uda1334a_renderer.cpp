#include "../../languages/lang_active.h"
#include "page_audio_dac_uda1334a_renderer.h"

#include "../../feature_flags.h"

#if ENABLE_UDA1334A == 1

#include "../assets/page_audio_dac_uda1334a_asset.h"
#include "page_render_common.h"

static const char UDA_TABS_SCRIPT[] PROGMEM = R"JSTABS(<style>
.uda-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.uda-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.uda-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.uda-tab-panel{display:none}
.uda-tab-panel.active{display:block}
@media(max-width:480px){.uda-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr))}.uda-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
</style><script>(function(){function on(btns,secs,idx){var i;for(i=0;i<btns.length;i++){if(i===idx)btns[i].classList.add('active');else btns[i].classList.remove('active');if(i===idx)secs[i].classList.add('active');else secs[i].classList.remove('active');}}function setup(){var wrap=document.querySelector('.wrap');if(!wrap)return;var labels=['Estado','Config','Info'];var nodes=wrap.querySelectorAll("div[style*='border:2px solid #3b82f6']");var tabs=[];var i;for(i=0;i<nodes.length;i++){if(nodes[i].querySelector('h2'))tabs.push(nodes[i]);}if(tabs.length<2)return;var count=tabs.length<labels.length?tabs.length:labels.length;var bar=document.createElement('div');bar.className='uda-tab-bar';var btns=[];var secs=[];for(i=0;i<count;i++){tabs[i].classList.add('uda-tab-panel');secs.push(tabs[i]);var b=document.createElement('button');b.type='button';b.className='uda-tab-btn';b.textContent=labels[i];(function(ix){b.onclick=function(){on(btns,secs,ix);};})(i);btns.push(b);bar.appendChild(b);}wrap.insertBefore(bar,tabs[0]);on(btns,secs,0);}setup();})();</script>)JSTABS";

size_t streamUDA1334APageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, L(activeLang->pageTitleUda1334a).c_str());
    out.print(FPSTR(UDA_TABS_SCRIPT));
    n += strlen_P(UDA_TABS_SCRIPT);
    out.print(FPSTR(PAGE_AUDIO_DAC_UDA1334A));
    n += strlen_P(PAGE_AUDIO_DAC_UDA1334A);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
}

#else

size_t streamUDA1334APageToPrint(Print& out) {
    (void)out;
    return 0;
}

#endif // ENABLE_UDA1334A == 1

