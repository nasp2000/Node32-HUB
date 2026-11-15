#include "mobile_system_asset.h"

// Shared "System" screen (summary + OTA + log + restart) for the phone pages.
// Same ids in every pack: mu* prefixed so they never collide with a page.
const char MOBILE_SYSTEM_HTML[] PROGMEM = R"MPSYS(
<section class="screen mpsys" id="gmSYS">
  <div class="subbar">
    <button type="button" class="subt on" id="muTabSum" data-mu-tab="sum">Summary</button>
    <button type="button" class="subt" id="muTabOta" data-mu-tab="ota">Firmware</button>
    <button type="button" class="subt" id="muTabLog" data-mu-tab="log">Log</button>
  </div>

  <div class="pane on" id="muPaneSum">
    <div class="tiles">
      <div class="tile"><span class="k" data-i18n="gmSysFw">Firmware</span><b id="muFw">--</b></div>
      <div class="tile"><span class="k" data-i18n="gmSysUp">Uptime</span><b id="muUp">--</b></div>
      <div class="tile"><span class="k" data-i18n="gmSysHeap">Heap</span><b id="muHeap">--</b></div>
      <div class="tile"><span class="k" data-i18n="gmSysPsram">PSRAM</span><b id="muPsram">--</b></div>
      <div class="tile"><span class="k" data-i18n="gmSysSdFree">SD card</span><b id="muSd">--</b></div>
      <div class="tile"><span class="k" data-i18n="gmSysWifi">WiFi</span><b id="muWifi">--</b></div>
    </div>
    <button type="button" class="reboot" id="muReboot" data-i18n="gmSysReboot">Restart</button>
  </div>

  <div class="pane" id="muPaneOta">
    <div class="note" id="muOtaCur">--</div>
    <button type="button" class="btn" id="muOtaPick" data-i18n="gmOtaPick">Choose the firmware (.bin)</button>
    <input type="file" id="muOtaFile" accept=".bin,application/octet-stream" hidden>
    <div class="bar" id="muOtaBar"><span id="muOtaFill"></span></div>
    <div class="msg" id="muOtaMsg" data-i18n="gmOtaHint">Pick the .bin, upload it and the controller restarts.</div>
    <button type="button" class="primary" id="muOtaGo" disabled data-i18n="gmOtaGo">Upload firmware</button>
  </div>

  <div class="pane" id="muPaneLog">
    <pre class="log" id="muLog"></pre>
    <div class="subbar">
      <button type="button" class="subt" id="muLogPause" data-i18n="gmLogPause">Pause</button>
      <button type="button" class="subt" id="muLogClear" data-i18n="gmLogClear">Clear</button>
      <button type="button" class="subt" id="muLogBottom" data-i18n="gmLogBottom">Bottom</button>
    </div>
  </div>
</section>
)MPSYS";

const char MOBILE_SYSTEM_JS[] PROGMEM = R"MPSYSJS(
<script>
// MPSystem — the shared System screen. Reads only public endpoints:
//   /api/version, /api/health, /api/status, /api/sd-status  (summary)
//   /api/enable-ota + /api/firmware-upload                 (OTA)
//   /api/serial-monitor                                    (log)
//   /api/restart                                           (restart)
window.MPSystem = (function () {
  'use strict';
  var el = {
    tabSum: document.getElementById('muTabSum'),
    tabOta: document.getElementById('muTabOta'),
    tabLog: document.getElementById('muTabLog'),
    paneSum: document.getElementById('muPaneSum'),
    paneOta: document.getElementById('muPaneOta'),
    paneLog: document.getElementById('muPaneLog'),
    fw: document.getElementById('muFw'),
    up: document.getElementById('muUp'),
    heap: document.getElementById('muHeap'),
    psram: document.getElementById('muPsram'),
    sd: document.getElementById('muSd'),
    wifi: document.getElementById('muWifi'),
    reboot: document.getElementById('muReboot'),
    otaCur: document.getElementById('muOtaCur'),
    otaPick: document.getElementById('muOtaPick'),
    otaFile: document.getElementById('muOtaFile'),
    otaBar: document.getElementById('muOtaBar'),
    otaFill: document.getElementById('muOtaFill'),
    otaMsg: document.getElementById('muOtaMsg'),
    otaGo: document.getElementById('muOtaGo'),
    log: document.getElementById('muLog'),
    logPause: document.getElementById('muLogPause'),
    logClear: document.getElementById('muLogClear'),
    logBottom: document.getElementById('muLogBottom')
  };

  var st = {
    open: false, tab: 'sum',
    busy: false, blocked: false,
    otaFile: null, otaBusy: false,
    logTimer: 0, logPaused: false, logSeq: 0, logLines: []
  };
  var blocker = null;

  function setText(node, txt) { if (node) node.textContent = txt || '--'; }

  // Destructive actions ask first. The dialog is the firmware's own
  // customConfirm (same as every page); if it is not there yet (script order,
  // very old client) the two-tap arming takes over.
  function confirmAction(text, cb) {
    if (typeof window.customConfirm === 'function') {
      window.customConfirm(text, function (ok) { if (ok) cb(); });
      return;
    }
    if (MU.arm('confirm')) { cb(); return; }
    MU.msg(el.otaMsg, MU.t('gmConfirmAgain', 'Tap again to confirm.'), 'err');
  }

  async function refreshSummary() {
    var res = await Promise.all([
      MU.get('/api/version').catch(function () { return { body: {} }; }),
      MU.get('/api/health').catch(function () { return { body: {} }; }),
      MU.get('/api/status').catch(function () { return { body: {} }; }),
      MU.get('/api/sd-status').catch(function () { return { body: {} }; })
    ]);
    var ver = res[0].body || {}, hl = res[1].body || {}, sys = res[2].body || {}, sd = res[3].body || {};
    setText(el.fw, ver.version || '--');
    if (el.otaCur) {
      el.otaCur.textContent = MU.tf('gmOtaCurrent', 'Current firmware: {v}', { v: (ver.version || '--') });
    }
    setText(el.up, hl.uptime_str || '--');
    setText(el.heap, hl.heap ? MU.fmtSize(hl.heap.free) : (sys.free_heap ? MU.fmtSize(sys.free_heap) : '--'));
    setText(el.psram, hl.psram ? MU.fmtSize(hl.psram.free) : (sys.psram_free ? MU.fmtSize(sys.psram_free) : '--'));
    var sdFree = (sd.total_bytes ? (Number(sd.total_bytes) - Number(sd.used_bytes)) : 0);
    setText(el.sd, sdFree > 0 ? (MU.fmtSize(sdFree) + ' ' + MU.t('gmSysFree', 'free')) : '--');
    if (el.sd && sd.total_bytes) el.sd.title = MU.fmtSize(sd.total_bytes) + ' total';
    setText(el.wifi, (sys.wifi_rssi !== undefined) ? (String(sys.wifi_rssi) + ' dBm') : '--');
  }

  async function refreshLog() {
    if (st.logPaused) return;
    try {
      var r = await MU.get('/api/serial-monitor?limit=120&no_cmds=1&last_seq=' + String(st.logSeq));
      var d = r.body || {};
      var msgs = Array.isArray(d.messages) ? d.messages : [];
      var changed = false;
      for (var i = 0; i < msgs.length; i++) {
        var line = String(msgs[i].data !== undefined ? msgs[i].data : (msgs[i].text || ''));
        if (!line) continue;
        st.logLines.push(line);
        changed = true;
      }
      if (d.current_seq !== undefined) st.logSeq = Number(d.current_seq) || st.logSeq;
      if (st.logLines.length > 400) st.logLines = st.logLines.slice(-400);
      if (changed && el.log) {
        var atBottom = (el.log.scrollHeight - el.log.scrollTop - el.log.clientHeight) < 24;
        el.log.textContent = st.logLines.join('\n');
        if (atBottom) el.log.scrollTop = el.log.scrollHeight;
      }
    } catch (e) {}
  }

  function tab(name) {
    st.tab = (name === 'ota' || name === 'log') ? name : 'sum';
    if (el.tabSum) el.tabSum.className = 'subt' + (st.tab === 'sum' ? ' on' : '');
    if (el.tabOta) el.tabOta.className = 'subt' + (st.tab === 'ota' ? ' on' : '');
    if (el.tabLog) el.tabLog.className = 'subt' + (st.tab === 'log' ? ' on' : '');
    if (el.paneSum) el.paneSum.className = 'pane' + (st.tab === 'sum' ? ' on' : '');
    if (el.paneOta) el.paneOta.className = 'pane' + (st.tab === 'ota' ? ' on' : '');
    if (el.paneLog) el.paneLog.className = 'pane' + (st.tab === 'log' ? ' on' : '');
    if (st.tab === 'sum') refreshSummary();
    if (st.tab === 'log') refreshLog();
  }

  function open() {
    st.open = true;
    tab(st.tab || 'sum');
    if (!st.logTimer) st.logTimer = setInterval(function () {
      if (!st.open) return;
      if (st.tab === 'sum') refreshSummary();
      else if (st.tab === 'log') refreshLog();
    }, 2000);
  }

  function close() {
    st.open = false;
    if (st.logTimer) { clearInterval(st.logTimer); st.logTimer = 0; }
  }

  function setBlocker(fn) { blocker = fn; }

  function reboot() {
    if (st.busy) return false;
    if (blocker && blocker()) {
      MU.msg(el.otaMsg, MU.t('gmSysBlocked', 'A job is running - stop it before restarting.'), 'err');
      return false;
    }
    confirmAction(MU.t('gmSysRebootConfirm', 'Restart the controller? The job in progress is cancelled.'),
      function () {
        st.busy = true;
        MU.post('/api/restart', {}).catch(function () {}).then(function () {
          st.busy = false;
        });
      });
    return true;
  }

  function pickOta() {
    if (el.otaFile) { el.otaFile.value = ''; el.otaFile.click(); }
  }

  function uploadOta() {
    if (!st.otaFile || st.otaBusy) return false;
    confirmAction(MU.tf('gmOtaConfirm', 'Write {name} ({size}) to the controller? It restarts when done.',
                        { name: st.otaFile.name, size: MU.fmtSize(st.otaFile.size) }),
      function () { doUploadOta(); });
    return true;
  }

  function doUploadOta() {
    st.otaBusy = true;
    if (el.otaGo) el.otaGo.disabled = true;
    if (el.otaBar) el.otaBar.className = 'bar on';
    if (el.otaFill) el.otaFill.style.width = '0%';
    MU.msg(el.otaMsg, MU.t('gmOtaEnabling', 'Enabling OTA...'), '');

    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/firmware-upload', true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-Filename', encodeURIComponent(st.otaFile.name));
    xhr.setRequestHeader('X-Filesize', String(st.otaFile.size || 0));
    xhr.upload.onprogress = function (e) {
      if (e && e.lengthComputable && e.total && el.otaFill) {
        el.otaFill.style.width = Math.round((e.loaded / e.total) * 100) + '%';
      }
    };
    xhr.onload = function () {
      st.otaBusy = false;
      var ok = (xhr.status === 200);
      if (ok) {
        if (el.otaFill) el.otaFill.style.width = '100%';
        MU.msg(el.otaMsg, MU.t('gmOtaDone', 'Firmware uploaded. The controller restarts now.'), 'ok');
        setTimeout(function () { MU.post('/api/restart', {}).catch(function () {}); }, 1200);
      } else {
        if (el.otaBar) el.otaBar.className = 'bar';
        MU.msg(el.otaMsg, MU.t('gmOtaFail', 'Firmware upload failed.'), 'err');
        if (el.otaGo) el.otaGo.disabled = false;
      }
    };
    xhr.onerror = function () {
      st.otaBusy = false;
      if (el.otaBar) el.otaBar.className = 'bar';
      MU.msg(el.otaMsg, MU.t('gmOtaFail', 'Firmware upload failed.'), 'err');
      if (el.otaGo) el.otaGo.disabled = false;
    };

    // OTA has to be armed in the firmware before the first byte.
    MU.post('/api/enable-ota', {}).catch(function () {}).then(function () {
      MU.msg(el.otaMsg, MU.t('gmOtaUploading', 'Uploading...'), '');
      xhr.send(st.otaFile);
    });
  }

  function init() {
    // Delegated so the three tabs work even if one id is missing.
    var subbar = document.querySelector('#gmSYS .subbar');
    if (subbar) {
      subbar.addEventListener('click', function (ev) {
        var b = ev.target && ev.target.closest ? ev.target.closest('button[data-mu-tab]') : null;
        if (!b) return;
        tab(String(b.getAttribute('data-mu-tab')));
      });
    }
    if (el.tabSum) el.tabSum.addEventListener('click', function () { tab('sum'); });
    if (el.tabOta) el.tabOta.addEventListener('click', function () { tab('ota'); });
    if (el.tabLog) el.tabLog.addEventListener('click', function () { tab('log'); });
    if (el.reboot) el.reboot.addEventListener('click', reboot);
    if (el.otaPick) el.otaPick.addEventListener('click', pickOta);
    if (el.otaFile) el.otaFile.addEventListener('change', function () {
      st.otaFile = (el.otaFile.files && el.otaFile.files[0]) || null;
      if (st.otaFile) {
        MU.msg(el.otaMsg, MU.tf('gmOtaReady', 'Ready to upload {name} ({size})',
                { name: st.otaFile.name, size: MU.fmtSize(st.otaFile.size) }), 'ok');
      }
      if (el.otaGo) {
        el.otaGo.disabled = !st.otaFile;
        el.otaGo.textContent = st.otaFile ? MU.t('gmOtaGo', 'Upload firmware') : MU.t('gmOtaGo', 'Upload firmware');
      }
    });
    if (el.otaGo) el.otaGo.addEventListener('click', uploadOta);
    if (el.logPause) el.logPause.addEventListener('click', function () {
      st.logPaused = !st.logPaused;
      el.logPause.className = 'subt' + (st.logPaused ? ' on' : '');
      el.logPause.textContent = st.logPaused ? MU.t('gmLogResume', 'Resume') : MU.t('gmLogPause', 'Pause');
    });
    if (el.logClear) el.logClear.addEventListener('click', function () {
      st.logLines = [];
      if (el.log) el.log.textContent = '';
    });
    if (el.logBottom) el.logBottom.addEventListener('click', function () {
      if (el.log) el.log.scrollTop = el.log.scrollHeight;
    });
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
    else init();
  }

  return {
    open: open, close: close, tab: tab,
    refresh: refreshSummary, refreshLog: refreshLog,
    reboot: reboot, setBlocker: setBlocker,
    _state: st
  };
})();
</script>
)MPSYSJS";
