#include "proximity_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_proximity_asset.h"
#include "page_render_common.h"

static const char PROXIMITY_TABS_JS[] PROGMEM = R"JSTABS(
<style>
.prox-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.prox-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.prox-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.prox-tab-panel{display:none}
.prox-tab-panel.active{display:block}
@media(max-width:480px){.prox-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.prox-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
</style>
<script>
(function(){
    function activate(buttons, sections, idx){
        var i;
        for (i = 0; i < buttons.length; i++) {
            if (i === idx) buttons[i].classList.add('active');
            else buttons[i].classList.remove('active');
        }
        for (i = 0; i < sections.length; i++) {
            if (i === idx) sections[i].classList.add('active');
            else sections[i].classList.remove('active');
        }
    }
    function setup(){
        var wrap = document.querySelector('.wrap');
        if (!wrap) return;
        var tabs = [];
        var sLive = document.querySelector('[data-prox-tab="status"]');
        var sCfg  = document.querySelector('[data-prox-tab="config"]');
        var sSh   = document.querySelector('[data-prox-tab="shift"]');
        var sCmd  = document.querySelector('[data-prox-tab="shiftday"]');
        var sOee  = document.querySelector('[data-prox-tab="oee"]');
        if (sLive) tabs.push({label:'Status', i18n:'proxTabStatus', section:sLive});
        if (sCfg)  tabs.push({label:'Config', i18n:'proxTabConfig', section:sCfg});
        if (sSh)   tabs.push({label:'Shifts', i18n:'proxTabShifts', section:sSh});
        if (sCmd)  tabs.push({label:'Shiftday', i18n:'proxTabShiftday', section:sCmd});
        if (sOee)  tabs.push({label:'OEE', i18n:'proxTabOEE', section:sOee});
        if (tabs.length < 2) return;

        var bar = document.createElement('div');
        bar.className = 'prox-tab-bar';
        var buttons = [];
        var sections = [];
        var i;
        for (i = 0; i < tabs.length; i++) {
            tabs[i].section.classList.add('prox-tab-panel');
            sections.push(tabs[i].section);
            var btn = document.createElement('button');
            btn.type = 'button';
            btn.className = 'prox-tab-btn';
            btn.textContent = tabs[i].label;
            if (tabs[i].i18n) btn.setAttribute('data-i18n', tabs[i].i18n);
            (function(index){
                btn.onclick = function(){ activate(buttons, sections, index); };
            })(i);
            buttons.push(btn);
            bar.appendChild(btn);
        }
        wrap.insertBefore(bar, tabs[0].section);
        activate(buttons, sections, 0);
    }
    if (document.readyState === 'complete' || document.readyState === 'interactive') setup();
    else document.addEventListener('DOMContentLoaded', setup);
})();
</script>
)JSTABS";

size_t streamProximityPageToPrint(Print& out) {
#if ENABLE_PROXIMITY==1
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x93\x8D Production Management and OEE");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(PROXIMITY_TABS_JS));
    n += strlen_P(PROXIMITY_TABS_JS);
    out.print(FPSTR(PROXIMITY_PAGE));
    n += strlen_P(PROXIMITY_PAGE);
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
