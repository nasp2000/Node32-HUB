// page_can_asset.h
// PROGMEM HTML fragment for the CAN bus (TWAI) monitor and configuration page.
// Does NOT include DOCTYPE/html/head/body — the shell renderer adds those.

#pragma once
#include <pgmspace.h>

static const char PAGE_CAN[] PROGMEM = R"HTML(
<style>
.can-state{display:inline-block;padding:4px 12px;border-radius:4px;font-weight:700;font-size:13px}
.cs-run{background:#14532d;color:#86efac}
.cs-stop{background:#334155;color:#94a3b8}
.cs-off{background:#7f1d1d;color:#fca5a5}
.cs-rec{background:#713f12;color:#fde68a}
.frame-tbl{width:100%;border-collapse:collapse;font-family:monospace;font-size:12px}
.frame-tbl th{background:#1e293b;color:#60a5fa;padding:6px 8px;text-align:left;position:sticky;top:0}
.frame-tbl td{padding:5px 8px;border-bottom:1px solid #1e293b;color:#e0e7ff}
.frame-tbl tr:nth-child(even) td{background:#0f172a}
.frame-tbl tr:nth-child(odd) td{background:#020617}
.tbl-wrap{max-height:320px;overflow-y:auto;border:1px solid #334155;border-radius:6px;margin-top:10px}
.data-byte{display:inline-block;margin:0 2px;padding:1px 4px;border-radius:3px;background:#1e293b;color:#93c5fd;font-family:monospace}
.ext-badge{font-size:10px;padding:1px 4px;border-radius:3px;background:#1e40af;color:#bfdbfe;margin-left:4px}
.rtr-badge{font-size:10px;padding:1px 4px;border-radius:3px;background:#5b21b6;color:#ddd6fe;margin-left:4px}
.stat-row{display:flex;flex-wrap:wrap;gap:12px;margin-top:10px}
.stat-box{flex:1;min-width:120px;background:#1e293b;border-radius:6px;padding:10px 14px;text-align:center}
.stat-val{font-size:22px;font-weight:700;color:#60a5fa}
.stat-lbl{font-size:11px;color:#94a3b8;margin-top:2px}
.data-inputs{display:flex;gap:6px;flex-wrap:wrap;margin:8px 0}
.data-inputs input{width:52px;text-align:center;padding:6px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-family:monospace;font-size:13px}
.send-row{display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end}
.send-row label{font-size:13px;color:#94a3b8;display:flex;flex-direction:column;gap:4px}
.send-row input[type=text]{width:90px;padding:6px 8px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-family:monospace}
.send-row select{padding:6px 8px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff}
.cb-row{display:flex;gap:16px;align-items:center;margin:8px 0;flex-wrap:wrap}
.cb-row label{font-size:13px;color:#94a3b8;display:flex;align-items:center;gap:6px}
.note{color:#94a3b8;font-size:11px;margin-top:8px;line-height:1.5}
#msgBox{display:none;padding:8px 14px;border-radius:6px;margin-top:10px;font-size:13px}
.msg-ok{background:#14532d;color:#86efac;border:1px solid #16a34a}
.msg-err{background:#7f1d1d;color:#fca5a5;border:1px solid #dc2626}
</style>
<main class='page'>
<div class='wrap'>
<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='canTipHome'>&#127968; Home</a></div>
<h1>&#128268; CAN Bus (TWAI)</h1>
<div class='can-tab-bar' style='display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin:12px 0'>
<button class='can-tab active' data-tab='status' style='padding:8px;border:2px solid #3b82f6;border-radius:999px;background:#1d4ed8;color:#fff;font-size:12px;font-weight:700;cursor:pointer;border:none' title='CAN bus status and bitrate' data-i18n-title='canTipTabStatus'>🚥 Status</button>
<button class='can-tab' data-tab='frames' style='padding:8px;border:2px solid #3b82f6;border-radius:999px;background:transparent;color:#bfdbfe;font-size:12px;font-weight:700;cursor:pointer' title='Captured CAN frames' data-i18n-title='canTipTabFrames'>📊 Frames</button>
<button class='can-tab' data-tab='send' style='padding:8px;border:2px solid #3b82f6;border-radius:999px;background:transparent;color:#bfdbfe;font-size:12px;font-weight:700;cursor:pointer' title='Send a CAN frame' data-i18n-title='canTipTabSend'>📤 Send</button>
<button class='can-tab' data-tab='config' style='padding:8px;border:2px solid #3b82f6;border-radius:999px;background:transparent;color:#bfdbfe;font-size:12px;font-weight:700;cursor:pointer' title='CAN interface configuration' data-i18n-title='canTipTabConfig'>⚙️ Config</button>
</div>

<!-- Status -->
<div class='can-panel' data-tab='status'>
<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2 style='margin-top:0;color:#60a5fa'>&#128225; Bus Status</h2>
<div style='display:flex;align-items:center;gap:14px;flex-wrap:wrap;margin-bottom:12px'>
  <span id='busState' class='can-state cs-stop'>Loading...</span>
  <span id='busMode' style='color:#94a3b8;font-size:13px'></span>
  <span id='busBitrate' style='color:#94a3b8;font-size:13px'></span>
</div>
<div class='stat-row'>
  <div class='stat-box'><div class='stat-val' id='sTxF'>—</div><div class='stat-lbl'>TX Frames</div></div>
  <div class='stat-box'><div class='stat-val' id='sRxF'>—</div><div class='stat-lbl'>RX Frames</div></div>
  <div class='stat-box'><div class='stat-val' id='sBuf'>—</div><div class='stat-lbl'>Buffer</div></div>
  <div class='stat-box'><div class='stat-val' id='sTec'>—</div><div class='stat-lbl'>TEC</div></div>
  <div class='stat-box'><div class='stat-val' id='sRec'>—</div><div class='stat-lbl'>REC</div></div>
  <div class='stat-box'><div class='stat-val' id='sErr'>—</div><div class='stat-lbl'>Errors</div></div>
</div>
<div id='busOffWarn' style='display:none;margin-top:10px;padding:8px 14px;border-radius:6px;background:#7f1d1d;color:#fca5a5;border:1px solid #dc2626;font-size:13px'>
  &#9888; Bus-Off detected — auto-recovery in progress. Check physical connection and terminators.
</div>
<div style='margin-top:12px;display:flex;gap:10px;flex-wrap:wrap'>
  <button class='btn' onclick='clearFrames()' style='padding:8px 20px' title='Clear all buffered frames' data-i18n-title='canTipClearBuffer'>&#128465; Clear buffer</button>
  <button class='btn' id='pauseBtn' onclick='togglePause()' style='padding:8px 20px;background:#374151' title='Pause or resume monitor' data-i18n-title='canTipPause'>&#9646;&#9646; Pause monitor</button>
</div>
</div>
</div>

<!-- Frame Monitor -->
<div class='can-panel' data-tab='frames' style='display:none'>
<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2 style='margin-top:0;color:#60a5fa'>&#128202; Frame Monitor</h2>
<p class='note'>&#128161; Last received frames (max 128). Updates automatically every 1s.</p>
<div class='table-wrapper'>
<table class='frame-tbl'>
<thead><tr><th>Timestamp (ms)</th><th>ID (hex)</th><th>Flags</th><th>DLC</th><th>Dados</th></tr></thead>
<tbody id='frameTbody'><tr><td colspan='5' style='text-align:center;color:#94a3b8'>No frames received</td></tr></tbody>
</table>
</div>
</div>
</div>

<!-- Send Frame -->
<div class='can-panel' data-tab='send' style='display:none'>
<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2 style='margin-top:0;color:#60a5fa'>&#128228; Send Frame</h2>
<div class='send-row'>
  <label>ID (hex)<input type='text' id='txId' value='123' maxlength='8' placeholder='123' title='CAN ID in hex' data-i18n-title='canTipTxId'></label>
  <label>DLC<select id='txDlc' onchange='showDataInputs()' title='Data length code' data-i18n-title='canTipTxDlc'>
    <option value='0'>0</option><option value='1'>1</option><option value='2'>2</option>
    <option value='3'>3</option><option value='4'>4</option><option value='5'>5</option>
    <option value='6'>6</option><option value='7'>7</option><option value='8' selected>8</option>
  </select></label>
</div>
<div class='cb-row'>
  <label><input type='checkbox' id='txExt' title='Use 29-bit extended ID' data-i18n-title='canTipTxExt'> Extended ID (29-bit)</label>
  <label><input type='checkbox' id='txRtr' title='Remote Transmission Request' data-i18n-title='canTipTxRtr'> RTR (Remote Request)</label>
</div>
<div id='dataInputsCont'>
  <p style='font-size:13px;color:#94a3b8;margin:6px 0'>Data bytes (hex, e.g.: FF 00 A5):</p>
  <div class='data-inputs'>
    <input type='text' id='d0' value='00' maxlength='2' placeholder='00' title='Data byte 0 (hex)' data-i18n-title='canTipTxData'>
    <input type='text' id='d1' value='00' maxlength='2' placeholder='00' title='Data byte 1 (hex)' data-i18n-title='canTipTxData'>
    <input type='text' id='d2' value='00' maxlength='2' placeholder='00' title='Data byte 2 (hex)' data-i18n-title='canTipTxData'>
    <input type='text' id='d3' value='00' maxlength='2' placeholder='00' title='Data byte 3 (hex)' data-i18n-title='canTipTxData'>
    <input type='text' id='d4' value='00' maxlength='2' placeholder='00' title='Data byte 4 (hex)' data-i18n-title='canTipTxData'>
    <input type='text' id='d5' value='00' maxlength='2' placeholder='00' title='Data byte 5 (hex)' data-i18n-title='canTipTxData'>
    <input type='text' id='d6' value='00' maxlength='2' placeholder='00' title='Data byte 6 (hex)' data-i18n-title='canTipTxData'>
    <input type='text' id='d7' value='00' maxlength='2' placeholder='00' title='Data byte 7 (hex)' data-i18n-title='canTipTxData'>
  </div>
</div>
<div style='margin-top:12px'>
  <button class='btn' onclick='sendFrame()' title='Send CAN frame' data-i18n-title='canTipSendFrame'>&#9654; Send Frame</button>
</div>
<div id='sendMsg' class='msg-ok' style='display:none'></div>
</div>
</div>

<!-- Configuration -->
<div class='can-panel' data-tab='config' style='display:none'>
<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2 style='margin-top:0;color:#60a5fa'>&#9881; Configuration</h2>
<form id='canForm'>
<div class='cb-row' style='margin-bottom:14px'>
  <label><input type='checkbox' id='cfgEnabled' title='Start CAN bus at boot' data-i18n-title='canTipEnable'> Enable CAN bus on boot</label>
  <label><input type='checkbox' id='cfgLogSd' title='Log frames to SD or PSRAM' data-i18n-title='canTipLogSd'> Save received frames to SD/PSRAM</label>
</div>
<div style='display:flex;gap:14px;flex-wrap:wrap'>
  <label style='display:flex;flex-direction:column;gap:4px;font-size:13px;color:#94a3b8;flex:1;min-width:130px'>
    Bitrate
    <select id='cfgBitrate' style='padding:8px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff' title='Bus speed in Kbps' data-i18n-title='canTipBitrate'>
      <option value='25'>25 Kbps</option>
      <option value='50'>50 Kbps</option>
      <option value='100'>100 Kbps</option>
      <option value='125'>125 Kbps</option>
      <option value='250'>250 Kbps</option>
      <option value='500' selected>500 Kbps</option>
      <option value='800'>800 Kbps</option>
      <option value='1000'>1 Mbps</option>
    </select>
  </label>
  <label style='display:flex;flex-direction:column;gap:4px;font-size:13px;color:#94a3b8;flex:1;min-width:130px'>
    Modo
    <select id='cfgMode' style='padding:8px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff' title='Operating mode' data-i18n-title='canTipMode'>
      <option value='0'>Normal (TX+RX)</option>
      <option value='1'>Listen-only (sniffer)</option>
      <option value='2'>Self-test (loopback)</option>
    </select>
  </label>
  <label style='display:flex;flex-direction:column;gap:4px;font-size:13px;color:#94a3b8;min-width:80px'>
    GPIO TX
    <input type='number' id='cfgTxPin' min='0' max='48' style='width:100%;padding:8px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff' title='GPIO pin for TX' data-i18n-title='canTipTxPin'>
  </label>
  <label style='display:flex;flex-direction:column;gap:4px;font-size:13px;color:#94a3b8;min-width:80px'>
    GPIO RX
    <input type='number' id='cfgRxPin' min='0' max='48' style='width:100%;padding:8px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff' title='GPIO pin for RX' data-i18n-title='canTipRxPin'>
  </label>
</div>
<div style='display:flex;gap:14px;flex-wrap:wrap;margin-top:14px'>
  <label style='display:flex;flex-direction:column;gap:4px;font-size:13px;color:#94a3b8;flex:1;min-width:130px'>
    Filtro ID (hex)
    <input type='text' id='cfgFiltId' maxlength='8' placeholder='0' style='padding:8px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-family:monospace' title='Accepted CAN ID' data-i18n-title='canTipFilterId'>
  </label>
  <label style='display:flex;flex-direction:column;gap:4px;font-size:13px;color:#94a3b8;flex:1;min-width:130px'>
    Filter mask (hex)
    <input type='text' id='cfgFiltMask' maxlength='8' placeholder='FFFFFFFF' style='padding:8px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-family:monospace' title='Filter bitmask' data-i18n-title='canTipFilterMask'>
  </label>
  <label style='display:flex;align-items:center;gap:6px;font-size:13px;color:#94a3b8;padding-top:20px'>
    <input type='checkbox' id='cfgFiltExt' title='Filter extended 29-bit IDs' data-i18n-title='canTipFilterExt'> Filtro para IDs extended
  </label>
</div>
<p class='note'>&#128161; Mask FFFFFFFF accepts all frames. Driver restarts after saving.</p>
<div style='margin-top:16px;display:flex;gap:10px;flex-wrap:wrap'>
  <button type='button' class='btn' onclick='canSaveConfig()' title='Save CAN configuration' data-i18n-title='canTipSaveConfig'>&#128190; Save</button>
  <button type='button' class='btn' onclick='restartDriver()' style='background:#374151' title='Restart CAN driver' data-i18n-title='canTipRestart'>&#9851; Restart driver</button>
</div>
<div id='msgBox'></div>
</form>
</div>
</div>

</div><!-- /.wrap -->
</main>
<script>
function t(k,fb){try{if(typeof window._t==='function')return window._t(k,fb);var raw=localStorage.getItem('ui_lang_cache');if(raw){var p=JSON.parse(raw);if(p&&p[k]!==undefined)return p[k];}}catch(e){}return fb!==undefined?fb:k;}
var _paused=false;
var _stTimer=null,_frTimer=null;


function showDataInputs(){var dlc=parseInt(document.getElementById('txDlc').value,10);var inputs=['d0','d1','d2','d3','d4','d5','d6','d7'];for(var i=0;i<8;i++){var el=document.getElementById(inputs[i]);if(el){el.style.display=(i<dlc?'':'none');}}}

function stateClass(s){if(s==='RUNNING')return 'cs-run';if(s==='BUS_OFF')return 'cs-off';if(s==='RECOVERING')return 'cs-rec';return 'cs-stop';}

function loadStatus(){
  fetch('/api/can/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(j){
    var el=document.getElementById('busState');
    if(el){el.textContent=j.state||'—';el.className='can-state '+stateClass(j.state||'');}
    var modeMap={0:'Normal',1:'Listen-only',2:'Self-test'};
    var md=document.getElementById('busMode');if(md)md.textContent='Mode: '+(modeMap[j.mode]||'—');
    var br=document.getElementById('busBitrate');if(br)br.textContent=j.bitrate_kbps+'Kbps';
    var set=function(id,v){var e=document.getElementById(id);if(e)e.textContent=(v!==undefined&&v!==null)?v:'—';};
    set('sTxF',j.total_tx);set('sRxF',j.total_rx);set('sBuf',j.buffer_frames);
    set('sTec',j.tec);set('sRec',j.rec);set('sErr',(j.rx_errors||0)+(j.tx_errors||0));
    var w=document.getElementById('busOffWarn');
    if(w)w.style.display=(j.state==='BUS_OFF'?'block':'none');
  }).catch(function(){});
}

function loadFrames(){
  if(_paused)return;
  fetch('/api/can/frames?limit=128',{cache:'no-store'}).then(function(r){return r.json();}).then(function(j){
    var tb=document.getElementById('frameTbody');
    if(!tb)return;
    var frames=j.frames||[];
    if(frames.length===0){tb.innerHTML="<tr><td colspan='5' style='text-align:center;color:#94a3b8'>No frames received</td></tr>";return;}
    var html='';
    for(var i=frames.length-1;i>=0;i--){
      var f=frames[i];
      var idStr=f.ext?f.id.toString(16).toUpperCase().padStart(8,'0'):f.id.toString(16).toUpperCase().padStart(3,'0');
      var flags='';
      if(f.ext)flags+='<span class="ext-badge">EXT</span>';
      if(f.rtr)flags+='<span class="rtr-badge">RTR</span>';
      var bytes='';
      for(var b=0;b<(f.dlc||0);b++){bytes+='<span class="data-byte">'+((f.data[b]||0).toString(16).toUpperCase().padStart(2,'0'))+'</span>';}
      html+='<tr><td>'+f.ts+'</td><td>'+idStr+'</td><td>'+(flags||'STD')+'</td><td>'+f.dlc+'</td><td>'+bytes+'</td></tr>';
    }
    tb.innerHTML=html;
  }).catch(function(){});
}

function clearFrames(){
  customConfirm(t('canConfirmClearBuffer','Clear all frames from buffer?'),function(ok){
   if(!ok)return;
   fetch('/api/can/clear',{method:'POST'}).then(function(r){return r.json();}).then(function(j){
    customAlert(j.success?t('canBufferCleared','Buffer cleared!'):t('canErrGeneric','Error:')+(j.message||''));
    if(j.success)loadFrames();
   });
  });
}

function togglePause(){
  _paused=!_paused;
  var btn=document.getElementById('pauseBtn');
  if(btn)btn.textContent=_paused?'\u25B6 '+t('canResumeMonitor','Resume monitor'):'\u258E\u258E '+t('canPauseMonitor','Pause monitor');
}

function sendFrame(){
  var btn=document.querySelector('[onclick="sendFrame()"]');disableBtn(btn);showLoading();
  var id=parseInt(document.getElementById('txId').value||'0',16);
  var dlc=parseInt(document.getElementById('txDlc').value,10);
  var ext=document.getElementById('txExt').checked;
  var rtr=document.getElementById('txRtr').checked;
  var dnames=['d0','d1','d2','d3','d4','d5','d6','d7'];
  var data=[];
  for(var i=0;i<dlc;i++){data.push(parseInt(document.getElementById(dnames[i]).value||'0',16)||0);}
  fetch('/api/can/send',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({id:id,dlc:dlc,ext:ext,rtr:rtr,data:data})})
  .then(function(r){return r.json();}).then(function(j){
    if(j.success){customAlert('✅ '+t('canFrameSent','Frame sent!'));}else{customAlert('❌ '+t('canErrGeneric','Error:')+(j.message||'failed'));}
  }).catch(function(e){customAlert('❌ '+t('canErrGeneric','Error:')+e.message);}).finally(function(){hideLoading();enableBtn(btn);});
}

function canLoadConfig(){
  fetch('/api/can/config',{cache:'no-store'}).then(function(r){return r.json();}).then(function(j){
    var c=j.config||{};
    var setChk=function(id,v){var e=document.getElementById(id);if(e)e.checked=!!v;};
    var setVal=function(id,v){var e=document.getElementById(id);if(e)e.value=v;};
    setChk('cfgEnabled',c.enabled);
    setChk('cfgLogSd',c.log_to_sd);
    setVal('cfgBitrate',c.bitrate_kbps||500);
    setVal('cfgMode',c.mode||0);
    setVal('cfgTxPin',c.tx_pin!==undefined?c.tx_pin:'');
    setVal('cfgRxPin',c.rx_pin!==undefined?c.rx_pin:'');
    setVal('cfgFiltId',(c.filter_id||0).toString(16).toUpperCase());
    setVal('cfgFiltMask',(c.filter_mask!==undefined?c.filter_mask:0xFFFFFFFF).toString(16).toUpperCase());
    setChk('cfgFiltExt',c.filter_ext);
  }).catch(function(){});
}

function canSaveConfig(){
  var btn=document.querySelector('[onclick="canSaveConfig()"]');disableBtn(btn);showLoading();
  var payload={
    enabled:document.getElementById('cfgEnabled').checked,
    log_to_sd:document.getElementById('cfgLogSd').checked,
    bitrate_kbps:parseInt(document.getElementById('cfgBitrate').value,10),
    mode:parseInt(document.getElementById('cfgMode').value,10),
    tx_pin:parseInt(document.getElementById('cfgTxPin').value,10),
    rx_pin:parseInt(document.getElementById('cfgRxPin').value,10),
    filter_id:parseInt(document.getElementById('cfgFiltId').value||'0',16)||0,
    filter_mask:parseInt(document.getElementById('cfgFiltMask').value||'FFFFFFFF',16)||0xFFFFFFFF,
    filter_ext:document.getElementById('cfgFiltExt').checked
  };
  fetch('/api/can/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)})
  .then(function(r){return r.json();}).then(function(j){
    if(j.success){customAlert('\u2705 '+t('proxConfigSaved','Configuration saved!'));showSavedCard();loadStatus();}
    else{customAlert('\u274C '+t('canErrGeneric','Error:')+(j.message||'Failed to save'));}
  }).catch(function(e){customAlert('\u274C '+t('canErrGeneric','Error:')+e.message);}).finally(function(){hideLoading();enableBtn(btn);});
}

function restartDriver(){
  customConfirm(t('canConfirmRestart','Restart CAN driver with current config?'),function(ok){
   if(!ok)return;
   fetch('/api/can/restart',{method:'POST'}).then(function(r){return r.json();}).then(function(j){
    customAlert(j.success?t('canDriverRestarted','Driver restarted!'):t('canErrGeneric','Error:')+(j.message||''));
    if(j.success)loadStatus();
   });
  });
}

// Init
showDataInputs();
canLoadConfig();
loadStatus();
loadFrames();
_stTimer=setInterval(loadStatus,2000);
_frTimer=setInterval(loadFrames,1000);
(function(){var btns=document.querySelectorAll('.can-tab');var panels=document.querySelectorAll('.can-panel');btns.forEach(function(b){b.addEventListener('click',function(){var t=b.getAttribute('data-tab');btns.forEach(function(x){x.style.background='transparent';x.style.color='#bfdbfe';});b.style.background='#1d4ed8';b.style.color='#fff';panels.forEach(function(p){p.style.display=p.getAttribute('data-tab')===t?'':'none';});});});})();
</script>
)HTML";
