#include "page_printer_asset.h"

const char PAGE_PRINTER_HEAD[] PROGMEM = R"HEAD(<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>
<title>⚙️ Settings</title><link rel='stylesheet' href='/core.css'>)HEAD";

const char PAGE_PRINTER_BODY[] PROGMEM = R"BODY(</head><body><div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'><div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div><p style='color:#e6f1ff;margin-top:15px;font-size:14px' data-i18n='loading'>Processing...</p></div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style><main class='page'><div class='wrap'>
<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' data-i18n-title='printerTipHome' title='🏠 Go to home page'>🏠 Home</a></div>
<h1 data-i18n='settPageTitle'>⚙️ Settings</h1>

<form id='printer-form' action='/api/config' method='POST'><input type='hidden' name='_html' value='1'>

<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:16px;text-align:center'>
<h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='settSysNameTitle'>🏷️ System Name</h2>
<div style='font-size:13px;color:#94a3b8;margin-bottom:10px' data-i18n='settSysNameDesc'>Identifies this Node32-HUB. - Useful for networks where you want to know who's who!</div>
<input name='system_name' id='system_name' placeholder='Node32-HUB' value='Node32-HUB' style='width:100%;max-width:420px;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px;display:block;margin:0 auto' data-i18n-title='printerTipSysName' title='Device name for network identification'>
<input type='hidden' name='mdns_enabled_present' value='1'>
<label style='display:flex;align-items:center;justify-content:center;gap:8px;margin:12px auto 4px;font-size:13px;color:#e0e7ff;cursor:pointer'>
  <input type='checkbox' name='mdns_enabled' id='mdns_enabled' style='accent-color:#3b82f6;width:18px;height:18px'>
  <span data-i18n='settMdnsLbl'>Ativar mDNS (encontrar por nome na rede)</span>
</label>
<div style='font-size:12px;color:#94a3b8;margin-bottom:8px;text-align:left' data-i18n-html='settMdnsDesc'>Lets you open the page at <b>http://&lt;name&gt;.local</b> on PC/phone without knowing the IP.<br>The address follows the System Name. Turning it OFF frees about 5-10 KB of RAM.</div>
<div id='mdnsAddr' style='font-size:13px;color:#60a5fa;font-weight:600'></div>
</div>

<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:20px;text-align:center'><h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='settLanguageTitle'>&#127760; Idioma / Language</h2>
<div style='font-size:13px;color:#94a3b8;margin-bottom:10px' data-i18n='settLangDesc'>Language for emails and notifications sent by the device.</div>
<select name='system_language' id='system_language' style='width:33%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px;display:block;margin:0 auto' data-i18n-title='printerTipLang' title='Language for emails and notifications'>
  <option value='pt'>&#127477;&#127481; Portugu&#234;s</option>
  <option value='en'>&#127468;&#127463; English</option>
</select>
</div>



<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:20px;text-align:center'>
<h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='settNtpTitle'>🌍 NTP / Timezone</h2>
<label><span data-i18n='settNtpTzLbl'>Timezone</span> <span class='desc' data-i18n='settNtpTzDesc'>🌍 Choose city/time (Lisbon default)</span></label>
<select name='timezone' id='timezone_select' style='width:33%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px;display:block;margin:0 auto' data-i18n-title='printerTipTimezone' title='Device timezone for scheduling'>
  <option value='WET0WEST,M3.5.0/1,M10.5.0' data-i18n='tzLisbon'>Lisboa (Portugal)</option>
  <option value='GMT0BST,M3.5.0/1,M10.5.0' data-i18n='tzLondon'>Londres (Reino Unido)</option>
  <option value='CET-1CEST,M3.5.0/2,M10.5.0/3' data-i18n='tzMadridParis'>Madrid / Paris / Lisboa (CET/CEST)</option>
  <option value='CET-1CEST,M3.5.0/2,M10.5.0/3' data-i18n='tzBerlin'>Berlim (Alemanha)</option>
  <option value='EST5EDT,M3.2.0/2,M11.1.0/2' data-i18n='tzNewYork'>Nova Iorque (EUA)</option>
  <option value='BRT-3' data-i18n='tzSaoPaulo'>Sao Paulo (Brazil)</option>
  <option value='JST-9' data-i18n='tzTokyo'>Tokyo (Japan)</option>
  <option value='AEST-10AEDT,M10.1.0,M4.1.0/3' data-i18n='tzSydney'>Sydney (Australia)</option>
  <option value='UTC0'>UTC</option>
</select>
<label><span data-i18n='settNtpSrvLbl'>NTP Server</span> <span class='desc' data-i18n='settNtpSrvDesc'>🕐 NTP server (e.g. pool.ntp.org)</span></label>
<select name='ntp_server' id='ntp_server' style='width:33%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px;display:block;margin:0 auto' data-i18n-title='printerTipNtp' title='NTP server for time sync'>
    <option value='pool.ntp.org' selected data-i18n='ntpDefault'>pool.ntp.org (Default)</option>
    <option value='time.google.com'>time.google.com</option>
    <option value='0.pool.ntp.org'>0.pool.ntp.org</option>
    <option value='pt.pool.ntp.org'>pt.pool.ntp.org</option>
</select>
</div>


<div style='text-align:center;margin-top:20px'><button type='submit' class='btn' style='padding:10px 24px' data-i18n='btnSave' data-i18n-title='printerTipSave' title='Save printer config'>💾 Save</button></div></form>
</div><script>
function mdnsHost(name){name=String(name||'Node32-HUB').toLowerCase();var out='';for(var i=0;i<name.length;i++){var code=name.charCodeAt(i);var ch=name[i];if((code>=97&&code<=122)||(code>=48&&code<=57))out+=ch;else if(out.length&&out[out.length-1]!=='-')out+='-';}while(out.length&&out.charAt(out.length-1)==='-')out=out.slice(0,-1);return out||'node32hub'}
function updateMdnsAddr(name){var el=document.getElementById('mdnsAddr');if(!el)return;var h=mdnsHost(name);el.innerHTML='<a href="http://'+h+'.local" target="_blank">http://'+h+'.local</a>';}
function loadCurrentConfig(){
    fetch('/api/config?section=system&t='+Date.now(),{cache:'no-cache'}).then(r=>r.json()).then(d=>{
            let tz=d.ntp_timezone;if(tz===undefined)tz=d.timezone;if(tz===undefined&&d.ntp)tz=d.ntp.timezone;
            if(document.getElementById('timezone_select'))document.getElementById('timezone_select').value=tz||'WET0WEST,M3.5.0/1,M10.5.0';
            let ns=d.ntp_server;if(ns===undefined&&d.ntp)ns=d.ntp.server;
            if(document.getElementById('ntp_server'))document.getElementById('ntp_server').value=ns||'pool.ntp.org';
            let sn=d.system_name;
            if(document.getElementById('system_name'))document.getElementById('system_name').value=sn||'Node32-HUB';
            let me=(d.mdns_enabled===undefined)?true:!!d.mdns_enabled;
            if(document.getElementById('mdns_enabled'))document.getElementById('mdns_enabled').checked=me;
            updateMdnsAddr(sn);
            let sl=d.system_language||'pt';
            if(document.getElementById('system_language'))document.getElementById('system_language').value=sl;
            if(typeof window.setUiLanguagePref==='function')window.setUiLanguagePref(sl);
    }).catch(e=>setTimeout(loadCurrentConfig,2000));
}
window.addEventListener('load',function(){var si=document.getElementById('system_name');if(si){si.addEventListener('input',function(){updateMdnsAddr(si.value);});}loadCurrentConfig();});
document.getElementById('printer-form').addEventListener('submit',function(e){
e.preventDefault();
var btn=this.querySelector('button[type="submit"]');disableBtn(btn);showLoading();
var fd=new FormData(this);
fd.set('section','system');
var chosenLang=(document.getElementById('system_language')||{}).value||'pt';
fetch(this.action,{method:'POST',body:new URLSearchParams(fd),headers:{'Content-Type':'application/x-www-form-urlencoded'}}).then(r=>r.json()).then(d=>{
if(d.success){if(typeof window.setUiLanguagePref==='function')window.setUiLanguagePref(chosenLang);customAlert(d.message||_t('alertSaved','✅ Configuration saved!'));if(typeof showSavedCard==='function')showSavedCard();loadCurrentConfig();if(typeof window.applyI18n==='function')window.applyI18n({forceRefresh:true,langHint:chosenLang});}
else{customAlert(d.message||_t('alertSaveError','❌ Error saving'));}
}).catch(e2=>{var errorPrefix=(typeof window._t==='function')?_t('emailErrorPrefix','Error: '):'Error: ';customAlert(errorPrefix+(e2&&e2.message?e2.message:e2));}).finally(function(){hideLoading();enableBtn(btn);});
});
</script>)BODY";

const char PAGE_PRINTER_FOOTER[] PROGMEM = R"FOOTER(</body></html>)FOOTER";