R"GRBL(
      function parseLightAxisValue(line, axis) {
        var idx = line.indexOf(axis);
        if (idx < 0) return null;
        var i = idx + 1;
        var end = line.length;
        while (i < end && line.charCodeAt(i) === 32) i++;
        var s = '';
        while (i < end) {
          var ch = line.charAt(i);
          var code = line.charCodeAt(i);
          var ok = (ch === '+' || ch === '-' || ch === '.' || (code >= 48 && code <= 57));
          if (!ok) break;
          s += ch;
          i++;
        }
        if (!s || s === '+' || s === '-' || s === '.') return null;
        var n = parseFloat(s);
        return isFinite(n) ? n : null;
      }

      function simplifyPolylineLight(points, tol) {
        if (!points || points.length < 3) return points || [];
        var out = [points[0]];
        var tol2 = tol * tol;
        for (var i = 1; i < points.length - 1; i++) {
          var p = points[i];
          var prev = out[out.length - 1];
          var dx = p.x - prev.x;
          var dy = p.y - prev.y;
          if ((dx * dx + dy * dy) >= tol2) out.push(p);
        }
        out.push(points[points.length - 1]);
        return out;
      }

      function buildLightPieceContoursFromGcode(text) {
        var src = String(text || '');
        if (!src || src.length > GRBL_LIGHT_PREVIEW_MAX_SCAN_BYTES) return null;

        try {
        var absolute = true;
        var laserEnabled = false;
        var spindlePower = 0;
        var lastSPower = 0;
        var motion = 'G0';
        var lastMode = '';
        var curX = 0, curY = 0;
        var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
        var rawContours = [];
        var seg = null;
        var firstCutX = NaN, firstCutY = NaN;
        var maxLines = 500000;
        var lineCount = 0;

        function finalizeSeg(rx, ry) {
          if (!seg) return;
          if (seg.pts.length >= 2) {
            var first = seg.pts[0];
            var last = seg.pts[seg.pts.length - 1];
            var span = Math.max(0.001, Math.max(seg.maxX - seg.minX, seg.maxY - seg.minY));
            // Size-relative tolerance with a small absolute floor: catches
            // lead-in/lead-out tabs on large pieces without treating open
            // strokes (engraving/fill) as shapes.
            var tol = Math.max(2, span * 0.10);
            function near(ax, ay, bx, by) {
              var dx = ax - bx, dy = ay - by;
              return (dx * dx + dy * dy) < (tol * tol);
            }
            // Closed loop when the stroke returns to its start (pre-cut
            // position) or to its first cut point — OR when the following
            // rapid (G0) returns to either, since CAMs often emit the closing
            // move of a contour as a rapid.
            var closed = near(last.x, last.y, seg.startX, seg.startY) ||
                         near(last.x, last.y, first.x, first.y);
            if (!closed && isFinite(rx) && isFinite(ry)) {
              closed = near(rx, ry, seg.startX, seg.startY) ||
                       near(rx, ry, first.x, first.y);
            }
            if (closed) {
              var simp = simplifyPolylineLight(seg.pts, 0.15);
              if (simp.length >= 2) {
                rawContours.push({
                  points: simp,
                  minX: seg.minX, maxX: seg.maxX,
                  minY: seg.minY, maxY: seg.maxY,
                  area: Math.max(0.01, (seg.maxX - seg.minX) * (seg.maxY - seg.minY)),
                  closed: true,
                  startLine: seg.startLine,
                  endLine: lineCount
                });
              }
            }
          }
          seg = null;
        }

        function startSeg() {
          if (!seg) {
            seg = { pts: [], minX: Infinity, maxX: -Infinity, minY: Infinity, maxY: -Infinity, startX: curX, startY: curY, startLine: lineCount };
          }
        }

        function segPt(x, y) {
          if (!seg) return;
          seg.pts.push({ x: x, y: y });
          if (x < seg.minX) seg.minX = x;
          if (x > seg.maxX) seg.maxX = x;
          if (y < seg.minY) seg.minY = y;
          if (y > seg.maxY) seg.maxY = y;
        }

        var pos = 0;
        while (pos < src.length && lineCount < maxLines) {
          lineCount++;
          var end = src.indexOf('\n', pos);
          if (end < 0) end = src.length;
          var line = src.substring(pos, end);
          if (end > pos && src.charCodeAt(end - 1) === 13) line = line.slice(0, -1);
          pos = end + 1;
          line = line.replace(/\([^)]*\)/g, ' ');
          var sem = line.indexOf(';');
          if (sem >= 0) line = line.slice(0, sem);
          line = line.trim();
          if (!line) continue;
          line = line.toUpperCase();

          if (line.indexOf('G90') >= 0) absolute = true;
          if (line.indexOf('G91') >= 0) absolute = false;

          if (line.indexOf('M5') >= 0) {
            laserEnabled = false;
            // S is modal in GRBL: M5 stops the spindle but keeps the last S
            // value, and a following M3 resumes at that power. LightBurn
            // relies on this (it only emits S on the first piece of a layer),
            // so spindlePower must NOT be reset here or modal-S pieces are
            // dropped from the light preview.
            lastMode = '';
          }
          var hasM3 = line.indexOf('M3') >= 0;
          var hasM4 = line.indexOf('M4') >= 0;
          if (hasM3) {
            laserEnabled = true;
            lastMode = 'M3';
          }
          if (hasM4) {
            laserEnabled = true;
            lastMode = 'M4';
          }

          var sv = parseLightAxisValue(line, 'S');
          if (sv !== null) {
            spindlePower = Math.max(0, sv);
            if (spindlePower <= 0.0001) {
              laserEnabled = false;
            } else {
              laserEnabled = true;
            }
            lastSPower = spindlePower;
          }

          var gCodesM = line.match(/G(\d+)/g);
          if (gCodesM) {
            for (var gi = 0; gi < gCodesM.length; gi++) {
              var gv = parseInt(gCodesM[gi].slice(1), 10);
              if (gv === 0) motion = 'G0';
              else if (gv >= 1 && gv <= 3) motion = 'G1';
            }
          }

          var xv = parseLightAxisValue(line, 'X');
          var yv = parseLightAxisValue(line, 'Y');
          if (xv === null && yv === null) continue;

          var nx = (xv === null) ? curX : (absolute ? xv : (curX + xv));
          var ny = (yv === null) ? curY : (absolute ? yv : (curY + yv));
          if (!isFinite(nx) || !isFinite(ny)) continue;

          if (nx < minX) minX = nx;
          if (nx > maxX) maxX = nx;
          if (ny < minY) minY = ny;
          if (ny > maxY) maxY = ny;

          var isCut = (motion !== 'G0') && laserEnabled && (spindlePower > 0.0001);

          if (motion === 'G0') finalizeSeg(nx, ny);

          if (isCut) {
            if (!isFinite(firstCutX)) { firstCutX = nx; firstCutY = ny; }
            startSeg();
            segPt(nx, ny);
          }

          curX = nx;
          curY = ny;
        }
        finalizeSeg();

        if (!isFinite(minX) || !isFinite(maxX) || !isFinite(minY) || !isFinite(maxY)) return null;

        var contours = [];
        var contourRanges = [];

        // Outlines show only closed drawings (outer cuts, squares, circles,
        // closed pockets). `rawContours` already holds only closed loops, so
        // just sort them by size and cap the count. Degenerate zero-width
        // loops (area < 0.1) are dropped.
        if (rawContours.length) {
          var closedLoops = rawContours.slice().sort(function(a, b) { return b.area - a.area; });
          if (closedLoops.length > GRBL_LIGHT_PREVIEW_MAX_CONTOURS) {
            closedLoops.length = GRBL_LIGHT_PREVIEW_MAX_CONTOURS;
          }
          for (var oj = 0; oj < closedLoops.length; oj++) {
            if (closedLoops[oj].area < 0.1) continue;
            contours.push(closedLoops[oj].points);
            contourRanges.push({ startLine: closedLoops[oj].startLine || 0, endLine: closedLoops[oj].endLine || 0 });
          }
        }

        return {
          minX: minX, maxX: maxX, minY: minY, maxY: maxY,
          firstCutX: firstCutX, firstCutY: firstCutY,
          contours: contours,
          contourRanges: contourRanges
        };
        } catch(e) { return null; }
      }

      // Segment-derived piece contours: reuse the fully-parsed toolpath
      // (segments of {a,b,rapid,lineNo}) to find closed cut loops. This is
      // the fallback path when the light scan (`buildLightPieceContoursFromGcode`)
      // cannot see shapes — e.g. arcs interpolated by the parser into many small
      // segments, or spindle-only cuts where the light scan's S-power heuristic
      // misses them. Segment `a`/`b` are point indices (worker) or point objects
      // (inline parser), so both forms are accepted.
      function buildPieceContoursFromSegments(pts, segs, maxPieces) {
        if (!segs || !segs.length) return { contours: [], ranges: [] };
        var cap = (maxPieces > 0) ? maxPieces : GRBL_LIGHT_PREVIEW_MAX_CONTOURS;
        var loopCap = 4000;

        function ptRef(ref) {
          if (ref === null || ref === undefined) return null;
          return (typeof ref === 'number') ? (pts[ref] || null) : ref;
        }

        var contours = [];
        var ranges = [];
        var maxActiveGroups = 5000;

        function evaluateGroup(g) {
          if (contours.length >= cap) return true;
          var gpts = g.pts;
          if (!gpts || gpts.length < 3) return false;
          var first = gpts[0];
          var last = gpts[gpts.length - 1];
          var minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
          var j;
          for (j = 0; j < gpts.length; j++) {
            var p = gpts[j];
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
          }
          var span = Math.max(0.001, Math.max(maxX - minX, maxY - minY));
          var tol = Math.max(2, span * 0.10);
          var dxf = first.x - last.x, dyf = first.y - last.y;
          var closed = ((dxf * dxf + dyf * dyf) < (tol * tol));
          if (!closed) {
            var firstPt = gpts[1];
            var dxc = firstPt.x - last.x, dyc = firstPt.y - last.y;
            closed = ((dxc * dxc + dyc * dyc) < (tol * tol));
          }
          if (!closed) return false;
          var boxArea = Math.max(0, maxX - minX) * Math.max(0, maxY - minY);
          if (boxArea < 0.1) return false;
          contours.push(gpts);
          ranges.push({ startLine: g.startLine || 0, endLine: g.endLine || (g.startLine || 0) });
          return contours.length >= cap;
        }

        var groupsOpen = 0;
        var cur = null;
        var done = false;
        var i;
        for (i = 0; i < segs.length && !done; i++) {
          var s = segs[i];
          if (!s) continue;
          if (s.rapid) {
            if (cur) {
              if (evaluateGroup(cur)) done = true;
              cur = null;
            }
            continue;
          }
          var pa = ptRef(s.a);
          var pb = ptRef(s.b);
          if (!pa || !pb) {
            if (cur) {
              if (evaluateGroup(cur)) done = true;
              cur = null;
            }
            continue;
          }
          if (!cur || (s.lineNo - cur.startLine) > 40 || (cur.pts.length >= loopCap)) {
            if (cur) {
              if (evaluateGroup(cur)) done = true;
              cur = null;
            }
            if (done) break;
            if (groupsOpen >= maxActiveGroups) continue;
            groupsOpen++;
            cur = { pts: [pa], startLine: s.lineNo, startX: pa.x, startY: pa.y, endLine: s.lineNo };
          }
          cur.pts.push(pb);
          cur.endLine = s.lineNo;
        }
        if (cur && !done) evaluateGroup(cur);

        return { contours: contours, ranges: ranges };
      }

      function quickWireframe(text) {
        try {
          var lines = String(text || '').split(/\r?\n/);
          var minX = Infinity, maxX = -Infinity;
          var minY = Infinity, maxY = -Infinity;
          var minZ = Infinity, maxZ = -Infinity;
          var sampleStep = Math.max(1, Math.floor(lines.length / 1000));
          var pts = [];
          var count = 0;
          var x = 0, y = 0, z = 0;
          var absoluteMode = true;
          var inGcode = false;

          for (var wi = 0; wi < lines.length; wi++) {
            var l = lines[wi].trim();
            if (l.length === 0 || l[0] === ';' || l[0] === '(') continue;
            if (l[0] === 'G') {
              inGcode = true;
              var gCode = 0;
              var gm = l.match(/G(\d+)/);
              if (gm) gCode = parseInt(gm[1], 10);
              if (gCode === 90) absoluteMode = true;
              else if (gCode === 91) absoluteMode = false;
            }
            if (!inGcode) continue;

            // Machine state is advanced exactly once per line below; do NOT
            // re-apply X/Y/Z in the sampling block or incremental (G91) paths
            // double-count every sampled line.

            var cx = parseFloat((l.match(/[Xx]([-+]?\d*\.?\d+)/) || [])[1]);
            var cy = parseFloat((l.match(/[Yy]([-+]?\d*\.?\d+)/) || [])[1]);
            var cz = parseFloat((l.match(/[Zz]([-+]?\d*\.?\d+)/) || [])[1]);
            if (!isNaN(cx)) { var ax = absoluteMode ? cx : x + cx; if (ax < minX) minX = ax; if (ax > maxX) maxX = ax; x = ax; }
            if (!isNaN(cy)) { var ay = absoluteMode ? cy : y + cy; if (ay < minY) minY = ay; if (ay > maxY) maxY = ay; y = ay; }
            if (!isNaN(cz)) { var az = absoluteMode ? cz : z + cz; if (az < minZ) minZ = az; if (az > maxZ) maxZ = az; z = az; }

            if (wi % sampleStep === 0 && count < 1000) {
              pts.push({ x: x, y: y, z: z });
              count++;
            }
          }

          if (!isFinite(minX)) { minX = 0; maxX = 1; minY = 0; maxY = 1; minZ = 0; maxZ = 1; }
          return {
            minX: minX, maxX: maxX,
            minY: minY, maxY: maxY,
            minZ: minZ, maxZ: maxZ,
            wireframePoints: pts,
            contours: []
          };
        } catch(e) { return null; }
      }

      // Filter the source G-code so only the ENABLED contours are cut, while
      // preserving the full-job coordinate frame, travel path and modal
      // spindle/feed state. The old filter removed whole line ranges, which
      // also deleted the G0 rapid that positions the machine at the next
      // contour (the range endLine points at the following G0) — so kept
      // pieces were cut from the wrong position. Here every G0
      // (approach/retract/return) and every state line (G90, M3/M5, comments)
      // is kept; only the G1/G2/G3 motion of disabled contours is dropped.
      // S and F are MODAL in GRBL and many CAMs (LightBurn etc.) set them only
      // once on the first cut line of the job — which may belong to a disabled
      // contour. If that line were dropped, kept contours would inherit a
      // stale power/feed (e.g. S0 from the end of a previous job). So the last
      // seen S/F value is re-emitted as a standalone state line right before
      // each enabled contour, guaranteeing every cut uses the original speed.
      function filterContoursPreserveCoords(text, ranges, disabled) {
        var lines = String(text || '').split(/\r?\n/);
        var enabledRanges = [], disabledRanges = [];
        var hasDisabledRanges = false;
        for (var ri = 0; ri < ranges.length; ri++) {
          var rr = ranges[ri];
          if (!rr || !(rr.startLine > 0) || !(rr.endLine > 0)) continue;
          if (disabled && disabled[ri]) {
            disabledRanges.push(rr);
            hasDisabledRanges = true;
          } else {
            enabledRanges.push(rr);
          }
        }

        function inRanges(ln, list) {
          for (var i = 0; i < list.length; i++) {
            if (ln >= list[i].startLine && ln <= list[i].endLine) return true;
          }
          return false;
        }
        function gCodes(line) {
          var m = line.match(/G(\d+)/g);
          return m || [];
        }
        function isRapid(line) {
          var gs = gCodes(line);
          for (var i = 0; i < gs.length; i++) {
            if (parseInt(gs[i].slice(1), 10) === 0) return true;
          }
          return false;
        }
        function isCutMotion(line) {
          var gs = gCodes(line);
          for (var i = 0; i < gs.length; i++) {
            var v = parseInt(gs[i].slice(1), 10);
            if (v === 1 || v === 2 || v === 3) return true;
          }
          return false;
        }
        // Track the modal S and F values set by any line so a kept contour
        // that inherits them from an earlier (possibly disabled) contour still
        // cuts at the original spindle speed / laser power and feed.
        var lastS = null, lastF = null;
        function updateModal(line) {
          var words = line.match(/[SF][-+]?\d*\.?\d+/g) || [];
          for (var i = 0; i < words.length; i++) {
            var w = words[i];
            if (w.charAt(0) === 'S') lastS = w.slice(1);
            else if (w.charAt(0) === 'F') lastF = w.slice(1);
          }
        }

        var out = [];
        var block = [];
        var blockHasEnabled = false;
        var blockHasDisabled = false;
        var blockS = null, blockF = null;

        function flushBlock() {
          if (!block.length) return;
          if (blockHasDisabled && !blockHasEnabled) {
            for (var i = 0; i < block.length; i++) {
              if (!block[i].motion) out.push(block[i].raw);
            }
          } else {
            // Enabled (or mixed) contour block: keep it whole, and before the
            // kept cuts inject the modal S/F that the original file had active
            // at this point, so power/feed can never be lost by dropping lines.
            if (hasDisabledRanges && blockHasEnabled && (blockS !== null || blockF !== null)) {
              var inj = '';
              if (blockS !== null) inj += 'S' + blockS;
              if (blockF !== null) { if (inj) inj += ' '; inj += 'F' + blockF; }
              out.push(inj);
            }
            for (var i = 0; i < block.length; i++) out.push(block[i].raw);
          }
          block = [];
          blockHasEnabled = false;
          blockHasDisabled = false;
          blockS = null;
          blockF = null;
        }

        for (var li = 0; li < lines.length; li++) {
          var ln = li + 1;
          var raw = lines[li];
          var up = String(raw || '').toUpperCase();
          var sem = up.indexOf(';');
          if (sem >= 0) up = up.slice(0, sem);
          up = up.replace(/\([^)]*\)/g, ' ').trim();
          if (block.length === 0) {
            blockS = lastS;
            blockF = lastF;
          }
          if (!up) {
            block.push({ raw: raw, motion: false });
            continue;
          }
          if (isRapid(up)) {
            updateModal(up);
            flushBlock();
            out.push(raw);
            continue;
          }
          updateModal(up);
          if (inRanges(ln, enabledRanges)) blockHasEnabled = true;
          if (inRanges(ln, disabledRanges)) blockHasDisabled = true;
          block.push({ raw: raw, motion: isCutMotion(up) });
        }
        flushBlock();

        return out.join('\n');
      }
)GRBL"
