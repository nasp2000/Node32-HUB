// Mobile G-code wizard, step 1 ("Send G-code") — jsdom tests.
//
// Scope of the page: vertical, no scroll, one action (upload the job to the
// controller's PSRAM workspace). The PC controller page is a different asset
// and is not exercised here.
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'gcode_mobile.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function loadPage(api) {
  var html = fs.readFileSync(PAGE, 'utf8');
  var dom = new JSDOM(html, {
    url: 'http://localhost/m',
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    beforeParse: function (w) { w.__MOCK_API__ = api || {}; },
  });
  return dom;
}
function wait(dom, ms) { return new Promise(function (r) { setTimeout(function () { r(dom); }, ms || 300); }); }

// Minimal XHR double: records the request and lets the test fire the callbacks.
function fakeXhr(log) {
  function X() {
    this.upload = {};
    X.last = this;
  }
  X.prototype.open = function (m, u) { this.method = m; this.url = u; log.push({ method: m, url: u }); };
  X.prototype.setRequestHeader = function (k, v) { (this.headers = this.headers || {})[k] = v; };
  X.prototype.send = function (body) {
    this.body = body;
    if (this.upload.onprogress) this.upload.onprogress({ lengthComputable: true, loaded: 50, total: 100 });
  };
  X.prototype.finish = function (status, body) {
    this.status = status;
    this.responseText = JSON.stringify(body);
    if (this.onload) this.onload();
  };
  return X;
}

async function run() {
  console.log('\n=== Mobile G-code wizard (step 1) ===\n');

  // ── Layout: vertical, no scroll ────────────────────────────────────────
  {
    var dom = loadPage({});
    await wait(dom);
    var win = dom.window, doc = win.document, css = doc.querySelector('style').textContent;

    test('page is a single vertical flex column that fills the viewport', function () {
      ok(/#gm\s*\{[^}]*display:\s*flex/.test(css), 'flex root');
      ok(/#gm\s*\{[^}]*flex-direction:\s*column/.test(css), 'column direction');
      ok(/#gm\s*\{[^}]*height:\s*100dvh/.test(css), 'uses the dynamic viewport height');
    });

    test('vertical scroll is disabled on the page', function () {
      ok(/html,\s*body\s*\{[^}]*overflow:\s*hidden/.test(css), 'html/body overflow hidden');
      ok(/#gm\s*\{[^}]*overflow:\s*hidden/.test(css), '#gm overflow hidden');
      is(win.getComputedStyle(doc.body).overflow, 'hidden', 'computed body overflow');
    });

    test('the pick zone absorbs the free height (no overflow to scroll)', function () {
      ok(/#gm\s*\.pick\s*\{[^}]*flex:\s*1 1 auto/.test(css), 'pick grows');
      ok(/#gm\s*\.pick\s*\{[^}]*min-height:\s*0/.test(css), 'min-height 0 keeps flex children shrinkable');
    });

    test('only the step-1 controls are on screen', function () {
      is(doc.querySelectorAll('#gm .send').length, 1, 'one primary action');
      is(doc.querySelectorAll('#gm .nxt').length, 1, 'one advance arrow');
      is(doc.getElementById('gmNo').textContent.trim(), '1 / 3', 'step counter');
      is(doc.querySelectorAll('#gm .steps i.on').length, 1, 'first dot active');
      ok(doc.getElementById('gmNext').disabled, 'arrow disabled (steps 2-3 are other pages)');
    });

    test('send starts disabled and enables only with a file', function () {
      ok(doc.getElementById('gmSend').disabled, 'disabled by default');
      win.__test__.gmSetFile({ name: 'job.gcode', size: 2048 });
      ok(!doc.getElementById('gmSend').disabled, 'enabled once a file is chosen');
    });

    dom.window.close();
  }

  // ── Behaviour: send the job ───────────────────────────────────────────
  {
    var log = [];
    var dom = loadPage({ '/api/grbl/control-token': { success: true, token: 'tok-1', can_write: true },
                         '/api/grbl/status': { usb_running: true, state: 'Idle' } });
    await wait(dom, 500);
    var win = dom.window, doc = win.document;
    var XHR = fakeXhr(log);
    win.XMLHttpRequest = XHR;

    test('control token and machine state are fetched on load', function () {
      is(win.__test__.gmState.token, 'tok-1', 'token stored');
      is(win.__test__.gmState.canWrite, true, 'writes allowed');
      is(doc.getElementById('gmLink').textContent.trim(), 'online', 'machine badge');
    });

    test('cleanName keeps a safe basename and a .nc default', function () {
      is(win.__test__.gmCleanName('C:\\Users\\x\\peça final.gcode'), 'pe_a_final.gcode', 'sanitised');
      is(win.__test__.gmCleanName('job'), 'job.nc', 'extension added');
    });

    test('fmtBytes switches units', function () {
      is(win.__test__.gmFmtBytes(512), '512 B', 'bytes');
      is(win.__test__.gmFmtBytes(2048), '2.0 kB', 'kB');
      is(win.__test__.gmFmtBytes(3 * 1024 * 1024), '3.0 MB', 'MB');
    });

    test('sending posts the job to /api/grbl/psram-upload with sid, token and headers', function () {
      win.__test__.gmSetFile({ name: 'job.gcode', size: 2048 });
      ok(win.__test__.gmSend() === true, 'send started');
      var last = XHR.last;
      is(last.method, 'POST', 'method');
      ok(last.url.indexOf('/api/grbl/psram-upload?') === 0, 'endpoint: ' + last.url);
      ok(last.url.indexOf('name=job.gcode') > 0, 'name');
      ok(last.url.indexOf('sid=') > 0, 'sid');
      ok(last.url.indexOf('token=tok-1') > 0, 'token');
      is(last.headers['X-Filename'], 'job.gcode', 'X-Filename');
      is(last.headers['X-Filesize'], '2048', 'X-Filesize');
      is(last.body && last.body.name, 'job.gcode', 'raw body is the file');
    });

    test('progress is shown while the upload runs', function () {
      is(doc.getElementById('gmBar').className, 'bar on', 'bar visible');
      is(doc.getElementById('gmFill').style.width, '50%', 'fill follows the upload');
    });

    test('success reports the stored job and keeps the wizard on step 1', function () {
      XHR.last.finish(200, { success: true, file_path: '/psram/job.gcode', bytes: 2048 });
      var msg = doc.getElementById('gmMsg');
      ok(msg.className.indexOf('ok') >= 0, 'ok message');
      ok(msg.textContent.indexOf('/psram/job.gcode') >= 0, 'path shown: ' + msg.textContent);
      ok(doc.getElementById('gmNext').disabled, 'arrow still disabled (no step 2 yet)');
      is(win.__test__.gmState.ready, true, 'job ready');
    });

    test('a rejected upload shows the mapped error and re-enables send', function () {
      win.__test__.gmSetFile({ name: 'big.nc', size: 4096 });
      win.__test__.gmSend();
      XHR.last.finish(500, { success: false, message: 'file_too_large' });
      var msg = doc.getElementById('gmMsg');
      ok(msg.className.indexOf('err') >= 0, 'error styling');
      ok(msg.textContent.indexOf('4 MB') >= 0, 'human message: ' + msg.textContent);
      ok(!doc.getElementById('gmSend').disabled, 'send usable again');
    });

    dom.window.close();
  }

  // ── Guard: no control / too large ─────────────────────────────────────
  {
    var dom = loadPage({ '/api/grbl/control-token': { success: true, token: 't', can_write: false } });
    await wait(dom, 400);
    var win = dom.window, doc = win.document;

    test('without write control the send button stays disabled and says why', function () {
      win.__test__.gmSetFile({ name: 'job.gcode', size: 100 });
      ok(doc.getElementById('gmSend').disabled, 'send disabled');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Another client') >= 0, 'explains the lock');
    });

    test('a file above the 4 MB workspace cap is refused', function () {
      win.__test__.gmState.canWrite = true;
      win.__test__.gmSetFile({ name: 'huge.nc', size: 5 * 1024 * 1024 });
      is(win.__test__.gmSend(), false, 'send refused');
      ok(doc.getElementById('gmMsg').textContent.indexOf('4 MB') >= 0, 'cap message');
    });

    dom.window.close();
  }

  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) { console.error('Fatal:', e); process.exit(1); });
