// Sound Level Sensor Page Tests — updateDisplay (dBFS color, level bar), showStatus, t, uiLocale
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var PAGE = path.join(__dirname, 'pages', 'sound_asset.html');
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
    w._t = function(k) { return 'T'; };
    is(T.t('k', 'fb'), 'T');
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
  test('updateDisplay: peakToPeak value', function() {
    T.updateDisplay({ last_peak_to_peak: 512 });
    is(w.document.getElementById('peakToPeak').textContent, '512');
  });
  test('updateDisplay: peak_mv value', function() {
    T.updateDisplay({ last_peak_mv: 123.456 });
    is(w.document.getElementById('peakMv').textContent, '123.5');
  });
  test('updateDisplay: dBFS color red (> -20)', function() {
    T.updateDisplay({ last_dbfs: -10 });
    is(w.document.getElementById('dbfsVal').textContent, '-10.0');
    ok(w.document.getElementById('dbfsVal').style.color.indexOf('239') >= 0, 'should be reddish');
  });
  test('updateDisplay: dBFS color orange (-40 to -20)', function() {
    T.updateDisplay({ last_dbfs: -30 });
    ok(w.document.getElementById('dbfsVal').style.color.indexOf('245') >= 0, 'should be orange-ish');
  });
  test('updateDisplay: dBFS color yellow (-60 to -40)', function() {
    T.updateDisplay({ last_dbfs: -50 });
    ok(w.document.getElementById('dbfsVal').style.color.indexOf('251') >= 0, 'should be yellowish');
  });
  test('updateDisplay: dBFS color green (< -60)', function() {
    T.updateDisplay({ last_dbfs: -80 });
    ok(w.document.getElementById('dbfsVal').style.color.indexOf('74') >= 0, 'should be greenish');
  });
  test('updateDisplay: level bar 0% at -90 dBFS', function() {
    T.updateDisplay({ last_dbfs: -90 });
    is(w.document.getElementById('levelBar').style.width, '0%');
  });
  test('updateDisplay: level bar 50% at -45 dBFS', function() {
    T.updateDisplay({ last_dbfs: -45 });
    is(w.document.getElementById('levelBar').style.width, '50%');
  });
  test('updateDisplay: level bar 100% at 0 dBFS', function() {
    T.updateDisplay({ last_dbfs: 0 });
    is(w.document.getElementById('levelBar').style.width, '100%');
  });
  test('updateDisplay: level bar clamped below 0%', function() {
    T.updateDisplay({ last_dbfs: -100 });
    is(w.document.getElementById('levelBar').style.width, '0%');
  });
  test('updateDisplay: level bar clamped above 100%', function() {
    T.updateDisplay({ last_dbfs: 10 });
    is(w.document.getElementById('levelBar').style.width, '100%');
  });
  test('updateDisplay: timestamp with valid reading_time', function() {
    T.updateDisplay({ last_dbfs: -50, last_reading_time: 3000000 });
    var te = w.document.getElementById('lastReadTime');
    ok(te.textContent.length > 5 && te.textContent.indexOf('No reading') < 0);
  });
  test('updateDisplay: no reading_time', function() {
    T.updateDisplay({ last_dbfs: -50, last_reading_time: 0 });
    is(w.document.getElementById('lastReadTime').textContent, 'No reading');
  });

  console.log('  --- showStatus ---');
  test('showStatus: customAlert path', function() {
    var called = null;
    w.customAlert = function(m) { called = m; };
    T.showStatus('snd', 'info');
    is(called, 'snd');
    delete w.customAlert;
  });
  test('showStatus: DOM fallback', function() {
    var el = w.document.getElementById('status');
    el.style.display = 'none';
    T.showStatus('dom', 'success');
    is(el.textContent, 'dom');
    is(el.style.display, 'block');
  });

  var total = passed + failed;
  console.log('\n  Sound: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
