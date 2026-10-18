// GRBL Parse Progress Throttle Tests
// Covers _startParse (grbl_2d_controls.h): during a long G-code parse the
// progress callback must keep updating state (points/segments) but must NOT
// re-render the full path on every chunk — that was O(N^2) for large files.
// Run: node test_grbl_parse_throttle.js
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

function readSource(fileName) {
  return fs.readFileSync(path.join(GRBL_DIR, fileName), 'utf8');
}

function extractFunctionSource(fileName, fnName) {
  var text = readSource(fileName);
  var re = new RegExp('function\\s+' + fnName + '\\s*\\(', 'm');
  var m = text.match(re);
  if (!m) throw new Error(fnName + ' not found in ' + fileName);
  var start = m.index;
  if (text.slice(Math.max(0, start - 6), start) === 'async ') start -= 6;
  var brace = text.indexOf('{', m.index);
  if (brace < 0) throw new Error('no body found for ' + fnName);
  var depth = 1, i = brace + 1;
  while (i < text.length && depth > 0) {
    if (text[i] === '{') depth++;
    else if (text[i] === '}') depth--;
    i++;
  }
  return text.slice(start, i);
}

function sandbox(fileName, fnName, deps) {
  var src = extractFunctionSource(fileName, fnName);
  var keys = Object.keys(deps);
  var args = keys.map(function (k) { return deps[k]; });
  var ctorArgs = keys.concat(['return ' + src]);
  var Fn = Function.bind.apply(Function, [null].concat(ctorArgs));
  return new Fn().apply(null, args);
}

function makeState() {
  return {
    preview: {
      parseJobId: 0, parseBusy: false, parseProgress: 0, parseTruncated: false,
      points: [], segments: [], hasPath: false, maxLine: 0,
      scrubEnabled: false, scrubLine: 0, currentLine: 0
    }
  };
}

// Mock parseToolpath3dAsync that drives onProgress synchronously in 2% steps
// (51 calls) with a growing segments array, then resolves.
function mockParse(calls) {
  return function (text, onProgress, jobId) {
    var segs = [];
    for (var p = 0; p <= 100; p += 2) {
      segs.push({ lineNo: segs.length + 1, a: 0, b: segs.length + 1 });
      calls.push(p);
      onProgress(p, { points: [], segments: segs, bounds: null });
    }
    return Promise.resolve({ points: [], segments: segs, maxExec: 100, truncated: false, bounds: null });
  };
}

function makeHarness() {
  var drawCalls = 0;
  var progressCalls = [];
  var state = makeState();
  var startParse = sandbox('grbl_2d_controls.h', '_startParse', {
    state: state,
    updatePreviewInfo: function () {},
    drawPreview: function () { drawCalls++; },
    updateScrubControls: function () {},
    lastLineNo: function (segs) { return (segs && segs.length) ? (segs[segs.length - 1].lineNo || 0) : 0; },
    parseToolpath3dAsync: mockParse(progressCalls),
    fitPreviewToBounds: function () {},
    _syncContoursFromSegments: function () {}
  });
  return { startParse: startParse, state: state, drawCalls: function () { return drawCalls; }, progressCalls: progressCalls };
}

var asyncChecks = [];

function run() {
  console.log('\n=== GRBL Parse Progress Throttle Tests ===\n');

  test('_startParse updates state every chunk but throttles drawPreview', function () {
    var h = makeHarness();
    h.startParse('G90\nM3\nG1 X1 Y1\n');
    asyncChecks.push(new Promise(function (res) { setTimeout(res, 0); }).then(function () {
      is(h.progressCalls.length, 51, 'mock drives 51 progress calls');
      is(h.state.preview.segments.length, 51, 'segments state updated on every chunk');
      is(h.state.preview.maxLine, 51, 'maxLine follows last segment');
      // Without throttling drawPreview would run ~51+2 times; throttling keeps
      // it far lower while still drawing during parse and at completion.
      ok(h.drawCalls() < 40, 'drawPreview throttled (called ' + h.drawCalls() + ' times)');
      ok(h.drawCalls() >= 3, 'still redraws during parse and at completion (called ' + h.drawCalls() + ' times)');
    }));
  });

  test('_startParse completes cleanly: busy cleared, progress 100, final redraw', function () {
    var h = makeHarness();
    h.startParse('G90\nM3\nG1 X1 Y1\n');
    asyncChecks.push(new Promise(function (res) { setTimeout(res, 0); }).then(function () {
      is(h.state.preview.parseBusy, false, 'parseBusy cleared after completion');
      is(h.state.preview.parseProgress, 100, 'progress reaches 100');
      ok(h.state.preview.hasPath, 'hasPath set after parse');
      ok(h.drawCalls() >= 1, 'final drawPreview happened');
    }));
  });

  // ====== parser adaptive downsampling ======
  function extractAxisValue(line, axis) {
    var rx = new RegExp(axis + '\\s*(-?[0-9]*\\.?[0-9]+)', 'i');
    var m = line.match(rx);
    if (!m || m.length < 2) return null;
    var v = parseFloat(m[1]);
    return isNaN(v) ? null : v;
  }

  function makeParser() {
    var state = { preview: { arcQuality: 'med', parseJobId: 1 } };
    var fn = sandbox('grbl_parser_3d.h', 'parseToolpath3dAsync', {
      state: state,
      _isEsp32Browser: false,
      _isLowResourceDevice: function () { return false; },
      extractAxisValue: extractAxisValue,
      yieldToUi: function () { return Promise.resolve(); }
    });
    return { fn: fn };
  }

  function buildRaster(lines, prefix) {
    var out = prefix || '';
    for (var i = 0; i < lines; i++) out += 'G1 X1Y0\n';
    return 'G91\n' + out;
  }

  test('large engrave file is downsampled to ~pointBudget segments', function () {
    var p = makeParser();
    asyncChecks.push(new Promise(function (res) { setTimeout(res, 0); }).then(function () {
      return p.fn(buildRaster(600000, ''), function () {}, 1).then(function (out) {
        ok(out, 'parse should not return null');
        is(out.truncated, false, 'strided storage never truncates');
        ok(out.segments.length < 300000, 'stored segments stay near budget (got ' + out.segments.length + ')');
        ok(out.segments.length > 50000, 'still stores a good portion (got ' + out.segments.length + ')');
      });
    }));
  });

  test('cut outlines are never downsampled', function () {
    var p = makeParser();
    asyncChecks.push(new Promise(function (res) { setTimeout(res, 0); }).then(function () {
      return p.fn(buildRaster(600000, 'M3\n'), function () {}, 1).then(function (out) {
        ok(out, 'parse should not return null');
        ok(out.segments.length >= 590000, 'all cut segments kept (got ' + out.segments.length + ')');
      });
    }));
  });

  test('Layer comments do not force cut classification on raster sections', function () {
    var p = makeParser();
    var src = '; Image @ 1000 mm/min\n; Layer C29\nG91\nG1 X1Y0\nG1 X1Y0\nG1 X1Y0\n';
    asyncChecks.push(new Promise(function (res) { setTimeout(res, 0); }).then(function () {
      return p.fn(src, function () {}, 1).then(function (out) {
        ok(out, 'parse should not return null');
        ok(out.segments.length >= 3, 'kept the raster lines (got ' + out.segments.length + ')');
        var allEngrave = true;
        for (var i = 0; i < out.segments.length; i++) {
          if (out.segments[i].cutType !== 'engrave') allEngrave = false;
        }
        ok(allEngrave, 'raster G1 moves are engrave, not cut (after ; Image @ + ; Layer)');
      });
    }));
  });

  test('alternating raster keeps both G0 and G1 polarities when strided', function () {
    var p = makeParser();
    var src = 'G91\n';
    for (var li = 0; li < 600000; li++) {
      src += (li % 2 === 0) ? 'G0 X1Y0\n' : 'G1 X1Y0\n';
    }
    asyncChecks.push(new Promise(function (res) { setTimeout(res, 0); }).then(function () {
      return p.fn(src, function () {}, 1).then(function (out) {
        ok(out, 'parse should not return null');
        var rapid = 0, engrave = 0;
        for (var i = 0; i < out.segments.length; i++) {
          if (out.segments[i].rapid) rapid++;
          else if (out.segments[i].cutType === 'engrave') engrave++;
        }
        ok(rapid > 0, 'rapid returns still sampled (got ' + rapid + ')');
        ok(engrave > 0, 'engrave lines still sampled (got ' + engrave + ')');
        ok(out.segments.length < 400000, 'strided near budget (got ' + out.segments.length + ')');
      });
    }));
  });

  var total = passed + failed;
  Promise.all(asyncChecks).then(function () {
    var total = passed + failed;
    console.log('\n  Results: ' + total + '/' + total + ' passed, ' + failed + ' failed\n');
    process.exit(failed > 0 ? 1 : 0);
  }).catch(function (e) {
    failed++;
    console.log('  \u2717 async throttle check: ' + e.message);
    var total = passed + failed;
    console.log('\n  Results: ' + total + '/' + total + ' passed, ' + failed + ' failed\n');
    process.exit(1);
  });
}

run();
