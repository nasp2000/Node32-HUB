// /tdk-lambda page: module switch + "apply RS232 defaults", and their persistence.
//
// The switch is a saved choice (enabled=0/1), not a session toggle: the server
// stops the poll/CSV/SCPI and keeps it off after a reboot. The RS232 defaults
// button writes the documented link (9600 8N1, no flow, 1.5 s) to the RS232
// section and restarts the port.
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets',
                      'page_tdk_lambda_asset.cpp');
const asset = fs.readFileSync(SRC, 'utf8');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

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

const htmlStart = asset.indexOf("<div class='page tdk-lambda'>");
const scriptAt = asset.indexOf('<script>', htmlStart);
const markup = asset.substring(htmlStart, scriptAt);

const CODE = ['_tdkQ', '_tdkEsc', '_tdkT', '_tdkDur', '_tdkAsleep', '_tdkPost', '_tdkFail',
              'tdkStatus', 'tdkApplyGuard', 'tdkSaveEnabled', 'tdkApplyRs232Defaults',
              'tdkProfileBody', 'tdkAllBody']
  .map(function (n) { return extractFn(asset, n); }).join('\n');

function boot(statusData, opts) {
  opts = opts || {};
  const dom = new JSDOM('<!doctype html><html><body>' + markup + '</body></html>',
                        { url: 'http://device/tdk-lambda', runScripts: 'outside-only',
                          pretendToBeVisual: true });
  const w = dom.window;
  w._t = function (k, fb) { return (w._lang && w._lang[k] !== undefined) ? w._lang[k] : fb; };
  w.eval(CODE);
  w.__posts = [];
  w.customConfirm = function (msg, cb) { w.__confirmMsg = msg; cb(opts.confirm !== false); };
  w.customAlert = function (msg) { w.__alert = msg; };
  w.fetch = function (url, o) {
    if (o && o.method === 'POST') {
      w.__posts.push({ url: url, body: o.body });
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, message: 'saved' }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve(statusData); } });
  };
  return w;
}

function status(over) {
  const base = { success: true, status: { enabled: true, polling: true, pollIntervalMs: 60000,
    lastRxAgeMs: 1000, listen: false, allowWrite: false, selfTest: { running: false, step: 0, steps: 5, result: '', detail: '' } },
    values: { valid: true, vMeas: 600, vSet: 600, iMeas: 1.28, iSet: 1.3, ovp: 660, uvl: 0, mode: 'CV', outOn: true } };
  Object.keys(over || {}).forEach(function (k) { base.status[k] = over[k]; });
  return base;
}

function flush(w) { return new Promise(function (r) { setTimeout(r, 0); }); }

async function run() {
  test('the switch and the RS232 defaults button exist with tooltips', function () {
    const dom = new JSDOM('<!doctype html><html><body>' + markup + '</body></html>');
    const d = dom.window.document;
    ok(d.querySelector('#tdkEnabled'), 'module switch');
    ok(d.querySelector('#tdkEnabled').closest('label').getAttribute('data-i18n-title'), 'switch tooltip');
    const btns = Array.from(d.querySelectorAll('button'));
    const rs = btns.filter(function (b) { return /RS232 defaults/.test(b.textContent); });
    is(rs.length, 1, 'one defaults button');
    ok(rs[0].getAttribute('data-i18n-title'), 'button tooltip');
    ok(rs[0].classList.contains('btn'), 'core .btn class');
  });

  const w = boot(status());
  w.document.querySelector('#tdkEnabled').checked = false;
  w.tdkSaveEnabled();
  await flush(w); await flush(w);
  test('turning the module off is POSTed as a saved choice', function () {
    const posts = w.__posts.filter(function (p) { return p.url === '/api/tdk-lambda-config'; });
    is(posts.length, 1, 'one config POST');
    ok(/enabled=0/.test(posts[0].body), 'body carries enabled=0: ' + posts[0].body);
  });

  const w2 = boot(status({ enabled: false }));
  w2.tdkStatus();
  await flush(w2); await flush(w2);
  test('a disabled module shows up as off and blocks the console', function () {
    const txt = w2.document.querySelector('#tdkStatus').textContent;
    ok(/module off/i.test(txt), 'badge: ' + txt);
    is(w2.document.querySelector('#tdkSendBtn').disabled, true, 'send disabled');
  });

  const w3 = boot(status({ enabled: false }));
  w3.document.querySelector('#tdkEnabled').checked = false;
  w3.tdkApplyGuard();
  test('the guard message explains why the console is blocked', function () {
    const msg = w3.document.querySelector('#tdkGuardMsg').textContent;
    ok(/module off/i.test(msg), 'guard message: ' + msg);
  });

  const w4 = boot(status());
  w4.tdkApplyRs232Defaults();
  await flush(w4); await flush(w4);
  test('the defaults button asks first, then posts to its own endpoint', function () {
    ok(w4.__confirmMsg && /9600/.test(w4.__confirmMsg), 'confirmation mentions 9600');
    const posts = w4.__posts.filter(function (p) { return p.url === '/api/tdk-lambda-rs232-defaults'; });
    is(posts.length, 1, 'one POST to the defaults route');
  });

  const w5 = boot(status(), { confirm: false });
  w5.tdkApplyRs232Defaults();
  await flush(w5); await flush(w5);
  test('cancelling the confirmation writes nothing', function () {
    is(w5.__posts.length, 0, 'no POST on cancel');
  });

  test('the final Save button sends every choice in one body', function () {
    const w6 = boot(status());
    w6.document.querySelector('#tdkEnabled').checked = false;
    w6.document.querySelector('#tdkListen').checked = true;
    w6.document.querySelector('#tdkPoll').checked = true;
    w6.document.querySelector('#tdkPollSec').value = '120';
    w6.document.querySelector('#tdkCsvOn').checked = true;
    w6.document.querySelector('#tdkCsvEvery').value = '3';
    w6.document.querySelector('#tdkFmt').value = 'table';
    w6.document.querySelector('#tdkMode2').value = 'dvc';
    w6.document.querySelector('#tdkRateV').value = '30';
    w6.document.querySelector('#tdkRateI').value = '50';
    w6.document.querySelector('#tdkAddr').value = '2';
    w6.document.querySelector('#tdkInject').checked = true;
    w6.document.querySelector('#tdkLimOn').checked = true;
    w6.document.querySelector('#tdkLimIMax').value = '1.5';
    w6.document.querySelector('#tdkAuto').checked = false;
    const body = w6.tdkAllBody();
    ['enabled=0','listen=1','poll=1','poll_s=120','csv=1','csv_every=3','format=table',
     'mode=dvc','rate_v=30','rate_i=50','addr=2','inject=1','limits_enabled=1',
     'limit_i_max=1.5','auto_map=0'].forEach(function (kv) {
      ok(body.indexOf(kv) >= 0, 'missing ' + kv + ' in ' + body);
    });
  });

  console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed');
  process.exit(failed ? 1 : 0);
}

run();
