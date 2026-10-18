// unikon_page_renderer.cpp
// Streams the Unikon RAMBOX monitoring page using the standard shell wrapper.

#include "unikon_page_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_unikon_asset.h"
#include "page_render_common.h"

static const char UNIKON_TABS_JS[] PROGMEM = R"JSTABS(
<style>
.uk-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.uk-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.uk-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.uk-tab-panel{display:none}
.uk-tab-panel.active{display:block}
@media(max-width:480px){.uk-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr))}.uk-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
</style>
<script>
(function(){
    function t(k, fb){
        try {
            if (typeof window._t === 'function') return window._t(k, fb);
            var raw = localStorage.getItem('ui_lang_cache');
            if (raw) {
                var parsed = JSON.parse(raw);
                if (parsed && parsed[k] !== undefined) return parsed[k];
            }
        } catch (e) {}
        return fb !== undefined ? fb : k;
    }
    function byId(id){
        return document.getElementById(id);
    }
    function activate(buttons, groups, idx){
        var i;
        for (i = 0; i < buttons.length; i++) {
            if (i === idx) buttons[i].classList.add('active');
            else buttons[i].classList.remove('active');
        }
        for (i = 0; i < groups.length; i++) {
            var visible = (i === idx);
            var j;
            for (j = 0; j < groups[i].nodes.length; j++) {
                groups[i].nodes[j].style.display = visible ? '' : 'none';
            }
        }
    }
    function setup(){
        var wrap = document.querySelector('.wrap');
        if (!wrap) return false;
        if (wrap.querySelector('.uk-tab-bar')) return true;
        var sStatus = byId('uk-sec-status');
        var sGuide = byId('uk-sec-guide');
        var sArrivals = byId('uk-sec-arrivals');
        var sConfig = byId('uk-sec-config');
        var sContests = byId('uk-sec-contests');
        var sPombal = byId('uk-sec-pombal');
        var sTraining = byId('uk-sec-training');
        if (!sStatus || !sGuide || !sArrivals || !sConfig || !sContests || !sPombal || !sTraining) return false;
        var groups = [
            { label:t('tabUnikonStatus', 'Status'), nodes:[sStatus] },
            { label:t('tabUnikonArrivals', 'Chegadas'), nodes:[sArrivals] },
            { label:t('tabUnikonContests', 'Concursos'), nodes:[sContests] },
            { label:t('tabUnikonPombal', 'Pombal'), nodes:[sPombal, sTraining] },
            { label:t('tabUnikonConfig', 'Configuration'), nodes:[sGuide, sConfig] }
        ];

        var bar = document.createElement('div');
        bar.className = 'uk-tab-bar';
        var buttons = [];
        var i;
        for (i = 0; i < groups.length; i++) {
            var btn = document.createElement('button');
            btn.type = 'button';
            btn.className = 'uk-tab-btn';
            btn.textContent = groups[i].label;
            (function(index){
                btn.onclick = function(){ activate(buttons, groups, index); };
            })(i);
            buttons.push(btn);
            bar.appendChild(btn);
        }
        wrap.insertBefore(bar, sStatus);
        activate(buttons, groups, 0);
        return true;
    }
    function boot(){
        var tries = 0;
        function tick(){
            if (setup()) return;
            tries++;
            if (tries < 60) setTimeout(tick, 16);
        }
        tick();
    }
    boot();
})();
</script>
)JSTABS";

size_t streamUnikonPageToPrint(Print& out) {
#if ENABLE_UNIKON == 1
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x90\xA6 Unikon RAMBOX");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(UNIKON_TABS_JS));
    n += strlen_P(UNIKON_TABS_JS);
    out.print(FPSTR(PAGE_UNIKON));
    n += strlen_P(PAGE_UNIKON);
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
