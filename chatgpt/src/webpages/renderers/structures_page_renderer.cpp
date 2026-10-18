#include "../../feature_flags.h"
#include "structures_page_renderer.h"

#if ENABLE_STRUCTURES_PAGE==1

#include "page_render_common.h"
#include "../assets/page_structures_asset.h"

static const char STRUCTURES_TABS_JS[] PROGMEM = R"JSTABS(
<style>
.aw-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.aw-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.aw-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
@media(max-width:480px){.aw-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr))}.aw-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
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
    function byId(id){ return document.getElementById(id); }
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
        if (!wrap) return;
        var secNav = byId('sec-nav');
        var secOverview = byId('sec-overview');
        var secConfig = byId('sec-config');
        var secDiagnostic = byId('sec-diagnostic');
        var secErrors = byId('sec-errors');
        if (!secOverview || !secConfig || !secDiagnostic || !secErrors) return;

        var groups = [
            { label:t('tabStructOverview', 'Overview'), nodes:[secNav || secOverview, secOverview] },
            { label:t('tabStructConfig', 'Configuration'), nodes:[secConfig] },
            { label:t('tabStructDiagnostic', 'Diagnostics'), nodes:[secDiagnostic] },
            { label:t('tabStructErrors', 'Erros'), nodes:[secErrors] }
        ];

        var bar = document.createElement('div');
        bar.className = 'aw-tab-bar';
        var buttons = [];
        var i;
        for (i = 0; i < groups.length; i++) {
            var btn = document.createElement('button');
            btn.type = 'button';
            btn.className = 'aw-tab-btn';
            btn.textContent = groups[i].label;
            (function(index){
                btn.onclick = function(){ activate(buttons, groups, index); };
            })(i);
            buttons.push(btn);
            bar.appendChild(btn);
        }

        var firstNode = secNav || secOverview;
        wrap.insertBefore(bar, firstNode);
        activate(buttons, groups, 0);
    }
    setup();
})();
</script>
)JSTABS";

size_t streamStructuresPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "🏭 Amada Welding");
    out.print(FPSTR(STRUCTURES_TABS_JS));
    n += strlen_P(STRUCTURES_TABS_JS);
    out.print(FPSTR(PAGE_STRUCTURES_BODY));
    n += strlen_P(PAGE_STRUCTURES_BODY);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsTag(out, true);
    n += streamPageShellEnd(out);
    return n;
}

#endif // ENABLE_STRUCTURES_PAGE