// Live state on the alerts page (ALERTS-5), client side.
//
// The page now fills each inactivity slot with "sent/cap today" and a
// "sent this period" marker from /api/status
// (inactivity_alerts.slots[i].alerts_sent_today / alert_sent_this_period,
// exposed by system_status_payload.h). These tests run the shipped helpers
// from the asset (extracted, not copied).
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'page_alerts_asset.cpp');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }

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
// The scheduling lines are top-level in the asset (they run when the page
// loads), so the snippet keeps them after the three functions.
const STARTUP = 'alertsLiveLoad();setInterval(alertsLiveLoad,30000);';
if (src.indexOf(STARTUP) < 0) throw new Error('live-state startup lines not found in the asset');
const code = extractFn(src, 'alertsLiveText') + '\n' + extractFn(src, 'alertsLiveApply') +
             '\n' + extractFn(src, 'alertsLiveLoad') + '\n' + STARTUP;
const rt = function (k, fb) { return fb !== undefined ? fb : k; };
// The page functions read the global `document`; bind each test's own DOM.
function makeApi(doc, hooks) {
  hooks = hooks || {};
  return new Function('rt', 'document', 'fetch', 'setInterval', code +
    '\nreturn { text: alertsLiveText, apply: alertsLiveApply, load: alertsLiveLoad };')(
      rt, doc,
      hooks.fetch || function () { return Promise.resolve({ json: function () { return Promise.resolve({}); } }); },
      hooks.setInterval || function (fn, ms) { return { fn: fn, ms: ms }; });
}
const api = makeApi(new JSDOM('<!doctype html><body></body>').window.document);

console.log('\n=== Alerts page live state ===\n');

test('formats sent/cap with the period marker', function () {
  is(api.text(3, 6, false, 'today', 'sent this period'), '3/6 today', 'counters');
  is(api.text(3, 6, true, 'today', 'sent this period'), '3/6 today \u00b7 sent this period', 'period marker');
  is(api.text(0, 0, false, 'today', 'x'), '0 today', 'no cap');
  is(api.text('4', '12', false, 'hoje', 'x'), '4/12 hoje', 'string inputs + PT labels');
});

test('applies /api/status to every rendered slot', function () {
  const dom = new JSDOM('<div id="alert_live_0"></div><select id="alert_max_0"><option value="6" selected>6</option></select>' +
                        '<div id="alert_live_1"></div><select id="alert_max_1"><option value="12" selected>12</option></select>',
                        { url: 'http://localhost' });
  const doc = dom.window.document;
  const api = makeApi(doc);
  const ok = api.apply({ inactivity_alerts: { slots: [
    { alerts_sent_today: 2, alert_sent_this_period: false },
    { alerts_sent_today: 5, alert_sent_this_period: true },
  ] } });
  is(ok, true, 'applied');
  is(doc.getElementById('alert_live_0').textContent, '2/6 today', 'slot 0 text');
  is(doc.getElementById('alert_live_1').textContent, '5/12 today \u00b7 sent this period', 'slot 1 text');
  is(doc.getElementById('alert_live_1').style.color, 'rgb(252, 165, 165)', 'period marker colour (jsdom normalises hex)');
});

test('bad payloads are ignored (no crash, nothing rendered)', function () {
  is(api.apply(null), false, 'null');
  is(api.apply({}), false, 'no inactivity_alerts');
  is(api.apply({ inactivity_alerts: { slots: 'nope' } }), false, 'slots not an array');
});

test('slots without a live element are skipped', function () {
  const bare = makeApi(new JSDOM('<!doctype html><body></body>').window.document);
  is(bare.apply({ inactivity_alerts: { slots: [
    { alerts_sent_today: 1, alert_sent_this_period: false },
    { alerts_sent_today: 1, alert_sent_this_period: false },
  ] } }), true, 'still applies what exists (no DOM nodes here)');
});

test('the live state is polled from /api/status every 30 s, not while hidden', function () {
  const doc = new JSDOM('<!doctype html><body></body>').window.document;
  // jsdom reports hidden=true without pretendToBeVisual; the page must see a
  // visible tab for the load call.
  Object.defineProperty(doc, 'hidden', { value: false, writable: true, configurable: true });
  const calls = [];
  let interval = null;
  const api = makeApi(doc, {
    fetch: function (url, opts) { calls.push({ url: url, cache: opts && opts.cache }); return Promise.resolve({ json: function () { return Promise.resolve({}); } }); },
    setInterval: function (fn, ms) { interval = { fn: fn, ms: ms }; return 1; },
  });
  // The snippet runs the startup lines at construction, exactly like the page.
  is(calls.length, 1, 'one fetch on load');
  is(calls[0].url, '/api/status', 'endpoint');
  is(calls[0].cache, 'no-store', 'cache policy');
  is(interval && interval.ms, 30000, 'polls every 30 s');
  // hidden tab: the timer fires but does not fetch
  doc.hidden = true;
  interval.fn();
  is(calls.length, 1, 'no request while the tab is hidden');
  doc.hidden = false;
  interval.fn();
  is(calls.length, 2, 'resumes when visible again');
});

test('renders the real /api/status shape (inactivity_alerts.slots[i])', function () {
  // Field names as written by system_status_payload.h:827-828.
  const dom = new JSDOM('<div id="alert_live_0"></div><select id="alert_max_0"><option value="6" selected>6</option></select>',
                        { url: 'http://localhost' });
  const doc = dom.window.document;
  const api = makeApi(doc);
  const payload = {
    status: 'ok',
    inactivity_alerts: {
      sensors_reset_timer: true,
      alerts_sent_today: 2,
      slots: [
        { enabled: true, timeout_minutes: 20, max_per_day: 6, emails: 'a@b.com',
          alerts_sent_today: 2, alert_sent_this_period: true },
      ],
    },
  };
  is(api.apply(payload), true, 'applied');
  is(doc.getElementById('alert_live_0').textContent, '2/6 today \u00b7 sent this period', 'text from the real shape');
});

var total = passed + failed;
console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
process.exit(failed > 0 ? 1 : 0);
