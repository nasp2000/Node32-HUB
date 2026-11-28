// /sensor-charts: the "no sources" guard.
//
// The page can only plot sensors, RS232 columns, Modbus or proximity. An
// RS232-only pack (no sensors, and no RS232 columns logged today) used to render
// blank charts with no explanation — this suite pins the guard and the banner.
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets',
                      'page_sensor_charts_asset.h');
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

const CODE = ['chartNoSourcesNeeded', 'showChartNoSources']
  .map(function (n) { return extractFn(asset, n); }).join('\n');

function boot() {
  const dom = new JSDOM('<!doctype html><html><body>' +
    "<div id='chartNoSources' class='no-sources' style='display:none'>" +
    "<b data-i18n='chartNoSourcesTitle'>No chart sources on this device</b>" +
    "<div id='chartNoSourcesMsg'></div></div></body></html>",
    { runScripts: 'outside-only' });
  dom.window.eval(CODE);
  return dom.window;
}

test('an RS232-only pack with no columns logged today needs the banner', function () {
  const w = boot();
  is(w.chartNoSourcesNeeded({ sensors: false, aht20: false, rs232: true }, 0), true);
});

test('RS232 columns logged today are a real source', function () {
  const w = boot();
  is(w.chartNoSourcesNeeded({ sensors: false, rs232: true }, 8), false);
});

test('compiled sensors, Modbus or proximity are sources even at zero', function () {
  const w = boot();
  is(w.chartNoSourcesNeeded({ sensors: true }, 0), false);
  is(w.chartNoSourcesNeeded({ aht20: true }, 0), false);
  is(w.chartNoSourcesNeeded({ mq135: true }, 0), false);
  is(w.chartNoSourcesNeeded({ modbus: true }, 0), false);
  is(w.chartNoSourcesNeeded({ proximity: true }, 0), false);
});

test('no caps at all never blocks the page', function () {
  const w = boot();
  is(w.chartNoSourcesNeeded(null, 0), false);
});

test('the banner is hidden until a reason arrives, then explains itself', function () {
  const w = boot();
  const box = w.document.getElementById('chartNoSources');
  is(box.style.display, 'none', 'hidden by default');
  w.showChartNoSources('RS232 charting is compiled, but no columns were logged for today.');
  is(box.style.display, '', 'shown');
  ok(/no columns were logged/.test(w.document.getElementById('chartNoSourcesMsg').textContent), 'reason text');
});

test('the cap check never adds phantom RS232 columns', function () {
  // addRS232Opts(cols>0?cols:8) made the page look configured while empty.
  ok(asset.indexOf('addRS232Opts(d&&d.cols>0?d.cols:8') < 0, 'no 8-column fallback');
  ok(asset.indexOf('if(cols>0){') >= 0, 'guarded by cols>0');
});

console.log('\n' + passed + '/' + (passed + failed) + ' passed');
process.exit(failed ? 1 : 0);
