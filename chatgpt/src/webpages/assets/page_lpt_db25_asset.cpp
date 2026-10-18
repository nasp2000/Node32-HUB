#include "page_lpt_db25_asset.h"

// LPT / DB25 printer configuration page.
// No DOCTYPE/html/head/body — those are injected by streamPageShellStart() in the renderer.
const char PAGE_LPT_DB25[] PROGMEM = R"HTML(
<main class='page'><div class='wrap'>
<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='lptTipHome'>&#127968; Home</a></div>
<h1>&#128424;&#65039; LPT DB25 Printer</h1>

<form id='lpt-form' action='/api/config' method='POST'>
<input type='hidden' name='_html' value='1'>

<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:16px;text-align:center'>
<h2 style='margin-top:0;color:#60a5fa'>&#128424;&#65039; Printer Emulation</h2>
<label><strong>Emulation Type</strong>
<span style='display:block;font-size:12px;color:#94a3b8;margin-bottom:6px'>Defines the code dialect and supported control sequences (impacts formatting).</span></label>
<select name='emulation_mode' id='emulation_mode' title='Printer emulation type' data-i18n-title='lptTipEmulation' style='width:240px;padding:5px 8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px;display:block;margin:0 auto'>
  <option value='0'>Generic (Standard)</option>
  <option value='1'>Epson ESC/P (Experimental)</option>
  <option value='2'>IBM Proprinter (Experimental)</option>
</select>
<span style='display:block;font-size:12px;color:#94a3b8;margin-bottom:6px'>Mapping table for extended characters or accents to ensure correct printing of national symbols.</span></label>
<select name='charset_mode' id='charset_mode' title='Character encoding table' data-i18n-title='lptTipCharset' style='width:240px;padding:5px 8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px;display:block;margin:0 auto'>
  <option value='0'>UTF-8 (Default)</option>
  <option value='1'>Latin-1 (Experimental)</option>
  <option value='2'>CP850 (Experimental)</option>
</select>
</div>

<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:20px;text-align:center'>
<h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='sendTimeoutsTitle'>&#9203;&#65039; Timeouts e Limites (Apenas LPT/DB25)</h2>
<div style='margin-bottom:14px'>
<div style='font-size:13px;color:#e0e7ff;margin-bottom:6px' data-i18n='sendMinCharsLbl'>&#9881;&#65039; Minimum Characters &mdash; avoids unnecessary sends.</div>
<select name='email_min_content_length' id='min_email_chars' title='Minimum characters before auto-send' data-i18n-title='lptTipMinChars' style='width:240px;padding:5px 8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px;display:block;margin:0 auto'><option value='1'>1</option><option value='3'>3</option><option value='5'>5</option><option value='10'>10</option></select></div>
<div style='margin-bottom:14px;text-align:center'>
<div style='font-size:13px;color:#e0e7ff;margin-bottom:6px' data-i18n='sendAutoSendLbl'>&#128231; Ativar auto-envio para dados LPT/DB25 (ativa Timeouts abaixo)</div>
<input type='hidden' name='email_auto_send_present' value='1'><input type='checkbox' name='email_auto_send' id='email_auto_send' value='1' title='Enable auto-send for LPT data' data-i18n-title='lptTipAutoSend' style='width:18px;height:18px;cursor:pointer'></div>
<div style='margin-bottom:14px'>
<div style='font-size:13px;color:#e0e7ff;margin-bottom:6px' data-i18n='sendActiveTimeoutLbl'>&#9203;&#65039; Active timeout: when LPT auto-send is on (fast sends)</div>
<select name='timeout_active' id='timeout_active' title='Auto-send timeout when active' data-i18n-title='lptTipTimeoutActive' style='width:240px;padding:5px 8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px;display:block;margin:0 auto'><option value='10000'>10s</option><option value='20000'>20s</option><option value='30000'>30s</option></select></div>
<div style='margin-bottom:14px'>
<div style='font-size:13px;color:#e0e7ff;margin-bottom:6px' data-i18n='sendInactiveTimeoutLbl'>&#128260; Inactive timeout: when LPT auto-send is off (spaced sends)</div>
<select name='timeout_inactive' id='timeout_inactive' title='Auto-send timeout when inactive' data-i18n-title='lptTipTimeoutInactive' style='width:240px;padding:5px 8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px;display:block;margin:0 auto'><option value='300000'>5min</option><option value='600000'>10min</option><option value='1200000'>20min</option><option value='1800000'>30min</option><option value='3600000'>1h</option><option value='7200000'>2h</option><option value='14400000'>4h</option><option value='21600000'>6h</option><option value='43200000'>12h</option><option value='86400000'>24h</option></select></div>
<div>
<div style='font-size:13px;color:#e0e7ff;margin-bottom:6px' data-i18n='sendMaxLinesLbl'>&#128203; Maximum lines per LPT/DB25 email</div>
<select name='email_max_lines' id='max_lines' title='Max lines per LPT email' data-i18n-title='lptTipMaxLines' style='width:240px;padding:5px 8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px;display:block;margin:0 auto'><option value='66'>66</option><option value='90'>90</option><option value='180'>180</option><option value='360'>360</option><option value='720'>720</option><option value='1000'>1000</option></select>
</div></div>

<div style='text-align:center;margin-top:24px'>
<button type='submit' class='btn' title='Save LPT config' data-i18n-title='lptTipSave' style='padding:10px 28px'>&#128190; Save</button>
</div>
</form>
</div></main>

<script>
function t(k,fb){try{if(typeof window._t==='function')return window._t(k,fb);var raw=localStorage.getItem('ui_lang_cache');if(raw){var p=JSON.parse(raw);if(p&&p[k]!==undefined)return p[k];}}catch(e){}return fb!==undefined?fb:k;}
(function(){
  function loadConfig(){
    fetch('/api/config?section=system',{cache:'no-cache'}).then(function(r){return r.json();}).then(function(d){
      var em=document.getElementById('emulation_mode');
      var cm=document.getElementById('charset_mode');
      if(em)em.value=d.printer_emulation_mode!==undefined?d.printer_emulation_mode:'0';
      if(cm)cm.value=d.charset_mode!==undefined?d.charset_mode:'0';
      var ea=document.getElementById('email_auto_send');
      var ta=document.getElementById('timeout_active');
      var ti=document.getElementById('timeout_inactive');
      var ml=document.getElementById('max_lines');
      var mc=document.getElementById('min_email_chars');
      if(ea)ea.checked=!!(d.email&&d.email.auto_send);
      if(ta)ta.value=d.autoEmailEnabledTimeout||10000;
      if(ti)ti.value=d.autoEmailDisabledTimeout||300000;
      if(ml)ml.value=(d.email&&d.email.max_lines)||66;
      if(mc)mc.value=(d.email&&d.email.min_content_length)||3;
    }).catch(function(){setTimeout(loadConfig,2000);});
  }
  window.addEventListener('load',loadConfig);

  document.getElementById('lpt-form').addEventListener('submit',function(e){
    e.preventDefault();
    var btn=this.querySelector('button[type="submit"]');disableBtn(btn);showLoading();
    var fd=new FormData(this);
    fd.set('section','system');
    fetch('/api/config',{method:'POST',body:new URLSearchParams(fd),headers:{'Content-Type':'application/x-www-form-urlencoded'}}).then(function(r){return r.json();}).then(function(d){
      if(d.success){customAlert(d.message||t('lptConfigSaved','&#9989; Configuration saved!'));if(typeof showSavedCard==='function')showSavedCard();}
      else{customAlert(t('lptErrSaving','&#10060; Error: ')+(d.message||t('accessUnknown','unknown')));}
    }).catch(function(e2){customAlert(t('lptErrSaving','&#10060; Error: ')+(e2&&e2.message?e2.message:e2));}).finally(function(){hideLoading();enableBtn(btn);});
  });
})();
</script>
)HTML";
