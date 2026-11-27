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
const CODE = ['_tdkQ', '_tdkEsc', 'tdkRenderPrev']
  .map(function (n) { return extractFn(asset, n); }).join('\n');

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

function sample(n) {
  const out = [];
  for (let i = 0; i < n; i++) out.push([1764300000 + i * 60, 600 + i, 1.3 + i / 100]);
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
  const w = boot([[1764300000, 599.4567, 1.30049], [1764300060, 600, 1.3]]);
  w.tdkRenderPrev(w.__rows);
  const rows = cells(w);
  is(rows.length, 2, 'row count');
  is(rows[0][1], '600.00', 'voltage decimals');
  is(rows[0][2], '1.300', 'current decimals');
  is(rows[1][1], '599.46', 'rounded voltage');
  is(rows[1][2], '1.300', 'rounded current');
});

test('the time column is a local time, not an empty cell', function () {
  const ts = 1764300000;
  const w = boot([[ts, 600, 1.3]]);
  w.tdkRenderPrev(w.__rows);
  const expected = new Date(ts * 1000).toLocaleTimeString();
  is(cells(w)[0][0], expected, 'time cell');
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

console.log('\n' + passed + '/' + (passed + failed) + ' passed');
process.exit(failed ? 1 : 0);
