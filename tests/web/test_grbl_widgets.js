// GRBL Widget Logic Tests — Jog, Override, Macros, Console, Probe, Connection, Frame
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'grbl_controller.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
async function asyncTest(name, fn) {
  try { await fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }
function approx(a, b, t) { if (Math.abs(a - b) > (t || 0.001)) throw new Error('expected ' + b + ' ±' + (t||0.001) + ' got ' + a); }

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
  console.log('\n=== GRBL Widget Logic Tests ===\n');

  // ====== JOG ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;
    var doc = dom.window.document;
    var spy = dom.window.__spy__;

    // Set up DOM elements that runJog/runJogXY depend on
    function setupJogDom(xyStep, zStep, feed) {
      function setSelectValue(el, val) {
        if (!el) return;
        var sv = String(val);
        var i;
        for (i = 0; i < el.options.length; i++) {
          if (String(el.options[i].value) === sv) {
            el.value = sv; return;
          }
        }
        // Try with .0 suffix for numeric values
        var num = Number(val);
        if (!isNaN(num)) {
          var rounded = num.toFixed(1);
          for (i = 0; i < el.options.length; i++) {
            if (String(el.options[i].value) === rounded) {
              el.value = rounded; return;
            }
          }
        }
        // Add option if not found
        var opt = el.ownerDocument.createElement('option');
        opt.value = sv;
        el.appendChild(opt);
        el.value = sv;
      }
      setSelectValue(doc.getElementById('grbl_step_xy_sel'), xyStep);
      setSelectValue(doc.getElementById('grbl_step_z_sel'), zStep);
      setSelectValue(doc.getElementById('grbl_feed'), feed);
    }

    test('runJog is a function', function () { ok(typeof T.runJog === 'function'); });
    test('runJogXY is a function', function () { ok(typeof T.runJogXY === 'function'); });
    test('quickUnlock is a function', function () { ok(typeof T.quickUnlock === 'function'); });
    test('quickHome is a function', function () { ok(typeof T.quickHome === 'function'); });
    test('quickZeroWcs is a function', function () { ok(typeof T.quickZeroWcs === 'function'); });
    test('quickReset is a function', function () { ok(typeof T.quickReset === 'function'); });
    test('quickStatus is a function', function () { ok(typeof T.quickStatus === 'function'); });
    test('setXyStepFromPreset is a function', function () { ok(typeof T.setXyStepFromPreset === 'function'); });

    test('normalizeZStepValue snaps correctly', function () {
      is(T.normalizeZStepValue(0.1), 0.1);
      is(T.normalizeZStepValue(0.0), 0.1);
      is(T.normalizeZStepValue(3), 2.0);
    });

    // Jog now runs through postBatch (M5 tool-off first, then the jog), so
    // commands are queued asynchronously. Override the mock to `success:true`
    // so every postSend resolves and collect the full command list.
    function jogAndCollect(run) {
      return new Promise(function (resolve) {
        spy.reset();
        dom.window.__MOCK_API__['/api/grbl/send'] = { success: true, status: 'ok', echo: '' };
        if (dom.window.__postBodies) dom.window.__postBodies.length = 0;
        run();
        setTimeout(function () { resolve((spy.calls.postSend || []).map(function (c) { return c[0]; })); }, 600);
      });
    }

    await asyncTest('runJog sends M5 tool-off before the jog for X+', async function () {
      setupJogDom(5, 0.5, 100);
      var cmds = await jogAndCollect(function () { T.runJog('X', 1); });
      is(cmds.length, 2, 'exactly M5 + jog are sent');
      is(cmds[0], 'M5', 'tool is off before jogging');
      is(cmds[1], '$J=G91 G21 X5 F100');
    });

    await asyncTest('runJog sends M5 tool-off before the jog for X-', async function () {
      setupJogDom(5, 0.5, 100);
      var cmds = await jogAndCollect(function () { T.runJog('X', -1); });
      is(cmds[0], 'M5', 'tool is off before jogging');
      is(cmds[1], '$J=G91 G21 X-5 F100');
    });

    await asyncTest('runJog sends M5 tool-off before the jog for Z', async function () {
      setupJogDom(10, 2.5, 200);
      var cmds = await jogAndCollect(function () { T.runJog('Z', 1); });
      is(cmds[0], 'M5', 'tool is off before jogging');
      is(cmds[1], '$J=G91 G21 Z2.5 F200');
    });

    await asyncTest('runJogXY sends M5 tool-off before the jog', async function () {
      setupJogDom(10, 1, 150);
      var cmds = await jogAndCollect(function () { T.runJogXY(1, 1); });
      is(cmds[0], 'M5', 'tool is off before jogging');
      is(cmds[1], '$J=G91 G21 X10 Y10 F150');
    });

    await asyncTest('runJogXY sends M5 tool-off before mixed-direction jog', async function () {
      setupJogDom(7.5, 1, 120);
      var cmds = await jogAndCollect(function () { T.runJogXY(-1, 1); });
      is(cmds[0], 'M5', 'tool is off before jogging');
      is(cmds[1], '$J=G91 G21 X-7.5 Y7.5 F120');
    });

    await asyncTest('runJogXYCenter sends M5 tool-off then jogs to center of max travel', async function () {
      T.state.maxTravel = { x: 400, y: 300, z: 100 };
      var feedEl = doc.getElementById('grbl_feed');
      if (feedEl) feedEl.value = '1000';
      var cmds = await jogAndCollect(function () { T.runJogXYCenter(); });
      is(cmds[0], 'M5', 'tool is off before center jog');
      ok(cmds[1].indexOf('$J=G90 G21 X200 Y150 F1000') >= 0, 'expected center jog, got ' + cmds[1]);
    });

    await asyncTest('runJogXYCenter sends M5 tool-off then X0 Y0 when max travel is unknown', async function () {
      T.state.maxTravel = { x: 0, y: 0, z: 0 };
      var feedEl = doc.getElementById('grbl_feed');
      if (feedEl) feedEl.value = '1000';
      var cmds = await jogAndCollect(function () { T.runJogXYCenter(); });
      is(cmds[0], 'M5', 'tool is off before fallback jog');
      ok(String(cmds[1]).indexOf('G90 G1 X0 Y0 F1000') >= 0, 'expected X0 Y0 jog, got ' + cmds[1]);
    });
  }

  // ====== OVERRIDE (apply-on-demand — sliders never send; Apply posts /api/grbl/override) ======
  {
    var dom = loadPage(); await wait(dom);
    var doc = dom.window.document;

    function setSlider(id, val) {
      var el = doc.getElementById(id);
      el.value = String(val);
      el.dispatchEvent(new dom.window.Event('input', { bubbles: true }));
    }
    function clickBtn(id) {
      var el = doc.getElementById(id);
      el.dispatchEvent(new dom.window.Event('click', { bubbles: true }));
    }
    function overridePosts() {
      return (dom.window.__postBodies || []).filter(function (p) {
        return p.url.indexOf('/api/grbl/override') >= 0;
      });
    }

    test('moving the feed slider updates the label but posts nothing', function () {
      dom.window.__postBodies = [];
      setSlider('grbl_feed_override', 150);
      is(doc.getElementById('grbl_feed_override_val').textContent, '150%');
      is(overridePosts().length, 0, 'no override request while dragging');
    });

    test('moving the spindle slider posts nothing either', function () {
      dom.window.__postBodies = [];
      setSlider('grbl_spindle_override', 80);
      is(doc.getElementById('grbl_spindle_override_val').textContent, '80%');
      is(overridePosts().length, 0);
    });

    test('Apply Override posts feed + spin to /api/grbl/override', function () {
      dom.window.__postBodies = [];
      setSlider('grbl_feed_override', 120);
      setSlider('grbl_spindle_override', 90);
      clickBtn('btn_apply_override');
      var posts = overridePosts();
      is(posts.length, 1, 'exactly one override request');
      var body = posts[0].body;
      ok(body.indexOf('feed=120') >= 0, 'feed param present, got ' + body);
      ok(body.indexOf('spin=90') >= 0, 'spin param present, got ' + body);
    });

    test('repeated Apply with unchanged values does not re-post', function () {
      dom.window.__postBodies = [];
      clickBtn('btn_apply_override');
      is(overridePosts().length, 0, 'unchanged apply is deduped');
    });

    test('Apply posts changed values again after a change', function () {
      dom.window.__postBodies = [];
      setSlider('grbl_spindle_override', 75);
      clickBtn('btn_apply_override');
      var posts = overridePosts();
      is(posts.length, 1, 'spin change triggers a new request');
      ok(posts[0].body.indexOf('spin=75') >= 0, 'new spin value posted');
    });

    await asyncTest('override slider changes are NOT persisted to browser localStorage', async function () {
      var ls = dom.window.localStorage;
      var KEY = 'node32_grbl_form_prefs_v1';
      ls.removeItem(KEY);
      dom.window.__postBodies = [];
      setSlider('grbl_feed_override', 150);
      setSlider('grbl_spindle_override', 75);
      await new Promise(function (r) { dom.window.setTimeout(r, 700); }); // let the 500ms debounce fire
      var raw = ls.getItem(KEY) || '{}';
      var prefs = {};
      try { prefs = JSON.parse(raw); } catch (e) {}
      is(prefs.grbl_feed_override, undefined, 'feed override must not be persisted');
      is(prefs.grbl_spindle_override, undefined, 'spindle override must not be persisted');
    });

    await asyncTest('override retries once after a transient failure', async function () {
      var realFetch = dom.window.fetch;
      var failOnce = true;
      dom.window.fetch = function (url, opts) {
        var u = typeof url === 'string' ? url : (url && url.url ? url.url : '');
        if (failOnce && u.indexOf('/api/grbl/override') >= 0) {
          failOnce = false;
          return Promise.reject(new Error('network'));
        }
        return realFetch(url, opts);
      };
      dom.window.__postBodies = [];
      setSlider('grbl_feed_override', 130);
      setSlider('grbl_spindle_override', 60);
      clickBtn('btn_apply_override');
      await new Promise(function (r) { dom.window.setTimeout(r, 800); }); // first fail + 400ms retry
      dom.window.fetch = realFetch;
      var posts = overridePosts();
      is(posts.length, 1, 'exactly one successful post after the transient failure');
      ok(posts[0].body.indexOf('feed=130') >= 0, 'retry posts feed');
      ok(posts[0].body.indexOf('spin=60') >= 0, 'retry posts spin');
    });
  }

  // ====== MACROS ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;

    test('defaultMacros returns 4 entries', function () {
      var m = T.defaultMacros();
      ok(Array.isArray(m) && m.length === 4, 'expected 4 macros got ' + (m ? m.length : 'null'));
    });

    test('defaultMacros has cmd and name', function () {
      var m = T.defaultMacros();
      m.forEach(function(entry, i) {
        ok(typeof entry.name === 'string', 'macro ' + i + ' name');
        ok(typeof entry.cmd === 'string', 'macro ' + i + ' cmd');
      });
    });

    test('defaultMacros values are correct', function () {
      var m = T.defaultMacros();
      is(m[0].name, '$$ View config');
      is(m[0].cmd, '$$');
      is(m[1].name, '$X Unlock');
      is(m[1].cmd, '$X');
      is(m[2].name, '$H Home');
      is(m[2].cmd, '$H');
      is(m[3].name, 'Zero WCS');
      is(m[3].cmd, 'G92 X0 Y0 Z0');
    });

    test('getMacroEntry returns valid inputs', function () {
      var entry = T.getMacroEntry(0);
      ok(entry !== null && entry !== undefined, 'should return entry');
      ok(typeof entry.nameInput === 'object' || typeof entry.cmdInput === 'object', 'DOM or stub');
    });

    test('loadMacrosToForm is a function', function () {
      ok(typeof T.loadMacrosToForm === 'function');
    });

    test('saveMacrosFromForm is a function', function () {
      ok(typeof T.saveMacrosFromForm === 'function');
    });

    test('runMacro is a function', function () {
      ok(typeof T.runMacro === 'function');
    });
  }

  // ====== CONSOLE ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;
    var doc = dom.window.document;

    test('clearConsole clears console element', function () {
      var consoleEl = doc.getElementById('grbl_console');
      if (consoleEl) {
        consoleEl.innerHTML = '<div>test</div>';
        T.clearConsole();
        is(consoleEl.innerHTML, '', 'console should be empty');
      } else {
        // Console might not be in DOM if widget is hidden
        ok(typeof T.clearConsole === 'function');
      }
    });

    test('logLine is a function', function () {
      ok(typeof T.logLine === 'function');
    });

    test('ingestConsoleEvents is a function', function () {
      ok(typeof T.ingestConsoleEvents === 'function');
    });

    test('toggleConsoleFullscreen is a function', function () {
      ok(typeof T.toggleConsoleFullscreen === 'function');
    });
  }

  // ====== STATUS / HOLD REASON ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;
    var doc = dom.window.document;
    var consoleEl = doc.getElementById('grbl_console');

    function countInConsole(s) {
      return (consoleEl.innerHTML.match(new RegExp(s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'g')) || []).length;
    }

    test('feed_stall hold reason logs warning line', function () {
      T.applyStatusSnapshot({ hold_reason: '' });
      T.clearConsole();
      T.applyStatusSnapshot({ hold_reason: 'feed_stall' });
      ok(countInConsole('GRBL feed stalled') === 1, 'expected feed stall log line');
    });

    test('feed_stall does not re-log on identical repeated status', function () {
      T.applyStatusSnapshot({ hold_reason: '' });
      T.clearConsole();
      T.applyStatusSnapshot({ hold_reason: 'feed_stall' });
      T.applyStatusSnapshot({ hold_reason: 'feed_stall' });
      is(countInConsole('GRBL feed stalled'), 1, 'dedup should keep a single line');
    });

    test('rx_stall still logs its own line', function () {
      T.applyStatusSnapshot({ hold_reason: '' });
      T.clearConsole();
      T.applyStatusSnapshot({ hold_reason: 'rx_stall' });
      ok(countInConsole('GRBL RX stall') === 1, 'expected rx stall log line');
    });

    test('unknown hold reason logs nothing', function () {
      T.applyStatusSnapshot({ hold_reason: '' });
      T.clearConsole();
      T.applyStatusSnapshot({ hold_reason: 'some_unknown_reason' });
      is(consoleEl.innerHTML, '', 'no log expected for unknown reason');
    });
  }

  // ====== PROBE ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;

    test('runAutoProbeZZero is a function', function () {
      ok(typeof T.runAutoProbeZZero === 'function');
    });

    test('fmtGcodeNum formats for probe G-code', function () {
      // Used inside runAutoProbeZZero
      is(T.fmtGcodeNum(3.0, 3), '3');
      is(T.fmtGcodeNum(1.5, 3), '1.5');
      is(T.fmtGcodeNum(0.05, 3), '0.05');
    });
  }

  // ====== CONNECTION / USB ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;

    test('selectedSlot is a function', function () {
      ok(typeof T.selectedSlot === 'function');
    });

    test('normalizeUsbSlots normalizes slots', function () {
      var raw = [
        { slot: 0, connected: true, desc: 'CH340', chip: 'CH340' },
        null,
        { slot: 2, connected: false, desc: '' }
      ];
      var result = T.normalizeUsbSlots(raw);
      ok(Array.isArray(result), 'should return array');
      ok(result.length >= 3, 'should have at least 3 slots');
      if (result[0]) {
        ok(result[0].slot !== undefined, 'slot 0 has slot number');
        ok(result[0].connected !== undefined, 'slot 0 has connected status');
        ok(typeof result[0].desc === 'string', 'slot 0 has description');
      }
    });

    test('normalizeUsbSlots handles empty', function () {
      var r = T.normalizeUsbSlots([]);
      ok(Array.isArray(r));
    });

    test('syncSelectedUsbBaud is a function', function () {
      ok(typeof T.syncSelectedUsbBaud === 'function');
    });

    test('ensureUsbBaudOption is a function', function () {
      ok(typeof T.ensureUsbBaudOption === 'function');
    });

    test('usbBaudPrefId generates key', function () {
      var result = T.usbBaudPrefId({ slot: 1, chip: 'CH340' });
      ok(typeof result === 'string' && result.length > 0);
    });

    test('getPreferredUsbBaud is a function', function () {
      ok(typeof T.getPreferredUsbBaud === 'function');
    });

    test('savePreferredUsbBaud is a function', function () {
      ok(typeof T.savePreferredUsbBaud === 'function');
    });
  }

  // ====== FRAME ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;
    var doc = dom.window.document;
    var spy = dom.window.__spy__;

    test('updateFrameInfo is a function', function () {
      ok(typeof T.updateFrameInfo === 'function');
    });

    test('getPreviewFrameBounds is a function', function () {
      ok(typeof T.getPreviewFrameBounds === 'function');
    });

    test('runFrameFromPreview is a function', function () {
      ok(typeof T.runFrameFromPreview === 'function');
    });

    test('setCommandMode is a function', function () {
      ok(typeof T.setCommandMode === 'function');
    });

    // Drive the full frame batch end-to-end. The mock /api/grbl/send response
    // has no `success` field by default, which would stall the postBatch chain
    // after the first command — override it to run every command to the end.
    function runFrameAndCollect(bounds, feed) {
      return new Promise(function (resolve) {
        spy.reset();
        dom.window.__MOCK_API__['/api/grbl/send'] = { success: true, status: 'ok', echo: '' };
        dom.window.__MOCK_API__['/api/grbl/status'] = { success: true, state: 'Idle', machine_state: 'Idle', position: {x:0,y:0,z:0}, machine: {x:0,y:0,z:0}, max_travel_x: 400, max_travel_y: 300, max_travel_z: 100, events: [], usb: {slot:1} };
        if (dom.window.__postBodies) dom.window.__postBodies.length = 0;
        T.state.preview.points = bounds;
        var feedSel = doc.getElementById('grbl_frame_feed');
        if (feedSel) feedSel.value = String(feed);
        T.runFrameFromPreview();
        setTimeout(function () { resolve(); }, 3500);
      });
    }
    function frameCommands() {
      return (spy.calls.postSend || [])
        .filter(function (c) { return String(c[1]).indexOf('FRAME') === 0; })
        .map(function (c) { return c[0]; });
    }

    await asyncTest('frame traces the bounds then returns to the frame center', async function () {
      await runFrameAndCollect([{x:10,y:10},{x:30,y:10},{x:30,y:40},{x:10,y:40}], 1200);
      var cmds = frameCommands();
      is(cmds.length, 12, 'frame sends 12 commands (10 trace batch + M5 + return to center)');
      is(cmds[0], 'M5', 'tool is off before the approach move to the corner');
      is(cmds[1], 'G90', 'second command selects absolute positioning');
      is(cmds[2], 'G21', 'frame forces metric (mm) mode');
      is(cmds[4], 'M3 S50', 'laser turns on with the configured power (5% of 1000) only at the frame contour, got: ' + cmds[4]);
      ok(cmds.slice(3, 4).every(function (c) { return c.indexOf('M3') < 0 && c.indexOf('M4') < 0; }), 'approach move to the corner has the tool off');
      is(cmds[9], 'M5', 'spindle is off at the end of the trace');
      is(cmds[10], 'M5', 'spindle is off again before the return to center');
      ok(cmds[11].indexOf('G90 G21 G1 X20.000 Y25.000 F1200') === 0, 'return to center is a separate command sent after the trace (20,25), got: ' + cmds[11]);
      ok(cmds.slice(0, 10).every(function (c) { return c.indexOf('X20') < 0; }), 'intermediate moves do not target the center (20,25)');
    });

    await asyncTest('return to center uses the widget feed and targets the piece center', async function () {
      await runFrameAndCollect([{x:50,y:-10},{x:70,y:-10},{x:70,y:10},{x:50,y:10}], 800);
      var cmds = frameCommands();
      is(cmds.length, 12, 'frame sends 12 commands for the second bounds set');
      is(cmds[0], 'M5', 'tool off precedes the approach move (second bounds set)');
      ok(cmds[11].indexOf('G90 G21 G1 X60.000 Y0.000 F800') === 0, 'return to center uses the configured frame feed, got: ' + cmds[11]);
      ok(cmds.slice(0, 10).every(function (c) { return c.indexOf('X60') < 0; }), 'intermediate moves do not target the piece center (60,0)');
    });
  }

  // ====== ALERT MESSAGES ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;
    var doc = dom.window.document;
    var spy = dom.window.__spy__;

    await asyncTest('postActionOnce catch shows descriptive error, not empty emoji', async function () {
      spy.reset();
      dom.window.__MOCK_API__['/api/grbl/action'] = function () { return Promise.reject(new Error('net fail')); };
      await T.postActionOnce('/api/grbl/action', {});
      var calls = (spy.calls.showAlert || []);
      ok(calls.length > 0, 'showAlert was called');
      var msg = String(calls[calls.length - 1][0]);
      ok(msg.length > 2, 'alert message is not just an emoji');
      ok(msg.indexOf('grblConnFailed') >= 0 || msg.indexOf('Connection failed') >= 0 || msg.indexOf('failed') >= 0 || msg.indexOf('Falha') >= 0,
        'alert contains descriptive text about connection failure, got: ' + msg);
    });

    await asyncTest('postAction catch shows descriptive error, not empty emoji', async function () {
      spy.reset();
      dom.window.__MOCK_API__['/api/grbl/action'] = function () { return Promise.reject(new Error('timeout')); };
      await T.postAction('/api/grbl/action', {});
      var calls = (spy.calls.showAlert || []);
      ok(calls.length > 0, 'showAlert was called');
      var msg = String(calls[calls.length - 1][0]);
      ok(msg.length > 2, 'alert message is not just an emoji');
      ok(msg.indexOf('grblConnFailed') >= 0 || msg.indexOf('Connection failed') >= 0 || msg.indexOf('failed') >= 0 || msg.indexOf('Falha') >= 0,
        'alert contains descriptive text, got: ' + msg);
    });

    await asyncTest('refreshStatus error shows descriptive error message', async function () {
      spy.reset();
      T.state.lastConsoleEventId = 0;
      dom.window.__MOCK_API__['/api/grbl/status'] = function () { return { success: false, message: 'err' }; };
      await T.refreshStatus(false);
      var calls = (spy.calls.showAlert || []);
      ok(calls.length > 0, 'showAlert was called');
      var msg = String(calls[0][0]);
      ok(msg.indexOf('grblStatusFail') >= 0 || msg.indexOf('Failed to fetch') >= 0 || msg.indexOf('failed') >= 0 || msg.indexOf('Falha') >= 0,
        'alert contains descriptive text about status failure, got: ' + msg);
    });

    await asyncTest('refreshStatus catch shows connection-lost message', async function () {
      spy.reset();
      T.state.lastConsoleEventId = 0;
      dom.window.__MOCK_API__['/api/grbl/status'] = function () { return Promise.reject(new Error('disconnect')); };
      await T.refreshStatus(false);
      var calls = (spy.calls.showAlert || []);
      ok(calls.length > 0, 'showAlert was called');
      var msg = String(calls[0][0]);
      ok(msg.indexOf('grblStatusConnFail') >= 0 || msg.indexOf('Connection to GRBL') >= 0 || msg.indexOf('lost') >= 0 || msg.indexOf('Conexão') >= 0,
        'alert contains descriptive text about lost connection, got: ' + msg);
    });

    await asyncTest('macro save failure shows descriptive error, not empty emoji', async function () {
      spy.reset();
      T.state.macroStorageKey = 'grbl_macros_test_fail';
      T.state.macroCount = 4;
      var origStringify = dom.window.JSON.stringify;
      dom.window.JSON.stringify = function () { throw new Error('quota exceeded'); };
      try { T.saveMacrosFromForm(); } catch (e) {}
      dom.window.JSON.stringify = origStringify;
      var calls = (spy.calls.showAlert || []);
      ok(calls.length > 0, 'showAlert was called');
      var msg = String(calls[0][0]);
      ok(msg.length > 2, 'alert message is not just an emoji');
      ok(msg.indexOf('grblMacroSaveFailed') >= 0 || msg.indexOf('Failed to save') >= 0 || msg.indexOf('Falha') >= 0,
        'alert contains descriptive text about macro save failure, got: ' + msg);
    });
  }

  // ====== GRBL TRANSPORT SELECTOR ======
  {
    var dom = loadPage(); await wait(dom);
    var T = dom.window.__test__;
    var doc = dom.window.document;
    var spy = dom.window.__spy__;

    test('transport selector widget is present and hidden', function () {
      var w = doc.getElementById('wid_transport');
      ok(!!w, 'wid_transport element exists');
      var sel = doc.getElementById('grbl_transport_sel');
      ok(!!sel, 'grbl_transport_sel exists');
      is(sel.options.length, 2, 'two transport options');
      is(sel.options[0].value, 'uart', 'first option is uart');
      is(sel.options[1].value, 'usb', 'second option is usb');
    });

    test('renderStatus with grbl_transport uart selects uart and shows UART label', function () {
      T.renderStatus({ connected: true, state: 'Idle', grbl_transport: 'uart', usb_connected: 0, usb_running: true, usb_backend: 'host', lines_total: 0, lines_sent: 0, lines_acked: 0, eta_sec: 0, events: [], workflow_state: 'idle', job_active: false, control_can_write: true, poll_interval_ms: 300 });
      var sel = doc.getElementById('grbl_transport_sel');
      is(sel.value, 'uart', 'selector reflects uart transport');
      is(doc.getElementById('grbl_transport_label').textContent, 'UART', 'transport label shows UART');
    });

    test('renderStatus with grbl_transport usb selects usb and shows USB Host label', function () {
      T.renderStatus({ connected: true, state: 'Idle', grbl_transport: 'usb', usb_connected: 1, usb_running: true, usb_backend: 'host', lines_total: 0, lines_sent: 0, lines_acked: 0, eta_sec: 0, events: [], workflow_state: 'idle', job_active: false, control_can_write: true, poll_interval_ms: 300 });
      var sel = doc.getElementById('grbl_transport_sel');
      is(sel.value, 'usb', 'selector reflects usb transport');
      is(doc.getElementById('grbl_transport_label').textContent, 'USB Host', 'transport label shows USB Host');
    });

    test('transport selector is disabled while a job is active', function () {
      T.renderStatus({ connected: true, state: 'Run', grbl_transport: 'uart', usb_connected: 0, usb_running: true, usb_backend: 'host', lines_total: 100, lines_sent: 10, lines_acked: 5, eta_sec: 60, events: [], workflow_state: 'running', job_active: true, control_can_write: true, poll_interval_ms: 300 });
      var sel = doc.getElementById('grbl_transport_sel');
      is(sel.disabled, true, 'selector is disabled with an active job');
      is(T.state.canWrite, true, 'canWrite stays true');
    });

    await asyncTest('changing the selector posts /api/grbl/transport with mode', async function () {
      spy.reset();
      if (dom.window.__postBodies) dom.window.__postBodies.length = 0;
      T.renderStatus({ connected: true, state: 'Idle', grbl_transport: 'uart', usb_connected: 0, usb_running: true, usb_backend: 'host', lines_total: 0, lines_sent: 0, lines_acked: 0, eta_sec: 0, events: [], workflow_state: 'idle', job_active: false, control_can_write: true, poll_interval_ms: 300 });
      dom.window.__MOCK_API__['/api/grbl/transport'] = { success: true, message: 'ok', grbl_transport: 'usb' };
      var sel = doc.getElementById('grbl_transport_sel');
      sel.value = 'usb';
      sel.dispatchEvent(new dom.window.Event('change'));
      await new Promise(function (r) { setTimeout(r, 250); });
      var posts = (dom.window.__postBodies || []).filter(function (p) { return p.url === '/api/grbl/transport'; });
      is(posts.length, 1, 'transport POST was sent exactly once');
      is(posts[0].method, 'POST', 'transport POST uses POST method');
      ok(posts[0].body.indexOf('mode=usb') >= 0, 'POST body carries mode=usb, got: ' + posts[0].body);
    });

    await asyncTest('transport switch failure reverts the selector', async function () {
      spy.reset();
      T.renderStatus({ connected: true, state: 'Idle', grbl_transport: 'uart', usb_connected: 0, usb_running: true, usb_backend: 'host', lines_total: 0, lines_sent: 0, lines_acked: 0, eta_sec: 0, events: [], workflow_state: 'idle', job_active: false, control_can_write: true, poll_interval_ms: 300 });
      dom.window.__MOCK_API__['/api/grbl/transport'] = function () { return Promise.reject(new Error('net fail')); };
      var sel = doc.getElementById('grbl_transport_sel');
      sel.value = 'usb';
      sel.dispatchEvent(new dom.window.Event('change'));
      await new Promise(function (r) { setTimeout(r, 250); });
      is(sel.value, 'uart', 'selector reverts to uart after a failed switch');
      var alerts = (spy.calls.showAlert || []);
      ok(alerts.length > 0, 'showAlert was called on failure');
    });
  }

  // ====== Summary ======
  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e){console.error('Fatal:',e);process.exit(1);});
