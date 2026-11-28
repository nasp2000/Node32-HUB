// /tdk-lambda page: first-contact line test, unit rating and the listen warning.
//
// The markup contract is also covered natively (tests/native/test_tdk_lambda_page.cpp);
// here the shipped functions run in jsdom against a mocked fetch:
//   - "Test the line" POSTs /api/tdk-lambda-selftest once and never invents a verdict;
//   - the verdict comes from status.selfTest (running / ok / partial / no reply);
//   - Rated V/I travel with the saved profile;
//   - listen mode surfaces single values it cannot attribute instead of guessing.
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

const CODE = ['_tdkQ', '_tdkEsc', '_tdkT', '_tdkDur', '_tdkAsleep', '_tdkPost',
              'tdkStatus', 'tdkApplyGuard', 'tdkSelfTest', 'tdkProfileBody']
  .map(function (n) { return extractFn(asset, n); }).join('\n')
  + '\nvar _tdkTestTimer=null;\n';

function boot(statusData) {
  const dom = new JSDOM('<!doctype html><html><body>' + markup + '</body></html>',
                        { url: 'http://device/tdk-lambda', runScripts: 'outside-only',
                          pretendToBeVisual: true });
  const w = dom.window;
  w._t = function (k, fb) { return (w._lang && w._lang[k] !== undefined) ? w._lang[k] : fb; };
  w.eval(CODE);
  w.__posts = [];
  w.__gets = 0;
  w.fetch = function (url, opts) {
    if (opts && opts.method === 'POST') {
      w.__posts.push({ url: url, body: opts.body });
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
    }
    w.__gets++;
    return Promise.resolve({ json: function () { return Promise.resolve(statusData); } });
  };
  return w;
}

function status(over) {
  const base = {
    success: true,
    status: { polling: true, pollIntervalMs: 60000, lastRxAgeMs: 0, listen: false,
              unattributed: 0, selfTest: { running: false, step: 0, steps: 5, ok: 0, fail: 0, result: '', detail: '' } },
    values: { valid: true, vMeas: 599.9, vSet: 600, iMeas: 1.28, iSet: 1.3, ovp: 660, uvl: 0, mode: 'CV', outOn: true }
  };
  Object.keys(over || {}).forEach(function (k) { base.status[k] = over[k]; });
  return base;
}

function flush(w) { return new Promise(function (res) { setTimeout(res, 0); }); }

async function run() {
  test('the line-test controls exist once and only once', function () {
    const dom = new JSDOM('<!doctype html><html><body>' + markup + '</body></html>');
    const d = dom.window.document;
    is(d.querySelectorAll('#tdkTestBtn').length, 1, 'test button');
    is(d.querySelectorAll('#tdkTestMsg').length, 1, 'result slot');
    is(d.querySelectorAll('#tdkRateV').length, 1, 'rated V');
    is(d.querySelectorAll('#tdkRateI').length, 1, 'rated I');
    ok(d.querySelector('#tdkTestBtn').getAttribute('data-i18n-title'), 'tooltip key');
  });

  const w = boot(status({ selfTest: { running: true, step: 2, steps: 5, ok: 0, fail: 0, result: '', detail: '' } }));
  w.tdkStatus();
  await flush(w); await flush(w);
  test('a running test shows the progress and blocks the button', function () {
    const msg = w.document.querySelector('#tdkTestMsg').textContent;
    ok(msg.indexOf('2/5') >= 0, 'progress shown: ' + msg);
    is(w.document.querySelector('#tdkTestBtn').disabled, true, 'button disabled while running');
  });

  const w2 = boot(status({ selfTest: { running: false, step: 5, steps: 5, ok: 5, fail: 0, result: 'ok', detail: 'IDN? ok | DVC? ok' } }));
  w2.tdkStatus();
  await flush(w2); await flush(w2);
  test('an ok verdict comes from the server detail', function () {
    const msg = w2.document.querySelector('#tdkTestMsg').textContent;
    ok(msg.toLowerCase().indexOf('line ok') >= 0, 'verdict text: ' + msg);
    ok(msg.indexOf('IDN? ok') >= 0, 'detail included: ' + msg);
    is(w2.document.querySelector('#tdkTestBtn').disabled, false, 'button usable again');
  });

  const w3 = boot(status({ selfTest: { running: false, step: 5, steps: 5, ok: 0, fail: 5, result: 'no reply', detail: 'IDN? no reply' } }));
  w3.tdkStatus();
  await flush(w3); await flush(w3);
  test('no reply is reported as no reply, never as zero volts', function () {
    const msg = w3.document.querySelector('#tdkTestMsg').textContent;
    ok(msg.indexOf('no reply') >= 0, 'verdict text: ' + msg);
    const badge = w3.document.querySelector('#tdkTestMsg .tdk-badge');
    ok(badge && badge.className.indexOf('err') >= 0, 'error styling');
  });

  const w4 = boot(status({ listen: true, unattributed: 7 }));
  w4.tdkStatus();
  await flush(w4); await flush(w4);
  test('listen mode says how many single values were ignored', function () {
    const text = w4.document.querySelector('#tdkStatus').textContent;
    ok(text.indexOf('7') >= 0, 'count shown: ' + text);
    ok(/ignored/i.test(text), 'explains why: ' + text);
  });

  test('the profile body carries the unit rating', function () {
    const w5 = boot(status());
    w5.document.querySelector('#tdkRateV').value = '30';
    w5.document.querySelector('#tdkRateI').value = '50';
    const body = w5.tdkProfileBody();
    ok(body.indexOf('rate_v=30') >= 0, body);
    ok(body.indexOf('rate_i=50') >= 0, body);
  });

  const w6 = boot(status());
  w6.tdkSelfTest();
  await flush(w6);
  test('the button starts the server-side test exactly once', function () {
    const posts = w6.__posts.filter(function (p) { return p.url === '/api/tdk-lambda-selftest'; });
    is(posts.length, 1, 'one POST to the selftest route');
    is(w6.document.querySelector('#tdkTestMsg').textContent.indexOf('testing') >= 0, true, 'running text');
  });

  console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed');
  process.exit(failed ? 1 : 0);
}

run();
