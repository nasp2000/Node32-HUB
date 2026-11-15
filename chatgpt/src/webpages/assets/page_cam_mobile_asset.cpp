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
      <button type="button" class="mini" id="cmSort" data-i18n-title="cmSortTip" title="Sort: name / newest">A&#8597;</button>
      <button type="button" class="mini" id="cmReload" data-i18n-title="cmReloadTip" title="Reload the list">&#8635;</button>
    </div>
    <div class="list" id="cmList"></div>
  </section>

)CAMMOBILE";

// Tail: message bar, progress, actions, footer navigation and the page script.
// The shared System screen is printed between HEAD and TAIL.
const char PAGE_CAM_MOBILE_TAIL[] PROGMEM = R"CAMTAIL(  <!-- ── 3: camera modes (same options as the desktop page) ───────── -->
  <section class="screen" id="gmS3">
    <div class="rowhdr">
      <span class="dir" data-i18n="cmOptTitle">Camera modes</span>
      <button type="button" class="mini" id="cmOptReload" data-i18n-title="cmOptReload" title="Reload">&#8635;</button>
    </div>
    <div class="subbar">
      <button type="button" class="subt on" data-cam-tab="img" data-i18n="cmTabImg">Image</button>
      <button type="button" class="subt" data-cam-tab="rec" data-i18n="cmTabRec">Rec</button>
      <button type="button" class="subt" data-cam-tab="mot" data-i18n="cmTabMot">Motion</button>
      <button type="button" class="subt" data-cam-tab="vis" data-i18n="cmTabVis">Vision</button>
    </div>

    <div class="pane on" id="cmPaneImg">
      <div class="chips" id="cmProfiles">
        <button type="button" class="chip" data-profile="day" data-i18n="cmProfDay">DAY</button>
        <button type="button" class="chip" data-profile="night" data-i18n="cmProfNight">NIGHT</button>
        <button type="button" class="chip" data-profile="fast" data-i18n="cmProfFast">FAST</button>
        <button type="button" class="chip" data-profile="max" data-i18n="cmProfMax">MAX</button>
      </div>
      <div class="opts">
        <label class="opt"><span data-i18n="cmOptRes">Resolution</span><select id="cmRes"></select></label>
        <label class="opt"><span data-i18n="cmOptFps">FPS</span><select id="cmFps"></select></label>
        <label class="opt"><span data-i18n="cmOptQuality">Quality</span>
          <input type="range" id="cmQ" min="4" max="63" step="1"><b id="cmQv">--</b></label>
        <div class="opt"><span data-i18n="cmOptBright">Brightness</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="brightness">&#8722;</button><b id="cmBr">0</b><button type="button" data-step="1" data-key="brightness">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptContrast">Contrast</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="contrast">&#8722;</button><b id="cmCt">0</b><button type="button" data-step="1" data-key="contrast">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptSaturation">Saturation</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="saturation">&#8722;</button><b id="cmSa">0</b><button type="button" data-step="1" data-key="saturation">+</button></div></div>
        <div class="chips">
          <button type="button" class="chip" id="cmMirror" data-i18n="cmOptMirror">Mirror</button>
          <button type="button" class="chip" id="cmFlip" data-i18n="cmOptFlip">Flip</button>
          <button type="button" class="chip" id="cmLamp" data-i18n="cmOptLamp">Lamp</button>
        </div>
        <div class="chips">
          <button type="button" class="chip" id="cmAwb">AWB</button>
          <button type="button" class="chip" id="cmAec">AEC</button>
          <button type="button" class="chip" id="cmAgc">AGC</button>
          <button type="button" class="chip" id="cmLenc">LENC</button>
        </div>
        <label class="opt"><span data-i18n="cmOptEffect">Effect</span><select id="cmEffect">
          <option value="0">None</option><option value="1">Neg</option><option value="2">Gray</option>
          <option value="3">Red</option><option value="4">Green</option><option value="5">Blue</option>
          <option value="6">Sepia</option>
        </select></label>
        <label class="opt"><span data-i18n="cmOptWb">White bal.</span><select id="cmWb">
          <option value="0">Auto</option><option value="1">Sun</option><option value="2">Cloud</option>
          <option value="3">Off</option><option value="4">Home</option>
        </select></label>
        <div class="opt"><span data-i18n="cmOptAe">Exposure</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="ae_level">&#8722;</button><b id="cmAe">0</b><button type="button" data-step="1" data-key="ae_level">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptAecV">AEC value</span>
          <div class="stepper"><button type="button" data-step="-50" data-key="aec_value">&#8722;</button><b id="cmAecV">0</b><button type="button" data-step="50" data-key="aec_value">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptAgcG">AGC gain</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="agc_gain">&#8722;</button><b id="cmAgcG">0</b><button type="button" data-step="1" data-key="agc_gain">+</button></div></div>
        <label class="opt"><span data-i18n="cmOptGc">Gain ceiling</span><select id="cmGc">
          <option value="0">0</option><option value="1">1</option><option value="2">2</option>
          <option value="3">3</option><option value="4">4</option><option value="5">5</option><option value="6">6</option>
        </select></label>
        <label class="opt"><span data-i18n="cmOptSrc">Source</span><select id="cmSrc">
          <option value="0">Native</option><option value="1">USB camera</option>
        </select></label>
        <div class="opt"><span data-i18n="cmOptLampLvl">Lamp</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="lamp">&#8722;</button><b id="cmLampLvl">0</b><button type="button" data-step="1" data-key="lamp">+</button></div></div>
      </div>
    </div>

    <div class="pane" id="cmPaneRec">
      <div class="opts">
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
        <div class="opt"><span data-i18n="cmOptDash">Dashcam</span>
          <div class="chips" style="flex:1 1 auto">
            <button type="button" class="chip" id="cmDash" data-i18n="rbOn">ON</button>
            <select id="cmDashMin"><option value="1">1 min</option><option value="5">5 min</option>
              <option value="10">10 min</option><option value="15">15 min</option><option value="30">30 min</option></select>
          </div>
        </div>
        <label class="opt"><span data-i18n="cmOptCodec">Codec</span><select id="cmCodec">
          <option value="0">MJPEG (.avi)</option><option value="1">H.264 (.264)</option>
        </select></label>
        <div class="opt"><span data-i18n="cmOptBitrate">Bitrate</span>
          <div class="stepper"><button type="button" data-step="-250" data-key="h264_bitrate">&#8722;</button><b id="cmBitrate">0</b><button type="button" data-step="250" data-key="h264_bitrate">+</button></div></div>
      </div>
    </div>

    <div class="pane" id="cmPaneMot">
      <div class="opts">
        <div class="opt"><span data-i18n="cmOptMotion">Motion</span>
          <div class="chips" style="flex:1 1 auto">
            <button type="button" class="chip" id="cmMotion" data-i18n="cmOptMotionOn">Detect</button>
            <button type="button" class="chip" id="cmMotionReset" data-i18n="cmMotionReset">Reset</button>
          </div>
        </div>
        <div class="opt"><span data-i18n="cmOptSensitivity">Sensitivity</span>
          <div class="stepper"><button type="button" data-step="-5" data-key="sensitivity">&#8722;</button><b id="cmSens">0</b><button type="button" data-step="5" data-key="sensitivity">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptConfirm">Confirm</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="mot_cf">&#8722;</button><b id="cmMotCf">3</b><button type="button" data-step="1" data-key="mot_cf">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptStopF">Stop after</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="mot_sf">&#8722;</button><b id="cmMotSf">10</b><button type="button" data-step="1" data-key="mot_sf">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptPost">Post record</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="mot_ps">&#8722;</button><b id="cmMotPs">5</b><button type="button" data-step="1" data-key="mot_ps">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptCooldown">Cooldown</span>
          <div class="stepper"><button type="button" data-step="-5" data-key="mot_cd">&#8722;</button><b id="cmMotCd">5</b><button type="button" data-step="5" data-key="mot_cd">+</button></div></div>
        <label class="opt"><span data-i18n="cmOptAction">Action</span><select id="cmMotAct">
          <option value="0">Record video</option><option value="1">Snap photo</option><option value="2">Email alert</option>
        </select></label>
        <div class="chips">
          <button type="button" class="chip" id="cmMotLamp" data-i18n="cmOptMotLamp">Lamp auto</button>
        </div>
      </div>
    </div>

    <div class="pane" id="cmPaneVis">
      <div class="opts">
        <label class="opt"><span data-i18n="cmOptVisMode">Vision</span><select id="cmVisMode">
          <option value="0">Off</option><option value="1">Count</option><option value="2">Defect</option>
        </select></label>
        <div class="opt"><span data-i18n="cmOptVisTh">Threshold</span>
          <div class="stepper"><button type="button" data-step="-5" data-key="vis_th">&#8722;</button><b id="cmVisTh">50</b><button type="button" data-step="5" data-key="vis_th">+</button></div></div>
        <div class="opt"><span data-i18n="cmOptVisN">Burst</span>
          <div class="stepper"><button type="button" data-step="-1" data-key="vis_n">&#8722;</button><b id="cmVisN">10</b><button type="button" data-step="1" data-key="vis_n">+</button></div></div>
        <div class="chips">
          <button type="button" class="chip" id="cmVisBurst" data-i18n="cmOptVisBurst">BURST</button>
          <button type="button" class="chip" id="cmVisReset" data-i18n="cmOptVisReset">RESET</button>
        </div>
      </div>
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
    sort: document.getElementById('cmSort'),
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
    effect: document.getElementById('cmEffect'),
    awb: document.getElementById('cmAwb'),
    aec: document.getElementById('cmAec'),
    agc: document.getElementById('cmAgc'),
    lenc: document.getElementById('cmLenc'),
    gc: document.getElementById('cmGc'),
    src: document.getElementById('cmSrc'),
    lampLvl: document.getElementById('cmLampLvl'),
    aecV: document.getElementById('cmAecV'),
    agcG: document.getElementById('cmAgcG'),
    dash: document.getElementById('cmDash'),
    dashMin: document.getElementById('cmDashMin'),
    codec: document.getElementById('cmCodec'),
    bitrate: document.getElementById('cmBitrate'),
    motCf: document.getElementById('cmMotCf'),
    motSf: document.getElementById('cmMotSf'),
    motPs: document.getElementById('cmMotPs'),
    motCd: document.getElementById('cmMotCd'),
    motAct: document.getElementById('cmMotAct'),
    motLamp: document.getElementById('cmMotLamp'),
    camTabs: document.querySelectorAll('#gmS3 .subt[data-cam-tab]'),
    panes: {
      img: document.getElementById('cmPaneImg'),
      rec: document.getElementById('cmPaneRec'),
      mot: document.getElementById('cmPaneMot'),
      vis: document.getElementById('cmPaneVis')
    },
    wb: document.getElementById('cmWb'),
    ae: document.getElementById('cmAe'),
    profiles: document.getElementById('cmProfiles'),
    visMode: document.getElementById('cmVisMode'),
    visTh: document.getElementById('cmVisTh'),
    visN: document.getElementById('cmVisN'),
    visBurst: document.getElementById('cmVisBurst'),
    visReset: document.getElementById('cmVisReset'),
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
    step: 1, busy: false, recording: false, files: [], sort: 'name',
    info: null, lastShot: 0, timer: 0, streamTimer: 0,
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
    el.img.style.visibility = 'hidden';
    // Re-assigning src is what makes the browser open a new MJPEG connection.
    el.img.src = streamUrl();
    if (el.info) el.info.textContent = t('cmConnecting', 'Connecting to the camera\u2026');
    clearTimeout(st.streamTimer);
    st.streamTimer = setTimeout(function () {
      if (el.img && el.img.naturalWidth > 0) {
        el.img.style.visibility = 'visible';
      } else if (el.info) {
        el.info.textContent = t('cmNoStream', 'No image from /cam/stream \u2014 tap to retry.');
      }
    }, 4000);
  }

  function toggleFull() {
    if (!el.view) return false;
    var on = el.view.className.indexOf('immersive') < 0;
    // Native fullscreen first (Android/Chrome hides the URL bar and the system
    // bars); the CSS "immersive" class stays as the fallback (iOS refuses it).
    try {
      if (on) {
        var req = el.view.requestFullscreen || el.view.webkitRequestFullscreen || el.view.webkitRequestFullScreen;
        if (req) { var pr = req.call(el.view); if (pr && pr.catch) pr.catch(function () {}); }
      } else if (document.fullscreenElement || document.webkitFullscreenElement) {
        var ex = document.exitFullscreen || document.webkitExitFullscreen;
        if (ex) { var pe = ex.call(document); if (pe && pe.catch) pe.catch(function () {}); }
      }
    } catch (e) {}
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

  // "YYYY-MM-DD_HHMMSS_..." at the start of the name → sortable number.
  function fileStamp(f) {
    var m = String((f && f.n) || '').match(/(\d{4})-(\d{2})-(\d{2})[_-](\d{2})(\d{2})(\d{2})/);
    if (!m) return 0;
    return Number(m[1] + m[2] + m[3] + m[4] + m[5] + m[6]);
  }

  function renderFiles() {
    if (!el.list) return;
    if (typeof document === 'undefined' || !document) return;
    el.list.innerHTML = '';
    st.files.sort(function (a, b) {
      if (st.sort === 'date') {
        var da = fileStamp(a), db = fileStamp(b);
        if (da !== db) return db - da;              // newest first
      }
      var an = String(a && a.n || '').toLowerCase();
      var bn = String(b && b.n || '').toLowerCase();
      return an < bn ? -1 : (an > bn ? 1 : 0);
    });
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
    if (el.effect) el.effect.value = String(c.special_effect !== undefined ? c.special_effect : 0);
    if (el.gc) el.gc.value = String(c.gainceiling !== undefined ? c.gainceiling : 0);
    if (el.src) el.src.value = String(c.source !== undefined ? c.source : 0);
    if (el.lampLvl) el.lampLvl.textContent = String(c.lamp !== undefined ? c.lamp : 0);
    if (el.aecV) el.aecV.textContent = String(c.aec_value !== undefined ? c.aec_value : 204);
    if (el.agcG) el.agcG.textContent = String(c.agc_gain !== undefined ? c.agc_gain : 5);
    if (el.dash) el.dash.className = 'chip' + (c.dashcam_mins ? ' on' : '');
    if (el.dashMin) el.dashMin.value = String(c.dashcam_mins || 5);
    if (el.codec) el.codec.value = String(c.rec_codec !== undefined ? c.rec_codec : 0);
    if (el.bitrate) el.bitrate.textContent = String(c.h264_bitrate || 2000);
    var mo = c.motion || {};
    if (el.motCf) el.motCf.textContent = String(mo.confirm_frames !== undefined ? mo.confirm_frames : 3);
    if (el.motSf) el.motSf.textContent = String(mo.stop_frames !== undefined ? mo.stop_frames : 10);
    if (el.motPs) el.motPs.textContent = String(mo.post_record_secs !== undefined ? mo.post_record_secs : 5);
    if (el.motCd) el.motCd.textContent = String(mo.cooldown_secs !== undefined ? mo.cooldown_secs : 5);
    if (el.motAct) el.motAct.value = String(mo.action !== undefined ? mo.action : 0);
    if (el.motLamp) el.motLamp.className = 'chip' + (mo.lamp_auto ? ' on' : '');
    if (el.visTh) el.visTh.textContent = String(c.vision_thresh !== undefined ? c.vision_thresh : 50);
    if (el.visN) el.visN.textContent = String(c.vision_burst_n !== undefined ? c.vision_burst_n : 10);
    if (el.visMode) el.visMode.value = String(c.vision_mode !== undefined ? c.vision_mode : 0);
    [el.awb, el.aec, el.agc, el.lenc].forEach(function (node, i) {
      if (!node) return;
      var key = ['awb', 'aec', 'agc', 'lenc'][i];
      node.className = 'chip' + (c[key] ? ' on' : '');
    });
    if (el.wb) el.wb.value = String(c.wb_mode !== undefined ? c.wb_mode : 0);
    if (el.ae) el.ae.textContent = String(c.ae_level !== undefined ? c.ae_level : 0);
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
    if (el.effect) body.special_effect = el.effect.value;
    if (el.wb) body.wb_mode = el.wb.value;
    if (el.ae) body.ae_level = Number(el.ae.textContent || 0);
    if (el.gc) body.gainceiling = el.gc.value;
    if (el.src) body.source = el.src.value;
    if (el.lampLvl) body.lamp = Number(el.lampLvl.textContent || 0);
    if (el.aecV) body.aec_value = Number(el.aecV.textContent || 0);
    if (el.agcG) body.agc_gain = Number(el.agcG.textContent || 0);
    if (el.awb) body.awb = el.awb.className.indexOf('on') >= 0 ? 1 : 0;
    if (el.aec) body.aec = el.aec.className.indexOf('on') >= 0 ? 1 : 0;
    if (el.agc) body.agc = el.agc.className.indexOf('on') >= 0 ? 1 : 0;
    if (el.lenc) body.lenc = el.lenc.className.indexOf('on') >= 0 ? 1 : 0;
    if (el.dash) body.dashcam_mins = (el.dash.className.indexOf('on') >= 0) ? Number(el.dashMin ? el.dashMin.value : 5) : 0;
    if (el.codec) body.rec_codec = el.codec.value;
    if (el.bitrate) body.h264_bitrate = Number(el.bitrate.textContent || 0);
    if (el.motCf) body.motion_confirm_frames = Number(el.motCf.textContent || 0);
    if (el.motSf) body.motion_stop_frames = Number(el.motSf.textContent || 0);
    if (el.motPs) body.motion_post_record_secs = Number(el.motPs.textContent || 0);
    if (el.motCd) body.motion_cooldown_secs = Number(el.motCd.textContent || 0);
    if (el.motAct) body.motion_action = el.motAct.value;
    if (el.motLamp) body.motion_lamp_auto = (el.motLamp.className.indexOf('on') >= 0) ? 1 : 0;
    if (el.visMode) body.vision_mode = el.visMode.value;
    if (el.visTh) body.vision_thresh = Number(el.visTh.textContent || 0);
    if (el.visN) body.vision_burst_n = Number(el.visN.textContent || 0);
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

  // Quick profiles (the operator still presses SAVE: nothing is written blind).
  var PROFILES = {
    day:   { fps: 15, quality: 12, brightness: 0, contrast: 0, saturation: 0, effect: 0, wb: 1, ae: 0 },
    night: { fps: 10, quality: 16, brightness: 1, contrast: 1, saturation: 0, effect: 0, wb: 0, ae: 1 },
    fast:  { fps: 30, quality: 22, brightness: 0, contrast: 0, saturation: 0, effect: 0, wb: 0, ae: 0 },
    max:   { fps: 10, quality: 4,  brightness: 0, contrast: 1, saturation: 1, effect: 0, wb: 1, ae: 0 }
  };

  function applyProfile(name) {
    var pr = PROFILES[name];
    if (!pr) return false;
    if (el.fps) el.fps.value = String(pr.fps);
    if (el.quality) { el.quality.value = String(pr.quality); if (el.qualityVal) el.qualityVal.textContent = String(pr.quality); }
    if (el.br) el.br.textContent = String(pr.brightness);
    if (el.ct) el.ct.textContent = String(pr.contrast);
    if (el.sa) el.sa.textContent = String(pr.saturation);
    if (el.effect) el.effect.value = String(pr.effect);
    if (el.wb) el.wb.value = String(pr.wb);
    if (el.ae) el.ae.textContent = String(pr.ae);
    if (el.profiles) {
      var chips = el.profiles.querySelectorAll('button[data-profile]');
      for (var i = 0; i < chips.length; i++) {
        chips[i].className = 'chip' + (String(chips[i].getAttribute('data-profile')) === name ? ' on' : '');
      }
    }
    msg(tf('cmProfileLoaded', 'Profile {name} loaded \u2014 press SAVE to apply.',
           { name: name.toUpperCase() }), '');
    return true;
  }

  function camTab(name) {
    var which = (name === 'rec' || name === 'mot' || name === 'vis') ? name : 'img';
    el.camTabs.forEach(function (b) {
      b.className = 'subt' + (String(b.getAttribute('data-cam-tab')) === which ? ' on' : '');
    });
    for (var k in el.panes) {
      if (el.panes[k]) el.panes[k].className = 'pane' + (k === which ? ' on' : '');
    }
    st.camTab = which;
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
    if (el.sort) el.sort.addEventListener('click', function () {
      st.sort = (st.sort === 'name') ? 'date' : 'name';
      el.sort.textContent = (st.sort === 'name') ? 'A\u2197' : '\u23F1';
      msg(t(st.sort === 'name' ? 'gmSdSortName' : 'gmSdSortNewest',
            st.sort === 'name' ? 'Sorted by name.' : 'Newest first.'), '');
      renderFiles();
    });
    if (el.optReload) el.optReload.addEventListener('click', loadOptions);
    el.camTabs.forEach(function (b) {
      b.addEventListener('click', function () { camTab(String(b.getAttribute('data-cam-tab'))); });
    });
    [el.dash, el.motLamp].forEach(function (b) {
      if (b) b.addEventListener('click', function () {
        b.className = (b.className.indexOf('on') >= 0) ? 'chip' : 'chip on';
      });
    });
    if (el.visBurst) el.visBurst.addEventListener('click', function () {
      MU.post('/api/camera/vision/burst', {}).then(function () {
        msg(t('cmVisBurstOk', 'Burst requested.'), 'ok');
      }).catch(function () {});
    });
    if (el.visReset) el.visReset.addEventListener('click', function () {
      MU.post('/api/camera/vision/reset', {}).then(function () {
        msg(t('cmVisResetOk', 'Vision counters cleared.'), 'ok');
      }).catch(function () {});
    });
    if (el.profiles) {
      el.profiles.addEventListener('click', function (ev) {
        var b = ev.target && ev.target.closest ? ev.target.closest('button[data-profile]') : null;
        if (b) applyProfile(String(b.getAttribute('data-profile')));
      });
    }
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
          var LIM = {
            brightness: [-2, 2], contrast: [-2, 2], saturation: [-2, 2], ae_level: [-2, 2],
            sensitivity: [1, 100], aec_value: [0, 1200], agc_gain: [0, 30], lamp: [0, 15],
            mot_cf: [1, 20], mot_sf: [2, 30], mot_ps: [0, 30], mot_cd: [0, 120],
            h264_bitrate: [500, 8000], vis_th: [0, 100], vis_n: [1, 50]
          };
          var NODE = {
            brightness: el.br, contrast: el.ct, saturation: el.sa, ae_level: el.ae,
            sensitivity: el.sens, aec_value: el.aecV, agc_gain: el.agcG, lamp: el.lampLvl,
            mot_cf: el.motCf, mot_sf: el.motSf, mot_ps: el.motPs, mot_cd: el.motCd,
            h264_bitrate: el.bitrate, vis_th: el.visTh, vis_n: el.visN
          };
          var lim = LIM[key]; var node = NODE[key];
          if (lim && node) {
            var v0 = (parseInt(node.textContent, 10) || 0) + delta;
            if (v0 < lim[0]) v0 = lim[0];
            if (v0 > lim[1]) v0 = lim[1];
            node.textContent = String(v0);
            return;
          }
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
    if (el.img) {
      el.img.addEventListener('click', startStream);
      el.img.addEventListener('load', function () { el.img.style.visibility = 'visible'; });
    }
    ['fullscreenchange', 'webkitfullscreenchange'].forEach(function (ev) {
      document.addEventListener(ev, function () {
        var native = !!(document.fullscreenElement || document.webkitFullscreenElement);
        if (!native && el.view && el.view.className.indexOf('immersive') >= 0) {
          el.view.className = 'viewwrap';
          if (el.full) { el.full.innerHTML = '&#9974;'; }
        }
      });
    });
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
  window.__test__.cmProfile = applyProfile;
  window.__test__.cmTab = camTab;
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
