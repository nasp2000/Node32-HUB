#ifndef PAGE_DO_ASSET_H
#define PAGE_DO_ASSET_H

#include "../../feature_flags.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

#include <Arduino.h>

// Page served at /do — control panel for 8 relay outputs via TCA9554 expander.
static const char DO_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>&#128268; Digital Outputs (DO)</title>
  <link rel='stylesheet' href='/core.css'>
  <script src='/auto.js'></script>
  <style>
    .do-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:12px;margin-top:16px}
    .do-card{border:1px solid #334155;border-radius:8px;padding:14px;text-align:center;background:#0f172a}
    .do-label{font-size:12px;color:#94a3b8;margin-bottom:8px}
    .do-label strong{display:block;font-size:14px;color:#e2e8f0;margin-bottom:2px}
    .btn-on{background:#16a34a;color:#fff;border:none;border-radius:6px;padding:10px 20px;cursor:pointer;font-size:14px;font-weight:600;width:100%;transition:background .2s;min-height:44px}
    .btn-on:hover{background:#15803d}
    .btn-off{background:#334155;color:#94a3b8;border:none;border-radius:6px;padding:10px 20px;cursor:pointer;font-size:14px;font-weight:600;width:100%;transition:background .2s;min-height:44px}
    .btn-off:hover{background:#475569}
    .btn-all{padding:8px 18px;border:none;border-radius:6px;cursor:pointer;font-size:13px;font-weight:600;transition:background .2s;min-height:40px}
    .btn-all-on{background:#2563eb;color:#fff}.btn-all-on:hover{background:#1d4ed8}
    .btn-all-off{background:#dc2626;color:#fff}.btn-all-off:hover{background:#b91c1c}
    .all-bar{display:flex;gap:10px;margin-top:8px;flex-wrap:wrap}
    .note{color:#94a3b8;font-size:11px;margin-top:12px;line-height:1.5}
    .do-cfg{width:100%;border-collapse:collapse;margin-top:12px;font-size:13px}
    .do-cfg th,.do-cfg td{border:1px solid #334155;padding:8px;text-align:left}
    .do-cfg th{background:#1e293b;color:#94a3b8;font-weight:600}
    .do-cfg input[type=text]{width:100%;box-sizing:border-box;padding:6px;border-radius:6px;border:1px solid #334155;background:#0b1220;color:#fff;font-size:13px;min-width:0}
    .do-cfg .do-inv{text-align:center}
    .do-cfg input[type=checkbox]{width:18px;height:18px;accent-color:#20a3b9;cursor:pointer}
  </style>
</head>
<body>
<div class='wrap'>
  <div class='actions nav-inicio'><a href='/' class='btn' title='Go to home page' data-i18n-title='btnHome'>&#127968; Home</a></div>
  <h1>&#128268; Digital Outputs (DO)</h1>
  <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
    <h2>Output Control</h2>
    <div class='all-bar'>
      <button class='btn-all btn-all-on' onclick='setAllDO(true)' title='Set all outputs ON' data-i18n-title='doTipSetAllOn'>&#9654; All ON</button>
      <button class='btn-all btn-all-off' onclick='setAllDO(false)' title='Set all outputs OFF' data-i18n-title='doTipSetAllOff'>&#9632; All OFF</button>
    </div>
    <div class='do-grid'>
      <div class='do-card'><div class='do-label'><strong>DO 1</strong><span id='doName0'>Relay 1</span></div><button id='do0' class='btn-off' data-state='false' onclick='toggleDO(0,this)' title='Toggle this output' data-i18n-title='doTipToggle'>OFF</button></div>
      <div class='do-card'><div class='do-label'><strong>DO 2</strong><span id='doName1'>Relay 2</span></div><button id='do1' class='btn-off' data-state='false' onclick='toggleDO(1,this)' title='Toggle this output' data-i18n-title='doTipToggle'>OFF</button></div>
      <div class='do-card'><div class='do-label'><strong>DO 3</strong><span id='doName2'>Relay 3</span></div><button id='do2' class='btn-off' data-state='false' onclick='toggleDO(2,this)' title='Toggle this output' data-i18n-title='doTipToggle'>OFF</button></div>
      <div class='do-card'><div class='do-label'><strong>DO 4</strong><span id='doName3'>Relay 4</span></div><button id='do3' class='btn-off' data-state='false' onclick='toggleDO(3,this)' title='Toggle this output' data-i18n-title='doTipToggle'>OFF</button></div>
      <div class='do-card'><div class='do-label'><strong>DO 5</strong><span id='doName4'>Relay 5</span></div><button id='do4' class='btn-off' data-state='false' onclick='toggleDO(4,this)' title='Toggle this output' data-i18n-title='doTipToggle'>OFF</button></div>
      <div class='do-card'><div class='do-label'><strong>DO 6</strong><span id='doName5'>Relay 6</span></div><button id='do5' class='btn-off' data-state='false' onclick='toggleDO(5,this)' title='Toggle this output' data-i18n-title='doTipToggle'>OFF</button></div>
      <div class='do-card'><div class='do-label'><strong>DO 7</strong><span id='doName6'>Relay 7</span></div><button id='do6' class='btn-off' data-state='false' onclick='toggleDO(6,this)' title='Toggle this output' data-i18n-title='doTipToggle'>OFF</button></div>
      <div class='do-card'><div class='do-label'><strong>DO 8</strong><span id='doName7'>Relay 8</span></div><button id='do7' class='btn-off' data-state='false' onclick='toggleDO(7,this)' title='Toggle this output' data-i18n-title='doTipToggle'>OFF</button></div>
    </div>
    <p class='note'>&#128161; State saved automatically after each change and restored on boot.</p>
  </div>
  <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
    <h2 data-i18n='doCfgTitle'>Output Configuration</h2>
    <table class='do-cfg'>
      <thead><tr><th>DO</th><th data-i18n='doCfgName'>Name</th><th data-i18n='doCfgInvert'>Invert</th></tr></thead>
      <tbody>
        <tr><td><strong>DO 1</strong></td><td><input type='text' id='doNameInput0' maxlength='16' title='Custom name (max 16 chars)' data-i18n-title='doCfgTipName'></td><td class='do-inv'><input type='checkbox' id='doInv0' title='Invert logical state (active-low load)' data-i18n-title='doCfgInvertTip'></td></tr>
        <tr><td><strong>DO 2</strong></td><td><input type='text' id='doNameInput1' maxlength='16' title='Custom name (max 16 chars)' data-i18n-title='doCfgTipName'></td><td class='do-inv'><input type='checkbox' id='doInv1' title='Invert logical state (active-low load)' data-i18n-title='doCfgInvertTip'></td></tr>
        <tr><td><strong>DO 3</strong></td><td><input type='text' id='doNameInput2' maxlength='16' title='Custom name (max 16 chars)' data-i18n-title='doCfgTipName'></td><td class='do-inv'><input type='checkbox' id='doInv2' title='Invert logical state (active-low load)' data-i18n-title='doCfgInvertTip'></td></tr>
        <tr><td><strong>DO 4</strong></td><td><input type='text' id='doNameInput3' maxlength='16' title='Custom name (max 16 chars)' data-i18n-title='doCfgTipName'></td><td class='do-inv'><input type='checkbox' id='doInv3' title='Invert logical state (active-low load)' data-i18n-title='doCfgInvertTip'></td></tr>
        <tr><td><strong>DO 5</strong></td><td><input type='text' id='doNameInput4' maxlength='16' title='Custom name (max 16 chars)' data-i18n-title='doCfgTipName'></td><td class='do-inv'><input type='checkbox' id='doInv4' title='Invert logical state (active-low load)' data-i18n-title='doCfgInvertTip'></td></tr>
        <tr><td><strong>DO 6</strong></td><td><input type='text' id='doNameInput5' maxlength='16' title='Custom name (max 16 chars)' data-i18n-title='doCfgTipName'></td><td class='do-inv'><input type='checkbox' id='doInv5' title='Invert logical state (active-low load)' data-i18n-title='doCfgInvertTip'></td></tr>
        <tr><td><strong>DO 7</strong></td><td><input type='text' id='doNameInput6' maxlength='16' title='Custom name (max 16 chars)' data-i18n-title='doCfgTipName'></td><td class='do-inv'><input type='checkbox' id='doInv6' title='Invert logical state (active-low load)' data-i18n-title='doCfgInvertTip'></td></tr>
        <tr><td><strong>DO 8</strong></td><td><input type='text' id='doNameInput7' maxlength='16' title='Custom name (max 16 chars)' data-i18n-title='doCfgTipName'></td><td class='do-inv'><input type='checkbox' id='doInv7' title='Invert logical state (active-low load)' data-i18n-title='doCfgInvertTip'></td></tr>
      </tbody>
    </table>
    <div class='all-bar'>
      <button class='btn-all btn-all-on' onclick='saveDoConfig()' data-i18n='doCfgSave' title='Save the digital-output configuration' data-i18n-title='doTipCfgSave'>Save Config</button>
    </div>
    <p class='note'>&#128161; Names and inversion are saved to /config/do.json and applied immediately.</p>
  </div>
</div>
<script>
function t(k,fb){try{if(typeof window._t==='function')return window._t(k,fb);var raw=localStorage.getItem('ui_lang_cache');if(raw){var p=JSON.parse(raw);if(p&&p[k]!==undefined)return p[k];}}catch(e){}return fb!==undefined?fb:k;}
function _updBtn(btn,st){btn.dataset.state=st?'true':'false';btn.className=st?'btn-on':'btn-off';btn.textContent=st?'ON':'OFF';}
function toggleDO(ch,btn){
  var st=btn.dataset.state!=='true';
  fetch('/api/do/set',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ch:ch,state:st})})
    .then(function(r){return r.json();})
    .then(function(d){if(d.success)_updBtn(btn,st);else customAlert(t('doErrPrefix','\u274c Error: ')+(d.message||t('doFailed','failed')));})
    .catch(function(){customAlert(t('doConnErr','\u274c Connection error'));});
}
function setAllDO(st){
  fetch('/api/do/set',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mask:st?255:0})})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.success){for(var i=0;i<8;i++){var b=document.getElementById('do'+i);if(b)_updBtn(b,st);}}
      else customAlert(t('doErrPrefix','\u274c Error: ')+(d.message||t('doFailed','failed')));
    }).catch(function(){customAlert(t('doConnErr','\u274c Connection error'));});
}
function loadStatus(){
  fetch('/api/do/status',{cache:'no-store'})
    .then(function(r){return r.json();})
    .then(function(d){for(var i=0;i<d.ch_count;i++){var b=document.getElementById('do'+i);if(b)_updBtn(b,d.outputs[i]);}})
    .catch(function(){});
}
function applyDoNames(names){
  for(var i=0;i<8;i++){
    var n=document.getElementById('doName'+i);
    if(n){var nm=(names&&names[i]&&names[i].length)?names[i]:('Relay '+(i+1));n.textContent=nm;}
  }
}
function loadDoConfig(){
  fetch('/api/do/config',{cache:'no-store'})
    .then(function(r){return r.json();})
    .then(function(d){
      if(!d)return;
      for(var i=0;i<8;i++){
        var nm=(d.names&&d.names[i])?d.names[i]:'';
        var n=document.getElementById('doName'+i);
        if(n)n.textContent=nm.length?nm:('Relay '+(i+1));
        var inp=document.getElementById('doNameInput'+i);
        if(inp)inp.value=nm;
        var chk=document.getElementById('doInv'+i);
        if(chk)chk.checked=!!((d.invert>>i)&1);
      }
    })
    .catch(function(){});
}
function saveDoConfig(){
  var names=[],invert=0;
  for(var i=0;i<8;i++){
    var inp=document.getElementById('doNameInput'+i);
    names.push(inp?inp.value.trim().substring(0,16):'');
    var chk=document.getElementById('doInv'+i);
    if(chk&&chk.checked)invert|=(1<<i);
  }
  fetch('/api/do/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({names:names,invert:invert})})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d&&d.success){applyDoNames(names);customAlert(t('doCfgSaved','\u2705 Config saved'));}
      else customAlert(t('doErrPrefix','\u274c Error: ')+((d&&d.message)||t('doFailed','failed')));
    })
    .catch(function(){customAlert(t('doConnErr','\u274c Connection error'));});
}
window.onload=function(){loadStatus();loadDoConfig();};
</script>
</body>
</html>
)HTML";

#endif // BOARD_HAS_TCA9554_DO
#endif // PAGE_DO_ASSET_H
