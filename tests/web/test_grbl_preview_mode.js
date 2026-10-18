// GRBL Preview Mode Resolution Tests
// Regression guard: `applyPreviewMode` was defined twice in the GRBL JS bundle
// (grbl_2d_controls.h took a `text` arg, grbl_light.h took none). In JS the last
// declaration wins, so renderPreviewFromText received `undefined` instead of a
// mode string and never built the light preview ("No path loaded" bug). The mode
// resolver in grbl_2d_controls.h was renamed to `resolvePreviewMode`.
// Run: node test_grbl_preview_mode.js
const fs = require('fs');
const path = require('path');

const GRBL_DIR = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'grbl');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

var INCLUDE_SET = [
  'grbl_state.h', 'grbl_ui.h', 'grbl_parser_3d.h', 'grbl_parser_worker.h',
  'grbl_2d_render.h', 'grbl_2d_controls.h', 'grbl_contour.h', 'grbl_light.h',
  'grbl_status.h', 'grbl_actions.h', 'grbl_sd.h', 'grbl_init.h'
];

function readSource(fileName) {
  return fs.readFileSync(path.join(GRBL_DIR, fileName), 'utf8');
}

function countDefinitions(fnName) {
  var total = 0;
  INCLUDE_SET.forEach(function (f) {
    var re = new RegExp('function\\s+' + fnName + '\\s*\\(', 'g');
    var text = readSource(f);
    var m;
    while ((m = re.exec(text)) !== null) total++;
  });
  return total;
}

// Extract a top-level function source (balanced braces) from a grbl *.h file.
function extractFunctionSource(fileName, fnName) {
  var text = readSource(fileName);
  var re = new RegExp('function\\s+' + fnName + '\\s*\\(', 'm');
  var m = text.match(re);
  if (!m) throw new Error(fnName + ' not found in ' + fileName);
  var brace = text.indexOf('{', m.index);
  if (brace < 0) throw new Error('no body found for ' + fnName);
  var depth = 1, i = brace + 1;
  while (i < text.length && depth > 0) {
    if (text[i] === '{') depth++;
    else if (text[i] === '}') depth--;
    i++;
  }
  return text.slice(m.index, i);
}

// Build a sandbox where resolvePreviewMode runs against a mock <select> + state.
function makeResolver(selectValue, statePreviewMode) {
  var src = extractFunctionSource('grbl_2d_controls.h', 'resolvePreviewMode');
  var byId = function (id) {
    if (id === 'grbl_preview_mode') return { value: selectValue };
    return null;
  };
  var state = { preview: { previewMode: statePreviewMode } };
  var fn = new Function('byId', 'state', 'GRBL_PREVIEW_MAX_BYTES', 'return ' + src)(
    byId, state, 50 * 1024 * 1024
  );
  return { fn: fn, state: state };
}

function run() {
  console.log('\n=== GRBL Preview Mode Resolution Tests ===\n');

  test('applyPreviewMode defined exactly once across the bundle (no shadowing)', function () {
    is(countDefinitions('applyPreviewMode'), 1, 'grbl_light.h must be the only definition');
  });

  test('resolvePreviewMode defined exactly once and used by renderPreviewFromText', function () {
    is(countDefinitions('resolvePreviewMode'), 1, 'single resolvePreviewMode definition');
    var ctl = readSource('grbl_2d_controls.h');
    ok(/var mode = resolvePreviewMode\(src\);/.test(ctl), 'renderPreviewFromText calls resolvePreviewMode');
    ok(!/applyPreviewMode\(src\)/.test(ctl), 'renderPreviewFromText no longer calls applyPreviewMode(src)');
  });

  test('resolvePreviewMode returns auto for small files (default mode)', function () {
    var r = makeResolver('auto', 'auto');
    is(r.fn('M3 S1000\nG1 X10 Y10'), 'auto');
    is(r.state.preview.previewMode, 'auto');
  });

  test('resolvePreviewMode keeps auto for files above the cap (no outlines fallback)', function () {
    var r = makeResolver('auto', 'auto');
    var big = new Array((52 * 1024 * 1024) + 10).join('a');
    is(r.fn(big), 'auto');
    is(r.state.preview.previewMode, 'auto');
  });

  test('resolvePreviewMode preserves explicit frame mode for small files', function () {
    var r = makeResolver('frame', 'frame');
    is(r.fn('M3 S1000\nG1 X10 Y10'), 'frame');
    is(r.state.preview.previewMode, 'frame');
  });

  test('resolvePreviewMode coerces the removed outlines value back to auto', function () {
    var r = makeResolver('outlines', 'outlines');
    is(r.fn(''), 'auto');
    is(r.state.preview.previewMode, 'auto');
  });

  test('resolvePreviewMode coerces invalid modes back to auto', function () {
    var r = makeResolver('bogus', 'bogus');
    is(r.fn('M3\nG1 X1 Y1'), 'auto');
    is(r.state.preview.previewMode, 'auto');
  });

  test('resolvePreviewMode defaults to auto when no selector present', function () {
    var r = makeResolver(null, '');
    is(r.fn(''), 'auto');
    is(r.state.preview.previewMode, 'auto');
  });

  // ====== applyPreviewMode re-parse on mode switch ======
  function makeApplier(overrides) {
    var src = extractFunctionSource('grbl_light.h', 'applyPreviewMode');
    var calls = { renderPreviewFromText: [], applyLightPreviewFromText: [], fetchSdTextForLightPreview: [] };
    var state = {
      workspaceText: 'G90\nM3 S1000\nG1 X0 Y0\nG1 X100 Y0\nG1 X0 Y0\nM5\n',
      selectedSdPath: '',
      preview: { previewMode: 'auto' }
    };
    var deps = {
      state: state,
      byId: function () { return null; },
      updatePreviewInfo: function () {},
      drawPreview: function () {},
      applyLightPreviewFromText: function (t) { calls.applyLightPreviewFromText.push(String(t)); return true; },
      applyLightPreviewFromBounds: function () {},
      fetchFrameBoundsFromSelectedSd: function () { return Promise.reject(new Error('unexpected fetchFrameBounds')); },
      fetchSdTextForLightPreview: function (p) { calls.fetchSdTextForLightPreview.push(String(p)); return Promise.resolve('SD_GCODE\nM5\n'); },
      renderPreviewFromText: function (t, p) { calls.renderPreviewFromText.push([String(t), String(p)]); }
    };
    for (var k in (overrides || {})) {
      if (k === 'state') { for (var k2 in overrides.state) state[k2] = overrides.state[k2]; }
      else deps[k] = overrides[k];
    }
    var keys = Object.keys(deps);
    var args = keys.map(function (k) { return deps[k]; });
    var ctorArgs = keys.concat(['return ' + src]);
    var Fn = Function.bind.apply(Function, [null].concat(ctorArgs));
    var fn = new Fn().apply(null, args);
    return { fn: fn, state: state, calls: calls };
  }

  var asyncChecks = [];

  test('applyPreviewMode auto with workspace text triggers full re-parse', function () {
    var a = makeApplier({});
    a.state.preview.previewMode = 'auto';
    a.fn();
    is(a.calls.renderPreviewFromText.length, 1, 'renderPreviewFromText must be called');
    is(a.calls.renderPreviewFromText[0][0], a.state.workspaceText);
    is(a.calls.applyLightPreviewFromText.length, 0, 'no light parse for auto');
  });

  test('applyPreviewMode auto with no text fetches SD then re-parses', function () {
    var a = makeApplier({});
    a.state.workspaceText = '';
    a.state.selectedSdPath = '/sd/part.gcode';
    a.state.preview.previewMode = 'auto';
    a.fn();
    asyncChecks.push(new Promise(function (res) { setTimeout(res, 0); }).then(function () {
      is(a.calls.fetchSdTextForLightPreview.length, 1, 'fetchSdText must be called');
      is(a.calls.renderPreviewFromText.length, 1, 're-parses after SD fetch resolves');
      is(a.calls.renderPreviewFromText[0][0], 'SD_GCODE\nM5\n');
    }));
  });

  test('applyPreviewMode non-frame modes trigger the full re-parse (matches auto)', function () {
    var a = makeApplier({});
    a.state.preview.previewMode = 'outlines';
    a.fn();
    is(a.calls.renderPreviewFromText.length, 1, 'renderPreviewFromText must be called for non-frame modes');
    is(a.calls.applyLightPreviewFromText.length, 0, 'no light parse for non-frame modes');
  });

  var total = passed + failed;
  Promise.all(asyncChecks).then(function () {
    var total = passed + failed;
    console.log('\n  Results: ' + total + '/' + total + ' passed, ' + failed + ' failed\n');
    process.exit(failed > 0 ? 1 : 0);
  }).catch(function (e) {
    failed++;
    console.log('  \u2717 async applyPreviewMode check: ' + e.message);
    var total = passed + failed;
    console.log('\n  Results: ' + total + '/' + total + ' passed, ' + failed + ' failed\n');
    process.exit(1);
  });
}

run();
