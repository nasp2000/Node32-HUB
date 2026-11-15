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
  #gm .fullbtn {
    position: absolute; top: 8px; right: 8px; width: 38px; height: 38px;
    border-radius: 10px; border: 1px solid rgba(148,163,184,.5); background: rgba(2,6,23,.55);
    color: #e6f1ff; font-size: 18px; line-height: 1;
  }
  /* immersive: the view covers the page (no Fullscreen API needed, works on iOS) */
  #gm .viewwrap.immersive {
    position: fixed; inset: 0; z-index: 80; border: 0; border-radius: 0;
    /* the page sets aspect-ratio inline from the stream resolution; the
       full-screen box must ignore it */
    aspect-ratio: auto !important; max-height: none; width: auto; background: #000;
  }
  #gm .viewwrap.immersive img { max-width: 100vw; max-height: 100vh; width: auto; height: auto; }
  #gm .viewwrap.immersive .fullbtn { top: 14px; right: 14px; width: 44px; height: 44px; }

  #gm .row a.n { color: #f1f5f9; text-decoration: none; font-weight: 700; }
  #gm .row a.n:active { color: #cbd5e1; }
  #gm .row .s { color: #94a3b8; }
  #gm .row .mini {
    color: #fca5a5; border-color: #7f1d1d; background: #1a0f12; font-size: 13px;
  }
  #gm .row .mini:active { background: #7f1d1d; color: #fff; }
  #gm .row a.n::after { content: ' \203A'; color: #475569; }
  #gm .row .dl {
    flex: 0 0 auto; width: 34px; height: 32px; display: flex; align-items: center;
    justify-content: center; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #93c5fd; font-size: 15px; text-decoration: none;
  }
  #gm .opts { flex: 0 1 auto; min-height: 0; max-height: 100%; overflow-y: auto; display: flex;
              flex-direction: column; gap: 8px; margin: auto 0; }
  #gm .opt { display: flex; align-items: center; gap: 10px; font-size: 13px; color: #cbd5e1; }
  #gm .opt > span { flex: 0 0 96px; color: #94a3b8; font-size: 12px; }
  #gm .opt select { flex: 1 1 auto; height: 36px; border-radius: 9px; border: 1px solid #334155;
                    background: #0f172a; color: #e6f1ff; font-size: 13px; padding: 0 8px; }
  #gm .opt input[type=range] { flex: 1 1 auto; accent-color: #3b82f6; }
  #gm .opt b { flex: 0 0 28px; text-align: center; color: #93c5fd; font-size: 13px; }
  #gm .stepper { flex: 1 1 auto; display: flex; align-items: center; gap: 8px; }
  #gm .stepper button { width: 40px; height: 36px; border-radius: 9px; border: 1px solid #334155;
                        background: #0f172a; color: #cbd5e1; font-size: 16px; }
  #gm .stepper b { flex: 1 1 auto; text-align: center; color: #93c5fd; }
</style>
<div id="gm" class="mp">
  <header>
    <nav class="navstrip" id="gmNavStrip">
      <button type="button" class="ncol on" data-step-go="1"><span class="ntab" data-i18n="cmNavLive">Live</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="2"><span class="ntab" data-i18n="cmNavFiles">Files</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="3"><span class="ntab" data-i18n="cmNavCam">Camera</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="4"><span class="ntab" data-i18n="cmNavFtp">FTP</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="5"><span class="ntab" data-i18n="cmNavSys">System</span><i class="nrail"></i></button>
    </nav>
    <div class="hrow">
      <span class="pack" id="gmPack"></span>
      <h1 id="gmTitle" data-i18n="cmTitleLive">Live</h1>
      <span class="badge" id="gmLink">--</span>
    </div>
  </header>

  <!-- ── 1: live ─────────────────────────────────────────────────── -->
  <section class="screen on" id="gmS1">
    <div class="viewwrap" id="cmView">
      <img id="cmImg" alt="live" src="">
      <div class="viewinfo"><span id="cmInfo">--</span></div>
      <button type="button" class="fullbtn" id="cmFull" data-i18n-title="cmFullscreen" title="Full screen">&#9974;</button>
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
const char PAGE_CAM_MOBILE_TAIL[] PROGMEM = R"CAMTAIL(  <!-- ── 3: camera options / modes ───────────────────────────────── -->
  <section class="screen" id="gmS3">
    <div class="rowhdr">
      <span class="dir" data-i18n="cmOptTitle">Camera modes</span>
      <button type="button" class="mini" id="cmOptReload" data-i18n-title="cmOptReload" title="Reload">&#8635;</button>
    </div>
    <div class="opts" id="cmOpts">
      <label class="opt"><span data-i18n="cmOptRes">Resolution</span><select id="cmRes"></select></label>
      <label class="opt"><span data-i18n="cmOptFps">FPS</span><select id="cmFps"></select></label>
      <label class="opt"><span data-i18n="cmOptQuality">Quality</span>
        <input type="range" id="cmQ" min="0" max="63" step="1"><b id="cmQv">--</b></label>
      <div class="opt"><span data-i18n="cmOptBright">Brightness</span>
        <div class="stepper"><button type="button" data-step="-1" data-key="brightness">&#8722;</button><b id="cmBr">0</b><button type="button" data-step="1" data-key="brightness">+</button></div></div>
      <div class="opt"><span data-i18n="cmOptContrast">Contrast</span>
        <div class="stepper"><button type="button" data-step="-1" data-key="contrast">&#8722;</button><b id="cmCt">0</b><button type="button" data-step="1" data-key="contrast">+</button></div></div>
      <div class="opt"><span data-i18n="cmOptSaturation">Saturation</span>
        <div class="stepper"><button type="button" data-step="-1" data-key="saturation">&#8722;</button><b id="cmSa">0</b><button type="button" data-step="1" data-key="saturation">+</button></div></div>
      <div class="opt"><span data-i18n="cmOptMode">Mode</span>
        <div class="chips" id="cmModes">
          <button type="button" class="chip" data-mode="0" data-i18n="cmModeOff">Off</button>
          <button type="button" class="chip" data-mode="1" data-i18n="cmModePhoto">Photo</button>
          <button type="button" class="chip" data-mode="2" data-i18n="cmModeVideo">Video</button>
          <button type="button" class="chip" data-mode="3" data-i18n="cmModeMotion">Motion</button>
          <button type="button" class="chip" data-mode="4" data-i18n="cmModeLapse">Lapse</button>
        </div>
      </div>
      <label class="opt"><span data-i18n="cmOptTimelapse">Timelapse</span><select id="cmLapse"></select></label>
      <div class="chips">
        <button type="button" class="chip" id="cmMirror" data-i18n="cmOptMirror">Mirror</button>
        <button type="button" class="chip" id="cmFlip" data-i18n="cmOptFlip">Flip</button>
        <button type="button" class="chip" id="cmLamp" data-i18n="cmOptLamp">Lamp</button>
      </div>
      <div class="opt"><span data-i18n="cmOptMotion">Motion</span>
        <div class="chips" style="flex:1 1 auto">
          <button type="button" class="chip" id="cmMotion" data-i18n="cmOptMotionOn">Detect</button>
          <button type="button" class="chip" id="cmMotionReset" data-i18n="cmMotionReset">Reset</button>
        </div>
      </div>
      <div class="opt"><span data-i18n="cmOptSensitivity">Sensitivity</span>
        <div class="stepper"><button type="button" data-step="-5" data-key="sensitivity">&#8722;</button><b id="cmSens">0</b><button type="button" data-step="5" data-key="sensitivity">+</button></div></div>
    </div>
  </section>

  <!-- ── 4: ftp ──────────────────────────────────────────────────── -->
  <section class="screen" id="gmS4">
    <div class="rowhdr">
      <span class="dir" data-i18n="cmFtpTitle">FTP uploads</span>
      <button type="button" class="mini" id="cmFtpReload" data-i18n-title="cmOptReload" title="Reload">&#8635;</button>
    </div>
    <div class="tiles">
      <div class="tile"><span class="k" data-i18n="cmFtpHost">Server</span><b id="cmFtpHost">--</b></div>
      <div class="tile"><span class="k" data-i18n="cmFtpPath">Remote folder</span><b id="cmFtpPath">--</b></div>
      <div class="tile"><span class="k" data-i18n="cmFtpLast">Last send</span><b id="cmFtpLast">--</b></div>
      <div class="tile"><span class="k" data-i18n="cmFtpCount">Sent / failed</span><b id="cmFtpCount">--</b></div>
    </div>
    <div class="chips">
      <button type="button" class="chip" id="cmFtpSend" data-i18n="cmFtpSend">SEND NOW</button>
      <button type="button" class="chip" id="cmFtpTest" data-i18n="cmFtpTest">TEST</button>
    </div>
    <div class="list" id="cmFtpList"></div>
  </section>

  <div class="msg" id="gmMsg" data-i18n="cmStep1Hint">Live view. Photo and video stay on the SD card.</div>
  <div class="bar" id="gmBar"><span id="gmFill"></span></div>

  <div class="acts">
    <button type="button" class="primary" id="gmPrimary" data-i18n="cmPhoto">PHOTO</button>
    <button type="button" class="recbtn" id="cmRec" data-i18n="cmRecord">REC</button>
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
  var STEPS = 5;
  var STEP_LIVE = 1, STEP_FILES = 2, STEP_CAM = 3, STEP_FTP = 4, STEP_SYS = 5;
  var MAX_BYTES_TXT = 0;

  var el = {
    title: document.getElementById('gmTitle'),
    navStrip: document.getElementById('gmNavStrip'),
    screens: [document.getElementById('gmS1'), document.getElementById('gmS2'),
              document.getElementById('gmS3'), document.getElementById('gmS4'),
              document.getElementById('gmSYS')],
    pack: document.getElementById('gmPack'),
    link: document.getElementById('gmLink'),
    img: document.getElementById('cmImg'),
    view: document.getElementById('cmView'),
    full: document.getElementById('cmFull'),
    info: document.getElementById('cmInfo'),
    dir: document.getElementById('cmDir'),
    list: document.getElementById('cmList'),
    reload: document.getElementById('cmReload'),
    msg: document.getElementById('gmMsg'),
    bar: document.getElementById('gmBar'),
    fill: document.getElementById('gmFill'),
    primary: document.getElementById('gmPrimary'),
    rec: document.getElementById('cmRec'),
    opts: document.getElementById('cmOpts'),
    optReload: document.getElementById('cmOptReload'),
    res: document.getElementById('cmRes'),
    fps: document.getElementById('cmFps'),
    quality: document.getElementById('cmQ'),
    qualityVal: document.getElementById('cmQv'),
    br: document.getElementById('cmBr'),
    ct: document.getElementById('cmCt'),
    sa: document.getElementById('cmSa'),
    mirror: document.getElementById('cmMirror'),
    flip: document.getElementById('cmFlip'),
    lamp: document.getElementById('cmLamp'),
    modes: document.getElementById('cmModes'),
    lapse: document.getElementById('cmLapse'),
    motion: document.getElementById('cmMotion'),
    motionReset: document.getElementById('cmMotionReset'),
    sens: document.getElementById('cmSens'),
    ftpReload: document.getElementById('cmFtpReload'),
    ftpHost: document.getElementById('cmFtpHost'),
    ftpPath: document.getElementById('cmFtpPath'),
    ftpLast: document.getElementById('cmFtpLast'),
    ftpCount: document.getElementById('cmFtpCount'),
    ftpSend: document.getElementById('cmFtpSend'),
    ftpTest: document.getElementById('cmFtpTest'),
    ftpList: document.getElementById('cmFtpList'),
    acts: document.querySelector('#gm .acts'),
    back: document.getElementById('gmBack'),
    next: document.getElementById('gmNext'),
    no: document.getElementById('gmNo')
  };

  var st = {
    step: 1, busy: false, recording: false, files: [],
    info: null, lastShot: 0, timer: 0,
    cfg: null, resList: [], fpsMax: 30
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

  function toggleFull() {
    if (!el.view) return false;
    var on = el.view.className.indexOf('immersive') < 0;
    el.view.className = 'viewwrap' + (on ? ' immersive' : '');
    if (el.full) {
      el.full.innerHTML = on ? '&#10005;' : '&#9974;';
      el.full.title = on ? t('cmExitFullscreen', 'Exit full screen') : t('cmFullscreen', 'Full screen');
    }
    return on;
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
      var dl = document.createElement('a');
      dl.className = 'dl';
      dl.href = '/cam/playback?f=' + encodeURIComponent(String(f.n || ''));
      dl.setAttribute('download', String(f.n || '').replace(/^.*\//, ''));
      dl.title = t('cmDownload', 'Download');
      dl.textContent = '\u2B07';
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
      row.appendChild(dl);
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

  // ── camera modes ─────────────────────────────────────────────────────
  var FRAMESIZES = [
    [1, 'QQVGA 160x120'], [2, 'HQVGA 240x176'], [3, 'QVGA 320x240'],
    [4, 'CIF 400x296'], [5, 'VGA 640x480'], [6, 'SVGA 800x600'],
    [7, 'XGA 1024x768'], [8, 'HD 1280x720'], [9, 'SXGA 1280x1024'], [10, 'UXGA 1600x1200']
  ];

  function fillResolutions() {
    if (!el.res) return;
    el.res.innerHTML = '';
    var list = (st.resList && st.resList.length) ? st.resList : null;
    var i, o;
    if (list) {
      for (i = 0; i < list.length; i++) {
        o = document.createElement('option');
        o.value = list[i];
        o.textContent = list[i];
        el.res.appendChild(o);
      }
    } else {
      for (i = 0; i < FRAMESIZES.length; i++) {
        o = document.createElement('option');
        o.value = String(FRAMESIZES[i][0]);
        o.textContent = FRAMESIZES[i][1];
        el.res.appendChild(o);
      }
    }
  }

  function fillFps() {
    if (!el.fps) return;
    el.fps.innerHTML = '';
    var max = Math.max(1, Math.min(30, Number(st.fpsMax) || 30));
    for (var v = 1; v <= max; v++) {
      var o = document.createElement('option');
      o.value = String(v);
      o.textContent = String(v);
      el.fps.appendChild(o);
    }
  }

  var LAPSE_STEPS = [0, 5, 10, 30, 60, 300, 900];

  function fillLapse() {
    if (!el.lapse) return;
    el.lapse.innerHTML = '';
    for (var i = 0; i < LAPSE_STEPS.length; i++) {
      var o = document.createElement('option');
      o.value = String(LAPSE_STEPS[i]);
      o.textContent = LAPSE_STEPS[i] === 0 ? t('cmOff', 'off') : (String(LAPSE_STEPS[i]) + ' s');
      el.lapse.appendChild(o);
    }
  }

  function setMode(mode) {
    if (!el.modes) return;
    var b = el.modes.querySelectorAll('button[data-mode]');
    for (var i = 0; i < b.length; i++) {
      b[i].className = 'chip' + (String(b[i].getAttribute('data-mode')) === String(mode) ? ' on' : '');
    }
  }

  function renderCfg() {
    var c = st.cfg || {};
    setMode(c.auto_mode !== undefined ? c.auto_mode : 0);
    if (el.lapse) el.lapse.value = String(c.timelapse_secs || 0);
    if (el.motion) el.motion.className = 'chip' + ((c.motion && c.motion.enabled) ? ' on' : '');
    if (el.sens) el.sens.textContent = String((c.motion && c.motion.sensitivity) || 0);
    if (el.res) {
      if (st.resList && st.resList.length) {
        var want = String(st.info && st.info.p4_resolution ? st.info.p4_resolution : (st.resList[0] || ''));
        if (want) el.res.value = want;
      } else if (c.framesize !== undefined) {
        el.res.value = String(c.framesize);
      }
    }
    if (el.fps) el.fps.value = String(c.fps !== undefined ? c.fps : 15);
    if (el.quality) { el.quality.value = String(c.quality !== undefined ? c.quality : 12); }
    if (el.qualityVal) el.qualityVal.textContent = el.quality ? el.quality.value : '--';
    if (el.br) el.br.textContent = String(c.brightness !== undefined ? c.brightness : 0);
    if (el.ct) el.ct.textContent = String(c.contrast !== undefined ? c.contrast : 0);
    if (el.sa) el.sa.textContent = String(c.saturation !== undefined ? c.saturation : 0);
    if (el.mirror) el.mirror.className = 'chip' + (c.hmirror ? ' on' : '');
    if (el.flip) el.flip.className = 'chip' + (c.vflip ? ' on' : '');
    if (el.lamp) {
      el.lamp.style.display = (c.lampAvail === false) ? 'none' : '';
      el.lamp.className = 'chip' + (c.lamp ? ' on' : '');
    }
  }

  function loadOptions() {
    if (!window.MU) return;
    return Promise.all([MU.get('/api/camera/config'), MU.get('/api/camera/status')]).then(function (r) {
      st.cfg = r[0].body || {};
      st.info = r[1].body || st.info;
      st.resList = [];
      var rl = String(st.info.resList || '');
      if (rl) { var parts = rl.split(','); for (var i = 0; i < parts.length; i++) if (parts[i]) st.resList.push(parts[i].trim()); }
      st.fpsMax = Number(st.info.fpsMax) || 30;
      fillResolutions();
      fillFps();
      fillLapse();
      renderCfg();
    }).catch(function () {});
  }

  function saveOptions() {
    if (!st.cfg) return false;
    var body = { fps: el.fps ? el.fps.value : undefined, quality: el.quality ? el.quality.value : undefined };
    if (st.resList.length) {
      body.resolution = el.res ? el.res.value : '';
    } else if (el.res) {
      body.framesize = el.res.value;
    }
    body.brightness = Number((el.br && el.br.textContent) || 0);
    body.contrast = Number((el.ct && el.ct.textContent) || 0);
    body.saturation = Number((el.sa && el.sa.textContent) || 0);
    body.auto_mode = (function () {
      var on = el.modes ? el.modes.querySelector('.chip.on') : null;
      return on ? parseInt(String(on.getAttribute('data-mode')), 10) : 0;
    })();
    if (el.lapse) body.timelapse_secs = el.lapse.value;
    body.motion_enabled = (el.motion && el.motion.className.indexOf('on') >= 0) ? 1 : 0;
    body.motion_sensitivity = Number((el.sens && el.sens.textContent) || 0);
    body.hmirror = (el.mirror && el.mirror.className.indexOf('on') >= 0) ? 1 : 0;
    body.vflip = (el.flip && el.flip.className.indexOf('on') >= 0) ? 1 : 0;
    if (el.lamp && el.lamp.style.display !== 'none') {
      body.lamp = (el.lamp.className.indexOf('on') >= 0) ? 1 : 0;
    }
    st.busy = true; render();
    msg(t('cmSaving', 'Saving\u2026'), '');
    return MU.post('/api/camera/config', body).then(function (res) {
      st.busy = false;
      var ok = !!(res.body && (res.body.success === true || res.body.ok === true));
      msg(ok ? t('cmSaved', 'Camera settings saved.') : t('cmSaveFail', 'The camera refused the settings.'),
          ok ? 'ok' : 'err');
      if (ok) loadOptions();
      render();
      return ok;
    }).catch(function () {
      st.busy = false; render();
      msg(t('cmSaveFail', 'The camera refused the settings.'), 'err');
      return false;
    });
  }

  // ── FTP ─────────────────────────────────────────────────────────────
  function renderFtp(d) {
    var on = !!(d && d.enabled !== false);
    var host = d && d.host ? (String(d.host) + (d.port ? ':' + String(d.port) : '')) : t('cmFtpOff', 'not configured');
    if (el.ftpHost) el.ftpHost.textContent = host;
    if (el.ftpPath) el.ftpPath.textContent = (d && d.remote_path) ? String(d.remote_path) : '--';
    if (el.ftpLast) {
      el.ftpLast.textContent = (d && d.last_file) ? String(d.last_file) : t('cmFtpNone', 'nothing yet');
    }
    if (el.ftpCount) el.ftpCount.textContent = String((d && d.ok_count) || 0) + ' / ' + String((d && d.fail_count) || 0);
    if (el.ftpSend) el.ftpSend.disabled = !on || st.busy;
    if (el.ftpTest) el.ftpTest.disabled = !on || st.busy;
    if (!el.ftpList) return;
    el.ftpList.innerHTML = '';
    var recent = (d && Array.isArray(d.recent)) ? d.recent : [];
    if (!recent.length) {
      var none = document.createElement('div');
      none.className = 'note';
      none.textContent = on ? t('cmFtpEmpty', 'No uploads yet.')
                            : t('cmFtpDisabled', 'FTP is off (enable it on the desktop page).');
      el.ftpList.appendChild(none);
      return;
    }
    for (var i = 0; i < recent.length; i++) {
      var r = recent[i] || {};
      var row = document.createElement('div');
      row.className = 'row';
      var n = document.createElement('span');
      n.className = 'n';
      n.textContent = String(r.file || r.name || '--');
      var s2 = document.createElement('span');
      s2.className = 's';
      s2.textContent = (r.ok === false ? '\u2717 ' : '\u2713 ') + String(r.time_str || r.when || '');
      row.appendChild(n);
      row.appendChild(s2);
      el.ftpList.appendChild(row);
    }
  }

  function loadFtp() {
    if (!window.MU) return;
    return MU.get('/api/ftp-status').then(function (r) {
      renderFtp(r.body || {});
    }).catch(function () { renderFtp({ enabled: false }); });
  }

  function ftpAction(kind) {
    if (st.busy) return false;
    var url = kind === 'test' ? '/api/ftp-test' : '/api/ftp-upload-now';
    st.busy = true; render();
    msg(t(kind === 'test' ? 'cmFtpTesting' : 'cmFtpSending',
          kind === 'test' ? 'Testing the server\u2026' : 'Sending the pending files\u2026'), '');
    MU.post(url, {}).then(function (res) {
      st.busy = false;
      var ok = !!(res.body && (res.body.success === true || res.body.ok === true));
      msg(ok ? t(kind === 'test' ? 'cmFtpTestOk' : 'cmFtpSendOk',
                 kind === 'test' ? 'Server reachable.' : 'Upload started.')
             : t('cmFtpFail', 'The FTP server refused the request.'), ok ? 'ok' : 'err');
      loadFtp();
      render();
    }).catch(function () {
      st.busy = false; render();
      msg(t('cmFtpFail', 'The FTP server refused the request.'), 'err');
    });
    return true;
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
    if (st.step === STEP_CAM) loadOptions();
    if (st.step === STEP_FTP) loadFtp();
    if (st.step === STEP_LIVE) startStream();
    try { history.replaceState(null, '', '#' + st.step); } catch (e) {}
    msg(t(st.step === STEP_LIVE ? 'cmStep1Hint'
         : st.step === STEP_FILES ? 'cmStep2Hint'
         : st.step === STEP_CAM ? 'cmOptHint'
         : st.step === STEP_FTP ? 'cmFtpHint'
         : 'gmStep6Hint',
         st.step === STEP_LIVE ? 'Live view. Photo and video stay on the SD card.'
         : st.step === STEP_FILES ? 'Tap a recording to play or download it (\u2B07).'
         : st.step === STEP_CAM ? 'Resolution, FPS, image and recording modes.'
         : st.step === STEP_FTP ? 'Send the pending files to the FTP server.'
         : 'Controller health, OTA, log and restart.'), '');
    render();
    return st.step;
  }

  function goNext() { if (st.step < STEPS) setStep(st.step + 1); }
  function goPrev() { if (st.step > 1) setStep(st.step - 1); }

  function render() {
    if (typeof document === 'undefined' || !document) return;
    var is1 = st.step === STEP_LIVE, is2 = st.step === STEP_FILES;
    var is3 = st.step === STEP_CAM, isF = st.step === STEP_FTP, is4 = st.step === STEP_SYS;
    el.title.textContent = t(is1 ? 'cmTitleLive' : is2 ? 'cmTitleFiles' : is3 ? 'cmTitleCam'
                                 : isF ? 'cmTitleFtp' : 'gmTitleSys',
                             is1 ? 'Live' : is2 ? 'Files' : is3 ? 'Camera' : isF ? 'FTP' : 'System');
    var showPrimary = is1 || is3;
    el.primary.className = 'primary' + (showPrimary ? '' : ' off');
    el.primary.textContent = is1 ? t('cmPhoto', 'PHOTO') : t('cmSave', 'SAVE');
    el.primary.disabled = st.busy || !showPrimary;
    if (el.rec) {
      el.rec.style.display = is1 ? '' : 'none';   // REC only on the Live screen
      el.rec.className = 'recbtn' + (st.recording ? ' on' : '');
      el.rec.textContent = st.recording ? t('cmStop', 'STOP') : t('cmRecord', 'REC');
      el.rec.disabled = st.busy || !is1;
    }
    if (el.acts) el.acts.style.display = showPrimary ? 'flex' : 'none';
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
    el.primary.addEventListener('click', function () {
      if (st.step === STEP_CAM) saveOptions();
      else if (st.step === STEP_LIVE) photo();
    });
    if (el.rec) el.rec.addEventListener('click', toggleRecording);
    if (el.reload) el.reload.addEventListener('click', loadFiles);
    if (el.optReload) el.optReload.addEventListener('click', loadOptions);
    if (el.ftpReload) el.ftpReload.addEventListener('click', loadFtp);
    if (el.ftpSend) el.ftpSend.addEventListener('click', function () { ftpAction('send'); });
    if (el.ftpTest) el.ftpTest.addEventListener('click', function () { ftpAction('test'); });
    if (el.motionReset) el.motionReset.addEventListener('click', function () {
      MU.post('/api/camera/motion/reset', {}).then(function () {
        msg(t('cmMotionResetOk', 'Motion events cleared.'), 'ok');
      }).catch(function () {});
    });
    if (el.quality) el.quality.addEventListener('input', function () {
      if (el.qualityVal) el.qualityVal.textContent = el.quality.value;
    });
    if (el.opts) {
      el.opts.addEventListener('click', function (ev) {
        var b = ev.target && ev.target.closest ? ev.target.closest('button[data-step]') : null;
        if (b) {
          var key = String(b.getAttribute('data-key') || '');
          var delta = parseInt(String(b.getAttribute('data-step') || '0'), 10) || 0;
          if (key === 'sensitivity') {
            var v0 = Math.max(0, Math.min(100, (parseInt(el.sens.textContent, 10) || 0) + delta));
            el.sens.textContent = String(v0);
            return;
          }
          var node = key === 'brightness' ? el.br : key === 'contrast' ? el.ct : el.sa;
          var v = Math.max(-2, Math.min(2, (parseInt(node.textContent, 10) || 0) + delta));
          node.textContent = String(v);
          return;
        }
        var mode = ev.target && ev.target.closest ? ev.target.closest('button[data-mode]') : null;
        if (mode) { setMode(mode.getAttribute('data-mode')); return; }
        var t1 = ev.target && ev.target.closest ? ev.target.closest('.chip') : null;
        if (t1) t1.className = (t1.className.indexOf('on') >= 0) ? 'chip' : 'chip on';
      });
    }
    if (el.img) el.img.addEventListener('click', startStream);
    if (el.full) el.full.addEventListener('click', function (ev) {
      ev.stopPropagation();
      toggleFull();
    });
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
  window.__test__.cmOptions = loadOptions;
  window.__test__.cmSave = saveOptions;
  window.__test__.cmFtp = loadFtp;
  window.__test__.cmFtpAction = ftpAction;
  window.__test__.cmFull = toggleFull;
  window.__test__.cmState = st;

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
</script>
)CAMTAIL";
#endif  // ENABLE_CAMERA
