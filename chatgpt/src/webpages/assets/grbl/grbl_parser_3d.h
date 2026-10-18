R"GRBL(
      function yieldToUi() {
        return new Promise(function(resolve) {
          window.setTimeout(resolve, 0);
        });
      }

      async function parseToolpath3dAsync(text, onProgress, parseJobId) {
        // Keep the source as one string and iterate lines incrementally — never
        // split() a large file (a 9MB/1M-line file would allocate ~1M strings
        // and its GC blocks the whole browser).
        var srcText = String(text || '');
        var srcLen = srcText.length;
        var x = 0;
        var y = 0;
        var z = 0;
        var absoluteMode = true;
        var arcCenterAbsolute = false;
        var unitToMm = 1;
        var motionMode = 1;
        var planeMode = 17;
        var pts = [{ x: x, y: y, z: z }];
        var segs = [];
        var maxPoints = _isEsp32Browser ? 200000 : 2000000;
        // Size-adaptive downsampling budget: small files keep every line
        // (up to ~500k segments), big files progressively drop more so the
        // page stays light up to ~30 MB. Low-resource devices (Raspberry Pi
        // etc.) scale the budget down to keep memory and rendering light.
        var srcMb = srcLen / (1024 * 1024);
        var resScale = _isLowResourceDevice() ? 0.25 : 1;
        var escale = _isEsp32Browser ? 0.1 : 1;
        var pointBudget;
        if (srcMb <= 4)      pointBudget = Math.max(20000, Math.round(500000 * escale * resScale));
        else if (srcMb <= 10) pointBudget = Math.max(15000, Math.round(200000 * escale * resScale));
        else if (srcMb <= 20) pointBudget = Math.max(10000, Math.round(100000 * escale * resScale));
        else                 pointBudget = Math.max(8000, Math.round(60000 * escale * resScale));
        var stride = 1;
        var tickEngrave = 0;
        var tickRapid = 0;
        var truncated = false;
        var bMinX = Infinity, bMaxX = -Infinity;
        var bMinY = Infinity, bMaxY = -Infinity;
        var bMinZ = Infinity, bMaxZ = -Infinity;
        var arcStepByQuality = {
          low: 2.4,
          med: 1.0,
          high: 0.45
        };
        var arcTolByQuality = {
          low: 0.18,
          med: 0.08,
          high: 0.03
        };
        var arcMaxAngleByQuality = {
          low: (Math.PI / 13),
          med: (Math.PI / 20),
          high: (Math.PI / 34)
        };

        function extractAxisInMm(line, axis) {
          var v = extractAxisValue(line, axis);
          if (v === null) return null;
          return v * unitToMm;
        }

        function clamp(val, lo, hi) {
          return Math.max(lo, Math.min(hi, val));
        }

        function appendLinear(prev, next, rapid, arcFlag, lineNo, execNo, cutType) {
          // Track the true bounds even for strided-out points so the fit/pan
          // math uses the real min/max of the whole toolpath.
          if (next.x < bMinX) bMinX = next.x;
          if (next.x > bMaxX) bMaxX = next.x;
          if (next.y < bMinY) bMinY = next.y;
          if (next.y > bMaxY) bMaxY = next.y;
          if (next.z < bMinZ) bMinZ = next.z;
          if (next.z > bMaxZ) bMaxZ = next.z;
          // Dense fill (engrave strokes and raster rapid returns) is
          // downsampled; cut outlines stay intact for the preview. Each fill
          // kind keeps its own counter so the stride cannot line up with an
          // alternating G0/G1 raster phase and drop one polarity entirely.
          if (stride > 1) {
            if (rapid) { tickRapid++; if ((tickRapid % stride) !== 0) return; }
            else if (cutType === 'engrave') { tickEngrave++; if ((tickEngrave % stride) !== 0) return; }
          }
          pts.push(next);
          segs.push({ a: prev, b: next, rapid: rapid, arc: !!arcFlag, lineNo: lineNo || 0, execNo: execNo || lineNo || 0, cutType: (cutType || ''), toolOn: !!toolOn });
          segsSinceYield++;
          if (pts.length > pointBudget) {
            stride = Math.max(stride, Math.ceil(pts.length / pointBudget));
          }
        }

        function buildArcSegmentsXY(prev, next, cx, cy, cw, forceFullCircle, lineNo, execNo, cutType) {
          var dx0 = prev.x - cx;
          var dy0 = prev.y - cy;
          var radius = Math.sqrt(dx0 * dx0 + dy0 * dy0);
          if (!isFinite(radius) || radius < 0.000001) {
            appendLinear(prev, next, false, false, lineNo, execNo, cutType);
            return;
          }

          var a0 = Math.atan2(prev.y - cy, prev.x - cx);
          var a1 = Math.atan2(next.y - cy, next.x - cx);
          var delta = a1 - a0;
          if (forceFullCircle) {
            delta = cw ? (-Math.PI * 2) : (Math.PI * 2);
          } else {
            if (cw) {
              if (delta >= 0) delta -= (Math.PI * 2);
            } else {
              if (delta <= 0) delta += (Math.PI * 2);
            }
          }

          if (!isFinite(delta) || Math.abs(delta) < 0.0000001) {
            if (!forceFullCircle) {
              appendLinear(prev, next, false, false, lineNo, execNo, cutType);
              return;
            }
          }

          var arcLen = Math.abs(delta) * radius;
          var stepMm = arcStepByQuality[state.preview.arcQuality] || arcStepByQuality.med;
          var tolMm = arcTolByQuality[state.preview.arcQuality] || arcTolByQuality.med;
          var maxThetaByTol = Math.PI;
          if (radius > tolMm && tolMm > 0) {
            var c = 1 - (tolMm / radius);
            c = clamp(c, -1, 1);
            maxThetaByTol = 2 * Math.acos(c);
          }
          var maxThetaByMode = arcMaxAngleByQuality[state.preview.arcQuality] || arcMaxAngleByQuality.med;
          var maxTheta = Math.max(0.01, Math.min(maxThetaByTol, maxThetaByMode));
          var nByAngle = Math.ceil(Math.abs(delta) / maxTheta);
          var nByLength = Math.ceil(arcLen / Math.max(0.12, stepMm));
          var qualityCap = state.preview.arcQuality === 'high' ? 2200 : (state.preview.arcQuality === 'low' ? 720 : 1300);
          var n = clamp(Math.max(6, nByAngle, nByLength), 6, qualityCap);

          var last = { x: prev.x, y: prev.y, z: prev.z };
          var k;
          for (k = 1; k <= n; k++) {
            if (pts.length >= maxPoints) {
              truncated = true;
              break;
            }
            var t = k / n;
            var a = a0 + (delta * t);
            var np = {
              x: cx + Math.cos(a) * radius,
              y: cy + Math.sin(a) * radius,
              z: prev.z + (next.z - prev.z) * t
            };
            appendLinear(last, np, false, true, lineNo, execNo, cutType);
            last = np;
          }
        }

        function chooseArcCenterFromR(prev, next, rVal, cw) {
          var dx = next.x - prev.x;
          var dy = next.y - prev.y;
          var chord = Math.sqrt(dx * dx + dy * dy);
          if (!isFinite(chord) || chord < 0.000001) return null;

          var rAbs = Math.abs(rVal);
          if (chord > (2 * rAbs)) rAbs = chord * 0.5;
          var h2 = (rAbs * rAbs) - ((chord * chord) / 4);
          if (h2 < 0) h2 = 0;
          var h = Math.sqrt(h2);
          var mx = (prev.x + next.x) * 0.5;
          var my = (prev.y + next.y) * 0.5;
          var ux = -dy / chord;
          var uy = dx / chord;

          var candidates = [
            { cx: mx + (ux * h), cy: my + (uy * h) },
            { cx: mx - (ux * h), cy: my - (uy * h) }
          ];

          function getSweep(c) {
            var a0 = Math.atan2(prev.y - c.cy, prev.x - c.cx);
            var a1 = Math.atan2(next.y - c.cy, next.x - c.cx);
            var d = a1 - a0;
            if (cw) {
              if (d >= 0) d -= (Math.PI * 2);
            } else {
              if (d <= 0) d += (Math.PI * 2);
            }
            return d;
          }

          var d0 = getSweep(candidates[0]);
          var d1 = getSweep(candidates[1]);
          var longArc = rVal < 0;
          if (longArc) {
            return Math.abs(d0) >= Math.abs(d1) ? candidates[0] : candidates[1];
          }
          return Math.abs(d0) <= Math.abs(d1) ? candidates[0] : candidates[1];
        }

        var i;
        var execNo = 0;
        // Count lines in a cheap char scan (no split / no per-line allocations).
        var total = 0;
        var t;
        for (t = 0; t < srcLen; t++) {
          if (srcText.charCodeAt(t) === 10) total++;
        }
        // Raster files keep ~1 segment per line, so a line-based pre-stride
        // keeps the stored path near pointBudget. Arc-heavy files are covered
        // by the adaptive bump in appendLinear.
        stride = Math.max(1, Math.ceil(total / pointBudget));
        var chunkCounter = 0;
        var segsSinceYield = 0;
        var currentCutType = '';
        var toolOn = false;
        var linePos = 0;
        i = 0;
        while (linePos < srcLen) {
          if (parseJobId && parseJobId !== state.preview.parseJobId) return null;

          var nl = srcText.indexOf('\n', linePos);
          if (nl < 0) nl = srcLen;
          var rawLn = srcText.substring(linePos, nl);
          if (nl > linePos && srcText.charCodeAt(nl - 1) === 13) rawLn = rawLn.slice(0, -1);
          linePos = nl + 1;
          i++;
          if (!rawLn) continue;
          var ln = rawLn;
          var lnC0 = ln.charCodeAt(0);
          if (lnC0 === 32 || lnC0 === 9) {
            ln = ln.trim();
            if (!ln) continue;
          }

          // Detect LightBurn-style operation comments: "; Cut @...", "; Scan @...", "; Image @..."
          if (ln.charAt(0) === ';') {
            var lc = ln.toLowerCase();
            // NOTE: "; Layer Cxx" appears in LightBurn for EVERY layer (raster
            // layers included) and must NOT force 'cut' — otherwise raster fill
            // would never be classified as engrave and never downsampled.
            if (lc.indexOf('; cut @') === 0) currentCutType = 'cut';
            else if (lc.indexOf('; scan @') === 0) currentCutType = 'engrave';
            else if (lc.indexOf('; image @') === 0) currentCutType = 'engrave';
            continue;
          }

          if (ln.indexOf('(') >= 0) ln = ln.replace(/\([^)]*\)/g, '');
          // Skip toUpperCase when the line is already uppercase (LightBurn
          // output) — avoids 1M string allocations on huge raster files.
          var upper = /[a-z]/.test(ln) ? ln.toUpperCase() : ln;
          if (upper.indexOf(';') >= 0) upper = upper.replace(/;.*$/, '');
          var upC0 = upper.charCodeAt(0);
          if (upC0 === 32 || upC0 === 9) upper = upper.trim();
          if (!upper) continue;

          // Track tool state (M3=M4=spindle on, M8=coolant/laser on, M5=spindle off, M9=coolant off).
          // Cheap indexOf guard first — raster lines carry no M code at all.
          if (upper.indexOf('M') >= 0) {
            if (/\bM[348]\b/.test(upper)) toolOn = true;
            if (/\bM[59]\b/.test(upper)) toolOn = false;
          }
          execNo++;

          var gm;
          if (upper.indexOf('G') >= 0) {
            var gx = /(?:^|\s)G(\d+(?:\.\d+)?)(?=\s|$)/g;
            while ((gm = gx.exec(upper)) !== null) {
              var gv = parseFloat(gm[1]);
              if (isNaN(gv)) continue;
              if (gv === 90) absoluteMode = true;
              else if (gv === 91) absoluteMode = false;
              else if (gv === 90.1) arcCenterAbsolute = true;
              else if (gv === 91.1) arcCenterAbsolute = false;
              else if (gv === 20) unitToMm = 25.4;
              else if (gv === 21) unitToMm = 1;
              else if (gv === 17 || gv === 18 || gv === 19) planeMode = gv;
              else if (gv === 0 || gv === 1 || gv === 2 || gv === 3) motionMode = gv;
            }
          }

          var prev = { x: x, y: y, z: z };

          var nx = (upper.indexOf('X') >= 0) ? extractAxisInMm(upper, 'X') : null;
          var ny = (upper.indexOf('Y') >= 0) ? extractAxisInMm(upper, 'Y') : null;
          var nz = (upper.indexOf('Z') >= 0) ? extractAxisInMm(upper, 'Z') : null;
          var ni = (upper.indexOf('I') >= 0) ? extractAxisInMm(upper, 'I') : null;
          var nj = (upper.indexOf('J') >= 0) ? extractAxisInMm(upper, 'J') : null;
          var nr = (upper.indexOf('R') >= 0) ? extractAxisInMm(upper, 'R') : null;

          if (nx !== null) {
            x = absoluteMode ? nx : (x + nx);
          }
          if (ny !== null) {
            y = absoluteMode ? ny : (y + ny);
          }
          if (nz !== null) {
            z = absoluteMode ? nz : (z + nz);
          }

          var isArcMotionXY = (motionMode === 2 || motionMode === 3) && planeMode === 17;
          var hasAxisMove = (nx !== null) || (ny !== null) || (nz !== null);
          var hasArcParams = (ni !== null) || (nj !== null) || (nr !== null);
          var isFullCircleCmd = isArcMotionXY && !hasAxisMove && (ni !== null || nj !== null);
          if (!hasAxisMove && !isFullCircleCmd) continue;
          var sourceLineNo = i;

          var next = { x: x, y: y, z: z };
          var segCutType = motionMode === 0 ? '' : (currentCutType || (toolOn ? 'cut' : 'engrave'));
          if (motionMode === 0 || motionMode === 1) {
            appendLinear(prev, next, motionMode === 0, false, sourceLineNo, execNo, segCutType);
          } else if (isArcMotionXY) {
            var cw = motionMode === 2;
            var center = null;
            if (ni !== null || nj !== null) {
              var iVal = (ni !== null) ? ni : 0;
              var jVal = (nj !== null) ? nj : 0;
              center = {
                cx: arcCenterAbsolute ? iVal : (prev.x + iVal),
                cy: arcCenterAbsolute ? jVal : (prev.y + jVal)
              };
            } else if (nr !== null) {
              center = chooseArcCenterFromR(prev, next, nr, cw);
            }

            if (center && isFinite(center.cx) && isFinite(center.cy)) {
              buildArcSegmentsXY(prev, next, center.cx, center.cy, cw, isFullCircleCmd, sourceLineNo, execNo, segCutType);
            } else {
              appendLinear(prev, next, false, false, sourceLineNo, execNo, segCutType);
            }
          } else {
            appendLinear(prev, next, false, false, sourceLineNo, execNo, segCutType);
          }

          if (pts.length > maxPoints) {
            truncated = true;
            break;
          }

          chunkCounter++;
          if (chunkCounter >= (_isLowResourceDevice() ? 1200 : 2500) || segsSinceYield >= 20000) {
            chunkCounter = 0;
            segsSinceYield = 0;
            if (typeof onProgress === 'function') {
              var progress = total > 0 ? Math.floor((i * 100) / total) : 100;
              var ib = isFinite(bMinX) ? { minX: bMinX, maxX: bMaxX, minY: bMinY, maxY: bMaxY, minZ: bMinZ, maxZ: bMaxZ } : null;
              if (onProgress(progress, { points: pts, segments: segs, bounds: ib }) === false) return null;
            }
            await yieldToUi();
          }
        }
        if (typeof onProgress === 'function') {
          var ib2 = isFinite(bMinX) ? { minX: bMinX, maxX: bMaxX, minY: bMinY, maxY: bMaxY, minZ: bMinZ, maxZ: bMaxZ } : null;
          onProgress(100, { points: pts, segments: segs, bounds: ib2 });
        }
        return { points: pts, segments: segs, maxExec: execNo, truncated: truncated,
          bounds: isFinite(bMinX) ? { minX: bMinX, maxX: bMaxX, minY: bMinY, maxY: bMaxY, minZ: bMinZ, maxZ: bMaxZ } : null };
      }
)GRBL"

