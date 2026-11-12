// Mobile G-code wizard — jsdom tests for the three steps.
//
//   1 Send   : pick + upload the job to the controller PSRAM workspace
//   2 Frame  : XY bounds from the file, outline as a G-code batch
//   3 Play   : start the stored job, stop it
// Plus the wizard contract: one screen at a time, arrow gating, no scroll CSS.
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'gcode_mobile.html');
var passed = 0, failed = 0;

async function test(name, fn) {
  try { await fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function loadPage(api, hash) {
  var html = fs.readFileSync(PAGE, 'utf8');
  return new JSDOM(html, {
    url: 'http://localhost/m' + (hash || ''),
    runScripts: 'dangerously',
    pretendToBeVisual: true,
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
  var ids = ['gmS1', 'gmS2', 'gmS3'];
  for (var i = 0; i < ids.length; i++) {
    if (doc.getElementById(ids[i]).className.indexOf('on') >= 0) return i + 1;
  }
  return 0;
}

async function run() {
  console.log('\n=== Mobile G-code wizard (3 steps) ===\n');

  // ── Wizard contract + layout ──────────────────────────────────────────
  {
    var dom = loadPage({});
    await wait(dom);
    var win = dom.window, doc = win.document, css = doc.querySelector('style').textContent;

    await test('root is a fixed 100dvh column with no scroll anywhere', function () {
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

    await test('exactly one of the three screens is visible', function () {
      is(screenOn(doc), 1, 'starts on step 1');
      is(doc.querySelectorAll('#gm .screen.on').length, 1, 'one screen on');
      is(doc.getElementById('gmNo').textContent, '1 / 3', 'counter');
    });

    await test('arrow gating: next waits for a job, back is disabled on step 1', function () {
      ok(doc.getElementById('gmBack').disabled, 'back disabled on 1');
      ok(doc.getElementById('gmNext').disabled, 'next disabled without a job');
      win.__test__.gmState.jobName = 'job.gcode';   // as if already uploaded
      win.__test__.gmSetStep(1);
      ok(!doc.getElementById('gmNext').disabled, 'next enabled with a job');
    });

    await test('each step shows its own title and primary action', function () {
      win.__test__.gmSetStep(2);
      is(doc.getElementById('gmTitle').textContent, 'Frame', 'step 2 title');
      is(doc.getElementById('gmPrimary').textContent, 'FRAME', 'step 2 action');
      win.__test__.gmSetStep(3);
      is(doc.getElementById('gmTitle').textContent, 'Play', 'step 3 title');
      is(doc.getElementById('gmPrimary').textContent, 'START', 'step 3 action');
      is(doc.getElementById('gmNo').textContent, '3 / 3', 'counter 3');
      ok(doc.getElementById('gmNext').disabled, 'next disabled on the last step');
    });

    dom.window.close();
  }

  // ── Step 1: send ──────────────────────────────────────────────────────
  {
    var log = [];
    var dom = loadPage({ '/api/grbl/control-token': { success: true, token: 'tok-1', can_write: true },
                         '/api/grbl/status': { usb_running: true, workflow_state: 'Idle' },
                         '/api/grbl/psram-file': { success: true, has_file: false } });
    await wait(dom, 500);
    var win = dom.window, doc = win.document;
    var XHR = fakeXhr(log);
    win.XMLHttpRequest = XHR;

    await test('load fetches the control token and shows the machine badge', function () {
      is(win.__test__.gmState.token, 'tok-1', 'token');
      is(doc.getElementById('gmLink').textContent, 'online', 'badge');
    });

    await test('bounds are computed from the file (comments and Z-only moves ignored)', function () {
      var b = win.__test__.gmBounds([
        'G21', '(job 100x50)', 'G0 X0 Y0 Z5', 'G1 Z-1 F300',
        'G1 X10.5 Y0', 'G1 X10.5 Y20 ; side', 'G1 X-2 Y20', 'G1 X-2 Y0', 'M5'
      ].join('\n'));
      is(b.minX, -2, 'minX'); is(b.maxX, 10.5, 'maxX'); is(b.minY, 0, 'minY'); is(b.maxY, 20, 'maxY');
      is(win.__test__.gmArea(b), 'X[-2.00..10.50]  Y[0.00..20.00]  12.50 x 20.00', 'area text');
    });

    await test('upload posts the file with sid, token and headers', function () {
      win.__test__.gmSetFile({ name: 'job.gcode', size: 2048 });
      ok(win.__test__.gmUpload() === true, 'upload started');
      var last = XHR.last;
      is(last.method, 'POST', 'method');
      ok(last.url.indexOf('/api/grbl/psram-upload?') === 0, 'endpoint');
      ok(last.url.indexOf('name=job.gcode') > 0 && last.url.indexOf('token=tok-1') > 0, 'query');
      is(last.headers['X-Filename'], 'job.gcode', 'X-Filename');
      is(doc.getElementById('gmFill').style.width, '50%', 'progress');
    });

    await test('success stores the job and unlocks the arrow', function () {
      XHR.last.finish(200, { success: true, file_path: '/psram/job.gcode', bytes: 2048 });
      ok(doc.getElementById('gmMsg').textContent.indexOf('/psram/job.gcode') >= 0, 'path shown');
      is(win.__test__.gmState.jobName, 'job.gcode', 'job name kept');
      ok(!doc.getElementById('gmNext').disabled, 'next enabled');
    });

    dom.window.close();
  }

  // ── Step 2: frame ─────────────────────────────────────────────────────
  {
    var dom = loadPage({ '/api/grbl/control-token': { success: true, token: 't', can_write: true },
                         '/api/grbl/status': { usb_running: true, workflow_state: 'Idle' },
                         '/api/grbl/send': { success: true, message: 'ok' } });
    await wait(dom, 500);
    var win = dom.window, doc = win.document;
    win.__test__.gmSetFile({ name: 'job.gcode', size: 2048 });
    await wait(dom, 200);          // let the (stubbed) file read settle before faking bounds
    win.__postBodies = [];

    await test('frame is blocked until a job with bounds exists', async function () {
      is(win.__test__.gmState.bbox, null, 'no bounds before the file is read');
      win.__test__.gmState.jobName = 'job.gcode';   // step 2 frames the job that was sent
      win.__test__.gmSetStep(2);
      win.__test__.gmState.bbox = { minX: 0, maxX: 10, minY: 0, maxY: 20 };
      win.__test__.gmSetStep(2);
      ok(!doc.getElementById('gmPrimary').disabled, 'frame enabled with bounds');
      is(doc.getElementById('gmFrameArea').textContent, 'X[0.00..10.00]  Y[0.00..20.00]  10.00 x 20.00', 'area shown');
    });

    await test('frame sends the outline batch to /api/grbl/send', async function () {
      var before = (win.__postBodies || []).length;
      ok(win.__test__.gmRunFrame() === true, 'frame started');
      await wait(dom, 2500);
      var bodies = (win.__postBodies || []).slice(before);
      var cmds = bodies.filter(function (b) { return b.url.indexOf('/api/grbl/send') >= 0; })
                       .map(function (b) { return decodeURIComponent(String(b.body)).replace(/.*cmd=/, '').split('&')[0]; });
      is(cmds.length, 10, 'outline is M5 G90 G21 + 5 moves + M5');
      is(cmds[0], 'M5', 'starts with spindle off');
      ok(/^G1 X0\.000 Y0\.000/.test(cmds[3]), 'moves to the min corner: ' + cmds[3]);
      ok(/^M3 S/.test(cmds[4]), 'spindle on for the outline');
      is(cmds[9], 'M5', 'ends with spindle off');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Outline done') >= 0, 'done message');
    });

    dom.window.close();
  }

  // ── Step 3: play ──────────────────────────────────────────────────────
  {
    var dom = loadPage({ '/api/grbl/control-token': { success: true, token: 't', can_write: true },
                         '/api/grbl/status': { usb_running: true, workflow_state: 'Idle' },
                         '/api/grbl/psram-file': { success: true, has_file: true, name: 'job.gcode', size: 2048 },
                         '/api/grbl/psram-stream': { success: true, message: 'ok' },
                         '/api/grbl/cancel': { success: true, message: 'ok' } });
    await wait(dom, 600);
    var win = dom.window, doc = win.document;

    await test('a job already on the controller survives a reload', function () {
      is(win.__test__.gmState.jobName, 'job.gcode', 'name from /api/grbl/psram-file');
      win.__test__.gmSetStep(3);
      is(doc.getElementById('gmPlayJob').textContent, 'job.gcode', 'shown on step 3');
      ok(!doc.getElementById('gmPrimary').disabled, 'start enabled');
    });

    await test('start streams the stored job', async function () {
      win.__postBodies = [];
      ok(win.__test__.gmStart() === true, 'start called');
      await wait(dom, 400);
      var body = (win.__postBodies || []).map(function (b) { return b.url + ' ' + b.body; }).join(' ');
      ok(body.indexOf('/api/grbl/psram-stream') >= 0, 'psram-stream requested');
      ok(/slot=0/.test(body), 'slot sent');
      ok(doc.getElementById('gmMsg').textContent.indexOf('started') >= 0, 'started message');
    });

    await test('stop cancels the stream and reveals the STOP button only while running', async function () {
      win.__test__.gmState.running = true;
      win.__test__.gmSetStep(3);
      ok(doc.getElementById('gmStop').className.indexOf('on') >= 0, 'STOP visible while running');
      win.__postBodies = [];
      win.__test__.gmStop();
      await wait(dom, 400);
      var body = (win.__postBodies || []).map(function (b) { return b.url + ' ' + b.body; }).join(' ');
      ok(body.indexOf('/api/grbl/cancel') >= 0, 'cancel requested');
      ok(doc.getElementById('gmMsg').textContent.indexOf('stopped') >= 0, 'stopped message');
    });

    dom.window.close();
  }

  // ── Guards ────────────────────────────────────────────────────────────
  {
    var dom = loadPage({ '/api/grbl/control-token': { success: true, token: 't', can_write: false },
                         '/api/grbl/status': { usb_running: true } });
    await wait(dom, 400);
    var win = dom.window, doc = win.document;

    await test('without write control every action stays disabled and says why', function () {
      win.__test__.gmSetFile({ name: 'job.gcode', size: 100 });
      ok(doc.getElementById('gmPrimary').disabled, 'send disabled');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Another client') >= 0, 'explains the lock');
    });

    await test('a file above the 4 MB workspace cap is refused', function () {
      win.__test__.gmState.canWrite = true;
      win.__test__.gmSetFile({ name: 'huge.nc', size: 5 * 1024 * 1024 });
      is(win.__test__.gmUpload(), false, 'refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('4 MB') >= 0, 'cap message');
    });

    dom.window.close();
  }

  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) { console.error('Fatal:', e); process.exit(1); });
