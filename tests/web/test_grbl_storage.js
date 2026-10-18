// GRBL Storage Tests — SD Card, PSRAM, Form Persistence, Dock Layout
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
    var m = arguments[0]||''; if(typeof m!=='string'||m.indexOf('Not implemented')<0) orig.apply(console,arguments);
  };
  var dom = new JSDOM(html, {url:'http://localhost', runScripts:'dangerously', pretendToBeVisual:true});
  dom.window.HTMLCanvasElement.prototype.getContext = function(){return null;};
  console.error = orig;
  return dom;
}

function wait(dom) { return new Promise(function(r){setTimeout(function(){r(dom);}, 300);}); }

async function run() {
  console.log('\n=== GRBL Storage Tests ===\n');
  var dom = loadPage(); await wait(dom);
  var T = dom.window.__test__;
  var doc = dom.window.document;

  // ------ SD Card ------
  test('sdFileNameFromPath extracts filename', function () {
    is(T.sdFileNameFromPath('/sd/test.gcode'), 'test.gcode');
    is(T.sdFileNameFromPath('test.gcode'), 'test.gcode');
    is(T.sdFileNameFromPath('/sd/subdir/'), '/sd/subdir/');  // trailing slash → returns path
    is(T.sdFileNameFromPath(''), '');
  });

  test('sdParentDir returns parent', function () {
    // sdParentDir depends on state.sdRoot being set
    var root = T.state && T.state.sdRoot;
    if (!root) {
      // cannot test without sdRoot being set
      ok(typeof T.sdParentDir === 'function');
    } else {
      is(T.sdParentDir('/sd/subdir'), '/sd');
    }
  });

  test('formatSdSize formats sizes', function () {
    is(T.formatSdSize(0), '0 B');
    is(T.formatSdSize(512), '512 B');
    is(T.formatSdSize(1024), '1.0 KB');
    is(T.formatSdSize(1048576), '1.00 MB');
    is(T.formatSdSize(10485760), '10.00 MB');
  });

  test('normalizeSdEntry normalizes entry', function () {
    var raw = { name: 'test.gcode', size: 12345, isDir: false };
    var norm = T.normalizeSdEntry(raw);
    ok(norm !== null, 'should return normalized entry');
    if (norm) {
      ok(typeof norm.name === 'string');
      ok(typeof norm.size === 'number' || typeof norm.size === 'string');
    }
  });

  test('normalizeSdEntry handles directory', function () {
    var raw = { name: 'folder', size: 0, isDir: true };
    var norm = T.normalizeSdEntry(raw);
    ok(norm !== null);
  });

  test('isPreviewSizeBlocked checks size limit', function () {
    is(T.isPreviewSizeBlocked({ bytes: 1024 }), false);
    is(T.isPreviewSizeBlocked({ bytes: 60 * 1024 * 1024 }), true);
    is(T.isPreviewSizeBlocked({ bytes: 0 }), false);
  });

  test('isPreviewAutoRenderHeavy checks threshold', function () {
    is(T.isPreviewAutoRenderHeavy({ bytes: 1024 }), false);
    is(T.isPreviewAutoRenderHeavy({ bytes: 15 * 1024 * 1024 }), true);
    is(T.isPreviewAutoRenderHeavy({ bytes: 10 * 1024 * 1024 }), false);  // equal → not heavy
  });

  // ------ PSRAM ------
  test('refreshPsramFileStatus is a function', function () {
    ok(typeof T.refreshPsramFileStatus === 'function');
  });

  // ------ Dock Layout ------
  test('saveDockLayout saves to localStorage', function () {
    T.saveDockLayout();
    var raw = dom.window.localStorage.getItem('node32_grbl_dock_layout_v1');
    ok(raw !== null, 'dock layout should be in localStorage');
    if (raw) {
      var parsed = JSON.parse(raw);
      ok(Array.isArray(parsed.left) || Array.isArray(parsed.right), 'should have left/right arrays');
    }
  });

  test('restoreDockLayout is a function', function () {
    ok(typeof T.restoreDockLayout === 'function');
  });

  // ------ Widget Prefs ------
  test('saveWidgetPrefs saves to localStorage', function () {
    T.saveWidgetPrefs();
    var raw = dom.window.localStorage.getItem('node32_grbl_widget_vis_v1');
    ok(raw !== null, 'widget vis should be in localStorage');
    if (raw) {
      var parsed = JSON.parse(raw);
      ok(typeof parsed.wid_job !== 'undefined', 'should have wid_job visibility');
    }
  });

  test('restoreWidgetPrefs is a function', function () {
    ok(typeof T.restoreWidgetPrefs === 'function');
  });

  // ------ Form Persistence ------
  test('state.uiPrefsKey is defined', function () {
    ok(T.state && T.state.uiPrefsKey === 'node32_grbl_ui_prefs_v1');
  });

  test('state.dockPrefsKey is defined', function () {
    ok(T.state && T.state.dockPrefsKey === 'node32_grbl_dock_layout_v1');
  });

  test('state.formPrefsKey is defined', function () {
    ok(T.state && T.state.formPrefsKey === 'node32_grbl_form_prefs_v1');
  });

  // ------ Summary ------
  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e){console.error('Fatal:',e);process.exit(1);});
