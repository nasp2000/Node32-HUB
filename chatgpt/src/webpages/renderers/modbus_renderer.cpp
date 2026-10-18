#include "../../languages/lang_active.h"
#include "modbus_renderer.h"

#include "../assets/page_modbus_asset.h"
#include "page_render_common.h"

static const char MODBUS_RTU_TABS_JS[] = R"JSTABS(<style>
.mb-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.mb-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.mb-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.mb-tab-panel{display:none}
.mb-tab-panel.active{display:block}
@media(max-width:480px){.mb-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr))}.mb-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
</style><script>(function(){
function findSection(k){var byKey=document.querySelector("h2[data-sec='"+k+"']");if(byKey)return byKey.parentNode;var h2=document.querySelectorAll('h2');var i;for(i=0;i<h2.length;i++){var t=(h2[i].textContent||'').toLowerCase();if(t.indexOf(k)>=0){return h2[i].parentNode;}}return null;}}return null;}
function on(btns,secs,idx){var i;for(i=0;i<btns.length;i++){if(i===idx)btns[i].classList.add('active');else btns[i].classList.remove('active');}for(i=0;i<secs.length;i++){if(i===idx)secs[i].classList.add('active');else secs[i].classList.remove('active');}}
function setup(){var wrap=document.querySelector('.wrap');if(!wrap)return;var tabs=[];var sCfg=findSection('rtuConfig');var sDev=findSection('rtuDevices');var sScan=findSection('rtuScan');if(sCfg)tabs.push({l:'Global',s:sCfg});if(sDev)tabs.push({l:L(activeLang->tabLabelDevices),s:sDev});if(sScan)tabs.push({l:'Scan',s:sScan});if(tabs.length<2)return;var bar=document.createElement('div');bar.className='mb-tab-bar';var btns=[];var secs=[];var i;for(i=0;i<tabs.length;i++){tabs[i].s.classList.add('mb-tab-panel');secs.push(tabs[i].s);var b=document.createElement('button');b.type='button';b.className='mb-tab-btn';b.textContent=tabs[i].l;(function(ix){b.onclick=function(){on(btns,secs,ix);};})(i);btns.push(b);bar.appendChild(b);}wrap.insertBefore(bar,tabs[0].s);on(btns,secs,0);}setup();})();</script>)JSTABS";

static const char MODBUS_TCP_TABS_JS[] = R"JSTABS(<style>
.mbtcp-tab-bar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin:20px 0 0 0}
.mbtcp-tab-btn{border:2px solid #3b82f6;border-radius:999px;background:#0f172a;color:#bfdbfe;padding:10px 16px;font-size:13px;cursor:pointer;transition:all .2s ease;min-height:44px;width:100%;display:flex;align-items:center;justify-content:center;line-height:1.25;text-align:center;white-space:normal;word-break:break-word}
.mbtcp-tab-btn.active{background:#1d4ed8;color:#fff;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
.mbtcp-tab-panel{display:none}
.mbtcp-tab-panel.active{display:block}
@media(max-width:480px){.mbtcp-tab-bar{grid-template-columns:repeat(2,minmax(0,1fr))}.mbtcp-tab-btn{font-size:12px;padding:10px 12px;min-height:46px}}
</style><script>(function(){
function findSection(k){var h2=document.querySelectorAll('h2');var i;for(i=0;i<h2.length;i++){var t=(h2[i].textContent||'').toLowerCase();if(t.indexOf(k)>=0){return h2[i].parentNode;}}return null;}
function on(btns,secs,idx){var i;for(i=0;i<btns.length;i++){if(i===idx)btns[i].classList.add('active');else btns[i].classList.remove('active');}for(i=0;i<secs.length;i++){if(i===idx)secs[i].classList.add('active');else secs[i].classList.remove('active');}}
function setup(){var wrap=document.querySelector('.wrap');if(!wrap)return;var tabs=[];var sCfg=findSection('tcpConfig');var sDev=findSection('tcpDevices');var sScan=findSection('tcpScan');if(sCfg)tabs.push({l:'Global',s:sCfg});if(sDev)tabs.push({l:L(activeLang->tabLabelDevices),s:sDev});if(sScan)tabs.push({l:'Scan',s:sScan});if(tabs.length<2)return;var bar=document.createElement('div');bar.className='mbtcp-tab-bar';var btns=[];var secs=[];var i;for(i=0;i<tabs.length;i++){tabs[i].s.classList.add('mbtcp-tab-panel');secs.push(tabs[i].s);var b=document.createElement('button');b.type='button';b.className='mbtcp-tab-btn';b.textContent=tabs[i].l;(function(ix){b.onclick=function(){on(btns,secs,ix);};})(i);btns.push(b);bar.appendChild(b);}wrap.insertBefore(bar,tabs[0].s);on(btns,secs,0);}setup();})();</script>)JSTABS";

size_t streamModbusRtuPageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "Modbus RTU");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(PAGE_MODBUS_RTU));
    n += strlen_P(PAGE_MODBUS_RTU);
    out.print(MODBUS_RTU_TABS_JS);
    n += strlen(MODBUS_RTU_TABS_JS);
    n += streamAutoJsFooter(out);
    out.print(F("<script src='/config-manager.js'></script>"));
    n += strlen("<script src='/config-manager.js'></script>");
    const char* initScr = "<script>if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',initModbusRtuPage);}else{initModbusRtuPage();}</script>";
    out.print(initScr);
    n += strlen(initScr);
    n += streamPageShellEnd(out);
    return n;
}

size_t streamModbusTcpPageToPrint(Print& out) {
    size_t n = 0;
    // PAGE_MODBUS_TCP already includes auto.js, config-manager.js, and the DOMContentLoaded init call.
    n += streamPageShellStart(out, "Modbus TCP");
    // Local-processing page: ALWAYS exempt (before the asset: PAGE_MODBUS_TCP
    // embeds its own auto.js tag, so the flag must precede it).
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(PAGE_MODBUS_TCP));
    n += strlen_P(PAGE_MODBUS_TCP);
    out.print(MODBUS_TCP_TABS_JS);
    n += strlen(MODBUS_TCP_TABS_JS);
    n += streamPageShellEnd(out);
    return n;
}

size_t streamModbusLivePageToPrint(Print& out) {
    size_t n = 0;
    n += streamPageShellStart(out, "📈 Modbus Live");
    // Local-processing page: ALWAYS exempt from the inactivity watchdog.
    n += streamNeverExpireFlag(out);
    out.print(FPSTR(PAGE_MODBUS_LIVE));
    n += strlen_P(PAGE_MODBUS_LIVE);
    n += streamAutoJsFooter(out);
    const char* initScr = "<script>if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',initModbusLivePage);}else{initModbusLivePage();}</script>";
    out.print(initScr);
    n += strlen(initScr);
    n += streamPageShellEnd(out);
    return n;
}
