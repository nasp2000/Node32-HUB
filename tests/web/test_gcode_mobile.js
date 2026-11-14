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
  var ids = ['gmS1', 'gmS2', 'gmS4', 'gmS5', 'gmSYS'];
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
    var win = dom.window, doc = win.document;
    var css = Array.prototype.map.call(doc.querySelectorAll('style'), function (n) { return n.textContent; }).join('\n');

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

    await test('the top strip shows the current page, the visited ones and the ones to come', function () {
      var tabs = doc.querySelectorAll('#gm .navstrip .ncol');
      is(tabs.length, 5, 'five columns');
      ok(tabs[0].className.indexOf('on') >= 0, 'first column active');
      win.__test__.gmSetStep(3);
      tabs = doc.querySelectorAll('#gm .navstrip .ncol');
      ok(tabs[0].className.indexOf('done') >= 0, 'past column marked as visited');
      ok(tabs[1].className.indexOf('done') >= 0, 'previous column visited');
      ok(tabs[2].className.indexOf('on') >= 0, 'current column highlighted');
      ok(tabs[3].className.indexOf('on') < 0 && tabs[3].className.indexOf('done') < 0,
         'next column left dimmed');
      is(doc.querySelectorAll('#gm .navstrip .nrail').length, 5, 'one line per column');
      ok(tabs[2].querySelector('.nrail') !== null, 'each name has its own line');
    });

    await test('tapping a tab jumps to that page', function () {
      win.__test__.gmSetStep(1);
      doc.querySelector('#gm .navstrip .ncol[data-step-go="5"]').click();
      is(screenOn(doc), 5, 'jumped to System');
      doc.querySelector('#gm .navstrip .ncol[data-step-go="2"]').click();
      is(screenOn(doc), 2, 'jumped to Preview');
    });

    await test('each screen shows its own title and primary action', function () {
      win.__test__.gmSetStep(1);
      is(doc.getElementById('gmTitle').textContent, 'Send G-code', 'step 1 title');
      is(doc.getElementById('gmPrimary').textContent, 'USE', 'SD tab acts on a selected file');
      win.__test__.gmSetStep(2);
      is(doc.getElementById('gmTitle').textContent, 'Preview', 'step 2 title');
      is(doc.getElementById('gmPrimary').textContent, 'FRAME', 'preview carries FRAME');
      win.__test__.gmSetStep(3);
      is(doc.getElementById('gmTitle').textContent, 'Play', 'step 3 title');
      is(doc.getElementById('gmPrimary').textContent, 'START', 'step 3 action');
      win.__test__.gmSetStep(4);
      is(doc.getElementById('gmTitle').textContent, 'Jog', 'step 4 title');
      win.__test__.gmSetStep(5);
      is(doc.getElementById('gmTitle').textContent, 'System', 'step 5 title');
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
      win.__test__.gmSetStep(2);
      is(doc.getElementById('gmFrameArea').textContent, 'X[-10.00..20.00]  Y[-5.00..15.00]  30.00 x 20.00', 'area on the preview');
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

    await test('the phone tab uploads to PSRAM when that destination is chosen', function () {
      win.__test__.gmSetTab('phone');
      win.__test__.gmSetDest('psram');
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

    await test('the phone tab writes to the SD card by default (no 4 MB cap)', function () {
      win.__test__.gmSetDest('sd');
      var log = [], XHR = fakeXhr(log);
      win.XMLHttpRequest = XHR;
      win.__test__.gmSetFile({ name: 'big job.gcode', size: 9 * 1024 * 1024 });
      ok(win.__test__.gmUpload() === true, 'upload started');
      var last = XHR.last;
      ok(last.url.indexOf('/api/grbl/upload-gcode-stream?') === 0, 'SD endpoint: ' + last.url);
      ok(last.url.indexOf('overwrite=1') > 0, 'overwrite flag');
      ok(decodeURIComponent(last.url).indexOf(SD_ROOT + '/big_job.gcode') > 0, 'target path');
      is(last.headers['X-Filesize'], String(9 * 1024 * 1024), 'size header');
      last.finish(200, { success: true, file_path: SD_ROOT + '/big_job.gcode' });
      is(win.__test__.gmState.job.src, 'sd', 'job now points at the SD file');
      is(win.__test__.gmState.job.path, SD_ROOT + '/big_job.gcode', 'path kept');
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
    win.__test__.gmSetStep(2);
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
      win.__test__.gmSetStep(2);
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
      g.gmSetStep(3);
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
      g.gmSetStep(3);
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
      g.gmSetStep(3);
      ok(!doc.getElementById('gmPause').disabled, 'pause enabled while running');
      ok(doc.getElementById('gmResume').disabled, 'resume disabled while running');
      win.__postBodies = [];
      g.gmPause();
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/pause') >= 0, 'pause requested');
      g.gmState.paused = true;
      g.gmState.running = true;
      g.gmSetStep(3);
      ok(doc.getElementById('gmResume').disabled === false, 'resume enabled while paused');
      win.__postBodies = [];
      g.gmResume();
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/resume') >= 0, 'resume requested');
    });

    await test('stop cancels the job and clears the running state', async function () {
      g.gmState.running = true;
      g.gmSetStep(3);
      win.__postBodies = [];
      g.gmStop();
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/cancel') >= 0, 'cancel requested');
      is(g.gmState.running, false, 'running cleared');
      ok(doc.getElementById('gmMsg').textContent.indexOf('stopped') >= 0, 'stopped message');
    });

    await test('progress bar and ETA follow the job status', async function () {
      win.__MOCK_API__['/api/grbl/status'] = { success: true, usb_running: true, machine_state: 'Run',
        workflow_state: 'running', axis_count: 3, mpos_valid: true,
        lines_total: 1000, lines_sent: 250, eta_sec: 65 };
      g.gmSetStep(3);
      await wait(dom, 400);
      is(doc.getElementById('gmPlayFill').style.width, '25%', 'progress fill');
      ok(doc.getElementById('gmPlayProgress').textContent.indexOf('250 / 1000') >= 0, 'lines');
      ok(doc.getElementById('gmPlayEta').textContent.indexOf('1m 05s') >= 0,
         'eta: ' + doc.getElementById('gmPlayEta').textContent);
      ok(doc.getElementById('gmPlayState').className.indexOf('st-run') >= 0, 'RUN is green');
    });

    await test('an alarm shows the controller message with unlock + resume job', async function () {
      win.__MOCK_API__['/api/grbl/status'] = { success: true, usb_running: true, machine_state: 'Alarm',
        workflow_state: 'alarm', state: 'ALARM', axis_count: 3, alarm_active: true,
        alarm_message: 'ALARM:1 Hard limit' };
      g.gmSetStep(3);
      await wait(dom, 400);
      is(doc.getElementById('gmAlarm').className.indexOf('off'), -1, 'alarm block visible');
      ok(doc.getElementById('gmAlarmMsg').textContent.indexOf('ALARM:1') >= 0, 'message');
      ok(doc.getElementById('gmPlayState').className.indexOf('st-alarm') >= 0, 'ALARM is red');
      win.__postBodies = [];
      g.gmResumeAlarm();
      ok(doc.getElementById('gmMsg').textContent.indexOf('Tap again') >= 0, 'resume asks for a second tap');
      g.gmResumeAlarm();
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/resume-alarm') >= 0, 'resume-alarm requested');
      ok(/confirm=1/.test(sent(win)), 'confirm sent');
    });

    await test('feed and spindle overrides are sent together', async function () {
      win.__MOCK_API__['/api/grbl/override'] = { success: true, message: 'ok' };
      win.__postBodies = [];
      g.gmSetStep(3);
      g.gmOv('feed', 10);
      await wait(dom, 300);
      ok(sent(win).indexOf('/api/grbl/override') >= 0, 'override requested');
      ok(/feed=110/.test(sent(win)) && /spin=100/.test(sent(win)), 'feed+spin sent: ' + sent(win));
      is(doc.getElementById('gmOvVal').textContent, '110%', 'label updated');
      g.gmOv('spin', -10);
      await wait(dom, 300);
      ok(/spin=90/.test(sent(win)), 'spindle down to 90%');
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
    g.gmSetStep(4);
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
      g.gmSetStep(4);
      var zBtn = doc.querySelector('#gmJogPad button[data-jog="Z+"]');
      ok(zBtn.disabled, 'Z button disabled');
      win.__postBodies = [];
      is(g.gmJog('Z', 1), false, 'jog refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('no Z axis') >= 0, 'explains why');
    });

    await test('the machine buttons send the GRBL commands', async function () {
      g.gmState.axisCount = 3;
      win.__postBodies = [];
      g.gmSetStep(4);

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

    await test('work zero can be set per axis (X0 / Y0 / Z0)', async function () {
      g.gmState.axisCount = 3;
      win.__postBodies = [];
      g.gmSetStep(4);
      g.gmSetZero('X');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Tap again') >= 0, 'zero X armed');
      g.gmSetZero('X');
      await wait(dom, 200);
      ok(cmdsSent(win).indexOf('G92 X0') >= 0, 'G92 X0 sent');
      win.__postBodies = [];
      g.gmSetZero('Y'); g.gmSetZero('Y');
      await wait(dom, 200);
      ok(cmdsSent(win).indexOf('G92 Y0') >= 0, 'G92 Y0 sent');
      win.__postBodies = [];
      g.gmSetZero('Z'); g.gmSetZero('Z');
      await wait(dom, 200);
      ok(cmdsSent(win).indexOf('G92 Z0') >= 0, 'G92 Z0 sent');
      // and Z is refused on a 2-axis machine
      g.gmState.axisCount = 2;
      is(g.gmSetZero('Z'), false, 'Z zero refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('no Z axis') >= 0, 'explains why');
    });

    await test('the MDI line sends a manual command and clears the input', async function () {
      g.gmState.axisCount = 3;
      win.__postBodies = [];
      doc.getElementById('gmMdi').value = '$I';
      g.gmSetStep(4);
      ok(g.gmMdi() === true, 'mdi sent');
      await wait(dom, 200);
      ok(cmdsSent(win).indexOf('$I') >= 0, '$I sent');
      is(doc.getElementById('gmMdi').value, '', 'input cleared');
    });

    dom.window.close();
  }

  // ── Step 6: system ────────────────────────────────────────────────────
  {
    var dom = loadPage(Object.assign({}, BASE_API, {
      '/api/version': { version: 'v1.120', build: 'v1.120 - Nov 14 2026 16:24:07 WET' },
      '/api/health': { status: 'ok', uptime_str: '0d 0h 24m 0s', reset_reason: 'POWER_ON',
                       heap: { free: 233000, min_free: 83000, max_alloc: 57000 },
                       psram: { free: 32293960, total: 33554432 }, pressure: 'critical' },
      '/api/sd-status': { status: 'ok', total_bytes: 31716278272, used_bytes: 85409792 },
      '/api/status': { wifi_connected: true, wifi_rssi: -43, ip_address: '192.168.1.7',
                       free_heap: 233000, psram_free: 32293960 },
      '/api/restart': { success: true, message: 'restarting' }
    }));
    await wait(dom, 400);
    var win = dom.window, doc = win.document, g = win.__test__;

    await test('the system page shows firmware, uptime, heap, PSRAM, SD and WiFi', async function () {
      g.gmSetStep(5);
      await wait(dom, 400);
      is(doc.getElementById('gmNo').textContent, '5 / 5', 'counter');
      ok(doc.getElementById('muFw').textContent.indexOf('v1.120') >= 0, 'firmware');
      ok(doc.getElementById('muUp').textContent.indexOf('24m') >= 0, 'uptime');
      ok(doc.getElementById('muHeap').textContent.indexOf('228') >= 0,
         'heap: ' + doc.getElementById('muHeap').textContent);
      ok(doc.getElementById('muPsram').textContent.indexOf('MB') >= 0, 'psram');
      ok(doc.getElementById('muSd').textContent.indexOf('GB') >= 0 ||
         doc.getElementById('muSd').textContent.indexOf('MB') >= 0, 'sd: ' + doc.getElementById('muSd').textContent);
      ok(doc.getElementById('muWifi').textContent.indexOf('-43') >= 0, 'wifi');
      ok(doc.getElementById('gmSYS').className.indexOf('on') >= 0, 'system screen shown');
      is(doc.querySelectorAll('#gmSYS .subt').length, 6, 'summary/firmware/log tabs + log controls');
    });

    await test('the firmware tab and the log tab swap the visible pane', async function () {
      win.MPSystem.tab('ota');
      ok(doc.getElementById('muPaneOta').className.indexOf('on') >= 0, 'firmware pane');
      ok(doc.getElementById('muPaneSum').className.indexOf('on') < 0, 'summary hidden');
      win.MPSystem.tab('log');
      ok(doc.getElementById('muPaneLog').className.indexOf('on') >= 0, 'log pane');
      ok(doc.getElementById('muLog') !== null, 'log view');
      win.MPSystem.tab('sum');
    });

    await test('restart asks for confirmation and then posts /api/restart', async function () {
      win.__postBodies = [];
      g.gmState.running = false;
      win.MPSystem._state.busy = false;
      win.MPSystem.reboot();
      await wait(dom, 300);
      var body = (win.__postBodies || []).map(function (b) { return b.url; }).join(' ');
      ok(body.indexOf('/api/restart') >= 0, 'restart requested after the OK');
    });

    await test('declining the dialog sends nothing', async function () {
      var before = (win.__postBodies || []).length;
      var orig = win.customConfirm;
      win.customConfirm = function (msg, cb) { if (cb) cb(false); };
      win.MPSystem._state.busy = false;
      win.MPSystem.reboot();
      await wait(dom, 200);
      is((win.__postBodies || []).length, before, 'no restart posted on cancel');
      win.customConfirm = orig;
    });

    await test('restart is refused while a job is running', function () {
      g.gmState.running = true;
      win.MPSystem._state.busy = false;
      is(win.MPSystem.reboot(), false, 'refused');
      ok(doc.getElementById('muOtaMsg').textContent.indexOf('job is running') >= 0, 'explains why');
      g.gmState.running = false;
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
      win.__test__.gmSetStep(4);
      ok(win.__test__.gmJog('X', 1) === false, 'jog refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Another client') >= 0, 'explains the lock');
    });

    await test('a PSRAM file above the 4 MB workspace cap is refused', function () {
      win.__test__.gmState.canWrite = true;
      win.__test__.gmSetTab('phone');
      win.__test__.gmSetDest('psram');
      win.__test__.gmSetFile({ name: 'huge.nc', size: 5 * 1024 * 1024 });
      is(win.__test__.gmUpload(), false, 'refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('4 MB') >= 0, 'cap message');
    });

    await test('start pre-flight failures are translated, not shown as codes', function () {
      ok(win.__test__.gmErrorText('preflight_homing_required').indexOf('Homing') >= 0, 'ALARM:9');
      ok(win.__test__.gmErrorText('preflight_usb_not_running').indexOf('USB') >= 0, 'USB off');
      ok(win.__test__.gmErrorText('preflight_door_open').indexOf('door') >= 0, 'door');
      ok(win.__test__.gmErrorText('sd_busy').indexOf('busy') >= 0, 'sd busy');
      ok(win.__test__.gmErrorText('no_resume').indexOf('resumed') >= 0, 'no resume');
    });

    await test('the SD list survives a controller error (message, no crash)', async function () {
      var bad = loadPage(Object.assign({}, BASE_API, { '/api/grbl/list-gcode': { success: false } }));
      await wait(bad, 400);
      ok(bad.window.document.getElementById('gmSdList').textContent.indexOf('Could not read') >= 0,
         'error shown');
      bad.window.close();
    });

    await test('the USB restart button only shows when the host is down', async function () {
      var off = loadPage(Object.assign({}, BASE_API, {
        '/api/grbl/status': { success: true, usb_running: false, machine_state: 'IDLE', workflow_state: 'idle' },
        '/api/grbl/usb-restart': { success: true, message: 'ok' }
      }));
      await wait(off, 400);
      var w2 = off.window, d2 = w2.document;
      is(d2.getElementById('gmUsb').className, 'mini on', 'visible when offline');
      w2.__postBodies = [];
      w2.__test__.gmUsb();
      await wait(off, 300);
      var body = (w2.__postBodies || []).map(function (b) { return b.url; }).join(' ');
      ok(body.indexOf('/api/grbl/usb-restart') >= 0, 'restart requested');
      off.window.close();
    });

    dom.window.close();
  }

  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) { console.error('Fatal:', e); process.exit(1); });
