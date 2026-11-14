#include "page_cam_mobile_asset.h"
#include "../../feature_flags.h"

// Cam-M / Cam-P4 phone page — three screens, no page scroll.
//
//   1 Live   — MJPEG live view (/cam/stream), photo and video recording.
//   2 Files  — recordings on the SD card (play/download + delete).
//   3 System — the shared System screen (summary/OTA/log/restart).
//
// Same standard as the CNC page: header strip with one column per screen, the
// shared message bar / progress / primary action, arrows + swipe, 180 ms tap
// guard and the shared System block printed between HEAD and TAIL.
#if ENABLE_CAMERA==1
const char PAGE_CAM_MOBILE[] PROGMEM = R"CAMMOBILE(
<style>
  /* camera-only bits; everything else comes from MOBILE_COMMON_CSS */
  #gm { position: fixed; inset: 0; display: flex; flex-direction: column; gap: 8px;
        padding: 10px 12px calc(10px + env(safe-area-inset-bottom));
        box-sizing: border-box; height: 100dvh; overflow: hidden;
        background: #0b1220; color: #e6f1ff;
        font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  #gm .viewwrap {
    flex: 0 1 auto; width: 100%; max-height: 100%; aspect-ratio: 16 / 9;
    display: flex; align-items: center; justify-content: center;
    background: #000; border: 1px solid #1e293b; border-radius: 12px; overflow: hidden; position: relative;
  }
  #gm .viewwrap img { max-width: 100%; max-height: 100%; display: block; }
  #gm .viewinfo {
    position: absolute; left: 8px; bottom: 8px; font-size: 11px; color: #cbd5e1;
    background: rgba(2,6,23,.65); padding: 3px 9px; border-radius: 999px;
  }
  #gm .recbtn {
    flex: 0 0 104px; height: 54px; border-radius: 12px; border: 1px solid #dc2626;
    background: #0f172a; color: #fca5a5; font-size: 14px; font-weight: 700;
  }
  #gm .recbtn.on { background: #7f1d1d; color: #fff; }
  #gm .recbtn:disabled { opacity: .45; }
  #gm .row a.n { color: #e6f1ff; text-decoration: none; }
  #gm .row a.n:active { color: #93c5fd; }
</style>
<div id="gm" class="mp">
  <header>
    <nav class="navstrip" id="gmNavStrip">
      <button type="button" class="ncol on" data-step-go="1"><span class="ntab" data-i18n="cmNavLive">Live</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="2"><span class="ntab" data-i18n="cmNavFiles">Files</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="3"><span class="ntab" data-i18n="cmNavSys">System</span><i class="nrail"></i></button>
    </nav>
    <div class="hrow">
      <span class="pack" id="gmPack"></span>
      <h1 id="gmTitle" class="sr" data-i18n="cmTitleLive">Live</h1>
      <span class="badge" id="gmLink">--</span>
    </div>
  </header>

  <!-- ── 1: live ─────────────────────────────────────────────────── -->
  <section class="screen on" id="gmS1">
    <div class="viewwrap" id="cmView">
      <img id="cmImg" alt="live" src="">
      <div class="viewinfo"><span id="cmInfo">--</span></div>
    </div>
  </section>

  <!-- ── 2: files ────────────────────────────────────────────────── -->
  <section class="screen" id="gmS2">
    <div class="rowhdr">
      <span class="dir" id="cmDir" data-i18n="cmFilesTitle">Recordings on the SD card</span>
      <button type="button" class="mini" id="cmReload" data-i18n-title="cmReloadTip" title="Reload the list">&#8635;</button>
    </div>
    <div class="list" id="cmList"></div>
  </section>

)CAMMOBILE";

// Tail: message bar, progress, actions, footer navigation and the page script.
// The shared System screen is printed between HEAD and TAIL.
const char PAGE_CAM_MOBILE_TAIL[] PROGMEM = R"CAMTAIL(  <div class="msg" id="gmMsg" data-i18n="cmStep1Hint">Live view. Photo and video stay on the SD card.</div>
  <div class="bar" id="gmBar"><span id="gmFill"></span></div>

  <div class="acts">
    <button type="button" class="primary" id="gmPrimary" data-i18n="cmPhoto">PHOTO</button>
    <button type="button" class="recbtn" id="cmRec" data-i18n="cmRecord">REC</button>
  </div>

  <div class="nav">
    <button type="button" class="arrow back" id="gmBack" disabled title="Back">&#8592;</button>
    <span class="no" id="gmNo">1 / 3</span>
    <button type="button" class="arrow next" id="gmNext" data-i18n-title="gmNextTip">&#8594;</button>
  </div>
</div>

<script>
(function () {
  'use strict';
  var STEPS = 3;
  var STEP_LIVE = 1, STEP_FILES = 2, STEP_SYS = 3;
  var MAX_BYTES_TXT = 0;

  var el = {
    title: document.getElementById('gmTitle'),
    navStrip: document.getElementById('gmNavStrip'),
    screens: [document.getElementById('gmS1'), document.getElementById('gmS2'), document.getElementById('gmSYS')],
    pack: document.getElementById('gmPack'),
    link: document.getElementById('gmLink'),
    img: document.getElementById('cmImg'),
    info: document.getElementById('cmInfo'),
    dir: document.getElementById('cmDir'),
    list: document.getElementById('cmList'),
    reload: document.getElementById('cmReload'),
    msg: document.getElementById('gmMsg'),
    bar: document.getElementById('gmBar'),
    fill: document.getElementById('gmFill'),
    primary: document.getElementById('gmPrimary'),
    rec: document.getElementById('cmRec'),
    acts: document.querySelector('#gm .acts'),
    back: document.getElementById('gmBack'),
    next: document.getElementById('gmNext'),
    no: document.getElementById('gmNo')
  };

  var st = {
    step: 1, busy: false, recording: false, files: [],
    info: null, lastShot: 0, timer: 0
  };

  function t(k, fb) { return window.MU ? MU.t(k, fb) : (fb !== undefined ? fb : k); }
  function tf(k, fb, v) { return window.MU ? MU.tf(k, fb, v) : (fb !== undefined ? fb : k); }
  function msg(text, kind) { if (window.MU) MU.msg(el.msg, text, kind); }

  function linkBadge(on, text) {
    if (!el.link) return;
    el.link.textContent = text;
    el.link.className = 'badge' + (on ? ' ok' : ' warn');
  }

  function streamUrl() { return '/cam/stream?_=' + String(Date.now()); }

  function startStream() {
    if (!el.img) return;
    // Re-assigning src is what makes the browser open a new MJPEG connection.
    el.img.src = streamUrl();
  }

  function loadStatus() {
    if (!window.MU) return;
    MU.get('/api/camera/status').then(function (res) {
      var d = res.body || {};
      st.info = d;
      st.recording = !!d.recording;
      var bits = [];
      if (d.fps !== undefined) bits.push(String(d.fps) + ' fps');
      if (d.resolution) bits.push(String(d.resolution));
      if (d.sdFree !== undefined) bits.push(MU.fmtSize(Number(d.sdFree) * 1024 * 1024) + ' free');
      if (d.streamClients !== undefined) bits.push(String(d.streamClients) + ' viewers');
      if (el.info) el.info.textContent = bits.join(' · ');
      // Match the view box to the stream so a 16:9 camera is not letterboxed
      // inside a tall black area.
      var wrap = document.getElementById('cmView');
      var res = String(d.resolution || '');
      var m = res.match(/(\d+)\s*[xX*]\s*(\d+)/);
      if (wrap && m) {
        var w = parseInt(m[1], 10), h = parseInt(m[2], 10);
        if (w > 0 && h > 0) wrap.style.aspectRatio = String(w) + ' / ' + String(h);
      }
      if (st.recording) {
        msg(tf('cmRecording', 'Recording ({s}s, {n} frames)', {
          s: String(d.recElapsed || 0), n: String(d.recFrames || 0)
        }), '');
      }
      linkBadge(true, t('gmOnline', 'online'));
      render();
    }).catch(function () { linkBadge(false, t('gmOffline', 'offline')); });
  }

  function photo() {
    if (st.busy) return false;
    st.busy = true; render();
    msg(t('cmPhotoSaving', 'Taking the photo…'), '');
    MU.post('/api/camera/photo', {}).then(function (res) {
      st.busy = false;
      var ok = !!(res.body && (res.body.success === true || res.body.ok === true));
      st.lastShot = Date.now();
      msg(ok ? t('cmPhotoOk', 'Photo saved on the SD card.') : t('cmPhotoFail', 'Could not take the photo.'),
          ok ? 'ok' : 'err');
      if (ok) startStream();
      render();
    }).catch(function () {
      st.busy = false; render();
      msg(t('cmPhotoFail', 'Could not take the photo.'), 'err');
    });
    return true;
  }

  function toggleRecording() {
    if (st.busy) return false;
    var url = st.recording ? '/api/camera/recording/stop' : '/api/camera/recording/start';
    st.busy = true; render();
    msg(t(st.recording ? 'cmRecStopping' : 'cmRecStarting',
          st.recording ? 'Stopping…' : 'Starting…'), '');
    MU.post(url, {}).then(function (res) {
      st.busy = false;
      var ok = !!(res.body && (res.body.success === true || res.body.ok === true));
      if (ok) {
        st.recording = !st.recording;
        msg(t(st.recording ? 'cmRecStarted' : 'cmRecStopped',
              st.recording ? 'Recording.' : 'Recording stopped.'), 'ok');
      } else {
        msg(t('cmRecFail', 'The camera refused the request.'), 'err');
      }
      render();
    }).catch(function () {
      st.busy = false; render();
      msg(t('cmRecFail', 'The camera refused the request.'), 'err');
    });
    return true;
  }

  function renderFiles() {
    if (!el.list) return;
    if (typeof document === 'undefined' || !document) return;
    el.list.innerHTML = '';
    if (!st.files.length) {
      var none = document.createElement('div');
      none.className = 'note';
      none.textContent = t('cmFilesEmpty', 'No recordings on the SD card.');
      el.list.appendChild(none);
      return;
    }
    for (var i = 0; i < st.files.length; i++) {
      var f = st.files[i] || {};
      var row = document.createElement('div');
      row.className = 'row';
      var a = document.createElement('a');
      a.className = 'n';
      a.href = '/cam/playback?f=' + encodeURIComponent(String(f.n || ''));
      a.target = '_blank';
      a.rel = 'noopener';
      a.textContent = String(f.n || '').replace(/^.*\//, '');
      var sz = document.createElement('span');
      sz.className = 's';
      sz.textContent = window.MU ? MU.fmtSize(f.s) : String(f.s || '');
      var del = document.createElement('button');
      del.type = 'button';
      del.className = 'mini';
      del.textContent = '\u2715';
      del.title = t('cmDelete', 'Delete');
      (function (path) {
        del.addEventListener('click', function () { deleteFile(path); });
      })(String(f.n || ''));
      row.appendChild(a);
      row.appendChild(sz);
      row.appendChild(del);
      el.list.appendChild(row);
    }
  }

  function loadFiles() {
    if (!window.MU) return;
    return MU.get('/api/camera/files').then(function (res) {
      var d = res.body || {};
      st.files = Array.isArray(d.files) ? d.files : [];
      renderFiles();
    }).catch(function () { st.files = []; renderFiles(); });
  }

  function deleteFile(path) {
    var doDelete = function () {
      MU.post('/api/camera/delete', { file: path, path: path }).then(function () {
        loadFiles();
        msg(t('cmDeleted', 'Recording deleted.'), 'ok');
      }).catch(function () { msg(t('cmDeleteFail', 'Could not delete.'), 'err'); });
    };
    if (typeof window.customConfirm === 'function') {
      window.customConfirm(tf('cmDeleteConfirm', 'Delete {name}?', { name: String(path).replace(/^.*\//, '') }),
        function (ok) { if (ok) doDelete(); });
      return;
    }
    if (MU.arm('del:' + path)) doDelete();
  }

  function setStep(n) {
    var next = Math.max(1, Math.min(STEPS, Number(n) || 1));
    st.step = next;
    el.screens.forEach(function (sec, i) {
      if (sec) sec.className = 'screen' + (i + 1 === st.step ? ' on' : '');
    });
    el.no.textContent = st.step + ' / ' + STEPS;
    if (window.MU && MU.stripActive) MU.stripActive(el.navStrip, st.step);
    if (window.MPSystem) {
      if (st.step === STEP_SYS) MPSystem.open();
      else MPSystem.close();
    }
    if (st.step === STEP_FILES) loadFiles();
    if (st.step === STEP_LIVE) startStream();
    try { history.replaceState(null, '', '#' + st.step); } catch (e) {}
    msg(t(st.step === STEP_LIVE ? 'cmStep1Hint'
         : st.step === STEP_FILES ? 'cmStep2Hint'
         : 'gmStep6Hint',
         st.step === STEP_LIVE ? 'Live view. Photo and video stay on the SD card.'
         : st.step === STEP_FILES ? 'Tap a recording to play or download it.'
         : 'Controller health, OTA, log and restart.'), '');
    render();
    return st.step;
  }

  function goNext() { if (st.step < STEPS) setStep(st.step + 1); }
  function goPrev() { if (st.step > 1) setStep(st.step - 1); }

  function render() {
    if (typeof document === 'undefined' || !document) return;
    var is1 = st.step === STEP_LIVE, is2 = st.step === STEP_FILES;
    el.title.textContent = t(is1 ? 'cmTitleLive' : is2 ? 'cmTitleFiles' : 'gmTitleSys',
                             is1 ? 'Live' : is2 ? 'Files' : 'System');
    el.primary.className = 'primary' + (is1 ? '' : ' off');
    el.primary.textContent = t('cmPhoto', 'PHOTO');
    el.primary.disabled = st.busy || !is1;
    if (el.rec) {
      el.rec.className = 'recbtn' + (st.recording ? ' on' : '');
      el.rec.textContent = st.recording ? t('cmStop', 'STOP') : t('cmRecord', 'REC');
      el.rec.disabled = st.busy || !is1;
    }
    if (el.acts) el.acts.style.display = is1 ? 'flex' : 'none';
    el.back.disabled = st.busy || st.step === 1;
    el.next.disabled = st.busy || st.step === STEPS;
  }

  function init() {
    if (el.pack && window.PACK_NAME) el.pack.textContent = String(window.PACK_NAME);
    setStep(parseInt(String(location.hash).replace('#', ''), 10) || 1);
    startStream();
    loadStatus();
    if (!st.timer) {
      st.timer = setInterval(function () {
        if (st.step === STEP_LIVE) loadStatus();
      }, 2000);
    }
    el.primary.addEventListener('click', photo);
    if (el.rec) el.rec.addEventListener('click', toggleRecording);
    if (el.reload) el.reload.addEventListener('click', loadFiles);
    if (el.img) el.img.addEventListener('click', startStream);
    el.back.addEventListener('click', function () { if (!el.back.disabled) goPrev(); });
    el.next.addEventListener('click', function () { if (!el.next.disabled) goNext(); });
    if (window.MU) {
      MU.strip(el.navStrip, setStep);
      MU.swipe(document.getElementById('gm'), goNext, goPrev);
    }
    if (window.MPSystem) MPSystem.setBlocker(function () { return !!st.recording; });
  }

  window.__test__ = window.__test__ || {};
  window.__test__.cmInit = init;
  window.__test__.cmSetStep = setStep;
  window.__test__.cmPhoto = photo;
  window.__test__.cmRec = toggleRecording;
  window.__test__.cmFiles = loadFiles;
  window.__test__.cmDelete = deleteFile;
  window.__test__.cmState = st;

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
</script>
)CAMTAIL";
#endif  // ENABLE_CAMERA
