// Sensor Config Page Tests — envTone (standalone reimpl), updateStatistics, trendArrow, showStatus, t, uiLocale
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var PAGE = path.join(__dirname, 'pages', 'sensor_config.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message + (e.stack ? '\n' + e.stack.split('\n').slice(1,3).join('\n') : '')); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// envTone — standalone reimplementation (nested inside updateReadingValues in source)
// Matches the logic in page_sensor_config_asset.h lines 494-538
function envTone(kind, value) {
  if (isNaN(value)) return { bg: '#064e3b', border: '#065f46' };
  if (kind === 'temp_aht20' || kind === 'temp_bmp280' || kind === 'temp_avg') {
    if (value >= 18 && value <= 26) return { bg: '#10b981', border: '#059669' };
    if ((value >= 16 && value < 18) || (value > 26 && value <= 29)) return { bg: '#84cc16', border: '#65a30d' };
    if ((value >= 12 && value < 16) || (value > 29 && value <= 32)) return { bg: '#f59e0b', border: '#d97706' };
    if ((value >= 5 && value < 12) || (value > 32 && value <= 38)) return { bg: '#f97316', border: '#ea580c' };
    return { bg: '#ef4444', border: '#dc2626' };
  }
  if (kind === 'humidity') {
    if (value >= 40 && value <= 60) return { bg: '#10b981', border: '#059669' };
    if ((value >= 30 && value < 40) || (value > 60 && value <= 70)) return { bg: '#84cc16', border: '#65a30d' };
    if ((value >= 20 && value < 30) || (value > 70 && value <= 80)) return { bg: '#f59e0b', border: '#d97706' };
    return { bg: '#ef4444', border: '#dc2626' };
  }
  if (kind === 'pressure_hpa') {
    if (value >= 980 && value <= 1030) return { bg: '#10b981', border: '#059669' };
    if ((value >= 965 && value < 980) || (value > 1030 && value <= 1045)) return { bg: '#84cc16', border: '#65a30d' };
    if ((value >= 950 && value < 965) || (value > 1045 && value <= 1060)) return { bg: '#f59e0b', border: '#d97706' };
    return { bg: '#ef4444', border: '#dc2626' };
  }
  if (kind === 'pressure_mmhg') {
    if (value >= 735 && value <= 772) return { bg: '#10b981', border: '#059669' };
    if ((value >= 724 && value < 735) || (value > 772 && value <= 783)) return { bg: '#84cc16', border: '#65a30d' };
    if ((value >= 712 && value < 724) || (value > 783 && value <= 795)) return { bg: '#f59e0b', border: '#d97706' };
    return { bg: '#ef4444', border: '#dc2626' };
  }
  if (kind === 'dew_point') {
    if (value >= 8 && value <= 16) return { bg: '#10b981', border: '#059669' };
    if ((value >= 5 && value < 8) || (value > 16 && value <= 20)) return { bg: '#84cc16', border: '#65a30d' };
    if ((value >= 0 && value < 5) || (value > 20 && value <= 24)) return { bg: '#f59e0b', border: '#d97706' };
    return { bg: '#ef4444', border: '#dc2626' };
  }
  if (kind === 'heat_index') {
    if (value >= 18 && value <= 27) return { bg: '#10b981', border: '#059669' };
    if ((value >= 16 && value < 18) || (value > 27 && value <= 30)) return { bg: '#84cc16', border: '#65a30d' };
    if ((value >= 12 && value < 16) || (value > 30 && value <= 34)) return { bg: '#f59e0b', border: '#d97706' };
    if (value > 34) return { bg: '#ef4444', border: '#dc2626' };
    return { bg: '#60a5fa', border: '#2563eb' };
  }
  if (kind === 'altitude') {
    return { bg: '#1e40af', border: '#2563eb' };
  }
  return { bg: '#1e293b', border: '#475569' };
}

function run() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
  var w = dom.window;
  var T = w.__test__;
  var doc = w.document;

  console.log('  --- envTone (standalone reimpl) ---');

  test('envTone: NaN returns silent tone', function() {
    var r = envTone('temperature', NaN);
    is(r.bg, '#064e3b');
  });

  test('envTone: temperature ranges', function() {
    ['temp_aht20', 'temp_bmp280', 'temp_avg'].forEach(function(k) {
      is(envTone(k, 22).bg, '#10b981', k + ' optimal');
      is(envTone(k, 17).bg, '#84cc16', k + ' cool');
      is(envTone(k, 15).bg, '#f59e0b', k + ' cold');
      is(envTone(k, 10).bg, '#f97316', k + ' very cold');
      is(envTone(k, 0).bg, '#ef4444', k + ' extreme');
      is(envTone(k, 27).bg, '#84cc16', k + ' warm');
      is(envTone(k, 31).bg, '#f59e0b', k + ' hot');
      is(envTone(k, 35).bg, '#f97316', k + ' very hot');
      is(envTone(k, 40).bg, '#ef4444', k + ' extreme hot');
    });
  });

  test('envTone: temperature boundary values', function() {
    is(envTone('temp_avg', 18).bg, '#10b981');
    is(envTone('temp_avg', 26).bg, '#10b981');
    is(envTone('temp_avg', 16).bg, '#84cc16');
    is(envTone('temp_avg', 29).bg, '#84cc16');
    is(envTone('temp_avg', 12).bg, '#f59e0b');
    is(envTone('temp_avg', 32).bg, '#f59e0b');
    is(envTone('temp_avg', 5).bg, '#f97316');
    is(envTone('temp_avg', 38).bg, '#f97316');
    is(envTone('temp_avg', 4).bg, '#ef4444');
    is(envTone('temp_avg', 39).bg, '#ef4444');
  });

  test('envTone: humidity ranges', function() {
    is(envTone('humidity', 50).bg, '#10b981');
    is(envTone('humidity', 35).bg, '#84cc16');
    is(envTone('humidity', 25).bg, '#f59e0b');
    is(envTone('humidity', 15).bg, '#ef4444');
    is(envTone('humidity', 65).bg, '#84cc16');
    is(envTone('humidity', 75).bg, '#f59e0b');
    is(envTone('humidity', 85).bg, '#ef4444');
  });

  test('envTone: humidity boundary values', function() {
    is(envTone('humidity', 40).bg, '#10b981', '40% min optimal');
    is(envTone('humidity', 60).bg, '#10b981', '60% max optimal');
    is(envTone('humidity', 30).bg, '#84cc16', '30% min moderate');
    is(envTone('humidity', 70).bg, '#84cc16', '70% max moderate');
    is(envTone('humidity', 20).bg, '#f59e0b', '20% min caution');
    is(envTone('humidity', 80).bg, '#f59e0b', '80% max caution');
  });

  test('envTone: pressure hPa ranges', function() {
    is(envTone('pressure_hpa', 1000).bg, '#10b981');
    is(envTone('pressure_hpa', 970).bg, '#84cc16');
    is(envTone('pressure_hpa', 955).bg, '#f59e0b');
    is(envTone('pressure_hpa', 940).bg, '#ef4444');
    is(envTone('pressure_hpa', 1040).bg, '#84cc16');
    is(envTone('pressure_hpa', 1055).bg, '#f59e0b');
    is(envTone('pressure_hpa', 1070).bg, '#ef4444');
  });

  test('envTone: pressure mmHg ranges', function() {
    is(envTone('pressure_mmhg', 750).bg, '#10b981');
    is(envTone('pressure_mmhg', 728).bg, '#84cc16');
    is(envTone('pressure_mmhg', 718).bg, '#f59e0b');
    is(envTone('pressure_mmhg', 700).bg, '#ef4444');
    is(envTone('pressure_mmhg', 780).bg, '#84cc16');
    is(envTone('pressure_mmhg', 790).bg, '#f59e0b');
    is(envTone('pressure_mmhg', 800).bg, '#ef4444');
  });

  test('envTone: dew point ranges', function() {
    is(envTone('dew_point', 12).bg, '#10b981');
    is(envTone('dew_point', 6).bg, '#84cc16');
    is(envTone('dew_point', 2).bg, '#f59e0b');
    is(envTone('dew_point', -1).bg, '#ef4444');
    is(envTone('dew_point', 18).bg, '#84cc16');
    is(envTone('dew_point', 22).bg, '#f59e0b');
    is(envTone('dew_point', 25).bg, '#ef4444');
  });

  test('envTone: heat index ranges', function() {
    is(envTone('heat_index', 22).bg, '#10b981');
    is(envTone('heat_index', 17).bg, '#84cc16');
    is(envTone('heat_index', 14).bg, '#f59e0b');
    is(envTone('heat_index', 10).bg, '#60a5fa');
    is(envTone('heat_index', 28).bg, '#84cc16');
    is(envTone('heat_index', 32).bg, '#f59e0b');
    is(envTone('heat_index', 35).bg, '#ef4444');
  });

  test('envTone: altitude always same color', function() {
    is(envTone('altitude', 0).bg, '#1e40af');
    is(envTone('altitude', 1000).bg, '#1e40af');
  });

  test('envTone: unknown kind returns fallback', function() {
    is(envTone('unknown', 50).bg, '#1e293b');
  });

  console.log('  --- trendArrow (via w.eval) ---');

  test('trendArrow: up when diff > delta', function() {
    w.eval('prevValues = {}; prevValues.t1 = 5;');
    var r = T.trendArrow('t1', 15, 5);
    ok(r.indexOf('\u2191') > 0);
  });

  test('trendArrow: down when diff < -delta', function() {
    w.eval('prevValues = {}; prevValues.t2 = 20;');
    var r = T.trendArrow('t2', 5, 5);
    ok(r.indexOf('\u2193') > 0);
  });

  test('trendArrow: flat when |diff| < delta', function() {
    w.eval('prevValues = {}; prevValues.t3 = 10;');
    var r = T.trendArrow('t3', 12, 5);
    ok(r.indexOf('\u2192') > 0);
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

  console.log('  --- updateStatistics ---');

  test('updateStatistics: renders full stats', function() {
    var stats = {
      window_hours: 24, window_minutes: 0,
      temperature: { min: 18.5, max: 25.3, avg: 21.8, count: 1440 },
      humidity: { min: 35, max: 65, avg: 50, count: 1440 },
      pressure: { min: 990.2, max: 1020.5, avg: 1005.3, count: 1440 }
    };
    T.updateStatistics(stats);
    is(doc.getElementById('stats_window').textContent, '24h 0m');
    is(doc.getElementById('stats_temp_min').textContent, '18.50');
    is(doc.getElementById('stats_temp_max').textContent, '25.30');
    is(doc.getElementById('stats_temp_avg').textContent, '21.80');
    is(doc.getElementById('stats_count').textContent, '1440');
    is(doc.getElementById('stats_hum_min').textContent, '35.00');
    is(doc.getElementById('stats_hum_max').textContent, '65.00');
    is(doc.getElementById('stats_hum_avg').textContent, '50.00');
    is(doc.getElementById('stats_press_min').textContent, '990.20');
    is(doc.getElementById('stats_press_max').textContent, '1020.50');
    is(doc.getElementById('stats_press_avg').textContent, '1005.30');
  });

  test('updateStatistics: empty stats shows dashes', function() {
    var stats = {
      window_hours: 0, window_minutes: 0,
      temperature: { count: 0 },
      humidity: { count: 0 },
      pressure: { count: 0 }
    };
    // Missing min/max/avg properties, only count=0
    T.updateStatistics(stats);
    is(doc.getElementById('stats_window').textContent, '0h 0m');
    is(doc.getElementById('stats_temp_min').textContent, '--');
    is(doc.getElementById('stats_temp_max').textContent, '--');
    is(doc.getElementById('stats_temp_avg').textContent, '--');
    is(doc.getElementById('stats_hum_min').textContent, '--');
    is(doc.getElementById('stats_hum_max').textContent, '--');
    is(doc.getElementById('stats_hum_avg').textContent, '--');
    is(doc.getElementById('stats_press_min').textContent, '--');
    is(doc.getElementById('stats_press_max').textContent, '--');
    is(doc.getElementById('stats_press_avg').textContent, '--');
  });

  test('updateStatistics: partial data only for temperature', function() {
    var stats = {
      window_hours: 6, window_minutes: 30,
      temperature: { min: 20.0, max: 22.0, avg: 21.0, count: 390 },
      humidity: { count: 0 },
      pressure: { count: 0 }
    };
    T.updateStatistics(stats);
    is(doc.getElementById('stats_window').textContent, '6h 30m');
    is(doc.getElementById('stats_temp_min').textContent, '20.00');
    is(doc.getElementById('stats_hum_min').textContent, '--');
    is(doc.getElementById('stats_press_min').textContent, '--');
  });

  console.log('  --- showStatus ---');

  test('showStatus: calls customAlert when available', function() {
    var called = false;
    w.customAlert = function(m) { called = true; is(m.indexOf('test msg') >= 0, true); };
    T.showStatus('test msg', 'success');
    ok(called, 'customAlert should be called');
    delete w.customAlert;
  });

  test('showStatus: falls back to DOM element without customAlert', function() {
    var el = doc.getElementById('status');
    el.style.display = 'none';
    var saved = w.customAlert;
    w.customAlert = 'not a function';
    T.showStatus('fallback msg', 'error');
    ok(el.style.display !== 'none', 'status element should be visible');
    ok(el.style.borderColor === 'rgb(239, 68, 68)' || el.style.border.indexOf('239, 68, 68') >= 0, 'error border color');
    w.customAlert = saved;
  });

  console.log('  --- t (i18n) ---');

  test('t: uses window._t when available', function() {
    w._t = function(k, fb) { return 'MOCKED:' + k; };
    is(T.t('key', 'fb'), 'MOCKED:key');
    delete w._t;
  });

  test('t: falls back to fb when no _t', function() {
    is(T.t('key', 'fallback'), 'fallback');
  });

  test('t: returns key when no _t and no fb', function() {
    is(T.t('key'), 'key');
  });

  console.log('  --- uiLocale ---');

  test('uiLocale: EN returns en-GB', function() {
    w.document.documentElement.lang = '';
    w.localStorage.setItem('ui_lang_code', 'en');
    is(T.uiLocale(), 'en-GB');
    w.document.documentElement.lang = 'en';
  });

  test('uiLocale: PT returns pt-PT', function() {
    w.document.documentElement.lang = '';
    w.localStorage.setItem('ui_lang_code', 'pt');
    is(T.uiLocale(), 'pt-PT');
    w.document.documentElement.lang = 'en';
  });

  test('uiLocale: uses document lang before localStorage', function() {
    w.document.documentElement.lang = 'pt';
    w.localStorage.setItem('ui_lang_code', 'en');
    is(T.uiLocale(), 'pt-PT');
    // sensor_config version: documentElement.lang || localStorage || 'en'
    // So documentElement.lang comes first
    w.document.documentElement.lang = 'en';
  });

  // ====== Summary ======
  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e) { console.error('Fatal:', e); process.exit(1); });
