// HX711 Weight Sensor Page Tests — updateDisplay, showStatus, performTare, t, uiLocale
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var PAGE = path.join(__dirname, 'pages', 'hx711_asset.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function run() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
  var w = dom.window;
  var T = w.__test__;

  console.log('  --- t (i18n) ---');
  test('t: uses window._t when available', function() {
    w._t = function(k, fb) { return 'TRANS-' + k; };
    is(T.t('key', 'fb'), 'TRANS-key');
    delete w._t;
  });
  test('t: returns fallback when no window._t', function() {
    is(T.t('key', 'myFallback'), 'myFallback');
  });

  console.log('  --- uiLocale ---');
  test('uiLocale: en from lang attr', function() {
    w.document.documentElement.lang = 'en';
    w.localStorage.removeItem('ui_lang_code');
    is(T.uiLocale(), 'en-GB');
  });
  test('uiLocale: pt from localStorage', function() {
    w.localStorage.setItem('ui_lang_code', 'pt');
    is(T.uiLocale(), 'pt-PT');
  });

  console.log('  --- updateDisplay ---');
  test('updateDisplay: weight value and unit label', function() {
    T.updateDisplay({ last_weight: 123.456, weight_unit: 1 });
    var wv = w.document.getElementById('weightValue');
    var ul = w.document.getElementById('weightUnitLabel');
    is(wv.textContent, '123.456');
    is(ul.textContent, 'kg');
  });
  test('updateDisplay: weight unit 0=g, 2=lbs, 3=oz, 4=N', function() {
    var units = ['g','kg','lbs','oz','N'];
    for (var i = 0; i < 5; i++) {
      T.updateDisplay({ last_weight: 1, weight_unit: i });
      is(w.document.getElementById('weightUnitLabel').textContent, units[i]);
    }
  });
  test('updateDisplay: undefined weight_unit defaults to 0 (g)', function() {
    T.updateDisplay({ last_weight: 5 });
    is(w.document.getElementById('weightUnitLabel').textContent, 'g');
  });
  test('updateDisplay: null weight does not change display', function() {
    w.document.getElementById('weightValue').textContent = 'OLD';
    T.updateDisplay({ weight_unit: 0 });
    is(w.document.getElementById('weightValue').textContent, 'OLD');
  });
  test('updateDisplay: timestamp with valid reading_time', function() {
    T.updateDisplay({ last_weight: 1, last_reading_time: 1000000, weight_unit: 0 });
    var te = w.document.getElementById('lastReadTime');
    ok(te.textContent.indexOf('Last reading') >= 0 || te.textContent.indexOf('1970') >= 0 || te.textContent.indexOf('01') >= 0);
  });
  test('updateDisplay: no reading_time shows fallback', function() {
    T.updateDisplay({ last_weight: 1, last_reading_time: 0, weight_unit: 0 });
    is(w.document.getElementById('lastReadTime').textContent, 'No reading');
  });

  console.log('  --- showStatus ---');
  test('showStatus: uses customAlert when available', function() {
    var called = null;
    w.customAlert = function(m) { called = m; };
    T.showStatus('test msg', 'error');
    is(called, 'test msg');
    delete w.customAlert;
  });
  test('showStatus: uses DOM fallback', function() {
    var el = w.document.getElementById('status');
    el.style.display = 'none';
    T.showStatus('dom msg', 'success');
    is(el.textContent, 'dom msg');
    is(el.style.display, 'block');
  });

  console.log('  --- performTare ---');
  test('performTare: calls customConfirm with correct message', function() {
    var called = null;
    w.customConfirm = function(msg, cb) {
      called = msg; cb(true);
    };
    T.performTare();
    ok(called && called.indexOf('Remove') >= 0);
    delete w.customConfirm;
  });

  var total = passed + failed;
  console.log('\n  HX711: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
