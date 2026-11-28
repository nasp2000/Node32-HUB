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

const CODE = ['chartNoSourcesNeeded', 'showChartNoSources', 'tdkChartPoints', 'tdkAutoConfigure']
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

test('a compiled TDK-Lambda module is a chart source on its own', function () {
  const w = boot();
  is(w.chartNoSourcesNeeded({ sensors: false, rs232: true, tdk: true }, 0), false);
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

test('TDK auto-configures the page in dual line: V on Y, A on Y2', function () {
  const dom = new JSDOM('<!doctype html><html><body>' +
    ['chart1Type','chart2Type','chart3Type','chart1Type2','chart2Type2','chart3Type2']
      .map(function (id) { return "<select id='" + id + "'></select>"; }).join('') +
    "<select id='chartStyle'><option value='line' selected>line</option><option value='dual'>dual</option></select>" +
    '</body></html>', { runScripts: 'outside-only' });
  const w = dom.window;
  w.syncDualSelectors = function () { w.__sync = true; };
  w.eval(CODE);
  w.tdkAutoConfigure();
  const d = w.document;
  is(d.getElementById('chart1Type').value, 'tdk_v', 'chart 1 = V');
  is(d.getElementById('chart1Type2').value, 'tdk_i', 'chart 1 Y2 = A');
  is(d.getElementById('chart2Type').value, 'tdk_p', 'chart 2 = W');
  is(d.getElementById('chart3Type').value, 'tdk_i', 'chart 3 = A');
  is(d.getElementById('chartStyle').value, 'dual', 'dual line style');
  is(w.__sync, true, 'Y2 selectors refreshed');
  // all six selects received the TDK options
  ['chart1Type','chart2Type','chart3Type','chart1Type2','chart2Type2','chart3Type2'].forEach(function (id) {
    ok(d.getElementById(id).querySelector("option[value='tdk_p']"), id + ' has TDK options');
  });
});

test('TDK samples become chart series with V, A and P', function () {
  const w = boot();
  const now = Date.UTC(2026, 10, 28, 12, 0, 0);
  const pts = w.tdkChartPoints([[120, 600.123, 1.2834], [60, 599.5, 1.27], [0, 600, 1.28]], now);
  is(pts.v.length, 3, 'three points');
  is(pts.v[0], 600.12, 'voltage rounded');
  is(pts.i[0], 1.283, 'current rounded');
  ok(Math.abs(pts.p[0] - 770.2) < 1.0, 'power = V x A: ' + pts.p[0]);
  is(pts.timestamps[0], Math.floor((now - 120000) / 1000), 'age -> epoch seconds');
  ok(/^\d\d:\d\d$/.test(pts.labels[0]), 'HH:MM label: ' + pts.labels[0]);
  is(w.tdkChartPoints([], now).v.length, 0, 'empty input');
  is(w.tdkChartPoints([[1, 'x', 2]], now).v.length, 0, 'non-numeric skipped');
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
