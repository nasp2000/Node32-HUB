// Mobile G-code wizard — jsdom tests for the five screens.
//
//   1 Send    : SD card list (folders + files) or the phone picker
//   2 Preview : toolpath built from the file / streamed from the controller
//   3 Frame   : outline the job area as a G-code batch
//   4 Play    : START / PAUSE / RESUME / STOP
//   5 Jog     : axis jog + the essential GRBL buttons
// Plus the contract: one screen at a time, free navigation (arrows + swipe)
// with or without a job, and no page scroll.
const { JSDOM, VirtualConsole } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'gcode_mobile.html');
var passed = 0, failed = 0;

// jsdom has no canvas backend; the page already no-ops when getContext fails.
function quietConsole() {
  var vc = new VirtualConsole();
  vc.on('jsdomError', function (e) {
    if (String((e && e.message) || '').indexOf('Not implemented') >= 0) return;
    console.error(e);
  });
  return vc;
}

async function test(name, fn) {
  try { await fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }
function eq(a, b, m) {
  var sa = JSON.stringify(a), sb = JSON.stringify(b);
  if (sa !== sb) throw new Error(m || ('expected ' + sb + ' got ' + sa));
}

function loadPage(api, hash) {
  var html = fs.readFileSync(PAGE, 'utf8');
  return new JSDOM(html, {
    url: 'http://localhost/m' + (hash || ''),
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    virtualConsole: quietConsole(),
    beforeParse: function (w) { w.__MOCK_API__ = api || {}; },
  });
}
function wait(dom, ms) { return new Promise(function (r) { setTimeout(function () { r(dom); }, ms || 300); }); }

function fakeXhr(log) {
  function X() { this.upload = {}; X.last = this; }
  X.prototype.open = function (m, u) { this.method = m; this.url = u; log.push({ method: m, url: u }); };
  X.prototype.setRequestHeader = function (k, v) { (this.headers = this.headers || {})[k] = v; };
  X.prototype.send = function (body) {
    this.body = body;
    if (this.upload.onprogress) this.upload.onprogress({ lengthComputable: true, loaded: 50, total: 100 });
  };
  X.prototype.finish = function (status, body) {
    this.status = status; this.responseText = JSON.stringify(body);
    if (this.onload) this.onload();
  };
  return X;
}

function screenOn(doc) {
  var ids = ['gmS1', 'gmS2', 'gmS3', 'gmS4', 'gmS5'];
  for (var i = 0; i < ids.length; i++) {
    if (doc.getElementById(ids[i]).className.indexOf('on') >= 0) return i + 1;
  }
  return 0;
}

function sent(win) {
  return (win.__postBodies || []).map(function (b) { return b.url + ' ' + decodeURIComponent(String(b.body || '')); }).join(' ');
}
function cmdsSent(win) {
  return (win.__postBodies || []).filter(function (b) { return b.url.indexOf('/api/grbl/send') >= 0; })
    .map(function (b) { return decodeURIComponent(String(b.body)).replace(/.*cmd=/, '').split('&')[0]; });
}

var SD_ROOT = '/GcodeSender-P4/gcode';
var SD_LIST = {
  success: true, message: 'ok', root: SD_ROOT, dir: SD_ROOT,
  files: [
    { path: SD_ROOT + '/Cachola.gc', name: 'Cachola.gc', size: 517814, is_dir: false },
    { path: SD_ROOT + '/Norberto.gc', name: 'Norberto.gc', size: 1280549, is_dir: false },
    { path: SD_ROOT + '/sub', name: 'sub', size: 0, is_dir: true }
  ]
};
var BASE_API = {
  '/api/grbl/control-token': { success: true, token: 'tok-1', can_write: true },
  '/api/grbl/status': { success: true, usb_running: true, state: 'IDLE', workflow_state: 'idle',
                        axis_count: 3, mpos_valid: true, mpos_x: 1.5, mpos_y: 2.5, mpos_z: 3.5 },
  '/api/grbl/psram-file': { success: true, has_file: false },
  '/api/grbl/list-gcode': SD_LIST,
  '/api/grbl/send': { success: true, message: 'ok' }
};

async function run() {
  console.log('\n=== Mobile G-code wizard (5 screens) ===\n');

  // ── Layout + navigation contract ──────────────────────────────────────
  {
    var dom = loadPage(BASE_API);
    await wait(dom);
    var win = dom.window, doc = win.document, css = doc.querySelector('style').textContent;

    await test('root is a fixed 100dvh column with no page scroll', function () {
      ok(/#gm\s*\{[^}]*display:\s*flex/.test(css), 'flex root');
      ok(/#gm\s*\{[^}]*flex-direction:\s*column/.test(css), 'column');
      ok(/#gm\s*\{[^}]*height:\s*100dvh/.test(css), '100dvh');
      ok(/#gm\s*\{[^}]*overflow:\s*hidden/.test(css), '#gm hidden');
      ok(/html,\s*body\s*\{[^}]*overflow:\s*hidden/.test(css), 'html/body hidden');
      is(win.getComputedStyle(doc.body).overflow, 'hidden', 'computed');
    });

    await test('the active screen absorbs the height (never overflows to a scrollbar)', function () {
      ok(/#gm\s*\.screen\s*\{[^}]*flex:\s*1 1 auto/.test(css), 'screen grows');
      ok(/#gm\s*\.screen\s*\{[^}]*min-height:\s*0/.test(css), 'min-height 0');
      ok(/#gm\s*\.screen\s*\{[^}]*overflow:\s*hidden/.test(css), 'screen hidden');
    });

    await test('exactly one of the five screens is visible', function () {
      is(screenOn(doc), 1, 'starts on step 1');
      is(doc.querySelectorAll('#gm .screen').length, 5, 'five screens');
      is(doc.querySelectorAll('#gm .screen.on').length, 1, 'one screen on');
      is(doc.getElementById('gmNo').textContent, '1 / 5', 'counter');
    });

    await test('navigation never waits for a job (arrows usable with nothing loaded)', function () {
      is(win.__test__.gmState.job, null, 'no job loaded');
      ok(doc.getElementById('gmBack').disabled, 'back disabled on the first screen');
      ok(!doc.getElementById('gmNext').disabled, 'next enabled without any job');
      win.__test__.gmNext();
      is(screenOn(doc), 2, 'moved to preview');
      ok(!doc.getElementById('gmBack').disabled, 'back enabled on 2');
      win.__test__.gmSetStep(5);
      is(doc.getElementById('gmNo').textContent, '5 / 5', 'counter 5');
      ok(doc.getElementById('gmNext').disabled, 'next disabled on the last screen');
    });

    await test('a horizontal swipe changes screen, a vertical drag does not', function () {
      win.__test__.gmSetStep(1);
      var gm = doc.getElementById('gm');
      function swipe(fromX, toX, y1, y2) {
        var a = new win.Event('touchstart');
        a.touches = [{ clientX: fromX, clientY: y1 }];
        gm.dispatchEvent(a);
        var b = new win.Event('touchend');
        b.changedTouches = [{ clientX: toX, clientY: y2 }];
        gm.dispatchEvent(b);
      }
      swipe(320, 120, 300, 310);           // left: next
      is(screenOn(doc), 2, 'swipe left -> next');
      swipe(120, 320, 300, 305);           // right: back
      is(screenOn(doc), 1, 'swipe right -> back');
      swipe(320, 120, 300, 520);           // mostly vertical: ignored (list scroll)
      is(screenOn(doc), 1, 'vertical drag ignored');
    });

    await test('each screen shows its own title and primary action', function () {
      win.__test__.gmSetStep(1);
      is(doc.getElementById('gmTitle').textContent, 'Send G-code', 'step 1 title');
      is(doc.getElementById('gmPrimary').textContent, 'USE', 'SD tab acts on a selected file');
      win.__test__.gmSetStep(2);
      is(doc.getElementById('gmTitle').textContent, 'Preview', 'step 2 title');
      ok(doc.getElementById('gmPrimary').className.indexOf('off') >= 0, 'preview has no primary');
      win.__test__.gmSetStep(3);
      is(doc.getElementById('gmTitle').textContent, 'Frame', 'step 3 title');
      is(doc.getElementById('gmPrimary').textContent, 'FRAME', 'step 3 action');
      win.__test__.gmSetStep(4);
      is(doc.getElementById('gmTitle').textContent, 'Play', 'step 4 title');
      is(doc.getElementById('gmPrimary').textContent, 'START', 'step 4 action');
      win.__test__.gmSetStep(5);
      is(doc.getElementById('gmTitle').textContent, 'Jog', 'step 5 title');
    });

    dom.window.close();
  }

  // ── Step 1: SD card list ──────────────────────────────────────────────
  {
    var dom = loadPage(BASE_API);
    await wait(dom, 400);
    var win = dom.window, doc = win.document;

    await test('the SD list is loaded on open (root + files + folders)', function () {
      var rows = doc.querySelectorAll('#gmSdList .row');
      is(rows.length, 3, 'three entries (root: no parent row)');
      ok(rows[0].textContent.indexOf('Cachola.gc') >= 0, 'first file listed');
      ok(rows[0].textContent.indexOf('506 kB') >= 0 || rows[0].textContent.indexOf('517') >= 0, 'size shown');
      ok(rows[2].textContent.indexOf('sub') >= 0, 'folder listed');
      is(doc.getElementById('gmSdDir').textContent, SD_ROOT, 'directory label');
    });

    await test('selecting a file marks it and is the USE target', function () {
      var rows = doc.querySelectorAll('#gmSdList .row');
      rows[1].click();
      is(win.__test__.gmState.sdPath, SD_ROOT + '/Norberto.gc', 'path selected');
      is(win.__test__.gmState.job.src, 'sd', 'job source is the SD card');
      var again = doc.querySelectorAll('#gmSdList .row');
      ok(again[1].className.indexOf('sel') >= 0, 'row highlighted');
      ok(!doc.getElementById('gmPrimary').disabled, 'USE enabled');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Norberto.gc') >= 0, 'ready message');
    });

    await test('USE asks the controller for the job bounds (frame-bbox)', async function () {
      win.__test__.gmState.bbox = null;
      var orig = win.fetch;
      win.fetch = function (url, opts) {
        if (String(url).indexOf('/api/grbl/frame-bbox') >= 0) {
          return Promise.resolve({ ok: true, status: 200,
            json: function () { return Promise.resolve({ success: true, min_x: -10, max_x: 20, min_y: -5, max_y: 15 }); } });
        }
        return orig(url, opts);
      };
      doc.getElementById('gmPrimary').click();
      await wait(dom, 300);
      eq(win.__test__.gmState.bbox, { minX: -10, maxX: 20, minY: -5, maxY: 15 }, 'bounds stored');
      win.__test__.gmSetStep(3);
      is(doc.getElementById('gmFrameArea').textContent, 'X[-10.00..20.00]  Y[-5.00..15.00]  30.00 x 20.00', 'area on step 3');
    });

    await test('a folder opens and the parent row goes back', async function () {
      var orig = win.fetch;
      win.fetch = function (url, opts) {
        if (String(url).indexOf('/api/grbl/list-gcode') >= 0 && String(url).indexOf('sub') >= 0) {
          return Promise.resolve({ ok: true, status: 200, json: function () {
            return Promise.resolve({ success: true, root: SD_ROOT, dir: SD_ROOT + '/sub',
                                     files: [{ path: SD_ROOT + '/sub/inner.gc', name: 'inner.gc', size: 900, is_dir: false }] });
          } });
        }
        return orig(url, opts);
      };
      win.__test__.gmSetStep(1);
      var rows = doc.querySelectorAll('#gmSdList .row'), sub = null;
      for (var i = 0; i < rows.length; i++) if (rows[i].textContent.indexOf('sub') >= 0) sub = rows[i];
      ok(sub, 'folder row exists');
      sub.click();
      await wait(dom, 200);
      is(win.__test__.gmState.sdDir, SD_ROOT + '/sub', 'folder opened');
      var first = doc.querySelector('#gmSdList .row');
      ok(first.textContent.indexOf('Parent folder') >= 0, 'parent row added');
      first.click();
      await wait(dom, 200);
      is(win.__test__.gmState.sdDir, SD_ROOT, 'back at the root');
    });

    await test('the phone tab keeps the picker and uploads with sid + token', function () {
      win.__test__.gmSetTab('phone');
      is(doc.getElementById('gmPrimary').textContent, 'SEND', 'phone upload action');
      var log = [], XHR = fakeXhr(log);
      win.XMLHttpRequest = XHR;
      win.__test__.gmSetFile({ name: 'job.gcode', size: 2048 });
      ok(win.__test__.gmUpload() === true, 'upload started');
      var last = XHR.last;
      is(last.method, 'POST', 'method');
      ok(last.url.indexOf('/api/grbl/psram-upload?') === 0, 'endpoint');
      ok(last.url.indexOf('name=job.gcode') > 0 && last.url.indexOf('token=tok-1') > 0, 'query');
      is(last.headers['X-Filename'], 'job.gcode', 'X-Filename');
      last.finish(200, { success: true, file_path: '/psram/job.gcode', bytes: 2048 });
      is(win.__test__.gmState.job.src, 'psram', 'job stored in the workspace');
    });

    dom.window.close();
  }

  // ── Step 2: preview ───────────────────────────────────────────────────
  {
    var dom = loadPage(BASE_API);
    await wait(dom, 300);
    var win = dom.window, doc = win.document;

    await test('toolpath points ignore comments, Z-only moves and non-motion lines', function () {
      var sink = win.__test__.gmPointsFromText([
        'G21', '(a comment X99 Y99)', 'G0 X0 Y0 Z5', 'G1 Z-1 F300',
        'G1 X10.5 Y20 ; side', 'M3 S100', 'G1 X-2 Y20', 'G1 X-2 Y0', 'M5'
      ].join('\n'));
      is(sink.count(), 4, 'four plotted moves');
      eq(sink.pts.slice(0, 2), [0, 0], 'starts at the origin');
      eq(sink.pts.slice(6, 8), [-2, 0], 'ends at the last move');
      var b = win.__test__.gmBounds(sink.pts);
      is(b.minX, -2, 'minX'); is(b.maxX, 10.5, 'maxX');
      is(b.minY, 0, 'minY'); is(b.maxY, 20, 'maxY');
      // G90/G21 (non-motion G words) must not switch off modal moves
      var sink2 = win.__test__.gmPointsFromText(['G21', 'G90', 'G0 X0 Y0', 'G1 X10 Y0',
                                                 'X20 Y5', 'G21 X30 Y0', 'G4 P0.2'].join('\n'));
      is(sink2.count(), 4, 'modal moves keep plotting after G90/G21');
      eq(sink2.pts.slice(2, 4), [10, 0], 'second point');
      eq(sink2.pts.slice(6, 8), [30, 0], 'fourth point');
    });

    await test('a phone file gives points and bounds immediately', async function () {
      win.__test__.gmSetFile({ name: 'job.gcode', size: 2048 });
      await wait(dom, 200);
      ok(win.__test__.gmState.points === null || win.__test__.gmState.pointsFor === '', 'no reader in jsdom -> no points');
    });

    await test('an SD preview streams the text and builds the points', async function () {
      win.__test__.gmSelectSd({ path: SD_ROOT + '/Cachola.gc', name: 'Cachola.gc', size: 517814 });
      var gcode = 'G90\nG0 X0 Y0\nG1 X10 Y0\nG1 X10 Y10\nG1 X0 Y10\nG1 X0 Y0\nM5\n';
      var orig = win.fetch;
      win.fetch = function (url, opts) {
        if (String(url).indexOf('/api/grbl/read-gcode-text') >= 0) {
          return Promise.resolve({ ok: true, status: 200, body: null,
            json: function () { return Promise.resolve({}); },
            text: function () { return Promise.resolve(gcode); } });
        }
        return orig(url, opts);
      };
      ok(win.__test__.gmPreviewSd() === true, 'preview started');
      await wait(dom, 400);
      is(win.__test__.gmState.points.length, 10, 'five points plotted');
      is(win.__test__.gmState.previewLoading, false, 'loading cleared');
      win.__test__.gmSetStep(2);
      ok(doc.getElementById('gmPrevInfo').textContent.indexOf('5 points') >= 0, 'info line: ' + doc.getElementById('gmPrevInfo').textContent);
    });

    dom.window.close();
  }

  // ── Step 3: frame ─────────────────────────────────────────────────────
  {
    var dom = loadPage(BASE_API);
    await wait(dom, 400);
    var win = dom.window, doc = win.document;
    win.__test__.gmSelectSd({ path: SD_ROOT + '/Cachola.gc', name: 'Cachola.gc', size: 517814 });
    win.__test__.gmState.bbox = { minX: 0, maxX: 10, minY: 0, maxY: 20 };
    win.__test__.gmSetStep(3);
    win.__postBodies = [];

    await test('frame needs bounds and sends the outline batch', async function () {
      ok(!doc.getElementById('gmPrimary').disabled, 'frame enabled with bounds');
      ok(win.__test__.gmRunFrame() === true, 'frame started');
      await wait(dom, 2500);
      var cmds = cmdsSent(win);
      is(cmds.length, 10, 'outline is M5 G90 G21 + 5 moves + M5');
      is(cmds[0], 'M5', 'starts with spindle off');
      ok(/^G1 X0\.000 Y0\.000/.test(cmds[3]), 'moves to the min corner: ' + cmds[3]);
      ok(/^M3 S/.test(cmds[4]), 'spindle on for the outline');
      is(cmds[9], 'M5', 'ends with spindle off');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Outline done') >= 0, 'done message');
    });

    await test('without bounds the frame action stays disabled', function () {
      win.__test__.gmState.bbox = null;
      win.__test__.gmSetStep(3);
      ok(doc.getElementById('gmPrimary').disabled, 'frame disabled');
    });

    dom.window.close();
  }

  // ── Step 4: play ──────────────────────────────────────────────────────
  {
    var dom = loadPage(Object.assign({}, BASE_API, {
      '/api/grbl/psram-stream': { success: true, message: 'ok' },
      '/api/grbl/stream': { success: true, message: 'ok' },
      '/api/grbl/pause': { success: true, message: 'ok' },
      '/api/grbl/resume': { success: true, message: 'ok' },
      '/api/grbl/cancel': { success: true, message: 'ok' }
    }));
    await wait(dom, 500);
    var win = dom.window, doc = win.document;
    var g = win.__test__;

    await test('machine buttons are always rendered and disabled while idle', function () {
      g.gmSetStep(4);
      ok(doc.getElementById('gmPause'), 'pause button');
      ok(doc.getElementById('gmResume'), 'resume button');
      ok(doc.getElementById('gmStop'), 'stop button');
      ok(doc.getElementById('gmPause').disabled, 'pause idle');
      ok(doc.getElementById('gmResume').disabled, 'resume idle');
      ok(doc.getElementById('gmStop').disabled, 'stop idle');
    });

    await test('start from the PSRAM workspace posts psram-stream', async function () {
      g.gmState.job = { src: 'psram', name: 'job.gcode', bytes: 2048, path: '' };
      win.__postBodies = [];
      g.gmSetStep(4);
      ok(g.gmStart() === true, 'start called');
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/psram-stream') >= 0, 'psram-stream requested');
      ok(/slot=0/.test(sent(win)), 'slot sent');
    });

    await test('start from the SD card posts stream with the file path', async function () {
      g.gmState.job = { src: 'sd', name: 'Cachola.gc', bytes: 517814, path: SD_ROOT + '/Cachola.gc' };
      win.__postBodies = [];
      g.gmStart();
      await wait(dom, 300);
      var body = sent(win);
      ok(body.indexOf('/api/grbl/stream') >= 0, 'stream requested');
      ok(body.indexOf(encodeURIComponent(SD_ROOT + '/Cachola.gc')) >= 0 ||
         body.indexOf(SD_ROOT + '/Cachola.gc') >= 0, 'path sent: ' + body);
    });

    await test('pause and resume are enabled by the machine state', async function () {
      g.gmState.running = true; g.gmState.paused = false;
      g.gmSetStep(4);
      ok(!doc.getElementById('gmPause').disabled, 'pause enabled while running');
      ok(doc.getElementById('gmResume').disabled, 'resume disabled while running');
      win.__postBodies = [];
      g.gmPause();
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/pause') >= 0, 'pause requested');
      g.gmState.paused = true;
      g.gmState.running = true;
      g.gmSetStep(4);
      ok(doc.getElementById('gmResume').disabled === false, 'resume enabled while paused');
      win.__postBodies = [];
      g.gmResume();
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/resume') >= 0, 'resume requested');
    });

    await test('stop cancels the job and clears the running state', async function () {
      g.gmState.running = true;
      g.gmSetStep(4);
      win.__postBodies = [];
      g.gmStop();
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/cancel') >= 0, 'cancel requested');
      is(g.gmState.running, false, 'running cleared');
      ok(doc.getElementById('gmMsg').textContent.indexOf('stopped') >= 0, 'stopped message');
    });

    dom.window.close();
  }

  // ── Step 5: jog + machine buttons ─────────────────────────────────────
  {
    var dom = loadPage(Object.assign({}, BASE_API, {
      '/api/grbl/pause': { success: true, message: 'ok' },
      '/api/grbl/resume': { success: true, message: 'ok' },
      '/api/grbl/cancel': { success: true, message: 'ok' }
    }));
    await wait(dom, 400);
    var win = dom.window, doc = win.document;
    var g = win.__test__;
    g.gmSetStep(5);
    win.__postBodies = [];

    await test('jog sends M5 then a relative $J move with the chosen step', async function () {
      g.gmState.jogStep = 10;
      ok(g.gmJog('X', 1) === true, 'jog started');
      await wait(dom, 300);
      var cmds = cmdsSent(win);
      is(cmds[0], 'M5', 'tool off first');
      ok(/^\$J=G91 G21 X10 F/.test(cmds[1]), 'relative jog: ' + cmds[1]);
      win.__postBodies = [];
      g.gmJog('Y', -1);
      await wait(dom, 300);
      ok(/^\$J=G91 G21 Y-10 F/.test(cmdsSent(win)[1]), 'negative jog');
    });

    await test('step chips change the jog distance', function () {
      var chips = doc.querySelectorAll('#gmStepChips button[data-step]');
      is(chips.length, 4, 'four steps');
      chips[0].click();
      is(g.gmState.jogStep, 0.1, '0.1 mm');
      ok(chips[0].className.indexOf('on') >= 0, 'chip highlighted');
      chips[2].click();
      is(g.gmState.jogStep, 10, '10 mm');
    });

    await test('Z jog is blocked on a 2-axis machine', async function () {
      g.gmState.axisCount = 2;
      g.gmSetStep(5);
      var zBtn = doc.querySelector('#gmJogPad button[data-jog="Z+"]');
      ok(zBtn.disabled, 'Z button disabled');
      win.__postBodies = [];
      is(g.gmJog('Z', 1), false, 'jog refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('no Z axis') >= 0, 'explains why');
    });

    await test('the machine buttons send the GRBL commands', async function () {
      g.gmState.axisCount = 3;
      win.__postBodies = [];
      g.gmSetStep(5);

      g.gmAct('unlock');
      await wait(dom, 200);
      ok(cmdsSent(win).indexOf('$X') >= 0, '$X sent');

      g.gmAct('home');                       // armed first
      ok(doc.getElementById('gmMsg').textContent.indexOf('Tap again') >= 0, 'home asks for confirmation');
      win.__postBodies = [];
      g.gmAct('home');                       // second tap runs it
      await wait(dom, 200);
      ok(cmdsSent(win).indexOf('$H') >= 0, '$H sent after the second tap');

      win.__postBodies = [];
      g.gmAct('zero');
      g.gmAct('zero');
      await wait(dom, 200);
      ok(cmdsSent(win).join(' ').indexOf('G92 X0 Y0 Z0') >= 0, 'G92 zero sent');

      win.__postBodies = [];
      g.gmAct('reset');
      g.gmAct('reset');
      await wait(dom, 200);
      is(cmdsSent(win).length, 1, 'one reset command');
      is(cmdsSent(win)[0], String.fromCharCode(24), 'Ctrl-X');

      win.__postBodies = [];
      g.gmAct('cancel');
      await wait(dom, 200);
      ok(sent(win).indexOf('/api/grbl/cancel') >= 0, 'cancel endpoint');
    });

    dom.window.close();
  }

  // ── Guards ────────────────────────────────────────────────────────────
  {
    var dom = loadPage(Object.assign({}, BASE_API, {
      '/api/grbl/control-token': { success: true, token: 't', can_write: false }
    }));
    await wait(dom, 400);
    var win = dom.window, doc = win.document;

    await test('without write control the actions stay disabled and say why', function () {
      win.__test__.gmSetTab('phone');
      win.__test__.gmSetFile({ name: 'job.gcode', size: 100 });
      ok(doc.getElementById('gmPrimary').disabled, 'send disabled');
      win.__test__.gmSetStep(5);
      ok(win.__test__.gmJog('X', 1) === false, 'jog refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Another client') >= 0, 'explains the lock');
    });

    await test('a phone file above the 4 MB workspace cap is refused', function () {
      win.__test__.gmState.canWrite = true;
      win.__test__.gmSetFile({ name: 'huge.nc', size: 5 * 1024 * 1024 });
      is(win.__test__.gmUpload(), false, 'refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('4 MB') >= 0, 'cap message');
    });

    await test('the SD list survives a controller error (message, no crash)', async function () {
      var bad = loadPage(Object.assign({}, BASE_API, { '/api/grbl/list-gcode': { success: false } }));
      await wait(bad, 400);
      ok(bad.window.document.getElementById('gmSdList').textContent.indexOf('Could not read') >= 0,
         'error shown');
      bad.window.close();
    });

    dom.window.close();
  }

  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) { console.error('Fatal:', e); process.exit(1); });
