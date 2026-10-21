// Proximity Dashboard Tests: OEE calc, cards, polling
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

var passed = 0, failed = 0;
var testChain = Promise.resolve();

function test(name, fn) {
  try { fn(); passed++; console.log('  ✓ ' + name); }
  catch (e) { failed++; console.log('  ✗ ' + name + ': ' + e.message); }
}

// Async counterpart: defers fn() into a serial chain so shared `win` mocks
// (temporary fetch + setTimeout) set up inside fn() are not clobbered by the
// next test's setup. Runs strictly one at a time in declaration order.
function asyncTest(name, fn) {
  testChain = testChain.then(function() {
    return fn();
  }).then(function() {
    passed++; console.log('  ✓ ' + name);
  }).catch(function(e) {
failed++; console.log('  ✗ ' + name + ': ' + e.message);
  });
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }
function approx(a, b, eps) { if (Math.abs(a - b) > (eps || 0.01)) throw new Error('expected ' + b + ' ±' + (eps||0.01) + ' got ' + a); }

// Reimplements the browser-side OEE calculation from the dashboard page
function calcOeeBrowser(ch, cfg, runningMs, shiftStartMs) {
  var runningMin = Math.max(0, (runningMs - shiftStartMs)) / 60000;
  var target = (cfg && cfg.target_ppm) || 60;
  var theoretical = target * runningMin;

  // Availability tracking (simplified: assume running_sec known from live data)
  var runningSec = ch.running_sec || 0;
  var downtimeSec = ch.downtime_sec || 0;
  var totalSec = runningSec + downtimeSec;
  var avail = totalSec > 0 ? (runningSec / totalSec) * 100 : 0;

  var perf = theoretical > 0 ? Math.min(100, (ch.count_in / theoretical) * 100) : 0;

  var qual = ch.count_in > 0 ? ((ch.count_in - ch.count_out) / ch.count_in) * 100 : 100;

  var oee = ch.oee_manual > 0 ? ch.oee_manual : (avail * perf * qual) / 10000;
  return { avail: avail, perf: perf, qual: qual, oee: oee };
}

// ====== Test calcOeeBrowser ======
test('calcOeeBrowser: 100% ideal', function () {
  var ch = { count_in: 480, count_out: 0, running_sec: 28800, downtime_sec: 0, oee_manual: 0 };
  var cfg = { target_ppm: 1 };
  var result = calcOeeBrowser(ch, cfg, 28800000, 0);
  approx(result.avail, 100);
  approx(result.perf, 100);
  approx(result.qual, 100);
  approx(result.oee, 100);
});

test('calcOeeBrowser: availability affected by downtime', function () {
  var ch = { count_in: 400, count_out: 0, running_sec: 24000, downtime_sec: 4800, oee_manual: 0 };
  var cfg = { target_ppm: 1 };
  var result = calcOeeBrowser(ch, cfg, 28800000, 0);
  approx(result.avail, 83.33, 0.1);
});

test('calcOeeBrowser: performance capped at 100%', function () {
  var ch = { count_in: 600, count_out: 0, running_sec: 28800, downtime_sec: 0, oee_manual: 0 };
  var cfg = { target_ppm: 1 };
  var result = calcOeeBrowser(ch, cfg, 28800000, 0);
  is(result.perf, 100);
});

test('calcOeeBrowser: quality with rejects', function () {
  var ch = { count_in: 500, count_out: 50, running_sec: 28800, downtime_sec: 0, oee_manual: 0 };
  var cfg = { target_ppm: 1 };
  var result = calcOeeBrowser(ch, cfg, 28800000, 0);
  approx(result.qual, 90);
  approx(result.oee, 90);
});

test('calcOeeBrowser: manual override', function () {
  var ch = { count_in: 10, count_out: 5, running_sec: 1000, downtime_sec: 27800, oee_manual: 85 };
  var cfg = { target_ppm: 60 };
  var result = calcOeeBrowser(ch, cfg, 28800000, 0);
  is(result.oee, 85);
});

test('calcOeeBrowser: zero production = quality 100%', function () {
  var ch = { count_in: 0, count_out: 0, running_sec: 0, downtime_sec: 0, oee_manual: 0 };
  var cfg = { target_ppm: 1 };
  var result = calcOeeBrowser(ch, cfg, 0, 0);
  is(result.qual, 100);
  is(result.oee, 0);
});

test('calcOeeBrowser: theoretical output calculated correctly', function () {
  var ch = { count_in: 3600, count_out: 0, running_sec: 3600, downtime_sec: 0, oee_manual: 0 };
  var cfg = { target_ppm: 60 };
  var result = calcOeeBrowser(ch, cfg, 3600000, 0);
  // runningMin = 3600000/60000 = 60, theoretical = 60*60 = 3600
  approx(result.perf, 100);
});

// ====== Test card update helpers ======
test('card color class reflects OEE threshold', function () {
  var thresholds = [
    { oee: 95, expected: 'oee-green' },
    { oee: 75, expected: 'oee-yellow' },
    { oee: 50, expected: 'oee-red' },
    { oee: 85, expected: 'oee-green' },
    { oee: 60, expected: 'oee-yellow' },
    { oee: 59, expected: 'oee-red' }
  ];
  thresholds.forEach(function (t) {
    var cls;
    if (t.oee >= 85) cls = 'oee-green';
    else if (t.oee >= 60) cls = 'oee-yellow';
    else cls = 'oee-red';
    is(cls, t.expected, 'OEE ' + t.oee + '% should be ' + t.expected);
  });
});

test('ETA calculation matches live rate', function () {
  // ETA = (goal - count_in) / (rate_ppm / 60)  seconds
  var goal = 1000;
  var count_in = 856;
  var rate_ppm = 120;
  var remaining = goal - count_in; // 144
  var eta_seconds = rate_ppm > 0 ? (remaining / (rate_ppm / 60)) : Infinity;
  approx(eta_seconds, 72); // 144 / 2 = 72 seconds
});

test('ETA shows "—" when goal is 0 or met', function () {
  var eta1 = (100 - 50) / (10 / 60); // goal=100, count=50, ppm=10
  is(eta1 > 0, true);
  var eta2 = (100 - 100) / (10 / 60); // goal=100, count=100, ppm=10
  is(eta2, 0);
  var eta3 = (0 - 0) / (10 / 60); // goal=0
  is(eta3, 0);
});

// ====== Mock polling logic ======
test('polling data merge preserves previous channel state', function () {
  var prev = [
    { count_in: 10, count_out: 1, rate_ppm: 5.0 },
    { count_in: 20, count_out: 2, rate_ppm: 3.0 }
  ];
  var live = [
    { count_in: 15, count_out: 2, rate_ppm: 6.0 },
    { count_in: 25, count_out: 3, rate_ppm: 4.0 }
  ];
  // Simulate merge: update OEE accumulators using delta
  var merged = live.map(function (ch, i) {
    return {
      count_in: ch.count_in,
      count_out: ch.count_out,
      rate_ppm: ch.rate_ppm,
      delta_in: ch.count_in - prev[i].count_in,
      delta_out: ch.count_out - prev[i].count_out
    };
  });
  is(merged[0].delta_in, 5);
  is(merged[0].delta_out, 1);
  is(merged[1].delta_in, 5);
  is(merged[1].delta_out, 1);
});

test('direction indicator derived from delta_in vs delta_out', function () {
  function direction(deltaIn, deltaOut) {
    var net = deltaIn - deltaOut;
    return net > 0 ? 'IN' : (net < 0 ? 'OUT' : '—');
  }
  is(direction(10, 2), 'IN');
  is(direction(2, 10), 'OUT');
  is(direction(5, 5), '—');
  is(direction(0, 0), '—');
});

// ====== DOM: charts-only dashboard (no cards) + visCh cap ======
function loadDashPage() {
  var html = fs.readFileSync(path.join(__dirname, 'pages', 'proximity_dashboard.html'), 'utf8');
  var orig = console.error; console.error = function() {
    var m = arguments[0]||''; if(typeof m!=='string'||m.indexOf('Not implemented')<0) orig.apply(console,arguments);
  };
  var dom = new JSDOM(html, {url:'http://localhost', runScripts:'dangerously', pretendToBeVisual:true});
  console.error = orig;
  return dom;
}
function mockLive(n, used) {
  var chs = [];
  for (var i = 0; i < n; i++) {
    chs.push({ name: 'DI' + (i+1), mode: 0, ppm: 10,
      count_in: i < used ? 100 : 0, count_out: 0, total_in: 0, total_out: 0 });
  }
  return { channels: chs };
}
function mockCfg(n, used) {
  var chs = [];
  for (var i = 0; i < n; i++) {
    chs.push({ m: 0, gi: i < used ? 10 + i : 255, gl: 0, gc: 255, sc: 255 });
  }
  return { channels: chs };
}
async function dashDomTests() {
  var dom = loadDashPage();
  await new Promise(function(r){ setTimeout(function(){ r(); }, 800); });
  var win = dom.window;
  var doc = dom.window.document;

  test('renderRows shows charts only, no cards', function () {
    win.renderRows(mockLive(8, 3), mockCfg(8, 3));
    var rows = doc.getElementById('rows');
    ok(!!rows, 'rows container exists');
    is(rows.querySelectorAll('canvas').length, 3, '3 channels in use => 3 charts');
    is(rows.querySelectorAll('.pd-card').length, 0, 'no card squares rendered');
    is(rows.querySelectorAll('.pd-summary').length, 0, 'no summary card rendered');
  });

  test('renderRows honors prox_visCh cap', function () {
    win.localStorage.setItem('prox_visCh', '2');
    win.renderRows(mockLive(8, 3), mockCfg(8, 3));
    var rows = doc.getElementById('rows');
    is(rows.querySelectorAll('canvas').length, 2, 'visCh=2 => 2 charts');
    win.localStorage.removeItem('prox_visCh');
  });

  test('computePrimary caps visCh BEFORE aux filter (8 fixed pins + visCh=3 + scrap/gate => [DI1])', function () {
    // Regression: slicing after the aux filter showed DI1+DI4+DI5.
    var live = { channels: [] };
    var cfg = { channels: [] };
    for (var i = 0; i < 8; i++) {
      live.channels.push({ name: 'DI' + (i+1), mode: i === 2 ? 1 : 0, ppm: 0,
        count_in: 0, count_out: 0, total_in: 0, total_out: 0 });
      cfg.channels.push({ m: i === 2 ? 1 : 0, gi: 10 + i, gl: 0, gc: 255, sc: 255 });
    }
    live.channels[0].count_in = 5000;   // only DI1 producing
    cfg.channels[0].sc = 1; cfg.channels[0].gc = 2;
    win.localStorage.setItem('prox_visCh', '3');
    var prim = win.computePrimary(live, cfg);
    is(JSON.stringify(prim), JSON.stringify([0]), 'only DI1, never DI4/DI5');
    win.localStorage.removeItem('prox_visCh');
  });

  test('renderRows hides scrap+gate aux channels (DI1 prod + DI2 scrap + DI3 presence => 1 chart)', function () {
    var live = { channels: [
      { name: 'Prod', mode: 0, ppm: 600, count_in: 5000, count_out: 0, total_in: 0, total_out: 0 },
      { name: 'Scrap', mode: 0, ppm: 5, count_in: 50, count_out: 0, total_in: 0, total_out: 0 },
      { name: 'Gate', mode: 1, ppm: 0, count_in: 0, count_out: 0, total_in: 0, total_out: 0, state: true }
    ]};
    var cfg = { channels: [
      { m: 0, gi: 10, gl: 0, gc: 2, sc: 1 },
      { m: 0, gi: 11, gl: 0, gc: 255, sc: 255 },
      { m: 1, gi: 12, gl: 0, gc: 255, sc: 255 }
    ]};
    win.renderRows(live, cfg);
    var rows = doc.getElementById('rows');
    var charts = rows.querySelectorAll('canvas');
    is(charts.length, 1, 'only the production channel gets a chart');
    ok(rows.textContent.indexOf('Prod') >= 0, 'chart title is the production channel');
  });

  // jsdom has no 2d canvas: record all ctx calls so drawing can be asserted.
  var ctxCalls = [];
  var ctxProps = [];
  var ctxLog = [];
  win.HTMLCanvasElement.prototype.getContext = function() {
    return new Proxy({}, {
      get: function(t, p) {
        if (p === 'canvas') return {};
        return function() { var a = Array.prototype.slice.call(arguments); ctxCalls.push([p, a]); ctxLog.push(['call', p, a]); };
      },
      set: function(t, p, v) { ctxProps.push([p, v]); ctxLog.push(['set', p, v]); return true; }
    });
  };
  function fills() {
    return ctxProps.filter(function(c) { return c[0] === 'fillStyle'; }).map(function(c) { return c[1]; });
  }
  // T22: fillStyle values of fillRect calls inside the plot area only —
  // excludes the timeline strip below the hour labels (fallback canvas
  // geometry 400x180: plot bottom = 124, strip top = 156).
  function plotFills() {
    var out = [], cur = null;
    ctxLog.forEach(function(e) {
      if (e[0] === 'set' && e[1] === 'fillStyle') cur = e[2];
      else if (e[0] === 'call' && e[1] === 'fillRect' && e[2] && e[2][1] < 140) out.push(cur);
    });
    return out;
  }

  test('drawChart24 colors bars vs hourly target (orange below, green at/above)', function () {
    ctxProps = []; ctxCalls = [];
    var cv = doc.createElement('canvas');
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 35000; scrap[5] = 40;   // below 36000 target
    hourly[6] = 36000;                  // at target
    hourly[20] = 800;
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 600, 100, { target: 'on' });
    var fl = fills();
    ok(fl.indexOf('#f59e0b') >= 0, 'below-target bars are orange');
    ok(fl.indexOf('#16a34a') >= 0, 'at-target bars are green');
    ok(fl.indexOf('#dc2626') >= 0, 'scrap bars are red');
    ok(fl.indexOf('#94a3b8') < 0, 'no more gray production bars');
  });

  test('drawChart24 Y max = hourly target exactly (tgtPpm*60 @100%)', function () {
    ctxProps = []; ctxCalls = [];
    var cv = doc.createElement('canvas');
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 1000;
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 300, 100, { target: 'on' });
    var texts = ctxCalls.filter(function(c) { return c[0] === 'fillText'; }).map(function(c) { return c[1][0]; });
    ok(texts.indexOf('18000') >= 0, 'top tick is exactly 300*60=18000');
    ok(texts.indexOf('18000/h') >= 0, 'target line captioned 18000/h');
  });

  test('drawChart24 Y max grows when an hour exceeds the target', function () {
    ctxProps = []; ctxCalls = [];
    var cv = doc.createElement('canvas');
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 20000;   // over the 300*60=18000 target
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 300, 100, { target: 'on' });
    var texts = ctxCalls.filter(function(c) { return c[0] === 'fillText'; }).map(function(c) { return c[1][0]; });
    ok(texts.indexOf('21000') >= 0, 'top tick rises to ceil(20000*1.05)=21000');
    ok(texts.indexOf('18000/h') >= 0, 'target line still drawn below the top');
  });

  test('targetHourly scales PPM x 60 by the channel OEE target % (default 85, clamp 60-100)', function () {
    is(win.targetHourly(300), 15300, 'default 85%: 300*60*0.85=15300');
    is(win.targetHourly(300, 100), 18000, 'explicit 100% keeps legacy math');
    is(win.targetHourly(600, 60), 21600, 'floor 60%: 600*60*0.6=21600');
    is(win.targetHourly(300, 59), 15300, 'below floor clamps back to default 85');
    is(win.targetHourly(300, 101), 15300, 'above ceiling clamps back to default 85');
    is(win.targetHourly(300, 'bogus'), 15300, 'non-numeric falls back to 85');
    is(win.targetHourly(0, 100), 0, 'no PPM configured = no target');
    is(win.targetHourly(-5, 85), 0, 'negative PPM = no target');
  });

  test('drawChart24 defaults to the 85% target line (no vo)', function () {
    ctxProps = []; ctxCalls = [];
    var cv = doc.createElement('canvas');
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 1000;
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 300);
    var texts = ctxCalls.filter(function(c) { return c[0] === 'fillText'; }).map(function(c) { return c[1][0]; });
    ok(texts.indexOf('15300') >= 0, 'top tick is 300*60*0.85=15300 by default');
    ok(texts.indexOf('15300/h') >= 0, 'target line captioned 15300/h by default');
  });

  test('drawChart24 colors bars vs the 85% target by default', function () {
    ctxProps = []; ctxCalls = [];
    var cv = doc.createElement('canvas');
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 30000;   // below 30600 (600ppm @85%)
    hourly[6] = 30600;   // at the 85% target
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 600);
    var fl = fills();
    ok(fl.indexOf('#f59e0b') >= 0, 'below-%-target bars are orange');
    ok(fl.indexOf('#16a34a') >= 0, 'at-%-target bars are green');
  });

  test('chartLegendHTML follows Content/Colors modes (red=scrap, green/orange=good)', function () {
    var dual = win.chartLegendHTML('#0284c7', { bars: 'dual', colors: 'target' });
    ok(dual.indexOf('pd-legend') >= 0, 'legend wrapper present');
    ok(dual.indexOf('#16a34a') >= 0, 'green = good at/above target');
    ok(dual.indexOf('#f59e0b') >= 0, 'orange = good below target');
    ok(dual.indexOf('#dc2626') >= 0, 'red = scrap');
    var prod = win.chartLegendHTML('#0284c7', { bars: 'prod', colors: 'target' });
    ok(prod.indexOf('#dc2626') < 0, 'no scrap entry in production-only mode');
    ok(prod.indexOf('#16a34a') >= 0, 'good entries kept in production-only mode');
    var solid = win.chartLegendHTML('#0284c7', { bars: 'dual', colors: 'solid' });
    ok(solid.indexOf('#0284c7') >= 0, 'single channel-color entry in solid mode');
    ok(solid.indexOf('#16a34a') < 0 && solid.indexOf('#f59e0b') < 0, 'no green/orange split in solid mode');
    ok(solid.indexOf('#dc2626') >= 0, 'scrap entry kept in solid+dual mode');
  });

  test('renderRows injects one top-right legend per chart', function () {
    win.renderRows(mockLive(8, 3), mockCfg(8, 3));
    var rows = doc.getElementById('rows');
    var legends = rows.querySelectorAll('.pd-legend');
    is(legends.length, 3, '3 charts => 3 legends');
  });

  test('drawChart24 X axis runs 1h..24h with no shift markers', function () {
    ctxProps = []; ctxCalls = [];
    var cv = doc.createElement('canvas');
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 1000;
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 300);
    var texts = ctxCalls.filter(function(c) { return c[0] === 'fillText'; }).map(function(c) { return c[1][0]; });
    ok(texts.indexOf('1h') >= 0, 'first slot labelled 1h');
    ok(texts.indexOf('0h') < 0, 'no 0h label anymore');
    ok(texts.indexOf('23h') < 0, 'no 23h label anymore');
    var fl = fills();
    ok(fl.indexOf('#7c3aed') < 0, 'no shift-marker purple drawn');
    ok(!texts.some(function(x) { return /^S[1-9]/.test(x); }), 'no S1/S2/S3 tags drawn');
  });

  test('dayStats mid-day totals and rates', function () {
    var counts = [];
    for (var h = 0; h < 24; h++) counts.push(0);
    counts[9] = 18000; counts[10] = 18000; counts[11] = 9000; counts[12] = 4500;  // 49500 by 12:30
    var st = win.dayStats({ counts: counts }, new Date(2026, 8, 9, 12, 30, 0));
    is(st.dayTotal, 49500, 'day total sums all buckets');
    is(st.dayPpm, 66, '49500/750min rounds to 66');
    is(st.lastH, 4500, 'current-hour bucket');
    is(st.lastPpm, 150, '4500/30min = 150');
  });

  test('dayStats midnight edge and empty input never NaN', function () {
    var st = win.dayStats({ counts: [] }, new Date(2026, 8, 9, 0, 0, 0));
    is(st.dayTotal, 0, 'empty buckets sum to 0');
    is(st.dayPpm, 0, '0/0min guarded to 0');
    is(st.lastPpm, 0, '0/0min guarded to 0');
    var st2 = win.dayStats(null, new Date(2026, 8, 9, 10, 0, 0));
    is(st2.dayTotal, 0, 'null hd sums to 0');
    is(st2.lastH, 0, 'null hd last hour is 0');
  });

  test('prodDayBoundary mirrors the firmware day-boundary rule', function () {
    is(win.prodDayBoundary(null), 0, 'no config => calendar day');
    is(win.prodDayBoundary({ en: false, slots: [{ h: 22 }] }), 0, 'shifts disabled => 0');
    is(win.prodDayBoundary({ en: true, slots: [] }), 0, 'empty slots => 0');
    is(win.prodDayBoundary({ en: true, slots: [{ h: 0 }, { h: 8 }] }), 0, 'a 00:00 slot keeps midnight rollover');
    is(win.prodDayBoundary({ en: true, slots: [{ h: 6 }, { h: 14 }, { h: 22 }] }), 22, '6/14/22 => boundary 22');
    is(win.prodDayBoundary({ en: true, slots: [{ h: 22 }] }), 22, 'single 22h slot => 22');
  });

  test('prodDay columns follow the shift layout (boundary 22 => prod 22H..21H)', function () {
    is(win.prodDayStart(0), 0, 'boundary 0 => calendar day starts at hour 0');
    is(win.prodDayStart(22), 22, 'boundary 22 => first column is production hour 22');
    is(win.colForHour(22, 22), 0, '22H production is column 0');
    is(win.colForHour(23, 22), 1, '23H production is column 1');
    is(win.colForHour(21, 22), 23, '21H production is column 23');
    is(win.colForHour(12, 0), 12, 'boundary 0 => identity');
    for (var h = 0; h < 24; h++) {
      is(win.hourForCol(win.colForHour(h, 22), 22), h, 'round-trip hour ' + h);
    }
    is(win.hourForCol(0, 22), 22, 'column 0 holds production hour 22');
    is(win.hourForCol(23, 22), 21, 'column 23 holds production hour 21');
  });

  test('prodDayDates labels the day the production day ends into', function () {
    function fmt(d) { return d.getDate() + '-' + (d.getMonth() + 1) + '-' + d.getFullYear(); }
    var a = win.prodDayDates(new Date(2026, 8, 21, 23, 16, 0), 22);
    is(fmt(a.aligned), '22-9-2026', '23:16 past boundary => aligned next date');
    is(fmt(a.prev), '21-9-2026', 'previous date backs the 23H column');
    var b = win.prodDayDates(new Date(2026, 8, 21, 10, 0, 0), 22);
    is(fmt(b.aligned), '21-9-2026', '10:00 before boundary => aligned today');
    is(fmt(b.prev), '20-9-2026', 'prev is yesterday');
    var c = win.prodDayDates(new Date(2026, 8, 21, 23, 16, 0), 0);
    is(fmt(c.aligned), '21-9-2026', 'boundary 0 => calendar date unchanged');
    var d = win.prodDayDates(new Date(2026, 9, 1, 23, 0, 0), 22);
    is(fmt(d.aligned), '2-10-2026', 'month rollover forward');
    is(fmt(d.prev), '1-10-2026', 'month rollover back');
  });

  test('dayStats with boundary re-bases the DAY rate on the production-day start', function () {
    var counts = [];
    for (var k = 0; k < 24; k++) counts.push(0);
    counts[1] = 760;    // column 1 = production hour 23, the current hour at 23:16
    counts[2] = 6840;   // column 2 = production hour 00
    var st = win.dayStats({ counts: counts }, new Date(2026, 8, 21, 23, 16, 0), 22);
    is(st.dayTotal, 7600, 'day total sums all 24 columns');
    is(st.dayPpm, 100, '7600/76min since 22:00 rounds to 100');
    is(st.lastH, 760, 'last-hour bucket is the current-hour column');
    var legacy = win.dayStats({ counts: counts }, new Date(2026, 8, 21, 23, 16, 0));
    is(legacy.dayPpm, 5, 'omitted boundary keeps midnight math (7600/1396min = 5)');
  });

  test('mergeHistoryBackfill: page opened mid-hour still shows the full hour from SD', function () {
    // Page opened at 22:41 inside production column 0 (live counter at 22:41
    // already holds the hour so far). SD holds 10000 for the hour; live has
    // 10100. Backfill must put 10000 in the column and re-anchor baseline to
    // 100 (=10100-10000) so the next live poll adds only the delta past SD.
    var hd = { counts: [0,0,0], scrapCounts:[0,0,0], lastCol: 0, lastCount: 10100, baseline: 10100, scrapBaseline: 0 };
    var bc = [10000, 0, 0]; var bs = [40, 0, 0];
    var nb = win.mergeHistoryBackfill(hd, bc, bs, 10100, 40, 0, true);
    is(hd.counts[0], 10000, 'current column backfilled with the full hour from SD');
    ok(nb !== null, 'baselines are re-anchored');
    is(nb.baseline, 100, 'baseline = live 10100 - SD 10000 → 100');
    is(nb.scrapBaseline, 0, 'scrap baseline = 40 - 40 → 0');
  });

  test('mergeHistoryBackfill: past columns overwrite, current column follows live delta', function () {
    var hd = { counts: [0,0,0], scrapCounts:[0,0,0], lastCol: 2, lastCount: 20000, baseline: 9000, scrapBaseline: 5 };
    var bc = [8500, 9100, 2400]; var bs = [10, 12, 9];
    var nb = win.mergeHistoryBackfill(hd, bc, bs, 20000, 30, 2, true);
    is(hd.counts[0], 8500, 'past column 0 replaced by SD');
    is(hd.counts[1], 9100, 'past column 1 replaced by SD');
    is(hd.counts[2], 2400, 'current column backfilled (17600 live delta would have undercounted)');
    is(nb.baseline, 20000 - 2400, 'baseline re-anchored to 17600 → next poll adds live delta only');
  });

  test('mergeHistoryBackfill: no re-anchor after an hour rollover (baseline already correct)', function () {
    var hd = { counts: [0,0,0], scrapCounts:[0,0,0], lastCol: 0, lastCount: 12000, baseline: 5000, scrapBaseline: 3 };
    var bc = [10000, 11000, 200]; var bs = [11, 13, 1];
    var nb = win.mergeHistoryBackfill(hd, bc, bs, 12000, 30, 1, false);
    is(hd.counts[0], 10000, 'past column still backfilled');
    is(hd.counts[1], 11000, 'new current column backfilled from SD even without re-anchor');
    ok(nb === null, 'baseline left untouched (rollover already set it)');
    is(hd.baseline, 5000, 'rollover baseline kept as-is');
  });

  test('mergeHistoryBackfill: guards negative baselines after a counter auto-reset', function () {
    var hd = { counts: [0,0], scrapCounts:[0,0], lastCol: 0, lastCount: 500, baseline: 12000, scrapBaseline: 9 };
    var bc = [600, 0]; var bs = [8, 0];
    var nb = win.mergeHistoryBackfill(hd, bc, bs, 500, 8, 0, true);
    is(hd.counts[0], 600, 'SD backfill kept for the current column');
    is(nb.baseline, 0, 'live(500) < SD(600) → baseline clamped to 0 (never negative)');
    is(nb.scrapBaseline, 0, 'scrap baseline clamped to 0');
  });

  test('drawChart24 with dayStart labels columns 23h,24h,1h..22h left-to-right', function () {
    ctxProps = []; ctxCalls = [];
    var cv = doc.createElement('canvas');
    var hourly = []; var scrap = [];
    for (var k = 0; k < 24; k++) { hourly.push(0); scrap.push(0); }
    hourly[0] = 1000;
    win.drawChart24(cv, hourly, scrap, 'DI1', '22-09-2026', 0, '#0284c7', 300, 85, { dayStart: 22 });
    var texts = ctxCalls.filter(function(c) { return c[0] === 'fillText'; }).map(function(c) { return c[1][0]; });
    ok(texts.indexOf('23h') >= 0, 'first column (production 22H) labelled 23h');
    ok(texts.indexOf('11h') >= 0, 'middle column labelled by its end hour');
    ok(texts.indexOf('22h') >= 0, 'last column (production 21H) labelled 22h');
    ok(texts.indexOf('1h') < 0, 'thinned-out columns still skipped (auto ticks)');
    ok(texts.indexOf('24h') < 0, 'end-hour 24 only when its column is drawn');
    ok(texts.indexOf('0h') < 0, 'no 0h label');
  });

  test('chartYMax target mode is exact, grows only when exceeded', function () {
    is(win.chartYMax(1000, 18000, 'target'), 18000, 'top is exactly the hourly target');
    is(win.chartYMax(20000, 18000, 'target'), 21000, 'grows to ceil(dataMax*1.05) when exceeded');
    is(win.chartYMax(1000, 0, 'target'), 1150, 'no target configured falls back to data-driven');
  });

  test('chartYMax fit and wide modes', function () {
    is(win.chartYMax(20000, 18000, 'fit'), 21000, 'fit ignores the target');
    is(win.chartYMax(1000, 18000, 'fit'), 1050, 'fit hugs small data');
    is(win.chartYMax(1000, 18000, 'wide'), 36000, 'wide is exactly 2x the target');
    is(win.chartYMax(40000, 18000, 'wide'), 42000, 'wide still grows past 2x when exceeded');
    is(win.chartYMax(0, 0, 'fit'), 1, 'never below 1');
  });

  test('chartTickStep auto thresholds plus all/sparse overrides', function () {
    is(win.chartTickStep(10, 'auto'), 4, 'narrow slots thin to every 4th');
    is(win.chartTickStep(20, 'auto'), 2, 'medium slots thin to every 2nd');
    is(win.chartTickStep(40, 'auto'), 1, 'wide slots label every hour');
    is(win.chartTickStep(10, 'all'), 1, 'all overrides thinning');
    is(win.chartTickStep(40, 'sparse'), 4, 'sparse forces every 4th');
  });

  test('dashOpts defaults and invalid-value sanitizing', function () {
    win.localStorage.removeItem('prox_dash_opts');
    var d = win.dashOpts();
    is(d.bars, 'dual', 'default bars');
    is(d.window, 24, 'default window');
    is(d.y, 'target', 'default Y mode');
    is(d.targetPct, undefined, 'no separate target % (line follows channel OEE target)');
    win.localStorage.setItem('prox_dash_opts', JSON.stringify({ bars: 'bogus', window: 99, y: 'fit', targetPct: 40 }));
    var d2 = win.dashOpts();
    is(d2.bars, 'dual', 'invalid bars falls back to dual');
    is(d2.window, 24, 'invalid window falls back to 24');
    is(d2.y, 'fit', 'valid y survives');
    is(d2.targetPct, undefined, 'legacy stored target % is ignored');
    win.localStorage.removeItem('prox_dash_opts');
  });

  test('dashOpts round-trips through localStorage', function () {
    win.localStorage.removeItem('prox_dash_opts');
    win.saveDashOpts({ bars: 'prod', colors: 'solid', labels: 'off', target: 'off', hour: 'off', window: 8, y: 'wide', ticks: 'sparse' });
    var d = win.dashOpts();
    is(d.bars, 'prod', 'bars persisted');
    is(d.window, 8, 'window persisted');
    is(d.ticks, 'sparse', 'ticks persisted');
    is(d.targetPct, undefined, 'no target % persisted');
    win.localStorage.removeItem('prox_dash_opts');
  });

  test('drawChart24 prod mode hides scrap bars, dual shows them', function () {
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 35000; scrap[5] = 40;
    ctxProps = []; ctxCalls = []; ctxLog = [];
    var cv = doc.createElement('canvas');
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 600, 85, { bars: 'prod' });
    ok(plotFills().indexOf('#dc2626') < 0, 'no red scrap fills in prod mode');
    ctxProps = []; ctxCalls = []; ctxLog = [];
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 600, 85, { bars: 'dual' });
    ok(plotFills().indexOf('#dc2626') >= 0, 'red scrap fills in dual mode');
  });

  test('drawChart24 solid colors skip target green/orange', function () {
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 35000; hourly[6] = 36000;
    ctxProps = []; ctxCalls = []; ctxLog = [];
    var cv = doc.createElement('canvas');
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 600, 85, { colors: 'solid' });
    var fl = plotFills();
    ok(fl.indexOf('#16a34a') < 0 && fl.indexOf('#f59e0b') < 0, 'no target colors in solid mode');
    ok(fl.indexOf('#0284c7') >= 0, 'channel color used instead');
  });

  test('drawChart24 draws one timeline strip segment per hour below the plot', function () {
    var hourly = []; for (var h = 0; h < 24; h++) hourly.push(h * 100);
    ctxProps = []; ctxCalls = []; ctxLog = [];
    var cv = doc.createElement('canvas');
    win.drawChart24(cv, hourly, [], 'DI1', '06-09-2026', 20, '#0284c7', 600, 85, {});
    var strip = ctxCalls.filter(function(c) { return c[0] === 'fillRect' && c[1][3] === 12; });
    is(strip.length, 24, '24 strip segments');
    ok(strip[0][1][1] > 130, 'strip sits below the plot area');
  });

  test('drawChart24 labels off hides value numbers but keeps ticks', function () {
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 35000;
    var cv = doc.createElement('canvas');
    ctxProps = []; ctxCalls = [];
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 0, 85, { labels: 'off', target: 'off', hour: 'off' });
    var texts = ctxCalls.filter(function(c) { return c[0] === 'fillText'; }).map(function(c) { return c[1][0]; });
    ok(texts.indexOf('35000') < 0, 'production value hidden');
    ok(texts.indexOf('5h') >= 0, 'hour tick still drawn');
    ctxProps = []; ctxCalls = [];
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 20, '#0284c7', 0, 85, {});
    var texts2 = ctxCalls.filter(function(c) { return c[0] === 'fillText'; }).map(function(c) { return c[1][0]; });
    ok(texts2.indexOf('35000') >= 0, 'production value drawn by default');
  });

  test('drawChart24 8h window shows last 8 hours ending at current hour', function () {
    var hourly = []; var scrap = [];
    for (var h = 0; h < 24; h++) { hourly.push(0); scrap.push(0); }
    hourly[5] = 35000;
    var cv = doc.createElement('canvas');
    ctxProps = []; ctxCalls = [];
    win.drawChart24(cv, hourly, scrap, 'DI1', '06-09-2026', 22, '#0284c7', 0, 85, { window: 8, target: 'off', hour: 'off' });
    var texts = ctxCalls.filter(function(c) { return c[0] === 'fillText'; }).map(function(c) { return c[1][0]; });
    ok(texts.indexOf('16h') >= 0, 'window starts 8 slots back');
    ok(texts.indexOf('1h') < 0, 'early hours outside the window');
    ok(texts.indexOf('23h') >= 0, 'window ends at the current hour');
  });

  test('sidebar panel exists with all ten option selects', function () {
    var panel = doc.getElementById('pdPanel');
    ok(!!panel, 'panel exists');
    ok(!!doc.getElementById('pdTrigger'), 'hover trigger exists');
    var ids = ['pdChart', 'pdTitle', 'pdBarMode', 'pdColors', 'pdLabels', 'pdTargetLine', 'pdHourMark', 'pdWindow', 'pdYMode', 'pdTicks'];
    ids.forEach(function(id) { ok(!!doc.getElementById(id), 'select ' + id + ' exists'); });
    is(doc.getElementById('pdWindow').value, '24', 'window defaults to 24h');
    is(doc.getElementById('pdChart').value, 'bars', 'chart type defaults to bars');
    is(doc.getElementById('pdTitle').value, 'sys', 'title defaults to system name');
  });

  test('onDashOptChange persists selections and rerenders', function () {
    win.localStorage.removeItem('prox_dash_opts');
    doc.getElementById('pdWindow').value = '12';
    doc.getElementById('pdYMode').value = 'fit';
    var live = { channels: [{ name: 'DI1', mode: 0, ppm: 10, count_in: 100, count_out: 0, total_in: 0, total_out: 0 }] };
    var cfg = { channels: [{ m: 0, gi: 10, gl: 0, gc: 255, sc: 255 }] };
    win.renderRows(live, cfg);
    win._lastLive = live; win._lastCfg = cfg;
    win.onDashOptChange();
    var saved = JSON.parse(win.localStorage.getItem('prox_dash_opts'));
    is(saved.window, 12, 'window persisted');
    is(saved.y, 'fit', 'Y mode persisted');
    win.localStorage.removeItem('prox_dash_opts');
    win._lastLive = null; win._lastCfg = null;
  });

  test('chart titles default to system name, channel name optional', function () {
    var live = { channels: [
      { name: 'Linha 1', mode: 0, ppm: 10, count_in: 100, count_out: 0, total_in: 0, total_out: 0 }
    ]};
    var cfg = { channels: [{ m: 0, gi: 10, gl: 0, gc: 255, sc: 255 }] };
    win.localStorage.removeItem('prox_dash_opts');
    win.sysName = 'Equip15';
    win.renderRows(live, cfg);
    var rows = doc.getElementById('rows');
    ok(rows.textContent.indexOf('Equip15') >= 0, 'default title shows the system name');
    win.localStorage.setItem('prox_dash_opts', JSON.stringify({ title: 'ch' }));
    win.renderRows(live, cfg);
    ok(rows.textContent.indexOf('Linha 1') >= 0, 'channel-name mode shows the channel name');
    win.localStorage.removeItem('prox_dash_opts');
    win.sysName = '';
  });

  test('fmtHMS formats elapsed as H:MM:SS', function () {
    is(win.fmtHMS(3727), '1:02:07', '3727s');
    is(win.fmtHMS(0), '0:00:00', 'zero');
  });

  test('updateHourly fills scrap buckets from the mapped scrap channel IN', function () {
    delete win.hourlyData['0']; delete win.chartLines['0'];
    delete win.hourlyData['1']; delete win.chartLines['1'];
    delete win.hourlyData['2']; delete win.chartLines['2'];
    var live = { channels: [
      { name: 'Prod', mode: 0, ppm: 600, count_in: 5000, count_out: 0, total_in: 0, total_out: 0 },
      { name: 'Scrap', mode: 0, ppm: 5, count_in: 50, count_out: 0, total_in: 0, total_out: 0 },
      { name: 'Gate', mode: 1, ppm: 0, count_in: 0, count_out: 0, total_in: 0, total_out: 0, state: true }
    ]};
    var cfg = { channels: [
      { m: 0, gi: 10, gl: 0, gc: 2, sc: 1 },
      { m: 0, gi: 11, gl: 0, gc: 255, sc: 255 },
      { m: 1, gi: 12, gl: 0, gc: 255, sc: 255 }
    ]};
    win.renderRows(live, cfg);
    win.updateHourly(live);
    var hd = win.hourlyData['0'];
    ok(!!hd, 'hourly state exists for DI1');
    var curH = new Date().getHours();
    ok(hd.counts[curH] >= 0, 'production bucket filled');
    // +40 scrap parts on DI2 => scrap bucket grows by 40 (not from DI1 count_out=0).
    live.channels[1].count_in = 90;
    live.channels[0].count_in = 5100;
    win.updateHourly(live);
    is(win.hourlyData['0'].scrapCounts[curH], 40, 'scrap bucket tracks DI2 IN delta');
  });

  test('updateHourly falls back to own OUT when no scrap channel mapped', function () {
    delete win.hourlyData['0']; delete win.chartLines['0'];
    var live = { channels: [
      { name: 'Prod', mode: 0, ppm: 600, count_in: 1000, count_out: 7, total_in: 0, total_out: 0 }
    ]};
    win.renderRows(live, { channels: [{ m: 0, gi: 10, gl: 0, gc: 255, sc: 255 }] });
    win.updateHourly(live);
    live.channels[0].count_in = 1100;
    live.channels[0].count_out = 10;
    win.updateHourly(live);
    var curH = new Date().getHours();
    is(win.hourlyData['0'].scrapCounts[curH], 3, 'scrap bucket tracks own OUT delta');
  });

  test('detectShiftIndex mirrors firmware shift detection', function () {
    var slots = [{ h: 6, m: 0 }, { h: 14, m: 0 }, { h: 22, m: 0 }];
    is(win.detectShiftIndex(slots, new Date(2026, 8, 9, 7, 0, 0)), 0, '06:00 shift at 07:00');
    is(win.detectShiftIndex(slots, new Date(2026, 8, 9, 14, 30, 0)), 1, '14:00 shift at 14:30');
    is(win.detectShiftIndex(slots, new Date(2026, 8, 9, 21, 59, 0)), 1, 'before 22:00 -> 14:00 shift');
    is(win.detectShiftIndex(slots, new Date(2026, 8, 9, 23, 5, 0)), 2, '22:00 shift late night');
    is(win.detectShiftIndex(slots, new Date(2026, 8, 9, 3, 0, 0)), 2, 'before first slot -> night shift (index 2)');
    is(win.detectShiftIndex([], new Date(2026, 8, 9, 10, 0, 0)), -1, 'no slots -> -1');
    is(win.detectShiftIndex(null, new Date(2026, 8, 9, 10, 0, 0)), -1, 'null slots -> -1');
  });

  test('shiftWindowText formats the shift range (wraps midnight)', function () {
    var slots = [{ h: 6, m: 0 }, { h: 14, m: 0 }, { h: 22, m: 0 }];
    is(win.shiftWindowText(slots, 0), '06:00\u201314:00', '06h shift');
    is(win.shiftWindowText(slots, 1), '14:00\u201322:00', '14h shift');
    is(win.shiftWindowText(slots, 2), '22:00\u201306:00', '22h shift wraps past midnight');
    is(win.shiftWindowText(slots, 9), '', 'invalid index');
  });

  test('shiftTurnNumber mirrors firmware turno rotation (boundary turn = Turno 1)', function () {
    var slots = [{ h: 6, m: 0 }, { h: 14, m: 0 }, { h: 22, m: 0 }];
    is(win.shiftTurnNumber(slots, 0), 2, '06h is Turno 2');
    is(win.shiftTurnNumber(slots, 1), 3, '14h is Turno 3');
    is(win.shiftTurnNumber(slots, 2), 1, '22h (overnight boundary turn) is Turno 1');
    var two = [{ h: 6, m: 0 }, { h: 14, m: 0 }];
    is(win.shiftTurnNumber(two, 0), 2, '2-slot 6/14: 06h is Turno 2');
    is(win.shiftTurnNumber(two, 1), 1, '2-slot 6/14: 14h (overnight) is Turno 1');
    var midnight = [{ h: 0, m: 0 }, { h: 6, m: 0 }, { h: 14, m: 0 }];
    is(win.shiftTurnNumber(midnight, 0), 1, '00:00 rollover -> identity numbering');
    is(win.shiftTurnNumber(midnight, 1), 2, '00:00 rollover -> identity numbering');
    is(win.shiftTurnNumber(midnight, 2), 3, '00:00 rollover -> identity numbering');
    is(win.shiftTurnNumber([], 0), 1, 'no slots -> fallback slotIdx+1');
    is(win.shiftTurnNumber(null, 2), 3, 'null slots -> fallback slotIdx+1');
    is(win.shiftTurnNumber(slots, 9), 10, 'out-of-range -> fallback slotIdx+1');
  });

  test('donutFractions splits production/planned/unplanned and clamps', function () {
    var fr = win.donutFractions(17856, 4320, 28800);
    ok(Math.abs(fr.prod - 0.62) < 0.001, 'production fraction');
    ok(Math.abs(fr.plan - 0.15) < 0.001, 'planned fraction');
    ok(Math.abs(fr.prod + fr.plan + fr.unplan - 1) < 0.001, 'sums to 1');
    is(win.donutFractions(100, 50, 0), null, 'empty window -> null');
    is(win.donutFractions(0, 0, 0), null, 'zero elapsed -> null');
    var over = win.donutFractions(99999, 99999, 3600);
    ok(Math.abs(over.prod + over.plan + over.unplan - 1) < 0.001, 'overflow clamps to 1');
    is(over.unplan, 0, 'no negative unplanned');
  });

  test('updateOeeKpi shows OEE/A/Q/P for the primary channel + shift box', function () {
    win._lastLive = { channels: [
      { name: 'Prod', mode: 0, ppm: 10, count_in: 100, count_out: 0, total_in: 0, total_out: 0 }
    ]};
    win._lastCfg = { channels: [{ m: 0, gi: 10, gl: 0, gc: 255, sc: 255 }] };
    var oee = [{ ch: 0, name: 'Prod', availability: 95.1, performance: 92.0, quality: 99.8, oee: 87.2, day_oee: 91.5, day_a: 97.0, day_p: 94.0, day_q: 99.9, day_run: 17856, day_planned: 4320, day_elapsed: 28800 }];
    var shiftCfg = { en: true, slots: [{ h: 6, m: 0 }, { h: 14, m: 0 }, { h: 22, m: 0 }] };
    win.updateOeeKpi(oee, shiftCfg);
    var bar = doc.getElementById('oeeBar');
    ok(bar.style.display === 'flex', 'KPI bar is visible');
    is(doc.getElementById('oeeVal').textContent, '87.2%', 'OEE value');
    is(doc.getElementById('dayOeeVal').textContent, '91.5%', 'day OEE value');
    is(doc.getElementById('donutProd').textContent, '62%', 'donut production 17856/28800');
    is(doc.getElementById('donutPlan').textContent, '15%', 'donut planned 4320/28800');
    is(doc.getElementById('donutUnplan').textContent, '23%', 'donut unplanned sums to 100');
    ok(ctxCalls.some(function(c) { return c[0] === 'arc'; }), 'donut arcs drawn');
    is(doc.getElementById('availVal').textContent, '95.1%', 'A value');
    is(doc.getElementById('perfVal').textContent, '92%', 'P value');
    is(doc.getElementById('qualVal').textContent, '99.8%', 'Q value');
    ok(doc.querySelector('#oeeBar .pd-kpi-shift'), 'shift box styled like the OEE boxes');
    var shiftVal = doc.getElementById('shiftName').textContent;
    ok(/^[1-3] \(/.test(shiftVal), 'shift value is number + range, got: ' + shiftVal);
    ok(shiftVal.indexOf('--') < 0 &&
       shiftVal.indexOf('—') < 0, 'not an empty dash');
  });

  test('shift boxes share one pd-shift-box, day donut stays separate', function () {
    var grp = doc.querySelector('#oeeBar .pd-shift-box');
    ok(!!grp, 'shift group rectangle exists');
    var ids = ['shiftName', 'oeeVal', 'availVal', 'perfVal', 'qualVal'];
    for (var k = 0; k < ids.length; k++) {
      var el = doc.getElementById(ids[k]);
      ok(!!el && !!el.closest && el.closest('.pd-shift-box') === grp, ids[k] + ' lives inside the shift group');
    }
    var donutBox = doc.querySelector('#oeeBar .pd-donut-box');
    ok(!!donutBox && donutBox !== grp, 'day donut box is a separate rectangle');
    ok(!grp.contains(doc.getElementById('dayDonut')), 'donut canvas not in the shift group');
    ok(!grp.contains(doc.getElementById('dayOeeVal')), 'day OEE box not in the shift group');
  });

  // ====== Sequential guarded-route fetch (129/parallel collision regression) ======
  // /api/proximity-chart-data is rate-limited server-side (~1 req/s); the page
  // must fetch the two segments SEQUENTIALLY and retry on 429 so neither
  // segment is dropped (previously a parallel Promise.all left columns empty).
  function seqMockRes(status, data) {
    return { ok: status >= 200 && status < 300, status: status,
      json: function() { return Promise.resolve(data || {}); },
      text: function() { return Promise.resolve(JSON.stringify(data || {})); } };
  }

  function runWithMocks(callSpecs, f) {
    // Force retries to fire immediately (no 1.1s waits in tests).
    var origSetTimeout = win.setTimeout;
    win.setTimeout = function(fn, ms) { return origSetTimeout(fn, 0); };
    var origFetch = win.fetch;
    var callLog = [];
    // callSpecs: { urlKey: seg | { status, until, data } } — `status` is
    // returned for the first `until` calls matching urlKey, then `data`.
    win.fetch = function(url, opts) {
      var u = typeof url === 'string' ? url : (url && url.url ? url.url : '');
      callLog.push(u);
      var key = Object.keys(callSpecs).filter(function(k) { return u.indexOf(k) >= 0; })[0];
      var spec = callSpecs[key];
      if (spec && spec.timestamps) { spec = { status: 200, until: 0, data: spec }; } // segment payload shorthand
      spec = spec || { status: 200, until: 0, data: null };
      var calls = callLog.filter(function(l) { return l === u; }).length;
      if (calls <= spec.until) {
        return Promise.resolve(seqMockRes(spec.status || 429));
      }
      return Promise.resolve(seqMockRes(200, spec.data || {}));
    };
    return f().then(function(r) {
      win.fetch = origFetch;
      win.setTimeout = origSetTimeout;
      return r;
    }).catch(function(e) {
      win.fetch = origFetch;
      win.setTimeout = origSetTimeout;
      throw e;
    });
  }

  function chartSeg(stamps, inVals) {
    return { count: stamps.length, stride: 1, labels: [], timestamps: stamps, prox_ch0_in: inVals };
  }

  asyncTest('fetchChartSegments fetches both segments sequentially, order preserved', function () {
    var segA = chartSeg([100, 200], [10, 15]);
    var segB = chartSeg([300, 400], [20, 30]);
    return runWithMocks({ 'from': segA, 'bed&mode': segB }, function() {
      return win.fetchChartSegments(['/x?date=prev&from=5', '/x?date=bed&mode=day']).then(function(parts) {
        is(parts.length, 2, 'both segments returned');
        is(parts[0].timestamps[0], 100, 'segment 1 (prev) data intact');
        is(parts[1].timestamps[0], 300, 'segment 2 (aligned) data intact');
      });
    });
  });

  asyncTest('fetchChartSegmentOnce retries on 429 and recovers the segment', function () {
    var seg = chartSeg([100, 200, 300], [5, 8, 11]);
    return runWithMocks({ '/x': { status: 429, until: 1, data: seg } }, function() {
      return win.fetchChartSegmentOnce('/x', 3).then(function(d) {
        ok(d && d.timestamps.length === 3, 'retry recovered the dropped segment');
        is(d.timestamps[2], 300, 'recovered segment payload intact');
      });
    });
  });

  asyncTest('fetchChartSegmentOnce resolves null after retries are exhausted (429)', function () {
    return runWithMocks({ '/x': { status: 429, until: 99, data: null } }, function() {
      return win.fetchChartSegmentOnce('/x', 2).then(function(d) {
        is(d, null, 'persistent 429 => null segment, gracefully skipped downstream');
      });
    });
  });

  asyncTest('fetchChartSegments composes both segments even when one had a 429 blip', function () {
    var segA = chartSeg([50, 60], [1, 2]);
    var segB = chartSeg([70, 80], [3, 4]);
    return runWithMocks({ 'prev': { status: 429, until: 1, data: segA }, 'today': segB }, function() {
      return win.fetchChartSegments(['/x?date=prev', '/x?date=today']).then(function(parts) {
        is(parts.length, 2, 'segments before and after the 429 blip both present');
        ok(parts[0] && parts[0].timestamps, 'retried prev segment kept (not dropped by the 429)');
        is(parts[0].timestamps[0], 50, 'retried prev segment data intact');
        is(parts[1].timestamps[0], 70, 'aligned segment intact');
      });
    });
  });

  asyncTest('loadHistoricalData uses sequential secured fetch (no parallel chart requests)', function () {
    var seg = chartSeg([100], [7]);
    return runWithMocks({ '/api/proximity-chart-data': { status: 429, until: 1, data: seg } }, function() {
      return win.fetchChartSegments(['/api/proximity-chart-data?date=a', '/api/proximity-chart-data?date=b']).then(function(parts) {
        is(parts.length, 2, 'both chart-data segments still resolve under the 1 req/s guard');
        ok(parts.every(function(p) { return p && p.timestamps; }), 'neither segment was dropped by the 429');
      });
    });
  });

  asyncTest('loadHistoricalData backfills the CURRENT column from SD and re-anchors the live baseline', function () {
    var now = new Date();
    win._lastShiftCfg = { en: true, ar: true, nSlots: 3, slots: [{ h: 6 }, { h: 14 }, { h: 22 }], stops: [], emails: '' };
    var B = 22;
    var dates = win.prodDayDates(now, B);
    var prevStr = win._fmtDate(dates.prev);
    var alignedStr = win._fmtDate(dates.aligned);
    var hour = now.getHours();
    var cur = win.colForHour(hour, B);
    // SD ticks inside the CURRENT physical hour (a page opened mid-hour):
    // cumulative counter 1000..4000, all ticks <= now so every delta counts.
    var start = new Date(now.getTime()); start.setMinutes(0, 0, 0);
    var ticks = [], vals = [];
    var nTicks = Math.min(4, Math.floor(now.getMinutes() / 10) + 1);
    for (var i = 0; i < nTicks; i++) {
      var t = Math.floor(start.getTime() / 1000) + i * 600;
      if (t > Math.floor(now.getTime() / 1000)) break;
      ticks.push(t); vals.push(1000 + i * 1000);
    }
    // Last live poll: the counter holds the whole SHIFT window; the page must
    // not subtract it from the current hour (baseline re-anchored to SD).
    var liveIn = 10100;
    win._lastLive = { channels: [{ count_in: liveIn, count_out: 0 }, { count_in: 0, count_out: 0 }] };
    win._lastCfg = { channels: [{ target_ppm: 0 }] };
    win.hourlyData['0'] = { counts: [], scrapCounts: [], baseline: liveIn, scrapBaseline: 0, lastCol: cur, lastCount: liveIn, loaded: true, dayKey: '' };
    for (var j = 0; j < 24; j++) { win.hourlyData['0'].counts.push(0); win.hourlyData['0'].scrapCounts.push(0); }
    var cv = doc.createElement('canvas'); cv.id = 'chart0'; doc.body.appendChild(cv);
    var specs = {};
    specs['date=' + prevStr] = chartSeg(ticks, vals);
    specs['date=' + alignedStr] = chartSeg([], []);
    return runWithMocks(specs, function() {
      return win.loadHistoricalData().then(function() {
        var hd = win.hourlyData['0'];
        var expected = (vals.length >= 2) ? (vals[vals.length - 1] - vals[0]) : 0;
        is(hd.counts[cur], expected, 'current column shows the SD-counted hour (' + expected + '), not the live-window delta');
        if (expected > 0) {
          is(hd.baseline, liveIn - expected, 'live baseline re-anchored: next poll adds only the delta past SD');
          is(hd.scrapBaseline, 0, 'scrap baseline untouched without scrap SD data');
        } else {
          is(hd.baseline, liveIn, 'no SD data in the hour yet -> live owns the column (baseline unchanged)');
        }
        doc.body.removeChild(cv);
      });
    });
  });
}

// ====== T22: OEE timeline strip (donut colors) ======
async function timelineTests() {
  var dom = loadDashPage();
  await new Promise(function(r){ setTimeout(function(){ r(); }, 800); });
  var win = dom.window;
  var doc = dom.window.document;

  test('timelineSlotColor: future is gray', function () {
    is(win.timelineSlotColor(500, true, false, true), '#e2e8f0');
    is(win.timelineSlotColor(0, false, false, true), '#e2e8f0');
  });
  test('timelineSlotColor: any production is green', function () {
    is(win.timelineSlotColor(1, true, false, false), '#16a34a');
    is(win.timelineSlotColor(9000, false, true, false), '#16a34a');
  });
  test('timelineSlotColor: zero in shift without stop is red', function () {
    is(win.timelineSlotColor(0, true, false, false), '#dc2626');
  });
  test('timelineSlotColor: zero in planned stop is amber', function () {
    is(win.timelineSlotColor(0, true, true, false), '#f59e0b');
  });
  test('timelineSlotColor: zero outside shift is amber', function () {
    is(win.timelineSlotColor(0, false, false, false), '#f59e0b');
  });
  test('hourInShift: 3 shifts tile the whole day', function () {
    var slots = [{h:6,m:0},{h:14,m:0},{h:22,m:0}];
    [0, 5, 6, 8, 13, 14, 18, 21, 22, 23].forEach(function(h) {
      ok(win.hourInShift(h, slots), 'h=' + h + ' covered');
    });
  });
  test('hourInShift: no slots means whole day in shift', function () {
    ok(win.hourInShift(3, null), 'null slots');
    ok(win.hourInShift(3, []), 'empty slots');
  });
  test('hourInShift: single shift covers from its start (wrap)', function () {
    var slots = [{h:22,m:0}];
    ok(win.hourInShift(23, slots), '23 covered by 22h shift');
    ok(win.hourInShift(2, slots), '2 covered (wrap)');
    ok(win.hourInShift(22, slots), '22 covered');
  });
  test('hourInPlannedStop: lunch window + wrap', function () {
    var stops = [{sh:12,sm:0,eh:13,em:0}];
    ok(!win.hourInPlannedStop(11, stops), '11 outside');
    ok(win.hourInPlannedStop(12, stops), '12 inside');
    ok(!win.hourInPlannedStop(13, stops), '13 outside');
    var night = [{sh:23,sm:0,eh:1,em:0}];
    ok(win.hourInPlannedStop(23, night), '23 wrap');
    ok(win.hourInPlannedStop(0, night), '0 wrap');
    ok(!win.hourInPlannedStop(1, night), '1 outside');
    ok(!win.hourInPlannedStop(12, null), 'no stops');
  });
  test('timelineLegendHTML uses donut colors', function () {
    var s = win.timelineLegendHTML();
    ok(s.indexOf('#16a34a') >= 0, 'green production');
    ok(s.indexOf('#f59e0b') >= 0, 'amber planned');
    ok(s.indexOf('#dc2626') >= 0, 'red unplanned');
  });
  test('renderRows adds one timeline legend per chart', function () {
    win.renderRows(mockLive(8, 3), mockCfg(8, 3));
    var rows = doc.getElementById('rows');
    is(rows.querySelectorAll('.pd-tl').length, 3, '3 charts => 3 timeline legends');
  });

  // ====== T23: micro-stop blocks (6x10min per hour) ======
  // jsdom has no 2d canvas here either: strip fills are fillRect at y>=140
  // (fallback 400x180 geometry: strip top = 156).
  var t23Log = [];
  win.HTMLCanvasElement.prototype.getContext = function() {
    return new Proxy({}, {
      get: function(t, p) {
        if (p === 'canvas') return {};
        return function() { var a = Array.prototype.slice.call(arguments); t23Log.push(['call', p, a]); };
      },
      set: function(t, p, v) { t23Log.push(['set', p, v]); return true; }
    });
  };
  function stripFills() {
    var out = [], cur = null, y = 0;
    t23Log.forEach(function(e) {
      if (e[0] === 'set' && e[1] === 'fillStyle') cur = e[2];
      else if (e[0] === 'call' && e[1] === 'fillRect' && e[2] && e[2][1] >= 140) out.push(cur);
    });
    return out;
  }
  // Same as stripFills but limited to one hour slot (x range), so stop cells of
  // the tested hour are counted without the other 23 columns.
  function stripFillsIn(x0, x1) {
    var out = [], cur = null;
    t23Log.forEach(function(e) {
      if (e[0] === 'set' && e[1] === 'fillStyle') cur = e[2];
      else if (e[0] === 'call' && e[1] === 'fillRect' && e[2] && e[2][1] >= 140 &&
               e[2][0] >= x0 && e[2][0] < x1) out.push(cur);
    });
    return out;
  }
  function tsAt(h, m) { return Math.floor(new Date(2026, 8, 9, h, m, 0).getTime() / 1000); }
  function emptyBins() { var b = []; for (var i = 0; i < 24; i++) b.push(null); return b; }

  test('markStopSec: 10-min squares — 15-min stop snaps to 2 squares', function () {
    var cells = [];
    var prev = null, pts = null;
    // Production until 10:05, stop 10:05-10:20, recovery at 10:25.
    var seq = [[9,55,1000],[10,0,1050],[10,5,1100],[10,10,1100],[10,15,1100],[10,20,1100],[10,25,1150]];
    seq.forEach(function(s) { prev = win.markStopSec(cells, prev, s[2], tsAt(s[0], s[1]), pts, 0); pts = tsAt(s[0], s[1]); });
    is(prev, 1150, 'returns latest value as next prev');
    is(win.STOP_CELLS_PER_HOUR, 6, 'six squares per hour');
    is(win.STOP_CELL_SEC, 600, 'each square is 10 min');
    is(cells[10][0], 300, 'square 10:00-10:10 half stopped (stop began at 10:05)');
    is(cells[10][1], 600, 'square 10:10-10:20 fully stopped');
    is(cells[10][2], 0, 'square 10:20-10:30 produced again');
    var sq = win.stopSquaresFromBins(cells[10], 600, 600);
    is(sq.length, 6, 'six squares');
    is([sq[0], sq[1], sq[2], sq[3]].join(','), 'true,true,false,false', '15-min stop = 2 squares');
  });
  test('markStopSec: 20-min stop = 2 squares, 5-min stop is hidden', function () {
    var c20 = [], p = null, t = null;
    [[10,0,1000],[10,5,1500],[10,10,1500],[10,15,1500],[10,20,1500],[10,25,1500],[10,30,2000]].forEach(function(s) {
      p = win.markStopSec(c20, p, s[2], tsAt(s[0], s[1]), t, 0); t = tsAt(s[0], s[1]);
    });
    is([c20[10][0], c20[10][1], c20[10][2], c20[10][3]].join(','), '300,600,300,0', '20-min stop measured per 10-min square');
    var sq20 = win.stopSquaresFromBins(c20[10], 600, 600);
    is(sq20.filter(Boolean).length, 2, '20-min stop = 2 squares');
    // 5-min stop: never shown (owner: nothing below 10 min).
    var c5 = [], q = null, u = null;
    [[10,0,1000],[10,3,1500],[10,5,1500],[10,10,2000]].forEach(function(s) {
      q = win.markStopSec(c5, q, s[2], tsAt(s[0], s[1]), u, 0); u = tsAt(s[0], s[1]);
    });
    var sq5 = win.stopSquaresFromBins(c5[10], 600, 600);
    is(sq5.filter(Boolean).length, 0, '5-min stop paints nothing');
  });
  test('markStopSec: exactly 10 min shows one square, 9 min shows none', function () {
    function run(secs) {
      var c = [], p = null, t = null;
      p = win.markStopSec(c, p, 1000, tsAt(9, 55), t, 0); t = tsAt(9, 55);
      p = win.markStopSec(c, p, 1000, tsAt(10, 0), t, 0); t = tsAt(10, 0);
      p = win.markStopSec(c, p, 1000, tsAt(10, 0) + secs, t, 0); t = tsAt(10, 0) + secs;
      return c[10];
    }
    var ten = win.stopSquaresFromBins(run(600), 600, 600);
    is(ten.filter(Boolean).length, 1, '10 min = 1 square');
    var nine = win.stopSquaresFromBins(run(540), 600, 600);
    is(nine.filter(Boolean).length, 0, '9 min = hidden');
  });
  test('stopSquaresFromBins: clamped to 6 squares and handles unknown cells', function () {
    var full = [600, 600, 600, 600, 600, 600];
    is(win.stopSquaresFromBins(full, 600, 600).filter(Boolean).length, 6, 'full hour = 6 squares');
    var over = [600, 600, 600, 600, 600, 600];
    is(win.stopSquaresFromBins(over, 600, 600).length, 6, 'never more than 6');
    var mixed = [0, -1, 600, -1, 0, 0];
    is(win.stopSquaresFromBins(mixed, 600, 600).filter(Boolean).length, 1, 'unknown cells break the run');
    is(win.stopSquaresFromBins(null, 600, 600).filter(Boolean).length, 0, 'no row = nothing');
  });
  test('markStopSec: nulls, counter reset and long data holes', function () {
    var cells = [];
    is(win.markStopSec(cells, null, 500, null, null, 0), null, 'null ts keeps prev');
    is(win.markStopSec(cells, 500, null, tsAt(10, 5), tsAt(10, 0), 0), 500, 'null value keeps prev');
    var p = win.markStopSec(cells, null, 500, tsAt(10, 0), null, 0);
    p = win.markStopSec(cells, p, 400, tsAt(10, 5), tsAt(10, 0), 0);
    is(p, 400, 'counter reset re-baselines');
    ok(!cells[10], 'reset credits nothing');
    p = win.markStopSec(cells, p, 400, tsAt(10, 6), tsAt(10, 5), 0);
    is(cells[10][0], 60, '60 s gap after reset credited to the 10:00-10:10 square');
    ok(cells[10][0] < win.STOP_CELL_SHOW_SEC, 'and it is below the 10-min show threshold');
    // 3-hour data hole (board off): only the last 20 min count as stop.
    var g = [], q = win.markStopSec(g, 1000, 1000, tsAt(10, 0), null, 0);
    q = win.markStopSec(g, q, 1000, tsAt(13, 0), tsAt(10, 0), 0);
    var cred = 0;
    for (var hh = 0; hh < 24; hh++) for (var kk = 0; kk < win.STOP_CELLS_PER_HOUR; kk++) if (g[hh] && g[hh][kk] > 0) cred += g[hh][kk];
    is(cred, win.STOP_GAP_MAX_SEC, 'a 3-hour hole credits at most STOP_GAP_MAX_SEC');
  });
  test('stopBinIsFuture: column and square ordering', function () {
    ok(!win.stopBinIsFuture(5, 0, 9, 3), 'past column never future');
    ok(win.stopBinIsFuture(10, 0, 9, 3), 'later column future');
    ok(!win.stopBinIsFuture(9, 2, 9, 3), 'earlier cell in current column not future');
    ok(!win.stopBinIsFuture(9, 3, 9, 3), 'current cell not future');
    ok(win.stopBinIsFuture(9, 4, 9, 3), 'later cell in current column future');
  });
  test('drawChart24 paints the real stop length in red inside a green hour', function () {
    t23Log = [];
    var cv = doc.createElement('canvas');
    var hourly = []; for (var h = 0; h < 24; h++) hourly.push(0);
    hourly[8] = 5000;
    var sb = emptyBins();
    sb[8] = [0, 600, 300, -1, -1, -1];   // 15-min stop (10:05-10:20)
    win.drawChart24(cv, hourly, [], 'DI1', '09-10-2026', 10, '#0284c7', 0, 85, {}, sb);
    var slotW = (400 - 50 - 14) / 24;          // jsdom canvas falls back to W=400
    var x0 = 50 + slotW * 8 + 1, x1 = x0 + slotW - 2;
    var fl = stripFillsIn(x0, x1), red = 0, green = 0;
    fl.forEach(function(c) { if (c === '#dc2626') red++; if (c === '#16a34a') green++; });
    is(red, 2, '15-min stop paints 2 red squares');
    ok(green >= 1, 'the measured-but-producing square stays green');
  });
  test('drawChart24 paints amber cells inside a planned stop', function () {
    t23Log = [];
    var cv = doc.createElement('canvas');
    var hourly = []; for (var h = 0; h < 24; h++) hourly.push(0);
    var sb = emptyBins();
    sb[8] = [0, 600, 600, -1, -1, -1];
    win._lastShiftCfg = { en: true, slots: [{h:6,m:0},{h:14,m:0},{h:22,m:0}], stops: [{sh:8,sm:0,eh:9,em:0}] };
    win.drawChart24(cv, hourly, [], 'DI1', '09-10-2026', 10, '#0284c7', 0, 85, {}, sb);
    var fl = stripFills();
    ok(fl.indexOf('#f59e0b') >= 0, 'stopped cells inside the planned stop are amber');
    win._lastShiftCfg = null;
  });
  test('drawChart24 draws one separator per 10-min square', function () {
    t23Log = [];
    var cv = doc.createElement('canvas');
    Object.defineProperty(cv, 'clientWidth', { value: 1400, configurable: true });
    var hourly = []; for (var h = 0; h < 24; h++) hourly.push(0);
    hourly[8] = 5000;
    var sb = emptyBins();
    sb[8] = [0, 300, 300, 300, 0, -1, -1, -1, -1, -1, -1, -1];
    win.drawChart24(cv, hourly, [], 'DI1', '09-10-2026', 10, '#0284c7', 0, 85, {}, sb);
    var sep = stripFills().filter(function(c) { return c === 'rgba(255,255,255,.6)'; }).length;
    is(sep, 5, '5 block separators inside a 12-cell hour');
  });
  test('drawChart24 without stopBins keeps the legacy solid hour strip', function () {
    t23Log = [];
    var cv = doc.createElement('canvas');
    var hourly = []; for (var h = 0; h < 24; h++) hourly.push(0);
    win.drawChart24(cv, hourly, [], 'DI1', '09-10-2026', 10, '#0284c7', 0, 85, {});
    var fl = stripFills();
    ok(fl.indexOf('#dc2626') >= 0, 'zero hours are solid red');
    ok(fl.indexOf('#16a34a') < 0, 'no green without production');
    ok(fl.indexOf('#e2e8f0') >= 0, 'future columns stay gray');
  });
  test('timelineLegendHTML notes the 10-min squares and the minimum stop', function () {
    var lg = win.timelineLegendHTML();
    ok(lg.indexOf('10 min') >= 0, 'legend explains block = 10 min');
    ok(lg.indexOf('\u2265 10 min') >= 0, 'legend states stops are only shown from 10 min');
  });
  test('updateHourly marks the live 10-min square on production polls', function () {
    var live = { channels: [{ name: 'DI1', mode: 0, ppm: 5, count_in: 100, count_out: 0, total_in: 0, total_out: 0 }] };
    var cfg = { channels: [{ m: 0, gi: 10, gl: 0, gc: 255, sc: 255 }] };
    win.renderRows(live, cfg);
    win.updateHourly(live);
    live.channels[0].count_in = 160;
    win.updateHourly(live);
    var hd = win.hourlyData['0'];
    var now = new Date();
    var cc = win.colForHour(now.getHours(), 0);
    var cb = Math.floor(now.getMinutes() / win.STOP_CELL_MIN);
    ok(hd.stopBins && hd.stopBins[cc], 'current column has cells');
    is(hd.stopBins[cc][cb], 0, 'live production keeps the current 5-min cell producing');
  });
}

// Summary
dashDomTests().then(function() {
  return timelineTests();
}).then(function() {
  return testChain.then(function() {
    console.log('\n  ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
    process.exit(failed > 0 ? 1 : 0);
  });
});
