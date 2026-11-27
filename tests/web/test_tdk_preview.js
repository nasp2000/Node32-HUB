// /tdk-lambda CSV tab: the "last polls" table.
//
// tdkRenderPrev() is fed by the same /api/tdk-lambda-samples the trend uses, so
// this suite runs it in jsdom and checks what the operator sees: newest first,
// at most 8 rows, fixed decimals, and an empty state instead of a blank table.
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets',
                      'page_tdk_lambda_asset.cpp');

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

const asset = fs.readFileSync(SRC, 'utf8');
const CODE = ['_tdkQ', '_tdkEsc', 'tdkRenderPrev', '_tdkArmTrend', '_tdkAsleep',
               'tdkStatus', 'tdkLog', 'tdkLoadTrend', '_tdkLines']
  .map(function (n) { return extractFn(asset, n); }).join('\n') +
  '\nvar _tdkTrendTimer = null;\nfunction tdkLoadTrend(){}\n';

function boot(rows) {
  const dom = new JSDOM('<!doctype html><html><body><table><tbody id="tdkPrevBody"></tbody></table>' +
                        '</body></html>', { runScripts: 'outside-only' });
  const w = dom.window;
  w.eval(CODE);
  w.__rows = rows || [];
  return w;
}

function cells(w) {
  return Array.prototype.slice.call(w.document.querySelectorAll('#tdkPrevBody tr')).map(function (tr) {
    return Array.prototype.slice.call(tr.querySelectorAll('td')).map(function (td) {
      return td.textContent;
    });
  });
}

// The endpoint contract: [ageSeconds, v, i], oldest first.
function sample(n) {
  const out = [];
  for (let i = 0; i < n; i++) out.push([(n - 1 - i) * 60, 600 + i, 1.3 + i / 100]);
  return out;
}

test('newest sample comes first and only the last 8 are shown', function () {
  const w = boot(sample(60));
  w.tdkRenderPrev(w.__rows);
  const rows = cells(w);
  is(rows.length, 8, 'row count');
  // sample 59 is the newest; sample 52 the oldest of the eight.
  is(rows[0][1], '659.00', 'newest voltage first');
  is(rows[7][1], '652.00', 'oldest of the eight last');
});

test('voltages keep 2 decimals, currents 3', function () {
  const w = boot([[60, 599.4567, 1.30049], [0, 600, 1.3]]);
  w.tdkRenderPrev(w.__rows);
  const rows = cells(w);
  is(rows.length, 2, 'row count');
  is(rows[0][1], '600.00', 'voltage decimals');
  is(rows[0][2], '1.300', 'current decimals');
  is(rows[1][1], '599.46', 'rounded voltage');
  is(rows[1][2], '1.300', 'rounded current');
});

test('the time column is now minus the sample age', function () {
  const age = 120;                              // 2 min old
  const w = boot([[age, 600, 1.3]]);
  const before = Date.now();
  w.tdkRenderPrev(w.__rows);
  const after = Date.now();
  const cell = cells(w)[0][0];
  const okBefore = new Date(before - age * 1000).toLocaleTimeString();
  const okAfter = new Date(after - age * 1000).toLocaleTimeString();
  ok(cell === okBefore || cell === okAfter, 'expected now-age, got ' + cell);
});

test('no samples shows the empty state, not an empty table', function () {
  const w = boot([]);
  w.tdkRenderPrev(w.__rows);
  const tr = w.document.querySelectorAll('#tdkPrevBody tr');
  is(tr.length, 1, 'one placeholder row');
  ok(/no samples yet/.test(tr[0].textContent), 'placeholder text');
});

test('a missing tbody does not throw', function () {
  const dom = new JSDOM('<!doctype html><html><body></body></html>', { runScripts: 'outside-only' });
  dom.window.eval(CODE);
  let threw = false;
  try { dom.window.tdkRenderPrev([[1, 2, 3]]); } catch (e) { threw = true; }
  ok(!threw, 'tdkRenderPrev must tolerate a page without the table');
});

test('a background browser tab does not fetch at all', function () {
  // jsdom always reports document.hidden, so the guard is exercised by stubbing
  // it: with the tab asleep none of the three pollers may touch fetch (or the
  // DOM), and that is exactly what stops the WiFi traffic and the heap churn.
  const w = boot([]);
  let fetches = 0;
  w.fetch = function () { fetches++; return Promise.resolve({}); };
  w._tdkAsleep = function () { return true; };
  w.tdkStatus(); w.tdkLog(); w.tdkLoadTrend();
  is(fetches, 0, 'no fetch while asleep');
});

test('the trend refresh follows the poll cadence, one timer only', function () {
  // The poll floor is 1 min now, so the refresh is clamped to 60..300 s.
  const w = boot([]);
  let created = 0, cleared = 0, lastMs = 0;
  w.setInterval = function (fn, ms) { created++; lastMs = ms; return created; };
  w.clearInterval = function () { cleared++; };
  w._tdkArmTrend(60000);            // initial: one row a minute
  is(created, 1, 'one timer');
  is(lastMs, 60000, 'initial cadence');
  w._tdkArmTrend(60000);            // same cadence: must not re-arm
  is(created, 1, 'no duplicate timer');
  w._tdkArmTrend(180000);           // 3 min poll: follow it
  is(created, 2, 're-armed for a new cadence');
  is(cleared, 1, 'old timer cleared');
  is(lastMs, 180000, 'poll cadence used');
  w._tdkArmTrend(5000);             // below the floor -> 60 s
  is(lastMs, 60000, 'clamp min (1 min)');
  w._tdkArmTrend(3600000);          // above the ceiling -> 300 s
  is(lastMs, 300000, 'clamp max');
});

test('the commands tab shows what was sent and what came back', function () {
  const dom = new JSDOM("<!doctype html><html><body>" +
    "<div id='tdkCatSentLine'>TX> --</div><div id='tdkCatReplyLine'>RX  --</div>" +
    "</body></html>", { runScripts: 'outside-only' });
  const w = dom.window;
  w.eval(CODE);
  w._tdkLines('DVC?', 'waiting\u2026');
  is(w.document.getElementById('tdkCatSentLine').textContent, 'TX> DVC?', 'sent line');
  is(w.document.getElementById('tdkCatReplyLine').textContent, 'RX  waiting\u2026', 'waiting line');
  w._tdkLines(null, '600.00,600.00,001.30,001.40,660.0,0.000');
  is(w.document.getElementById('tdkCatSentLine').textContent, 'TX> DVC?', 'null leaves the TX line');
  is(w.document.getElementById('tdkCatReplyLine').textContent,
     'RX  600.00,600.00,001.30,001.40,660.0,0.000', 'reply line');
});

console.log('\n' + passed + '/' + (passed + failed) + ' passed');
process.exit(failed ? 1 : 0);
