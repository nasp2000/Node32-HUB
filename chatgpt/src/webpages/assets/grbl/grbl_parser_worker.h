R"GRBL(
      // ── Web Worker Parser (desktop only) ──────────────────────────────
      // Self-contained parser that runs in a background thread.
      // Falls back to the inline parser when Worker is unavailable (ESP32 browser).

      function _buildParserWorkerCode() {
        // extractAxisValue is needed inside the worker (no main-thread access)
        var extractAxisSrc = extractAxisValue.toString();

        // Build a string that runs as a Web Worker
        return [
          'self.onmessage = function(e) {',
          '  var opts = e.data.options || {};',
          '  var text = e.data.gcode || "";',
          '  var lines = text.split(/\\r?\\n/);',
          '  var x = 0, y = 0, z = 0;',
          '  var absoluteMode = true, arcCenterAbsolute = false;',
          '  var unitToMm = 1, motionMode = 1, planeMode = 17;',
          '  var pts = [{x:x,y:y,z:z}];',
          '  var segs = [];',
          '  var maxPoints = opts.maxPoints || 2000000;',
          '  var arcQuality = opts.arcQuality || "med";',
          '  var truncated = false;',
          '  var bMinX = Infinity, bMaxX = -Infinity;',
          '  var bMinY = Infinity, bMaxY = -Infinity;',
          '  var bMinZ = Infinity, bMaxZ = -Infinity;',
          '  var arcStepByQuality = { low:2.4, med:1.0, high:0.45 };',
          '  var arcTolByQuality = { low:0.18, med:0.08, high:0.03 };',
          '  var arcMaxAngleByQuality = { low:Math.PI/13, med:Math.PI/20, high:Math.PI/34 };',
          '',
          '  ' + extractAxisSrc.replace(/function extractAxisValue/, 'function extractAxisValue'),
          '',
          '  function extractAxisInMm(line, axis) {',
          '    var v = extractAxisValue(line, axis);',
          '    if (v === null) return null;',
          '    return v * unitToMm;',
          '  }',
          '  function clamp(val, lo, hi) { return Math.max(lo, Math.min(hi, val)); }',
          '',
          '  function appendLinear(prevIdx, next, rapid, arcFlag, lineNo, execNo) {',
          '    pts.push(next);',
          '    segs.push({ a:prevIdx, b:pts.length-1, rapid:rapid, arc:!!arcFlag, lineNo:lineNo||0, execNo:execNo||lineNo||0 });',
          '    if (next.x < bMinX) bMinX = next.x; if (next.x > bMaxX) bMaxX = next.x;',
          '    if (next.y < bMinY) bMinY = next.y; if (next.y > bMaxY) bMaxY = next.y;',
          '    if (next.z < bMinZ) bMinZ = next.z; if (next.z > bMaxZ) bMaxZ = next.z;',
          '  }',
          '',
          '  function buildArcSegmentsXY(prevIdx, next, cx, cy, cw, forceFullCircle, lineNo, execNo) {',
          '    var p = pts[prevIdx];',
          '    var dx0 = p.x - cx; var dy0 = p.y - cy;',
          '    var radius = Math.sqrt(dx0*dx0 + dy0*dy0);',
          '    if (!isFinite(radius) || radius < 0.000001) { appendLinear(prevIdx, next, false, false, lineNo, execNo); return; }',
          '    var a0 = Math.atan2(p.y - cy, p.x - cx);',
          '    var a1 = Math.atan2(next.y - cy, next.x - cx);',
          '    var delta = a1 - a0;',
          '    if (forceFullCircle) { delta = cw ? (-Math.PI*2) : (Math.PI*2); }',
          '    else { if (cw) { if (delta >= 0) delta -= (Math.PI*2); } else { if (delta <= 0) delta += (Math.PI*2); } }',
          '    if (!isFinite(delta) || Math.abs(delta) < 0.0000001) { if (!forceFullCircle) { appendLinear(prevIdx, next, false, false, lineNo, execNo); return; } }',
          '    var arcLen = Math.abs(delta) * radius;',
          '    var stepMm = arcStepByQuality[arcQuality] || arcStepByQuality.med;',
          '    var tolMm = arcTolByQuality[arcQuality] || arcTolByQuality.med;',
          '    var maxThetaByTol = Math.PI;',
          '    if (radius > tolMm && tolMm > 0) { var c = 1 - (tolMm/radius); c = clamp(c, -1, 1); maxThetaByTol = 2 * Math.acos(c); }',
          '    var maxThetaByMode = arcMaxAngleByQuality[arcQuality] || arcMaxAngleByQuality.med;',
          '    var maxTheta = Math.max(0.01, Math.min(maxThetaByTol, maxThetaByMode));',
          '    var nByAngle = Math.ceil(Math.abs(delta) / maxTheta);',
          '    var nByLength = Math.ceil(arcLen / Math.max(0.12, stepMm));',
          '    var qualityCap = arcQuality === "high" ? 2200 : (arcQuality === "low" ? 720 : 1300);',
          '    var n = clamp(Math.max(6, nByAngle, nByLength), 6, qualityCap);',
          '    var last = { x:p.x, y:p.y, z:p.z };',
          '    for (var k = 1; k <= n; k++) {',
          '      if (pts.length >= maxPoints) { truncated = true; break; }',
          '      var t = k / n; var a = a0 + (delta * t);',
          '      var np = { x: cx + Math.cos(a)*radius, y: cy + Math.sin(a)*radius, z: p.z + (next.z - p.z)*t };',
          '      appendLinear(pts.length-1, np, false, true, lineNo, execNo);',
          '      last = np;',
          '    }',
          '  }',
          '',
          '  function chooseArcCenterFromR(prevIdx, next, rVal, cw) {',
          '    var p = pts[prevIdx];',
          '    var dx = next.x - p.x; var dy = next.y - p.y;',
          '    var chord = Math.sqrt(dx*dx + dy*dy);',
          '    if (!isFinite(chord) || chord < 0.000001) return null;',
          '    var rAbs = Math.abs(rVal);',
          '    if (chord > (2*rAbs)) rAbs = chord * 0.5;',
          '    var h2 = (rAbs*rAbs) - ((chord*chord)/4);',
          '    if (h2 < 0) h2 = 0;',
          '    var h = Math.sqrt(h2);',
          '    var mx = (p.x + next.x) * 0.5; var my = (p.y + next.y) * 0.5;',
          '    var ux = -dy / chord; var uy = dx / chord;',
          '    var candidates = [{cx:mx+(ux*h),cy:my+(uy*h)}, {cx:mx-(ux*h),cy:my-(uy*h)}];',
          '    function getSweep(c) {',
          '      var a0 = Math.atan2(p.y - c.cy, p.x - c.cx);',
          '      var a1 = Math.atan2(next.y - c.cy, next.x - c.cx);',
          '      var d = a1 - a0;',
          '      if (cw) { if (d >= 0) d -= (Math.PI*2); } else { if (d <= 0) d += (Math.PI*2); }',
          '      return d;',
          '    }',
          '    var d0 = getSweep(candidates[0]); var d1 = getSweep(candidates[1]);',
          '    return (rVal < 0) ? (Math.abs(d0)>=Math.abs(d1)?candidates[0]:candidates[1]) : (Math.abs(d0)<=Math.abs(d1)?candidates[0]:candidates[1]);',
          '  }',
          '',
          '  var i, execNo = 0;',
          '  for (i = 0; i < lines.length; i++) {',
          '    var ln = String(lines[i] || "").trim();',
          '    if (!ln) continue;',
          '    if (ln.charAt(0) === ";") continue;',
          '    if (ln.indexOf("(") >= 0) ln = ln.replace(/\\([^)]*\\)/g, "");',
          '    var upper = ln.toUpperCase().replace(/;.*$/, "").trim();',
          '    if (!upper) continue;',
          '    execNo++;',
          '    var gm;',
          '    var gx = /(?:^|\\s)G(\\d+(?:\\.\\d+)?)(?=\\s|$)/g;',
          '    while ((gm = gx.exec(upper)) !== null) {',
          '      var gv = parseFloat(gm[1]);',
          '      if (isNaN(gv)) continue;',
          '      if (gv === 90) absoluteMode = true;',
          '      else if (gv === 91) absoluteMode = false;',
          '      else if (gv === 90.1) arcCenterAbsolute = true;',
          '      else if (gv === 91.1) arcCenterAbsolute = false;',
          '      else if (gv === 20) unitToMm = 25.4;',
          '      else if (gv === 21) unitToMm = 1;',
          '      else if (gv === 17 || gv === 18 || gv === 19) planeMode = gv;',
          '      else if (gv === 0 || gv === 1 || gv === 2 || gv === 3) motionMode = gv;',
          '    }',
          '    var prevIdx = pts.length - 1;',
          '    var nx = extractAxisInMm(upper, "X");',
          '    var ny = extractAxisInMm(upper, "Y");',
          '    var nz = extractAxisInMm(upper, "Z");',
          '    var ni = extractAxisInMm(upper, "I");',
          '    var nj = extractAxisInMm(upper, "J");',
          '    var nr = extractAxisInMm(upper, "R");',
          '    if (nx !== null) x = absoluteMode ? nx : (x + nx);',
          '    if (ny !== null) y = absoluteMode ? ny : (y + ny);',
          '    if (nz !== null) z = absoluteMode ? nz : (z + nz);',
          '    var isArcMotionXY = (motionMode === 2 || motionMode === 3) && planeMode === 17;',
          '    var hasAxisMove = (nx !== null) || (ny !== null) || (nz !== null);',
          '    if (!hasAxisMove && !(isArcMotionXY && (ni !== null || nj !== null))) continue;',
          '    var sourceLineNo = i + 1;',
          '    var next = { x:x, y:y, z:z };',
          '    if (motionMode === 0 || motionMode === 1) {',
          '      appendLinear(prevIdx, next, motionMode === 0, false, sourceLineNo, execNo);',
          '    } else if (isArcMotionXY) {',
          '      var cw = motionMode === 2; var center = null;',
          '      if (ni !== null || nj !== null) {',
          '        var iVal = (ni !== null) ? ni : 0; var jVal = (nj !== null) ? nj : 0;',
          '        center = { cx: arcCenterAbsolute ? iVal : (pts[prevIdx].x + iVal), cy: arcCenterAbsolute ? jVal : (pts[prevIdx].y + jVal) };',
          '      } else if (nr !== null) { center = chooseArcCenterFromR(prevIdx, next, nr, cw); }',
          '      if (center && isFinite(center.cx) && isFinite(center.cy)) {',
          '        buildArcSegmentsXY(prevIdx, next, center.cx, center.cy, cw, false, sourceLineNo, execNo);',
          '      } else { appendLinear(prevIdx, next, false, false, sourceLineNo, execNo); }',
          '    } else { appendLinear(prevIdx, next, false, false, sourceLineNo, execNo); }',
          '    if (pts.length > maxPoints) { truncated = true; break; }',
          '    if (i % 2500 === 0) {',
          '      self.postMessage({ type:"progress", progress: Math.floor((i*100)/lines.length), intermediate: {',
          '        points: pts, segments: segs,',
          '        bounds: isFinite(bMinX) ? { minX:bMinX, maxX:bMaxX, minY:bMinY, maxY:bMaxY, minZ:bMinZ, maxZ:bMaxZ } : null',
          '      }});',
          '    }',
          '  }',
          '  self.postMessage({',
          '    type:"result",',
          '    points: pts, segments: segs, maxExec: execNo, truncated: truncated,',
          '    bounds: isFinite(bMinX) ? { minX:bMinX, maxX:bMaxX, minY:bMinY, maxY:bMaxY, minZ:bMinZ, maxZ:bMaxZ } : null',
          '  });',
          '};'
        ].join('\n');
      }

      // ── Worker parser wrapper ────────────────────────────────────────
      // Returns an object with {start, cancel} or null if workers not supported.

      function createParserWorker() {
        if (typeof Worker === 'undefined') return null;
        if (_isEsp32Browser) return null;
        try {
          var code = _buildParserWorkerCode();
          var blob = new Blob([code], { type: 'application/javascript' });
          var url = URL.createObjectURL(blob);
          var worker = new Worker(url);
          URL.revokeObjectURL(url);
          return {
            parse: function(text, options, onProgress) {
              worker.onmessage = function(e) {
                var data = e.data;
                if (data.type === 'progress' && onProgress) {
                  var ib = data.intermediate.bounds;
                  onProgress(data.progress, {
                    points: data.intermediate.points,
                    segments: data.intermediate.segments,
                    bounds: ib
                  });
                }
              };
              worker.postMessage({ gcode: text, options: options || {} });
              return worker;
            },
            onResult: function(worker, callback) {
              worker.onmessage = function(e) {
                if (e.data.type === 'result') callback(e.data);
              };
            },
            cancel: function(worker) {
              if (worker) worker.terminate();
            }
          };
        } catch (e) {
          return null;
        }
      }

      var _parserWorker = null;
      function _useWorkerParser() {
        if (!_parserWorker) _parserWorker = createParserWorker();
        return _parserWorker;
      }
)GRBL"
