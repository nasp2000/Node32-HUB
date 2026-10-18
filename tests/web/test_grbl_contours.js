// GRBL Contour Selection (light preview)
// Verifies the contour hit-test math, the screen->machine inverse mapping,
// contourRanges alignment after parsing, click toggling, Tab/Space navigation,
// and the Select/Undo toggle logic. Regression for the original bug where
// _handleCanvasContourClick referenced an undefined `availH` and threw a
// ReferenceError on every click (selection never worked).
// Run: node test_grbl_contours.js

const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'grbl_controller.html');
var passed = 0, failed = 0, pendingAsync = 0;

function finishIfDone() {
  if (pendingAsync > 0) return;
  console.log('\n' + passed + '/' + (passed + failed) + ' passed');
  process.exit(failed ? 1 : 0);
}

function test(name, fn) {
  if (fn.length >= 1) {
    pendingAsync++;
    var doneFlag = false;
    var timer = setTimeout(function() {
      if (doneFlag) return;
      doneFlag = true;
      pendingAsync--;
      failed++; console.log('  \u2717 ' + name + ': timeout');
      finishIfDone();
    }, 8000);
    fn(function(err) {
      if (doneFlag) return;
      doneFlag = true;
      clearTimeout(timer);
      pendingAsync--;
      if (err) { failed++; console.log('  \u2717 ' + name + ': ' + err.message); }
      else { passed++; console.log('  \u2713 ' + name); }
      finishIfDone();
    });
    return;
  }
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }
function near(a, b, tol, m) { if (Math.abs(a - b) > (tol || 1e-6)) throw new Error(m || ('expected ' + b + ' got ' + a)); }

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

// Standard controlled preview used by hit-test cases.
function setupPreview(dom, contours, opts) {
  var T = dom.window.__test__;
  var doc = dom.window.document;
  var lp = {
    minX: 0, maxX: 100, minY: 0, maxY: 100,
    contours: contours,
    contourRanges: opts && opts.ranges
  };
  T.state.preview = {
    lightPreview: lp,
    contourSelectEnabled: false,
    contourDisabled: [],
    contourUndoStack: [],
    contourHighlight: -1,
    scale: 1, fitScale: 1, panX: 0, panY: 0
  };
  var canvas = doc.getElementById('grbl_preview_canvas');
  canvas.width = 300; canvas.height = 300;
  canvas.getBoundingClientRect = function() {
    return { left: 0, top: 0, width: 300, height: 300, right: 300, bottom: 300 };
  };
  return { T: T, doc: doc, canvas: canvas };
}

// Mock 2D context that records every stroke/fill so the overlay colors can be
// asserted. setLineDash must not leak into the next stroke (as in the real
// canvas), so each stroke captures the dash list set before it.
function makeOverlayCtx() {
  var strokes = [], fills = [];
  var ctx = {
    strokes: strokes, fills: fills,
    set strokeStyle(v) { this._ss = v; },
    get strokeStyle() { return this._ss; },
    set lineWidth(v) {}, get lineWidth() { return 1; },
    set lineCap(v) {}, set lineJoin(v) {},
    set shadowColor(v) {}, set shadowBlur(v) {},
    set fillStyle(v) { this._fs = v; }, get fillStyle() { return this._fs; },
    save: function () {},
    restore: function () {},
    setLineDash: function (d) { this._dash = d || []; },
    beginPath: function () {},
    moveTo: function () {},
    lineTo: function () {},
    closePath: function () {},
    stroke: function () { strokes.push({ style: this._ss, dash: this._dash || [] }); this._dash = undefined; },
    fill: function () { fills.push(this._fs); }
  };
  return ctx;
}

// Full 2D context stub for exercising _drawTopView end-to-end (the live page
// renders with maxTravel unset on boards without a configured travel, which
// used to throw `margin is not defined` in the fallback branch).
function makeFullCtx() {
  var strokes = [], fills = [];
  var ctx = {
    strokes: strokes, fills: fills,
    set strokeStyle(v) { this._ss = v; },
    get strokeStyle() { return this._ss; },
    set fillStyle(v) { this._fs = v; }, get fillStyle() { return this._fs; },
    set lineWidth(v) {}, set lineCap(v) {}, set lineJoin(v) {},
    set font(v) {}, set textAlign(v) {}, set textBaseline(v) {},
    setTransform: function () {},
    fillRect: function () {},
    strokeRect: function () {},
    setLineDash: function (d) { this._dash = d || []; },
    measureText: function (t) { return { width: (t || '').length * 7 }; },
    beginPath: function () { this._p = this._p || []; },
    moveTo: function (x, y) { this._p = this._p || []; this._p.push({ op: 'M', x: x, y: y }); },
    lineTo: function (x, y) { this._p.push({ op: 'L', x: x, y: y }); },
    closePath: function () { this._p.push({ op: 'Z' }); },
    arc: function () {},
    fillText: function () {},
    save: function () {},
    restore: function () {},
    stroke: function () { strokes.push({ style: this._ss, w: this.lineWidth, p: this._p || [] }); this._p = []; },
    fill: function () { fills.push({ style: this._fs }); }
  };
  return ctx;
}

async function run() {
  console.log('\n=== GRBL Contour Selection Tests ===\n');

  {
    var dom = loadPage();
    var T = dom.window.__test__;

    test('contour helpers are exposed', function () {
      ok(typeof T._minDistToContour === 'function', '_minDistToContour');
      ok(typeof T._distToSegment === 'function', '_distToSegment');
      ok(typeof T._pointInPolygon === 'function', '_pointInPolygon');
      ok(typeof T._screenToMachine === 'function', '_screenToMachine');
      ok(typeof T._handleCanvasContourClick === 'function', '_handleCanvasContourClick');
      ok(typeof T._navContourHighlight === 'function', '_navContourHighlight');
      ok(typeof T._spaceToggleHighlighted === 'function', '_spaceToggleHighlighted');
      ok(typeof T._toggleContourSelect === 'function', '_toggleContourSelect');
      ok(typeof T._undoContourToggle === 'function', '_undoContourToggle');
      ok(typeof T.buildLightPreview === 'function', 'buildLightPreview');
      ok(typeof T.renderPreviewFromText === 'function', 'renderPreviewFromText');
      ok(typeof T._drawContourOverlay === 'function', '_drawContourOverlay');
      ok(typeof T._drawCrosshair === 'function', '_drawCrosshair');
      ok(typeof T._drawTopView === 'function', '_drawTopView');
      ok(typeof T._drawIsoView === 'function', '_drawIsoView');
    });

    test('_drawTopView renders without maxTravel (fallback branch, no margin crash)', function () {
      var prevMt = T.state.maxTravel;
      var prevPv = T.state.preview;
      T.state.maxTravel = null;
      T.state.preview = {
        lightPreview: { minX: 0, maxX: 30, minY: 0, maxY: 10, contours: [], contourRanges: [] },
        points: [], segments: [],
        previewMode: 'auto',
        contourSelectEnabled: false, contourDisabled: [], contourUndoStack: [], contourHighlight: -1,
        scale: 1, fitScale: 1, panX: 0, panY: 0
      };
      var ctx = makeFullCtx();
      var threw = false;
      try { T._drawTopView(ctx, 300, 300, 1, T.state.preview); } catch (e) { threw = true; }
      T.state.maxTravel = prevMt;
      T.state.preview = prevPv;
      ok(threw === false, 'no ReferenceError in _drawTopView without maxTravel');
      var hasXAxis = ctx.strokes.some(function (s) { return s.style === '#dc2626'; });
      var hasYAxis = ctx.strokes.some(function (s) { return s.style === '#16a34a'; });
      ok(hasXAxis, 'red X-axis origin line drawn');
      ok(hasYAxis, 'green Y-axis origin line drawn');
    });

    test('_distToSegment: perpendicular + clamped projection', function () {
      near(T._distToSegment(5, 5, 0, 0, 10, 0), 5, 1e-9, 'above segment');
      near(T._distToSegment(15, 5, 0, 0, 10, 0), Math.sqrt(50), 1e-9, 'projection clamped to end');
      near(T._distToSegment(-3, 0, 0, 0, 10, 0), 3, 1e-9, 'projection clamped to start');
      near(T._distToSegment(4, 3, 0, 0, 8, 6), 0, 1e-9, 'point on line');
    });

    test('_minDistToContour: square edges', function () {
      var sq = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
      near(T._minDistToContour(5, 5, sq), 5, 1e-9, 'center is 5mm from each edge');
      near(T._minDistToContour(12, 5, sq), 2, 1e-9, 'outside right');
      near(T._minDistToContour(5, 20, sq), 10, 1e-9, 'above top');
      near(T._minDistToContour(9, 9, sq), 1, 1e-9, 'near corner');
    });

    test('_pointInPolygon: inside/outside', function () {
      var sq = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
      ok(T._pointInPolygon(5, 5, sq) === true, 'center inside');
      ok(T._pointInPolygon(15, 5, sq) === false, 'outside right');
      ok(T._pointInPolygon(-1, 5, sq) === false, 'outside left');
    });
  }

  // Parsing: two squares => two contours with aligned, increasing ranges.
  {
    var dom = loadPage();
    var T = dom.window.__test__;
    var gcode = [
      'G21', 'G90', 'M3 S100',
      'G1 X0 Y0',
      'G1 X10 Y0',
      'G1 X10 Y10',
      'G1 X0 Y10',
      'G1 X0 Y0',
      'G0 X20 Y0',
      'G1 X30 Y0',
      'G1 X30 Y10',
      'G1 X20 Y10',
      'G1 X20 Y0',
      'M5'
    ].join('\n');
    var lp = T.buildLightPreview(gcode);

    test('buildLightPreview returns contours + contourRanges', function () {
      ok(!!lp, 'lightPreview built');
      is(lp.contours.length, 2, 'two squares parsed');
      is(lp.contourRanges.length, 2, 'one range per contour');
    });

    test('contourRanges startLine <= endLine and strictly increasing', function () {
      var r0 = lp.contourRanges[0], r1 = lp.contourRanges[1];
      ok(r0.startLine > 0 && r0.endLine > 0, 'range0 positive lines');
      ok(r1.startLine > r0.endLine, 'range1 starts after range0 ends');
      ok(r0.startLine <= r0.endLine, 'range0 valid span');
      ok(r1.startLine <= r1.endLine, 'range1 valid span');
    });
  }

  // Hit-test click: click highlights only, Space marks. Far clicks no-op.
  {
    var dom = loadPage();
    var sq = [{ x: 49, y: 49 }, { x: 51, y: 49 }, { x: 51, y: 51 }, { x: 49, y: 51 }];
    var ctx = setupPreview(dom, [sq], { ranges: [{ startLine: 3, endLine: 7 }] });
    ctx.T.state.preview.contourSelectEnabled = true;

    test('screen->machine maps click to machine coords (center roundtrip)', function () {
      var mp = ctx.T._screenToMachine(ctx.canvas, 150, 150);
      near(mp.x, 50, 0.01, 'x roundtrip');
      near(mp.y, 50, 0.01, 'y roundtrip');
    });

    test('click near contour edge highlights it but does NOT disable', function () {
      ctx.T._handleCanvasContourClick(ctx.canvas, 150, 150);
      is(ctx.T.state.preview.contourHighlight, 0, 'highlight set to clicked contour');
      is(ctx.T.state.preview.contourDisabled.length, 0, 'click never disables');
      is(ctx.T.state.preview.contourUndoStack.length, 0, 'no undo entry from click');
    });

    test('Space after click marks the highlighted contour', function () {
      ctx.T._spaceToggleHighlighted();
      is(ctx.T.state.preview.contourDisabled[0], true, 'Space marks contour 0');
      ctx.T._spaceToggleHighlighted();
      is(ctx.T.state.preview.contourDisabled[0], false, 'Space unmarks contour 0');
    });

    test('click far from any contour keeps highlight and marks intact', function () {
      var undoBefore = ctx.T.state.preview.contourUndoStack.length;
      ctx.T._handleCanvasContourClick(ctx.canvas, 20, 20);
      is(ctx.T.state.preview.contourHighlight, 0, 'highlight unchanged');
      is(ctx.T.state.preview.contourDisabled[0], false, 'marks intact (still unmarked)');
      is(ctx.T.state.preview.contourUndoStack.length, undoBefore, 'no undo entry added by click');
    });
  }

  // Tab / Space navigation and Undo.
  {
    var dom = loadPage();
    var s1 = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
    var s2 = [{ x: 20, y: 0 }, { x: 30, y: 0 }, { x: 30, y: 10 }, { x: 20, y: 10 }];
    var ctx = setupPreview(dom, [s1, s2]);
    ctx.T.state.preview.contourSelectEnabled = true;

    test('Tab highlights next contour, Space toggles it', function () {
      ctx.T._navContourHighlight(1);
      is(ctx.T.state.preview.contourHighlight, 0, 'Tab from -1 goes to 0');
      ctx.T._spaceToggleHighlighted();
      is(ctx.T.state.preview.contourDisabled[0], true, 'Space disabled contour 0');
      ctx.T._navContourHighlight(1);
      is(ctx.T.state.preview.contourHighlight, 1, 'Tab goes to 1');
      ctx.T._navContourHighlight(1);
      is(ctx.T.state.preview.contourHighlight, 0, 'wraps back to 0');
    });

    test('_undoContourToggle reverts the last toggle', function () {
      ctx.T._undoContourToggle();
      is(ctx.T.state.preview.contourDisabled[0], false, 'undo re-enabled contour 0');
      ctx.T._undoContourToggle();
      is(ctx.T.state.preview.contourUndoStack.length, 0, 'stack empty');
    });

    test('_toggleContourSelect toggles off (clears), then back on', function () {
      ctx.T._toggleContourSelect();
      is(ctx.T.state.preview.contourSelectEnabled, false, 'disabled after toggle');
      is(ctx.T.state.preview.contourDisabled.length, 0, 'selection cleared when off');
      is(ctx.T.state.preview.contourHighlight, -1, 'highlight reset when off');
      ctx.T._toggleContourSelect();
      is(ctx.T.state.preview.contourSelectEnabled, true, 're-enabled after second toggle');
    });
  }

  // renderPreviewFromText always builds the light preview (even in auto mode),
  // so click/Tab/Space selection works after any load. Regression: lightPreview
  // was only built for frame/wireframe, breaking selection in auto mode.
  {
    var dom = loadPage();
    var T = dom.window.__test__;
    var gcode = [
      'G21', 'G90', 'M3 S100',
      'G1 X0 Y0', 'G1 X10 Y0', 'G1 X10 Y10', 'G1 X0 Y10', 'G1 X0 Y0',
      'G0 X20 Y0', 'G1 X30 Y0', 'G1 X30 Y10', 'G1 X20 Y10', 'G1 X20 Y0',
      'M5'
    ].join('\n');

    test('auto mode: lightPreview built by renderPreviewFromText', function () {
      T.renderPreviewFromText(gcode, 'test.gcode');
      is(T.state.preview.previewMode, 'auto', 'resolved to auto mode');
      var lp = T.state.preview.lightPreview;
      ok(!!lp, 'lightPreview exists after load');
      ok(lp.contours && lp.contours.length >= 2, 'contours ready for click selection');
    });

    test('frame mode: lightPreview also built', function () {
      T.state.preview.previewMode = 'frame';
      T.renderPreviewFromText(gcode, 'test.gcode');
      var lp = T.state.preview.lightPreview;
      ok(!!lp && lp.contours && lp.contours.length >= 2, 'frame mode still exposes contours');
    });
  }

  // Enabling contour select starts with the first piece pre-highlighted so the
  // user immediately sees the active drawing.
  {
    var dom = loadPage();
    var s1 = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
    var s2 = [{ x: 20, y: 0 }, { x: 30, y: 0 }, { x: 30, y: 10 }, { x: 20, y: 10 }];
    var ctx = setupPreview(dom, [s1, s2]);

    test('_toggleContourSelect pre-highlights the first piece', function () {
      is(ctx.T.state.preview.contourHighlight, -1, 'starts unhighlighted');
      ctx.T._toggleContourSelect();
      is(ctx.T.state.preview.contourSelectEnabled, true, 'select on');
      is(ctx.T.state.preview.contourHighlight, 0, 'first piece highlighted');
    });
  }

  // Canvas mousemove while select is active tracks a crosshair position; leave
  // clears it. Regression: crosshair depended on a proper rect in CSS pixels.
  {
    var dom = loadPage();
    var sq = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
    var ctx = setupPreview(dom, [sq]);
    // init()/bindPreviewInteractions is deferred to DOMContentLoaded which
    // never fires in jsdom before exit, so bind the canvas listeners directly.
    ctx.T.bindPreviewInteractions();
    ctx.T.state.preview.contourSelectEnabled = true;
    var evOpts = { clientX: 150, clientY: 150, bubbles: true };

    test('mousemove over canvas records crosshair, mouseleave clears it', function () {
      ctx.canvas.dispatchEvent(new dom.window.MouseEvent('mousemove', evOpts));
      var m = ctx.T.state.preview._mousePx;
      ok(!!m && m.x === 150 && m.y === 150, 'crosshair position tracked');
      ctx.canvas.dispatchEvent(new dom.window.MouseEvent('mouseleave', evOpts));
      ok(ctx.T.state.preview._mousePx === null, 'crosshair cleared on leave');
    });

    test('no crosshair tracking while select disabled', function () {
      ctx.T.state.preview._mousePx = null;
      ctx.T.state.preview.contourSelectEnabled = false;
      ctx.canvas.dispatchEvent(new dom.window.MouseEvent('mousemove', evOpts));
      ok(ctx.T.state.preview._mousePx === null, 'mousemove ignored when disabled');
      ctx.T.state.preview.contourSelectEnabled = true;
      ctx.canvas.dispatchEvent(new dom.window.MouseEvent('mousemove', evOpts));
      ok(!!ctx.T.state.preview._mousePx, 'mousemove tracked again when enabled');
    });
  }

  // Overlay visuals: selector ON => enabled green, disabled gray dashed + red
  // X, highlighted gold. Selector OFF keeps the idle cyan.
  {
    var dom = loadPage();
    var T = dom.window.__test__;
    var sqA = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
    var sqB = [{ x: 20, y: 0 }, { x: 30, y: 0 }, { x: 30, y: 10 }, { x: 20, y: 10 }];
    var idMap = function (x, y) { return { x: x, y: y }; };
    var lpOn = { minX: 0, maxX: 30, minY: 0, maxY: 10, contours: [sqA, sqB] };
    var pvOn = {
      lightPreview: lpOn,
      contourSelectEnabled: true,
      contourDisabled: [false, true],
      contourHighlight: 0
    };

    test('overlay: selector ON, no highlight: enabled green + disabled gray dashed + red X', function () {
      var ctx = makeOverlayCtx();
      var pv = { lightPreview: lpOn, contourSelectEnabled: true, contourDisabled: [false, true], contourHighlight: -1 };
      T._drawContourOverlay(ctx, 1, pv, idMap);
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(74,222,128,0.45)'; }).length, 1, 'enabled piece green');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(148,163,184,0.7)'; }).length, 1, 'disabled piece gray');
      var gray = ctx.strokes.filter(function (s) { return s.style === 'rgba(148,163,184,0.7)'; })[0];
      is(gray.dash.length, 2, 'disabled stroke is dashed');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(239,68,68,0.85)'; }).length, 1, 'exactly one red X');
      is(ctx.fills.filter(function (s) { return s === 'rgba(74,222,128,0.04)'; }).length, 1, 'enabled green fill');
      is(ctx.fills.filter(function (s) { return s === 'rgba(148,163,184,0.12)'; }).length, 1, 'disabled gray fill');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(255,220,40,1)'; }).length, 0, 'no gold without highlight');
    });

    test('overlay: highlighted enabled piece gets gold, others keep their on/off look', function () {
      var ctx = makeOverlayCtx();
      T._drawContourOverlay(ctx, 1, pvOn, idMap);
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(255,220,40,1)'; }).length, 1, 'highlighted piece gold pass');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(255,255,255,0.7)'; }).length, 1, 'highlight white contour pass');
      is(ctx.fills.filter(function (s) { return s === 'rgba(250,204,21,0.18)'; }).length, 1, 'highlight gold fill');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(74,222,128,0.45)'; }).length, 0, 'highlight replaces the green pass');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(148,163,184,0.7)'; }).length, 1, 'non-highlighted disabled piece still gray');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(239,68,68,0.85)'; }).length, 1, 'its red X still drawn');
    });

    test('overlay: a contour with a null point is skipped, healthy ones still draw', function () {
      var bad = { lightPreview: { minX: 0, maxX: 30, minY: 0, maxY: 10,
        contours: [[{ x: 0, y: 0 }, null, { x: 10, y: 10 }], sqA] },
        contourSelectEnabled: true, contourDisabled: [], contourHighlight: -1 };
      var ctx = makeOverlayCtx();
      var badMap = function (x, y) { return (x === null || x === undefined) ? null : { x: x, y: y }; };
      var threw = false;
      try { T._drawContourOverlay(ctx, 1, bad, badMap); } catch (e) { threw = true; }
      ok(threw === false, 'no exception escapes the overlay (per-contour guard)');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(74,222,128,0.45)'; }).length, 1, 'healthy contour still drawn (enabled green)');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(34,211,238,0.92)'; }).length, 0, 'no idle cyan in selector mode');
    });

    test('overlay: selector OFF keeps idle cyan for every piece', function () {
      var ctx = makeOverlayCtx();
      var pvIdle = { lightPreview: lpOn, contourSelectEnabled: false, contourDisabled: [], contourHighlight: -1 };
      T._drawContourOverlay(ctx, 1, pvIdle, idMap);
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(34,211,238,0.92)'; }).length, 2, 'both pieces cyan idle');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(74,222,128,0.45)'; }).length, 0, 'no green when idle');
      is(ctx.strokes.filter(function (s) { return s.style === 'rgba(239,68,68,0.85)'; }).length, 0, 'no red X when idle');
    });

    test('info line: shows ON x / OFF y counts with pc position', function () {
      T.state.preview = {
        lightPreview: lpOn,
        contourSelectEnabled: true,
        contourDisabled: [false, true],
        contourHighlight: 0
      };
      T._updateContourSelectInfo();
      var txt = dom.window.document.getElementById('contour_select_info').textContent;
      ok(txt.indexOf('pc 1/2') >= 0, 'info has pc 1/2, got: ' + txt);
      ok(txt.indexOf('ON 1 / OFF 1') >= 0, 'info has ON 1 / OFF 1, got: ' + txt);
    });

    test('info line: zero disabled still shows full counts', function () {
      T.state.preview = {
        lightPreview: lpOn,
        contourSelectEnabled: true,
        contourDisabled: [false, false],
        contourHighlight: 1
      };
      T._updateContourSelectInfo();
      var txt = dom.window.document.getElementById('contour_select_info').textContent;
      ok(txt.indexOf('ON 2 / OFF 0') >= 0, 'info has ON 2 / OFF 0, got: ' + txt);
    });
  }

  // Segment-derived contour fallback (buildPieceContoursFromSegments): the
  // light scan can miss closed loops (arc-interpolated outlines, spindle-only
  // cuts), so once the full parser finishes, closed cut loops are derived from
  // the parsed segments. Both segment shapes are accepted: worker parser emits
  // {a,b} point indices, the inline parser emits point objects.
  {
    var dom = loadPage();
    var T = dom.window.__test__;
    var mk = function (x, y) { return { x: x, y: y, z: 0 }; };

    test('buildPieceContoursFromSegments exposed', function () {
      ok(typeof T.buildPieceContoursFromSegments === 'function', 'exported');
      ok(typeof T._syncContoursFromSegments === 'function', 'sync helper exported');
    });

    test('closed square (worker index form) => 1 contour with range', function () {
      var pts = [mk(0, 0), mk(10, 0), mk(10, 10), mk(0, 10), mk(0, 0)];
      var segs = [
        { a: 0, b: 1, rapid: false, lineNo: 1 },
        { a: 1, b: 2, rapid: false, lineNo: 2 },
        { a: 2, b: 3, rapid: false, lineNo: 3 },
        { a: 3, b: 4, rapid: false, lineNo: 4 }
      ];
      var r = T.buildPieceContoursFromSegments(pts, segs, 20);
      is(r.contours.length, 1, 'one contour');
      is(r.ranges.length, 1, 'one range');
      ok(r.ranges[0].startLine === 1 && r.ranges[0].endLine > r.ranges[0].startLine, 'range spans the square');
    });

    test('two squares split by a rapid => 2 contours', function () {
      var pts = [
        mk(0, 0), mk(10, 0), mk(10, 10), mk(0, 10), mk(0, 0),
        mk(20, 0), mk(30, 0), mk(30, 10), mk(20, 10), mk(20, 0)
      ];
      var segs = [
        { a: 0, b: 1, rapid: false, lineNo: 1 },
        { a: 1, b: 2, rapid: false, lineNo: 2 },
        { a: 2, b: 3, rapid: false, lineNo: 3 },
        { a: 3, b: 4, rapid: false, lineNo: 4 },
        { a: 4, b: 5, rapid: true, lineNo: 5 },
        { a: 5, b: 6, rapid: false, lineNo: 6 },
        { a: 6, b: 7, rapid: false, lineNo: 7 },
        { a: 7, b: 8, rapid: false, lineNo: 8 },
        { a: 8, b: 9, rapid: false, lineNo: 9 }
      ];
      var r = T.buildPieceContoursFromSegments(pts, segs, 20);
      is(r.contours.length, 2, 'two contours');
      ok(r.ranges[1].startLine > r.ranges[0].endLine, 'ranges are ordered');
    });

    test('object-form segments (inline parser) => 1 contour', function () {
      var segs = [
        { a: mk(0, 0), b: mk(10, 0), rapid: false, lineNo: 1 },
        { a: mk(10, 0), b: mk(10, 10), rapid: false, lineNo: 2 },
        { a: mk(10, 10), b: mk(0, 10), rapid: false, lineNo: 3 },
        { a: mk(0, 10), b: mk(0, 0), rapid: false, lineNo: 4 }
      ];
      var r = T.buildPieceContoursFromSegments(null, segs, 20);
      is(r.contours.length, 1, 'object form closes too');
    });

    test('open stroke => 0 contours', function () {
      var segs = [
        { a: mk(0, 0), b: mk(10, 0), rapid: false, lineNo: 1 },
        { a: mk(10, 0), b: mk(10, 10), rapid: false, lineNo: 2 }
      ];
      var r = T.buildPieceContoursFromSegments(null, segs, 20);
      is(r.contours.length, 0, 'open stroke not a piece');
    });

    test('degenerate back-and-forth (zero area) => 0 contours', function () {
      var segs = [
        { a: mk(0, 0), b: mk(10, 0), rapid: false, lineNo: 1 },
        { a: mk(10, 0), b: mk(0, 0), rapid: false, lineNo: 2 }
      ];
      var r = T.buildPieceContoursFromSegments(null, segs, 20);
      is(r.contours.length, 0, 'zero-area loop dropped');
    });

    test('sync: light-scan contours are NOT clobbered by the segment fallback', function () {
      var sqA = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }, { x: 0, y: 0 }];
      var sqB = [{ x: 20, y: 0 }, { x: 30, y: 0 }, { x: 30, y: 10 }, { x: 20, y: 10 }, { x: 20, y: 0 }];
      var pts = [mk(0, 0), mk(10, 0), mk(10, 10), mk(0, 10), mk(0, 0),
                 mk(20, 0), mk(30, 0), mk(30, 10), mk(20, 10), mk(20, 0)];
      var segs = [
        { a: 0, b: 1, rapid: false, lineNo: 1 },
        { a: 1, b: 2, rapid: false, lineNo: 2 },
        { a: 2, b: 3, rapid: false, lineNo: 3 },
        { a: 3, b: 4, rapid: false, lineNo: 4 },
        { a: 4, b: 5, rapid: true, lineNo: 5 },
        { a: 5, b: 6, rapid: false, lineNo: 6 },
        { a: 6, b: 7, rapid: false, lineNo: 7 },
        { a: 7, b: 8, rapid: false, lineNo: 8 },
        { a: 8, b: 9, rapid: false, lineNo: 9 }
      ];
      var prev = T.state.preview;
      T.state.preview = {
        points: pts,
        segments: segs,
        lightPreview: { minX: 0, maxX: 30, minY: 0, maxY: 10, contours: [sqA, sqB], contourRanges: [{ startLine: 1, endLine: 4 }, { startLine: 6, endLine: 9 }] },
        contourSelectEnabled: false
      };
      T._syncContoursFromSegments();
      var lp = T.state.preview.lightPreview;
      is(lp.contours.length, 2, 'light-scan pieces survive the sync (got ' + lp.contours.length + ')');
      T.state.preview = prev;
    });

    test('sync: empty light-scan contours still get the segment fallback', function () {
      var pts = [mk(0, 0), mk(10, 0), mk(10, 10), mk(0, 10), mk(0, 0)];
      var segs = [
        { a: 0, b: 1, rapid: false, lineNo: 1 },
        { a: 1, b: 2, rapid: false, lineNo: 2 },
        { a: 2, b: 3, rapid: false, lineNo: 3 },
        { a: 3, b: 4, rapid: false, lineNo: 4 }
      ];
      var prev = T.state.preview;
      T.state.preview = {
        points: pts,
        segments: segs,
        lightPreview: { minX: 0, maxX: 10, minY: 0, maxY: 10, contours: [], contourRanges: [] },
        contourSelectEnabled: false
      };
      T._syncContoursFromSegments();
      var lp = T.state.preview.lightPreview;
      ok(lp.contours.length >= 1, 'fallback fills empty light-scan contours (got ' + lp.contours.length + ')');
      T.state.preview = prev;
    });
  }

  // Regression: arc-interpolated outlines. The light scan only records G1
  // chord endpoints with S-power, so a circle cut with G2/G3 arcs produces
  // ZERO light contours. After the full parser completes, the segment fallback
  // must make the piece selectable (the bug the user hit: no gold outline ever
  // appeared for such files). init() clears the preview on DOMContentLoaded, so
  // wait for it to settle before rendering.
  {
    var dom = loadPage();
    var T = dom.window.__test__;

    test('arc circle: segment fallback exposes a selectable contour after parse', function (done) {
      setTimeout(function () {
        var gcode = [
          'G21', 'G90', 'M3 S100',
          'G0 X20 Y10',
          'G2 X20 Y30 I0 J10',
          'G2 X20 Y10 I0 J-10',
          'M5'
        ].join('\n');
        T.renderPreviewFromText(gcode, 'circle.gcode');
        var lp = T.state.preview.lightPreview;
        ok(!!lp, 'light preview exists');
        is(lp.contours.length, 0, 'light scan alone sees no contour for arcs');
        (function poll() {
          var pv = T.state.preview;
          if (pv.parseBusy) {
            setTimeout(poll, 20);
            return;
          }
          try {
            var lpc = pv.lightPreview && pv.lightPreview.contours ? pv.lightPreview.contours : [];
            is(pv.parseProgress, 100, 'parse finished');
            is(pv.segments.length > 0, true, 'full parser produced segments');
            ok(lpc.length >= 1, 'arc circle becomes selectable after parse (got ' + lpc.length + ')');
            var canvas = dom.window.document.getElementById('grbl_preview_canvas');
            canvas.getBoundingClientRect = function() {
              return { left: 0, top: 0, width: 300, height: 300, right: 300, bottom: 300 };
            };
            pv.contourSelectEnabled = true;
            pv.contourDisabled = [];
            pv.contourHighlight = 0;
            var ctx = makeFullCtx();
            var threw = null;
            try { T._drawTopView(ctx, 300, 300, 1, pv); } catch (e) { threw = e; }
            ok(threw === null, 'H7 smoke: _drawTopView does not throw with real arcs contours' + (threw ? ': ' + threw.message : ''));
            var gold = ctx.strokes.filter(function (s) { return s.style === 'rgba(255,220,40,1)'; }).length;
            var white = ctx.strokes.filter(function (s) { return s.style === 'rgba(255,255,255,0.7)'; }).length;
            ok(gold >= 1, 'H7 smoke: highlighted contour drawn in gold (got ' + gold + ')');
            ok(white >= 1, 'H7 smoke: white contour pass drawn (got ' + white + ')');
            // A real canvas silently ignores NaN/undefined path coordinates, so
            // the overlay must emit finite pixels or nothing paints. The mock
            // records styles regardless, so validate coordinates explicitly.
            var badPts = 0;
            ctx.strokes.forEach(function (s) {
              (s.p || []).forEach(function (pt) {
                if (pt.op !== 'Z' && (!isFinite(pt.x) || !isFinite(pt.y))) badPts++;
              });
            });
            is(badPts, 0, 'overlay strokes have finite canvas coordinates (got ' + badPts + ' non-finite)');
            done();
          } catch (e) {
            done(e);
          }
        })();
      }, 250);
    });

    test('H3: light-preview rebuild preserves derived contours', function (done) {
      setTimeout(function () {
        var gcode = [
          'G21', 'G90', 'M3 S100',
          'G0 X20 Y10',
          'G2 X20 Y30 I0 J10',
          'G2 X20 Y10 I0 J-10',
          'M5'
        ].join('\n');
        T.renderPreviewFromText(gcode, 'circle.gcode');
        (function poll() {
          var pv = T.state.preview;
          if (pv.parseBusy) { setTimeout(poll, 20); return; }
          try {
            var lp = pv.lightPreview;
            ok(!!lp && lp.contours && lp.contours.length >= 1, 'derived contours exist before rebuild');
            var before = lp.contours.length;
            if (typeof T.applyLightPreviewFromBounds === 'function') {
              T.applyLightPreviewFromBounds({ minX: 0, maxX: 30, minY: 0, maxY: 10 }, 'circle.gcode');
              var after = pv.lightPreview && pv.lightPreview.contours ? pv.lightPreview.contours.length : 0;
              is(after, before, 'H3: bounds rebuild keeps the derived contours (was ' + before + ', got ' + after + ')');
            }
            done();
          } catch (e) { done(e); }
        })();
      }, 250);
    });
  }

  // H2: the click inverse must follow the RENDER transform, which anchors on
  // the parsed points bounds (_computeBounds prefers pv.points over the light
  // preview). Previously _screenToMachine centered on the lightPreview bounds,
  // so clicks drifted whenever they differed from the parsed-points bounds.
  {
    var dom = loadPage();
    var T = dom.window.__test__;
    var lpSmall = { minX: 0, maxX: 100, minY: 0, maxY: 100, contours: [], contourRanges: [] };
    T.state.preview = {
      lightPreview: lpSmall,
      points: [{ x: 0, y: 0, z: 0 }, { x: 200, y: 200, z: 0 }],
      contourSelectEnabled: false,
      contourDisabled: [],
      contourUndoStack: [],
      contourHighlight: -1,
      scale: 1, fitScale: 1, panX: 0, panY: 0
    };
    var canvas = dom.window.document.getElementById('grbl_preview_canvas');
    canvas.width = 300; canvas.height = 300;
    canvas.getBoundingClientRect = function() {
      return { left: 0, top: 0, width: 300, height: 300, right: 300, bottom: 300 };
    };

    test('_screenToMachine anchors on parsed-points bounds, not light bounds', function () {
      var mp = T._screenToMachine(canvas, 150, 150);
      near(mp.x, 100, 0.01, 'center click maps to center of 0..200 (points), got ' + mp.x);
      near(mp.y, 100, 0.01, 'y mirrored center, got ' + mp.y);
    });

    test('_screenToMachine: pan shifts the inverse exactly (machine units)', function () {
      // With panX/panY in machine units applied as ox/oy offset, the canvas
      // center must resolve to (center - panX, center + panY) at ANY zoom.
      T.state.preview.panX = 12; T.state.preview.panY = -8; T.state.preview.scale = 2; T.state.preview.fitScale = 1;
      var mp = T._screenToMachine(canvas, 150, 150);
      near(mp.x, 100 - 12, 0.01, 'x center shifted by -panX, got ' + mp.x);
      near(mp.y, 100 + (-8), 0.01, 'y center shifted by +panY, got ' + mp.y);
      T.state.preview.panX = 0; T.state.preview.panY = 0; T.state.preview.scale = 1; T.state.preview.fitScale = 1;
    });

    test('_screenToMachine matches the shared transform scale for zoom levels', function () {
      // Independent recompute of _computeTopTransformPx::sc so the inverse
      // cannot silently alias a different zoom. avail = 300 - 2*34 = 232.
      var lpT = { minX: 0, maxX: 200, minY: 0, maxY: 200 };
      T.state.preview.lightPreview = lpT;
      T.state.preview.panX = 0; T.state.preview.panY = 0; T.state.preview.fitScale = 1;
      [1, 2, 4].forEach(function (zs) {
        T.state.preview.scale = zs;
        var mp = T._screenToMachine(canvas, 150, 150);
        near(mp.x, 100, 0.001, 'no-pan center stable at zoom ' + zs);
        var top = T._computeTopTransformPx(canvas.width, canvas.height, T.state.preview);
        near(top.sc, (Math.min(232 / 200, 232 / 200)) * zs, 0.001, 'shared sc = fit * zoomMult at ' + zs);
      });
      T.state.preview.scale = 1;
    });
  }

  // UX: Tab navigation must refresh the pc N/M info bar (it previously only
  // redrew the canvas, so the highlighted piece number never changed on Tab).
  {
    var dom = loadPage();
    var sq1 = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
    var sq2 = [{ x: 20, y: 0 }, { x: 30, y: 0 }, { x: 30, y: 10 }, { x: 20, y: 10 }];
    var ctxN = setupPreview(dom, [sq1, sq2], { ranges: [{ startLine: 2, endLine: 5 }, { startLine: 7, endLine: 10 }] });
    ctxN.T.state.preview.contourSelectEnabled = true;
    ctxN.T.state.preview.contourHighlight = 0;

    test('Tab (_navContourHighlight) updates the info bar highlight position', function () {
      ctxN.T._navContourHighlight(1);
      var txt = dom.window.document.getElementById('contour_select_info').textContent;
      ok(txt.indexOf('pc 2/2') >= 0, 'info shows pc 2/2 after Tab, got: ' + txt);
      ctxN.T._navContourHighlight(1);
      txt = dom.window.document.getElementById('contour_select_info').textContent;
      ok(txt.indexOf('pc 1/2') >= 0, 'wrap-around back to pc 1/2, got: ' + txt);
    });
  }

  finishIfDone();
}

run();
