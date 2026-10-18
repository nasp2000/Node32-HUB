// Debug: capture what _drawTopView actually strokes, per mode.
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'grbl_controller.html');
var html = fs.readFileSync(PAGE, 'utf8');
var fix = fs.readFileSync(path.join(__dirname, 'mocks', 'api_fixtures.js'), 'utf8');
html = html.replace('// ===== MOCK API LAYER =====', fix + '\n// ===== MOCK API LAYER =====');
var orig = console.error; console.error = function() {};
var dom = new JSDOM(html, { url: 'http://localhost', runScripts: 'dangerously', pretendToBeVisual: true });
console.error = orig;

var T = dom.window.__test__;
var canvas = dom.window.document.getElementById('grbl_preview_canvas');

function makeCtx() {
  var strokes = [];
  var fills = [];
  return {
    strokes: strokes, fills: fills,
    _lastStyle: null, _lastWidth: 0,
    setTransform: function(){},
    fillRect: function(){},
    strokeRect: function(){},
    setLineDash: function(){},
    measureText: function(t){ return { width: (t||'').length * 7 }; },
    beginPath: function(){ this._p = []; },
    moveTo: function(x,y){ if(!this._p) this._p=[]; this._p.push('M'+x.toFixed(1)+','+y.toFixed(1)); },
    lineTo: function(x,y){ this._p.push('L'+x.toFixed(1)+','+y.toFixed(1)); },
    closePath: function(){ this._p.push('Z'); },
    arc: function(){},
    stroke: function(){ strokes.push({ style: this.strokeStyle, w: this.lineWidth, n: (this._p||[]).length }); },
    fill: function(){ fills.push({ style: this.fillStyle, n: (this._p||[]).length }); },
    fillText: function(){},
    save: function(){}, restore: function(){},
  };
}

function setup(contourCount, opts) {
  var contours = [];
  for (var i = 0; i < contourCount; i++) {
    var bx = i * 12;
    contours.push([{x:bx,y:0},{x:bx+10,y:0},{x:bx+10,y:10},{x:bx,y:10}]);
  }
  var lp = {
    minX: 0, maxX: 10 + (contourCount-1)*12, minY: 0, maxY: 10,
    contours: contours,
    contourRanges: [],
    firstCutX: 0, firstCutY: 0
  };
  T.state.preview = {
    lightPreview: lp,
    contourSelectEnabled: opts.sel === true,
    contourDisabled: opts.disabled || [],
    contourUndoStack: [],
    contourHighlight: typeof opts.hl === 'number' ? opts.hl : -1,
    scale: 1, fitScale: 1, panX: 0, panY: 0,
    points: opts.points || [],
    segments: opts.segments || [],
    previewMode: opts.mode || 'auto',
    hasPath: true
  };
  canvas.getBoundingClientRect = function(){ return { left:0, top:0, width:300, height:300, right:300, bottom:300 }; };
}

// Outlines mode
setup(20, { mode: 'outlines' });
var c1 = makeCtx();
try { T._drawTopView(c1, 300, 300, 1, T.state.preview); }
catch (e) { console.log('OUTLINES THREW: ' + e.message); }
console.log('--- outlines: strokes=' + c1.strokes.length + ' fills=' + c1.fills.length);
c1.strokes.slice(0, 6).forEach(function(s){ console.log('  stroke', s.style, 'w=' + s.w, 'pts=' + s.n); });
c1.fills.slice(0, 4).forEach(function(f){ console.log('  fill', f.style, 'pts=' + f.n); });

// Auto mode with segments + select on + highlight 4
setup(20, { mode: 'auto', sel: true, hl: 4, disabled: [0,1,2,3,5,6,7] });
var c2 = makeCtx();
try { T._drawTopView(c2, 300, 300, 1, T.state.preview); }
catch (e) { console.log('AUTO THREW: ' + e.message); }
console.log('--- auto(sel,hl=4): strokes=' + c2.strokes.length + ' fills=' + c2.fills.length);
c2.strokes.slice(0, 10).forEach(function(s){ console.log('  stroke', s.style, 'w=' + s.w, 'pts=' + s.n); });
c2.fills.slice(0, 4).forEach(function(f){ console.log('  fill', f.style, 'pts=' + f.n); });

process.exit(0);
