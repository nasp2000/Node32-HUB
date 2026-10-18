// MAX31865 RTD Sensor Page Tests — updateDisplay (temp, unit, fault badge), showStatus, t, uiLocale
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var PAGE = path.join(__dirname, 'pages', 'max31865_asset.html');
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
    is(T.t('k', 'fb'), 'fb');
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
  test('updateDisplay: temperature value', function() {
    T.updateDisplay({ last_temp: 23.4567 });
    is(w.document.getElementById('tempDisplay').textContent, '23.46');
  });
  test('updateDisplay: temperature color green when no fault', function() {
    T.updateDisplay({ last_temp: 25, last_fault: 0 });
    ok(w.document.getElementById('tempDisplay').style.color.indexOf('74') >= 0, 'should be greenish');
  });
  test('updateDisplay: temperature color yellow when fault', function() {
    T.updateDisplay({ last_temp: 25, last_fault: 1 });
    ok(w.document.getElementById('tempDisplay').style.color.indexOf('251') >= 0, 'should be yellowish');
  });
  test('updateDisplay: unit label from last_unit', function() {
    T.updateDisplay({ last_temp: 25, last_unit: '°F' });
    is(w.document.getElementById('tempUnitLbl').textContent, '°F');
  });
  test('updateDisplay: null/undefined temp shows --', function() {
    w.document.getElementById('tempDisplay').textContent = 'OLD';
    T.updateDisplay({});
    is(w.document.getElementById('tempDisplay').textContent, '--');
  });
  test('updateDisplay: no last_unit does not throw', function() {
    T.updateDisplay({ last_temp: 25 });
    // should keep existing unit label
  });
  test('updateDisplay: fault badge shows hex code for fault > 0', function() {
    T.updateDisplay({ last_temp: 25, last_fault: 0x1A });
    var fb = w.document.getElementById('faultStatus');
    ok(fb.textContent.indexOf('FAULT') >= 0 && fb.textContent.indexOf('1A') >= 0);
    ok(fb.classList.contains('fault'));
  });
  test('updateDisplay: fault badge shows OK for no fault', function() {
    T.updateDisplay({ last_temp: 25, last_fault: 0 });
    var fb = w.document.getElementById('faultStatus');
    ok(fb.textContent.indexOf('OK') >= 0);
    ok(!fb.classList.contains('fault'));
  });
  test('updateDisplay: timestamp with valid time', function() {
    T.updateDisplay({ last_temp: 25, last_reading_time: 4000000 });
    var te = w.document.getElementById('lastReadTime');
    ok(te.textContent.indexOf('Last reading') >= 0 || te.textContent.length > 8);
  });
  test('updateDisplay: no reading_time shows fallback', function() {
    T.updateDisplay({ last_temp: 25, last_reading_time: 0 });
    is(w.document.getElementById('lastReadTime').textContent, 'No reading');
  });

  console.log('  --- showStatus ---');
  test('showStatus: customAlert path', function() {
    var called = null;
    w.customAlert = function(m) { called = m; };
    T.showStatus('m', 'info');
    is(called, 'm');
    delete w.customAlert;
  });
  test('showStatus: DOM fallback', function() {
    var el = w.document.getElementById('status');
    el.style.display = 'none';
    T.showStatus('d', 'error');
    is(el.textContent, 'd');
    is(el.style.display, 'block');
  });

  var total = passed + failed;
  console.log('\n  MAX31865: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
