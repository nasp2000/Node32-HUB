#include "../../languages/lang_active.h"
#include "sensor_config_page_renderer.h"
#include "../../feature_flags.h"
#include "../assets/page_sensor_config_asset.h"
#include "page_render_common.h"

static const char SENSOR_CONFIG_TABS_JS[] PROGMEM = R"JSTABS(
<style>
.sensor-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.sensor-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.sensor-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.sensor-tab-panel{display:none}
.sensor-tab-panel.active{display:block}
@media(max-width:480px){.sensor-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr))}.sensor-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
</style>
<script>
(function(){
    function findSectionByI18n(key){
        var h = document.querySelector("h3[data-i18n='" + key + "']");
        if (!h) return null;
        if (h.closest) return h.closest("div[style*='border:2px solid #3b82f6']");
        return h.parentNode;
    }
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
        var sMain = findSectionByI18n('sensorMainTitle');
        var sRead = findSectionByI18n('sensorReadingsTitle');
        var sOut  = findSectionByI18n('sensorOutputTitle');
        var sStats= findSectionByI18n('sensorStatsTitle');
        if (sMain) tabs.push({label:'Principal', section:sMain});
        if (sRead) tabs.push({label:'Leituras', section:sRead});
        if (sOut)  tabs.push({label:L(activeLang->tabLabelOutput), section:sOut});
        if (sStats)tabs.push({label:'Estatisticas', section:sStats});
        if (tabs.length < 2) return;

        var bar = document.createElement('div');
        bar.className = 'sensor-tab-bar';
        var buttons = [];
        var sections = [];
        var i;
        for (i = 0; i < tabs.length; i++) {
            tabs[i].section.classList.add('sensor-tab-panel');
            sections.push(tabs[i].section);
            var btn = document.createElement('button');
            btn.type = 'button';
            btn.className = 'sensor-tab-btn';
            btn.textContent = tabs[i].label;
            (function(index){
                btn.onclick = function(){ activate(buttons, sections, index); };
            })(i);
            buttons.push(btn);
            bar.appendChild(btn);
        }
        wrap.insertBefore(bar, tabs[0].section);
        activate(buttons, sections, 0);
    }
    setup();
})();
</script>
)JSTABS";

size_t streamSensorConfigPageToPrint(Print& out) {
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    size_t n = 0;
    n += streamPageShellStart(out, "\xF0\x9F\x8C\xA1\xEF\xB8\x8F Sensor Ambiental");
    out.print(FPSTR(SENSOR_CONFIG_TABS_JS));
    n += strlen_P(SENSOR_CONFIG_TABS_JS);
    out.print(FPSTR(SENSOR_CONFIG_PAGE));
    n += strlen_P(SENSOR_CONFIG_PAGE);
    out.print("<script src='/config-manager.js'></script>");
    n += strlen("<script src='/config-manager.js'></script>");
    n += streamAutoJsFooter(out);
    n += streamPageShellEnd(out);
    return n;
#else
    (void)out;
    return 0;
#endif
}
