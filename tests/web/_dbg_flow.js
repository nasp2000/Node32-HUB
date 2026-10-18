// Faithful device-flow reproduction: maxTravel set, parse completes, 20 pieces.
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
dom.window.HTMLCanvasElement.prototype.getContext = function() { return null; };

var T = dom.window.__test__;
var canvas = dom.window.document.getElementById('grbl_preview_canvas');
canvas.getBoundingClientRect = function(){ return { left:0, top:0, width:300, height:300, right:300, bottom:300 }; };
T.state.maxTravel = { x: 200, y: 300, z: 10 };

var lines = ['G21', 'G90', 'M3 S100'];
for (var p = 0; p < 20; p++) {
  var bx = p * 12;
  lines.push('G1 X' + bx + ' Y0');
  lines.push('G1 X' + (bx + 10) + ' Y0');
  lines.push('G1 X' + (bx + 10) + ' Y10');
  lines.push('G1 X' + bx + ' Y10');
  lines.push('G1 X' + bx + ' Y0');
  if (p < 19) lines.push('G0 X' + (bx + 12) + ' Y0');
}
lines.push('M5');
var gcode = lines.join('\n');

function makeCtx() {
  var strokes = [];
  return {
    strokes: strokes,
    setTransform: function(){}, fillRect: function(){}, strokeRect: function(){},
    setLineDash: function(){}, measureText: function(t){ return { width: (t||'').length*7 }; },
    beginPath: function(){ this._p = []; },
    moveTo: function(x,y){ if (!this._p) this._p = []; this._p.push('M'+Math.round(x)+','+Math.round(y)); },
    lineTo: function(x,y){ this._p.push('L'+Math.round(x)+','+Math.round(y)); },
    closePath: function(){ this._p.push('Z'); },
    arc: function(){}, stroke: function(){ strokes.push({ style: this.strokeStyle, w: this.lineWidth }); },
    fill: function(){}, fillText: function(){}, save: function(){}, restore: function(){},
  };
}

function count(c) {
  var out = {};
  c.strokes.forEach(function(s){ out[s.style] = (out[s.style]||0)+1; });
  return JSON.stringify(out);
}

function waitParse(done) {
  if (!T.state.preview.parseBusy) { done(); return; }
  setTimeout(function(){ waitParse(done); }, 5);
}

// Wait for DOMContentLoaded init (real-browser ordering: init -> clearPreview
// -> then user picks a file and renderPreviewFromText runs). In jsdom the
// event fires a macrotask late, so drive the load AFTER init.
setTimeout(function() {
  T.renderPreviewFromText(gcode, 'x.nc');
  console.log('contours=', T.state.preview.lightPreview.contours.length);
  waitParse(function() {
    console.log('parse done: segs=', T.state.preview.segments.length, 'pts=', T.state.preview.points.length, 'previewMode=', T.state.preview.previewMode);
    var c1 = makeCtx(); T._drawTopView(c1, 300, 300, 1, T.state.preview);
    console.log('AUTO strokes:', count(c1));
    T.state.preview.contourSelectEnabled = true;
    T.state.preview.contourHighlight = 5;
    T.state.preview.contourDisabled = [0,1,2,3,4,6,7,8];
    var c3 = makeCtx(); T._drawTopView(c3, 300, 300, 1, T.state.preview);
    console.log('AUTO sel hl=5 strokes:', count(c3));
    process.exit(0);
  });
}, 200);
setTimeout(function(){ console.log('TIMEOUT'); process.exit(2); }, 20000);
