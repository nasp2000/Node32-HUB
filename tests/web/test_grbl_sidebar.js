// GRBL Controller Sidebar Tests
// Run: node test_grbl_sidebar.js
// Requires: npm install jsdom

const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'grbl_controller.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try {
    fn();
    passed++;
    console.log('  \u2713 ' + name);
  } catch (e) {
    failed++;
    console.log('  \u2717 ' + name + ': ' + e.message);
  }
}

function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function loadPage() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var fixtures = fs.readFileSync(path.join(__dirname, 'mocks', 'api_fixtures.js'), 'utf8');
  html = html.replace('// ===== MOCK API LAYER =====', fixtures + '\n// ===== MOCK API LAYER =====');
  // Suppress jsdom "Not implemented" warnings to stderr
  var origError = console.error;
  console.error = function() {
    var msg = arguments[0] || '';
    if (typeof msg === 'string' && msg.indexOf('Not implemented') >= 0) return;
    origError.apply(console, arguments);
  };
  try {
    var dom = new (require('jsdom').JSDOM)(html, {
      url: 'http://localhost',
      runScripts: 'dangerously',
      pretendToBeVisual: true,
    });
    dom.window.HTMLCanvasElement.prototype.getContext = function() { return null; };
    return dom;
  } finally {
    console.error = origError;
  }
}

function wait(dom) {
  return new Promise(function (res) {
    setTimeout(function () { res(dom); }, 200);
  });
}

async function run() {
  console.log('\n=== GRBL Sidebar Tests ===\n');

  // ------ renderWidgetSidebar ------
  {
    var dom = loadPage();
    await wait(dom);
    var doc = dom.window.document;
    var T = dom.window.__test__;

    test('renders 12 widget rows', function () {
      is(doc.querySelectorAll('.glp-widget-row').length, 12);
    });

    test('widget names in order', function () {
      var names = doc.querySelectorAll('.glp-widget-name');
      var exp = [
        'GRBL Transport', 'G-code Job', 'USB Connection', 'Override', 'Recent Errors',
        'Manual Jog', 'Frame (work area)', 'Macros', 'Probe',
        'Height Map', 'Workspace (DRO + 3D)', 'GRBL Console'
      ];
      names.forEach(function (n, i) { is(n.textContent.trim(), exp[i], 'row ' + i); });
    });

    test('first row has no up button', function () {
      var r = doc.querySelector('.glp-widget-row');
      ok(!r.querySelector('[data-action="up"]'));
    });

    test('last row has no down button', function () {
      var rows = doc.querySelectorAll('.glp-widget-row');
      var last = rows[rows.length - 1];
      ok(!last.querySelector('[data-action="down"]'));
    });

    test('center widgets have no side button', function () {
      doc.querySelectorAll('.glp-widget-row').forEach(function (r) {
        var wid = r.getAttribute('data-wid');
        var sb = r.querySelector('[data-action="side"]');
        if (wid === 'wid_workspace' || wid === 'wid_console') ok(!sb, wid + ' has side btn');
      });
    });

    test('all checkboxes checked by default', function () {
      doc.querySelectorAll('.glp-widget-row input[type="checkbox"]').forEach(function (c, i) {
        ok(c.checked, 'row ' + i + ' not checked');
      });
    });

    test('moveWidgetUp swaps rows', function () {
      var rows = doc.querySelectorAll('.glp-widget-row');
      var second = rows[1].getAttribute('data-wid');
      T.moveWidgetUp(second);
      T.renderWidgetSidebar();
      var after = doc.querySelectorAll('.glp-widget-row');
      is(after[0].getAttribute('data-wid'), second);
    });

    test('moveWidgetUp on first is no-op', function () {
      var rows = doc.querySelectorAll('.glp-widget-row');
      var first = rows[0].getAttribute('data-wid');
      T.moveWidgetUp(first);
      T.renderWidgetSidebar();
      is(doc.querySelectorAll('.glp-widget-row')[0].getAttribute('data-wid'), first);
    });

    test('moveWidgetDown swaps rows', function () {
      var rows = doc.querySelectorAll('.glp-widget-row');
      var first = rows[0].getAttribute('data-wid');
      T.moveWidgetDown(first);
      T.renderWidgetSidebar();
      var after = doc.querySelectorAll('.glp-widget-row');
      is(after[1].getAttribute('data-wid'), first);
    });

    test('moveWidgetDown on last is no-op', function () {
      var rows = doc.querySelectorAll('.glp-widget-row');
      var last = rows[rows.length - 1].getAttribute('data-wid');
      T.moveWidgetDown(last);
      T.renderWidgetSidebar();
      var after = doc.querySelectorAll('.glp-widget-row');
      is(after[after.length - 1].getAttribute('data-wid'), last);
    });
  }

  // ------ toggleWidgetVis ------
  {
    var dom = loadPage();
    await wait(dom);
    var doc = dom.window.document;
    var T = dom.window.__test__;

    test('toggleWidgetVis hides and shows muted', function () {
      T.toggleWidgetVis('wid_jog', false);
      is(doc.getElementById('wid_jog').style.display, 'none');
      T.renderWidgetSidebar();
      var row = doc.querySelector('[data-wid="wid_jog"]');
      ok(row.querySelector('.glp-widget-name').classList.contains('muted'));
      ok(!row.querySelector('input[type="checkbox"]').checked);
    });

    test('toggleWidgetVis shows widget', function () {
      T.toggleWidgetVis('wid_jog', true);
      ok(doc.getElementById('wid_jog').style.display !== 'none');
      T.renderWidgetSidebar();
      var row = doc.querySelector('[data-wid="wid_jog"]');
      ok(!row.querySelector('.glp-widget-name').classList.contains('muted'));
      ok(row.querySelector('input[type="checkbox"]').checked);
    });
  }

  // ------ moveWidgetToSide ------
  {
    var dom = loadPage();
    await wait(dom);
    var doc = dom.window.document;
    var T = dom.window.__test__;

    test('moveWidgetToSide moves to right dock', function () {
      var right = doc.getElementById('grbl_right_dock');
      var before = right.querySelectorAll('.grbl-dock-widget').length;
      T.moveWidgetToSide('wid_job', 'right');
      var after = right.querySelectorAll('.grbl-dock-widget').length;
      is(after, before + 1);
      ok(right.querySelector('#wid_job'));
    });
  }

  // ------ Persistence ------
  {
    var dom = loadPage();
    await wait(dom);
    var T = dom.window.__test__;
    var doc = dom.window.document;

    test('saveUiPrefs is defined', function () {
      ok(typeof T.saveUiPrefs === 'function');
    });

    test('saveUiPrefs writes to localStorage', function () {
      T.saveUiPrefs();
      var raw = dom.window.localStorage.getItem('node32_grbl_ui_prefs_v1');
      ok(raw !== null && raw.length > 0, 'localStorage empty after saveUiPrefs');
      var data = JSON.parse(raw);
      ok(data.confirmCritical === true || data.confirmCritical === false);
      ok('keyboardJogEnabled' in data, 'keyboardJogEnabled in payload');
      ok('jogStepXy' in data, 'jogStepXy in payload');
      ok('jogStepZ' in data, 'jogStepZ in payload');
      ok(typeof data.theme === 'undefined', 'theme no longer stored (dark-only)');
    });

    test('readUiPrefs is defined', function () {
      ok(typeof T.readUiPrefs === 'function');
    });

    test('readUiPrefs applies persisted preferences', function () {
      dom.window.localStorage.setItem('node32_grbl_ui_prefs_v1',
        JSON.stringify({confirmCritical: false, keyboardJogEnabled: false, jogStepXy: '1.0', jogStepZ: '0.5'}));
      T.readUiPrefs();
      ok(T.state.confirmCritical === false, 'confirmCritical applied');
      ok(T.state.keyboardJogEnabled === false, 'keyboardJogEnabled applied');
      is(doc.getElementById('grbl_step_xy_sel').value, '1.0', 'jogStepXy applied');
      is(doc.getElementById('grbl_step_z_sel').value, '0.5', 'jogStepZ applied');
    });
  }

  // ------ Summary ------
  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) {
  console.error('Fatal:', e);
  process.exit(1);
});
