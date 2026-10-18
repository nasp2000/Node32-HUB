#include "printer_page_renderer.h"

#include "../assets/page_printer_asset.h"
#include "page_render_common.h"
#include "../../feature_flags.h"

static const char PRINTER_TABS_SCRIPT[] PROGMEM = R"JSTABS(<style>
.prn-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.prn-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.prn-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.prn-tab-panel{display:none}
.prn-tab-panel.active{display:block}
@media(max-width:480px){.prn-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr))}.prn-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
</style><script>(function(){function on(btns,secs,idx){if(!btns||!secs||!btns.length||!secs.length)return;var i;for(i=0;i<btns.length;i++){if(i===idx)btns[i].classList.add('active');else btns[i].classList.remove('active');if(i===idx)secs[i].classList.add('active');else secs[i].classList.remove('active');}}function setup(){var form=document.getElementById('printer-form');if(!form)return;var labels=['Nome','Idioma','NTP'];var tabs=[];var i;for(i=0;i<form.children.length;i++){var node=form.children[i];if(!node||node.tagName!=='DIV')continue;var style=node.getAttribute('style')||'';if(style.indexOf('border:2px solid #3b82f6')===-1)continue;if(!node.querySelector('h2'))continue;tabs.push(node);}if(tabs.length<2)return;var count=tabs.length<labels.length?tabs.length:labels.length;var bar=document.createElement('div');bar.className='prn-tab-bar';var btns=[];var secs=[];for(i=0;i<count;i++){secs.push(tabs[i]);var b=document.createElement('button');b.type='button';b.className='prn-tab-btn';b.textContent=labels[i];(function(ix){b.onclick=function(){on(btns,secs,ix);};})(i);btns.push(b);bar.appendChild(b);}if(!secs.length||!tabs[0])return;for(i=0;i<secs.length;i++){secs[i].classList.add('prn-tab-panel');}form.insertBefore(bar,tabs[0]);on(btns,secs,0);}setup();})();</script>)JSTABS";

size_t streamPrinterPageToPrint(Print& out) {
    size_t n = 0;

    out.print(FPSTR(PAGE_PRINTER_HEAD));
    n += strlen_P(PAGE_PRINTER_HEAD);

    const char* configManager = "<script src='/config-manager.js'></script>";
    out.print(configManager);
    n += strlen(configManager);
    n += streamAutoJsTag(out, false);

    out.print(FPSTR(PRINTER_TABS_SCRIPT));
    n += strlen_P(PRINTER_TABS_SCRIPT);

    out.print(FPSTR(PAGE_PRINTER_BODY));
    n += strlen_P(PAGE_PRINTER_BODY);

    out.print(FPSTR(PAGE_PRINTER_FOOTER));
    n += strlen_P(PAGE_PRINTER_FOOTER);

    return n;
}
