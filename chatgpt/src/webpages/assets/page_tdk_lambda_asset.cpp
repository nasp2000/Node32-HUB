// page_tdk_lambda_asset.cpp — /tdk-lambda console (HTML + inline JS, PROGMEM).
//
// English UI (repo rule: HTML fallback text in English).
// Read-only by default; the trend chart draws in the browser from a compact
// JSON endpoint (no JS library, no PSRAM allocation on the device).
// UI rules: no arrow functions, d.success (not d.ok), Home button first child.
#include "page_tdk_lambda_asset.h"

const char PAGE_TDK_LAMBDA[] PROGMEM = R"rawliteral(
<style>
.tdk-box{border:2px solid #3b82f6;border-radius:10px;padding:12px;margin:14px 0;background:#0f172a;color:#e2e8f0}
.tdk-box h2{margin:0 0 8px;font-size:16px;color:#93c5fd}
.tdk-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}
.tdk-val{background:#1e293b;border-radius:8px;padding:8px}
.tdk-val .k{font-size:11px;color:#94a3b8}
.tdk-val .v{font-size:22px;font-weight:700;color:#f8fafc}
.tdk-val .u{font-size:12px;color:#94a3b8;margin-left:3px}
.tdk-in{width:100%;padding:8px;border-radius:6px;border:1px solid #334155;background:#0b1220;color:#e2e8f0}
.tdk-btn{padding:8px 12px;border-radius:6px;border:0;background:#2563eb;color:#fff;font-weight:600;cursor:pointer;margin:2px}
.tdk-btn.sec{background:#334155}
.tdk-btn:disabled{opacity:.45;cursor:not-allowed}
.tdk-pre{background:#020617;border:1px solid #1e293b;border-radius:8px;padding:8px;height:180px;overflow:auto;font:12px/1.45 ui-monospace,Consolas,monospace;white-space:pre-wrap}
.tdk-tbl{width:100%;border-collapse:collapse;font-size:12px}
.tdk-tbl th,.tdk-tbl td{border-bottom:1px solid #1e293b;padding:5px 6px;text-align:left;vertical-align:top}
.tdk-tbl th{color:#93c5fd;position:sticky;top:0;background:#0f172a}
.tdk-tbl tr:hover{background:#111c33}
.tdk-scroll{max-height:320px;overflow:auto}
.tdk-note{font-size:11px;color:#94a3b8}
.tdk-badge{display:inline-block;padding:2px 7px;border-radius:999px;font-size:11px;font-weight:700}
.tdk-badge.ok{background:#052e16;color:#4ade80;border:1px solid #14532d}
.tdk-badge.warn{background:#3b1d05;color:#fbbf24;border:1px solid #78350f}
.tdk-badge.err{background:#450a0a;color:#f87171;border:1px solid #7f1d1d}
.tdk-badge.off{background:#1f2937;color:#9ca3af;border:1px solid #374151}
canvas#tdkChart{width:100%;height:190px;background:#020617;border:1px solid #1e293b;border-radius:8px;display:block}
</style>

<div class="actions nav-inicio"><a class="btn" href="/">&#127968; Home</a></div>
<h1>&#9889; TDK-Lambda / Genesys</h1>
<p class="tdk-note">SCPI console and CSV logger for TDK-Lambda Genesys (GEN/GENH) supplies.
Every command and reply also appears in the
<a href="/monitor">Serial Monitor</a> like any other RS232 line.</p>

<div class="tdk-box">
  <h2>1) Status</h2>
  <div id="tdkStatus">loading&hellip;</div>
  <div style="margin-top:8px" class="tdk-note">
    Unit: <span id="tdkIdn">--</span> &middot; FW <span id="tdkRev">--</span> &middot; SN <span id="tdkSn">--</span>
    <button class="tdk-btn sec" onclick="tdkIdentify()">Identify (IDN? REV? SN?)</button>
  </div>
</div>

<div class="tdk-box">
  <h2>2) Live values</h2>
  <div class="tdk-grid">
    <div class="tdk-val"><div class="k">Measured voltage</div><div><span class="v" id="vMeas">--</span><span class="u">V</span></div></div>
    <div class="tdk-val"><div class="k">Set voltage</div><div><span class="v" id="vSet">--</span><span class="u">V</span></div></div>
    <div class="tdk-val"><div class="k">Measured current</div><div><span class="v" id="iMeas">--</span><span class="u">A</span></div></div>
    <div class="tdk-val"><div class="k">Set current</div><div><span class="v" id="iSet">--</span><span class="u">A</span></div></div>
    <div class="tdk-val"><div class="k">Mode</div><div class="v" id="tdkMode">--</div></div>
    <div class="tdk-val"><div class="k">Output</div><div class="v" id="tdkOut">--</div></div>
    <div class="tdk-val"><div class="k">OVP / UVL</div><div class="v" id="tdkOvp" style="font-size:16px">-- / --</div></div>
    <div class="tdk-val"><div class="k">Last line</div><div class="v" id="tdkLast" style="font-size:12px;word-break:break-all">--</div></div>
  </div>
</div>

<div class="tdk-box">
  <h2>3) Trend</h2>
  <div style="margin-bottom:6px">
    <label class="tdk-note">Window
      <select id="tdkWin" onchange="tdkLoadTrend()" style="padding:4px;background:#0b1220;color:#e2e8f0;border:1px solid #334155;border-radius:5px">
        <option value="30">30 samples</option>
        <option value="60" selected>60 samples</option>
        <option value="120">120 samples</option>
      </select>
    </label>
    <span class="tdk-note">&nbsp;<b style="color:#60a5fa">&#9472; V</b> &nbsp; <b style="color:#fbbf24">&#9472; A</b> &nbsp;
      <span id="tdkTrendInfo">--</span></span>
  </div>
  <canvas id="tdkChart" width="900" height="190"></canvas>
  <div style="margin-top:8px" class="tdk-note">
    <label><input type="checkbox" id="tdkLimOn" onchange="tdkSaveExtras()"> Alert limits</label>
    <label style="margin-left:12px">V min <input id="tdkLimVMin" value="0" style="width:70px;padding:3px;background:#0b1220;color:#e2e8f0;border:1px solid #334155;border-radius:4px" onchange="tdkSaveExtras()"></label>
    <label style="margin-left:12px">V max <input id="tdkLimVMax" value="0" style="width:70px;padding:3px;background:#0b1220;color:#e2e8f0;border:1px solid #334155;border-radius:4px" onchange="tdkSaveExtras()"></label>
    <label style="margin-left:12px">I max <input id="tdkLimIMax" value="0" style="width:70px;padding:3px;background:#0b1220;color:#e2e8f0;border:1px solid #334155;border-radius:4px" onchange="tdkSaveExtras()"></label>
    &nbsp;<span id="tdkAlert"></span>
  </div>
  <p class="tdk-note">0 disables a threshold. An alert is logged to the Serial Monitor and shows here.</p>
</div>

<div class="tdk-box">
  <h2>4) Console</h2>
  <div style="margin-bottom:8px">
    <label class="tdk-note"><input type="checkbox" id="tdkListen" onchange="tdkSaveSafety()">
      <b>Listen mode</b> &mdash; never transmit (the PLC owns the line)</label><br>
    <label class="tdk-note"><input type="checkbox" id="tdkAllowW" onchange="tdkSaveSafety()">
      Allow <b>write</b> commands (PV / PC / OUT / OVP&hellip;)</label>
    <div class="tdk-note" id="tdkGuardMsg"></div>
  </div>
  <input class="tdk-in" id="tdkCmd" placeholder="e.g. DVC?   MV?   MC?   MODE?   STT?   STAT?">
  <div style="margin-top:8px">
    <button class="tdk-btn" id="tdkSendBtn" onclick="tdkSend()">&#10148; Send</button>
    <button class="tdk-btn sec" onclick="tdkQuick('DVC?')">DVC?</button>
    <button class="tdk-btn sec" onclick="tdkQuick('STT?')">STT?</button>
    <button class="tdk-btn sec" onclick="tdkQuick('STAT?')">STAT?</button>
    <button class="tdk-btn sec" onclick="tdkQuick('FLT?')">FLT?</button>
    <button class="tdk-btn sec" onclick="tdkClear()">&#128465; Clear log</button>
  </div>
  <div class="tdk-pre" id="tdkLog" style="margin-top:8px">no lines yet.</div>
</div>

<div class="tdk-box">
  <h2>5) CSV log</h2>
  <div id="tdkCsv">--</div>
  <div style="margin-top:8px">
    <label><input type="checkbox" id="tdkPoll" onchange="tdkSaveCfg()"> Automatic polling</label>
    <label style="margin-left:14px">Interval (s)
      <input type="number" id="tdkPollSec" min="5" max="3600" value="60" style="width:80px;padding:4px;background:#0b1220;color:#e2e8f0;border:1px solid #334155;border-radius:5px" onchange="tdkSaveCfg()">
    </label>
    <label style="margin-left:14px"><input type="checkbox" id="tdkCsvOn" onchange="tdkSaveCfg()"> Write CSV</label>
    <label style="margin-left:14px">Format
      <select id="tdkFmt" onchange="tdkSaveCfg()" style="padding:4px;background:#0b1220;color:#e2e8f0;border:1px solid #334155;border-radius:5px">
        <option value="sensor">sensor (key, value)</option>
        <option value="table">table (columns)</option>
      </select>
    </label>
    <button class="tdk-btn sec" onclick="tdkCsvFlush()">&#128190; Flush now</button>
    <a class="tdk-btn sec" href="/api/tdk-lambda-csv" download="TDK.csv" style="text-decoration:none">&#11015; Download CSV</a>
  </div>
  <p class="tdk-note">CSV lives at <code>/&lt;system&gt;/TDK_DD-MM-YYYY.csv</code> &mdash; on the SD card when
  available, otherwise on LittleFS at the same path (written at the configured flush interval).</p>
</div>

<div class="tdk-box">
  <h2>6) Supply profile <span class="tdk-note">(manual / advanced)</span></h2>
  <p class="tdk-note">Pick the model/family and save: the profile defines which queries are sent and
  where the values sit in the reply, so the same page works for any Genesys (and for supplies that
  only offer <code>MV?</code>/<code>MC?</code>).</p>
  <div style="margin-bottom:8px">
    <button class="tdk-btn sec" onclick="tdkPreset('genh')">GENH600-1.3 / GEN (DVC? 6 fields)</button>
    <button class="tdk-btn sec" onclick="tdkPreset('split')">MV? + MC? (two queries)</button>
    <button class="tdk-btn sec" onclick="tdkPreset('mv')">DVC? in mV/mA</button>
  </div>
  <div class="tdk-grid">
    <label class="tdk-note">Mode<br>
      <select id="tdkMode2" class="tdk-in">
        <option value="dvc">dvc (one query, N fields)</option>
        <option value="split">split (V and I separate)</option>
      </select>
    </label>
    <label class="tdk-note">V query (or DVC?)<br><input class="tdk-in" id="tdkCmdV" value="DVC?"></label>
    <label class="tdk-note">I query (split mode)<br><input class="tdk-in" id="tdkCmdI" value="MC?"></label>
    <label class="tdk-note">State query<br><input class="tdk-in" id="tdkCmdSt" value="MODE?"></label>
    <label class="tdk-note">Field map<br><input class="tdk-in" id="tdkMap" value="0,1,2,3,4,5"></label>
    <label class="tdk-note">Scale V<br><input class="tdk-in" id="tdkSv" value="1"></label>
    <label class="tdk-note">Scale I<br><input class="tdk-in" id="tdkSi" value="1"></label>
    <label class="tdk-note">Offset V<br><input class="tdk-in" id="tdkOv" value="0"></label>
    <label class="tdk-note">Offset I<br><input class="tdk-in" id="tdkOi" value="0"></label>
    <label class="tdk-note">Multi-drop address (0 = off)<br><input class="tdk-in" id="tdkAddr" value="0"></label>
  </div>
  <div style="margin-top:8px">
    <label class="tdk-note"><input type="checkbox" id="tdkInject" onchange="tdkSaveExtras()">
      Also inject a sensor-style line (<code>vout, …</code>) into the monitor/chart</label><br>
    <label class="tdk-note"><input type="checkbox" id="tdkAuto" onchange="tdkSaveAuto()"> Detect automatically on the first reply
      <b>(never overwrites the saved profile)</b></label><br>
    <button class="tdk-btn" onclick="tdkSaveProfile()">&#128190; Save profile</button>
    <button class="tdk-btn sec" onclick="tdkAdoptDetected()">&#10133; Save the detected profile</button>
    <span class="tdk-note" id="tdkProfMsg"></span>
  </div>
</div>

<div class="tdk-box">
  <h2>7) Commands</h2>
  <input class="tdk-in" id="tdkSearch" placeholder="Filter (e.g. DVC, OVP, monitor)" oninput="tdkRenderCatalog()">
  <div class="tdk-scroll" style="margin-top:8px">
    <table class="tdk-tbl">
      <thead><tr><th>Command</th><th>Group</th><th>Usage</th><th>Description</th><th>Reply</th><th>Notes</th><th></th></tr></thead>
      <tbody id="tdkCat"></tbody>
    </table>
  </div>
</div>

<script>
var _tdkCat=[];
var _tdkTrendSig='';
var _tdkLastSendMs=0;    /* console rate limit (300 ms) */
function _tdkQ(id){return document.getElementById(id);}
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
    if(!d.success){alert('Failed: '+(d.message||'?'));}
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
    alert(d.success?'CSV flushed.':'Failed: '+(d.message||'?'));
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
      if(!d.success){alert('Failed: '+(d.message||'?'));}
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
    if(!d.success){alert('Failed: '+(d.message||'?'));}
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
    if(!d.success){alert('Failed: '+(d.message||'?'));}
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
         _tdkEsc(c.notes||'')+'</td><td><button class="tdk-btn sec" onclick="tdkUse(\''+_tdkEsc(c.usage)+'\')">use</button></td></tr>';
  }
  tb.innerHTML=out||'<tr><td colspan="7" class="tdk-note">no matches</td></tr>';
}

function tdkUse(u){_tdkQ('tdkCmd').value=u;window.scrollTo(0,0);}

tdkStatus();tdkLog();tdkLoadCfg();tdkLoadCatalog();tdkLoadTrend();
setInterval(tdkStatus,5000);
setInterval(tdkLog,3000);
setInterval(tdkLoadTrend,30000);
</script>
)rawliteral";
