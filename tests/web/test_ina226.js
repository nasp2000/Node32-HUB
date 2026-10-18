// INA226 Current/Power Monitor Page Tests — updateDisplay, showStatus, t, uiLocale
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var PAGE = path.join(__dirname, 'pages', 'ina226_asset.html');
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
  test('t: uses window._t', function() {
    w._t = function(k) { return 'TRANS'; };
    is(T.t('k', 'fb'), 'TRANS');
    delete w._t;
  });
  test('t: returns fallback', function() {
    is(T.t('k', 'fall'), 'fall');
  });

  console.log('  --- uiLocale ---');
  test('uiLocale: en', function() {
    w.document.documentElement.lang = 'en';
    w.localStorage.removeItem('ui_lang_code');
    is(T.uiLocale(), 'en-GB');
  });
  test('uiLocale: pt', function() {
    w.localStorage.setItem('ui_lang_code', 'pt');
    is(T.uiLocale(), 'pt-PT');
  });

  console.log('  --- updateDisplay ---');
  test('updateDisplay: bus voltage', function() {
    T.updateDisplay({ last_bus_v: 5.123 });
    is(w.document.getElementById('busVoltage').textContent, '5.123');
  });
  test('updateDisplay: current in A (unit=0)', function() {
    T.updateDisplay({ last_current_a: 1.2345, current_unit: 0 });
    is(w.document.getElementById('currentVal').textContent, '1.2345');
    is(w.document.getElementById('currentUnitLbl').textContent, 'A');
  });
  test('updateDisplay: current in mA (unit=1)', function() {
    T.updateDisplay({ last_current_a: 0.5, current_unit: 1 });
    is(w.document.getElementById('currentVal').textContent, (0.5 * 1000).toFixed(1));
    is(w.document.getElementById('currentUnitLbl').textContent, 'mA');
  });
  test('updateDisplay: power', function() {
    T.updateDisplay({ last_power_w: 12.345 });
    is(w.document.getElementById('powerVal').textContent, '12.345');
  });
  test('updateDisplay: shunt voltage (V -> mV * 1000)', function() {
    T.updateDisplay({ last_shunt_v: 0.075 });
    is(w.document.getElementById('shuntVoltage').textContent, '75.000');
  });
  test('updateDisplay: timestamp with valid time', function() {
    T.updateDisplay({ last_bus_v: 1, last_reading_time: 2000000 });
    var te = w.document.getElementById('lastReadTime');
    ok(te.textContent.indexOf('Last reading') >= 0 || te.textContent.length > 10);
  });
  test('updateDisplay: no reading_time', function() {
    T.updateDisplay({ last_bus_v: 1 });
    is(w.document.getElementById('lastReadTime').textContent, 'No reading');
  });
  test('updateDisplay: partial data (only bus voltage)', function() {
    w.document.getElementById('currentVal').textContent = 'OLD';
    w.document.getElementById('powerVal').textContent = 'OLD';
    w.document.getElementById('shuntVoltage').textContent = 'OLD';
    T.updateDisplay({ last_bus_v: 3.3 });
    is(w.document.getElementById('busVoltage').textContent, '3.300');
    is(w.document.getElementById('currentVal').textContent, 'OLD');
    is(w.document.getElementById('powerVal').textContent, 'OLD');
    is(w.document.getElementById('shuntVoltage').textContent, 'OLD');
  });

  console.log('  --- showStatus ---');
  test('showStatus: customAlert path', function() {
    var called = null;
    w.customAlert = function(m) { called = m; };
    T.showStatus('msg1', 'info');
    is(called, 'msg1');
    delete w.customAlert;
  });
  test('showStatus: DOM fallback', function() {
    var el = w.document.getElementById('status');
    el.style.display = 'none';
    T.showStatus('dom2', 'error');
    is(el.textContent, 'dom2');
    is(el.style.display, 'block');
  });

  var total = passed + failed;
  console.log('\n  INA226: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
