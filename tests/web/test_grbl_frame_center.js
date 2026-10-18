// GRBL Frame: return-to-center after trace
// Verifies _doRunFrameWithBounds (via runFrameFromPreview) traces the work-area
// box and then returns the tool to the CENTER of the frame box, with the
// spindle off before the return move. The return-to-center is sent as a
// separate command AFTER the machine is idle again (not inside the rapid trace
// batch, where the ESP32 send buffer can drop the final command).
// Run: node test_grbl_frame_center.js

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

function loadPage() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var fix = fs.readFileSync(path.join(__dirname, 'mocks', 'api_fixtures.js'), 'utf8');
  html = html.replace('// ===== MOCK API LAYER =====', fix + '\n// ===== MOCK API LAYER =====');
  var orig = console.error; console.error = function() {
    var m = arguments[0] || '';
    if (typeof m !== 'string' || m.indexOf('Not implemented') < 0) orig.apply(console, arguments);
  };
  var dom = new JSDOM(html, { url: 'http://localhost', runScripts: 'dangerously', pretendToBeVisual: true });
  dom.window.HTMLCanvasElement.prototype.getContext = function() { return null; };
  console.error = orig;
  return dom;
}

function waitForCmdCount(dom, n, timeout) {
  return new Promise(function(resolve, reject) {
    var t0 = Date.now();
    (function poll() {
      var calls = dom.window.__spy__.calls.postSend || [];
      if (calls.length >= n) { resolve(); return; }
      if (Date.now() - t0 > timeout) {
        reject(new Error('timeout waiting for ' + n + ' sent commands (got ' + calls.length + ')'));
        return;
      }
      setTimeout(poll, 40);
    })();
  });
}

function runFrame(dom, points, lastStatus) {
  var T = dom.window.__test__;
  dom.window.__spy__.reset();
  T.state.lastStatus = lastStatus;
  T.state.preview = { points: points };
  T.runFrameFromPreview();
}

async function run() {
  console.log('\n=== GRBL Frame Center-Return Tests ===\n');

  // Variation 1: default power 5%, feed 1200, box (0,0)-(100,50)
  {
    var dom = loadPage();
    var spy = dom.window.__spy__;
    runFrame(dom,
      [{ x: 0, y: 0 }, { x: 100, y: 0 }, { x: 100, y: 50 }, { x: 0, y: 50 }],
      { machine_state: 'IDLE' });
    await waitForCmdCount(dom, 12, 6000);
    var cmds = spy.calls.postSend.map(function(a) { return a[0]; });

    test('frame batch = 12 commands (10 trace + M5 + center return)', function () {
      is(cmds.length, 12);
    });

    test('tool off, absolute/units, then start corner', function () {
      is(cmds[0], 'M5');
      is(cmds[1], 'G90');
      is(cmds[2], 'G21');
      is(cmds[3], 'G1 X0.000 Y0.000 F1200');
    });

    test('spindle on for trace, off before return', function () {
      is(cmds[4], 'M3 S50');
      is(cmds[9], 'M5');
      var trace = cmds.slice(5, 9);
      ok(trace.every(function(c) { return c.indexOf('M5') !== 0; }), 'no M5 during trace');
      ok(cmds[10].indexOf('M5') === 0, 'M5 again before the return move');
      ok(cmds[11].indexOf('M3') !== 0, 'no spindle command on the return move');
    });

    test('returns to CENTER of frame box after trace', function () {
      is(cmds[11], 'G90 G21 G1 X50.000 Y25.000 F1200');
    });
  }

  // Variation 2: custom feed 800 / power 0 (clamped to 1%), decimal + negative box
  {
    var dom = loadPage();
    var spy = dom.window.__spy__;
    var doc = dom.window.document;
    doc.getElementById('grbl_frame_feed').value = '800';
    doc.getElementById('grbl_frame_power').value = '10';
    runFrame(dom,
      [{ x: -10.5, y: 5.25 }, { x: 42.125, y: 5.25 }, { x: 42.125, y: 87.75 }, { x: -10.5, y: 87.75 }],
      { machine_state: 'IDLE', spindle_max_rpm: 24000 });
    await waitForCmdCount(dom, 12, 6000);
    var cmds = spy.calls.postSend.map(function(a) { return a[0]; });

    test('power 10% + 24000 max rpm -> S = max(1, round(24000*10/100))', function () {
      is(cmds[4], 'M3 S2400');
    });

    test('trace follows box corners at custom feed', function () {
      is(cmds[3], 'G1 X-10.500 Y5.250 F800');
      is(cmds[5], 'G1 X42.125 Y5.250 F800');
      is(cmds[6], 'G1 X42.125 Y87.750 F800');
      is(cmds[7], 'G1 X-10.500 Y87.750 F800');
      is(cmds[8], 'G1 X-10.500 Y5.250 F800');
    });

    test('returns to CENTER of decimal box at custom feed', function () {
      is(cmds[9], 'M5');
      is(cmds[10], 'M5');
      is(cmds[11], 'G90 G21 G1 X15.813 Y46.500 F800');
    });
  }

  console.log('\n' + passed + '/' + (passed + failed) + ' passed');
  process.exit(failed ? 1 : 0);
}

run();
