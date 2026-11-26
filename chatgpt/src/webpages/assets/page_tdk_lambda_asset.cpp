// page_tdk_lambda_asset.cpp — /tdk-lambda console (HTML + inline JS, PROGMEM).
//
// Layout follows the Node32-HUB standard (page_ipg_laser_asset.cpp for the
// shell, /access for the tab organisation): the asset opens its own
// .page/.wrap, sections are .section-box and the page is split into tabs so it
// does not grow into one long vertical strip.
//
// English UI (repo rule: HTML fallback text in English).
// Read-only by default; the trend chart draws in the browser from a compact
// JSON endpoint (no JS library, no PSRAM allocation on the device).
// UI rules: no arrow functions, d.success (not d.ok), Home button first child.
#include "page_tdk_lambda_asset.h"

const char PAGE_TDK_LAMBDA[] PROGMEM = R"rawliteral(
<div class='page tdk-lambda'>
<div class='wrap'>
<style>
  .section-box{margin:14px 0;padding:16px;background:#0f172a;border-radius:8px;border:2px solid #3b82f6}
  .section-title{margin:0 0 12px 0;font-size:18px;color:#60a5fa;font-weight:bold}
  /* tabs — same look as /access */
  .tdk-tab-bar{display:flex;flex-wrap:wrap;gap:6px;margin:14px 0 0 0}
  .tdk-tab-btn{flex:1 1 auto;min-width:90px;border:2px solid #1e40af;border-radius:8px;background:#0f172a;color:#93c5fd;padding:8px 10px;font-size:13px;cursor:pointer;transition:all .2s ease;text-align:center;white-space:nowrap}
  .tdk-tab-btn.active{background:#1d4ed8;color:#fff;border-color:#3b82f6;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
  .tdk-panel{display:none}
  .tdk-panel.active{display:block}
  @media(max-width:480px){.tdk-tab-btn{min-width:72px;padding:7px 6px;font-size:12px}}
  .tdk-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px}
  .tdk-val{background:#1e293b;border-radius:8px;padding:8px;cursor:help}
  .tdk-val .k{font-size:11px;color:#94a3b8}
  .tdk-val .v{font-size:22px;font-weight:700;color:#f8fafc}
  .tdk-val .u{font-size:12px;color:#94a3b8;margin-left:3px}
  .tdk-pre{background:#020617;border:1px solid #1e293b;border-radius:8px;padding:8px;height:220px;overflow:auto;font:12px/1.45 ui-monospace,Consolas,monospace;white-space:pre-wrap}
  .tdk-tbl{width:100%;border-collapse:collapse;font-size:12px}
  .tdk-tbl th,.tdk-tbl td{border-bottom:1px solid #1e293b;padding:5px 6px;text-align:left;vertical-align:top}
  .tdk-tbl th{color:#93c5fd;position:sticky;top:0;background:#0f172a}
  .tdk-scroll{max-height:340px;overflow:auto}
  .tdk-note{font-size:12px;color:#94a3b8}
  .tdk-badge{display:inline-block;padding:2px 7px;border-radius:999px;font-size:11px;font-weight:700}
  .tdk-badge.ok{background:#052e16;color:#4ade80;border:1px solid #14532d}
  .tdk-badge.warn{background:#3b1d05;color:#fbbf24;border:1px solid #78350f}
  .tdk-badge.err{background:#450a0a;color:#f87171;border:1px solid #7f1d1d}
  .tdk-badge.off{background:#1f2937;color:#9ca3af;border:1px solid #374151}
  .tdk-row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:8px}
  .tdk-field{display:flex;flex-direction:column;gap:2px;font-size:12px;color:#94a3b8}
  .tdk-field input,.tdk-field select{min-height:30px}
  canvas#tdkChart{width:100%;height:190px;background:#020617;border:1px solid #1e293b;border-radius:8px;display:block;cursor:help}
</style>

<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Back to the home page' data-i18n-title='tdkTipHome'>&#127968; Home</a></div>
<h1>&#9889; TDK-Lambda / Genesys</h1>
<p class='tdk-note'>SCPI console and CSV logger for TDK-Lambda Genesys (GEN/GENH) supplies.
Every command and reply also appears in the <a href='/monitor' title='Open the Serial Monitor: every TX and RX line appears there' data-i18n-title='tdkTipMonitorLink'>Serial Monitor</a> like any other RS232 line.</p>
<p class='tdk-note' data-i18n='tdkHoverHint'>Hover any control for a short explanation.</p>

<div class='tdk-tab-bar' id='tdkTabBar'></div>

<div class='tdk-panel' data-tab='live' data-tab-default='1'>
  <div class='section-box'>
    <h3 class='section-title'>1) Status</h3>
    <p class='tdk-note' data-i18n='tdkDescStatus'>State of the module itself: enabled, listen or master, link health, polling and CSV. The unit identity appears after Identify.</p>
    <div id='tdkStatus'>loading&hellip;</div>
    <div class='tdk-row' style='margin-top:10px'>
      <span class='tdk-note' title='Identification strings answered by the supply' data-i18n-title='tdkTipIdn'>Unit: <b id='tdkIdn'>--</b> &middot; FW <b id='tdkRev'>--</b> &middot; SN <b id='tdkSn'>--</b></span>
      <button class='btn' onclick='tdkIdentify()' title='Sends IDN?, REV? and SN? to read the unit identity' data-i18n-title='tdkTipIdentify'>&#128269; Identify (IDN? REV? SN?)</button>
    </div>
  </div>

  <div class='section-box'>
    <h3 class='section-title'>2) Live values</h3>
    <p class='tdk-note' data-i18n='tdkDescLive'>Last values reported by the supply. Hover a tile to see which query produces it.</p>
    <div class='tdk-grid'>
      <div class='tdk-val' title='Measured output voltage, as the supply reports it (DVC? field 0 or MV?)' data-i18n-title='tdkTipVMeas'><div class='k'>Measured voltage</div><div><span class='v' id='vMeas'>--</span><span class='u'>V</span></div></div>
      <div class='tdk-val' title='Voltage setpoint (PV?)' data-i18n-title='tdkTipVSet'><div class='k'>Set voltage</div><div><span class='v' id='vSet'>--</span><span class='u'>V</span></div></div>
      <div class='tdk-val' title='Measured output current (DVC? field 1 or MC?)' data-i18n-title='tdkTipIMeas'><div class='k'>Measured current</div><div><span class='v' id='iMeas'>--</span><span class='u'>A</span></div></div>
      <div class='tdk-val' title='Current setpoint (PC?)' data-i18n-title='tdkTipISet'><div class='k'>Set current</div><div><span class='v' id='iSet'>--</span><span class='u'>A</span></div></div>
      <div class='tdk-val' title='Regulation mode reported by the supply: CV for voltage, CC for current' data-i18n-title='tdkTipMode'><div class='k'>Mode</div><div class='v' id='tdkMode'>--</div></div>
      <div class='tdk-val' title='Output state on or off, from STT? or STAT?' data-i18n-title='tdkTipOut'><div class='k'>Output</div><div class='v' id='tdkOut'>--</div></div>
      <div class='tdk-val' title='Over-voltage protection and under-voltage limit (OVP? and UVL?)' data-i18n-title='tdkTipOvp'><div class='k'>OVP / UVL</div><div class='v' id='tdkOvp' style='font-size:16px'>-- / --</div></div>
      <div class='tdk-val' title='Last line received from the supply, exactly as it arrived' data-i18n-title='tdkTipLast'><div class='k'>Last line</div><div class='v' id='tdkLast' style='font-size:12px;word-break:break-all'>--</div></div>
    </div>
  </div>

  <div class='section-box'>
    <h3 class='section-title'>3) Trend</h3>
    <div class='tdk-row'>
      <label class='tdk-note' title='How many samples the chart draws' data-i18n-title='tdkTipWindow'>Window
        <select id='tdkWin' onchange='tdkLoadTrend()' title='How many samples the chart draws' data-i18n-title='tdkTipWindow'>
          <option value='30'>30 samples</option>
          <option value='60' selected>60 samples</option>
          <option value='120'>120 samples</option>
        </select>
      </label>
      <span class='tdk-note'><b style='color:#60a5fa'>&#9472; V</b> &nbsp; <b style='color:#fbbf24'>&#9472; A</b> &nbsp; <span id='tdkTrendInfo'>--</span></span>
    </div>
    <canvas id='tdkChart' width='900' height='190' title='Voltage in blue and current in amber, from the polling buffer' data-i18n-title='tdkTipChart'></canvas>
    <div class='tdk-row' style='margin-top:10px'>
      <label class='tdk-note' title='Log an alert when a reading leaves these limits; 0 turns a limit off' data-i18n-title='tdkTipLimits'><input type='checkbox' id='tdkLimOn' onchange='tdkSaveExtras()'> Alert limits</label>
      <label class='tdk-field' title='Minimum acceptable voltage in V; 0 disables' data-i18n-title='tdkTipLimVMin'>V min <input id='tdkLimVMin' value='0' style='width:80px' onchange='tdkSaveExtras()'></label>
      <label class='tdk-field' title='Maximum acceptable voltage in V; 0 disables' data-i18n-title='tdkTipLimVMax'>V max <input id='tdkLimVMax' value='0' style='width:80px' onchange='tdkSaveExtras()'></label>
      <label class='tdk-field' title='Maximum acceptable current in A; 0 disables' data-i18n-title='tdkTipLimIMax'>I max <input id='tdkLimIMax' value='0' style='width:80px' onchange='tdkSaveExtras()'></label>
      <span id='tdkAlert'></span>
    </div>
    <p class='tdk-note'>0 disables a threshold. An alert is logged to the Serial Monitor and shows here.</p>
  </div>
</div>

<div class='tdk-panel' data-tab='console'>
  <div class='section-box'>
    <h3 class='section-title'>4) Console</h3>
    <div class='tdk-row'>
      <label class='tdk-note' title='Never transmit: only copies what the PLC sends, which is safe on a shared line' data-i18n-title='tdkTipListen'><input type='checkbox' id='tdkListen' onchange='tdkSaveSafety()'> <b>Listen mode</b> &mdash; never transmit</label>
      <label class='tdk-note' title='Allow setpoint commands such as PV, PC, OUT and OVP; off means queries only' data-i18n-title='tdkTipAllowW'><input type='checkbox' id='tdkAllowW' onchange='tdkSaveSafety()'> Allow <b>write</b> commands (PV / PC / OUT / OVP&hellip;)</label>
    </div>
    <div class='tdk-note' id='tdkGuardMsg'></div>
    <input id='tdkCmd' placeholder='e.g. DVC?   MV?   MC?   MODE?   STT?   STAT?' data-i18n-placeholder='tdkTipPhCmd' title='SCPI command to send, for example DVC? or STAT?' data-i18n-title='tdkTipCmd' style='width:100%;margin-top:8px'>
    <div class='tdk-row' style='margin-top:8px'>
      <button class='btn' id='tdkSendBtn' onclick='tdkSend()' title='Send the command and append the reply to the log' data-i18n-title='tdkTipSend'>&#10148; Send</button>
      <button class='btn' onclick="tdkQuick('DVC?')" title='Read voltage and current in a single reply' data-i18n-title='tdkTipQuickDvc'>DVC?</button>
      <button class='btn' onclick="tdkQuick('STT?')" title='Read the status registers' data-i18n-title='tdkTipQuickStt'>STT?</button>
      <button class='btn' onclick="tdkQuick('STAT?')" title='Read the fault and status summary' data-i18n-title='tdkTipQuickStat'>STAT?</button>
      <button class='btn' onclick="tdkQuick('FLT?')" title='Read the fault text' data-i18n-title='tdkTipQuickFlt'>FLT?</button>
      <button class='btn' onclick='tdkClear()' title='Clear the console log only; the module keeps running' data-i18n-title='tdkTipClear'>&#128465; Clear log</button>
    </div>
    <div class='tdk-pre' id='tdkLog' style='margin-top:8px' title='Command and reply log, the same lines the Serial Monitor shows' data-i18n-title='tdkTipLog'>no lines yet.</div>
  </div>
</div>

<div class='tdk-panel' data-tab='csv'>
  <div class='section-box'>
    <h3 class='section-title'>5) CSV log</h3>
    <div id='tdkCsv' title='File path, storage backend and buffered lines' data-i18n-title='tdkTipCsvBox'>--</div>
    <div class='tdk-row' style='margin-top:10px'>
      <label class='tdk-note' title='Ask the supply for values at the interval below and keep them in the trend buffer' data-i18n-title='tdkTipPoll'><input type='checkbox' id='tdkPoll' onchange='tdkSaveCfg()'> Automatic polling</label>
      <label class='tdk-field' title='Seconds between polls, 5 to 3600; one poll is one CSV row' data-i18n-title='tdkTipPollSec'>Interval (s) <input type='number' id='tdkPollSec' min='5' max='3600' value='60' style='width:90px' onchange='tdkSaveCfg()'></label>
      <label class='tdk-note' title='Write the polls to the daily CSV file' data-i18n-title='tdkTipCsvOn'><input type='checkbox' id='tdkCsvOn' onchange='tdkSaveCfg()'> Write CSV</label>
      <label class='tdk-field' title='table: one column per value; sensor: key,value pairs like the sensor logs' data-i18n-title='tdkTipFmt'>Format
        <select id='tdkFmt' onchange='tdkSaveCfg()' title='table: one column per value; sensor: key,value pairs like the sensor logs' data-i18n-title='tdkTipFmt'>
          <option value='sensor'>sensor (key, value)</option>
          <option value='table'>table (columns)</option>
        </select>
      </label>
      <button class='btn' onclick='tdkCsvFlush()' title='Write the buffered lines to the file now, without waiting for the automatic flush' data-i18n-title='tdkTipFlush'>&#128190; Flush now</button>
      <a class='btn' href='/api/tdk-lambda-csv' download='TDK.csv' style='text-decoration:none' title='Download the CSV of the current day from the device' data-i18n-title='tdkTipDownload'>&#11015; Download CSV</a>
    </div>
    <p class='tdk-note'>CSV lives at <code>/&lt;system&gt;/TDK_DD-MM-YYYY.csv</code> &mdash; on the SD card when available,
    otherwise on LittleFS at the same path (written at the configured flush interval).</p>
  </div>
</div>

<div class='tdk-panel' data-tab='profile'>
  <div class='section-box'>
    <h3 class='section-title'>6) Supply profile <span class='tdk-note'>(manual / advanced)</span></h3>
    <p class='tdk-note'>Pick the model/family and save: the profile defines which queries are sent and
    where the values sit in the reply, so the same page works for any Genesys (and for supplies that
    only offer <code>MV?</code>/<code>MC?</code>).</p>
    <div class='tdk-row'>
      <button class='btn' onclick="tdkPreset('genh')" title='GEN and GENH: DVC? returns voltage, current, OVP and UVL in one line' data-i18n-title='tdkTipPresetGenh'>GENH600-1.3 / GEN (DVC? 6 fields)</button>
      <button class='btn' onclick="tdkPreset('split')" title='For supplies that answer MV? and MC? separately' data-i18n-title='tdkTipPresetSplit'>MV? + MC? (two queries)</button>
      <button class='btn' onclick="tdkPreset('mv')" title='DVC? replies in mV and mA, so the scale becomes 0.001' data-i18n-title='tdkTipPresetMv'>DVC? in mV/mA</button>
    </div>
    <div class='tdk-grid'>
      <label class='tdk-field' title='dvc: one query with several fields; split: one query per value' data-i18n-title='tdkTipMode2'>Mode
        <select id='tdkMode2' title='dvc: one query with several fields; split: one query per value' data-i18n-title='tdkTipMode2'>
          <option value='dvc'>dvc (one query, N fields)</option>
          <option value='split'>split (V and I separate)</option>
        </select>
      </label>
      <label class='tdk-field' title='Voltage query, or the multi-field DVC?' data-i18n-title='tdkTipCmdV'>V query (or DVC?)<input id='tdkCmdV' value='DVC?'></label>
      <label class='tdk-field' title='Current query, used in split mode only' data-i18n-title='tdkTipCmdI'>I query (split mode)<input id='tdkCmdI' value='MC?'></label>
      <label class='tdk-field' title='Query for the regulation mode and output state' data-i18n-title='tdkTipCmdSt'>State query<input id='tdkCmdSt' value='MODE?'></label>
      <label class='tdk-field' title='Which reply fields hold V and I, for example 0,1' data-i18n-title='tdkTipMap'>Field map<input id='tdkMap' value='0,1,2,3,4,5'></label>
      <label class='tdk-field' title='Multiplier applied to the voltage field, for unit conversion' data-i18n-title='tdkTipSv'>Scale V<input id='tdkSv' value='1'></label>
      <label class='tdk-field' title='Multiplier applied to the current field' data-i18n-title='tdkTipSi'>Scale I<input id='tdkSi' value='1'></label>
      <label class='tdk-field' title='Value added to the voltage after scaling' data-i18n-title='tdkTipOv'>Offset V<input id='tdkOv' value='0'></label>
      <label class='tdk-field' title='Value added to the current after scaling' data-i18n-title='tdkTipOi'>Offset I<input id='tdkOi' value='0'></label>
      <label class='tdk-field' title='Multi-drop address prefix for supplies on an addressed bus; 0 means none' data-i18n-title='tdkTipAddr'>Multi-drop address (0 = off)<input id='tdkAddr' value='0'></label>
    </div>
    <div class='tdk-row' style='margin-top:10px'>
      <label class='tdk-note' title='Also emit a TDK sensor line, so the values reach the charts and alarms' data-i18n-title='tdkTipInject'><input type='checkbox' id='tdkInject' onchange='tdkSaveExtras()'> Also inject a sensor-style line (<code>TDK &hellip;, vout, &hellip;</code>) into the monitor/chart</label>
      <label class='tdk-note' title='Guess the profile from the first reply; it is only saved when you press Save the detected profile' data-i18n-title='tdkTipAuto'><input type='checkbox' id='tdkAuto' onchange='tdkSaveAuto()'> Detect automatically on the first reply <b>(never overwrites the saved profile)</b></label>
    </div>
    <div class='tdk-row'>
      <button class='btn' onclick='tdkSaveProfile()' title='Save the profile to flash and restart the poller with it' data-i18n-title='tdkTipSaveProfile'>&#128190; Save profile</button>
      <button class='btn' onclick='tdkAdoptDetected()' title='Copy the auto-detected profile into the saved settings' data-i18n-title='tdkTipAdopt'>&#10133; Save the detected profile</button>
      <span class='tdk-note' id='tdkProfMsg'></span>
    </div>
  </div>
</div>

<div class='tdk-panel' data-tab='commands'>
  <div class='section-box'>
    <h3 class='section-title'>7) Commands</h3>
    <p class='tdk-note' data-i18n='tdkDescCatalog'>Genesys SCPI catalogue. Press use on a row to load the command into the console.</p>
    <input id='tdkSearch' placeholder='Filter (e.g. DVC, OVP, monitor)' data-i18n-placeholder='tdkTipPhSearch' oninput='tdkRenderCatalog()' title='Filter the catalogue by command, group or text' data-i18n-title='tdkTipSearch' style='width:100%'>
    <div class='tdk-scroll' style='margin-top:10px'>
      <table class='tdk-tbl'>
        <thead><tr><th title='SCPI mnemonic; queries end with ?' data-i18n-title='tdkTipThCommand'>Command</th><th title='Catalogue group' data-i18n-title='tdkTipThGroup'>Group</th><th title='Typical syntax, with parameters where they exist' data-i18n-title='tdkTipThUsage'>Usage</th><th title='What the command does' data-i18n-title='tdkTipThDesc'>Description</th><th title='Reply format' data-i18n-title='tdkTipThReply'>Reply</th><th title='Cautions and model differences' data-i18n-title='tdkTipThNotes'>Notes</th><th title='Load the command into the console' data-i18n-title='tdkTipThSend'></th></tr></thead>
        <tbody id='tdkCat'></tbody>
      </table>
    </div>
  </div>
</div>

<script>
var _tdkCat=[];
var _tdkTrendSig='';
var _tdkLastSendMs=0;    /* console rate limit (300 ms) */
function _tdkQ(id){return document.getElementById(id);}
function _tdkFail(d){
  var m='Failed: '+((d&&d.message)||'?');
  if(window.customAlert){window.customAlert(m);}
}
function _tdkEsc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
function _tdkPost(url,body){
  return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
    .then(function(r){return r.json();});
}

function tdkStatus(){
  fetch('/api/tdk-lambda-status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    if(!d.success){_tdkQ('tdkStatus').innerHTML='<span class="tdk-badge off">module disabled</span> '+_tdkEsc(d.message||'');return;}
    var st=d.status||{}, v=d.values||{};
    var linkOk=!st.polling||!st.lastRxAgeMs||(st.lastRxAgeMs < (st.pollIntervalMs*3));
    var b='<span class="tdk-badge ok">module active</span> ';
    b+='<span class="tdk-badge '+(st.listen?'warn':'ok')+'">'+(st.listen?'listen':'master')+'</span> ';
    b+='<span class="tdk-badge '+(linkOk?'ok':'warn')+'">'+(linkOk?'link ok':'no data')+'</span> ';
    b+='<span class="tdk-badge '+(st.polling?'ok':'off')+'">polling '+(st.polling?'on':'off')+'</span> ';
    b+='<span class="tdk-badge '+(st.csvEnabled?'ok':'off')+'">csv '+(st.csvEnabled?'on':'off')+'</span> ';
    if(st.lastError){b+='<span class="tdk-badge err">error '+_tdkEsc(st.lastError)+'</span> ';}
    if(st.alertActive){b+='<span class="tdk-badge err">ALERT '+_tdkEsc(st.alertText)+'</span> ';}
    _tdkQ('tdkAlert').innerHTML=st.alertActive?'<span class="tdk-badge err">'+_tdkEsc(st.alertText)+'</span>':'';
    b+='<span class="tdk-note">every '+(st.pollIntervalMs/1000)+' s &middot; last RX '+
       (st.lastRxAgeMs?Math.round(st.lastRxAgeMs/1000)+' s':'--')+' &middot; lines '+(st.logCount||0)+'</span>';
    _tdkQ('tdkStatus').innerHTML=b;

    _tdkQ('vMeas').textContent=(v.valid?v.vMeas.toFixed(2):'--');
    _tdkQ('vSet').textContent=(v.valid?v.vSet.toFixed(2):'--');
    _tdkQ('iMeas').textContent=(v.valid?v.iMeas.toFixed(3):'--');
    _tdkQ('iSet').textContent=(v.valid?v.iSet.toFixed(3):'--');
    var md=(v.mode||'--');
    var mdCls=(md==='CV')?'ok':((md==='CC')?'warn':'off');
    _tdkQ('tdkMode').innerHTML='<span class="tdk-badge '+mdCls+'">'+_tdkEsc(md)+'</span>';
    _tdkQ('tdkOut').innerHTML='<span class="tdk-badge '+(v.outOn?'ok':'off')+'">'+(v.outOn?'ON':'OFF')+'</span>';
    _tdkQ('tdkOvp').textContent=(v.valid?v.ovp.toFixed(1):'--')+' / '+(v.valid?v.uvl.toFixed(1):'--');
    _tdkQ('tdkLast').textContent=st.lastLine||'--';
    _tdkQ('tdkIdn').textContent=st.idn||'--';
    _tdkQ('tdkRev').textContent=st.rev||'--';
    _tdkQ('tdkSn').textContent=st.sn||'--';
    _tdkQ('tdkCsv').innerHTML='<span class="tdk-note">'+(st.csvOnSd?'SD':'LittleFS')+' &middot; '+
      (st.csvSensorFormat?'sensor format':'table format')+' &middot; '+
      _tdkEsc(st.csvPath||'')+' &middot; rows '+(st.csvRows||0)+'</span>';
    _tdkQ('tdkCsv').innerHTML+='<div class="tdk-note" style="margin-top:4px">'+
      (st.autoMap?(st.autoNote?('auto ['+_tdkEsc(st.autoNote)+']'):'auto (no detection - saved profile in use)'):'manual (fixed)')+
      ' &middot; map '+(st.mapUsed||'--')+'</div>';
  }).catch(function(){_tdkQ('tdkStatus').innerHTML='<span class="tdk-badge warn">no response</span>';});
}

function tdkLog(){
  fetch('/api/tdk-lambda-log',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    if(!d.success||!d.entries){return;}
    var out='';
    for(var i=0;i<d.entries.length;i++){out+='['+String(d.entries[i].dir)+'] '+_tdkEsc(d.entries[i].text)+'\n';}
    var el=_tdkQ('tdkLog');
    el.textContent=out||'no lines yet.';
    el.scrollTop=el.scrollHeight;
  }).catch(function(){});
}

function tdkLoadTrend(){
  var n=parseInt(_tdkQ('tdkWin').value,10)||60;
  fetch('/api/tdk-lambda-samples?n='+n,{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    if(!d.success||!d.samples){return;}
    var s=d.samples, sig=s.length+':'+(s.length?String(s[s.length-1][0])+String(s[s.length-1][1]):'');
    if(sig===_tdkTrendSig){return;}          /* no new sample: do not redraw */
    _tdkTrendSig=sig;
    tdkDrawTrend(s);
  }).catch(function(){});
}

function tdkDrawTrend(rows){
  var c=_tdkQ('tdkChart'), ctx=c.getContext('2d');
  var W=c.width, H=c.height;
  ctx.clearRect(0,0,W,H);
  ctx.strokeStyle='#1e293b'; ctx.lineWidth=1;
  for(var g=0;g<=4;g++){
    var y=12+g*(H-24)/4;
    ctx.beginPath(); ctx.moveTo(34,y); ctx.lineTo(W-8,y); ctx.stroke();
  }
  if(!rows||rows.length<2){
    ctx.fillStyle='#64748b'; ctx.font='12px sans-serif';
    ctx.fillText(rows&&rows.length?'collecting samples...':'no samples yet',44,H/2);
    return;
  }
  var maxV=0,maxI=0;
  for(var k=0;k<rows.length;k++){
    if(rows[k][1]>maxV){maxV=rows[k][1];}
    if(rows[k][2]>maxI){maxI=rows[k][2];}
  }
  if(maxV<=0){maxV=1;} if(maxI<=0){maxI=1;}
  function px(i){return 34+(W-42)*(i/(rows.length-1));}
  function pyV(v){return H-12-(H-24)*(v/maxV);}
  function pyI(a){return H-12-(H-24)*(a/maxI);}
  ctx.lineWidth=2;
  ctx.strokeStyle='#60a5fa'; ctx.beginPath();
  for(var i=0;i<rows.length;i++){var x=px(i),y=pyV(rows[i][1]); if(i){ctx.lineTo(x,y);}else{ctx.moveTo(x,y);}}
  ctx.stroke();
  ctx.strokeStyle='#fbbf24'; ctx.beginPath();
  for(var j=0;j<rows.length;j++){var x2=px(j),y2=pyI(rows[j][2]); if(j){ctx.lineTo(x2,y2);}else{ctx.moveTo(x2,y2);}}
  ctx.stroke();
  ctx.fillStyle='#94a3b8'; ctx.font='10px sans-serif';
  ctx.fillText(maxV.toFixed(1)+' V',2,14);
  ctx.fillText('0',22,H-10);
  ctx.fillText(maxI.toFixed(2)+' A',W-52,14);
  _tdkQ('tdkTrendInfo').textContent=rows.length+' pts, max '+maxV.toFixed(1)+' V / '+maxI.toFixed(3)+' A';
}

function tdkSend(){
  var c=_tdkQ('tdkCmd').value.trim();
  if(!c){return;}
  var now=Date.now();
  if((now-_tdkLastSendMs)<300){return;}   /* rate limit: one command per 300 ms */
  _tdkLastSendMs=now;
  _tdkPost('/api/tdk-lambda-send','cmd='+encodeURIComponent(c)).then(function(d){
    if(!d.success){_tdkFail(d);}
    tdkLog();tdkStatus();
  });
}

function tdkQuick(c){
  var now=Date.now();
  if((now-_tdkLastSendMs)<300){return;}
  _tdkQ('tdkCmd').value=c;tdkSend();
}

function tdkIdentify(){
  var seq=['IDN?','REV?','SN?'], i=0;
  function next(){
    if(i>=seq.length){tdkStatus();return;}
    _tdkPost('/api/tdk-lambda-send','cmd='+encodeURIComponent(seq[i++])).then(function(){setTimeout(next,400);});
  }
  next();
}

function tdkClear(){_tdkPost('/api/tdk-lambda-clear','').then(function(){tdkLog();tdkStatus();});}

function tdkCsvFlush(){
  _tdkPost('/api/tdk-lambda-csv-flush','').then(function(d){
    if(d.success){if(window.customAlert){window.customAlert('CSV flushed.');}}else{_tdkFail(d);}
    tdkStatus();
  });
}

function tdkLoadCfg(){
  fetch('/api/tdk-lambda-config',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    if(!d.success){return;}
    _tdkQ('tdkPoll').checked=!!d.poll;
    _tdkQ('tdkPollSec').value=d.poll_s||60;
    _tdkQ('tdkCsvOn').checked=!!d.csv;
    _tdkQ('tdkFmt').value=(d.format==='table')?'table':'sensor';
    _tdkQ('tdkMode2').value=(d.mode==='split')?'split':'dvc';
    _tdkQ('tdkCmdV').value=d.cmd_v||'DVC?';
    _tdkQ('tdkCmdI').value=d.cmd_i||'MC?';
    _tdkQ('tdkCmdSt').value=d.cmd_state||'MODE?';
    _tdkQ('tdkMap').value=d.map||'0,1,2,3,4,5';
    _tdkQ('tdkSv').value=(d.scale_v!==undefined?d.scale_v:1);
    _tdkQ('tdkSi').value=(d.scale_i!==undefined?d.scale_i:1);
    _tdkQ('tdkOv').value=(d.off_v!==undefined?d.off_v:0);
    _tdkQ('tdkOi').value=(d.off_i!==undefined?d.off_i:0);
    _tdkQ('tdkAuto').checked=(d.auto_map!==false);
    _tdkQ('tdkAddr').value=(d.addr!==undefined?d.addr:0);
    _tdkQ('tdkInject').checked=!!d.inject;
    if(d.limits){
      _tdkQ('tdkLimOn').checked=!!d.limits.enabled;
      _tdkQ('tdkLimVMin').value=d.limits.v_min||0;
      _tdkQ('tdkLimVMax').value=d.limits.v_max||0;
      _tdkQ('tdkLimIMax').value=d.limits.i_max||0;
    }
    _tdkQ('tdkListen').checked=!!d.listen;
    _tdkQ('tdkAllowW').checked=!!d.allow_write;
    tdkApplyGuard();
  }).catch(function(){});
}

function tdkApplyGuard(){
  var listen=_tdkQ('tdkListen').checked;
  var allow=_tdkQ('tdkAllowW').checked;
  _tdkQ('tdkCmd').disabled=listen;
  _tdkQ('tdkSendBtn').disabled=listen;
  _tdkQ('tdkGuardMsg').textContent=listen
    ? 'TX disabled: only what passes on the line is read.'
    : (allow ? 'write enabled: PV/PC/OUT/OVP can change the supply.'
             : 'read-only commands (allow-list).');
}

function tdkSaveSafety(){
  tdkApplyGuard();
  _tdkPost('/api/tdk-lambda-config',
    'listen='+(_tdkQ('tdkListen').checked?'1':'0')+
    '&allow_write='+(_tdkQ('tdkAllowW').checked?'1':'0')).then(function(d){
      if(!d.success){_tdkFail(d);}
      tdkStatus();
  });
}

function tdkSaveExtras(){
  var b='addr='+encodeURIComponent(_tdkQ('tdkAddr').value)+
        '&inject='+(_tdkQ('tdkInject').checked?'1':'0')+
        '&limits_enabled='+(_tdkQ('tdkLimOn').checked?'1':'0')+
        '&limit_v_min='+encodeURIComponent(_tdkQ('tdkLimVMin').value)+
        '&limit_v_max='+encodeURIComponent(_tdkQ('tdkLimVMax').value)+
        '&limit_i_max='+encodeURIComponent(_tdkQ('tdkLimIMax').value);
  _tdkPost('/api/tdk-lambda-config',b).then(function(d){
    if(!d.success){_tdkFail(d);}
    tdkStatus();
  });
}

function tdkSaveAuto(){
  _tdkPost('/api/tdk-lambda-config','auto_map='+(_tdkQ('tdkAuto').checked?'1':'0')).then(function(d){
    _tdkQ('tdkProfMsg').textContent=d.success?'auto mode saved.':'failed.';
  });
}

function tdkSaveCfg(){
  var b='poll='+(_tdkQ('tdkPoll').checked?'1':'0')+
        '&poll_s='+encodeURIComponent(_tdkQ('tdkPollSec').value)+
        '&csv='+(_tdkQ('tdkCsvOn').checked?'1':'0')+
        '&format='+encodeURIComponent(_tdkQ('tdkFmt').value);
  _tdkPost('/api/tdk-lambda-config',b).then(function(d){
    if(!d.success){_tdkFail(d);}
    tdkStatus();
  });
}

function tdkProfileBody(){
  return 'mode='+encodeURIComponent(_tdkQ('tdkMode2').value)+
         '&cmd_v='+encodeURIComponent(_tdkQ('tdkCmdV').value)+
         '&cmd_i='+encodeURIComponent(_tdkQ('tdkCmdI').value)+
         '&cmd_state='+encodeURIComponent(_tdkQ('tdkCmdSt').value)+
         '&map='+encodeURIComponent(_tdkQ('tdkMap').value)+
         '&scale_v='+encodeURIComponent(_tdkQ('tdkSv').value)+
         '&scale_i='+encodeURIComponent(_tdkQ('tdkSi').value)+
         '&off_v='+encodeURIComponent(_tdkQ('tdkOv').value)+
         '&off_i='+encodeURIComponent(_tdkQ('tdkOi').value);
}

function tdkSaveProfile(){
  _tdkPost('/api/tdk-lambda-config',tdkProfileBody()).then(function(d){
    _tdkQ('tdkProfMsg').textContent=d.success?'profile saved.':'failed.';
    tdkStatus();
  });
}

function tdkAdoptDetected(){
  fetch('/api/tdk-lambda-status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    var st=d.status||{};
    if(!st.mapUsed){_tdkQ('tdkProfMsg').textContent='nothing detected yet.';return;}
    _tdkQ('tdkMap').value=st.mapUsed;
    _tdkQ('tdkSv').value=st.scaleVUsed;
    _tdkQ('tdkSi').value=st.scaleIUsed;
    tdkSaveProfile();
    _tdkQ('tdkProfMsg').textContent='detected profile saved: '+st.mapUsed;
  });
}

function tdkPreset(which){
  if(which==='split'){
    _tdkQ('tdkMode2').value='split';
    _tdkQ('tdkCmdV').value='MV?';
  } else {
    _tdkQ('tdkMode2').value='dvc';
    _tdkQ('tdkCmdV').value='DVC?';
  }
  _tdkQ('tdkCmdI').value='MC?';
  _tdkQ('tdkCmdSt').value='MODE?';
  _tdkQ('tdkMap').value='0,1,2,3,4,5';
  _tdkQ('tdkSv').value=(which==='mv')?'0.001':'1';
  _tdkQ('tdkSi').value=(which==='mv')?'0.001':'1';
  _tdkQ('tdkOv').value='0';
  _tdkQ('tdkOi').value='0';
  tdkSaveProfile();
}

function tdkLoadCatalog(){
  fetch('/api/tdk-lambda-commands',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    _tdkCat=d.commands||[];
    tdkRenderCatalog();
  }).catch(function(){});
}

function tdkRenderCatalog(){
  var q=(_tdkQ('tdkSearch').value||'').toLowerCase();
  var tb=_tdkQ('tdkCat'),out='';
  for(var i=0;i<_tdkCat.length;i++){
    var c=_tdkCat[i];
    var hay=(c.cmd+' '+c.group+' '+c.desc+' '+c.usage+' '+(c.notes||'')).toLowerCase();
    if(q&&hay.indexOf(q)<0){continue;}
    out+='<tr><td><b>'+_tdkEsc(c.cmd)+'</b></td><td>'+_tdkEsc(c.group)+'</td><td><code>'+_tdkEsc(c.usage)+
         '</code></td><td>'+_tdkEsc(c.desc)+'</td><td><code>'+_tdkEsc(c.reply)+'</code></td><td class="tdk-note">'+
         _tdkEsc(c.notes||'')+'</td><td><button class="btn" onclick="tdkUse(\''+_tdkEsc(c.usage)+'\')" title="Load the command into the console" data-i18n-title="tdkTipThSend">use</button></td></tr>';
  }
  tb.innerHTML=out||'<tr><td colspan="7" class="tdk-note">no matches</td></tr>';
}

function tdkUse(u){
  _tdkQ('tdkCmd').value=u;
  tdkShowTab('console');
  try { _tdkQ('tdkCmd').focus(); } catch (e) {}
}

tdkStatus();tdkLog();tdkLoadCfg();tdkLoadCatalog();tdkLoadTrend();
setInterval(tdkStatus,5000);
setInterval(tdkLog,3000);
setInterval(tdkLoadTrend,30000);

/* ---- tabs (same organisation as /access) ---------------------------------- */
var TDK_TABS = [
  { key:'live',     label:'Live',     labelKey:'tdkTabLive',     tip:'Status, live readings and the trend chart',        tipKey:'tdkTipTabLive' },
  { key:'console',  label:'Console',  labelKey:'tdkTabConsole',  tip:'Send SCPI commands and read the replies',          tipKey:'tdkTipTabConsole' },
  { key:'csv',      label:'CSV',      labelKey:'tdkTabCsv',      tip:'CSV logging: settings, flush and download',        tipKey:'tdkTipTabCsv' },
  { key:'profile',  label:'Setup',    labelKey:'tdkTabProfile',  tip:'Model family and how the reply is parsed',         tipKey:'tdkTipTabProfile' },
  { key:'commands', label:'Commands', labelKey:'tdkTabCommands', tip:'Catalogue of Genesys commands',                    tipKey:'tdkTipTabCommands' }
];

function tdkSwitchTab(btns, panels, idx){
  for (var i = 0; i < btns.length; i++) btns[i].classList.toggle('active', i === idx);
  for (var i = 0; i < panels.length; i++) panels[i].classList.toggle('active', i === idx);
  try { localStorage.setItem('tdk_tab', String(idx)); } catch (e) {}
}

/* Switch to a tab by key (used by the catalogue "use" button). */
function tdkShowTab(key){
  var bar = _tdkQ('tdkTabBar');
  var panels = document.querySelectorAll('.tdk-panel[data-tab]');
  if (!bar || !panels.length) return;
  var btns = bar.querySelectorAll('.tdk-tab-btn');
  for (var i = 0; i < panels.length; i++) {
    if (panels[i].getAttribute('data-tab') === key) { tdkSwitchTab(btns, panels, i); return; }
  }
}

function tdkInitTabs(){
  var bar = _tdkQ('tdkTabBar');
  if (!bar) return;
  if (bar.children.length) return;   /* already built: never duplicate the bar */
  var panels = document.querySelectorAll('.tdk-panel[data-tab]');
  if (!panels.length) return;
  var btns = [];
  var defaultIdx = 0;
  for (var i = 0; i < panels.length; i++) {
    var key = panels[i].getAttribute('data-tab');
    if (panels[i].hasAttribute('data-tab-default')) defaultIdx = i;
    var t = { label:key, labelKey:'', tip:'', tipKey:'' };
    for (var j = 0; j < TDK_TABS.length; j++) {
      if (TDK_TABS[j].key === key) { t = TDK_TABS[j]; break; }
    }
    var b = document.createElement('button');
    b.type = 'button';
    b.className = 'tdk-tab-btn';
    /* label + tooltip: the attribute keeps them translatable, the value gives
       something readable before /api/lang answers. */
    if (t.labelKey) b.setAttribute('data-i18n', t.labelKey);
    if (t.tipKey) b.setAttribute('data-i18n-title', t.tipKey);
    b.textContent = (window._t && t.labelKey) ? window._t(t.labelKey, t.label) : t.label;
    b.title = (window._t && t.tipKey) ? window._t(t.tipKey, t.tip) : t.tip;
    (function(ix){ b.onclick = function(){ tdkSwitchTab(btns, panels, ix); }; })(i);
    btns.push(b);
    bar.appendChild(b);
  }
  try {
    var saved = parseInt(localStorage.getItem('tdk_tab'), 10);
    if (!isNaN(saved) && saved >= 0 && saved < panels.length) defaultIdx = saved;
  } catch (e) {}
  tdkSwitchTab(btns, panels, defaultIdx);
}

tdkInitTabs();
</script>
</div>
</div>
)rawliteral";
