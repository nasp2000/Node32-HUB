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
  /* core.css sets button{width:100%;max-width:420px;margin:6px auto}: undo it so
     the five tabs share one row and the action buttons stay compact. */
  .tdk-tab-btn{flex:1 1 0;min-width:0;width:auto;max-width:none;margin:0;border:2px solid #1e40af;border-radius:8px;background:#0f172a;color:#93c5fd;padding:8px 10px;font-size:13px;cursor:pointer;transition:all .2s ease;text-align:center;white-space:nowrap}
  .tdk-tab-btn.active{background:#1d4ed8;color:#fff;border-color:#3b82f6;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}
  .tdk-panel{display:none}
  .tdk-panel.active{display:block}
  @media(max-width:480px){.tdk-tab-btn{min-width:72px;padding:7px 6px;font-size:12px}}
  .tdk-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}
  @media (min-width:700px){.tdk-val.wide{grid-column:span 2}}
  /* the catalogue rows: core would stretch every button to 100% of the cell */
  .tdk-tbl .btn,.tdk-tbl button{width:auto;max-width:none;margin:0;padding:4px 14px;min-height:30px;display:inline-block;font-size:12px}
  .tdk-val{background:#1e293b;border-radius:8px;padding:8px;cursor:help}
  .tdk-val .k{font-size:11px;color:#94a3b8}
  .tdk-val .v{font-size:22px;font-weight:700;color:#f8fafc}
  .tdk-val .u{font-size:12px;color:#94a3b8;margin-left:3px}
  .tdk-pre{background:#020617;border:1px solid #1e293b;border-radius:8px;padding:8px;height:220px;overflow:auto;font:12px/1.45 ui-monospace,Consolas,monospace;white-space:pre-wrap}
  /* Table: fixed layout + no horizontal scroll. The whole row is the action,
     so there is no per-row button and nothing to scroll sideways. */
  .tdk-tbl{width:100%;border-collapse:collapse;font-size:12px;table-layout:fixed}
  .tdk-tbl th:nth-child(1){width:13%}
  .tdk-tbl th:nth-child(2){width:10%}
  .tdk-tbl th:nth-child(3){width:17%}
  .tdk-tbl th:nth-child(4){width:28%}
  .tdk-tbl th:nth-child(5){width:16%}
  .tdk-tbl th:nth-child(6){width:16%}
  .tdk-tbl th,.tdk-tbl td{border-bottom:1px solid #1e293b;padding:5px 6px;text-align:left;vertical-align:top;word-break:break-word;overflow-wrap:anywhere}
  .tdk-tbl th{color:#93c5fd;position:sticky;top:0;background:#0f172a;z-index:1}
  .tdk-tbl tbody tr{cursor:pointer}
  .tdk-tbl tbody tr:hover{background:#1e293b}
  .tdk-scroll{max-height:340px;overflow-y:auto;overflow-x:hidden}
  @media(max-width:720px){
    .tdk-tbl th:nth-child(2),.tdk-tbl td:nth-child(2),
    .tdk-tbl th:nth-child(6),.tdk-tbl td:nth-child(6){display:none}
    .tdk-tbl th:nth-child(1){width:22%}
    .tdk-tbl th:nth-child(3){width:24%}
    .tdk-tbl th:nth-child(4){width:34%}
    .tdk-tbl th:nth-child(5){width:20%}
  }
  .tdk-note{font-size:12px;color:#94a3b8}
  .tdk-line{font:12px/1.45 ui-monospace,Consolas,monospace;background:#020617;border:1px solid #1e293b;border-radius:6px;padding:6px 8px;margin-top:6px;white-space:pre-wrap;word-break:break-all;color:#93c5fd;cursor:help}
  .tdk-line-rx{color:#fbbf24}
  /* sub-heading inside a section (not a .section-title: that one is per box) */
  .tdk-subtitle{margin:16px 0 6px 0;font-size:14px;color:#60a5fa;font-weight:bold}
  .tdk-badge{display:inline-block;padding:2px 7px;border-radius:999px;font-size:11px;font-weight:700}
  .tdk-badge.ok{background:#052e16;color:#4ade80;border:1px solid #14532d}
  .tdk-badge.warn{background:#3b1d05;color:#fbbf24;border:1px solid #78350f}
  .tdk-badge.err{background:#450a0a;color:#f87171;border:1px solid #7f1d1d}
  .tdk-badge.off{background:#1f2937;color:#9ca3af;border:1px solid #374151}
  .tdk-row{display:flex;gap:8px;flex-wrap:wrap;align-items:flex-end;margin-bottom:8px}
  .tdk-row > .btn,.tdk-row > button{width:auto;max-width:none;margin:0;flex:0 0 auto;padding:8px 16px;min-height:36px;display:inline-block}
  .tdk-field{display:flex;flex-direction:column;gap:2px;font-size:12px;color:#94a3b8}
  .tdk-field input,.tdk-field select{min-height:30px}
  canvas#tdkChart{width:100%;height:270px;background:#020617;border:1px solid #1e293b;border-radius:8px;display:block;cursor:help}
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
    </div>
    <div class='tdk-row'>
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
      <div class='tdk-val wide' title='Last line received from the supply, exactly as it arrived' data-i18n-title='tdkTipLast'><div class='k'>Last line</div><div class='v' id='tdkLast' style='font-size:12px;word-break:break-all'>--</div></div>
    </div>
  </div>

  <div class='section-box'>
    <h3 class='section-title'>3) Trend</h3>
    <div class='tdk-row'>
      <label class='tdk-note' title='How many samples the chart draws' data-i18n-title='tdkTipWindow'>Window
        <select id='tdkWin' onchange='tdkLoadTrend()' title='How many samples the chart draws (the buffer holds 240)' data-i18n-title='tdkTipWindow'>
          <option value='60'>60 samples</option>
          <option value='120' selected>120 samples</option>
          <option value='240'>240 samples</option>
        </select>
      </label>
      <span class='tdk-note'><b style='color:#60a5fa'>&#9472; V</b> &nbsp; <b style='color:#fbbf24'>&#9472; A</b> &nbsp; <b style='color:#4ade80'>&#9472; W</b> &nbsp; <span id='tdkTrendInfo'>--</span></span>
    </div>
    <canvas id='tdkChart' width='900' height='270' title='Voltage, current and power, each on its own scale, from the polling buffer' data-i18n-title='tdkTipChart'></canvas>
    <div class='tdk-row' style='margin-top:10px'>
      <label class='tdk-note' title='Log an alert when a reading leaves these limits; 0 turns a limit off' data-i18n-title='tdkTipLimits'><input type='checkbox' id='tdkLimOn' onchange='tdkSaveExtras()'> Alert limits</label>
      <span id='tdkAlert'></span>
    </div>
    <div class='tdk-row'>
      <label class='tdk-field' title='Minimum acceptable voltage in V; 0 disables' data-i18n-title='tdkTipLimVMin'>V min <input id='tdkLimVMin' value='0' style='width:90px' onchange='tdkSaveExtras()'></label>
      <label class='tdk-field' title='Maximum acceptable voltage in V; 0 disables' data-i18n-title='tdkTipLimVMax'>V max <input id='tdkLimVMax' value='0' style='width:90px' onchange='tdkSaveExtras()'></label>
      <label class='tdk-field' title='Maximum acceptable current in A; 0 disables' data-i18n-title='tdkTipLimIMax'>I max <input id='tdkLimIMax' value='0' style='width:90px' onchange='tdkSaveExtras()'></label>
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
    <div class='tdk-grid'>
      <div class='tdk-val' title='Storage backend: SD card when mounted, otherwise LittleFS at the same path' data-i18n-title='tdkTipCsvBackend'><div class='k'>Backend</div><div class='v' id='csvBackend' style='font-size:16px'>--</div></div>
      <div class='tdk-val wide' title='File path of the day' data-i18n-title='tdkTipCsvPath'><div class='k'>File</div><div class='v' id='csvPath' style='font-size:12px;word-break:break-all'>--</div></div>
      <div class='tdk-val' title='Rows written to that file today' data-i18n-title='tdkTipCsvRows'><div class='k'>Rows today</div><div class='v' id='csvRows' style='font-size:20px'>--</div></div>
      <div class='tdk-val' title='sensor: key,value pairs; table: one column per value' data-i18n-title='tdkTipCsvFormat'><div class='k'>Format</div><div class='v' id='csvFormat' style='font-size:14px'>--</div></div>
    </div>
    <div class='tdk-note' id='tdkCsvAuto' style='margin-top:8px'>--</div>
    <div class='tdk-row' style='margin-top:10px'>
      <label class='tdk-note' title='Ask the supply for values at the interval below and keep them in the trend buffer' data-i18n-title='tdkTipPoll'><input type='checkbox' id='tdkPoll' onchange='tdkSaveCfg()'> Automatic polling</label>
      <label class='tdk-field' title='Seconds between polls, 60 minimum' data-i18n-title='tdkTipPollSec'>Interval (s) <input type='number' id='tdkPollSec' min='60' max='3600' value='60' style='width:90px' onchange='tdkSaveCfg()'></label>
      <label class='tdk-field' title='One CSV row every N polls. With N above 1 the row carries min/avg/max and the energy of the window' data-i18n-title='tdkTipCsvEvery'>Row every <input type='number' id='tdkCsvEvery' min='1' max='60' value='1' style='width:80px' onchange='tdkSaveCfg()'></label>
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
    <h4 class='tdk-subtitle' data-i18n='tdkSessionTitle'>Since boot</h4>
    <div class='tdk-grid'>
      <div class='tdk-val' title='Energy delivered since boot (V x I integrated over time)' data-i18n-title='tdkTipSessWh'><div class='k'>Energy</div><div class='v' id='sessWh' style='font-size:18px'>--</div></div>
      <div class='tdk-val' title='Time the supply spent in current limit (CC)' data-i18n-title='tdkTipSessCc'><div class='k'>In CC</div><div class='v' id='sessCc' style='font-size:18px'>--</div></div>
      <div class='tdk-val' title='Time with the output current at its setpoint' data-i18n-title='tdkTipSessLim'><div class='k'>At limit</div><div class='v' id='sessLim' style='font-size:18px'>--</div></div>
      <div class='tdk-val' title='Time with the output switched off' data-i18n-title='tdkTipSessOff'><div class='k'>Output off</div><div class='v' id='sessOff' style='font-size:18px'>--</div></div>
      <div class='tdk-val' title='How many times the regulation changed between CV and CC (inrush, load steps)' data-i18n-title='tdkTipSessTrans'><div class='k'>CV/CC changes</div><div class='v' id='sessTrans' style='font-size:18px'>--</div></div>
    </div>

    <h4 class='tdk-subtitle' data-i18n='tdkCsvPrevTitle'>Last polls (ring buffer)</h4>
    <div class='tdk-scroll' style='max-height:340px'>
      <table class='tdk-tbl'>
        <thead><tr><th title='Time of the poll' data-i18n-title='tdkTipThTime'>Time</th><th title='Measured voltage' data-i18n-title='tdkTipThVolt'>V</th><th title='Measured current' data-i18n-title='tdkTipThAmp'>A</th></tr></thead>
        <tbody id='tdkPrevBody'><tr><td colspan='3' class='tdk-note'>no samples yet.</td></tr></tbody>
      </table>
    </div>
    <p class='tdk-note'>CSV lives at <code>/&lt;system&gt;/TDK_DD-MM-YYYY.csv</code> &mdash; on the SD card when available,
    otherwise on LittleFS at the same path (written at the configured flush interval). The table above is
    the same ring buffer the trend draws from.</p>
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
          <option value='dvc'>dvc (one query)</option>
          <option value='split'>split (two queries)</option>
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
      <label class='tdk-field' title='Multi-drop address prefix for supplies on an addressed bus; 0 means none' data-i18n-title='tdkTipAddr'>Addr (0 = off)<input id='tdkAddr' value='0'></label>
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
    <p class='tdk-note' data-i18n='tdkDescCatalog'>Genesys SCPI catalogue. Pick a command in the menu and press Send (queries), or click a row to load it into the console.</p>
    <input id='tdkSearch' placeholder='Filter (e.g. DVC, OVP, monitor)' data-i18n-placeholder='tdkTipPhSearch' oninput='tdkRenderCatalog()' title='Filter the catalogue by command, group or text' data-i18n-title='tdkTipSearch' style='width:100%'>
    <div class='tdk-row' style='margin-top:10px'>
      <label class='tdk-field' style='min-width:260px;flex:1 1 260px' title='Pick a command and press Send (queries) or Load (editable in the Console)' data-i18n-title='tdkTipCatPick'>Command
        <select id='tdkCatSel' title='Catalogue command' data-i18n-title='tdkTipCatPick'></select>
      </label>
      <button class='btn' id='tdkCatSendBtn' onclick='tdkCatSend()' title='Send the selected command' data-i18n-title='tdkTipCatSendBtn'>&#10148; Send</button>
      <button class='btn' onclick='tdkCatLoad()' title='Load the selected command into the console' data-i18n-title='tdkTipCatLoad'>Load</button>
      <span class='tdk-note' id='tdkCatSendMsg'></span>
    </div>
    <div class='tdk-line' id='tdkCatSentLine' title='Command sent from this menu' data-i18n-title='tdkTipCatSent'>TX&gt; --</div>
    <div class='tdk-line tdk-line-rx' id='tdkCatReplyLine' title='Last reply from the supply' data-i18n-title='tdkTipCatReply'>RX  --</div>
    <div class='tdk-scroll' style='margin-top:10px'>
      <table class='tdk-tbl'>
        <thead><tr><th title='SCPI mnemonic; queries end with ?' data-i18n-title='tdkTipThCommand'>Command</th><th title='Catalogue group' data-i18n-title='tdkTipThGroup'>Group</th><th title='Typical syntax, with parameters where they exist' data-i18n-title='tdkTipThUsage'>Usage</th><th title='What the command does' data-i18n-title='tdkTipThDesc'>Description</th><th title='Reply format' data-i18n-title='tdkTipThReply'>Reply</th><th title='Cautions and model differences' data-i18n-title='tdkTipThNotes'>Notes</th></tr></thead>
        <tbody id='tdkCat'></tbody>
      </table>
    </div>
  </div>
</div>

<script>
var _tdkCat=[];
var _tdkTrendSig='';
var _tdkTrendTimer=null;   /* {id, ms} of the adaptive trend refresh */
var _tdkLastSendMs=0;    /* console rate limit (300 ms) */
function _tdkQ(id){return document.getElementById(id);}
function _tdkFail(d){
  var m='Failed: '+((d&&d.message)||'?');
  if(window.customAlert){window.customAlert(m);}
}
function _tdkT(k,fb){return (window._t)?window._t(k,fb):fb;}
function _tdkDur(ms){
  var t=Math.floor((ms||0)/1000),h=Math.floor(t/3600),m=Math.floor((t%3600)/60);
  if(h>0){return h+'h '+m+'m';}
  if(m>0){return m+'m';}
  return t+'s';
}
function _tdkEsc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
function _tdkPost(url,body){
  return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
    .then(function(r){return r.json();});
}

function _tdkAsleep(){return (typeof document!=='undefined' && document.hidden===true);}
function tdkStatus(){
  if(_tdkAsleep()){return;}                 /* background tab: no fetch */
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
    if(typeof _tdkArmTrend==='function'){_tdkArmTrend(st.pollIntervalMs||30000);}
    var cadence = Math.round((st.pollIntervalMs||0)/1000);
    var rowS = Math.round((st.csvRowMs||st.pollIntervalMs||0)/1000);
    b+='<span class="tdk-note">poll '+cadence+' s'+
       (rowS!==cadence?' \u00B7 row '+rowS+' s':'')+' \u00B7 last RX '+
       (st.lastRxAgeMs?Math.round(st.lastRxAgeMs/1000)+' s':'--')+' \u00B7 lines '+(st.logCount||0)+'</span>';
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
    _tdkQ('csvBackend').textContent=(st.csvOnSd?'SD card':'LittleFS');
    _tdkQ('csvPath').textContent=st.csvPath||'--';
    _tdkQ('csvRows').textContent=(st.csvRows||0);
    _tdkQ('csvFormat').textContent=(st.csvSensorFormat?'sensor':'table');
    var wh = st.sessionWh||0;
    _tdkQ('sessWh').textContent = (wh>=1000? (wh/1000).toFixed(2)+' kWh' : wh.toFixed(2)+' Wh');
    _tdkQ('sessCc').textContent  = _tdkDur(st.sessionCcMs);
    _tdkQ('sessLim').textContent = _tdkDur(st.sessionLimitMs);
    _tdkQ('sessOff').textContent = _tdkDur(st.sessionOffMs);
    _tdkQ('sessTrans').textContent = (st.cvccTransitions||0);
    _tdkQ('tdkCsvAuto').textContent=
      (st.autoMap?(st.autoNote?('auto ['+st.autoNote+']'):'auto (no detection - saved profile in use)'):'manual (fixed)')+
      ' \u00B7 map '+(st.mapUsed||'--');
  }).catch(function(){_tdkQ('tdkStatus').innerHTML='<span class="tdk-badge warn">no response</span>';});
}

function tdkLog(){
  if(_tdkAsleep()){return;}
  fetch('/api/tdk-lambda-log',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    if(!d.success||!d.entries){return;}
    var out='',lastRx='',lastTx='';
    for(var i=0;i<d.entries.length;i++){
      out+='['+String(d.entries[i].dir)+'] '+_tdkEsc(d.entries[i].text)+'\n';
      var dir=String(d.entries[i].dir).toUpperCase();
      if(dir==='RX'){lastRx=String(d.entries[i].text);}
      else if(dir==='TX'){lastTx=String(d.entries[i].text);}
    }
    _tdkLines(lastTx||'--',lastRx||'--');
    var el=_tdkQ('tdkLog');
    el.textContent=out||'no lines yet.';
    el.scrollTop=el.scrollHeight;
  }).catch(function(){});
}

function tdkLoadTrend(){
  if(_tdkAsleep()){return;}                 /* resume on visibilitychange */
  var n=parseInt(_tdkQ('tdkWin').value,10)||120;
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
  var W=c.width, H=c.height, i;
  ctx.clearRect(0,0,W,H);
  if(!rows||rows.length<2){
    ctx.fillStyle='#64748b'; ctx.font='12px sans-serif';
    ctx.fillText(rows&&rows.length?'collecting samples...':'no samples yet',46,H/2);
    return;
  }

  /* Three bands on one canvas: V, I and P (P = V x I computed here, so the
     module keeps sending only v and i). Each band scales to its own min/max
     with a 10 % margin — on a 0..max axis a 600 V supply with 2 V of ripple is
     a flat line. Nothing is fetched for this: same samples, larger window. */
  var bands=[
    {key:'V', unit:'V', colour:'#60a5fa', dec:2, get:function(r){return r[1];}},
    {key:'I', unit:'A', colour:'#fbbf24', dec:3, get:function(r){return r[2];}},
    {key:'P', unit:'W', colour:'#4ade80', dec:1, get:function(r){return r[1]*r[2];}}
  ];
  var left=54, right=W-52, top=4, bottom=H-10;
  var bandH=(bottom-top)/bands.length;

  function spanOf(f){
    var lo=1e18, hi=-1e18, v, d, pad;
    for(var k=0;k<rows.length;k++){v=f(rows[k]); if(v<lo){lo=v;} if(v>hi){hi=v;}}
    d=hi-lo;
    if(!(d>1e-9)){pad=(hi?Math.abs(hi)*0.001:0.5); lo-=pad; hi+=pad; d=hi-lo;}
    pad=d*0.1;
    return {lo:lo-pad, hi:hi+pad};
  }
  function px(k){return left+(right-left)*(k/(rows.length-1));}

  ctx.font='10px sans-serif';
  for(var b=0;b<bands.length;b++){
    var band=bands[b], y0=top+b*bandH, y1=y0+bandH-8;
    var st=spanOf(band.get);
    var yAt=function(v){return y1-(y1-y0)*((v-st.lo)/(st.hi-st.lo));};

    ctx.strokeStyle='#1e293b'; ctx.lineWidth=1;
    for(var g=0;g<3;g++){
      var y=y0+(y1-y0)*g/2;
      ctx.beginPath(); ctx.moveTo(left,y); ctx.lineTo(right,y); ctx.stroke();
    }
    ctx.fillStyle=band.colour;
    ctx.fillText(band.key, 8, y0+9);
    ctx.fillStyle='#94a3b8';
    ctx.fillText(st.hi.toFixed(band.dec)+' '+band.unit, 8, y0+21);
    if(b===bands.length-1){ctx.fillText(st.lo.toFixed(band.dec)+' '+band.unit, 8, y1);}
    else {ctx.fillText(st.lo.toFixed(band.dec), 8, y1);}

    ctx.strokeStyle=band.colour; ctx.lineWidth=1.6;
    ctx.beginPath();
    for(i=0;i<rows.length;i++){
      var x=px(i), yv=yAt(band.get(rows[i]));
      if(i){ctx.lineTo(x,yv);}else{ctx.moveTo(x,yv);}
    }
    ctx.stroke();
  }

  var sv=spanOf(bands[0].get), si=spanOf(bands[1].get), sp=spanOf(bands[2].get);
  _tdkQ('tdkTrendInfo').textContent=
    rows.length+' pts \u00B7 '+_tdkDur(rows[0][0]*1000)+
    ' \u00B7 V '+sv.lo.toFixed(1)+'-'+sv.hi.toFixed(1)+
    ' \u00B7 A '+si.lo.toFixed(2)+'-'+si.hi.toFixed(2)+
    ' \u00B7 W '+sp.lo.toFixed(0)+'-'+sp.hi.toFixed(0);
  tdkRenderPrev(rows);
}

/* Last 8 polls, newest first: what the CSV rows carry. The endpoint sends the
   sample AGE in seconds, so the clock time is now - age. */
function tdkRenderPrev(rows){
  var tb=_tdkQ('tdkPrevBody');
  if(!tb){return;}
  var out='',n=Math.min(8,rows.length);
  for(var i=rows.length-1;i>=rows.length-n;i--){
    var t=(rows[i][0]!==undefined)?new Date(Date.now()-rows[i][0]*1000).toLocaleTimeString():'--';
    out+='<tr><td>'+_tdkEsc(t)+'</td><td>'+Number(rows[i][1]).toFixed(2)+'</td><td>'+Number(rows[i][2]).toFixed(3)+'</td></tr>';
  }
  tb.innerHTML=out||'<tr><td colspan="3" class="tdk-note">no samples yet.</td></tr>';
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
    _tdkQ('tdkCsvEvery').value=d.csv_every||1;
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
        '&csv_every='+encodeURIComponent(_tdkQ('tdkCsvEvery').value)+
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
    tdkRenderCatSel();
  }).catch(function(){});
}

function tdkCatPick(){
  var sel=_tdkQ('tdkCatSel');
  if(!sel||!sel.value){return {cmd:'',usage:''};}
  var p=sel.value.split('|');
  return {cmd:p[0]||'',usage:p[1]||p[0]||''};
}
function tdkCatLoad(){
  var p=tdkCatPick();
  if(!p.cmd){return;}
  tdkUse(p.usage||p.cmd);
}
/* The two lines under the menu: what we sent, what came back. `null` leaves a
   line untouched (the log poll only knows the RX side). */
function _tdkLines(tx,rx){
  var a=_tdkQ('tdkCatSentLine'), b=_tdkQ('tdkCatReplyLine');
  if(a&&tx!==null&&tx!==undefined){a.textContent='TX> '+tx;}
  if(b&&rx!==null&&rx!==undefined){b.textContent='RX  '+rx;}
}
function tdkCatSend(){
  var p=tdkCatPick(), msg=_tdkQ('tdkCatSendMsg');
  if(!p.cmd){return;}
  if(p.cmd.charAt(p.cmd.length-1)!=='?'){
    /* A setpoint mnemonic alone is not a command (PV needs a value): load it
       into the console instead of firing something at the supply. */
    tdkUse(p.usage||p.cmd);
    if(msg){msg.textContent=_tdkT('tdkCatNeedsValue','Needs a value: edit it in the Console and press Send there.');}
    return;
  }
  _tdkLines(p.cmd,'waiting\u2026');
  tdkQuick(p.cmd);
  if(msg){msg.textContent=_tdkT('tdkCatSent','Sent \u2014 the reply appears below.');}
}
function tdkRenderCatSel(){
  var sel=_tdkQ('tdkCatSel');
  if(!sel||sel.options.length){return;}      /* built once */
  var out='';
  for(var i=0;i<_tdkCat.length;i++){
    var c=_tdkCat[i];
    out+='<option value="'+_tdkEsc(c.cmd)+'|'+_tdkEsc(c.usage)+'">'+
         _tdkEsc(c.cmd)+' \u2014 '+_tdkEsc(c.desc)+'</option>';
  }
  sel.innerHTML=out;
}

function tdkRenderCatalog(){
  var q=(_tdkQ('tdkSearch').value||'').toLowerCase();
  var tb=_tdkQ('tdkCat'),out='';
  var rowTitle=_tdkEsc(_tdkT('tdkTipCatRow','Load this command into the console'));
  for(var i=0;i<_tdkCat.length;i++){
    var c=_tdkCat[i];
    var hay=(c.cmd+' '+c.group+' '+c.desc+' '+c.usage+' '+(c.notes||'')).toLowerCase();
    if(q&&hay.indexOf(q)<0){continue;}
    /* The row IS the action: no per-row button (and no sideways scroll). */
    out+='<tr onclick="tdkUse(\''+_tdkEsc(c.usage)+'\')" title="'+rowTitle+'" data-i18n-title="tdkTipCatRow">'+
         '<td><b>'+_tdkEsc(c.cmd)+'</b></td><td>'+_tdkEsc(c.group)+'</td><td><code>'+_tdkEsc(c.usage)+
         '</code></td><td>'+_tdkEsc(c.desc)+'</td><td><code>'+_tdkEsc(c.reply)+'</code></td><td class="tdk-note">'+
         _tdkEsc(c.notes||'')+'</td></tr>';
  }
  tb.innerHTML=out||'<tr><td colspan="6" class="tdk-note">no matches</td></tr>';
}

function tdkUse(u){
  _tdkQ('tdkCmd').value=u;
  tdkShowTab('console');
  try { _tdkQ('tdkCmd').focus(); } catch (e) {}
}

tdkStatus();tdkLog();tdkLoadCfg();tdkLoadCatalog();tdkLoadTrend();
setInterval(tdkStatus,5000);
setInterval(tdkLog,3000);
/* The chart follows the poll cadence (5..60 s), not a fixed 30 s: with a 10 s
   poll the trend used to lag three samples behind. One re-arm only. */
function _tdkArmTrend(ms){
  var want=Math.max(60000,Math.min(300000,ms||60000));   /* never below the 1 min floor */
  if(_tdkTrendTimer&&_tdkTrendTimer.ms===want){return;}
  if(_tdkTrendTimer){clearInterval(_tdkTrendTimer.id);}
  _tdkTrendTimer={id:setInterval(tdkLoadTrend,want),ms:want};
}
_tdkArmTrend(30000);
document.addEventListener('visibilitychange',function(){
  if(!document.hidden){tdkStatus();tdkLog();tdkLoadTrend();}
});

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
