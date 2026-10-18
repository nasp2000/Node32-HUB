// Camera Page Tests — upd (rec time formatting), preset (data lookup), showPsec (string manipulation)
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// Standalone reimplementation of camera page pure logic

// From upd(): REC elapsed time formatting
function fmtRecElapsed(recElapsed) {
  var m = Math.floor(recElapsed / 60);
  var s = recElapsed % 60;
  return (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
}

// From showPsec(): capitalize first letter of section id
function fmtPsecId(id) {
  return id.charAt(0).toUpperCase() + id.slice(1);
}

// From preset(): preset data structure (mirrors page_camera_asset.cpp)
var PRESETS = {
  hd:    {fr:13,q:10,fs:10,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  vga:   {fr:10,q:20,fs:12,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  fast:  {fr:6,q:35,fs:25,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  night: {fr:6,q:15,fs:5,br:1,ct:-1,sa:-1,awb:0,aec:1,agc:1,gc:0},
  safe: {fr:13,q:12,fs:10,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  eco:  {fr:6,q:24,fs:8,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0}
};

function getPreset(n) {
  return PRESETS[n] || PRESETS.vga;
}

// P4 per-sensor presets (mirrors page_camera_asset.cpp P4_OV5647_PRESETS /
// P4_USB_PRESETS, CAM-68). OV5647 = RPi v1.3 MIPI modes from kP4CsiModes
// (never 1280x720 — experimental profile stalls, CAM-62); USB posts
// quality/fps only (geometry is negotiated with the webcam).
var P4_OV5647_PRESETS = {
  hd:    {res:'1920x1080',q:10,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  vga:   {res:'800x640',q:20,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  fast:  {res:'800x640',q:35,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  night: {res:'800x800',q:15,fs:4,br:1,ct:-1,sa:-1,awb:0,aec:1,agc:1,gc:0},
  safe:  {res:'800x800',q:12,fs:5,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  eco:   {res:'800x640',q:24,fs:4,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0}
};
var P4_USB_PRESETS = {
  hd:    {q:10,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  vga:   {q:20,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  fast:  {q:35,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  night: {q:15,fs:4,br:1,ct:-1,sa:-1,awb:0,aec:1,agc:1,gc:0},
  safe:  {q:12,fs:5,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  eco:   {q:24,fs:4,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0}
};
var CAM_RES_MODES = ['1920x1080','1280x960','1280x720','800x1280','800x800','800x640'];

function getP4Preset(n, model, fpsMax) {
  var usb = (model === 'USB-UVC');
  var tab = usb ? P4_USB_PRESETS : P4_OV5647_PRESETS;
  var t = tab[n] || tab.vga;
  var ceiling = fpsMax || 30;
  return { res: t.res, q: t.q, fs: t.fs > ceiling ? ceiling : t.fs,
    br: t.br, ct: t.ct, sa: t.sa, awb: t.awb, aec: t.aec, agc: t.agc, gc: t.gc, usb: usb };
}

function buildP4PresetBody(p) {
  return (p.usb || !p.res ? '' : 'resolution=' + encodeURIComponent(p.res) + '&') +
    'quality=' + p.q + '&fps=' + p.fs +
    '&brightness=' + p.br + '&contrast=' + p.ct + '&saturation=' + p.sa +
    '&awb=' + p.awb + '&aec=' + p.aec + '&agc=' + p.agc + '&gainceiling=' + p.gc;
}

// P4 preset tooltips (mirrors applyPresetTitles): per-sensor titles while a
// P4 sensor is known, null on S3 (static HTML titles kept).
function presetTitles(model, isP4) {
  if (!isP4) return null;
  var usb = (model === 'USB-UVC');
  return {
    hd: usb ? 'HD preset (max quality — resolution fixed by the USB camera)' : 'HD preset (1920x1080)',
    safe: usb ? 'Safe preset (quality/fps only — resolution fixed by the USB camera)' : 'Safe preset (800x800 verified stable)',
    eco: usb ? 'Economy preset (low CPU/bandwidth — resolution fixed by the USB camera)' : 'Economy preset (800x640, lowest CPU/bandwidth)'
  };
}

// From upd(): SD free display (mirrors page logic)
function fmtSdInfo(d) {
  if (d && (d.sdTotal !== undefined && d.sdTotal > 0)) return 'SD ' + d.sdFree + '/' + d.sdTotal + ' MB free';
  return 'SD n/a';
}

// From snapPhoto(): photo saved line (mirrors page logic)
function fmtPhotoInfo(d) {
  if (d && d.success) return 'Saved: ' + (d.filename || 'photo.jpg');
  return null;
}

// From tlCfg(): timelapse body (mirrors page logic)
function buildTimelapseBody(secs) {
  return 'timelapse_secs=' + secs;
}

// From srcCfg(): source body (mirrors page logic)
function buildSrcBody(v) {
  return 'source=' + v;
}

// From tlStop(): stop timelapse body (mirrors page logic)
function buildTlStopBody() {
  return 'timelapse_secs=0';
}

// From upd(): timelapse countdown + last saved path (mirrors page logic)
function fmtTlInfo(d) {
  if (!(d && d.timelapseActive)) return null;
  var s = 'Next capture in ' + d.timelapseNextInSecs + 's';
  if (d.timelapseLast && d.timelapseLast.length) s += ' \u00b7 ' + d.timelapseLast;
  return s;
}

// From cfg(): aec2 null-guard (chkAec2 may be absent in old cached pages)
function fmtAec2(el) {
  return ((el && el.checked) ? 1 : 0);
}

// From upd(): top-bar heap/temp pills (mirrors page logic)
function fmtHeapPill(d) {
  return '🧠 ' + ((d && d.freeHeap !== undefined) ? Math.round(d.freeHeap / 1024) + 'K' : '--');
}
function fmtTempPill(d) {
  return '🌡️ ' + ((d && d.boardTemp !== undefined) ? d.boardTemp.toFixed(1) + '°C' : '--');
}

// From poll()/streamWatchdog(): never kill the view on a transient blip,
// reconnect in-page when the monotonic streamFrames counter freezes.
// Mirrors page logic: 3 consecutive not-ready polls stop the img; 3 polls
// with unchanged streamFrames reconnect it; any ready poll clears blips.
function pollStreamStep(st, ready, frames) {
  // st: {R, notReady, lastFr, frozen}
  if (ready) {
    st.notReady = 0;
    if (!st.R) { st.R = true; st.action = 'start'; }
    else {
      st.action = 'none';
      if (st.lastFr >= 0 && frames >= 0) {
        if (frames === st.lastFr) { st.frozen++; if (st.frozen >= 3) { st.frozen = 0; st.action = 'reconnect'; } }
        else st.frozen = 0;
      }
      st.lastFr = frames;
    }
  } else {
    if (st.R) {
      st.notReady++;
      st.action = 'none';
      if (st.notReady >= 3) { st.R = false; st.action = 'stop'; st.notReady = 0; }
    } else st.action = 'none';
  }
  return st;
}

// From upd(): status pill (mirrors page logic, incl. the CAM-62 capStall flag)
function fmtStatusPill(d) {
  var on = d && d.ready;
  var pill = on ? ((d.model || 'Cam') + ' ' + (d.streamFps || 0) + 'fps') : 'Off';
  if (d && d.initInProgress && !on) pill = 'Starting...';
  if (on && d.recording) pill += ' [REC]';
  if (on && d && d.capStall) pill += ' [NO FRAMES]';
  return pill;
}

// From upd(): footer strip fields (mirrors page logic)
function fmt2(n) { return (n < 10 ? '0' : '') + n; }
function fmtFooter(d) {
  var on = d && d.ready;
  var out = { model: on ? ((d.model || 'Cam') + ' ' + (d.streamFps || 0) + ' fps') : 'Camera off', rec: '', mot: '' };
  if (!on) return out;
  if (d.recording) {
    out.rec = 'REC ' + fmt2(Math.floor(d.recElapsed / 60)) + ':' + fmt2(d.recElapsed % 60) + ' | ' + d.recFrames + ' fr';
  } else if (d.timelapseActive) {
    out.rec = 'TL next ' + d.timelapseNextInSecs + 's';
  }
  if (d.motion) out.mot = 'Motion ' + (d.motionScore || 0) + '%';
  return out;
}

// From mot(): motion body with cooldown/lamp/ROI (mirrors page logic)
function buildMotBody(o) {
  return 'motion_enabled=' + (o.en ? 1 : 0) + '&motion_sensitivity=' + o.s +
    '&motion_confirm_frames=' + o.cf + '&motion_stop_frames=' + o.sf +
    '&motion_post_record_secs=' + o.ps + '&motion_action=' + o.ma +
    '&motion_cooldown_secs=' + o.cd + '&motion_lamp_auto=' + (o.lamp ? 1 : 0) +
    '&motion_roi_x=' + o.rx + '&motion_roi_y=' + o.ry +
    '&motion_roi_w=' + o.rw + '&motion_roi_h=' + o.rh;
}

// Poll interval: 5s (page must not hammer the S3 while streaming)
var POLL_MS = 5000;

// From upd(): maximum-selectable resolution filtering (mirrors applyMaxFrameSize).
// Options above the detected sensor max are disabled (never offered); if the
// current selection sits above the max it is clamped to the highest allowed.
var RES_OPTIONS = [
  { v: 6,  label: '320x240'   },
  { v: 10, label: '640x480'   },
  { v: 11, label: '800x600'   },
  { v: 12, label: '1024x768'  },
  { v: 13, label: '1280x720'  },
  { v: 14, label: '1280x1024' },
  { v: 15, label: '1600x1200' },
  { v: 16, label: '1920x1080' },
  { v: 19, label: '2048x1536' },
  { v: 24, label: '2592x1944' }
];
function applyMaxFrameSizeLogic(maxFs, currentValue) {
  var highestAllowed = -1;
  for (var i = 0; i < RES_OPTIONS.length; i++) {
    var v = RES_OPTIONS[i].v;
    if (v <= maxFs && v > highestAllowed) highestAllowed = v;
  }
  var clamped = currentValue;
  if (currentValue > maxFs && highestAllowed >= 0) clamped = highestAllowed;
  return { highestAllowed: highestAllowed, clamped: clamped };
}

// From loadCfg(): motion-action options depend on email availability
// (mirrors the motionEmailAvail branch). All three actions when SMTP is
// compiled; Email alert (2) is dropped and a legacy stored 2 is clamped to
// Snap photo (1) otherwise.
var MOTION_ACTIONS = [
  { v: '0', label: 'Record video' },
  { v: '1', label: 'Snap photo'   },
  { v: '2', label: 'Email alert'  }
];
function filterMotionActionsLogic(emailAvail, currentValue) {
  var opts = [];
  for (var i = 0; i < MOTION_ACTIONS.length; i++) {
    if (!emailAvail && MOTION_ACTIONS[i].v === '2') continue;
    opts.push(MOTION_ACTIONS[i].v);
  }
  var clamped = currentValue;
  if (!emailAvail && (currentValue === '2' || currentValue === '' || currentValue == null)) clamped = '1';
  return { options: opts, clamped: clamped };
}

// From upd(): Source row visibility (mirrors page_camera_asset.cpp rwSrc)
function srcRowDisplay(d) {
  var p4model = (d && (d.model === 'OV5647' || d.model === 'USB-UVC'));
  return (p4model || (d && d.p4src)) ? 'flex' : 'none';
}

// From upd()/applySrcList(): Source options list ATTACHED cameras pre-Start
// (mirrors page_camera_asset.cpp). Labels from detMipi/detUsb/usbId.
function srcOpts(d) {
  if (!d || d.detMipi === undefined) return null;  // non-P4 status: untouched
  return [
    d.detMipi ? 'Native \u2014 OV5647 (CSI)' : 'Native \u2014 no CSI camera',
    d.detUsb ? ('USB \u2014 ' + (d.usbId || 'camera')) : 'USB \u2014 none detected'
  ];
}

// From applySrcList(): persisted choice is kept when still valid, otherwise
// default to the detected camera (USB first, then MIPI, else '0').
function srcPick(cur, d) {
  if (cur === '0' || cur === '1') return cur;
  if (d && d.detUsb) return '1';
  return '0';
}

// From upd()/applyResList(): adaptive resolution list (mirrors page_camera_asset.cpp)
function applyResListOpts(resList) {
  if (!resList) return null;  // absent/empty: keep the legacy enum list
  var out = [];
  var parts = resList.split(',');
  for (var i = 0; i < parts.length; i++) {
    var s = parts[i].replace(/^\s+|\s+$/g, '');
    if (/^[0-9]+x[0-9]+$/.test(s)) out.push(s);
  }
  return out;
}

// From page_camera_asset.cpp selectResOption(): keep a persisted adaptive
// resolution once options exist; live width/height may override it afterward.
function selectResValue(options, preferred, actual) {
  var targets = [actual, preferred];
  for (var t = 0; t < targets.length; t++) {
    for (var i = 0; i < options.length; i++) {
      if (targets[t] && options[i] === targets[t]) return options[i];
    }
  }
  return options.length ? options[0] : '';
}

// From upd(): FPS slider ceiling from status fpsMax
function clampFpsSlider(cur, fpsMax) {
  if (fpsMax === undefined) return cur;
  return cur > fpsMax ? fpsMax : cur;
}

// From cfg(): framesize field is skipped for adaptive "WxH" picks
function framesizeField(selFrValue) {
  return (selFrValue.indexOf('x') >= 0) ? '' : 'framesize=' + selFrValue + '&';
}
function resolutionPostField(selFrValue) {
  return (selFrValue.indexOf('x') >= 0)
    ? 'resolution=' + encodeURIComponent(selFrValue) + '&'
    : 'framesize=' + selFrValue + '&';
}

function run() {
  console.log('  --- srcRowDisplay (Source selector visibility) ---');
  test('P4 before first start (Not detected + p4src) shows Source row', function() {
    is(srcRowDisplay({ ready: false, model: 'Not detected', p4src: true }), 'flex');
  });
  test('non-P4 board without flag keeps Source row hidden', function() {
    is(srcRowDisplay({ ready: false, model: 'Not detected' }), 'none');
  });
  test('OV5647 detected shows Source row even without flag (legacy path)', function() {
    is(srcRowDisplay({ ready: true, model: 'OV5647' }), 'flex');
  });
  test('USB-UVC detected shows Source row even without flag (legacy path)', function() {
    is(srcRowDisplay({ ready: true, model: 'USB-UVC' }), 'flex');
  });
  console.log('  --- srcOpts/srcPick (attached-camera Source list) ---');
  test('both cameras attached: named options', function() {
    var o = srcOpts({ detMipi: true, detUsb: true, usbId: '1b3f:1167' });
    is(o[0], 'Native \u2014 OV5647 (CSI)');
    is(o[1], 'USB \u2014 1b3f:1167');
  });
  test('nothing attached: honest empty labels', function() {
    var o = srcOpts({ detMipi: false, detUsb: false, usbId: '' });
    is(o[0], 'Native \u2014 no CSI camera');
    is(o[1], 'USB \u2014 none detected');
  });
  test('USB without id still shows a camera entry', function() {
    var o = srcOpts({ detMipi: false, detUsb: true, usbId: '' });
    is(o[1], 'USB \u2014 camera');
  });
  test('non-P4 status leaves the static options alone', function() {
    is(srcOpts({ ready: false }), null);
  });
  test('persisted choice is kept when valid', function() {
    is(srcPick('0', { detMipi: false, detUsb: true }), '0');
    is(srcPick('1', { detMipi: true, detUsb: false }), '1');
  });
  test('invalid choice defaults to the detected camera', function() {
    is(srcPick('7', { detMipi: true, detUsb: true }), '1');
    is(srcPick('', { detMipi: true, detUsb: false }), '0');
    is(srcPick('', { detMipi: false, detUsb: false }), '0');
  });
  console.log('  --- fmtRecElapsed ---');
  test('fmtRecElapsed: 0 seconds', function() {
    is(fmtRecElapsed(0), '00:00');
  });
  test('fmtRecElapsed: 30 seconds', function() {
    is(fmtRecElapsed(30), '00:30');
  });
  test('fmtRecElapsed: 1 minute 5 seconds', function() {
    is(fmtRecElapsed(65), '01:05');
  });
  test('fmtRecElapsed: 10 minutes 0 seconds', function() {
    is(fmtRecElapsed(600), '10:00');
  });
  test('fmtRecElapsed: 59 minutes 59 seconds', function() {
    is(fmtRecElapsed(3599), '59:59');
  });
  test('fmtRecElapsed: 100 minutes', function() {
    is(fmtRecElapsed(6000), '100:00');
  });
  test('fmtRecElapsed: 1 second', function() {
    is(fmtRecElapsed(1), '00:01');
  });

  console.log('  --- fmtPsecId ---');
  test('fmtPsecId: lowercases first char of section', function() {
    is(fmtPsecId('camera'), 'Camera');
  });
  test('fmtPsecId: single char', function() {
    is(fmtPsecId('a'), 'A');
  });

  console.log('  --- preset coverage: both cameras (OV5647 v1.3 / USB) ---');
  test('every preset exists for BOTH cameras', function() {
    var names = ['hd', 'vga', 'fast', 'night', 'safe', 'eco'];
    for (var i = 0; i < names.length; i++) {
      ok(P4_OV5647_PRESETS[names[i]], 'OV5647 has preset ' + names[i]);
      ok(P4_USB_PRESETS[names[i]], 'USB has preset ' + names[i]);
    }
  });
  test('OV5647 preset resolutions are real CSI modes (never the stalling 720p)', function() {
    var names = ['hd', 'vga', 'fast', 'night', 'safe', 'eco'];
    for (var i = 0; i < names.length; i++) {
      var p = P4_OV5647_PRESETS[names[i]];
      ok(p.res, names[i] + ' carries a resolution');
      ok(CAM_RES_MODES.indexOf(p.res) >= 0, names[i] + ' res is a sensor mode: ' + p.res);
      ok(p.res !== '1280x720', names[i] + ' must not use the stalling 720p profile (CAM-62)');
    }
  });
  test('USB presets carry NO resolution (geometry is negotiated)', function() {
    var names = ['hd', 'vga', 'fast', 'night', 'safe', 'eco'];
    for (var i = 0; i < names.length; i++) {
      var p = P4_USB_PRESETS[names[i]];
      ok(p.res === undefined, names[i] + ' posts no resolution its body');
      var body = buildP4PresetBody({ res: p.res, q: p.q, fs: p.fs, br: p.br, ct: p.ct,
        sa: p.sa, awb: p.awb, aec: p.aec, agc: p.agc, gc: p.gc, usb: true });
      ok(body.indexOf('resolution=') < 0, names[i] + ' USB body omits resolution');
    }
  });
  test('preset fps never exceeds the per-camera ceiling', function() {
    var names = ['hd', 'vga', 'fast', 'night', 'safe', 'eco'];
    for (var i = 0; i < names.length; i++) {
      var o = getP4Preset(names[i], 'OV5647', 6);
      is(o.fs, P4_OV5647_PRESETS[names[i]].fs);   // all at/below 6
      ok(o.fs <= 6, 'OV5647 ' + names[i] + ' within MIPI ceiling');
      var u = getP4Preset(names[i], 'USB-UVC', 30);
      ok(u.fs <= 30, 'USB ' + names[i] + ' within ceiling');
    }
  });
  test('OV5647 preset body posts resolution, USB one does not', function() {
    var o = buildP4PresetBody(getP4Preset('hd', 'OV5647', 6));
    ok(o.indexOf('resolution=1920x1080') >= 0, 'OV5647 posts explicit resolution');
    var u = buildP4PresetBody(getP4Preset('hd', 'USB-UVC', 30));
    ok(u.indexOf('resolution=') < 0, 'USB posts quality/fps only');
    ok(u.indexOf('quality=10') >= 0, 'USB still pushes quality');
    ok(u.indexOf('fps=6') >= 0, 'USB still pushes fps');
  });

  console.log('  --- photo RAM state (mirrors page upd photoInfo) ---');
  test('photoInfo shows view link when RAM copy exists', function() {
    var asset = fs.readFileSync(path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'page_camera_asset.cpp'), 'utf8');
    ok(asset.indexOf('/api/camera/last-photo') >= 0, 'last-photo link present in page');
    ok(asset.indexOf('photoPend') >= 0, 'pending count read from status');
  });
  test('last-photo + ring stats routes exist', function() {
    var routes = fs.readFileSync(path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'routes', 'web_routes_camera.cpp'), 'utf8');
    ok(routes.indexOf('/api/camera/last-photo') >= 0, 'last-photo route registered');
    var h = fs.readFileSync(path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'handlers_unified', 'camera_handler.cpp'), 'utf8');
    ok(h.indexOf('psramBuf') >= 0, 'ring bytes in status');
    ok(h.indexOf('psramDrop') >= 0, 'ring drops in status');
  });

  console.log('  --- getPreset ---');
  test('getPreset: hd preset values', function() {
    var p = getPreset('hd');
    is(p.fr, 13); is(p.q, 10); is(p.fs, 10);
    is(p.br, 0); is(p.ct, 0); is(p.sa, 0);
    is(p.awb, 1); is(p.aec, 1); is(p.agc, 1); is(p.gc, 0);
  });
  test('getPreset: vga preset values', function() {
    var p = getPreset('vga');
    is(p.fr, 10); is(p.q, 20); is(p.fs, 12);
  });
  test('getPreset: fast preset values', function() {
    var p = getPreset('fast');
    is(p.fs, 25); is(p.q, 35);
  });
  test('getPreset: night preset (negative brightness, contrast, saturation)', function() {
    var p = getPreset('night');
    is(p.br, 1); is(p.ct, -1); is(p.sa, -1);
    is(p.awb, 0);
  });
  test('getPreset: safe preset (measured stable: HD/q12/10fps)', function() {
    var p = getPreset('safe');
    is(p.fr, 13); is(p.q, 12); is(p.fs, 10);
    is(p.awb, 1); is(p.aec, 1); is(p.agc, 1);
  });
  test('getPreset: eco preset (economy: QVGA/q24/8fps)', function() {
    var p = getPreset('eco');
    is(p.fr, 6); is(p.q, 24); is(p.fs, 8);
    is(p.gc, 0);
  });
  test('getPreset: eco is the lowest-cost preset (area x fps x compression)', function() {
    var eco = getPreset('eco'), safe = getPreset('safe');
    ok(eco.fr < safe.fr, 'eco resolution below safe');
    ok(eco.fs < safe.fs, 'eco fps below safe');
    ok(eco.q > safe.q, 'eco more compressed than safe');
  });
  test('getPreset: unknown preset falls back to vga', function() {
    var p = getPreset('unknown');
    is(p.fr, 10); is(p.q, 20);
  });

  console.log('  --- getP4Preset (per-sensor presets, CAM-68) ---');
  test('P4 OV5647 hd targets 1920x1080 at ceiling fps', function() {
    var p = getP4Preset('hd', 'OV5647', 6);
    is(p.res, '1920x1080'); is(p.q, 10); is(p.fs, 6); is(p.usb, false);
  });
  test('P4 OV5647 safe targets the verified 800x800 mode', function() {
    var p = getP4Preset('safe', 'OV5647', 6);
    is(p.res, '800x800'); is(p.q, 12); is(p.fs, 5);
  });
  test('P4 OV5647 eco is the lowest-cost preset', function() {
    var eco = getP4Preset('eco', 'OV5647', 6), safe = getP4Preset('safe', 'OV5647', 6);
    var area = function(r) { var w = r.split('x'); return (+w[0]) * (+w[1]); };
    ok(area(eco.res) <= area(safe.res), 'eco resolution at/below safe');
    ok(eco.fs <= safe.fs, 'eco fps at/below safe');
    ok(eco.q > safe.q, 'eco more compressed than safe');
  });
  test('P4 OV5647 preset targets are all known sensor modes, never 720p', function() {
    var names = ['hd', 'vga', 'fast', 'night', 'safe', 'eco'];
    for (var i = 0; i < names.length; i++) {
      var p = getP4Preset(names[i], 'OV5647', 6);
      ok(CAM_RES_MODES.indexOf(p.res) >= 0, names[i] + ' res ' + p.res + ' is a sensor mode');
      ok(p.res !== '1280x720', names[i] + ' avoids the stalled 720p profile');
      ok(p.fs <= 6, names[i] + ' fps within the MIPI sustained ceiling');
    }
  });
  test('P4 OV5647 unknown preset falls back to vga mode', function() {
    var p = getP4Preset('bogus', 'OV5647', 6);
    is(p.res, '800x640'); is(p.q, 20);
  });
  test('P4 USB presets carry no resolution (negotiated geometry kept)', function() {
    var p = getP4Preset('hd', 'USB-UVC', 30);
    is(p.res, undefined); is(p.usb, true); is(p.q, 10);
    ok(buildP4PresetBody(p).indexOf('resolution=') < 0, 'no resolution field posted');
    ok(buildP4PresetBody(p).indexOf('quality=10') >= 0, 'quality posted');
  });
  test('P4 USB safe posts quality/fps only', function() {
    var b = buildP4PresetBody(getP4Preset('safe', 'USB-UVC', 15));
    is(b, 'quality=12&fps=5&brightness=0&contrast=0&saturation=0&awb=1&aec=1&agc=1&gainceiling=0');
  });
  test('P4 OV5647 safe posts explicit resolution first', function() {
    var b = buildP4PresetBody(getP4Preset('safe', 'OV5647', 6));
    ok(b.indexOf('resolution=800x800&') === 0, 'resolution leads the body');
    ok(b.indexOf('framesize=') < 0, 'no legacy framesize enum on P4');
  });
  test('P4 fps clamps to the status fpsMax ceiling', function() {
    is(getP4Preset('hd', 'OV5647', 4).fs, 4);
    is(getP4Preset('fast', 'USB-UVC', 15).fs, 6);
    is(getP4Preset('hd', 'OV5647', undefined).fs, 6);
  });
  test('P4 night keeps exposure tweaks on both sensors', function() {
    var m = getP4Preset('night', 'OV5647', 6), u = getP4Preset('night', 'USB-UVC', 30);
    is(m.br, 1); is(m.awb, 0); is(m.res, '800x800');
    is(u.br, 1); is(u.awb, 0); is(u.usb, true);
  });

  console.log('  --- presetTitles (per-sensor tooltips) ---');
  test('S3 keeps static titles (no override)', function() {
    is(presetTitles('', false), null);
    is(presetTitles('OV3660', false), null);
  });
  test('OV5647 titles name the real modes', function() {
    var t = presetTitles('OV5647', true);
    ok(t.hd.indexOf('1920x1080') >= 0, 'hd names FHD');
    ok(t.safe.indexOf('800x800') >= 0, 'safe names the verified mode');
  });
  test('USB titles state resolution is fixed by the camera', function() {
    var t = presetTitles('USB-UVC', true);
    ok(t.hd.indexOf('fixed by the USB camera') >= 0, 'hd honest on USB');
    ok(t.safe.indexOf('fixed by the USB camera') >= 0, 'safe honest on USB');
  });

  console.log('  --- resolution selector (mirrors page_camera_asset.cpp selFr) ---');
  // Framework framesize enum -> option value. OV3660 tops at QXGA(19); OV5640 at 5MP(24).
  function maxResOptionFor(pids) { return RES_OPTIONS.map(function(o){return o.v;}).filter(function(v){return pids.indexOf(v) >= 0;}).pop(); }
  test('resolution options map to framework enum values', function() {
    is(RES_OPTIONS.map(function(o){return o.v;}).join(','), '6,10,11,12,13,14,15,16,19,24');
  });
  test('OV3660 capped at QXGA(19), never offered 5MP(24)', function() {
    is(maxResOptionFor([6,10,11,12,13,14,15,16,19]), 19);
  });
  test('OV5640 offered up to 5MP(24)', function() {
    is(maxResOptionFor([6,10,11,12,13,14,15,16,19,24]), 24);
  });

  console.log('  --- applyMaxFrameSize (resolution list filtering + clamp) ---');
  test('OV3660 (max 19): 5MP(24) option disabled, current 24 clamps to 19', function() {
    var r = applyMaxFrameSizeLogic(19, 24);
    is(r.highestAllowed, 19);
    is(r.clamped, 19);
  });
  test('OV3660 (max 19): current 13 stays 13, highest allowed 19', function() {
    var r = applyMaxFrameSizeLogic(19, 13);
    is(r.highestAllowed, 19);
    is(r.clamped, 13);
  });
  test('OV5640 (max 24): everything selectable, current 24 stays', function() {
    var r = applyMaxFrameSizeLogic(24, 24);
    is(r.highestAllowed, 24);
    is(r.clamped, 24);
  });
  test('pre-init unknown sensor (max 15): 19/24 unavailable, current 6 stays', function() {
    var r = applyMaxFrameSizeLogic(15, 6);
    is(r.highestAllowed, 15);
    is(r.clamped, 6);
  });
  test('VGA-only sensor (max 10): only 6/10 usable, current 14 clamps to 10', function() {
    var r = applyMaxFrameSizeLogic(10, 14);
    is(r.highestAllowed, 10);
    is(r.clamped, 10);
  });
  test('same max reported twice: idempotent (no re-clamp)', function() {
    var r1 = applyMaxFrameSizeLogic(19, 19);
    var r2 = applyMaxFrameSizeLogic(19, r1.clamped);
    is(r2.clamped, 19);
    is(r2.highestAllowed, 19);
  });

  console.log('  --- pills ---');
  test('heap pill shows KB', function() {
    is(fmtHeapPill({ freeHeap: 137420 }), '🧠 134K');
  });
  test('heap pill missing data', function() {
    is(fmtHeapPill({}), '🧠 --');
    is(fmtHeapPill(null), '🧠 --');
  });
  test('temp pill shows Celsius', function() {
    is(fmtTempPill({ boardTemp: 36.5 }), '🌡️ 36.5°C');
  });
  test('temp pill missing data (P4 build)', function() {
    is(fmtTempPill({}), '🌡️ --');
  });

  console.log('  --- fmtSdInfo ---');
  test('fmtSdInfo: shows free/total when SD present', function() {
    is(fmtSdInfo({ sdTotal: 29800, sdFree: 12400 }), 'SD 12400/29800 MB free');
  });
  test('fmtSdInfo: zero total means n/a', function() {
    is(fmtSdInfo({ sdTotal: 0, sdFree: 0 }), 'SD n/a');
  });
  test('fmtSdInfo: missing keys means n/a', function() {
    is(fmtSdInfo({}), 'SD n/a');
  });
  test('fmtSdInfo: null status means n/a', function() {
    is(fmtSdInfo(null), 'SD n/a');
  });

  console.log('  --- fmtPhotoInfo ---');
  test('fmtPhotoInfo: shows server filename', function() {
    is(fmtPhotoInfo({ success: true, filename: '/CAM/PHOTOS/20260101_120000.jpg' }), 'Saved: /CAM/PHOTOS/20260101_120000.jpg');
  });
  test('fmtPhotoInfo: falls back when filename missing', function() {
    is(fmtPhotoInfo({ success: true }), 'Saved: photo.jpg');
  });
  test('fmtPhotoInfo: failure returns null', function() {
    is(fmtPhotoInfo({ success: false }), null);
  });

  console.log('  --- buildTimelapseBody ---');
  test('buildTimelapseBody: off', function() {
    is(buildTimelapseBody(0), 'timelapse_secs=0');
  });
  test('buildTimelapseBody: 60s interval', function() {
    is(buildTimelapseBody(60), 'timelapse_secs=60');
  });

  console.log('  --- buildSrcBody ---');
  test('buildSrcBody: native', function() {
    is(buildSrcBody(0), 'source=0');
  });
  test('buildSrcBody: usb', function() {
    is(buildSrcBody(1), 'source=1');
  });

  console.log('  --- buildTlStopBody ---');
  test('buildTlStopBody: always stops', function() {
    is(buildTlStopBody(), 'timelapse_secs=0');
  });
  test('buildTlStopBody: matches the off body exactly', function() {
    is(buildTlStopBody(), buildTimelapseBody(0));
  });

  console.log('  --- fmtTlInfo ---');
  test('fmtTlInfo: null when inactive', function() {
    is(fmtTlInfo(null), null);
    is(fmtTlInfo({ timelapseActive: false }), null);
  });
  test('fmtTlInfo: countdown only, no last path', function() {
    is(fmtTlInfo({ timelapseActive: true, timelapseNextInSecs: 7 }), 'Next capture in 7s');
  });
  test('fmtTlInfo: countdown + last saved path', function() {
    is(fmtTlInfo({ timelapseActive: true, timelapseNextInSecs: 3, timelapseLast: '/CAM/TIMELAPSE/20260101_120000.jpg' }),
       'Next capture in 3s \u00b7 /CAM/TIMELAPSE/20260101_120000.jpg');
  });
  test('fmtTlInfo: empty last path is omitted', function() {
    is(fmtTlInfo({ timelapseActive: true, timelapseNextInSecs: 9, timelapseLast: '' }), 'Next capture in 9s');
  });

  console.log('  --- fmtAec2 ---');
  test('fmtAec2: checked element yields 1', function() {
    is(fmtAec2({ checked: true }), 1);
  });
  test('fmtAec2: unchecked element yields 0', function() {
    is(fmtAec2({ checked: false }), 0);
  });
  test('fmtAec2: missing element (stale cached page) yields 0, no throw', function() {
    is(fmtAec2(null), 0);
    is(fmtAec2(undefined), 0);
  });

  console.log('  --- poll interval ---');
  test('poll interval is 5s, not the old 3s hammer', function() {
    is(POLL_MS, 5000);
    ok(POLL_MS >= 5000, 'poll must be >= 5s while streaming');
  });

  console.log('  --- live badges (CSS .rec-badge, no fmtLiveBadge fn) ---');
  test('live badge: page asset renders REC/LIVE via CSS badge', function() {
    var asset = fs.readFileSync(path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'page_camera_asset.cpp'), 'utf8');
    ok(asset.indexOf('rec-badge') >= 0, 'REC badge markup present in page asset');
  });

  console.log('  --- vision/motion rail visibility (page asset source scan) ---');
  test('psecVis: no inline display:none (showPsec .on class must win)', function() {
    var asset = fs.readFileSync(path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'page_camera_asset.cpp'), 'utf8');
    var m = asset.match(/<div[^>]*id='psecVis'[^>]*>/);
    ok(m, 'psecVis div present');
    ok(m[0].indexOf('display:none') < 0, 'psecVis has no inline hide, else the tab can never open: ' + m[0]);
  });
  test('railMot: always visible (P4 motion via UYVY tap, S3 unchanged)', function() {
    var asset = fs.readFileSync(path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'page_camera_asset.cpp'), 'utf8');
    ok(asset.indexOf("id='railMot'") >= 0, 'motion rail button has an id');
    ok(asset.indexOf('railMot') >= 0, 'railMot referenced');
    ok(asset.indexOf("style.display=isP4B") < 0, 'no P4 hide left');
    ok(asset.indexOf('motP4note') < 0, 'stale P4-unavailable note removed');
  });
  test('railVis: still gated until P4 caps arrive (no regression)', function() {
    var asset = fs.readFileSync(path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'page_camera_asset.cpp'), 'utf8');
    ok(asset.indexOf("id='railVis' style='display:none'") >= 0, 'vision rail hidden by default');
    ok(asset.indexOf('rv.style.display=') >= 0, 'vision rail shown by updVision on P4 caps');
  });

  console.log('  --- buildMotBody ---');
  test('buildMotBody: full field set', function() {
    is(buildMotBody({ en: true, s: 20, cf: 3, sf: 10, ps: 5, ma: 0, cd: 5, lamp: false, rx: 0, ry: 0, rw: 0, rh: 0 }),
      'motion_enabled=1&motion_sensitivity=20&motion_confirm_frames=3&motion_stop_frames=10&motion_post_record_secs=5&motion_action=0&motion_cooldown_secs=5&motion_lamp_auto=0&motion_roi_x=0&motion_roi_y=0&motion_roi_w=0&motion_roi_h=0');
  });
  test('buildMotBody: ROI + lamp on', function() {
    var b = buildMotBody({ en: true, s: 30, cf: 2, sf: 8, ps: 3, ma: 1, cd: 0, lamp: true, rx: 5, ry: 4, rw: 20, rh: 15 });
    ok(b.indexOf('motion_lamp_auto=1') >= 0, 'lamp flag');
    ok(b.indexOf('motion_roi_x=5') >= 0, 'roi x');
    ok(b.indexOf('motion_roi_w=20') >= 0, 'roi w');
    ok(b.indexOf('motion_cooldown_secs=0') >= 0, 'cooldown off');
  });

  console.log('  --- filterMotionActions (email availability) ---');
  test('filterMotionActions: SMTP build keeps all three actions', function() {
    var r = filterMotionActionsLogic(true, '2');
    is(r.options.join(','), '0,1,2');
    is(r.clamped, '2');
  });
  test('filterMotionActions: no-SMTP build drops Email alert, folds legacy 2 to Snap', function() {
    var r = filterMotionActionsLogic(false, '2');
    is(r.options.join(','), '0,1');
    is(r.clamped, '1');
  });
  test('filterMotionActions: no-SMTP build keeps 0/1 untouched', function() {
    is(filterMotionActionsLogic(false, '0').clamped, '0');
    is(filterMotionActionsLogic(false, '1').clamped, '1');
    is(filterMotionActionsLogic(false, '1').options.join(','), '0,1');
  });
  test('fmtFooter: live model + fps', function() {
    is(fmtFooter({ ready: true, model: 'OV3660', streamFps: 12 }).model, 'OV3660 12 fps');
  });
  test('fmtFooter: off state', function() {
    is(fmtFooter(null).model, 'Camera off');
    is(fmtFooter({ ready: false }).model, 'Camera off');
  });
  test('fmtFooter: recording shows REC timer', function() {
    is(fmtFooter({ ready: true, recording: true, recElapsed: 65, recFrames: 100 }).rec, 'REC 01:05 | 100 fr');
  });
  test('fmtFooter: idle shows timelapse next', function() {
    is(fmtFooter({ ready: true, timelapseActive: true, timelapseNextInSecs: 7 }).rec, 'TL next 7s');
  });
  test('fmtFooter: motion score shown when enabled', function() {
    is(fmtFooter({ ready: true, motion: true, motionScore: 42 }).mot, 'Motion 42%');
  });
  console.log('  --- fmtStatusPill (status pill + capStall) ---');
  test('live pill shows model and fps', function() {
    is(fmtStatusPill({ ready: true, model: 'OV5647', streamFps: 6 }), 'OV5647 6fps');
  });
  test('stalled pipeline appends NO FRAMES flag', function() {
    is(fmtStatusPill({ ready: true, model: 'OV5647', streamFps: 0, capStall: 1 }), 'OV5647 0fps [NO FRAMES]');
  });
  test('healthy pipeline shows no flag, off shows Off', function() {
    is(fmtStatusPill({ ready: true, model: 'OV5647', streamFps: 6, capStall: 0 }), 'OV5647 6fps');
    is(fmtStatusPill({ ready: false }), 'Off');
  });
  console.log('  --- pollStreamStep (view self-healing) ---');
  test('single not-ready blip keeps the view alive', function() {
    var st = pollStreamStep({ R: true, notReady: 0, lastFr: 100, frozen: 0 }, false, 100);
    is(st.R, true); is(st.action, 'none');
    st = pollStreamStep(st, true, 130);
    is(st.R, true); is(st.notReady, 0); is(st.action, 'none');
  });
  test('three consecutive not-ready polls stop the view', function() {
    var st = { R: true, notReady: 0, lastFr: 100, frozen: 0 };
    st = pollStreamStep(st, false, 100);
    st = pollStreamStep(st, false, 100);
    is(st.R, true);
    st = pollStreamStep(st, false, 100);
    is(st.R, false); is(st.action, 'stop');
  });
  test('later ready restarts the view without refresh', function() {
    var st = pollStreamStep({ R: false, notReady: 0, lastFr: -1, frozen: 0 }, true, 200);
    is(st.R, true); is(st.action, 'start');
  });
  test('frozen streamFrames reconnects after 3 polls', function() {
    var st = { R: true, notReady: 0, lastFr: 100, frozen: 0 };
    st = pollStreamStep(st, true, 100);
    st = pollStreamStep(st, true, 100);
    is(st.action, 'none');
    st = pollStreamStep(st, true, 100);
    is(st.action, 'reconnect');
  });
  test('advancing frames and session reset clear the freeze count', function() {
    var st = pollStreamStep({ R: true, notReady: 0, lastFr: 100, frozen: 2 }, true, 140);
    is(st.action, 'none');
    st = pollStreamStep({ R: true, notReady: 0, lastFr: 500, frozen: 2 }, true, 12);
    is(st.action, 'none');
  });
  test('fmtFooter: no motion configured hides field', function() {
    is(fmtFooter({ ready: true }).mot, '');
  });
  console.log('  --- applyResListOpts (adaptive resolution list) ---');
  test('MIPI single mode builds one WxH option', function() {
    is(JSON.stringify(applyResListOpts('800x800')), JSON.stringify(['800x800']));
  });
  test('USB negotiated list builds every entry', function() {
    is(JSON.stringify(applyResListOpts('320x240,640x480')), JSON.stringify(['320x240', '640x480']));
  });
  test('empty resList keeps the legacy enum list', function() {
    is(applyResListOpts(''), null);
  });
  test('bogus entries are filtered, spacing tolerated', function() {
    is(JSON.stringify(applyResListOpts('800x800, junk, 640x480 ')), JSON.stringify(['800x800', '640x480']));
  });
  test('OV5647 list includes the requested 720p mode', function() {
    var opts = applyResListOpts('1920x1080,1280x960,1280x720,800x800');
    ok(opts.indexOf('1280x720') >= 0);
  });
  console.log('  --- persisted/live adaptive resolution selection ---');
  test('saved P4 resolution is selected after list rebuild', function() {
    is(selectResValue(['1920x1080', '800x800'], '800x800', ''), '800x800');
  });
  test('live negotiated dimensions override the saved mode', function() {
    is(selectResValue(['1920x1080', '800x800'], '1920x1080', '800x800'), '800x800');
  });
  test('missing persisted mode falls back to the first offered mode', function() {
    is(selectResValue(['800x800', '800x640'], '1280x720', ''), '800x800');
  });
  console.log('  --- clampFpsSlider (fpsMax ceiling) ---');
  test('over-range value clamps to fpsMax', function() {
    is(clampFpsSlider(12, 6), 6);
  });
  test('in-range value passes through', function() {
    is(clampFpsSlider(5, 6), 5);
  });
  test('absent fpsMax leaves the slider alone (S3 path)', function() {
    is(clampFpsSlider(12, undefined), 12);
  });
  console.log('  --- framesizeField (cfg post) ---');
  test('legacy enum value posts framesize', function() {
    is(framesizeField('13'), 'framesize=13&');
  });
  test('adaptive WxH pick skips framesize', function() {
    is(framesizeField('800x800'), '');
  });
  test('720p selection posts explicit resolution for P4 mode handler', function() {
    is(resolutionPostField('1280x720'), 'resolution=1280x720&');
  });
  test('legacy selection still posts enum unchanged', function() {
    is(resolutionPostField('16'), 'framesize=16&');
  });

  var total = passed + failed;
  console.log('\n  Camera: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
