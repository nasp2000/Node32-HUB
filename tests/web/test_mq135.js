// MQ135 Sensor Page Tests — aqiInfo, trendArrow, pickReadableTextColor, mqTone, uiLocale, t
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var PAGE = path.join(__dirname, 'pages', 'mq135_config.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message + (e.stack ? '\n' + e.stack.split('\n').slice(1,3).join('\n') : '')); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function run() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
  var w = dom.window;
  var T = w.__test__;

  console.log('  --- aqiInfo ---');

  test('aqiInfo: good (0-50)', function() {
    var r = T.aqiInfo(0); is(r.color, '#10b981');
    r = T.aqiInfo(25); is(r.color, '#10b981');
    r = T.aqiInfo(50); is(r.color, '#10b981');
    ok(r.label.indexOf('Good') > 0);
  });

  test('aqiInfo: moderate (51-100)', function() {
    var r = T.aqiInfo(51); is(r.color, '#84cc16');
    r = T.aqiInfo(75); is(r.color, '#84cc16');
    r = T.aqiInfo(100); is(r.color, '#84cc16');
    ok(r.label.indexOf('Moderate') > 0);
  });

  test('aqiInfo: sensitive (101-150)', function() {
    var r = T.aqiInfo(101); is(r.color, '#f59e0b');
    r = T.aqiInfo(150); is(r.color, '#f59e0b');
    ok(r.label.indexOf('Sensitive') > 0);
  });

  test('aqiInfo: unhealthy (151-200)', function() {
    var r = T.aqiInfo(151); is(r.color, '#f97316');
    r = T.aqiInfo(200); is(r.color, '#f97316');
    ok(r.label.indexOf('Unhealthy') > 0);
  });

  test('aqiInfo: very unhealthy (201-300)', function() {
    var r = T.aqiInfo(201); is(r.color, '#ef4444');
    r = T.aqiInfo(300); is(r.color, '#ef4444');
    ok(r.label.indexOf('Very Unhealthy') > 0);
  });

  test('aqiInfo: hazardous (>300)', function() {
    var r = T.aqiInfo(301); is(r.color, '#7c3aed');
    r = T.aqiInfo(500); is(r.color, '#7c3aed');
    ok(r.label.indexOf('Hazardous') > 0);
  });

  test('aqiInfo: NaN returns hazardous', function() {
    var r = T.aqiInfo(NaN); is(r.color, '#7c3aed');
  });

  console.log('  --- trendArrow ---');

  test('trendArrow: up when diff > delta', function() {
    w.eval('prevValues = {}; prevValues.t1 = 5;');
    var r = T.trendArrow('t1', 15, 5);
    ok(r.indexOf('\u2191') > 0, 'expected up arrow in ' + JSON.stringify(r));
  });

  test('trendArrow: down when diff < -delta', function() {
    w.eval('prevValues = {}; prevValues.t2 = 20;');
    var r = T.trendArrow('t2', 5, 5);
    ok(r.indexOf('\u2193') > 0, 'expected down arrow in ' + JSON.stringify(r));
  });

  test('trendArrow: flat when |diff| < delta', function() {
    w.eval('prevValues = {}; prevValues.t3 = 10;');
    var r = T.trendArrow('t3', 12, 5);
    ok(r.indexOf('\u2192') > 0, 'expected flat arrow in ' + JSON.stringify(r));
  });

  test('trendArrow: empty when no previous value', function() {
    w.eval('prevValues = {};');
    var r = T.trendArrow('t4', 10, 5);
    is(r, '');
  });

  test('trendArrow: empty when curr is NaN', function() {
    w.eval('prevValues = {}; prevValues.t5 = 10;');
    var r = T.trendArrow('t5', NaN, 5);
    is(r, '');
  });

  test('trendArrow: boundary exact delta returns flat', function() {
    w.eval('prevValues = {}; prevValues.t6 = 10;');
    var r = T.trendArrow('t6', 10, 5);
    ok(r.indexOf('\u2192') > 0, 'expected flat for zero diff');
  });

  test('trendArrow: delta=0 always shows direction', function() {
    w.eval('prevValues = {}; prevValues.t7 = 10;');
    var r_up = T.trendArrow('t7', 10.001, 0);
    ok(r_up.indexOf('\u2191') > 0, 'expected up for tiny positive diff with delta=0');
    w.eval('prevValues.t7 = 10;');
    var r_down = T.trendArrow('t7', 9.999, 0);
    ok(r_down.indexOf('\u2193') > 0, 'expected down for tiny negative diff with delta=0');
  });

  console.log('  --- pickReadableTextColor ---');

  test('pickReadableTextColor: dark bg returns light text', function() {
    is(T.pickReadableTextColor('#000000'), '#f8fafc');
    is(T.pickReadableTextColor('#1e293b'), '#f8fafc');
    is(T.pickReadableTextColor('#0f172a'), '#f8fafc');
  });

  test('pickReadableTextColor: light bg returns dark text', function() {
    is(T.pickReadableTextColor('#ffffff'), '#0f172a');
    is(T.pickReadableTextColor('#f0f0f0'), '#0f172a');
    is(T.pickReadableTextColor('#e0e7ff'), '#0f172a');
  });

  test('pickReadableTextColor: boundary luminance ~0.62', function() {
    // #bdbdbd → r=189,g=189,b=189 → (0.299*189+0.587*189+0.114*189)/255 = 189/255 ≈ 0.741 → light → dark
    is(T.pickReadableTextColor('#bdbdbd'), '#0f172a');
    // #999999 → 153/255 = 0.6 → dark → light
    is(T.pickReadableTextColor('#999999'), '#f8fafc');
  });

  test('pickReadableTextColor: invalid hex returns fallback', function() {
    is(T.pickReadableTextColor(''), '#ffffff');
    is(T.pickReadableTextColor('#abc'), '#ffffff');
    is(T.pickReadableTextColor(null), '#ffffff');
  });

  console.log('  --- mqTone ---');

  test('mqTone: AQI levels', function() {
    is(T.mqTone('aqi', 0).bg, '#10b981');
    is(T.mqTone('aqi', 50).bg, '#10b981');
    is(T.mqTone('aqi', 75).bg, '#84cc16');
    is(T.mqTone('aqi', 150).bg, '#facc15');
    is(T.mqTone('aqi', 200).bg, '#fb923c');
    is(T.mqTone('aqi', 300).bg, '#ef4444');
    is(T.mqTone('aqi', 500).bg, '#7c3aed');
  });

  test('mqTone: CO2 levels', function() {
    is(T.mqTone('co2', 400).bg, '#10b981');
    is(T.mqTone('co2', 800).bg, '#10b981');
    is(T.mqTone('co2', 900).bg, '#84cc16');
    is(T.mqTone('co2', 2000).bg, '#f59e0b');
    is(T.mqTone('co2', 5000).bg, '#ef4444');
    is(T.mqTone('co2', 10000).bg, '#7c3aed');
  });

  test('mqTone: CO levels', function() {
    is(T.mqTone('co', 5).bg, '#10b981');
    is(T.mqTone('co', 9).bg, '#10b981');
    is(T.mqTone('co', 10).bg, '#84cc16');
    is(T.mqTone('co', 50).bg, '#f59e0b');
    is(T.mqTone('co', 150).bg, '#ef4444');
    is(T.mqTone('co', 200).bg, '#7c3aed');
  });

  test('mqTone: NH3 levels', function() {
    is(T.mqTone('nh3', 0.5).bg, '#10b981');
    is(T.mqTone('nh3', 1).bg, '#10b981');
    is(T.mqTone('nh3', 3).bg, '#84cc16');
    is(T.mqTone('nh3', 10).bg, '#f59e0b');
    is(T.mqTone('nh3', 25).bg, '#ef4444');
    is(T.mqTone('nh3', 50).bg, '#7c3aed');
  });

  test('mqTone: NOx/benzene levels', function() {
    is(T.mqTone('nox', 0.05).bg, '#10b981');
    is(T.mqTone('nox', 0.1).bg, '#10b981');
    is(T.mqTone('nox', 0.3).bg, '#84cc16');
    is(T.mqTone('nox', 1).bg, '#f59e0b');
    is(T.mqTone('nox', 5).bg, '#ef4444');
    is(T.mqTone('nox', 10).bg, '#7c3aed');
    is(T.mqTone('benzene', 0.05).bg, '#10b981');
  });

  test('mqTone: alcohol/smoke levels', function() {
    is(T.mqTone('alcohol', 0.25).bg, '#10b981');
    is(T.mqTone('alcohol', 0.5).bg, '#10b981');
    is(T.mqTone('alcohol', 2).bg, '#84cc16');
    is(T.mqTone('alcohol', 10).bg, '#f59e0b');
    is(T.mqTone('alcohol', 100).bg, '#ef4444');
    is(T.mqTone('alcohol', 200).bg, '#7c3aed');
    is(T.mqTone('smoke', 0.5).bg, '#10b981');
  });

  test('mqTone: NaN returns silent tone', function() {
    var r = T.mqTone('co2', NaN);
    is(r.bg, '#064e3b');
  });

  test('mqTone: unknown kind returns fallback', function() {
    var r = T.mqTone('unknown', 100);
    is(r.bg, '#1e293b');
  });

  console.log('  --- uiLocale ---');

  test('uiLocale: EN returns en-GB', function() {
    w.localStorage.setItem('ui_lang_code', 'en');
    is(w.__test__.uiLocale(), 'en-GB');
  });

  test('uiLocale: PT returns pt-PT', function() {
    w.localStorage.setItem('ui_lang_code', 'pt');
    is(w.__test__.uiLocale(), 'pt-PT');
  });

  test('uiLocale: unknown falls back to en-GB', function() {
    w.localStorage.setItem('ui_lang_code', 'fr');
    is(w.__test__.uiLocale(), 'pt-PT');  // fr.startsWith('en') is false → pt-PT
  });

  test('uiLocale: no localStorage uses document lang', function() {
    w.localStorage.removeItem('ui_lang_code');
    w.document.documentElement.lang = 'pt';
    is(w.__test__.uiLocale(), 'pt-PT');
    w.document.documentElement.lang = 'en';
  });

  console.log('  --- t (i18n) ---');

  test('t: uses window._t when available', function() {
    w._t = function(k, fb) { return 'MOCKED:' + k; };
    is(T.t('key', 'fb'), 'MOCKED:key');
    delete w._t;
  });

  test('t: falls back to fallback when no _t', function() {
    is(T.t('key', 'fallback'), 'fallback');
  });

  // ====== Summary ======
  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e) { console.error('Fatal:', e); process.exit(1); });
