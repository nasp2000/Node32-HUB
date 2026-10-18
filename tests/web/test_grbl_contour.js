// GRBL Light Contour Tests
// Covers buildLightPieceContoursFromGcode: outlines mode must return only
// closed drawings (squares, circles, closed pockets). Open cut/engraving
// strokes and isolated open lines must never produce contours.
// Run: node test_grbl_contour.js
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

function sandbox(fileName, fnName, deps) {
  var src = extractFunctionSource(fileName, fnName);
  var keys = Object.keys(deps);
  var args = keys.map(function (k) { return deps[k]; });
  var ctorArgs = keys.concat(['return ' + src]);
  var Fn = Function.bind.apply(Function, [null].concat(ctorArgs));
  return new Fn().apply(null, args);
}

function makeBuilder() {
  return sandbox('grbl_contour.h', 'buildLightPieceContoursFromGcode', {
    parseLightAxisValue: sandbox('grbl_contour.h', 'parseLightAxisValue', {}),
    simplifyPolylineLight: sandbox('grbl_contour.h', 'simplifyPolylineLight', {}),
    GRBL_LIGHT_PREVIEW_MAX_SCAN_BYTES: 20 * 1024 * 1024,
    GRBL_LIGHT_PREVIEW_MAX_CONTOURS: 2000
  });
}

function makeFilter() {
  return sandbox('grbl_contour.h', 'filterContoursPreserveCoords', {});
}

// Full-job fixture: square centered at 0,0 (corners +/-100) with a circle in
// each corner. Origin of the job is the center of the square. Ranges mirror
// what buildLightPieceContoursFromGcode produces: each contour's endLine
// points at the following G0 approach (so a naive range removal drops the
// positioning move of the next kept piece).
function makeSquareCirclesGcode() {
  return [
    'G90',
    'M3 S1000',
    'G0 X-100 Y-100 Z5',
    'G1 Z-2 F300',
    'G1 X100 Y-100 F500',
    'G1 X100 Y100',
    'G1 X-100 Y100',
    'G1 X-100 Y-100',
    'G0 Z5',
    'G0 X-100 Y-100',
    'G1 Z-2 F300',
    'G1 X-80 Y-100 F500',
    'G2 X-100 Y-80 I0 J20',
    'G2 X-80 Y-100 I20 J0',
    'G0 Z5',
    'G0 X100 Y-100',
    'G1 Z-2 F300',
    'G1 X120 Y-100 F500',
    'G2 X100 Y-80 I0 J20',
    'G2 X120 Y-100 I20 J0',
    'G0 Z5',
    'G0 X100 Y100',
    'G1 Z-2 F300',
    'G1 X120 Y100 F500',
    'G2 X100 Y120 I0 J20',
    'G2 X120 Y100 I20 J0',
    'G0 Z5',
    'G0 X-100 Y100',
    'G1 Z-2 F300',
    'G1 X-80 Y100 F500',
    'G2 X-100 Y120 I0 J20',
    'G2 X-80 Y100 I20 J0',
    'G0 Z5',
    'M5'
  ].join('\n');
}

function run() {
  console.log('\n=== GRBL Light Contour Tests ===\n');
  var builder = makeBuilder();

  var CLOSED_SQUARE = [
    'G90',
    'M3 S1000',
    'G1 X0 Y0 F500',
    'G1 X100 Y0',
    'G1 X100 Y100',
    'G1 X0 Y100',
    'G1 X0 Y0',
    'M5'
  ].join('\n');

  test('closed square produces exactly one contour with correct bounds', function () {
    var r = builder(CLOSED_SQUARE);
    ok(r, 'result should not be null');
    is(r.contours.length, 1);
    var c = r.contours[0];
    is(c[0].x, 0); is(c[0].y, 0);
    is(c[c.length - 1].x, 0); is(c[c.length - 1].y, 0);
    approx(r.minX, 0); approx(r.maxX, 100); approx(r.minY, 0); approx(r.maxY, 100);
  });

  var SQUARE_PLUS_ENGRAVING = CLOSED_SQUARE + [
    '',
    'G0 X30 Y30',
    'M3 S300',
    'G1 X70 Y30',
    'M5',
    'G0 X70 Y70',
    'M3 S300',
    'G1 X30 Y70',
    'M5'
  ].join('\n');

  test('interior engraving (open strokes) does not add contours', function () {
    var r = builder(SQUARE_PLUS_ENGRAVING);
    ok(r, 'result should not be null');
    is(r.contours.length, 1, 'only the closed square should be outlined');
  });

  var MODAL_S_TWO_SQUARES = [
    'G90',
    'M3 S1000',
    'G1 X0 Y0 F500',
    'G1 X100 Y0',
    'G1 X100 Y100',
    'G1 X0 Y100',
    'G1 X0 Y0',
    'M5',
    'G0 X200 Y0',
    'M3',
    'G1 X200 Y0',
    'G1 X300 Y0',
    'G1 X300 Y100',
    'G1 X200 Y100',
    'G1 X200 Y0',
    'M5'
  ].join('\n');

  test('modal-S piece (M5/M3 without repeating S) is still detected', function () {
    var r = builder(MODAL_S_TWO_SQUARES);
    ok(r, 'result should not be null');
    is(r.contours.length, 2, 'both squares outlined (second inherits S modally, got ' + r.contours.length + ')');
  });

  var OPEN_SIDES = [
    'G90',
    'M3 S1000',
    'G1 X0 Y0 F500',
    'G1 X100 Y0',
    'M5',
    'G0 X100 Y0',
    'M3 S1000',
    'G1 X100 Y100',
    'M5',
    'G0 X100 Y100',
    'M3 S1000',
    'G1 X0 Y100',
    'M5',
    'G0 X0 Y100',
    'M3 S1000',
    'G1 X0 Y0',
    'M5'
  ].join('\n');

  test('open lines only (each side cut separately) produce no contours', function () {
    var r = builder(OPEN_SIDES);
    ok(r, 'result should not be null');
    is(r.contours.length, 0);
  });

  var TINY_STROKE = [
    'G90',
    'M3 S1000',
    'G1 X0 Y0 F500',
    'G1 X1 Y0',
    'M5'
  ].join('\n');

  test('short open stroke is not misclassified as a closed shape', function () {
    var r = builder(TINY_STROKE);
    ok(r, 'result should not be null');
    is(r.contours.length, 0);
  });

  var ARC_CIRCLE = [
    'G90',
    'M3 S1000',
    'G1 X100 Y0 F500',
    'G3 X0 Y100 I-100 J0',
    'G3 X-100 Y0 I0 J-100',
    'G3 X0 Y-100 I100 J0',
    'G3 X100 Y0 I0 J100',
    'M5'
  ].join('\n');

  test('four-arc circle yields one closed contour', function () {
    var r = builder(ARC_CIRCLE);
    ok(r, 'result should not be null');
    is(r.contours.length, 1);
    var c = r.contours[0];
    is(c[0].x, 100); is(c[0].y, 0);
    is(c[c.length - 1].x, 100); is(c[c.length - 1].y, 0);
    approx(r.minX, -100); approx(r.maxX, 100); approx(r.minY, -100); approx(r.maxY, 100);
  });

  var TWO_SQUARES = [
    'G90',
    'M3 S1000',
    'G1 X0 Y0 F500',
    'G1 X100 Y0',
    'G1 X100 Y100',
    'G1 X0 Y100',
    'G1 X0 Y0',
    'M5',
    'G0 X300 Y0',
    'M3 S1000',
    'G1 X400 Y0',
    'G1 X400 Y100',
    'G1 X300 Y100',
    'G1 X300 Y0',
    'M5'
  ].join('\n');

  test('two separate closed squares produce two contours', function () {
    var r = builder(TWO_SQUARES);
    ok(r, 'result should not be null');
    is(r.contours.length, 2);
    approx(r.maxX, 400);
  });

  test('empty input returns null', function () {
    is(builder(''), null);
    is(builder(null), null);
  });

  // ====== filterContoursPreserveCoords (contour-filtered streaming) ======
  // Regression: the old range-removal filter deleted the G0 approach that
  // positions the machine at the NEXT (kept) contour, so selected pieces were
  // cut from the wrong position instead of their full-job coordinates.
  {
    var filter = makeFilter();
    var gcode = makeSquareCirclesGcode();
    var ranges = [
      { startLine: 5, endLine: 10 },   // rectangle
      { startLine: 12, endLine: 16 },  // bottom-left circle
      { startLine: 18, endLine: 22 },  // bottom-right circle
      { startLine: 24, endLine: 28 },  // top-right circle
      { startLine: 30, endLine: 34 }   // top-left circle
    ];

    test('filter keeps approach G0 and cut of the selected circle', function () {
      var out = filter(gcode, ranges, [true, false, true, true, true]);
      var lines = out.split('\n');
      ok(out.indexOf('G0 X-100 Y-100') >= 0, 'approach G0 to BL circle preserved');
      ok(out.indexOf('G1 X-80 Y-100 F500') >= 0, 'BL circle first cut preserved');
      ok(lines.some(function (l) { return /^G2 X-100 Y-80 I0 J20/.test(l); }), 'BL circle arc preserved');
    });

    test('filter removes cut moves of disabled contours but keeps state lines', function () {
      var out = filter(gcode, ranges, [true, false, true, true, true]);
      ok(out.indexOf('G1 X100 Y-100 F500') < 0, 'rectangle cut removed');
      ok(out.indexOf('G1 X120 Y-100 F500') < 0, 'BR circle cut removed');
      ok(out.indexOf('G1 X120 Y100 F500') < 0, 'TR circle cut removed');
      ok(out.indexOf('G1 X-80 Y100 F500') < 0, 'TL circle cut removed');
      ok(out.indexOf('G90') >= 0, 'G90 kept');
      ok(out.indexOf('M3 S1000') >= 0, 'M3 S kept');
      ok(out.indexOf('M5') >= 0, 'M5 kept');
    });

    test('filter preserves full-job coordinates (circle stays at -80/-100)', function () {
      var out = filter(gcode, ranges, [true, false, true, true, true]);
      ok(out.indexOf('G1 X-80 Y-100 F500') >= 0, 'cut X at -80 (job coords)');
      ok(out.indexOf('G2 X-100 Y-80 I0 J20') >= 0, 'arc end X-100 Y-80 (job coords)');
      ok(out.indexOf('G2 X-80 Y-100 I20 J0') >= 0, 'arc end X-80 Y-100 (job coords)');
    });

    test('filter with nothing disabled returns source unchanged', function () {
      var out = filter(gcode, ranges, [false, false, false, false, false]);
      is(out, gcode);
    });

    test('filter with all disabled drops all cuts but keeps header/state', function () {
      var out = filter(gcode, ranges, [true, true, true, true, true]);
      ok(out.indexOf('G90') >= 0, 'header kept');
      ok(out.indexOf('M3 S1000') >= 0, 'M3 kept');
      ok(out.indexOf('G1 X100 Y-100 F500') < 0, 'rect cut dropped');
      ok(out.indexOf('G1 X-80 Y-100 F500') < 0, 'circle cut dropped');
    });

    test('filter handles empty ranges/disabled', function () {
      is(filter(gcode, [], []), gcode);
      is(filter('', [], []), '');
    });

    test('filter real contour ranges from builder align with filter', function () {
      var b = builder(gcode);
      ok(b.contourRanges.length === 5, 'expected 5 contours, got ' + b.contourRanges.length);
      var out = filter(gcode, b.contourRanges, [true, false, true, true, true]);
      ok(out.indexOf('G1 X-80 Y-100 F500') >= 0, 'kept circle survives with real ranges');
      ok(out.indexOf('G1 X100 Y-100 F500') < 0, 'rect dropped with real ranges');
    });

    // ====== Modal S/F preservation (power/feed must survive the filter) ======
    // LightBurn-style: S800 F900 only on the FIRST cut line of the FIRST
    // (disabled) contour; all later contours inherit modally. Regression: the
    // filter used to drop that line, so kept cuts ran at the stale power.
    test('filter re-emits modal S/F before a kept contour (LightBurn style)', function () {
      var src = [
        'G91',
        'M8',
        'M5',
        'G0 X-22.617Y20.603',
        'M3',
        'G1 X-0.437Y-0.21S800F900',
        'G1 X-0.459Y-0.168',
        'M5',
        'G0 X-45.102Y-3.992',
        'M3',
        'G1 X0.076Y-0.497',
        'G1 X0.026Y-0.511',
        'M5',
        'M9',
        'G1 S0'
      ].join('\n');
      var ranges = [
        { startLine: 6, endLine: 8 },   // disabled first circle (holds S800F900)
        { startLine: 11, endLine: 12 }  // enabled BL circle
      ];
      var out = filter(src, ranges, [true, false]);
      var sIdx = out.indexOf('S800');
      var cutIdx = out.indexOf('G1 X0.076Y-0.497');
      ok(sIdx >= 0, 'S800 preserved');
      ok(out.indexOf('F900') >= 0, 'F900 preserved');
      ok(cutIdx >= 0 && sIdx < cutIdx, 'S800 emitted before the kept cut');
      ok(out.indexOf('G1 X-0.437Y-0.21S800F900') < 0, 'disabled contour cut dropped');
    });

    test('filter injects modal F/S once per kept block and keeps original lines', function () {
      var src = [
        'G90',
        'M3 S1000',
        'G0 X-100 Y-100',
        'G1 X100 Y-100 F500',
        'M5',
        'G0 X-100 Y-100',
        'M3',
        'G1 X-80 Y-100 F300',
        'G2 X-100 Y-80 I0 J20',
        'M5'
      ].join('\n');
      var ranges = [
        { startLine: 4, endLine: 4 },
        { startLine: 8, endLine: 9 }
      ];
      var out = filter(src, ranges, [true, false]);
      ok(out.indexOf('S1000') >= 0, 'modal S re-emitted');
      ok(out.indexOf('F500') >= 0, 'modal F re-emitted');
      ok(out.indexOf('G1 X-80 Y-100 F300') >= 0, 'kept cut intact with its own F');
      ok(out.indexOf('G1 X100 Y-100 F500') < 0, 'disabled cut dropped');
    });

    test('filter with no modal S/F emits no stray state lines', function () {
      var src = ['G90', 'G0 X0 Y0', 'G1 X10 Y10', 'G1 X10 Y0', 'G0 X0 Y0'].join('\n');
      var ranges = [{ startLine: 3, endLine: 4 }];
      var out = filter(src, ranges, [false]);
      is(out, src);
    });

    // Regression: a comment as the FIRST line of the kept contour's block
    // (LightBurn emits "; Layer C02 Pass 1 of 6") used to prevent the modal
    // S/F capture, so no S800/F900 was injected and GRBL raised
    // "Gcode undefined feed rate". The capture must happen on the first line
    // of the block even when that line is a comment.
    test('filter injects S/F when the kept block starts with a comment', function () {
      var src = [
        'G91',
        'G0 X-22.617Y20.603',
        'M3',
        'G1 X-0.437Y-0.21S800F900',
        'G1 X-0.459Y-0.168',
        'M5',
        'G0 X-45.102Y-3.992',
        '; Layer C02 Pass 1 of 6',
        'M3',
        'G1 X0.076Y-0.497',
        'G1 X0.026Y-0.511',
        'M5'
      ].join('\n');
      var ranges = [
        { startLine: 4, endLine: 5 },   // disabled (holds S800F900)
        { startLine: 10, endLine: 11 }  // enabled BL circle, comment before it
      ];
      var out = filter(src, ranges, [true, false]);
      var fIdx = out.indexOf('F900');
      var cutIdx = out.indexOf('G1 X0.076Y-0.497');
      ok(fIdx >= 0, 'F900 injected');
      ok(out.indexOf('S800') >= 0, 'S800 injected');
      ok(cutIdx >= 0 && fIdx >= 0 && fIdx < cutIdx, 'F900 emitted before the kept cut');
    });
  }

  // ====== quickWireframe (2D preview bounds scan, grbl_contour.h) ======
  {
    var qwf = sandbox('grbl_contour.h', 'quickWireframe', {});

    test('quickWireframe parses simple G-code into bounds + points', function () {
      var r = qwf('G90\nG0 X0 Y0 Z5\nG1 X100 Y50 Z-10 F1000\nG1 X200 Y0 Z0\n');
      ok(r !== null, 'should return result');
      is(r.minX, 0); is(r.maxX, 200);
      is(r.minY, 0); is(r.maxY, 50);
      is(r.minZ, -10); is(r.maxZ, 5);
      ok(Array.isArray(r.wireframePoints), 'wireframePoints is array');
      ok(r.wireframePoints.length > 0, 'has some points');
    });

    test('quickWireframe handles empty text with finite bounds', function () {
      var r = qwf('');
      ok(r !== null, 'should return result for empty');
      ok(isFinite(r.minX) && isFinite(r.maxX), 'bounds should be finite');
      ok(Array.isArray(r.wireframePoints), 'wireframePoints is array');
    });

    test('quickWireframe extracts Z height range', function () {
      var r = qwf('G90\nG0 X0 Y0 Z10\nG1 X10 Y0 Z-5\nG1 X20 Y0 Z2\n');
      ok(r !== null, 'should return result');
      is(r.minZ, -5, 'minZ should be -5');
      is(r.maxZ, 10, 'maxZ should be 10');
    });

    test('quickWireframe respects incremental mode (G91)', function () {
      var r = qwf('G91\nG0 X0 Y0 Z0\nG1 X10 Y0 F500\nG1 X10 Y0\nG1 X0 Y20\n');
      ok(r !== null, 'should return result');
      is(r.minX, 0); is(r.maxX, 20, 'two incremental +10 X moves reach 20');
      is(r.minY, 0); is(r.maxY, 20);
    });
  }

  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run();
