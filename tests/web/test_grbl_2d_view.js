// GRBL 2D View Transform Tests
// Covers the permanent mirror-Y transform fix (parts render flipped because
// screen Y grows down, so Y is always negated), the scrub playhead segment
// filter and bounds computation in the 2D renderer.
// Run: node test_grbl_2d_view.js
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
function approx(a, b, t) { if (Math.abs(a - b) > (t || 0.001)) throw new Error('expected ' + b + ' \u00b1' + (t||0.001) + ' got ' + a); }

function readSource(fileName) {
  return fs.readFileSync(path.join(GRBL_DIR, fileName), 'utf8');
}

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

function sandbox(fnName, deps) {
  return sandboxFrom('grbl_2d_render.h', fnName, deps);
}

function sandboxFrom(fileName, fnName, deps) {
  var src = extractFunctionSource(fileName, fnName);
  var keys = Object.keys(deps);
  var args = keys.map(function (k) { return deps[k]; });
  var ctorArgs = keys.concat(['return ' + src]);
  var Fn = Function.bind.apply(Function, [null].concat(ctorArgs));
  return new Fn().apply(null, args);
}

function makePv(overrides) {
  var pv = {
    points: [],
    lightPreview: null,
    previewMode: 'auto',
    fitScale: 1,
    scale: 1,
    panX: 0,
    panY: 0,
    scrubEnabled: false,
    scrubLine: 0
  };
  for (var k in overrides) { pv[k] = overrides[k]; }
  return pv;
}

function makeSegs() {
  return [
    { a: {x:0,y:0}, b: {x:10,y:0}, lineNo: 1, rapid: false, cutType: 'cut' },
    { a: {x:10,y:0}, b: {x:10,y:10}, lineNo: 2, rapid: false, cutType: 'cut' },
    { a: {x:10,y:10}, b: {x:0,y:10}, lineNo: 3, rapid: false, cutType: 'engrave' },
    { a: {x:0,y:10}, b: {x:0,y:0}, lineNo: 4, rapid: false, cutType: 'cut' }
  ];
}

function run() {
  console.log('\n=== GRBL 2D View Transform Tests ===\n');

  // ====== _viewMappedY ======
  {
    var viewMappedY = sandbox('_viewMappedY', {});

    test('_viewMappedY always negates Y (permanent mirror fix)', function () {
      is(viewMappedY(makePv({}), 42), -42);
      is(viewMappedY(makePv({}), -10), 10);
    });
    test('_viewMappedY keeps zero at zero', function () {
      is(viewMappedY(makePv({}), 0), 0);
    });
  }

  // ====== _viewTransformBounds ======
  {
    var viewTransformBounds = sandbox('_viewTransformBounds', {});
    var SQ = { minX: 0, maxX: 100, minY: 0, maxY: 100 };

    test('bounds keep X, Y mirrored by the permanent fix', function () {
      var b = viewTransformBounds(makePv({}), SQ);
      approx(b.minX, 0); approx(b.maxX, 100); approx(b.minY, -100); approx(b.maxY, 0);
      approx(b.spanX, 100); approx(b.spanY, 100);
    });
    test('non-square rect keeps spans with Y flipped', function () {
      var b = viewTransformBounds(makePv({}), { minX: 0, maxX: 200, minY: 0, maxY: 50 });
      approx(b.minX, 0); approx(b.maxX, 200); approx(b.minY, -50); approx(b.maxY, 0);
      approx(b.spanX, 200); approx(b.spanY, 50);
    });
    test('negative-X bounds mirror Y without changing X', function () {
      var b = viewTransformBounds(makePv({}), { minX: -30, maxX: 10, minY: 5, maxY: 25 });
      approx(b.minX, -30); approx(b.maxX, 10); approx(b.minY, -25); approx(b.maxY, -5);
    });
  }

  // ====== _computeBounds ======
  {
    var st = { maxTravel: null };
    var computeBounds = sandbox('_computeBounds', { state: st });

    test('_computeBounds uses points min/max when present', function () {
      var pv = makePv({ points: [{x:0,y:0,z:0},{x:50,y:80,z:-3},{x:-10,y:20,z:5}] });
      st.maxTravel = null;
      var b = computeBounds(pv);
      approx(b.minX, -10); approx(b.maxX, 50); approx(b.minY, 0); approx(b.maxY, 80);
      approx(b.minZ, -3); approx(b.maxZ, 5);
    });
    test('_computeBounds falls back to lightPreview when no points', function () {
      var pv = makePv({ points: [], lightPreview: { minX: 5, maxX: 15, minY: 6, maxY: 16 } });
      st.maxTravel = null;
      var b = computeBounds(pv);
      approx(b.minX, 5); approx(b.maxX, 15); approx(b.minY, 6); approx(b.maxY, 16);
    });
    test('_computeBounds falls back to maxTravel when neither present', function () {
      var pv = makePv({ points: [], lightPreview: null });
      st.maxTravel = { x: 200, y: 300 };
      var b = computeBounds(pv);
      approx(b.minX, 0); approx(b.maxX, 200); approx(b.minY, 0); approx(b.maxY, 300);
    });
    test('_computeBounds defaults when nothing available', function () {
      var pv = makePv({ points: [], lightPreview: null });
      st.maxTravel = null;
      var b = computeBounds(pv);
      approx(b.minX, -50); approx(b.maxX, 50); approx(b.minY, -50); approx(b.maxY, 50);
    });
  }

  // ====== _playheadSegs ======
  {
    var state2 = { preview: makePv({}) };
    var playheadSegs = sandbox('_playheadSegs', { state: state2, parseInt: parseInt, isNaN: isNaN });

    test('_playheadSegs returns all when scrub disabled', function () {
      state2.preview.scrubEnabled = false;
      var segs = makeSegs();
      is(playheadSegs(segs), segs);
    });
    test('_playheadSegs returns [] when scrub enabled and limit 0', function () {
      state2.preview.scrubEnabled = true; state2.preview.scrubLine = 0;
      is(playheadSegs(makeSegs()).length, 0);
    });
    test('_playheadSegs truncates at limit and stops at first over-limit line', function () {
      state2.preview.scrubEnabled = true; state2.preview.scrubLine = 2;
      var out = playheadSegs(makeSegs());
      is(out.length, 2);
      is(out[0].lineNo, 1);
      is(out[1].lineNo, 2);
    });
    test('_playheadSegs returns all when limit exceeds max lineNo', function () {
      state2.preview.scrubEnabled = true; state2.preview.scrubLine = 999;
      is(playheadSegs(makeSegs()).length, 4);
    });
    test('_playheadSegs handles empty segments', function () {
      state2.preview.scrubEnabled = true; state2.preview.scrubLine = 2;
      is(playheadSegs([]).length, 0);
    });
  }

  // ====== resetPreviewSim ======
  {
    function makeResetHarness() {
      var calls = { resetPreviewView: 0, drawPreview: 0 };
      var pst = { scrubPlaying: true, scrubEnabled: true, scrubLine: 500, currentLine: 42, parseBusy: false, points: [], segments: [] };
      var state = { preview: pst };
      var resetPreviewSim = sandboxFrom('grbl_2d_controls.h', 'resetPreviewSim', {
        state: state,
        resetPreviewView: function () { calls.resetPreviewView++; },
        updatePreviewInfo: function () {},
        updateScrubControls: function () {},
        drawPreview: function () { calls.drawPreview++; }
      });
      return { fn: resetPreviewSim, state: state, calls: calls };
    }

    test('resetPreviewSim stops play, disables scrub, shows full path and resets view', function () {
      var h = makeResetHarness();
      h.fn();
      is(h.state.preview.scrubPlaying, false);
      is(h.state.preview.scrubEnabled, false);
      is(h.state.preview.scrubLine, 42, 'scrub line returns to currentLine');
      is(h.calls.resetPreviewView, 1);
      is(h.calls.drawPreview, 1);
    });
    test('resetPreviewSim handles missing currentLine', function () {
      var h = makeResetHarness();
      h.state.preview.currentLine = 0;
      h.fn();
      is(h.state.preview.scrubLine, 0);
    });
  }

  // ====== _drawSegments render stride ======
  {
    function makeCtx() {
      return {
        moveToCalls: 0, lineToCalls: 0, strokeCalls: 0, beginPathCalls: 0,
        moveTo: function () { this.moveToCalls++; },
        lineTo: function () { this.lineToCalls++; },
        stroke: function () { this.strokeCalls++; },
        beginPath: function () { this.beginPathCalls++; },
        setLineDash: function () {}, set lineWidth(v) {}, get lineWidth() { return 1; },
        set strokeStyle(v) {}, get strokeStyle() { return ''; },
        set lineCap(v) {}, set lineJoin(v) {}
      };
    }
    function buildSegs(n, cutType) {
      var out = [];
      for (var i = 0; i < n; i++) {
        out.push({ a: { x: i, y: i, z: 0 }, b: { x: i + 1, y: i + 1, z: 0 }, rapid: false, cutType: cutType, lineNo: i + 1 });
      }
      return out;
    }
    var drawSt = { workspaceText: '' };
    var drawSegments = sandboxFrom('grbl_2d_render.h', '_drawSegments', {
      _segStyle: sandboxFrom('grbl_2d_render.h', '_segStyle', {}),
      _effectivePreviewLine: function () { return 0; },
      _drawSegmentsJob: function () { throw new Error('job render path must not run without currentLine'); },
      state: drawSt
    });
    var idMap = function (x, y, z) { return { x: x, y: y, z: z || 0 }; };
    var MB = 1024 * 1024;

    test('_drawSegments draws every cut segment (contours stay intact)', function () {
      drawSt.workspaceText = 'x'.repeat(1 * MB);
      var ctx = makeCtx();
      drawSegments(ctx, buildSegs(100, 'cut'), 1, idMap);
      is(ctx.lineToCalls, 100);
      is(ctx.moveToCalls, 1, 'connected path uses a single moveTo');
      is(ctx.strokeCalls, 1, 'one style run strokes once');
    });
    test('_drawSegments draws all engrave lines for small files (<=4MB)', function () {
      drawSt.workspaceText = 'x'.repeat(1 * MB);
      var ctx = makeCtx();
      drawSegments(ctx, buildSegs(1000, 'engrave'), 1, idMap);
      is(ctx.lineToCalls, 1000, 'small files keep every line');
    });
    test('_drawSegments keeps 3 of every 4 engrave lines above 4MB', function () {
      drawSt.workspaceText = 'x'.repeat(6 * MB);
      var ctx = makeCtx();
      drawSegments(ctx, buildSegs(1000, 'engrave'), 1, idMap);
      approx(ctx.lineToCalls, 750, 2, 'three of every four engrave lines are drawn');
      is(ctx.moveToCalls, 1, 'path stays connected');
      is(ctx.strokeCalls, 1);
    });
    test('_drawSegments keeps 1 of 2 engrave lines above 10MB', function () {
      drawSt.workspaceText = 'x'.repeat(12 * MB);
      var ctx = makeCtx();
      drawSegments(ctx, buildSegs(1000, 'engrave'), 1, idMap);
      approx(ctx.lineToCalls, 500, 2, 'half the engrave lines are drawn');
    });
    test('_drawSegments keeps short engrave runs intact', function () {
      drawSt.workspaceText = 'x'.repeat(12 * MB);
      var ctx = makeCtx();
      drawSegments(ctx, buildSegs(20, 'engrave'), 1, idMap);
      is(ctx.lineToCalls, 20, 'short engrave runs are not decimated');
    });
    test('_drawSegments keeps huge cut files fully drawn (no decimation)', function () {
      drawSt.workspaceText = 'x'.repeat(1 * MB);
      var ctx = makeCtx();
      drawSegments(ctx, buildSegs(300000, 'cut'), 1, idMap);
      is(ctx.lineToCalls, 300000, 'cut contours are never decimated');
      is(ctx.strokeCalls, 1);
    });
    test('_drawSegments applies the Cut/Rapid/Engrave view filters', function () {
      drawSt.workspaceText = 'x'.repeat(1 * MB);
      drawSt.preview = null;
      var rapids = [];
      for (var i = 0; i < 50; i++) rapids.push({ a: { x: i, y: i, z: 0 }, b: { x: i + 1, y: i + 1, z: 0 }, rapid: true, cutType: '', lineNo: i + 1 });
      drawSt.preview = { showRapid: false, showCut: true, showEngrave: true };
      var c1 = makeCtx();
      drawSegments(c1, rapids, 1, idMap);
      is(c1.lineToCalls, 0, 'rapids hidden when the Rapid checkbox is off');
      drawSt.preview = { showRapid: true, showCut: false, showEngrave: true };
      var c2 = makeCtx();
      drawSegments(c2, buildSegs(50, 'cut'), 1, idMap);
      is(c2.lineToCalls, 0, 'cuts hidden when the Cut checkbox is off');
      drawSt.preview = { showRapid: true, showCut: true, showEngrave: false };
      var c3 = makeCtx();
      drawSegments(c3, buildSegs(50, 'engrave'), 1, idMap);
      is(c3.lineToCalls, 0, 'engraving hidden when the Engrave checkbox is off');
      drawSt.preview = null;
    });
  }

  // ====== Z-axis filter (2-axis machines must never send Z) ======
  {
    function makeZState(axisCount) {
      return { lastStatus: axisCount ? { axis_count: axisCount } : null };
    }
    function hasZFor(state) {
      var src = extractFunctionSource('grbl_actions.h', '_hasZAxis');
      var Fn = new Function('state', 'return ' + src);
      return Fn(state)();
    }
    // Rebind _hasZAxis to read from our per-test state.
    function filt(state, cmd) {
      var hz = function () { return !!(state.lastStatus && parseInt(state.lastStatus.axis_count, 10) >= 3); };
      var src = extractFunctionSource('grbl_actions.h', '_filterAxisCommand');
      var Fn = new Function('state', '_hasZAxis', 'return ' + src);
      return Fn(state, hz)(cmd);
    }

    test('_hasZAxis false for 2-axis, true for 3-axis/unknown', function () {
      is(hasZFor(makeZState(2)), false);
      is(hasZFor(makeZState(3)), true);
      is(hasZFor(makeZState(0)), true, 'unknown machine assumes 3-axis');
    });

    test('2-axis machine strips Z from commands', function () {
      var s = makeZState(2);
      is(filt(s, 'G92 X0 Y0 Z0'), 'G92 X0 Y0');
      is(filt(s, 'G1 X10 Z-5'), 'G1 X10');
      is(filt(s, 'G0 Z5'), 'G0');
    });
    test('2-axis machine drops a Z-only $J jog and keeps $X/$H', function () {
      var s = makeZState(2);
      is(filt(s, '$J=G91 G21 Z-5 F1000'), '', 'Z-only jog dropped');
      is(filt(s, '$J=G91 G21 X-5 F1000'), '$J=G91 G21 X-5 F1000', 'XY jog kept');
      is(filt(s, '$X'), '$X');
      is(filt(s, '$H'), '$H');
    });
    test('3-axis machine passes Z through untouched', function () {
      var s = makeZState(3);
      is(filt(s, 'G92 X0 Y0 Z0'), 'G92 X0 Y0 Z0');
      is(filt(s, '$J=G91 G21 Z-5 F1000'), '$J=G91 G21 Z-5 F1000');
    });
  }

  // ====== Job-aware preview (done vs remaining) ======
  {
    var segStyle2 = sandboxFrom('grbl_2d_render.h', '_segStyle', {});
    var strokePath = sandboxFrom('grbl_2d_render.h', '_strokePath', {});
    var effectiveLine = sandboxFrom('grbl_2d_render.h', '_effectivePreviewLine', {});
    var segmentExecNo = sandboxFrom('grbl_2d_render.h', 'getSegmentExecNo', {});
    var activeExecNo = sandboxFrom('grbl_2d_render.h', 'getActiveExecNo', { getSegmentExecNo: segmentExecNo });
    var jobState = { workspaceText: '' };

    function makeJobCtx() {
      var strokes = [], arcs = [];
      var ctx = {
        strokes: strokes, arcs: arcs, lineToCalls: 0,
        set strokeStyle(v) { this._ss = v; },
        get strokeStyle() { return this._ss; },
        set lineWidth(v) {}, get lineWidth() { return 1; },
        set lineCap(v) {}, set lineJoin(v) {},
        setLineDash: function (d) { this._dash = d; },
        beginPath: function () {},
        moveTo: function (x, y) { this._mx = x; this._my = y; },
        lineTo: function (x, y) { this._lx = x; this._ly = y; this.lineToCalls++; },
        stroke: function () { strokes.push({ style: this._ss, dash: this._dash, lx: this._lx, ly: this._ly }); },
        arc: function (cx, cy, r) { arcs.push({ cx: cx, cy: cy, r: r }); },
        fill: function () {}
      };
      return ctx;
    }

    function makeJobSegs() {
      return [
        { a: { x: 0, y: 0, z: 0 }, b: { x: 10, y: 0, z: 0 }, rapid: true, cutType: '', lineNo: 1, execNo: 1 },
        { a: { x: 10, y: 0, z: 0 }, b: { x: 10, y: 10, z: 0 }, rapid: false, cutType: 'cut', lineNo: 2, execNo: 2 },
        { a: { x: 10, y: 10, z: 0 }, b: { x: 0, y: 10, z: 0 }, rapid: false, cutType: 'engrave', lineNo: 3, execNo: 3 },
        { a: { x: 0, y: 10, z: 0 }, b: { x: 0, y: 0, z: 0 }, rapid: false, cutType: 'cut', lineNo: 4, execNo: 4 }
      ];
    }
    var idMap = function (x, y, z) { return { x: x, y: y, z: z || 0 }; };

    function jobRender(currentLine, segs, stateOverrides) {
      jobState.preview = { currentLine: currentLine, showCut: true, showRapid: true, showEngrave: true };
      for (var k in (stateOverrides || {})) jobState.preview[k] = stateOverrides[k];
      var fn = sandboxFrom('grbl_2d_render.h', '_drawSegmentsJob', {
        _segStyle: segStyle2,
        _strokePath: strokePath,
        getActiveExecNo: activeExecNo,
        getSegmentExecNo: segmentExecNo,
        state: jobState
      });
      var ctx = makeJobCtx();
      fn(ctx, segs || makeJobSegs(), 1, idMap, currentLine);
      jobState.preview = null;
      return ctx;
    }

    test('_effectivePreviewLine uses scrubLine while scrubbing', function () {
      is(effectiveLine({ scrubEnabled: true, scrubLine: 3, currentLine: 9 }), 3);
      is(effectiveLine({ scrubEnabled: false, scrubLine: 3, currentLine: 9 }), 9);
    });
    test('_effectivePreviewLine returns 0 for zero/negative/NaN', function () {
      is(effectiveLine({ scrubEnabled: false, currentLine: 0 }), 0);
      is(effectiveLine({ scrubEnabled: false, currentLine: -1 }), 0);
      is(effectiveLine({ scrubEnabled: false, currentLine: NaN }), 0);
      is(effectiveLine({ scrubEnabled: true, scrubLine: -2 }), 0);
    });

    test('job render dims done, highlights current, keeps remaining bright', function () {
      var ctx = jobRender(2);
      is(ctx.strokes.length, 5, 'past rapid + remaining cut/engrave + current glow/core');
      is(ctx.strokes[0].style, 'rgba(245,158,11,0.4)', 'done rapid is dim');
      is(ctx.strokes[1].style, 'rgba(239,68,68,0.95)', 'remaining cut stays full red');
      is(ctx.strokes[2].style, 'rgba(34,197,94,0.95)', 'remaining engrave stays bright');
      is(ctx.strokes[3].style, 'rgba(12,74,110,0.96)', 'current segment gets the glow pass');
      is(ctx.strokes[4].style, '#22d3ee', 'current segment gets the bright core');
      is(ctx.arcs.length, 1, 'head dot drawn once');
      is(ctx.arcs[0].cx, 10); is(ctx.arcs[0].cy, 10);
    });

    test('job render with currentLine 0 puts every segment in remaining', function () {
      var ctx = jobRender(0);
      is(ctx.strokes.length, 3, 'only the three remaining buckets are drawn');
      is(ctx.strokes[0].style, 'rgba(245,158,11,0.95)');
      is(ctx.strokes[1].style, 'rgba(239,68,68,0.95)', 'remaining cut stays full red');
      is(ctx.strokes[2].style, 'rgba(34,197,94,0.95)');
      is(ctx.arcs.length, 0, 'no head dot when nothing is current');
    });

    test('job render honors the Cut/Rapid/Engrave checkboxes', function () {
      var ctx = jobRender(2, null, { showCut: false });
      is(ctx.strokes.length, 2, 'cut buckets (past + current) are hidden');
      is(ctx.strokes[0].style, 'rgba(245,158,11,0.4)', 'done rapid still dim');
      is(ctx.strokes[1].style, 'rgba(34,197,94,0.95)', 'remaining engrave still bright');
      is(ctx.arcs.length, 0, 'current cut segment hidden so no head dot');
    });

    test('job render decimates long engrave runs on big files', function () {
      jobState.workspaceText = 'x'.repeat(6 * 1024 * 1024);
      var segs = [];
      for (var i = 0; i < 1000; i++) {
        segs.push({ a: { x: i, y: i, z: 0 }, b: { x: i + 1, y: i + 1, z: 0 }, rapid: false, cutType: 'engrave', lineNo: i + 1, execNo: i + 1 });
      }
      var ctx = jobRender(999999, segs);
      is(ctx.strokes[0].style, 'rgba(34,197,94,0.42)', 'done engrave is dim');
      approx(ctx.lineToCalls, 750, 2, 'big engrave file keeps 3 of 4 in job mode too');
      jobState.workspaceText = '';
    });

    test('_drawSegments delegates to job renderer only when a line is active', function () {
      var delegated = 0;
      var drawSeg = sandboxFrom('grbl_2d_render.h', '_drawSegments', {
        _segStyle: segStyle2,
        _effectivePreviewLine: effectiveLine,
        _drawSegmentsJob: function () { delegated++; },
        state: jobState
      });
      var ctx = makeJobCtx();
      jobState.preview = { currentLine: 5 };
      drawSeg(ctx, makeJobSegs(), 1, idMap);
      is(delegated, 1, 'running job delegates to the job renderer');
      jobState.preview = { currentLine: 0 };
      drawSeg(ctx, makeJobSegs(), 1, idMap);
      is(delegated, 1, 'no delegation when no line is active');
      jobState.preview = null;
    });
  }

  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run();
