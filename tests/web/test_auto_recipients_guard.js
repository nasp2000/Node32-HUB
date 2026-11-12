// Guard tests for auto.js loadRecipientsConfig (chatgpt/src/webpages/auto_js.h).
//
// Crawl 2026-11-11 (.29 / .24): /email requested /api/recipients?limit=100 on
// builds without the Recipients page, got a 404, and — because the failure path
// retried unconditionally — re-polled every 2 s for as long as the page stayed
// open. The function now returns before touching the network when the page has
// no recipients table, and the retry is capped.
//
// The shipped function is extracted from the production header (no copy), run
// against stubs, so the test pins the real source.

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'auto_js.h');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }

// Extract "var _rcpRetries = 0;" + "function loadRecipientsConfig(){...}" verbatim.
function extractFn() {
  var src = fs.readFileSync(SRC, 'utf8');
  var start = src.indexOf('var _rcpRetries');
  if (start < 0) throw new Error('_rcpRetries not found in auto_js.h');
  var fnStart = src.indexOf('function loadRecipientsConfig()', start);
  if (fnStart < 0) throw new Error('loadRecipientsConfig not found');
  var i = src.indexOf('{', fnStart), depth = 0, end = -1;
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}') { depth--; if (depth === 0) { end = i + 1; break; } }
  }
  if (end < 0) throw new Error('unbalanced braces in loadRecipientsConfig');
  return src.substring(start, end);
}

// Build a sandbox: returns { run(), calls, timers }.
function sandbox(opts) {
  var code = extractFn();
  var calls = [];
  var timers = [];
  var table = opts.table ? { id: 'recipients-table' } : null;

  var document = {
    getElementById: function (id) { return (table && id === 'recipients-table') ? table : null; }
  };

  var rejecting = function () { return Promise.reject(new Error('HTTP 404')); };
  var optimizedFetch = {
    request: function (url) { calls.push(url); return rejecting(); }
  };
  var window = {};
  var consoleStub = { error: function () {} };
  var setTimeoutStub = function (fn, ms) { timers.push(ms); };

  // The extracted source declares _rcpRetries and loadRecipientsConfig; expose both.
  var factory = new Function(
    'document', 'optimizedFetch', 'fetch', 'console', 'setTimeout',
    code + '\nreturn { run: loadRecipientsConfig, retries: function () { return _rcpRetries; } };'
  );
  var api = factory(document, optimizedFetch, function () {
    calls.push('native-fetch'); return Promise.reject(new Error('no network'));
  }, consoleStub, setTimeoutStub);

  return { run: api.run, retries: api.retries, calls: calls, timers: timers };
}

(async function run() {
  console.log('\n=== auto.js loadRecipientsConfig guard ===\n');

  // 1) No recipients table (the /email page on .29 / .24): nothing is fetched.
  var sb = sandbox({ table: false });
  sb.run();
  await new Promise(function (r) { setImmediate(r); });
  test('no recipients table -> no /api/recipients request', function () {
    is(sb.calls.length, 0, 'requests: ' + JSON.stringify(sb.calls));
    is(sb.timers.length, 0, 'no retry scheduled');
  });

  // 2) Table present: the endpoint is fetched once, and a failing endpoint
  //    retries a bounded number of times instead of forever.
  sb = sandbox({ table: true });
  for (var n = 0; n < 8; n++) {
    sb.run();
    await new Promise(function (r) { setImmediate(r); });
    await new Promise(function (r) { setImmediate(r); });
  }
  test('recipients table -> endpoint requested', function () {
    is(sb.calls.filter(function (u) { return u.indexOf('/api/recipients') >= 0; }).length, 8,
       'one recipients request per explicit call');
  });
  test('retry scheduling is capped (no endless polling)', function () {
    if (sb.timers.length > 3) throw new Error('scheduled ' + sb.timers.length + ' retries (expected <= 3)');
  });

  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
})().catch(function (e) { console.error('Fatal:', e); process.exit(1); });
