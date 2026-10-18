R"GRBL(
      var _drawPreviewRafId = 0;

      function drawPreview() {
        if (_drawPreviewRafId) return;
        _drawPreviewRafId = window.requestAnimationFrame(function() {
          _drawPreviewRafId = 0;
          var canvas = byId('grbl_preview_canvas');
          if (!canvas) return;
          var rect = canvas.getBoundingClientRect();
          if (rect.width < 10 || rect.height < 10) return;
          _drawPreviewNow();
        });
      }

      function _drawPreviewNow() {
        try {
          var canvas = byId('grbl_preview_canvas');
          if (!canvas) return;
          var rect = canvas.getBoundingClientRect();
          var w = Math.max(10, Math.floor(rect.width));
          var h = Math.max(10, Math.floor(rect.height));
          if (w < 10 || h < 10) return;
          var dpr = window.devicePixelRatio || 1;
          canvas.width = Math.round(w * dpr);
          canvas.height = Math.round(h * dpr);
          var ctx = canvas.getContext('2d');
          if (!ctx) return;
          ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
          ctx.fillStyle = '#0f172a';
          ctx.fillRect(0, 0, w, h);
          var pv = state.preview;
          if (pv.viewMode === 'iso') _drawIsoView(ctx, w, h, dpr, pv);
          else if (pv.viewMode === 'side') _drawSideView(ctx, w, h, dpr, pv);
          else _drawTopView(ctx, w, h, dpr, pv);
        } catch (e) {
          console.error('[grbl] _drawPreviewNow', e);
        }
      }

      function _computeBounds(pv) {
        var pts = pv.points, lp = pv.lightPreview, mt = state.maxTravel;
        var minX, maxX, minY, maxY, minZ, maxZ;
        if (pts && pts.length > 0) {
          minX = pts[0].x; maxX = pts[0].x; minY = pts[0].y; maxY = pts[0].y; minZ = pts[0].z; maxZ = pts[0].z;
          for (var i = 1; i < pts.length; i++) {
            var p = pts[i];
            if (p.x < minX) minX = p.x; if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y; if (p.y > maxY) maxY = p.y;
            if (p.z < minZ) minZ = p.z; if (p.z > maxZ) maxZ = p.z;
          }
        } else if (lp && isFinite(lp.minX) && isFinite(lp.maxX) && isFinite(lp.minY) && isFinite(lp.maxY)) {
          minX = lp.minX; maxX = lp.maxX; minY = lp.minY; maxY = lp.maxY; minZ = 0; maxZ = 0;
        } else if (mt && mt.x > 0 && mt.y > 0) {
          minX = 0; maxX = mt.x; minY = 0; maxY = mt.y; minZ = 0; maxZ = (mt.z > 0 ? mt.z : 50);
        } else {
          minX = -50; maxX = 50; minY = -50; maxY = 50; minZ = 0; maxZ = 10;
        }
        return { minX: minX, maxX: maxX, minY: minY, maxY: maxY, minZ: minZ, maxZ: maxZ,
                 spanX: Math.max(0.01, maxX - minX), spanY: Math.max(0.01, maxY - minY), spanZ: Math.max(0.01, maxZ - minZ) };
      }

      function _viewMappedY(pv, y) {
        // Permanent mirror-Y fix: parts rendered in machine coords appear
        // flipped (screen Y grows down), so Y is always negated here.
        return -y;
      }

      function _viewTransformBounds(pv, b) {
        var corners = [[b.minX, b.minY], [b.maxX, b.minY], [b.maxX, b.maxY], [b.minX, b.maxY]];
        var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
        for (var i = 0; i < corners.length; i++) {
          var px = corners[i][0], py = -corners[i][1];
          if (px < minX) minX = px; if (px > maxX) maxX = px;
          if (py < minY) minY = py; if (py > maxY) maxY = py;
        }
        return { minX: minX, maxX: maxX, minY: minY, maxY: maxY,
                 spanX: Math.max(0.01, maxX - minX), spanY: Math.max(0.01, maxY - minY) };
      }

      // Single source of truth for the top-view transform so the click inverse
      // mapping (_screenToMachine) and the render (_drawTopView) can never
      // drift apart. Matches the light path exactly: bounds come from
      // _computeBounds (prefers parsed points), so pan/zoom apply to both.
      function _computeTopTransformPx(w, h, pv) {
        var bounds = _computeBounds(pv);
        var tb = _viewTransformBounds(pv, bounds);
        var margin = 34, availW = Math.max(20, w - margin * 2), availH = Math.max(20, h - margin * 2);
        var zoomMult = (pv.fitScale > 0 && pv.scale > 0) ? (pv.scale / pv.fitScale) : 1;
        var sc = Math.max(0.01, Math.min(5000, Math.min(availW / tb.spanX, availH / tb.spanY) * zoomMult));
        var ox = margin + (availW - tb.spanX * sc) / 2 + (pv.panX || 0) * sc;
        var oy = margin + (availH - tb.spanY * sc) / 2 + (pv.panY || 0) * sc;
        return { sc: sc, ox: ox, oy: oy, tb: tb };
      }

      function _segStyle(s) {
        if (!s) return 3;
        return s.rapid ? 0 : (s.cutType === 'cut' ? 1 : (s.cutType === 'engrave' ? 2 : 3));
      }

      function _effectivePreviewLine(pv) {
        if (pv.scrubEnabled) {
          var sl = parseInt(pv.scrubLine || 0, 10);
          return (isNaN(sl) || sl < 0) ? 0 : sl;
        }
        var cl = parseInt(pv.currentLine || 0, 10);
        return (isNaN(cl) || cl < 0) ? 0 : cl;
      }

      function _strokePath(ctx, pts, strokeStyle, lineWidth, dash) {
        if (!pts || !pts.length) return;
        ctx.strokeStyle = strokeStyle;
        ctx.lineWidth = lineWidth;
        ctx.setLineDash(dash || []);
        ctx.beginPath();
        for (var pi = 0; pi < pts.length; pi += 4) {
          ctx.moveTo(pts[pi], pts[pi + 1]);
          ctx.lineTo(pts[pi + 2], pts[pi + 3]);
        }
        ctx.stroke();
      }

      function getSegmentExecNo(seg) {
        if (!seg) return 0;
        var n = parseInt(seg.execNo || seg.lineNo || 0, 10);
        return isNaN(n) ? 0 : n;
      }

      function getActiveExecNo(segs, currentLine) {
        if (!segs || !segs.length || currentLine <= 0) return 0;
        var best = 0;
        var gi;
        for (gi = 0; gi < segs.length; gi++) {
          var n = getSegmentExecNo(segs[gi]);
          if (n > 0 && n <= currentLine && n > best) best = n;
        }
        return best;
      }

      // Job-aware render: when a job is running (state.preview.currentLine > 0)
      // the preview splits the toolpath into already-cut (dim), the segment
      // being cut right now (bright highlight + head dot) and the remaining
      // path (full color), updating in real time as GRBL acks lines.
      function _drawSegmentsJob(ctx, segs, dpr, mapFn, currentLine) {
        ctx.lineCap = 'round'; ctx.lineJoin = 'round';
        var activeExec = getActiveExecNo(segs, currentLine);
        var pvF = state && state.preview ? state.preview : {};
        var showCut = pvF.showCut !== false;
        var showRapid = pvF.showRapid !== false;
        var showEngrave = pvF.showEngrave !== false;
        var srcLen = (state && state.workspaceText) ? state.workspaceText.length : 0;
        var srcMb = srcLen / (1024 * 1024);
        var fillMode = 0;
        if (srcMb > 4) fillMode = 1;
        if (srcMb > 10) fillMode = 2;
        if (srcMb > 20) fillMode = 3;
        var n = segs.length;
        var globalStride = (n > 400000) ? Math.ceil(n / 400000) : 1;
        // past/future buckets indexed by style (0=rapid,1=cut,2=engrave+other)
        var past = [[], [], []];
        var future = [[], [], []];
        var current = [];
        var head = null;
        var i = 0;
        while (i < n) {
          var style = _segStyle(segs[i]);
          var runLen = 1;
          while (i + runLen < n && _segStyle(segs[i + runLen]) === style) runLen++;
          var isFillRun = ((style === 2 || style === 3) && runLen >= 40);
          var ri = 0;
          for (; ri < runLen; ri++, i++) {
            if (globalStride > 1 && (ri % globalStride) !== 0) continue;
            if (isFillRun) {
              if (fillMode === 1 && (ri % 4) === 0) continue;
              if (fillMode === 2 && (ri % 2) !== 0) continue;
              if (fillMode === 3 && (ri % 3) !== 0) continue;
            }
            var s = segs[i];
            if ((style === 0 && !showRapid) || (style === 1 && !showCut) || ((style === 2 || style === 3) && !showEngrave)) continue;
            var a = mapFn(s.a.x, s.a.y, s.a.z);
            var b = mapFn(s.b.x, s.b.y, s.b.z);
            if (!isFinite(a.x) || !isFinite(a.y) || !isFinite(b.x) || !isFinite(b.y)) continue;
            var exec = getSegmentExecNo(s);
            var bucket = (style === 0) ? 0 : (style === 1 ? 1 : 2);
            if (activeExec > 0 && exec === activeExec) {
              current.push(a.x, a.y, b.x, b.y);
              head = b;
            } else if (exec > 0 && exec < currentLine) {
              past[bucket].push(a.x, a.y, b.x, b.y);
            } else {
              future[bucket].push(a.x, a.y, b.x, b.y);
            }
          }
        }
        // Done (past) first, dimmed, so the remaining path sits clearly on top.
        _strokePath(ctx, past[0], 'rgba(245,158,11,0.4)', Math.max(0.8, dpr * 0.7), [Math.round(4 * dpr), Math.round(3 * dpr)]);
        _strokePath(ctx, past[1], 'rgba(239,68,68,0.42)', Math.max(1.5, dpr * 1.2), []);
        _strokePath(ctx, past[2], 'rgba(34,197,94,0.42)', Math.max(1, dpr * 0.85), []);
        // Remaining (future): full brightness.
        _strokePath(ctx, future[0], 'rgba(245,158,11,0.95)', Math.max(0.8, dpr * 0.7), [Math.round(4 * dpr), Math.round(3 * dpr)]);
        _strokePath(ctx, future[1], 'rgba(239,68,68,0.95)', Math.max(1.5, dpr * 1.2), []);
        _strokePath(ctx, future[2], 'rgba(34,197,94,0.95)', Math.max(1, dpr * 0.85), []);
        // Current segment: dark glow then bright core.
        _strokePath(ctx, current, 'rgba(12,74,110,0.96)', Math.max(3.7, dpr * 3.05), []);
        _strokePath(ctx, current, '#22d3ee', Math.max(2.2, dpr * 1.8), []);
        // Head dot at the tip of the segment being cut.
        if (head && isFinite(head.x) && isFinite(head.y)) {
          var hp = mapFn(head.x, head.y, head.z);
          if (isFinite(hp.x) && isFinite(hp.y)) {
            ctx.fillStyle = '#fbbf24';
            ctx.beginPath(); ctx.arc(hp.x, hp.y, Math.max(4, Math.round(dpr * 3)), 0, Math.PI * 2); ctx.fill();
          }
        }
      }

      function _drawSegments(ctx, segs, dpr, mapFn) {
        if (!segs || !segs.length) return;
        var pv0 = state && state.preview ? state.preview : {};
        var effLine = _effectivePreviewLine(pv0);
        if (effLine > 0) { _drawSegmentsJob(ctx, segs, dpr, mapFn, effLine); return; }
        ctx.lineCap = 'round'; ctx.lineJoin = 'round';
        var n = segs.length;
        // File-size-aware fill decimation: files up to ~4 MB keep every line,
        // larger files progressively drop engrave/fill lines so the preview
        // stays fast up to ~30 MB. Cut outlines always stay intact.
        var srcLen = (state && state.workspaceText) ? state.workspaceText.length : 0;
        var srcMb = srcLen / (1024 * 1024);
        var fillMode = 0; // 0 = all lines
        if (srcMb > 4) fillMode = 1;  // keep 3 of 4
        if (srcMb > 10) fillMode = 2; // keep 1 of 2
        if (srcMb > 20) fillMode = 3; // keep 1 of 3
        // Last-resort cap for extreme cut-heavy files (cuts are never decimated
        // by fillMode, so a pathological file with hundreds of thousands of cut
        // segments still stays fluid).
        var globalStride = 1;
        if (n > 400000) globalStride = Math.ceil(n / 400000);
        var i = 0;
        while (i < n) {
          var style = _segStyle(segs[i]);
          var strokeStyle, lineWidth, dash;
          if (style === 0) { strokeStyle = '#f59e0b'; lineWidth = Math.max(0.8, dpr * 0.7); dash = [Math.round(4 * dpr), Math.round(3 * dpr)]; }
          else if (style === 1) { strokeStyle = '#ef4444'; lineWidth = Math.max(1.5, dpr * 1.2); dash = []; }
          else if (style === 2) { strokeStyle = '#22c55e'; lineWidth = Math.max(1, dpr * 0.85); dash = []; }
          else { strokeStyle = '#60a5fa'; lineWidth = Math.max(1.2, dpr * 1); dash = []; }
          ctx.strokeStyle = strokeStyle;
          ctx.lineWidth = lineWidth;
          ctx.setLineDash(dash);
          // Measure the style run up front.
          var runLen = 1;
          while (i + runLen < n && _segStyle(segs[i + runLen]) === style) runLen++;
          // View filters: hide whole style runs based on the Cut/Rapid/Engrave
          // checkboxes.
          var pvF = state && state.preview ? state.preview : {};
          var showCut = pvF.showCut !== false;
          var showRapid = pvF.showRapid !== false;
          var showEngrave = pvF.showEngrave !== false;
          if ((style === 0 && !showRapid) || (style === 1 && !showCut) || ((style === 2 || style === 3) && !showEngrave)) {
            i += runLen;
            continue;
          }
          var isFillRun = ((style === 2 || style === 3) && runLen >= 40);
          ctx.beginPath();
          var started = false;
          var end = i + runLen;
          for (var ri = 0; i < end; i++, ri++) {
            var draw = true;
            if (isFillRun) {
              if (fillMode === 1) { if ((ri % 4) === 0) draw = false; }
              else if (fillMode === 2) { if ((ri % 2) !== 0) draw = false; }
              else if (fillMode === 3) { if ((ri % 3) !== 0) draw = false; }
            }
            if (globalStride > 1 && (ri % globalStride) !== 0) draw = false;
            if (draw) {
              var s = segs[i];
              var a = mapFn(s.a.x, s.a.y, s.a.z);
              var b = mapFn(s.b.x, s.b.y, s.b.z);
              if (isFinite(a.x) && isFinite(a.y) && isFinite(b.x) && isFinite(b.y)) {
                if (!started) { ctx.moveTo(a.x, a.y); started = true; }
                ctx.lineTo(b.x, b.y);
              }
            }
          }
          ctx.stroke();
        }
        ctx.setLineDash([]);
      }

      function _playheadSegs(segs) {
        var pv = state.preview;
        if (!pv.scrubEnabled) return segs;
        var limit = parseInt(pv.scrubLine || 0, 10);
        if (!isFinite(limit) || limit <= 0) return [];
        if (!segs || !segs.length) return segs;
        var out = [];
        for (var i = 0; i < segs.length; i++) {
          if ((segs[i].lineNo || 0) > limit) break;
          out.push(segs[i]);
        }
        return out;
      }

      function _drawPlayhead(ctx, mapFn, dpr) {
        var pv = state.preview;
        if (!pv.scrubEnabled) return;
        var segs = pv.segments;
        if (!segs || !segs.length) return;
        var limit = parseInt(pv.scrubLine || 0, 10);
        if (!isFinite(limit) || limit <= 0) return;
        var head = null;
        for (var i = 0; i < segs.length; i++) {
          if ((segs[i].lineNo || 0) > limit) break;
          head = segs[i].b;
        }
        if (!head) return;
        var p = mapFn(head.x, head.y, head.z);
        if (!isFinite(p.x) || !isFinite(p.y)) return;
        ctx.fillStyle = '#fbbf24';
        ctx.beginPath(); ctx.arc(p.x, p.y, Math.max(4, Math.round(dpr * 3)), 0, Math.PI * 2); ctx.fill();
        ctx.strokeStyle = '#b45309'; ctx.lineWidth = Math.max(1.5, dpr); ctx.stroke();
      }

      function _drawLabelPill(ctx, text, x, y, font, dpr, noBg) {
        ctx.font = font + 'px monospace';
        var tw = Math.ceil(ctx.measureText(text).width);
        var pad = Math.round(3 * dpr);
        if (noBg !== true) {
          ctx.fillStyle = 'rgba(2,6,23,0.85)';
          ctx.fillRect(x - tw / 2 - pad, y - pad, tw + pad * 2, font + pad * 2);
        }
        ctx.fillStyle = '#e2e8f0';
        ctx.fillText(text, x, y + font - pad);
      }

      // Shared contour selection overlay (cyan idle / green enabled / gray
      // dashed disabled with a red X / gold highlighted). With the selector ON
      // the full path is dimmed so these piece colors dominate and it is
      // unmistakable which contours stay in the job and which are skipped.
      // mapP(point) converts machine coords to canvas CSS pixels and must match
      // the click inverse mapping (_screenToMachine) so what you click is what
      // you see.
      function _drawContourOverlay(ctx, dpr, pv, mapP) {
        try {
          var lp = pv.lightPreview;
          if (!lp || !lp.contours || !lp.contours.length) return;
          var selEnabled = pv.contourSelectEnabled === true;
          var disabled = pv.contourDisabled || [];
          var hlIdx = (typeof pv.contourHighlight === 'number') ? pv.contourHighlight : -1;
          var hasHl = selEnabled && hlIdx >= 0 && hlIdx < lp.contours.length;
          ctx.lineCap = 'round'; ctx.lineJoin = 'round';

          for (var pass = 0; pass < (hasHl ? 2 : 1); pass++) {
            for (var ci = 0; ci < lp.contours.length; ci++) {
              var c = lp.contours[ci]; if (!c || c.length < 2) continue;
              try {
              var isOff = !!(disabled[ci]);
              var isHl = (ci === hlIdx);
              if (hasHl && pass === 0 && isHl) continue;
              if (hasHl && pass === 1 && !isHl) continue;

              ctx.beginPath();
              // mapP is a (x, y, z) scalar projector (topMap/lpMap/isoMap) —
              // same calling convention as _drawSegments. Passing the point
              // object used to produce NaN/undefined screen coords, which a
              // real canvas silently ignores (the overlay never painted).
              var p0 = mapP(c[0].x, c[0].y, c[0].z); ctx.moveTo(p0.x, p0.y);
              for (var cj = 1; cj < c.length; cj++) { var pp = mapP(c[cj].x, c[cj].y, c[cj].z); ctx.lineTo(pp.x, pp.y); }
              ctx.closePath();

              if (selEnabled) {
                if (isOff) {
                  ctx.strokeStyle = 'rgba(148,163,184,0.7)'; ctx.lineWidth = Math.max(1.2, dpr * 1.0);
                  ctx.setLineDash([Math.round(4 * dpr), Math.round(4 * dpr)]);
                  ctx.stroke(); ctx.setLineDash([]);
                  ctx.fillStyle = 'rgba(148,163,184,0.12)'; ctx.fill();
                  var xMin = Infinity, xMax = -Infinity, yMin = Infinity, yMax = -Infinity;
                  for (var xk = 0; xk < c.length; xk++) {
                    var px = mapP(c[xk].x, c[xk].y, c[xk].z);
                    if (px.x < xMin) xMin = px.x; if (px.x > xMax) xMax = px.x;
                    if (px.y < yMin) yMin = px.y; if (px.y > yMax) yMax = px.y;
                  }
                  var xr = Math.min(xMax - xMin, yMax - yMin) * 0.22;
                  xr = Math.max(Math.round(dpr * 4), Math.min(xr, Math.round(dpr * 10)));
                  var xc = (xMin + xMax) / 2, yc = (yMin + yMax) / 2;
                  ctx.strokeStyle = 'rgba(239,68,68,0.85)'; ctx.lineWidth = Math.max(1.2, dpr);
                  ctx.beginPath();
                  ctx.moveTo(xc - xr, yc - xr); ctx.lineTo(xc + xr, yc + xr);
                  ctx.moveTo(xc - xr, yc + xr); ctx.lineTo(xc + xr, yc - xr);
                  ctx.stroke();
                } else if (isHl) {
                  ctx.save();
                  ctx.shadowColor = 'rgba(250,204,21,0.8)';
                  ctx.shadowBlur = Math.max(8, dpr * 6);
                  ctx.strokeStyle = 'rgba(255,220,40,1)'; ctx.lineWidth = Math.max(3.5, dpr * 3.0);
                  ctx.stroke();
                  ctx.restore();
                  ctx.fillStyle = 'rgba(250,204,21,0.18)'; ctx.fill();
                  ctx.strokeStyle = 'rgba(255,255,255,0.7)'; ctx.lineWidth = Math.max(1.0, dpr * 0.8);
                  ctx.stroke();
                } else {
                  ctx.strokeStyle = 'rgba(74,222,128,0.45)'; ctx.lineWidth = Math.max(1.2, dpr * 1.0); ctx.stroke();
                  ctx.fillStyle = 'rgba(74,222,128,0.04)'; ctx.fill();
                }
              } else {
                ctx.strokeStyle = 'rgba(34,211,238,0.92)'; ctx.lineWidth = Math.max(1.6, dpr * 1.3); ctx.stroke();
              }
              } catch (e) { console.error('[grbl] contour ' + ci, e); }
            }
          }
        } catch (e) {
          console.error('[grbl] _drawContourOverlay', e);
        }
      }

      // Crosshair + live machine coordinates at the pointer, shown while
      // contour select is active so the user can aim clicks precisely.
      // A small localized X around the pointer (not full-canvas lines) so it
      // never obscures the toolpath.
      function _drawCrosshair(ctx, w, h, dpr) {
        var pv = state.preview;
        if (pv.contourSelectEnabled !== true) return;
        var m = pv._mousePx;
        if (!m) return;
        var px = m.x, py = m.y;
        ctx.save();
        ctx.setLineDash([]);
        var arm = Math.max(7, Math.round(dpr * 6));
        ctx.strokeStyle = 'rgba(248,113,113,0.95)';
        ctx.lineWidth = Math.max(1.2, dpr);
        ctx.beginPath();
        ctx.moveTo(px - arm, py - arm); ctx.lineTo(px + arm, py + arm);
        ctx.moveTo(px + arm, py - arm); ctx.lineTo(px - arm, py + arm);
        ctx.stroke();
        var canvas = byId('grbl_preview_canvas');
        if (canvas) {
          var cr = canvas.getBoundingClientRect();
          var mp = _screenToMachine(canvas, px + cr.left, py + cr.top);
          if (mp) {
            var mFont = Math.max(9, Math.round(dpr * 8));
            _drawLabelPill(ctx, 'X ' + formatMm(mp.x) + '  Y ' + formatMm(mp.y), px, py + Math.max(18, Math.round(dpr * 14)), mFont, dpr, true);
          }
        }
        ctx.restore();
      }

      function _drawTopView(ctx, w, h, dpr, pv) {
        var bounds = _computeBounds(pv);
        var tf = _computeTopTransformPx(w, h, pv);
        var sc = tf.sc, ox = tf.ox, oy = tf.oy, tb = tf.tb;

        function mx(mxv) { return ox + (mxv - tb.minX) * sc; }
        function my(myv) { return oy + (_viewMappedY(pv, myv) - tb.minY) * sc; }
        function topMap(x, y, z) { return { x: mx(x), y: my(y) }; }

        var mt = state.maxTravel;
        if (mt && mt.x > 0 && mt.y > 0) {
          var x0 = mx(0), y0 = my(0), x1 = mx(mt.x), y1 = my(mt.y);
          ctx.fillStyle = 'rgba(15,23,42,0.82)'; ctx.fillRect(x0, y0, mt.x * sc, mt.y * sc);
          ctx.strokeStyle = 'rgba(59,130,246,0.85)'; ctx.lineWidth = Math.max(2, dpr * 1.6);
          ctx.strokeRect(x0, y0, mt.x * sc, mt.y * sc);

          var gridStep = 10;
          var candidates = [5, 10, 20, 25, 50, 100, 200, 250, 500];
          for (var gi = 0; gi < candidates.length; gi++) { if (candidates[gi] * sc >= 35 * dpr) { gridStep = candidates[gi]; break; } }
          ctx.strokeStyle = 'rgba(148,163,184,0.18)'; ctx.lineWidth = 0.6 * dpr;
          ctx.setLineDash([Math.round(3 * dpr), Math.round(3 * dpr)]); ctx.beginPath();
          for (var gv = gridStep; gv < mt.x; gv += gridStep) { var gx = mx(gv); ctx.moveTo(gx, y0); ctx.lineTo(gx, y1); }
          for (gv = gridStep; gv < mt.y; gv += gridStep) { var gy = my(gv); ctx.moveTo(x0, gy); ctx.lineTo(x1, gy); }
          ctx.stroke(); ctx.setLineDash([]);

          var lf = Math.max(9, Math.round(dpr * 8)); ctx.font = lf + 'px monospace';
          ctx.textAlign = 'center'; ctx.textBaseline = 'top';
          for (gv = 0; gv <= mt.x; gv += gridStep) { _drawLabelPill(ctx, String(gv), mx(gv), y1 + Math.round(4 * dpr), lf, dpr); }
          ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
          for (gv = 0; gv <= mt.y; gv += gridStep) { _drawLabelPill(ctx, String(gv), x0 - Math.round(4 * dpr), my(gv), lf, dpr); }
          ctx.textAlign = 'left'; ctx.textBaseline = 'alphabetic';

          ctx.strokeStyle = '#dc2626'; ctx.lineWidth = Math.max(1.2, dpr);
          ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y0); ctx.stroke();
          ctx.strokeStyle = '#16a34a'; ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x0, y1); ctx.stroke();
        } else {
          ctx.strokeStyle = 'rgba(148,163,184,0.2)'; ctx.lineWidth = 0.6 * dpr;
          for (var gx2 = 0; gx2 <= w; gx2 += 40) { ctx.beginPath(); ctx.moveTo(gx2, 0); ctx.lineTo(gx2, h); ctx.stroke(); }
          for (var gy2 = 0; gy2 <= h; gy2 += 40) { ctx.beginPath(); ctx.moveTo(0, gy2); ctx.lineTo(w, gy2); ctx.stroke(); }
          ctx.strokeStyle = '#dc2626'; ctx.lineWidth = Math.max(1.2, dpr);
          ctx.beginPath(); ctx.moveTo(0, my(0)); ctx.lineTo(w, my(0)); ctx.stroke();
          ctx.strokeStyle = '#16a34a'; ctx.beginPath(); ctx.moveTo(mx(0), 0); ctx.lineTo(mx(0), h); ctx.stroke();
        }

        var lp = pv.lightPreview, mode = pv.previewMode || 'auto', segs = pv.segments;
        var useLight = lp && isFinite(lp.minX) && isFinite(lp.maxX) && isFinite(lp.minY) && isFinite(lp.maxY) &&
          (mode === 'frame' || mode === 'wireframe' || (mode === 'auto' && (!segs || !segs.length)));
        if (useLight) {
          var lpsc = tf.sc;
          var lpx = (w - tb.spanX * lpsc) / 2 + (pv.panX || 0) * lpsc;
          var lpy = (h - tb.spanY * lpsc) / 2 + (pv.panY || 0) * lpsc;
          function lpmX(v) { return lpx + (v - tb.minX) * lpsc; }
          function lpmY(v) { return lpy + (_viewMappedY(pv, v) - tb.minY) * lpsc; }
          var lpw = tb.spanX * lpsc, lph = tb.spanY * lpsc;

          if (mode === 'frame' || !(lp.contours && lp.contours.length)) {
            ctx.strokeStyle = 'rgba(34,211,238,0.9)'; ctx.fillStyle = 'rgba(34,211,238,0.08)';
            ctx.lineWidth = Math.max(2, dpr * 1.6); ctx.fillRect(lpx, lpy, lpw, lph); ctx.strokeRect(lpx, lpy, lpw, lph);
            ctx.strokeStyle = 'rgba(148,163,184,0.5)'; ctx.lineWidth = Math.max(1, dpr * 0.8);
            ctx.setLineDash([Math.round(4 * dpr), Math.round(4 * dpr)]); ctx.beginPath();
            ctx.moveTo(lpx + lpw / 2, lpy); ctx.lineTo(lpx + lpw / 2, lpy + lph);
            ctx.moveTo(lpx, lpy + lph / 2); ctx.lineTo(lpx + lpw, lpy + lph / 2);
            ctx.stroke(); ctx.setLineDash([]);
          }
          if (mode === 'frame') {
            var dtFont = Math.max(10, Math.round(dpr * 9));
            _drawLabelPill(ctx, 'W=' + formatMm(bounds.spanX) + '  H=' + formatMm(bounds.spanY) + ' mm',
              Math.round(w / 2), Math.round(Math.max(6, lpy - Math.max(15, dtFont + 6))), dtFont, dpr);
          } else if (lp.contours && lp.contours.length) {
            function lpMap(x, y, z) { return { x: lpmX(x), y: lpmY(y) }; }
            _drawContourOverlay(ctx, dpr, pv, lpMap);
            var otFont = Math.max(10, Math.round(dpr * 9));
            _drawLabelPill(ctx, String(lp.contours.length) + ' pc  ' + formatMm(bounds.spanX) + ' x ' + formatMm(bounds.spanY) + ' mm',
              Math.round(w / 2), Math.round(Math.max(6, lpy - Math.max(15, otFont + 6))), otFont, dpr);
          }
          if (isFinite(lp.firstCutX) && isFinite(lp.firstCutY)) {
            var jsx = lpmX(lp.firstCutX), jsy = lpmY(lp.firstCutY);
            ctx.beginPath(); ctx.arc(jsx, jsy, Math.max(7, Math.round(dpr * 5)), 0, Math.PI * 2);
            ctx.strokeStyle = '#f59e0b'; ctx.lineWidth = Math.max(2, dpr * 1.5); ctx.stroke();
            ctx.beginPath(); ctx.arc(jsx, jsy, Math.max(3, Math.round(dpr * 2)), 0, Math.PI * 2);
            ctx.fillStyle = '#fbbf24'; ctx.fill();
            var stFont = Math.max(9, Math.round(dpr * 8)); ctx.font = 'bold ' + stFont + 'px monospace';
            ctx.fillStyle = '#f59e0b'; ctx.textAlign = 'center';
            ctx.fillText('START', jsx, jsy - Math.max(11, Math.round(dpr * 9)));
          }
          _drawCrosshair(ctx, w, h, dpr);
          updatePreviewHud(sc, bounds.spanX, bounds.spanY);
          return;
        }

        if (segs && segs.length > 0) {
          var dimSel = pv.contourSelectEnabled === true;
          if (dimSel) { ctx.save(); ctx.globalAlpha = 0.22; }
          _drawSegments(ctx, _playheadSegs(segs), dpr, topMap);
          if (dimSel) ctx.restore();
        }
        _drawContourOverlay(ctx, dpr, pv, topMap);

        var ox0 = mx(0), oy0 = my(0);
        ctx.strokeStyle = '#dc2626'; ctx.lineWidth = Math.max(1.2, dpr);
        ctx.beginPath(); ctx.moveTo(Math.max(0, ox0 - 14 * dpr), oy0); ctx.lineTo(Math.min(w, ox0 + 14 * dpr), oy0); ctx.stroke();
        ctx.strokeStyle = '#16a34a';
        ctx.beginPath(); ctx.moveTo(ox0, Math.max(0, oy0 - 14 * dpr)); ctx.lineTo(ox0, Math.min(h, oy0 + 14 * dpr)); ctx.stroke();
        ctx.fillStyle = '#22c55e'; ctx.beginPath(); ctx.arc(ox0, oy0, Math.max(3, Math.round(dpr * 2.5)), 0, Math.PI * 2); ctx.fill();

        _drawPlayhead(ctx, topMap, dpr);

        if (pv.hasPath && segs && segs.length > 0) {
          var fs = segs[0].a, fsx = mx(fs.x), fsy = my(fs.y);
          if (isFinite(fsx) && isFinite(fsy)) {
            ctx.fillStyle = '#06b6d4'; ctx.beginPath(); ctx.arc(fsx, fsy, Math.max(4, Math.round(dpr * 3)), 0, Math.PI * 2); ctx.fill();
          }
        }
        _drawCrosshair(ctx, w, h, dpr);
        updatePreviewHud(sc, bounds.spanX, bounds.spanY);
      }

      function _drawIsoView(ctx, w, h, dpr, pv) {
        var bounds = _computeBounds(pv);
        var tb = _viewTransformBounds(pv, bounds);
        var margin = 28, availW = Math.max(20, w - margin * 2), availH = Math.max(20, h - margin * 2);
        var cos30 = Math.cos(Math.PI / 6), sin30 = Math.sin(Math.PI / 6);

        function isoX(x, y) { return (x - y) * cos30; }
        function isoY(x, y, z) { return (x + y) * sin30 - (z || 0); }

        var isoMinX = Infinity, isoMaxX = -Infinity, isoMinY = Infinity, isoMaxY = -Infinity;
        var corners = [[tb.minX, tb.minY], [tb.maxX, tb.minY], [tb.maxX, tb.maxY], [tb.minX, tb.maxY]];
        for (var ic = 0; ic < corners.length; ic++) {
          var ix = isoX(corners[ic][0], corners[ic][1]), iy = isoY(corners[ic][0], corners[ic][1], 0);
          if (ix < isoMinX) isoMinX = ix; if (ix > isoMaxX) isoMaxX = ix;
          if (iy < isoMinY) isoMinY = iy; if (iy > isoMaxY) isoMaxY = iy;
          var iz = isoY(corners[ic][0], corners[ic][1], bounds.spanZ);
          if (iz < isoMinY) isoMinY = iz; if (iz > isoMaxY) isoMaxY = iz;
        }

        var zoomMult = (pv.fitScale > 0 && pv.scale > 0) ? (pv.scale / pv.fitScale) : 1;
        var sc = Math.max(0.01, Math.min(5000, Math.min(availW / Math.max(0.01, isoMaxX - isoMinX), availH / Math.max(0.01, isoMaxY - isoMinY)) * zoomMult * 1.2));
        var cx = w / 2, cy = h / 2;

        function ym(y) { return _viewMappedY(pv, y); }
        function imx(x, y) { return cx + isoX(x, ym(y)) * sc; }
        function imy(x, y, z) { return cy - isoY(x, ym(y), z) * sc; }
        function isoMap(x, y, z) { return { x: imx(x, y), y: imy(x, y, z) }; }

        var mt = state.maxTravel;
        if (mt && mt.x > 0 && mt.y > 0) {
          var mz = (mt.z > 0 ? mt.z : 30), mxx = mt.x, myy = mt.y;
          var bot = [[imx(0,0),imy(0,0,0)], [imx(mxx,0),imy(mxx,0,0)], [imx(mxx,myy),imy(mxx,myy,0)], [imx(0,myy),imy(0,myy,0)]];
          var top = [[imx(0,0),imy(0,0,mz)], [imx(mxx,0),imy(mxx,0,mz)], [imx(mxx,myy),imy(mxx,myy,mz)], [imx(0,myy),imy(0,myy,mz)]];
          ctx.fillStyle = 'rgba(15,23,42,0.78)';
          ctx.beginPath(); ctx.moveTo(bot[0][0], bot[0][1]); ctx.lineTo(bot[1][0], bot[1][1]); ctx.lineTo(bot[2][0], bot[2][1]); ctx.lineTo(bot[3][0], bot[3][1]); ctx.closePath(); ctx.fill();
          ctx.beginPath(); ctx.moveTo(top[0][0], top[0][1]); ctx.lineTo(top[1][0], top[1][1]); ctx.lineTo(top[2][0], top[2][1]); ctx.lineTo(top[3][0], top[3][1]); ctx.closePath(); ctx.fill();
          ctx.strokeStyle = 'rgba(59,130,246,0.75)'; ctx.lineWidth = Math.max(1.5, dpr * 1.2);
          ctx.beginPath(); ctx.moveTo(bot[0][0], bot[0][1]); ctx.lineTo(bot[1][0], bot[1][1]); ctx.lineTo(bot[2][0], bot[2][1]); ctx.lineTo(bot[3][0], bot[3][1]); ctx.closePath(); ctx.stroke();
          ctx.strokeStyle = 'rgba(30,64,175,0.55)'; ctx.lineWidth = Math.max(0.8, dpr * 0.6);
          ctx.setLineDash([Math.round(3 * dpr), Math.round(3 * dpr)]); ctx.beginPath();
          ctx.moveTo(bot[0][0], bot[0][1]); ctx.lineTo(top[0][0], top[0][1]);
          ctx.moveTo(bot[1][0], bot[1][1]); ctx.lineTo(top[1][0], top[1][1]);
          ctx.moveTo(bot[2][0], bot[2][1]); ctx.lineTo(top[2][0], top[2][1]);
          ctx.moveTo(bot[3][0], bot[3][1]); ctx.lineTo(top[3][0], top[3][1]);
          ctx.stroke(); ctx.setLineDash([]);
          ctx.strokeStyle = '#dc2626'; ctx.lineWidth = Math.max(1.2, dpr);
          ctx.beginPath(); ctx.moveTo(bot[0][0], bot[0][1]); ctx.lineTo(bot[1][0], bot[1][1]); ctx.stroke();
          ctx.strokeStyle = '#16a34a';
          ctx.beginPath(); ctx.moveTo(bot[0][0], bot[0][1]); ctx.lineTo(bot[3][0], bot[3][1]); ctx.stroke();
        }

        var lp = pv.lightPreview, mode = pv.previewMode || 'auto', segs = pv.segments;
        var useLight = lp && isFinite(lp.minX) && isFinite(lp.maxX) && isFinite(lp.minY) && isFinite(lp.maxY) &&
          (mode === 'frame' || mode === 'wireframe' || (mode === 'auto' && (!segs || !segs.length)));
        if (useLight) {
          if (lp.contours && lp.contours.length) {
            _drawContourOverlay(ctx, dpr, pv, isoMap);
          }
          updatePreviewHud(sc, bounds.spanX, bounds.spanY);
          return;
        }

        if (segs && segs.length > 0) {
          var dimSelIso = pv.contourSelectEnabled === true;
          if (dimSelIso) { ctx.save(); ctx.globalAlpha = 0.22; }
          _drawSegments(ctx, _playheadSegs(segs), dpr, isoMap);
          if (dimSelIso) ctx.restore();
        }
        _drawContourOverlay(ctx, dpr, pv, isoMap);
        _drawPlayhead(ctx, isoMap, dpr);
        updatePreviewHud(sc, bounds.spanX, bounds.spanY);
      }

      // Side (XZ) view: X horizontal, Z vertical with the permanent mirror-Y so
      // Z=0 (material top) is up and negative depth points down — ideal to see
      // CNC cut depth/profile.
      function _drawSideView(ctx, w, h, dpr, pv) {
        var bounds = _computeBounds(pv);
        var margin = 28, availW = Math.max(20, w - margin * 2), availH = Math.max(20, h - margin * 2);
        var zoomMult = (pv.fitScale > 0 && pv.scale > 0) ? (pv.scale / pv.fitScale) : 1;
        var spanX = Math.max(0.01, bounds.maxX - bounds.minX);
        var zTopM = _viewMappedY(pv, bounds.maxZ || 0);
        var zBotM = _viewMappedY(pv, bounds.minZ || 0);
        var spanZ = Math.max(0.01, zBotM - zTopM);
        var sc = Math.max(0.01, Math.min(5000, Math.min(availW / spanX, availH / spanZ) * zoomMult * 1.2));
        var ox = margin + (availW - spanX * sc) / 2 + (pv.panX || 0) * sc;
        var oy = margin + (availH - spanZ * sc) / 2 + (pv.panY || 0) * sc;
        function mx(mxv) { return ox + (mxv - bounds.minX) * sc; }
        function my(mzv) { return oy + (_viewMappedY(pv, mzv) - zTopM) * sc; }
        function sideMap(x, y, z) { return { x: mx(x), y: my(z || 0) }; }

        var mt = state.maxTravel;
        if (mt && mt.x > 0) {
          var depth = (mt.z > 0) ? mt.z : Math.max(10, bounds.maxZ - bounds.minZ);
          var bx0 = mx(0), bx1 = mx(mt.x), bzT = my(0), bzB = my(-depth);
          ctx.fillStyle = 'rgba(15,23,42,0.78)';
          ctx.fillRect(Math.min(bx0, bx1), Math.min(bzT, bzB), Math.abs(bx1 - bx0), Math.abs(bzB - bzT));
          ctx.strokeStyle = 'rgba(59,130,246,0.75)'; ctx.lineWidth = Math.max(1.5, dpr * 1.2);
          ctx.strokeRect(Math.min(bx0, bx1), Math.min(bzT, bzB), Math.abs(bx1 - bx0), Math.abs(bzB - bzT));
          // Material top line at Z=0
          ctx.strokeStyle = 'rgba(148,163,184,0.7)'; ctx.setLineDash([Math.round(4 * dpr), Math.round(4 * dpr)]);
          ctx.beginPath(); ctx.moveTo(Math.min(bx0, bx1), bzT); ctx.lineTo(Math.max(bx0, bx1), bzT); ctx.stroke();
          ctx.setLineDash([]);
        }

        var lp = pv.lightPreview, mode = pv.previewMode || 'auto', segs = pv.segments;
        if (segs && segs.length > 0) _drawSegments(ctx, _playheadSegs(segs), dpr, sideMap);

        var oxp = mx(0), oyp = my(0);
        ctx.strokeStyle = '#dc2626'; ctx.lineWidth = Math.max(1.2, dpr);
        ctx.beginPath(); ctx.moveTo(Math.max(0, oxp - 14 * dpr), oyp); ctx.lineTo(Math.min(w, oxp + 14 * dpr), oyp); ctx.stroke();
        ctx.strokeStyle = '#16a34a';
        ctx.beginPath(); ctx.moveTo(oxp, Math.max(0, oyp - 14 * dpr)); ctx.lineTo(oxp, Math.min(h, oyp + 14 * dpr)); ctx.stroke();
        ctx.fillStyle = '#22c55e'; ctx.beginPath(); ctx.arc(oxp, oyp, Math.max(3, Math.round(dpr * 2.5)), 0, Math.PI * 2); ctx.fill();

        _drawPlayhead(ctx, sideMap, dpr);
        updatePreviewHud(sc, bounds.spanX, bounds.spanZ);
      }

      function formatMm(num) {
        var n = parseFloat(num);
        if (!isFinite(n)) n = 0;
        return String(Math.round(n * 10) / 10);
      }

      function updatePreviewHud(scale, spanX, spanY) {
        var el = byId('grbl_preview_hud');
        if (!el) return;
        var pv = state.preview;
        var baseScale = Math.max(0.001, parseFloat(pv.fitScale || pv.scale || 1));
        var curScale = Math.max(0.001, parseFloat(pv.scale || 1));
        var zoomPct = Math.max(1, Math.round((curScale / baseScale) * 100));
        var mmPer100 = (scale > 0) ? (100 / scale) : 0;
        // Always show the loaded piece size (W x H mm) regardless of view mode.
        el.textContent = 'W ' + formatMm(spanX) + ' x H ' + formatMm(spanY) + ' mm'
          + ' | Zoom ' + String(zoomPct) + '%'
          + ' | 100px=' + (mmPer100 > 0 ? formatMm(mmPer100) : '--') + ' mm';
      }
)GRBL"
