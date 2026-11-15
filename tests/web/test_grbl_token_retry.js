// GRBL desktop page: the write token lease (T23 parity with /m)
//
// The controller keeps a write token for 15 minutes (grbl_web_api.cpp
// kTokenLeaseMs) and only a WRITE renews it, so a job longer than that leaves
// the page holding a dead token. Two rules have to hold for the SECOND start
// (same file, after a finished job) to work without a reload:
//
//   1. a token older than the refresh window is renewed BEFORE the write;
//   2. a write still rejected with auth_token_invalid renews it and retries.
//
// A rejected write comes back as HTTP 400 + {"success":false,...}
// (sendGrblActionResult), so apiPost must read the body of a 400 - throwing on
// !r.ok used to hide the code and the retry never ran.
//
// Run: node test_grbl_token_retry.js

const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'grbl_controller.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error((m || 'is') + ': expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a)); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function loadPage() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var fix = fs.readFileSync(path.join(__dirname, 'mocks', 'api_fixtures.js'), 'utf8');
  html = html.replace('// ===== MOCK API LAYER =====', fix + '\n// ===== MOCK API LAYER =====');
  var orig = console.error;
  console.error = function () {
    var m = arguments[0] || '';
    if (typeof m !== 'string' || m.indexOf('Not implemented') < 0) orig.apply(console, arguments);
  };
  var dom = new JSDOM(html, { url: 'http://localhost', runScripts: 'dangerously', pretendToBeVisual: true });
  dom.window.HTMLCanvasElement.prototype.getContext = function () { return null; };
  console.error = orig;
  return dom;
}

// The page fetches its first token on DOMContentLoaded; let that settle before
// the scripted fetch is installed, so the counters only see the action itself.
function settle(dom) { return new Promise(function (r) { setTimeout(function () { r(dom); }, 400); }); }

function reply(status, body) {
  return Promise.resolve({
    ok: status >= 200 && status < 300,
    status: status,
    json: function () { return Promise.resolve(body); },
    text: function () { return Promise.resolve(JSON.stringify(body)); }
  });
}

// Replace the page fetch with a scriptable controller. `streamPlan(n)` decides
// the n-th /api/grbl/stream answer; every other endpoint is benign.
function installFetch(win, streamPlan) {
  var calls = { token: 0, posts: [] };
  win.fetch = function (url, o) {
    var u = String(url);
    var method = (o && o.method) || 'GET';
    if (u.indexOf('/api/grbl/control-token') >= 0) {
      calls.token++;
      return reply(200, { success: true, token: 'tok-' + calls.token, can_write: true });
    }
    if (u.indexOf('/api/grbl/stream') >= 0 && method === 'POST') {
      calls.posts.push(decodeURIComponent(String((o && o.body) || '')));
      var answer = streamPlan ? streamPlan(calls.posts.length, calls) : { status: 200, body: { success: true, message: 'ok' } };
      return reply(answer.status, answer.body);
    }
    return reply(200, { success: true, workflow_state: 'idle', machine_state: 'IDLE' });
  };
  return calls;
}

function runStream(T) {
  return T.postAction('/api/grbl/stream', { slot: 0, path: '/GcodeSender-P4/gcode/Andreia.gc' }, 'STREAM', true);
}

async function run() {
  console.log('\n=== GRBL desktop token lease (2nd START) ===\n');

  // ── 1: the token is renewed BEFORE the write when it is old ─────────────
  {
    var dom = loadPage();
    await settle(dom);
    var win = dom.window, T = win.__test__;
    var calls = installFetch(win, null);
    T.state.controlToken = 'seed';
    T.state.tokenAt = Date.now() - 11 * 60 * 1000;   // beyond the 10 min window

    var result = await runStream(T);

    test('an old token is renewed before the write (no rejected attempt)', function () {
      is(calls.token, 1, 'one control-token GET');
      is(calls.posts.length, 1, 'exactly one stream POST');
      ok(calls.posts[0].indexOf('token=tok-1') >= 0, 'the POST carries the fresh token: ' + calls.posts[0]);
      ok(result === true, 'action reported success');
    });
    dom.window.close();
  }

  // ── 2: a rejected write (HTTP 400) revives the token and retries once ───
  {
    var dom = loadPage();
    await settle(dom);
    var win = dom.window, T = win.__test__;
    var calls = installFetch(win, function (n) {
      return (n === 1)
        ? { status: 400, body: { success: false, message: 'auth_token_invalid' } }
        : { status: 200, body: { success: true, message: 'ok' } };
    });
    T.state.controlToken = 'tok-expired';
    T.state.tokenAt = Date.now();   // the page thinks it is fresh: the lease is dead

    var result = await runStream(T);

    test('auth_token_invalid on HTTP 400 is seen, retried once and succeeds', function () {
      is(calls.posts.length, 2, 'the start was posted twice');
      is(calls.token, 1, 'the token was renewed once');
      ok(calls.posts[1].indexOf('token=tok-1') >= 0, 'retry carries the new token: ' + calls.posts[1]);
      ok(calls.posts[1].indexOf('Andreia.gc') >= 0, 'same file on the retry');
      ok(result === true, 'the second start reported success');
    });
    dom.window.close();
  }

  // ── 3: any other 400 keeps the server code and is NOT retried ───────────
  {
    var dom = loadPage();
    await settle(dom);
    var win = dom.window, T = win.__test__;
    var calls = installFetch(win, function () {
      return { status: 400, body: { success: false, message: 'preflight_workflow_busy' } };
    });
    T.state.controlToken = 'seed';
    T.state.tokenAt = Date.now();

    var result = await runStream(T);

    test('a non-auth 400 is surfaced and not retried', function () {
      is(calls.posts.length, 1, 'one POST only');
      is(calls.token, 0, 'no pointless token refresh');
      ok(result === false, 'action reported failure');
    });
    dom.window.close();
  }

  // ── 4: an error page with no JSON must not crash the action ─────────────
  {
    var dom = loadPage();
    await settle(dom);
    var win = dom.window, T = win.__test__;
    var calls = installFetch(win, function () {
      return { status: 500, body: null };
    });
    T.state.controlToken = 'seed';
    T.state.tokenAt = Date.now();

    var result = await runStream(T);

    test('a 500 without a JSON body fails cleanly', function () {
      is(calls.posts.length, 1, 'one POST');
      ok(result === false, 'no throw, reported failure');
    });
    dom.window.close();
  }

  console.log('\n' + passed + '/' + (passed + failed) + ' passed');
  process.exit(failed ? 1 : 0);
}

run();
