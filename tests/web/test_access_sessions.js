// Access page: session list + "terminate all sessions" (BACKLOG ACC-1).
//
// Both endpoints already existed (/api/session-history, /api/access-kick-all);
// this suite runs the shipped helpers from page_access_asset.h and checks what
// the page does with them: rows rendered newest-first with escaping, the empty
// and failure states, the confirm gate before the POST, and the refresh after
// a successful kick.
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'page_access_asset.h');

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

const src = fs.readFileSync(SRC, 'utf8');
const CODE = ['accEsc', 'accSessionLabel', 'accSessionRows', 'accLoadSessions', 'accKickAll']
  .map(function (n) { return extractFn(src, n); }).join('\n');

function boot(api, opts) {
  opts = opts || {};
  const dom = new JSDOM('<!doctype html><html><body>' +
    "<div id='acc_sessions_list'>--</div><div id='acc_sessions_msg'></div>" +
    "<button id='acc_kick_all'>kick</button><button id='acc_sessions_refresh'>refresh</button>" +
    '</body></html>', { url: 'http://localhost/access', runScripts: 'outside-only', pretendToBeVisual: true });
  const w = dom.window;
  const calls = [];
  w.fetch = function (url, o) {
    calls.push({ url: String(url), method: (o && o.method) || 'GET' });
    const key = Object.keys(api).find(function (k) { return String(url).indexOf(k) >= 0; });
    const v = typeof api[key] === 'function' ? api[key]() : (key ? api[key] : {});
    return Promise.resolve({ ok: true, status: 200, json: function () { return Promise.resolve(v); } });
  };
  w._t = function (k, fb) { return fb !== undefined ? fb : k; };
  w.disableBtn = function () {};
  w.enableBtn = function () {};
  w.customAlert = function (m) { w.__lastAlert = m; };
  w.customConfirm = function (msg, cb) { w.__lastConfirm = msg; cb(opts.confirm !== false); };
  w.eval(CODE + '\nwindow.__acc = { rows: accSessionRows, load: accLoadSessions, kick: accKickAll, esc: accEsc };');
  return { dom: dom, w: w, calls: calls };
}
const tick = () => new Promise(function (r) { setTimeout(r, 40); });

(async function run() {
  console.log('\n=== Access: session list + terminate all (ACC-1) ===\n');

  test('the page ships the list, the refresh and the terminate button', function () {
    ok(src.indexOf("id='acc_sessions_list'") > 0, 'list element');
    ok(src.indexOf("id='acc_kick_all'") > 0, 'kick button');
    ok(src.indexOf("id='acc_sessions_refresh'") > 0, 'refresh button');
    ok(/id='acc_kick_all'[^>]*type='button'|type='button'[^>]*id='acc_kick_all'/.test(src), 'kick is type=button (cannot submit the form)');
    ok(/id='acc_sessions_refresh'[^>]*type='button'|type='button'[^>]*id='acc_sessions_refresh'/.test(src), 'refresh is type=button');
  });

  test('rows are newest first, escaped, and capped', function () {
    const b = boot({});
    const html = b.w.__acc.rows([
      { ts: 100, ip: '10.0.0.2', type: 'connect', when: '2026-11-14 10:00:00' },
      { ts: 300, ip: '10.0.0.9', type: 'kick', when: '2026-11-14 12:00:00' },
      { ts: 200, ip: '10.0.0.5', type: 'expired', when: '2026-11-14 11:00:00' },
    ], 20);
    ok(html.indexOf('10.0.0.9') < html.indexOf('10.0.0.5'), 'kick (300) before expired (200)');
    ok(html.indexOf('10.0.0.5') < html.indexOf('10.0.0.2'), 'expired (200) before connect (100)');
    ok(html.indexOf('terminated') > 0 && html.indexOf('expired') > 0 && html.indexOf('connected') > 0, 'labels');
    // escaping
    const esc = b.w.__acc.rows([{ ts: 1, ip: '<img src=x onerror=alert(1)>', type: 'connect', when: 'x' }], 5);
    ok(esc.indexOf('<img') < 0 && esc.indexOf('&lt;img') > 0, 'ip escaped');
    // cap + remainder counter
    const many = [];
    for (var i = 0; i < 25; i++) many.push({ ts: i, ip: '10.0.0.' + i, type: 'connect', when: 't' });
    const capped = b.w.__acc.rows(many, 20);
    is((capped.match(/acc-session-row/g) || []).length, 20, '20 rows');
    ok(capped.indexOf('older events: 5') > 0, 'remainder reported');
  });

  test('the list loads from /api/session-history', async function () {
    const b = boot({ '/api/session-history': { events: [{ ts: 1, ip: '10.0.0.7', type: 'connect', when: '2026-11-14 09:00:00' }] } });
    const n = await b.w.__acc.load();
    is(n, 1, 'one event');
    is(b.calls[0].url, '/api/session-history', 'endpoint');
    ok(b.w.document.getElementById('acc_sessions_list').innerHTML.indexOf('10.0.0.7') > 0, 'row rendered');
  });

  test('empty and failing history are explained, not left blank', async function () {
    var b = boot({ '/api/session-history': { events: [] } });
    await b.w.__acc.load();
    ok(b.w.document.getElementById('acc_sessions_list').innerHTML.indexOf('No sessions recorded') > 0, 'empty state');

    b = boot({ '/api/session-history': function () { throw new Error('boom'); } });
    // the stub throws inside the promise chain -> catch path of accLoadSessions
    b.w.fetch = function () { return Promise.reject(new Error('boom')); };
    await b.w.__acc.load();
    ok(b.w.document.getElementById('acc_sessions_list').innerHTML.indexOf('Could not load') > 0, 'failure state');
  });

  test('terminate asks first, then POSTs and refreshes the list', async function () {
    const b = boot({ '/api/access-kick-all': { success: true },
                     '/api/session-history': { events: [] } }, { confirm: true });
    const before = b.calls.length;
    b.w.__acc.kick();
    await tick();
    const posts = b.calls.slice(before).filter(function (c) { return c.method === 'POST'; });
    is(posts.length, 1, 'one POST');
    is(posts[0].url, '/api/access-kick-all', 'endpoint');
    ok(/Terminate every web session/.test(b.w.__lastConfirm || ''), 'confirm shown');
    is(b.w.document.getElementById('acc_sessions_msg').textContent, 'All sessions terminated', 'message');
    ok(b.calls.some(function (c) { return c.url === '/api/session-history'; }), 'list refreshed after the kick');
    ok((b.w.__lastAlert || '').indexOf('\u2705') === 0, 'success alert');
  });

  test('cancelling the confirm sends nothing', async function () {
    const b = boot({ '/api/access-kick-all': { success: true } }, { confirm: false });
    b.w.__acc.kick();
    await tick();
    is(b.calls.filter(function (c) { return c.method === 'POST'; }).length, 0, 'no POST');
  });

  test('a refused kick is reported in the page', async function () {
    const b = boot({ '/api/access-kick-all': { success: false },
                     '/api/session-history': { events: [] } }, { confirm: true });
    b.w.__acc.kick();
    await tick();
    is(b.w.document.getElementById('acc_sessions_msg').textContent, 'Could not terminate the sessions', 'failure message');
    ok((b.w.__lastAlert || '').indexOf('\u274C') === 0, 'failure alert');
  });

  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
})();
