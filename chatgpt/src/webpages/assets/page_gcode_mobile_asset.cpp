#include "page_gcode_mobile_asset.h"

// Mobile G-code wizard — five screens, one action per screen, no page scroll.
//
//   1 Send     — SD card list (or the phone picker); the job is either streamed
//                straight from the SD or uploaded to the PSRAM workspace.
//   2 Preview  — 2D toolpath of the selected job (canvas), drawn from points
//                parsed here (phone file) or streamed from the controller (SD).
//   3 Frame    — outline the job area (same command sequence as the PC page).
//   4 Play     — start/pause/resume/stop the job (PSRAM workspace or SD path).
//   5 Jog      — axis jog + the essential GRBL buttons (home/unlock/zero/hold/
//                resume/cancel/reset) without leaving the page.
//
// Every screen is reachable at any time: the footer arrows and a horizontal
// swipe always move between the screens (they never wait for a job), so the
// phone is never locked on step 1. #gm is a fixed 100dvh column and the active
// screen takes the leftover height; only the SD list and the jog screen scroll
// internally, the page itself never does.
//
// The PC page (/grbl-controller) is a separate asset/renderer/route and is
// untouched by design.
const char PAGE_GCODE_MOBILE[] PROGMEM = R"GMOBILE(
<style>
  html, body { height: 100%; margin: 0; overflow: hidden; background: #0b1220; }
  body { color: #e6f1ff; font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  #gm {
    position: fixed; inset: 0; display: flex; flex-direction: column;
    gap: 8px; padding: 10px 12px calc(10px + env(safe-area-inset-bottom));
    box-sizing: border-box; height: 100dvh; overflow: hidden;
  }
  #gm header { flex: 0 0 auto; display: flex; align-items: center; gap: 8px; }
  #gm .steps { display: flex; gap: 4px; }
  #gm .steps i { width: 14px; height: 4px; border-radius: 2px; background: #24344d; display: block; }
  #gm .steps i.on { background: #3b82f6; }
  #gm .steps i.done { background: #1d4ed8; }
  #gm h1 { font-size: 15px; margin: 0; font-weight: 700; letter-spacing: .2px; }
  #gm .badge {
    margin-left: auto; font-size: 11px; padding: 3px 8px; border-radius: 999px;
    border: 1px solid #334155; color: #94a3b8; white-space: nowrap;
  }
  #gm .badge.ok { border-color: #10b981; color: #6ee7b7; }
  #gm .badge.warn { border-color: #f59e0b; color: #fcd34d; }

  #gm .screen { flex: 1 1 auto; min-height: 0; display: none; flex-direction: column; gap: 6px; overflow: hidden; }
  #gm .screen.on { display: flex; }
  #gm #gmS5 { overflow-y: auto; -webkit-overflow-scrolling: touch; }

  /* step 1 — source tabs + SD list */
  #gm .tabs { flex: 0 0 auto; display: flex; gap: 6px; }
  #gm .tab {
    flex: 1 1 0; height: 34px; border-radius: 10px; border: 1px solid #334155;
    background: #0f172a; color: #94a3b8; font-size: 13px; font-weight: 700;
  }
  #gm .tab.on { border-color: #3b82f6; background: #1d2b47; color: #dbeafe; }
  #gm .pane { flex: 1 1 auto; min-height: 0; display: none; flex-direction: column; gap: 6px; }
  #gm .pane.on { display: flex; }
  #gm .rowhdr { flex: 0 0 auto; display: flex; align-items: center; gap: 8px; font-size: 11px; color: #94a3b8; }
  #gm .rowhdr .dir { flex: 1 1 auto; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  #gm .mini {
    flex: 0 0 auto; width: 32px; height: 32px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #cbd5e1; font-size: 15px; line-height: 1;
  }
  #gm .list {
    flex: 1 1 auto; min-height: 0; overflow-y: auto; -webkit-overflow-scrolling: touch;
    border: 1px solid #1e293b; border-radius: 12px; background: #0f172a;
  }
  #gm .row {
    display: flex; align-items: center; gap: 8px; padding: 11px 12px;
    border-bottom: 1px solid #16213a; font-size: 13px; background: transparent;
    width: 100%; text-align: left; color: #e6f1ff; border-left: 0; border-right: 0; border-top: 0;
  }
  #gm .row:last-child { border-bottom: 0; }
  #gm .row.sel { background: #1d2b47; }
  #gm .row .n { flex: 1 1 auto; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  #gm .row .s { flex: 0 0 auto; font-size: 11px; color: #94a3b8; }
  #gm .note { padding: 14px 12px; font-size: 12px; color: #94a3b8; }

  #gm .card {
    flex: 1 1 auto; min-height: 0; width: 100%; display: flex; flex-direction: column;
    align-items: center; justify-content: center; gap: 8px; box-sizing: border-box;
    border: 2px dashed #3b82f6; border-radius: 14px; background: #0f172a;
    color: #dbeafe; font-size: 15px; padding: 16px; text-align: center;
  }
  #gm button.card { cursor: pointer; }
  #gm button.card:active { background: #14213a; }
  #gm .card.solid { border-style: solid; cursor: default; }
  #gm .card .ico { font-size: 38px; line-height: 1; }
  #gm .card .nm { font-weight: 700; word-break: break-all; max-height: 3.2em; overflow: hidden; }
  #gm .card .sz { font-size: 12px; color: #94a3b8; }
  #gm .card .big { font-size: 24px; font-weight: 800; letter-spacing: .5px; }
  #gm .card .row { font-size: 13px; color: #cbd5e1; }
  #gm .card .kv { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12px; color: #93c5fd; }

  /* step 2 — preview canvas */
  #gm #gmCanvas {
    flex: 1 1 auto; min-height: 0; width: 100%; display: block;
    border: 1px solid #1e293b; border-radius: 12px; background: #0f172a;
  }

  /* step 5 — jog + machine buttons */
  #gm .state { flex: 0 0 auto; display: flex; align-items: center; gap: 8px; font-size: 12px; color: #94a3b8; }
  #gm .state b { color: #e6f1ff; font-size: 13px; }
  #gm .state .pos { margin-left: auto; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 11px; color: #93c5fd; }
  #gm .chips { flex: 0 0 auto; display: flex; gap: 6px; }
  #gm .chip {
    flex: 1 1 0; height: 32px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #94a3b8; font-size: 12px; font-weight: 700;
  }
  #gm .chip.on { border-color: #3b82f6; background: #1d2b47; color: #dbeafe; }
  #gm .jog { flex: 0 0 auto; display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 6px; }
  #gm .jog button {
    height: 56px; border-radius: 12px; border: 1px solid #334155; background: #0f172a;
    color: #dbeafe; font-size: 15px; font-weight: 700;
  }
  #gm .jog button:active { background: #1d2b47; }
  #gm .jog button:disabled { opacity: .3; }
  #gm .acts3 { flex: 0 0 auto; display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 6px; }
  #gm .acts3 button {
    height: 40px; border-radius: 10px; border: 1px solid #334155; background: #0f172a;
    color: #cbd5e1; font-size: 11px; font-weight: 700; letter-spacing: .3px;
  }
  #gm .acts3 button:active { background: #1d2b47; }
  #gm .acts3 button.arm { border-color: #dc2626; background: #7f1d1d; color: #fff; }
  #gm .acts3 button.wide { grid-column: 1 / span 3; height: 34px; }

  #gm .msg { flex: 0 0 auto; font-size: 12px; line-height: 1.3; color: #94a3b8; min-height: 2.6em; }
  #gm .msg.err { color: #fca5a5; }
  #gm .msg.ok { color: #6ee7b7; }
  #gm .bar { flex: 0 0 auto; height: 6px; border-radius: 3px; background: #1e293b; overflow: hidden; display: none; }
  #gm .bar.on { display: block; }
  #gm .bar span { display: block; height: 100%; width: 0%; background: #3b82f6; transition: width .15s linear; }
  #gm .bar.indet span { width: 35%; animation: gmslide 1.1s linear infinite; }
  @keyframes gmslide { from { margin-left: -35%; } to { margin-left: 100%; } }

  #gm .acts { flex: 0 0 auto; display: flex; gap: 8px; }
  #gm .primary {
    flex: 1 1 auto; height: 52px; border: 0; border-radius: 12px;
    background: #2563eb; color: #fff; font-size: 16px; font-weight: 700;
  }
  #gm .primary:disabled { background: #1e293b; color: #64748b; }
  #gm .primary.off { display: none; }

  /* step 4 — machine buttons (always visible, enabled by state) */
  #gm .playrow { flex: 0 0 auto; display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 6px; }
  #gm .playrow button {
    height: 46px; border-radius: 11px; border: 1px solid #334155; background: #0f172a;
    color: #dbeafe; font-size: 12px; font-weight: 700; letter-spacing: .3px;
  }
  #gm .playrow button:active { background: #1d2b47; }
  #gm .playrow button:disabled { opacity: .45; }
  #gm .playrow button.stop { border-color: #dc2626; background: #7f1d1d; color: #fff; }
  #gm .playrow button.stop:disabled { background: #0f172a; color: #64748b; border-color: #334155; }

  #gm .nav { flex: 0 0 auto; display: flex; align-items: center; gap: 10px; }
  #gm .nav .no { font-size: 12px; color: #94a3b8; }
  #gm .nav .arrow {
    width: 46px; height: 46px; border-radius: 50%; border: 1px solid #3b82f6;
    background: #0f172a; color: #e6f1ff; font-size: 20px;
  }
  #gm .nav .arrow:disabled { opacity: .3; border-color: #334155; color: #64748b; }
  #gm .nav .back { margin-left: 0; }
  #gm .nav .next { margin-left: auto; }

  @media (max-height: 620px) {
    #gm h1 { font-size: 13px; }
    #gm .card .ico { font-size: 26px; }
    #gm .jog button { height: 44px; font-size: 13px; }
    #gm .acts3 button { height: 34px; }
    #gm .primary, #gm .danger { height: 44px; font-size: 14px; }
    #gm .nav .arrow { width: 40px; height: 40px; }
    #gm .msg { min-height: 2.2em; font-size: 11px; }
  }
</style>

<div id="gm">
  <header>
    <div class="steps"><i class="on"></i><i></i><i></i><i></i><i></i></div>
    <h1 id="gmTitle" data-i18n="gmTitle1">Send G-code</h1>
    <span class="badge" id="gmLink">--</span>
  </header>

  <!-- ── Step 1: send ─────────────────────────────────────────────── -->
  <section class="screen on" id="gmS1">
    <div class="tabs">
      <button type="button" class="tab on" id="gmTabSd" data-i18n="gmTabSd">SD card</button>
      <button type="button" class="tab" id="gmTabPhone" data-i18n="gmTabPhone">Phone</button>
    </div>

    <div class="pane on" id="gmPaneSd">
      <div class="rowhdr">
        <span class="dir" id="gmSdDir">/</span>
        <button type="button" class="mini" id="gmSdReload" data-i18n-title="gmSdRefresh" title="Reload the SD list">&#8635;</button>
      </div>
      <div class="list" id="gmSdList"></div>
    </div>

    <div class="pane" id="gmPanePhone">
      <button type="button" class="card" id="gmPick" data-i18n-title="gmPickTip">
        <span class="ico">&#128196;</span>
        <span class="nm" id="gmName" data-i18n="gmPick">Choose a file</span>
        <span class="sz" id="gmSize">.gcode &middot; .nc &middot; .txt</span>
      </button>
      <input type="file" id="gmFile" accept=".gcode,.nc,.ngc,.txt,text/plain" hidden>
    </div>
  </section>

  <!-- ── Step 2: preview ──────────────────────────────────────────── -->
  <section class="screen" id="gmS2">
    <canvas id="gmCanvas"></canvas>
    <div class="rowhdr">
      <span class="dir" id="gmPrevInfo">--</span>
      <button type="button" class="mini" id="gmPrevLoad" data-i18n-title="gmPreviewLoad" title="Load the preview">&#8635;</button>
    </div>
  </section>

  <!-- ── Step 3: frame ────────────────────────────────────────────── -->
  <section class="screen" id="gmS3">
    <div class="card solid">
      <span class="ico">&#128207;</span>
      <span class="nm" id="gmFrameJob">--</span>
      <span class="kv" id="gmFrameArea">--</span>
      <span class="row" id="gmFrameHint" data-i18n="gmFrameHint">Outlines the job area with the spindle at 5%.</span>
    </div>
  </section>

  <!-- ── Step 4: play ─────────────────────────────────────────────── -->
  <section class="screen" id="gmS4">
    <div class="card solid">
      <span class="ico">&#9654;&#65039;</span>
      <span class="nm" id="gmPlayJob">--</span>
      <span class="big" id="gmPlayState">--</span>
      <span class="kv" id="gmPlayProgress"></span>
    </div>
    <div class="playrow">
      <button type="button" id="gmPause" data-i18n="gmPause">PAUSE</button>
      <button type="button" id="gmResume" data-i18n="gmResume">RESUME</button>
      <button type="button" id="gmStop" class="stop" data-i18n="gmStop">STOP</button>
    </div>
  </section>

  <!-- ── Step 5: jog + machine buttons ────────────────────────────── -->
  <section class="screen" id="gmS5">
    <div class="state">
      <span data-i18n="gmState">State</span>
      <b id="gmJogState">--</b>
      <span class="pos" id="gmJogPos"></span>
    </div>
    <div class="chips" id="gmStepChips">
      <button type="button" class="chip" data-step="0.1">0.1</button>
      <button type="button" class="chip on" data-step="1">1</button>
      <button type="button" class="chip" data-step="10">10</button>
      <button type="button" class="chip" data-step="50">50</button>
    </div>
    <div class="jog" id="gmJogPad">
      <button type="button" data-jog="X-">X&#8722;</button>
      <button type="button" data-jog="Y+">Y+</button>
      <button type="button" data-jog="Z+">Z+</button>
      <button type="button" data-jog="X+">X+</button>
      <button type="button" data-jog="Y-">Y&#8722;</button>
      <button type="button" data-jog="Z-">Z&#8722;</button>
    </div>
    <div class="acts3" id="gmActions">
      <button type="button" class="actbtn" data-act="home" data-i18n="gmBtnHome">HOME</button>
      <button type="button" class="actbtn" data-act="unlock" data-i18n="gmBtnUnlock">UNLOCK</button>
      <button type="button" class="actbtn" data-act="zero" data-i18n="gmBtnZero">ZERO</button>
      <button type="button" class="actbtn" data-act="hold" data-i18n="gmBtnHold">HOLD</button>
      <button type="button" class="actbtn" data-act="resume" data-i18n="gmBtnResume">RESUME</button>
      <button type="button" class="actbtn" data-act="cancel" data-i18n="gmBtnCancel">CANCEL</button>
      <button type="button" class="actbtn wide" data-act="reset" data-i18n="gmBtnReset">RESET</button>
    </div>
  </section>

  <div class="msg" id="gmMsg" data-i18n="gmStep1Hint">Pick a file from the SD card or from this phone.</div>
  <div class="bar" id="gmBar"><span id="gmFill"></span></div>

  <div class="acts">
    <button type="button" class="primary" id="gmPrimary" disabled data-i18n="gmSend">SEND</button>
  </div>

  <div class="nav">
    <button type="button" class="arrow back" id="gmBack" disabled title="Back">&#8592;</button>
    <span class="no" id="gmNo">1 / 5</span>
    <button type="button" class="arrow next" id="gmNext" data-i18n-title="gmNextTip">&#8594;</button>
  </div>
</div>

<script>
(function () {
  'use strict';
  var MAX_BYTES = 4 * 1024 * 1024;      // controller PSRAM workspace cap (phone uploads)
  var STORE_KEY = 'gm.sid';
  var STEPS = 5;
  var STEP_SEND = 1, STEP_PREVIEW = 2, STEP_FRAME = 3, STEP_PLAY = 4, STEP_JOG = 5;
  var FRAME_POWER_PCT = 5;
  var FRAME_FEED = 1200;
  var JOG_FEED = 1000;                  // XY jog feed (mm/min)
  var JOG_FEED_Z = 300;                 // Z jog feed, deliberately slower
  var MEASURE_TIMEOUT_MS = 45000;       // frame-bbox on a big SD file takes ~20 s
  var PREVIEW_CAP = 40000;              // plot points kept (decimated above this)
  var PREVIEW_MAX_MS = 25000;           // stop streaming an SD file after this

  var el = {
    title: document.getElementById('gmTitle'),
    steps: document.querySelectorAll('#gm .steps i'),
    screens: [document.getElementById('gmS1'), document.getElementById('gmS2'),
              document.getElementById('gmS3'), document.getElementById('gmS4'),
              document.getElementById('gmS5')],
    tabSd: document.getElementById('gmTabSd'),
    tabPhone: document.getElementById('gmTabPhone'),
    paneSd: document.getElementById('gmPaneSd'),
    panePhone: document.getElementById('gmPanePhone'),
    sdDir: document.getElementById('gmSdDir'),
    sdReload: document.getElementById('gmSdReload'),
    sdList: document.getElementById('gmSdList'),
    canvas: document.getElementById('gmCanvas'),
    prevInfo: document.getElementById('gmPrevInfo'),
    prevLoad: document.getElementById('gmPrevLoad'),
    pick: document.getElementById('gmPick'),
    file: document.getElementById('gmFile'),
    name: document.getElementById('gmName'),
    size: document.getElementById('gmSize'),
    frameJob: document.getElementById('gmFrameJob'),
    frameArea: document.getElementById('gmFrameArea'),
    frameHint: document.getElementById('gmFrameHint'),
    playJob: document.getElementById('gmPlayJob'),
    playState: document.getElementById('gmPlayState'),
    playProgress: document.getElementById('gmPlayProgress'),
    jogState: document.getElementById('gmJogState'),
    jogPos: document.getElementById('gmJogPos'),
    jogPad: document.getElementById('gmJogPad'),
    stepChips: document.getElementById('gmStepChips'),
    actions: document.getElementById('gmActions'),
    msg: document.getElementById('gmMsg'),
    bar: document.getElementById('gmBar'),
    fill: document.getElementById('gmFill'),
    acts: document.querySelector('#gm .acts'),
    primary: document.getElementById('gmPrimary'),
    pause: document.getElementById('gmPause'),
    resume: document.getElementById('gmResume'),
    stop: document.getElementById('gmStop'),
    back: document.getElementById('gmBack'),
    next: document.getElementById('gmNext'),
    no: document.getElementById('gmNo'),
    link: document.getElementById('gmLink')
  };

  var st = {
    sid: '', token: '', canWrite: true,
    step: 1, tab: 'sd',
    phoneFile: null,             // File chosen from the phone
    sdRoot: '', sdDir: '', sdFiles: [], sdPath: '', sdLoading: false, sdError: 0,
    job: null,                   // { src:'sd'|'psram', name, bytes, path }
    bbox: null, measuring: false, measureFor: '',
    points: null, pointsFor: '', previewLoading: false, previewPartial: false, previewPoints: 0,
    busy: false, running: false, paused: false, axisCount: 0, mpos: null,
    jogStep: 1,
    armedKey: '', armedUntil: 0,
    pollTimer: 0
  };

  // ── i18n ────────────────────────────────────────────────────────────────
  function t(k, fb) {
    try {
      if (typeof window._t === 'function') {
        var v = window._t(k, fb);
        if (v !== undefined && v !== null && v !== '' && v !== k) return v;
      }
      var raw = localStorage.getItem('ui_lang_cache');
      if (raw) {
        var p = JSON.parse(raw);
        if (p && p[k] !== undefined) return p[k];
      }
    } catch (e) {}
    return fb !== undefined ? fb : k;
  }
  function tf(k, fb, vars) {
    var s = String(t(k, fb));
    for (var key in (vars || {})) {
      if (Object.prototype.hasOwnProperty.call(vars, key)) s = s.replace('{' + key + '}', String(vars[key]));
    }
    return s;
  }

  // ── helpers ────────────────────────────────────────────────────────────
  function sessionId() {
    if (st.sid) return st.sid;
    var v = '';
    try { v = sessionStorage.getItem(STORE_KEY) || ''; } catch (e) {}
    if (!v) {
      v = 'm-' + String(Date.now()) + '-' + String(Math.floor(Math.random() * 1000000));
      try { sessionStorage.setItem(STORE_KEY, v); } catch (e2) {}
    }
    st.sid = v;
    return v;
  }

  function cleanName(n) {
    var s = String(n || '').replace(/^.*[\\\/]/, '').replace(/[^\w.\-]/g, '_');
    if (!s) s = 'job.nc';
    if (!/\.[a-z0-9]+$/i.test(s)) s += '.nc';
    return s.slice(0, 48);
  }

  function fmtBytes(b) {
    b = Number(b) || 0;
    if (b < 1024) return b + ' B';
    if (b < 1024 * 1024) return (b / 1024).toFixed(b < 10240 ? 1 : 0) + ' kB';
    return (b / (1024 * 1024)).toFixed(1) + ' MB';
  }

  function setMsg(text, kind) {
    el.msg.textContent = text || '';
    el.msg.className = 'msg' + (kind === 'err' ? ' err' : kind === 'ok' ? ' ok' : '');
  }

  // code -> [i18n key, English fallback] so the message is readable even
  // before (or without) the language dictionary.
  var ERROR_TEXT = {
    file_too_large: ['gmErrTooLarge', 'File is larger than 4 MB.'],
    upload_empty: ['gmErrEmpty', 'The file is empty.'],
    psram_store_failed: ['gmErrStore', 'The controller could not store the file.'],
    vfs_budget_exceeded: ['gmErrBudget', 'No space left on the controller.'],
    alloc_failed: ['gmErrAlloc', 'Not enough memory on the controller.'],
    no_control: ['gmErrNoControl', 'Another client holds control. Close the PC page and retry.'],
    network: ['gmErrNetwork', 'Network error. Try again.'],
    no_file: ['gmFrameNoJob', 'Pick a job first (step 1).'],
    empty_file: ['gmPlayNoJob', 'No job selected.'],
    not_idle: ['gmFrameNotIdle', 'Machine is not idle.'],
    machine_not_idle: ['gmFrameNotIdle', 'Machine is not idle.'],
    stream_failed: ['gmPlayFailed', 'Could not start the job.'],
    psram_stream_failed: ['gmPlayFailed', 'Could not start the job.'],
    sd_list_failed: ['gmSdError', 'Could not read the SD card.'],
    sd_measure_failed: ['gmMeasureFail', 'Could not measure the area.'],
    preview_failed: ['gmPreviewFail', 'Could not load the preview.'],
    send_failed: ['gmErrSend', 'The controller refused the command.'],
    no_z_axis: ['gmErrNoZAxis', 'This machine has no Z axis.']
  };
  function errorText(code) {
    var e = ERROR_TEXT[code];
    if (e) return t(e[0], e[1]);
    return tf('gmErrGeneric', 'Error: {code}', { code: code });
  }

  function progress(pct) {
    var on = pct > 0 && pct < 100;
    el.bar.className = 'bar' + (on ? ' on' : '');
    el.fill.style.width = Math.max(0, Math.min(100, pct)) + '%';
  }
  function progressBusy(on) {
    el.bar.className = 'bar' + (on ? ' on indet' : '');
    el.fill.style.width = '';
  }

  function linkBadge(on, text) {
    el.link.textContent = text;
    el.link.className = 'badge' + (on ? ' ok' : ' warn');
  }

  // ── HTTP ───────────────────────────────────────────────────────────────
  function apiGet(url) {
    return fetch(url, { cache: 'no-store' }).then(function (r) {
      return r.json().then(function (d) { return { status: r.status, body: d || {} }; })
                     .catch(function () { return { status: r.status, body: {} }; });
    });
  }

  function post(url, payload) {
    var body = payload || {};
    body.sid = sessionId();
    if (st.token) body.token = st.token;
    var enc = [];
    for (var k in body) {
      if (Object.prototype.hasOwnProperty.call(body, k)) {
        enc.push(encodeURIComponent(k) + '=' + encodeURIComponent(body[k]));
      }
    }
    return fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8' },
      body: enc.join('&')
    }).then(function (r) {
      return r.json().then(function (d) { return { status: r.status, body: d || {} }; },
                         function () { return { status: r.status, body: {} }; });
    });
  }

  // One GRBL line through /api/grbl/send (same path the PC page uses).
  function sendCmd(cmd) {
    return post('/api/grbl/send', { slot: 0, cmd: cmd }).then(function (res) {
      return !!(res.body && res.body.success === true);
    }).catch(function () { return false; });
  }

  function hasZ() {
    return !(st.axisCount > 0 && st.axisCount < 3);
  }

  // ── G-code → points (bounded, decimated as it grows) ───────────────────
  // Points are stored flat: [x0,y0,x1,y1,...]. Past PREVIEW_CAP the array is
  // halved and the stride doubles, so the whole path stays visible without
  // ever holding more than ~CAP points.
  function PointSink(cap) {
    this.cap = cap || PREVIEW_CAP;
    this.pts = [];
    this.stride = 1;
    this.seen = 0;
    this.curX = 0;
    this.curY = 0;
    this.modal = '';
    this.saw = false;
  }
  PointSink.prototype.add = function (x, y) {
    this.seen++;
    if (this.seen % this.stride) return;
    this.pts.push(x, y);
    if ((this.pts.length / 2) > this.cap) {
      var out = [];
      for (var i = 0; i < this.pts.length; i += 4) { out.push(this.pts[i], this.pts[i + 1]); }
      this.pts = out;
      this.stride *= 2;
    }
  };
  PointSink.prototype.line = function (raw) {
    var line = String(raw || '').replace(/\([^)]*\)/g, ' ').replace(/;.*$/, ' ').trim().toUpperCase();
    if (!line) return;
    // Only motion G words change the motion mode: G90/G21/G17/G4/G10 must not
    // switch off plotting for the modal moves that follow them.
    var g = line.match(/^G\s*0*(\d+)/);
    if (g) {
      var n = parseInt(g[1], 10);
      if (n === 0 || n === 1 || n === 2 || n === 3) this.modal = 'G' + n;
    }
    var mx = line.match(/X\s*(-?\d*\.?\d+)/);
    var my = line.match(/Y\s*(-?\d*\.?\d+)/);
    if (!mx && !my) return;
    if (mx) this.curX = parseFloat(mx[1]);
    if (my) this.curY = parseFloat(my[1]);
    if (this.modal === 'G0' || this.modal === 'G1' || this.modal === 'G2' ||
        this.modal === 'G3' || this.modal === '') {
      this.saw = true;
      this.add(this.curX, this.curY);
    }
  };
  PointSink.prototype.count = function () { return this.pts.length / 2; };

  function pointsFromText(text) {
    var sink = new PointSink(PREVIEW_CAP);
    var lines = String(text || '').split(/\r?\n/);
    for (var i = 0; i < lines.length; i++) sink.line(lines[i]);
    return sink;
  }

  function boundsFromPoints(pts) {
    if (!pts || pts.length < 4) return null;
    var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    for (var i = 0; i < pts.length; i += 2) {
      var x = pts[i], y = pts[i + 1];
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
    if (!isFinite(minX) || !isFinite(minY)) return null;
    return { minX: minX, maxX: maxX, minY: minY, maxY: maxY };
  }

  function areaText(b) {
    var w = (b.maxX - b.minX).toFixed(2);
    var h = (b.maxY - b.minY).toFixed(2);
    return 'X[' + b.minX.toFixed(2) + '..' + b.maxX.toFixed(2) + ']  Y[' +
           b.minY.toFixed(2) + '..' + b.maxY.toFixed(2) + ']  ' + w + ' x ' + h;
  }

  // ── preview canvas ─────────────────────────────────────────────────────
  function drawPreview() {
    var cv = el.canvas;
    if (!cv || typeof document === 'undefined' || !document) return;
    var rect = cv.getBoundingClientRect ? cv.getBoundingClientRect() : { width: 300, height: 300 };
    var cw = Math.max(1, Math.round(rect.width || cv.width || 300));
    var ch = Math.max(1, Math.round(rect.height || cv.height || 300));
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (cv.width !== Math.round(cw * dpr) || cv.height !== Math.round(ch * dpr)) {
      cv.width = Math.round(cw * dpr);
      cv.height = Math.round(ch * dpr);
    }
    var ctx = null;
    try { ctx = cv.getContext ? cv.getContext('2d') : null; } catch (e) { ctx = null; }
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, cw, ch);
    ctx.fillStyle = '#0f172a';
    ctx.fillRect(0, 0, cw, ch);

    var pts = st.points;
    if (!pts || pts.length < 4) return;
    var b = boundsFromPoints(pts);
    if (!b) return;
    var spanX = Math.max(0.001, b.maxX - b.minX);
    var spanY = Math.max(0.001, b.maxY - b.minY);
    var pad = 10;
    var scale = Math.min((cw - pad * 2) / spanX, (ch - pad * 2) / spanY);
    var offX = (cw - spanX * scale) / 2;
    var offY = (ch - spanY * scale) / 2;
    function px(x) { return offX + (x - b.minX) * scale; }
    function py(y) { return ch - offY - (y - b.minY) * scale; }   // Y up

    // frame (job area)
    ctx.strokeStyle = '#1e40af';
    ctx.lineWidth = 1;
    ctx.strokeRect(px(b.minX), py(b.maxY), spanX * scale, spanY * scale);

    ctx.strokeStyle = '#3b82f6';
    ctx.lineWidth = 1;
    ctx.lineJoin = 'round';
    ctx.beginPath();
    ctx.moveTo(px(pts[0]), py(pts[1]));
    for (var i = 2; i < pts.length; i += 2) ctx.lineTo(px(pts[i]), py(pts[i + 1]));
    ctx.stroke();

    // origin cross
    ctx.strokeStyle = '#64748b';
    ctx.beginPath();
    ctx.moveTo(px(0) - 5, py(0)); ctx.lineTo(px(0) + 5, py(0));
    ctx.moveTo(px(0), py(0) - 5); ctx.lineTo(px(0), py(0) + 5);
    ctx.stroke();
  }

  function previewInfoText() {
    if (st.previewLoading) return t('gmPreviewLoading', 'Loading the preview\u2026');
    if (!st.points || st.points.length < 4) {
      return st.job ? t('gmPreviewEmpty', 'No preview yet \u2014 tap \u21bb to load it.')
                    : t('gmFrameNoJob', 'Pick a job first (step 1).');
    }
    var txt = tf('gmPreviewPoints', '{n} points', { n: String(st.previewPoints || (st.points.length / 2)) });
    if (st.previewPartial) txt += ' \u00b7 ' + t('gmPreviewPartial', 'partial');
    return txt;
  }

  function jobKey(j) {
    if (!j) return '';
    return String(j.src || '') + ':' + String(j.path || j.name || '');
  }

  // Phone file: the text is already in the browser, so points cost nothing.
  function readPhoneFile(file) {
    return new Promise(function (resolve) {
      try {
        var fr = new FileReader();
        fr.onload = function () {
          var sink = pointsFromText(fr.result);
          resolve({ sink: sink, bbox: boundsFromPoints(sink.pts) });
        };
        fr.onerror = function () { resolve(null); };
        fr.readAsText(file);
      } catch (e) { resolve(null); }
    });
  }

  // SD file: stream the text from the controller and parse line by line. Big
  // files are cut at PREVIEW_MAX_MS / PREVIEW_CAP and reported as partial.
  function loadPreviewFromSd() {
    if (!st.job || st.job.src !== 'sd' || !st.job.path) return false;
    if (st.previewLoading) return false;
    var path = st.job.path;
    var key = jobKey(st.job);
    st.previewLoading = true;
    st.previewPartial = false;
    progressBusy(true);
    setMsg(t('gmPreviewLoading', 'Loading the preview\u2026'), '');
    render();

    var sink = new PointSink(PREVIEW_CAP);
    var started = Date.now();
    var leftover = '';
    function feed(text, last) {
      var chunk = leftover + text;
      var parts = chunk.split(/\r?\n/);
      leftover = last ? '' : parts.pop();
      for (var i = 0; i < parts.length; i++) sink.line(parts[i]);
      if (last && leftover) sink.line(leftover);
    }
    function finish(partial) {
      if (st.job && jobKey(st.job) !== key) { st.previewLoading = false; progressBusy(false); render(); return; }
      st.points = sink.pts;
      st.pointsFor = key;
      st.previewPoints = sink.count();
      st.previewPartial = !!partial;
      st.previewLoading = false;
      progressBusy(false);
      if (!st.bbox) st.bbox = boundsFromPoints(sink.pts);
      if (!sink.saw) {
        st.points = null;
        setMsg(errorText('preview_failed'), 'err');
      } else if (partial) {
        setMsg(t('gmPreviewPartial', 'Partial preview (large file).'), '');
      } else {
        setMsg(previewInfoText(), 'ok');
      }
      render();
    }

    var url = '/api/grbl/read-gcode-text?path=' + encodeURIComponent(path) +
              '&sid=' + encodeURIComponent(sessionId());
    fetch(url, { cache: 'no-store' }).then(function (resp) {
      if (!resp) throw new Error('network');
      if (!resp.ok) throw new Error('http_' + String(resp.status || 0));
      var canStream = resp.body && typeof resp.body.getReader === 'function' &&
                      typeof TextDecoder === 'function';
      if (!canStream) {
        return resp.text().then(function (txt) { feed(txt, true); finish(false); });
      }
      var reader = resp.body.getReader();
      var decoder = new TextDecoder('utf-8');
      var stopped = false;
      function pump() {
        return reader.read().then(function (part) {
          if (!part || part.done) { feed(decoder.decode(), true); finish(stopped); return true; }
          feed(decoder.decode(part.value, { stream: true }), false);
          if (sink.count() >= PREVIEW_CAP || (Date.now() - started) > PREVIEW_MAX_MS) {
            stopped = true;
            try { reader.cancel(); } catch (e) {}
            feed(decoder.decode(), true);
            finish(true);
            return true;
          }
          return pump();
        });
      }
      return pump();
    }).catch(function () {
      st.previewLoading = false;
      progressBusy(false);
      st.previewPartial = false;
      setMsg(errorText('preview_failed'), 'err');
      render();
    });
    return true;
  }

  // ── step 1a: SD card list ──────────────────────────────────────────────
  function sdParent(p) {
    var s = String(p || '').replace(/\/+$/, '');
    var i = s.lastIndexOf('/');
    if (i <= 0) return st.sdRoot || '';
    return s.slice(0, i);
  }

  function listSd(dir) {
    st.sdLoading = true;
    st.sdError = 0;
    render();
    var url = '/api/grbl/list-gcode?dir=' + encodeURIComponent(dir || st.sdDir || '') +
              '&sid=' + encodeURIComponent(sessionId());
    return apiGet(url).then(function (res) {
      st.sdLoading = false;
      var d = res.body || {};
      if (res.status !== 200 || !d || d.success !== true || !Array.isArray(d.files)) {
        st.sdError = 1;
        st.sdFiles = [];
        render();
        setMsg(errorText('sd_list_failed'), 'err');
        return false;
      }
      if (d.root) st.sdRoot = String(d.root);
      st.sdDir = String(d.dir || dir || '');
      st.sdFiles = d.files;
      if (st.sdPath) {
        var still = false;
        for (var i = 0; i < d.files.length; i++) {
          if (String(d.files[i].path || '') === st.sdPath) { still = true; break; }
        }
        if (!still) st.sdPath = '';
      }
      // A reload that works must clear the error left by an earlier attempt.
      if (el.msg && el.msg.textContent === errorText('sd_list_failed')) defaultMsg(st.step);
      render();
      return true;
    }).catch(function () {
      st.sdLoading = false;
      st.sdError = 1;
      st.sdFiles = [];
      render();
      setMsg(errorText('sd_list_failed'), 'err');
      return false;
    });
  }

  function selectSd(entry) {
    if (!entry || entry.is_dir) return false;
    st.sdPath = String(entry.path || '');
    st.job = { src: 'sd', path: st.sdPath, name: String(entry.name || st.sdPath),
               bytes: Number(entry.size) || 0 };
    st.bbox = null;
    st.points = null;
    st.pointsFor = '';
    st.previewPoints = 0;
    st.previewPartial = false;
    render();
    setMsg(tf('gmReadyToUse', 'Ready: {name} ({size})', { name: st.job.name, size: fmtBytes(st.job.bytes) }), 'ok');
    return true;
  }

  // Server-side XY bounds of an SD file (frame-bbox). Big files take ~20 s, so
  // the call is fire-and-forget with an indeterminate bar and a hard timeout.
  function measureSd() {
    if (!st.job || st.job.src !== 'sd' || !st.job.path) return false;
    var path = st.job.path;
    st.measureFor = path;
    st.measuring = true;
    progressBusy(true);
    setMsg(t('gmMeasuring', 'Measuring the job area on the controller\u2026'), '');
    render();
    var done = false;
    function finish(b) {
      if (done || !st.job || st.job.path !== path) return;
      done = true;
      st.measuring = false;
      progressBusy(false);
      if (b) st.bbox = b;
      if (!st.bbox && st.points) st.bbox = boundsFromPoints(st.points);
      if (!st.bbox) setMsg(errorText('sd_measure_failed'), 'err');
      else setMsg(tf('gmReady', 'Job ready: {path} ({size})',
                     { path: st.job.name, size: fmtBytes(st.job.bytes) }), 'ok');
      render();
    }
    var timer = setTimeout(function () { finish(null); }, MEASURE_TIMEOUT_MS);
    apiGet('/api/grbl/frame-bbox?path=' + encodeURIComponent(path) +
           '&sid=' + encodeURIComponent(sessionId()))
      .then(function (res) {
        clearTimeout(timer);
        var d = res.body || {};
        if (d && d.success === true && isFinite(d.min_x) && isFinite(d.max_x) &&
            isFinite(d.min_y) && isFinite(d.max_y)) {
          finish({ minX: Number(d.min_x), maxX: Number(d.max_x),
                   minY: Number(d.min_y), maxY: Number(d.max_y) });
        } else {
          finish(null);
        }
      })
      .catch(function () { clearTimeout(timer); finish(null); });
    return true;
  }

  // ── step 1b: phone file ────────────────────────────────────────────────
  function setFile(f) {
    st.phoneFile = f || null;
    st.bbox = null;
    st.points = null;
    st.pointsFor = '';
    st.previewPoints = 0;
    st.previewPartial = false;
    if (!f) {
      el.name.textContent = t('gmPick', 'Choose a file');
      el.size.textContent = '.gcode \u00b7 .nc \u00b7 .txt';
    } else {
      el.name.textContent = cleanName(f.name);
      el.size.textContent = fmtBytes(f.size);
      st.job = { src: 'psram', name: cleanName(f.name), bytes: Number(f.size) || 0, path: '' };
    }
    render();
    if (f) {
      readPhoneFile(f).then(function (r) {
        if (st.phoneFile !== f) return;
        if (r && r.sink) {
          st.points = r.sink.saw ? r.sink.pts : null;
          st.pointsFor = jobKey(st.job);
          st.previewPoints = r.sink.count();
          if (r.bbox) st.bbox = r.bbox;
        }
        render();
      });
    }
    return !!f;
  }

  function upload() {
    if (!st.phoneFile || st.busy) return false;
    if (!st.canWrite) { setMsg(errorText('no_control'), 'err'); return false; }
    if (st.phoneFile.size > MAX_BYTES) { setMsg(errorText('file_too_large'), 'err'); return false; }
    var name = cleanName(st.phoneFile.name);
    var query = 'name=' + encodeURIComponent(name) + '&sid=' + encodeURIComponent(sessionId());
    if (st.token) query += '&token=' + encodeURIComponent(st.token);
    var xhr = new XMLHttpRequest();
    st.busy = true; render();
    setMsg(tf('gmSending', 'Uploading {name}\u2026', { name: name }), '');
    progress(1);
    xhr.open('POST', '/api/grbl/psram-upload?' + query, true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-Filename', name);
    xhr.setRequestHeader('X-Filesize', String(st.phoneFile.size || 0));
    xhr.upload.onprogress = function (e) {
      if (e && e.lengthComputable && e.total) progress(Math.round((e.loaded / e.total) * 100));
    };
    xhr.onload = function () {
      st.busy = false;
      var d = null;
      try { d = JSON.parse(xhr.responseText || '{}'); } catch (e) { d = null; }
      if (xhr.status === 200 && d && d.success === true) {
        st.job = { src: 'psram', name: name, bytes: Number(d.bytes) || (st.phoneFile ? st.phoneFile.size : 0), path: '' };
        progress(100);
        el.bar.className = 'bar';
        setMsg(tf('gmReady', 'Job ready: {path} ({size})',
                  { path: String(d.file_path || name), size: fmtBytes(st.job.bytes) }), 'ok');
      } else {
        progress(0);
        setMsg(errorText((d && d.message) ? String(d.message) : ('http_' + xhr.status)), 'err');
      }
      render();
    };
    xhr.onerror = function () {
      st.busy = false; progress(0);
      setMsg(errorText('network'), 'err');
      render();
    };
    xhr.send(st.phoneFile);
    return true;
  }

  // ── step 3: frame ──────────────────────────────────────────────────────
  function machineFresh() {
    return apiGet('/api/grbl/status').then(function (r) { return r.body || null; })
                                     .catch(function () { return null; });
  }

  function runFrame() {
    if (st.busy || !st.bbox) return false;
    if (!st.canWrite) { setMsg(errorText('no_control'), 'err'); return false; }
    st.busy = true; render();
    setMsg(t('gmFrameRunning', 'Tracing the outline\u2026'), '');
    machineFresh().then(function (s) {
      var wf = String((s && (s.workflow_state || s.state)) || '').toLowerCase();
      if (wf === 'running' || wf === 'paused' || wf === 'hold') {
        st.busy = false; render();
        setMsg(errorText('not_idle'), 'err');
        return;
      }
      var b = st.bbox;
      var mx = Math.round(1000 * FRAME_POWER_PCT / 5);   // S value for 5% of a 1000 rpm scale
      var sVal = Math.max(1, Math.min(1000, mx));
      var lines = [
        'M5', 'G90', 'G21',
        'G1 X' + b.minX.toFixed(3) + ' Y' + b.minY.toFixed(3) + ' F' + FRAME_FEED,
        'M3 S' + String(sVal),
        'G1 X' + b.maxX.toFixed(3) + ' Y' + b.minY.toFixed(3) + ' F' + FRAME_FEED,
        'G1 X' + b.maxX.toFixed(3) + ' Y' + b.maxY.toFixed(3) + ' F' + FRAME_FEED,
        'G1 X' + b.minX.toFixed(3) + ' Y' + b.maxY.toFixed(3) + ' F' + FRAME_FEED,
        'G1 X' + b.minX.toFixed(3) + ' Y' + b.minY.toFixed(3) + ' F' + FRAME_FEED,
        'M5'
      ];
      var chain = Promise.resolve();
      lines.forEach(function (cmd) {
        chain = chain.then(function () { return sendCmd(cmd); }).then(function (ok) {
          if (!ok) throw new Error('send_failed');
          return new Promise(function (r) { setTimeout(r, 150); });
        });
      });
      return chain.then(function () {
        st.busy = false; render();
        setMsg(t('gmFrameDone', 'Outline done.'), 'ok');
      });
    }).catch(function (e) {
      st.busy = false; render();
      setMsg(errorText(String((e && e.message) || 'network')), 'err');
    });
    return true;
  }

  // ── step 4: play ───────────────────────────────────────────────────────
  function startJob() {
    if (st.busy) return false;
    if (!st.job) { setMsg(errorText('empty_file'), 'err'); return false; }
    if (!st.canWrite) { setMsg(errorText('no_control'), 'err'); return false; }
    st.busy = true; render();
    setMsg(t('gmStarting', 'Starting\u2026'), '');
    var req = (st.job.src === 'sd')
      ? post('/api/grbl/stream', { slot: 0, path: st.job.path })
      : post('/api/grbl/psram-stream', { slot: 0 });
    req.then(function (res) {
      st.busy = false;
      if (res.body && res.body.success === true) {
        st.running = true;
        st.paused = false;
        setMsg(t('gmPlayStarted', 'Job started.'), 'ok');
        pollStatus(true);
      } else {
        setMsg(errorText((res.body && res.body.message) ? String(res.body.message) : 'stream_failed'), 'err');
      }
      render();
    }).catch(function () {
      st.busy = false; render();
      setMsg(errorText('network'), 'err');
    });
    return true;
  }

  function stopJob() {
    if (st.busy) return false;
    st.busy = true; render();
    post('/api/grbl/cancel', { slot: 0 }).then(function (res) {
      st.busy = false;
      st.running = false;
      st.paused = false;
      if (res.body && res.body.success === true) setMsg(t('gmPlayStopped', 'Job stopped.'), 'ok');
      else setMsg(errorText((res.body && res.body.message) ? String(res.body.message) : 'cancel_failed'), 'err');
      pollStatus(true);
      render();
    }).catch(function () {
      st.busy = false; render();
      setMsg(errorText('network'), 'err');
    });
    return true;
  }

  // Pause/resume run on both screens (Play has its own buttons, Jog its own).
  function pauseJob() {
    if (st.busy) return false;
    if (!st.canWrite) { setMsg(errorText('no_control'), 'err'); return false; }
    st.busy = true; render();
    setMsg(t('gmPausing', 'Pausing\u2026'), '');
    post('/api/grbl/pause', { slot: 0 }).then(function (res) {
      st.busy = false;
      var ok = !!(res.body && res.body.success === true);
      if (ok) st.paused = true;
      setMsg(ok ? t('gmPlayPaused', 'Job paused.')
                : errorText((res.body && res.body.message) ? String(res.body.message) : 'network'),
             ok ? 'ok' : 'err');
      pollStatus(true);
      render();
    }).catch(function () {
      st.busy = false; render();
      setMsg(errorText('network'), 'err');
    });
    return true;
  }

  function resumeJob() {
    if (st.busy) return false;
    if (!st.canWrite) { setMsg(errorText('no_control'), 'err'); return false; }
    st.busy = true; render();
    setMsg(t('gmResuming', 'Resuming\u2026'), '');
    post('/api/grbl/resume', { slot: 0 }).then(function (res) {
      st.busy = false;
      var ok = !!(res.body && res.body.success === true);
      if (ok) st.paused = false;
      setMsg(ok ? t('gmPlayResumed', 'Job resumed.')
                : errorText((res.body && res.body.message) ? String(res.body.message) : 'network'),
             ok ? 'ok' : 'err');
      pollStatus(true);
      render();
    }).catch(function () {
      st.busy = false; render();
      setMsg(errorText('network'), 'err');
    });
    return true;
  }

  // ── step 5: jog + machine buttons ──────────────────────────────────────
  function jog(axis, dir) {
    if (st.busy || !st.canWrite) {
      if (!st.canWrite) setMsg(errorText('no_control'), 'err');
      return false;
    }
    if (axis === 'Z' && !hasZ()) { setMsg(errorText('no_z_axis'), 'err'); return false; }
    var step = Number(st.jogStep) || 0;
    if (!(step > 0)) return false;
    var signed = (dir < 0 ? '-' : '') + String(step);
    var line = '$J=G91 G21 ' + axis + signed + ' F' + String(axis === 'Z' ? JOG_FEED_Z : JOG_FEED);
    st.busy = true; render();
    // Tool always OFF before a manual move (same rule as the PC page).
    sendCmd('M5').then(function () { return sendCmd(line); }).then(function (ok) {
      st.busy = false;
      if (!ok) setMsg(errorText('send_failed'), 'err');
      else setMsg(line, 'ok');
      pollStatus(true);
      render();
    });
    return true;
  }

  function armOrRun(kind, fn) {
    var now = Date.now();
    if (st.armedKey === kind && now < st.armedUntil) {
      st.armedKey = '';
      fn();
      return true;
    }
    st.armedKey = kind;
    st.armedUntil = now + 4000;
    setMsg(t('gmConfirmAgain', 'Tap again to confirm.'), 'err');
    render();
    return false;
  }

  function act(kind) {
    if (st.busy) return false;
    if (!st.canWrite && kind !== 'cancel') { setMsg(errorText('no_control'), 'err'); return false; }
    if (kind === 'home') {
      return armOrRun('home', function () {
        st.busy = true; render();
        setMsg('$H', '');
        sendCmd('$H').then(function (ok) {
          st.busy = false;
          setMsg(ok ? t('gmActHome', 'Homing\u2026') : errorText('send_failed'), ok ? 'ok' : 'err');
          render();
        });
      });
    }
    if (kind === 'unlock') {
      st.busy = true; render();
      setMsg('$X', '');
      sendCmd('$X').then(function (ok) {
        st.busy = false;
        setMsg(ok ? t('gmActUnlock', 'Alarm unlocked.') : errorText('send_failed'), ok ? 'ok' : 'err');
        render();
      });
      return true;
    }
    if (kind === 'zero') {
      return armOrRun('zero', function () {
        var z = hasZ() ? ' Z0' : '';
        st.busy = true; render();
        setMsg('G92 X0 Y0' + z, '');
        sendCmd('G92 X0 Y0' + z).then(function (ok) {
          st.busy = false;
          setMsg(ok ? t('gmActZero', 'Work zero set.') : errorText('send_failed'), ok ? 'ok' : 'err');
          render();
        });
      });
    }
    if (kind === 'reset') {
      return armOrRun('reset', function () {
        st.busy = true; render();
        setMsg(t('gmActReset', 'Resetting GRBL\u2026'), '');
        sendCmd(String.fromCharCode(24)).then(function (ok) {
          st.busy = false;
          setMsg(ok ? t('gmActResetOk', 'GRBL reset sent.') : errorText('send_failed'), ok ? 'ok' : 'err');
          render();
        });
      });
    }
    if (kind === 'hold') return pauseJob();
    if (kind === 'resume') return resumeJob();
    if (kind === 'cancel') return stopJob();
    return false;
  }

  function pollStatus(once) {
    machineFresh().then(function (s) {
      if (!s) return;
      var wf = String(s.workflow_state || s.state || '').toLowerCase();
      st.running = (wf === 'running' || wf === 'paused' || wf === 'hold');
      st.paused = (wf === 'paused' || wf === 'hold');
      st.axisCount = Number(s.axis_count) || 0;
      if (s.mpos_valid) {
        st.mpos = { x: Number(s.mpos_x) || 0, y: Number(s.mpos_y) || 0, z: Number(s.mpos_z) || 0 };
      }
      var label = String(s.machine_state || s.state || '').toUpperCase();
      if (el.playState) el.playState.textContent = wf ? wf.toUpperCase() : '--';
      if (el.playProgress) {
        var tot = Number(s.lines_total) || 0;
        var sent = Number(s.lines_sent) || 0;
        el.playProgress.textContent = tot > 0 ? (sent + ' / ' + tot) : '';
      }
      if (el.jogState) el.jogState.textContent = label || '--';
      if (el.jogPos) {
        el.jogPos.textContent = st.mpos
          ? ('X' + st.mpos.x.toFixed(2) + ' Y' + st.mpos.y.toFixed(2) + (hasZ() ? ' Z' + st.mpos.z.toFixed(2) : ''))
          : '';
      }
      if (s.usb_running) linkBadge(true, t('gmOnline', 'online'));
      else if (s.usb_running === false) linkBadge(false, t('gmOffline', 'offline'));
      render();
    });
  }

  // ── render / navigation ────────────────────────────────────────────────
  function jobReady() { return !!st.job; }

  function defaultMsg(step) {
    if (step === STEP_SEND) setMsg(t('gmStep1Hint', 'Pick a file from the SD card or from this phone.'), '');
    else if (step === STEP_PREVIEW) setMsg(t('gmStep2Hint', 'Toolpath of the selected job.'), '');
    else if (step === STEP_FRAME) setMsg(t('gmStep3Hint', 'Check the job area on the machine before cutting.'), '');
    else if (step === STEP_PLAY) setMsg(t('gmStep4Hint', 'Starts the job selected in step 1.'), '');
    else setMsg(t('gmStep5Hint', 'Jog with the arrows; machine buttons below.'), '');
  }

  function setStep(n) {
    var next = Math.max(1, Math.min(STEPS, Number(n) || 1));
    var changed = (next !== st.step);
    st.step = next;
    el.screens.forEach(function (sec, i) {
      if (sec) sec.className = 'screen' + (i + 1 === st.step ? ' on' : '');
    });
    el.steps.forEach(function (dot, i) {
      dot.className = (i + 1 === st.step) ? 'on' : (i + 1 < st.step ? 'done' : '');
    });
    el.no.textContent = st.step + ' / ' + STEPS;
    // An SD job needs the controller to measure it: do that once, in the
    // background, as soon as the operator reaches Frame or Play (never blocks
    // the navigation, and Play does not depend on the bounds).
    if (st.step >= STEP_FRAME && st.job && st.job.src === 'sd' && st.job.path &&
        !st.bbox && !st.measuring && st.measureFor !== st.job.path) {
      measureSd();
    }
    pollTimerForStep();
    try { history.replaceState(null, '', '#' + st.step); } catch (e) {}
    if (changed) defaultMsg(st.step);
    render();
    return st.step;
  }

  // One timer for the live screens (4 = job progress, 5 = machine state).
  function pollTimerForStep() {
    var live = (st.step === STEP_PLAY || st.step === STEP_JOG);
    if (!live) {
      if (st.pollTimer) { clearInterval(st.pollTimer); st.pollTimer = 0; }
      return;
    }
    pollStatus(false);
    if (!st.pollTimer) {
      st.pollTimer = setInterval(function () { pollStatus(false); }, 1000);
    }
  }

  function goNext() { if (st.step < STEPS) setStep(st.step + 1); }
  function goPrev() { if (st.step > 1) setStep(st.step - 1); }

  function setTab(tab) {
    st.tab = (tab === 'phone') ? 'phone' : 'sd';
    if (el.tabSd) el.tabSd.className = 'tab' + (st.tab === 'sd' ? ' on' : '');
    if (el.tabPhone) el.tabPhone.className = 'tab' + (st.tab === 'phone' ? ' on' : '');
    if (el.paneSd) el.paneSd.className = 'pane' + (st.tab === 'sd' ? ' on' : '');
    if (el.panePhone) el.panePhone.className = 'pane' + (st.tab === 'phone' ? ' on' : '');
    render();
  }

  function renderSdList() {
    if (!el.sdList || typeof document === 'undefined' || !document) return;
    el.sdList.innerHTML = '';
    if (st.sdLoading) {
      var ld = document.createElement('div');
      ld.className = 'note';
      ld.textContent = t('gmSdLoading', 'Loading the SD card\u2026');
      el.sdList.appendChild(ld);
      return;
    }
    if (st.sdError) {
      var er = document.createElement('div');
      er.className = 'note';
      er.textContent = t('gmSdError', 'Could not read the SD card.');
      el.sdList.appendChild(er);
      return;
    }
    var files = Array.isArray(st.sdFiles) ? st.sdFiles : [];
    var shown = 0;
    if (st.sdRoot && String(st.sdDir || '') !== String(st.sdRoot)) {
      var up = document.createElement('button');
      up.type = 'button';
      up.className = 'row';
      up.textContent = '\u21a9 ' + t('gmSdUp', 'Parent folder');
      up.addEventListener('click', function () { listSd(sdParent(st.sdDir)); });
      el.sdList.appendChild(up);
      shown++;
    }
    for (var i = 0; i < files.length; i++) {
      var f = files[i] || {};
      var isDir = !!f.is_dir;
      var row = document.createElement('button');
      row.type = 'button';
      row.className = 'row' + (!isDir && st.sdPath && String(f.path || '') === st.sdPath ? ' sel' : '');
      var nm = document.createElement('span');
      nm.className = 'n';
      nm.textContent = (isDir ? '\ud83d\udcc1 ' : '\ud83d\udcc4 ') + String(f.name || f.path || '');
      var sz = document.createElement('span');
      sz.className = 's';
      sz.textContent = isDir ? '' : fmtBytes(f.size);
      row.appendChild(nm);
      row.appendChild(sz);
      (function (entry, directory) {
        row.addEventListener('click', function () {
          if (directory) listSd(String(entry.path || ''));
          else selectSd(entry);
        });
      })(f, isDir);
      el.sdList.appendChild(row);
      shown++;
    }
    if (!shown) {
      var empty = document.createElement('div');
      empty.className = 'note';
      empty.textContent = t('gmSdEmpty', 'No G-code files on the SD card.');
      el.sdList.appendChild(empty);
    }
  }

  function render() {
    if (typeof document === 'undefined' || !document) return;
    var is1 = st.step === STEP_SEND, is2 = st.step === STEP_PREVIEW,
        is3 = st.step === STEP_FRAME, is4 = st.step === STEP_PLAY, is5 = st.step === STEP_JOG;

    el.title.textContent = t(is1 ? 'gmTitle1' : is2 ? 'gmTitlePreview' : is3 ? 'gmTitleFrame'
                                 : is4 ? 'gmTitlePlay' : 'gmTitleJog',
                               is1 ? 'Send G-code' : is2 ? 'Preview' : is3 ? 'Frame'
                                 : is4 ? 'Play' : 'Jog');
    el.title.setAttribute('data-i18n', is1 ? 'gmTitle1' : is2 ? 'gmTitlePreview' : is3 ? 'gmTitleFrame'
                                            : is4 ? 'gmTitlePlay' : 'gmTitleJog');

    // primary action per step ('USE' selects an SD file, 'SEND' uploads a phone file)
    var useSd = is1 && st.tab === 'sd';
    el.primary.textContent = useSd ? t('gmUse', 'USE')
                      : is1 ? t('gmSend', 'SEND')
                      : is3 ? t('gmFrameRun', 'FRAME')
                      : t('gmPlay', 'START');
    el.primary.setAttribute('data-i18n', useSd ? 'gmUse' : is1 ? 'gmSend' : is3 ? 'gmFrameRun' : 'gmPlay');
    el.primary.className = 'primary' + ((is2 || is5) ? ' off' : '');
    el.primary.disabled = st.busy || (useSd ? !st.sdPath : false) ||
      (is1 && !st.canWrite) ||
      (is3 && (!st.bbox || !st.canWrite)) ||
      (is4 && (!st.canWrite || !jobReady() || st.running));

    el.stop.className = 'stop';
    el.acts.style.display = ((is2 || is5) || (is4 && st.running)) ? 'none' : 'flex';

    // step 4 machine buttons: PAUSE while running, RESUME while paused, STOP
    if (el.pause) el.pause.disabled = st.busy || !st.canWrite || !st.running || st.paused;
    if (el.resume) el.resume.disabled = st.busy || !st.canWrite || !st.running || !st.paused;
    if (el.stop) el.stop.disabled = st.busy || (!st.running && !st.paused);

    // step 1 readouts
    if (is1) {
      if (el.sdDir) el.sdDir.textContent = String(st.sdDir || st.sdRoot || '/');
      renderSdList();
    }
    // step 2 preview
    if (is2) {
      if (el.prevInfo) el.prevInfo.textContent = previewInfoText();
      drawPreview();
    }
    // step 3 frame
    if (is3) {
      el.frameJob.textContent = jobReady() ? (st.job.name || t('gmJobOnController', 'Job on the controller'))
                                           : t('gmFrameNoJob', 'Pick a job first (step 1).');
      var showBounds = st.bbox && jobReady();
      el.frameArea.textContent = showBounds ? areaText(st.bbox) : (jobReady() ? (st.measuring ? '\u2026' : '') : '');
      el.frameHint.setAttribute('data-i18n', (jobReady() && !st.bbox && !st.measuring) ? 'gmMeasureFail' : 'gmFrameHint');
      el.frameHint.textContent = (jobReady() && !st.bbox && !st.measuring)
        ? t('gmMeasureFail', 'Could not measure the area.')
        : t('gmFrameHint', 'Outlines the job area with the spindle at 5%.');
    }
    // step 4 play
    if (is4) {
      el.playJob.textContent = jobReady() ? st.job.name : t('gmPlayNoJob', 'No job selected.');
      if (!el.playState.textContent) el.playState.textContent = '--';
    }
    // step 5 — Z column only on a 3-axis machine, armed button highlight
    if (is5 && el.jogPad) {
      var zDisabled = !hasZ();
      el.jogPad.querySelectorAll('button[data-jog]').forEach(function (b) {
        var axis = String(b.getAttribute('data-jog') || '').charAt(0);
        b.disabled = st.busy || (axis === 'Z' && zDisabled);
      });
    }
    if (el.actions) {
      el.actions.querySelectorAll('button[data-act]').forEach(function (b) {
        var kind = String(b.getAttribute('data-act') || '');
        b.className = (kind === 'reset') ? 'actbtn wide' : 'actbtn';
        if (st.armedKey === kind) b.className += ' arm';
      });
    }

    // navigation: always usable between the screens (only the ends stop it)
    el.back.disabled = st.busy || st.step === 1;
    el.next.disabled = st.busy || st.step === STEPS;
  }

  function init() {
    sessionId();
    var h = parseInt(String(location.hash || '').replace('#', ''), 10);
    st.step = (h >= 1 && h <= STEPS) ? h : 1;
    setStep(st.step);
    defaultMsg(st.step);
    setTab('sd');

    apiGet('/api/grbl/control-token?sid=' + encodeURIComponent(st.sid)).then(function (res) {
      var d = res.body || {};
      if (d && d.success === true) {
        st.token = String(d.token || '');
        st.canWrite = (d.can_write !== false);
        if (!st.canWrite) setMsg(errorText('no_control'), 'err');
      }
      render();
    }).catch(function () { render(); });

    // A job already on the controller (survives a reload) keeps steps 3/4 usable.
    apiGet('/api/grbl/psram-file').then(function (res) {
      var d = res.body || {};
      if (d && d.success === true && d.has_file) {
        st.job = { src: 'psram', name: String(d.name || 'job.nc'), bytes: Number(d.size) || 0, path: '' };
      }
      render();
    }).catch(function () {});

    machineFresh().then(function (s) {
      if (s) {
        st.axisCount = Number(s.axis_count) || 0;
        if (s.mpos_valid) st.mpos = { x: Number(s.mpos_x) || 0, y: Number(s.mpos_y) || 0, z: Number(s.mpos_z) || 0 };
        st.running = /running|paused|hold/.test(String(s.workflow_state || s.state || '').toLowerCase());
        st.paused = /paused|hold/.test(String(s.workflow_state || s.state || '').toLowerCase());
        linkBadge(!!s.usb_running, t(s.usb_running ? 'gmOnline' : 'gmOffline', s.usb_running ? 'online' : 'offline'));
      } else {
        linkBadge(false, t('gmOffline', 'offline'));
      }
      render();
    });

    listSd('');

    el.tabSd.addEventListener('click', function () { setTab('sd'); });
    el.tabPhone.addEventListener('click', function () { setTab('phone'); });
    el.sdReload.addEventListener('click', function () { listSd(st.sdDir); });
    el.prevLoad.addEventListener('click', function () {
      if (st.job && st.job.src === 'sd') loadPreviewFromSd();
      else if (!st.job) setMsg(t('gmFrameNoJob', 'Pick a job first (step 1).'), 'err');
      else setMsg(t('gmPreviewEmpty', 'No preview yet.'), 'err');
    });

    el.pick.addEventListener('click', function () { el.file.click(); });
    el.file.addEventListener('change', function () {
      var f = el.file.files && el.file.files[0];
      setFile(f || null);
      if (f && f.size > MAX_BYTES) setMsg(errorText('file_too_large'), 'err');
      else if (f) setMsg(t('gmReadyToSend', 'Ready to send.'), '');
    });

    el.primary.addEventListener('click', function () {
      if (st.step === STEP_SEND) {
        if (st.tab === 'sd') {
          if (!st.sdPath) setMsg(t('gmSdPickFirst', 'Pick a file from the list.'), 'err');
          else measureSd();
        } else upload();
      }
      else if (st.step === STEP_FRAME) runFrame();
      else if (st.step === STEP_PLAY) startJob();
    });
    el.stop.addEventListener('click', stopJob);
    el.pause.addEventListener('click', pauseJob);
    el.resume.addEventListener('click', resumeJob);
    el.back.addEventListener('click', function () { if (!el.back.disabled) goPrev(); });
    el.next.addEventListener('click', function () { if (!el.next.disabled) goNext(); });

    // jog pad + step chips
    el.jogPad.addEventListener('click', function (ev) {
      var b = ev.target && ev.target.closest ? ev.target.closest('button[data-jog]') : null;
      if (!b || b.disabled) return;
      var v = String(b.getAttribute('data-jog') || '');
      jog(v.charAt(0), v.charAt(1) === '-' ? -1 : 1);
    });
    el.stepChips.addEventListener('click', function (ev) {
      var b = ev.target && ev.target.closest ? ev.target.closest('button[data-step]') : null;
      if (!b) return;
      st.jogStep = Number(b.getAttribute('data-step')) || 1;
      el.stepChips.querySelectorAll('button[data-step]').forEach(function (x) {
        x.className = 'chip' + (Number(x.getAttribute('data-step')) === st.jogStep ? ' on' : '');
      });
      render();
    });
    el.actions.addEventListener('click', function (ev) {
      var b = ev.target && ev.target.closest ? ev.target.closest('button[data-act]') : null;
      if (!b) return;
      act(String(b.getAttribute('data-act') || ''));
    });

    // swipe left/right changes screen (a vertical drag through the SD list is
    // not a swipe: the horizontal distance has to clearly win).
    var tx = 0, ty = 0, tracking = false;
    function touchPoint(e) {
      var list = (e.touches && e.touches.length) ? e.touches : (e.changedTouches || []);
      return list && list[0] ? list[0] : null;
    }
    document.getElementById('gm').addEventListener('touchstart', function (e) {
      var p = touchPoint(e);
      if (!p) { tracking = false; return; }
      tracking = true;
      tx = p.clientX; ty = p.clientY;
    }, { passive: true });
    document.getElementById('gm').addEventListener('touchend', function (e) {
      if (!tracking) return;
      tracking = false;
      var p = touchPoint(e);
      if (!p) return;
      var dx = p.clientX - tx, dy = p.clientY - ty;
      if (Math.abs(dx) < 60 || Math.abs(dx) < Math.abs(dy) * 1.5) return;
      if (dx < 0) goNext(); else goPrev();
    }, { passive: true });
  }

  window.__test__ = window.__test__ || {};
  window.__test__.gmInit = init;
  window.__test__.gmSetStep = setStep;
  window.__test__.gmNext = goNext;
  window.__test__.gmPrev = goPrev;
  window.__test__.gmSetTab = setTab;
  window.__test__.gmListSd = listSd;
  window.__test__.gmSelectSd = selectSd;
  window.__test__.gmMeasure = measureSd;
  window.__test__.gmPreviewSd = loadPreviewFromSd;
  window.__test__.gmPointsFromText = function (txt) { return pointsFromText(txt); };
  window.__test__.gmJog = jog;
  window.__test__.gmAct = act;
  window.__test__.gmUpload = upload;
  window.__test__.gmRunFrame = runFrame;
  window.__test__.gmStart = startJob;
  window.__test__.gmStop = stopJob;
  window.__test__.gmPause = pauseJob;
  window.__test__.gmResume = resumeJob;
  window.__test__.gmSetFile = setFile;
  window.__test__.gmBounds = boundsFromPoints;
  window.__test__.gmArea = areaText;
  window.__test__.gmCleanName = cleanName;
  window.__test__.gmFmtBytes = fmtBytes;
  window.__test__.gmErrorText = errorText;
  window.__test__.gmState = st;
  window.__test__.gmT = t;

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
</script>
)GMOBILE";
