// /sdcard feedback (BACKLOG SD-4), client side.
//
// 1) The page must carry #sd_msg — auto.js writes mount/unmount/refresh results
//    into it, and without the element the action alert was dropped.
// 2) /api/sd-status must be able to say "SD disabled" (auto.js: if (j.disabled));
//    here we prove the page renders that state when the payload carries it.
//
// Runs the shipped markup (three PROGMEM blocks of page_sdcard_asset.cpp) and
// the shipped initSdcardPage() from auto_js.h — extracted, not copied.
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const ROOT = path.join(__dirname, '..', '..');
const ASSET = path.join(ROOT, 'chatgpt', 'src', 'webpages', 'assets', 'page_sdcard_asset.cpp');
const AUTOJS = path.join(ROOT, 'chatgpt', 'src', 'webpages', 'auto_js.h');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// All R"HTML(...)HTML" blocks of the asset, in file order = render order.
function pageBody() {
  const src = fs.readFileSync(ASSET, 'utf8');
  const re = /R"HTML\(([\s\S]*?)\)HTML"/g;
  const parts = [];
  let m;
  while ((m = re.exec(src)) !== null) parts.push(m[1]);
  if (!parts.length) throw new Error('no HTML blocks in the asset');
  return parts.join('');
}

function extractFn(src, name) {
  const start = src.indexOf('function ' + name + '(');
  if (start < 0) throw new Error(name + ' not found');
  let i = src.indexOf('{', start), depth = 0, end = -1;
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}') { depth--; if (depth === 0) { end = i + 1; break; } }
  }
  if (end < 0) throw new Error('unbalanced braces in ' + name);
  return src.substring(start, end);
}

const INIT = extractFn(fs.readFileSync(AUTOJS, 'utf8'), 'initSdcardPage');
const BODY = pageBody();

// Boot a page: real markup + auto.js's initialiser, with a fetch stub table.
function boot(api) {
  const dom = new JSDOM('<!doctype html><html><body>' + BODY + '</body></html>',
                        { url: 'http://localhost/sdcard', runScripts: 'outside-only', pretendToBeVisual: true });
  const w = dom.window;
  const calls = [];
  w.fetch = function (url, opts) {
    calls.push(String(url));
    const key = Object.keys(api).find(function (k) { return String(url).indexOf(k) >= 0; });
    const v = key ? api[key] : {};
    return Promise.resolve({ ok: true, status: 200, json: function () { return Promise.resolve(v); } });
  };
  w.FormData = w.FormData;   // provided by jsdom
  w.customAlert = function () {};
  w.showSavedCard = function () {};
  w.disableBtn = function () {};
  w.enableBtn = function () {};
  w.showLoading = function () {};
  w.hideLoading = function () {};
  w.eval(INIT + '\nwindow.__initSdcardPage = initSdcardPage;');
  return { dom: dom, w: w, calls: calls };
}

function tick() { return new Promise(function (r) { setTimeout(r, 60); }); }

(async function run() {
  console.log('\n=== /sdcard feedback (SD-4) ===\n');

  test('the page ships the #sd_msg feedback element, empty at load', function () {
    const dom = new JSDOM('<!doctype html><html><body>' + BODY + '</body></html>', { url: 'http://localhost/sdcard' });
    const doc = dom.window.document;
    const msg = doc.getElementById('sd_msg');
    ok(msg, '#sd_msg missing from the page');
    is(msg.textContent, '', 'starts empty');
    is(doc.querySelectorAll('#sd_msg').length, 1, 'exactly one');
  });

  test('a disabled payload renders the "SD disabled" state', async function () {
    const b = boot({ '/api/sd-status': { status: 'ok', disabled: true },
                     '/api/sdcard-config': { status: 'ok' } });
    b.w.__initSdcardPage();
    await tick();
    const info = b.w.document.getElementById('sd_info');
    is(info.textContent, 'SD disabled', 'disabled branch reached');
  });

  test('an enabled payload keeps the status line (disabled is not always set)', async function () {
    const b = boot({ '/api/sd-status': { status: 'ok', disabled: false, mounted: true, storage_mode: 'SD Card',
                                         total_bytes: 10485760, used_bytes: 5242880 },
                     '/api/sdcard-config': { status: 'ok' } });
    b.w.__initSdcardPage();
    await tick();
    const line = b.w.document.getElementById('sd_info').textContent;
    ok(line.indexOf('Mounted=Yes') >= 0, 'line: ' + line);
    ok(line.indexOf('Total=') >= 0, 'line: ' + line);
    ok(line.indexOf('SD disabled') < 0, 'not disabled');
  });

  test('mount feedback reaches #sd_msg', async function () {
    const b = boot({ '/api/sd-status': { status: 'ok' }, '/api/sdcard-config': { status: 'ok' },
                     '/api/sd-action': { status: 'ok' } });
    b.w.__initSdcardPage();
    await tick();
    b.w.document.getElementById('sd_mount').click();
    await tick();
    is(b.w.document.getElementById('sd_msg').textContent, 'Action mount OK', 'feedback shown');
  });

  test('a failed action reports the failure in #sd_msg', async function () {
    const b = boot({ '/api/sd-status': { status: 'ok' }, '/api/sdcard-config': { status: 'ok' },
                     '/api/sd-action': { status: 'error' } });
    b.w.__initSdcardPage();
    await tick();
    b.w.document.getElementById('sd_unmount').click();
    await tick();
    is(b.w.document.getElementById('sd_msg').textContent, 'Failed: unmount', 'failure shown');
  });

  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
})();
