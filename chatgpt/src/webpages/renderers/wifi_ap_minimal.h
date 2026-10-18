#pragma once

#include <Arduino.h>

// Minimal self-contained WiFi config page for AP mode.
// No external CSS, JS, or i18n dependencies — single HTTP request.
// Served when the device is in AP-only mode (no STA connection).

static const char PAGE_WIFI_AP_MINIMAL[] PROGMEM = R"RAW(
<!DOCTYPE html>
<html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>WiFi Setup</title>
<style>
*{box-sizing:border-box}
html,body{height:100%}
body{font-family:Arial,sans-serif;background:#0a1929;color:#e6f1ff;margin:0;padding:16px;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100%}
.w{max-width:460px;width:100%}
h1{font-size:20px;color:#60a5fa;text-align:center;margin-top:0}
.card{background:#0f172a;border:2px solid #3b82f6;border-radius:8px;padding:16px;margin-top:16px}
.card h2{margin-top:0;font-size:16px;color:#60a5fa}
label{display:block;font-size:14px;color:#e0e7ff;margin-bottom:6px}
input{width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px;box-sizing:border-box;margin-bottom:16px}
input:focus{outline:none;border-color:#60a5fa}
.btn{background:#3b82f6;color:#fff;border:none;padding:10px 24px;border-radius:6px;font-size:14px;cursor:pointer;text-align:center;display:inline-block}
.btn:disabled{opacity:0.5;cursor:default}
.net{padding:10px;border:1px solid #334155;border-radius:6px;margin:4px 0;cursor:pointer;font-size:13px;color:#e0e7ff}
.net:hover,.net.sel{border-color:#3b82f6;background:#1e3a8a}
.tc{text-align:center;margin-top:16px}
.hint{font-size:12px;color:#94a3b8;margin-top:-12px;margin-bottom:16px}
#msg{text-align:center;padding:10px;border-radius:6px;margin-top:16px;display:none}
#msg.ok{background:#064e3b;border:1px solid #34d399;color:#a7f3d0}
#msg.err{background:#7f1d1d;border:1px solid #f87171;color:#fecaca}
</style>
</head><body>
<div class='w'>
  <h1>WiFi Setup</h1>
  <div class='card'>
    <form id='wf' onsubmit='return save(event)'>
      <label>Network (SSID)</label>
      <input name='ssid' id='ssid' placeholder='Enter network name' autocomplete='off'>
      <label>Password</label>
      <input type='password' name='password' id='password' placeholder='Leave empty for open networks'>
      <div class='tc'>
        <button type='button' class='btn' id='sb' onclick='scan()'>Scan</button>
        <button type='submit' class='btn' style='margin-left:8px'>Save</button>
      </div>
    </form>
  </div>
  <div class='card' id='netsc' style='display:none'>
    <h2>Available Networks</h2>
    <div id='nets' style='font-size:13px;color:#94a3b8'>Scanning...</div>
  </div>
  <div id='msg'></div>
</div>
<script>
function $(id){return document.getElementById(id)}
function msg(t,ok){var m=$('msg');m.textContent=t;m.className=ok?'ok':'err';m.style.display='block'}
function scan(){
  $('sb').disabled=1;$('sb').textContent='Scanning...';
  $('netsc').style.display='block';$('nets').textContent='Scanning...';
  fetch('/api/wifi-scan').then(function(r){return r.json()}).then(function(d){
    var h='';
    if(d.networks)d.networks.forEach(function(n){
      var rssi=n.rssi>-50?'***':n.rssi>-70?'**':'*';
      var icon=n.encryption==='Open'?'Open':'Secured';
      var esc=n.ssid.replace(/'/g,"\\'").replace(/</g,"&lt;");
      h+='<div class="net" onclick="sel(\''+esc+'\')">'+icon+' '+n.ssid+' '+rssi+'</div>';
    });
    $('nets').innerHTML=h||'<span style="color:#94a3b8">No networks found</span>';
    $('sb').textContent='Scan';
  })['catch'](function(e){$('nets').textContent='Error: '+e.message;$('sb').textContent='Retry'})['finally'](function(){$('sb').disabled=0});
}
function sel(ssid){
  $('ssid').value=ssid;
  document.querySelectorAll('.net').forEach(function(n){n.classList.remove('sel')});
  if(event&&event.target)event.target.classList.add('sel');
  $('password').focus();
}
function save(e){
  e.preventDefault();
  var fd=new FormData($('wf'));fd.set('section','wifi');
  msg('Saving...',true);
  fetch('/api/config',{method:'POST',body:new URLSearchParams(fd),headers:{'Content-Type':'application/x-www-form-urlencoded'}})
  .then(function(r){return r.json()})
  .then(function(d){
    if(d.success){msg((window._t?window._t('apCfgSavedConnecting','Configuration saved! Connecting to the network...'):'Configuration saved! Connecting to the network...'),true);setTimeout(function(){document.getElementById('wf').style.display='none'},2000)}
    else{msg('Error: '+(d.message||'Unknown error'),false)}
  })['catch'](function(e){msg('Network error: '+e.message,false)});
  return false;
}
</script>
</body></html>
)RAW";

inline size_t streamMinimalWifiApPage(Print& out) {
    out.print(FPSTR(PAGE_WIFI_AP_MINIMAL));
    return strlen_P(PAGE_WIFI_AP_MINIMAL);
}
