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
const code = extractFn(src, 'alertsLiveText') + '\n' + extractFn(src, 'alertsLiveApply');
const rt = function (k, fb) { return fb !== undefined ? fb : k; };
// The page functions read the global `document`; bind each test's own DOM.
function makeApi(doc) {
  return new Function('rt', 'document', code +
    '\nreturn { text: alertsLiveText, apply: alertsLiveApply };')(rt, doc);
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

var total = passed + failed;
console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
process.exit(failed > 0 ? 1 : 0);
