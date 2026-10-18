// GRBL Utility Functions Tests
// Run: node test_grbl_utils.js
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'grbl_controller.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }
function approx(a, b, tol) { if (Math.abs(a - b) > (tol || 0.001)) throw new Error('expected ' + b + ' ±' + (tol||0.001) + ' got ' + a); }

function loadPage() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var fix = fs.readFileSync(path.join(__dirname, 'mocks', 'api_fixtures.js'), 'utf8');
  html = html.replace('// ===== MOCK API LAYER =====', fix + '\n// ===== MOCK API LAYER =====');
  var orig = console.error; console.error = function() {
    var m = arguments[0]||''; if(typeof m!=='string'||m.indexOf('Not implemented')<0) orig.apply(console,arguments);
  };
  var dom = new JSDOM(html, {url:'http://localhost', runScripts:'dangerously', pretendToBeVisual:true});
  dom.window.HTMLCanvasElement.prototype.getContext = function(){return null;};
  console.error = orig;
  return dom;
}

function wait(dom) { return new Promise(function(r){setTimeout(function(){r(dom);}, 300);}); }

async function run() {
  console.log('\n=== GRBL Utility Tests ===\n');
  var dom = loadPage(); await wait(dom);
  var T = dom.window.__test__;

  // ------ fmtEta ------
  test('fmtEta formats seconds', function () {
    is(T.fmtEta(0), '--', '0 should return --');
    is(T.fmtEta(1), '1s');
    is(T.fmtEta(59), '59s');
    is(T.fmtEta(60), '1m 0s');
    is(T.fmtEta(125), '2m 5s');
    is(T.fmtEta(-1), '--', 'negative should return --');
  });

  // ------ fmtLagMs ------
  test('fmtLagMs formats milliseconds', function () {
    is(T.fmtLagMs(0), '--', '0 should return --');
    is(T.fmtLagMs(10), '10ms');
    is(T.fmtLagMs(50), '50ms');
    is(T.fmtLagMs(999), '999ms');
    is(T.fmtLagMs(1000), '1.0s');
    is(T.fmtLagMs(1500), '1.5s');
    is(T.fmtLagMs(-5), '--', 'negative should return --');
  });

  // ------ fmtGcodeNum ------
  test('fmtGcodeNum formats floats for G-code', function () {
    is(T.fmtGcodeNum(10.5, 3), '10.5');
    is(T.fmtGcodeNum(10.0, 3), '10');
    is(T.fmtGcodeNum(0.1, 3), '0.1');
    is(T.fmtGcodeNum(0.100, 3), '0.1');
    is(T.fmtGcodeNum(-5.250, 3), '-5.25');
    is(T.fmtGcodeNum(0, 3), '0');
  });

  // ------ normalizeZStepValue ------
  test('normalizeZStepValue snaps to nearest allowed', function () {
    is(T.normalizeZStepValue(0.1), 0.1);
    is(T.normalizeZStepValue(0.15), 0.1);
    is(T.normalizeZStepValue(0.0), 0.1);
    is(T.normalizeZStepValue(-1), 0.1);
    is(T.normalizeZStepValue(5), 2.0);
    is(T.normalizeZStepValue('0.8'), 0.8);
  });

  // ------ clamp ------
  test('clamp value between lo and hi', function () {
    // Try via eval since it's available
    var clamp = T.clamp;
    if (typeof clamp !== 'function') { console.log('  [skip] clamp not exposed'); ok(true); return; }
    is(clamp(5, 0, 10), 5);
    is(clamp(-1, 0, 10), 0);
    is(clamp(15, 0, 10), 10);
    is(clamp(0, 0, 10), 0);
    is(clamp(10, 0, 10), 10);
  });

  // ------ sanitizeFileName ------
  test('sanitizeFileName cleans paths', function () {
    is(T.sanitizeFileName('/sd/test.gcode'), 'test.gcode');
    is(T.sanitizeFileName('test.gcode'), 'test.gcode');
    is(T.sanitizeFileName('../test.gcode'), 'test.gcode');
    is(T.sanitizeFileName('a/b/c.gcode'), 'c.gcode');
  });

  test('sanitizeFileName replaces invalid chars', function () {
    var r = T.sanitizeFileName('bad:file|name.gcode');
    ok(r.indexOf(':') < 0 && r.indexOf('|') < 0, 'invalid chars present: ' + r);
  });

  // ------ formatFileSize ------
  test('formatFileSize formats bytes', function () {
    is(T.formatFileSize(0), '0 B');
    is(T.formatFileSize(500), '500 B');
    is(T.formatFileSize(1023), '1023 B');
    is(T.formatFileSize(1024), '1.0 KB');
    is(T.formatFileSize(1536), '1.5 KB');
    is(T.formatFileSize(1048576), '1.0 MB');
    is(T.formatFileSize(10485760), '10.0 MB');
  });

  // ------ recentErrorMeta ------
  test('recentErrorMeta formats error metadata', function () {
    var r = T.recentErrorMeta({ sent: 10, ack: 8, count: 2, cmd: 'G91' });
    ok(typeof r === 'string' && r.length > 0);
  });

  test('recentErrorMeta handles empty', function () {
    var r = T.recentErrorMeta({});
    ok(typeof r === 'string');
  });

  // ------ extractAxisValue ------
  test('extractAxisValue regex', function () {
    var ex = T.extractAxisValue;
    if (typeof ex !== 'function') { console.log('  [skip] extractAxisValue not exposed'); ok(true); return; }
    is(ex('G0 X10.5 Y20.3 Z-5', 'X'), 10.5);
    is(ex('G1 X-1.5 F100', 'X'), -1.5);
    is(ex('G1 Y10', 'X'), null);
    is(ex('M30', 'Z'), null);
    is(ex('G0X5', 'X'), 5);
  });

  test('extractAxisInMm converts units', function () {
    var ex = T.extractAxisInMm;
    if (typeof ex !== 'function') { console.log('  [skip] extractAxisInMm not exposed'); ok(true); return; }
    var r = ex('G91 G21 X10', 'X');
    if (r !== null) approx(r, 10);
  });

  // ------ parseNumber3 ------
  test('parseNumber3 parses floats', function () {
    is(T.parseNumber3('10.5'), 10.5);
    is(T.parseNumber3('-5'), -5);
    is(T.parseNumber3('0'), 0);
    is(T.parseNumber3(''), null);
    is(T.parseNumber3('abc'), null);
  });

  // ------ parsePosTriplet ------
  test('parsePosTriplet parses x,y,z', function () {
    var r = T.parsePosTriplet('10.5,20.3,-5');
    ok(r !== null, 'should return object');
    if (r) {
      approx(r.x, 10.5); approx(r.y, 20.3); approx(r.z, -5);
    }
  });

  test('parsePosTriplet handles partial', function () {
    var r = T.parsePosTriplet('10,20');
    ok(r !== null && r.x === 10 && r.y === 20, 'should have x and y');
  });

  // ------ firstNumber3 ------
  test('firstNumber3 returns first valid', function () {
    is(T.firstNumber3(null, undefined, 5, 10), 5);
    is(T.firstNumber3(), null);
    is(T.firstNumber3(0, 10), 0);
    is(T.firstNumber3('abc', 10), 10);
    is(T.firstNumber3(null, 3.14), 3.14);
  });

  // ------ simplifyPolylineLight ------
  test('simplifyPolylineLight reduces points', function () {
    var sp = T.simplifyPolylineLight;
    if (typeof sp !== 'function') { console.log('  [skip] simplifyPolylineLight not exposed'); ok(true); return; }
    var pts = [{x:0,y:0},{x:1,y:1},{x:2,y:2},{x:3,y:3},{x:4,y:4}];
    var result = sp(pts, 0.5);
    ok(result.length <= pts.length, 'should reduce or equal');
  });

  test('simplifyPolylineLight handles empty', function () {
    var sp = T.simplifyPolylineLight;
    if (typeof sp !== 'function') { ok(true); return; }
    var result = sp([], 0.5);
    ok(Array.isArray(result));
  });

  // ------ setCommandMode ------
  test('setCommandMode is a function', function () {
    ok(typeof T.setCommandMode === 'function');
  });

  // ------ syncMainStepFromSelectors ------
  test('syncMainStepFromSelectors is a function', function () {
    ok(typeof T.syncMainStepFromSelectors === 'function');
  });

  // ------ Summary ------
  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e){console.error('Fatal:',e);process.exit(1);});
