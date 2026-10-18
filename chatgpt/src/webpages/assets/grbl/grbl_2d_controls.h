R"GRBL(
      function resolvePreviewMode(text) {
        var sel = byId('grbl_preview_mode');
        var mode = String(sel && sel.value ? sel.value : state.preview.previewMode || 'auto').toLowerCase();
        if (mode !== 'auto' && mode !== 'frame') mode = 'auto';
        state.preview.previewMode = mode;
        return mode;
      }

      function buildLightPreview(text) {
        var src = String(text || '');
        if (!src) return null;

        var bounds = buildLightPieceContoursFromGcode(src);
        var minX = -Infinity, maxX = -Infinity, minY = -Infinity, maxY = -Infinity;
        var contours = [];
        var contourRanges = [];
        var firstCutX = NaN, firstCutY = NaN;

        if (bounds) {
          minX = Number(bounds.minX);
          maxX = Number(bounds.maxX);
          minY = Number(bounds.minY);
          maxY = Number(bounds.maxY);
          contours = bounds.contours || [];
          contourRanges = bounds.contourRanges || [];
          firstCutX = isFinite(bounds.firstCutX) ? bounds.firstCutX : NaN;
          firstCutY = isFinite(bounds.firstCutY) ? bounds.firstCutY : NaN;
        }

        var wireframePoints = [];
        // quickWireframe is a second full pass over the source and only feeds a
        // status label; skip it for large files to keep the parse fast.
        if (src.length <= GRBL_WIREFRAME_MAX_SCAN_BYTES) {
          var wf = quickWireframe(src);
          if (wf) {
            if (!isFinite(minX)) { minX = Number(wf.minX); maxX = Number(wf.maxX); }
            if (!isFinite(minY)) { minY = Number(wf.minY); maxY = Number(wf.maxY); }
            wireframePoints = wf.wireframePoints || [];
          }
        }

        if (!isFinite(minX) || !isFinite(maxX)) {
          minX = 0; maxX = 50; minY = 0; maxY = 50;
        }

        if (!isFinite(minY)) minY = 0;
        if (!isFinite(maxY)) maxY = 50;

        if (!isFinite(firstCutX)) {
          var lines = src.split(/\r?\n/);
          var cutMode = false;
          var cx = 0, cy = 0, found = false;
          var absMode = true;
          for (var li = 0; li < lines.length; li++) {
            var line = lines[li].trim().toUpperCase();
            var sem = line.indexOf(';');
            if (sem >= 0) line = line.slice(0, sem);
            line = line.replace(/\([^)]*\)/g, ' ');
            if (!line) continue;
            if (line.indexOf('G90') >= 0) absMode = true;
            if (line.indexOf('G91') >= 0) absMode = false;
            if (line.indexOf('M3') >= 0 || line.indexOf('M4') >= 0 || line.indexOf('M8') >= 0) cutMode = true;
            if (!cutMode) continue;
            var gMatch = line.match(/G(\d+)/);
            if (gMatch) {
              var gv = parseInt(gMatch[0].slice(1), 10);
              if (gv === 0) continue;
              if (gv >= 1 && gv <= 3) {
                var xv = parseFloat((line.match(/[Xx]([-+]?\d*\.?\d+)/) || [])[1]);
                var yv = parseFloat((line.match(/[Yy]([-+]?\d*\.?\d+)/) || [])[1]);
                if (!isNaN(xv) && !isNaN(yv)) {
                  firstCutX = absMode ? xv : cx + xv;
                  firstCutY = absMode ? yv : cy + yv;
                  found = true;
                  break;
                }
              }
            }
          }
          if (!found) { firstCutX = NaN; firstCutY = NaN; }
        }

        state.preview.lightPreview = {
          minX: minX, maxX: maxX,
          minY: minY, maxY: maxY,
          contours: contours,
          contourRanges: contourRanges,
          wireframePoints: wireframePoints,
          firstCutX: firstCutX, firstCutY: firstCutY
        };

        var c = byId('grbl_preview_canvas');
        if (c) {
          var rect = c.getBoundingClientRect();
          var vw = Math.max(10, Math.floor(rect.width || c.width || 10));
          var vh = Math.max(10, Math.floor(rect.height || c.height || 10));
          var spanX = Math.max(1, maxX - minX);
          var spanY = Math.max(1, maxY - minY);
          var s = Math.min((vw * 0.82) / spanX, (vh * 0.72) / spanY);
          state.preview.fitScale = Math.max(0.25, Math.min(5000, s));
          state.preview.scale = state.preview.fitScale;
        }

        return state.preview.lightPreview;
      }

      function lastLineNo(segs) {
        return (segs && segs.length) ? (segs[segs.length - 1].lineNo || 0) : 0;
      }

      // Piece-contour fallback: once the full parser is done, derive closed
      // cut loops from the parsed segments. The light scan (`buildLightPreview`)
      // runs first and covers closed straight-line cuts with S-power enabled,
      // but misses arc-interpolated outlines (circles become many small chord
      // segments that the light scan never records) and spindle-only cuts.
      // This syncs those contours in so Tab/Space selection works everywhere.
      function _syncContoursFromSegments() {
        var pv = state.preview;
        var pts = pv.points, segs = pv.segments;
        if (!pts || !segs || !segs.length) return;
        var lp = pv.lightPreview;
        // The light scan is the primary contour source and is accurate for
        // normal G1 files (one piece per S-power group). Only use the
        // segment-derived fallback when the light scan found nothing (e.g.
        // arc-only outlines), so the weaker segment grouping never clobbers
        // the real pieces.
        if (lp && lp.contours && lp.contours.length) return;
        var derived = buildPieceContoursFromSegments(pts, segs);
        if (!derived || !derived.contours || !derived.contours.length) return;
        if (!lp || !isFinite(lp.minX)) {
          var cminX = Infinity, cmaxX = -Infinity, cminY = Infinity, cmaxY = -Infinity;
          for (var ci = 0; ci < derived.contours.length; ci++) {
            var cpts = derived.contours[ci];
            for (var cj = 0; cj < cpts.length; cj++) {
              var cp = cpts[cj];
              if (cp.x < cminX) cminX = cp.x;
              if (cp.x > cmaxX) cmaxX = cp.x;
              if (cp.y < cminY) cminY = cp.y;
              if (cp.y > cmaxY) cmaxY = cp.y;
            }
          }
          if (!lp) lp = {};
          lp.minX = cminX; lp.maxX = cmaxX;
          lp.minY = cminY; lp.maxY = cmaxY;
          pv.lightPreview = lp;
        }
        var changed = (lp.contours ? lp.contours.length : 0) !== derived.contours.length;
        lp.contours = derived.contours;
        lp.contourRanges = derived.ranges;
        if (!changed) return;
        pv.contourDisabled = [];
        pv.contourUndoStack = [];
        pv.contourAutoChildren = {};
        pv.contourHighlight = (pv.contourSelectEnabled === true && lp.contours.length) ? 0 : -1;
        _updateContourSelectInfo();
      }

      function _startParse(text) {
        var src = String(text || '');
        if (!src) return;

        var jobId = (state.preview.parseJobId || 0) + 1;
        state.preview.parseJobId = jobId;
        state.preview.parseBusy = true;
        state.preview.parseProgress = 0;
        state.preview.parseTruncated = false;
        updatePreviewInfo();
        drawPreview();

        var lastSegsLen = 0;
        var lastDrawPct = -1;

        parseToolpath3dAsync(src, function(progress, intermediate) {
          if (jobId !== state.preview.parseJobId) return false;
          state.preview.parseProgress = progress;
          if (intermediate && intermediate.segments && intermediate.segments.length > lastSegsLen) {
            lastSegsLen = intermediate.segments.length;
            state.preview.points = intermediate.points || [];
            state.preview.segments = intermediate.segments || [];
            state.preview.hasPath = (intermediate.points && intermediate.points.length > 1) || (intermediate.segments && intermediate.segments.length > 0);
            state.preview.maxLine = lastLineNo(intermediate.segments);
            updateScrubControls();
            // Re-rendering the whole path on every chunk is O(N^2) for large
            // files; only redraw when progress moves ~5% (and on completion).
            if ((progress - lastDrawPct) >= 5 || progress >= 100) {
              lastDrawPct = progress;
              drawPreview();
            }
          }
          if ((progress % 10) === 0 || progress >= 100) updatePreviewInfo();
          return true;
        }, jobId).then(function(parsed) {
          if (!parsed || jobId !== state.preview.parseJobId) return;
          state.preview.points = parsed.points || [];
          state.preview.segments = parsed.segments || [];
          state.preview.parseTruncated = !!parsed.truncated;
          state.preview.hasPath = (parsed.points && parsed.points.length > 1) || (parsed.segments && parsed.segments.length > 0);
          state.preview.maxLine = lastLineNo(parsed.segments);
          if (state.preview.scrubEnabled && (state.preview.scrubLine || 0) > state.preview.maxLine) {
            state.preview.scrubLine = state.preview.maxLine;
          }
          if (state.preview.hasPath) fitPreviewToBounds();
          state.preview.parseBusy = false;
          state.preview.parseProgress = 100;
          _syncContoursFromSegments();
          updatePreviewInfo();
          updateScrubControls();
          drawPreview();
        }).catch(function() {
          if (jobId !== state.preview.parseJobId) return;
          state.preview.parseBusy = false;
          state.preview.parseProgress = 0;
          updatePreviewInfo();
          updateScrubControls();
          drawPreview();
        });
      }

      function renderPreviewFromText(text, filePath) {
        var src = String(text || '');
        state.preview.hasPath = true;
        state.preview.points = [];
        state.preview.segments = [];
        state.preview.lightPreview = null;
        state.preview.currentLine = 0;
        state.preview.maxLine = 0;
        state.preview.parseBusy = false;
        state.preview.parseProgress = 0;
        state.preview.parseTruncated = false;
        state.preview.jobTotal = 0;
        state.preview.jobAcked = 0;

        var mode = resolvePreviewMode(src);

        // Always build the light contour preview: contour selection (Tab /
        // Space / click) depends on lightPreview.contours in every mode, not
        // only the frame/wireframe lightweight view.
        state.preview.lightPreview = buildLightPreview(src);

        // 'auto' runs the full (downsampled) path parse so every piece shows —
        // not only the closed loops. 'frame' stays as the lightweight
        // bounds/frame view.
        if (mode === 'auto') {
          _startParse(src);
        }

        if (mode === 'frame' || mode === 'wireframe') {
          fitPreviewToBounds();
        }

        updatePreviewInfo();
        updateScrubControls();
        drawPreview();
      }

      function resetPreviewView() {
        state.preview.scale = 1;
        state.preview.fitScale = 1;
        state.preview.panX = 0;
        state.preview.panY = 0;

        var pts = state.preview.points;
        var lp = state.preview.lightPreview;
        var mt = state.maxTravel;

        if (pts && pts.length > 1) {
          fitPreviewToBounds();
        } else if (lp && isFinite(lp.minX) && isFinite(lp.maxX) && isFinite(lp.minY) && isFinite(lp.maxY)) {
          var c = byId('grbl_preview_canvas');
          if (c) {
            var rect = c.getBoundingClientRect();
            var vw = Math.max(10, Math.floor(rect.width || c.width || 10));
            var vh = Math.max(10, Math.floor(rect.height || c.height || 10));
            var spanX = Math.max(1, lp.maxX - lp.minX);
            var spanY = Math.max(1, lp.maxY - lp.minY);
            var fitScale = Math.max(0.25, Math.min(5000, Math.min((vw * 0.82) / spanX, (vh * 0.72) / spanY)));
            state.preview.fitScale = fitScale;
            state.preview.scale = fitScale;
          }
        } else if (mt && mt.x > 0 && mt.y > 0) {
          fitPreviewToBounds();
        }

        drawPreview();
      }

      function fitPreviewToBounds() {
        var c = byId('grbl_preview_canvas');
        if (!c) return 1;

        var rect = c.getBoundingClientRect();
        var vw = Math.max(10, Math.floor(rect.width || c.width || 10));
        var vh = Math.max(10, Math.floor(rect.height || c.height || 10));

        var pts = state.preview.points;
        var lp = state.preview.lightPreview;
        var mt = state.maxTravel;

        var minX, maxX, minY, maxY;

        if (pts && pts.length > 1) {
          minX = pts[0].x; maxX = pts[0].x; minY = pts[0].y; maxY = pts[0].y;
          for (var i = 1; i < pts.length; i++) {
            var p = pts[i];
            if (p.x < minX) minX = p.x; if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y; if (p.y > maxY) maxY = p.y;
          }
        } else if (lp && isFinite(lp.minX) && isFinite(lp.maxX) && isFinite(lp.minY) && isFinite(lp.maxY)) {
          minX = lp.minX; maxX = lp.maxX; minY = lp.minY; maxY = lp.maxY;
        } else if (mt && mt.x > 0 && mt.y > 0) {
          minX = 0; maxX = mt.x; minY = 0; maxY = mt.y;
        } else {
          minX = -50; maxX = 50; minY = -50; maxY = 50;
        }

        var spanX = Math.max(1, maxX - minX);
        var spanY = Math.max(1, maxY - minY);
        var fitScale = Math.max(0.25, Math.min(5000, Math.min((vw * 0.82) / spanX, (vh * 0.72) / spanY)));

        state.preview.fitScale = fitScale;
        state.preview.scale = fitScale;
        state.preview.panX = 0;
        state.preview.panY = 0;

        return fitScale;
      }

      function getSimSpeed() {
        var el = byId('grbl_3d_scrub_speed');
        var s = parseInt(el && el.value ? el.value : '10', 10);
        if (isNaN(s) || s < 1) s = 1;
        if (s > 40) s = 40;
        state.preview.scrubSpeed = s;
        return s;
      }

      function updateScrubControls() {
        var maxLine = parseInt(state.preview.maxLine || 0, 10);
        if (isNaN(maxLine) || maxLine < 0) maxLine = 0;
        var range = byId('grbl_3d_line_scrub');
        var label = byId('grbl_3d_scrub_info');
        var playBtn = byId('btn_3d_scrub_play');
        var speedEl = byId('grbl_3d_scrub_speed');
        var show = state.preview.scrubEnabled ? state.preview.scrubLine : state.preview.currentLine;
        if (isNaN(show) || show < 0) show = 0;
        if (show > maxLine) show = maxLine;
        if (range) {
          range.max = String(maxLine);
          range.disabled = (maxLine <= 0);
          range.value = String(show);
        }
        if (label) label.textContent = 'L' + String(show) + '/' + String(maxLine);
        if (playBtn) {
          playBtn.disabled = (maxLine <= 1);
          playBtn.textContent = state.preview.scrubPlaying ? wtSafe('grbl3dStopBtn', 'Stop') : wtSafe('grbl3dReplayBtn', 'Play');
        }
        if (speedEl) speedEl.value = String(state.preview.scrubSpeed || 10);
      }

      function _simTick() {
        if (!state.preview.scrubPlaying) return;
        var maxLine = parseInt(state.preview.maxLine || 0, 10);
        if (isNaN(maxLine) || maxLine <= 0) {
          state.preview.scrubPlaying = false;
          updateScrubControls();
          return;
        }
        var step = getSimSpeed();
        var cur = parseInt(state.preview.scrubLine || 0, 10);
        if (isNaN(cur) || cur < 0) cur = 0;
        var next = cur + step;
        if (next >= maxLine) {
          next = maxLine;
          state.preview.scrubPlaying = false;
        }
        state.preview.scrubLine = next;
        updatePreviewInfo();
        updateScrubControls();
        drawPreview();
        if (state.preview.scrubPlaying) window.setTimeout(_simTick, 33);
      }

      function toggleSimPlay() {
        if (state.preview.scrubPlaying) {
          state.preview.scrubPlaying = false;
          updateScrubControls();
          return;
        }
        var maxLine = parseInt(state.preview.maxLine || 0, 10);
        if (isNaN(maxLine) || maxLine <= 1) return;
        state.preview.scrubEnabled = true;
        var cur = parseInt(state.preview.scrubLine || 0, 10);
        if (isNaN(cur) || cur <= 0 || cur >= maxLine) cur = 1;
        state.preview.scrubLine = cur;
        state.preview.scrubPlaying = true;
        updateScrubControls();
        _simTick();
      }

      function resetPreviewSim() {
        state.preview.scrubPlaying = false;
        state.preview.scrubEnabled = false;
        state.preview.scrubLine = state.preview.currentLine || 0;
        resetPreviewView();
        updatePreviewInfo('');
        updateScrubControls();
        drawPreview();
      }

      function bindPreviewInteractions() {
        var canvas = byId('grbl_preview_canvas');
        if (!canvas) return;

        canvas.addEventListener('wheel', function(ev) {
          ev.preventDefault();
          var step = (ev.deltaY < 0) ? 1.1 : 0.9;
          state.preview.scale = Math.max(0.25, Math.min(5000, (state.preview.scale || 1) * step));
          drawPreview();
        }, { passive: false });

        canvas.addEventListener('dblclick', function(ev) {
          ev.preventDefault();
          resetPreviewView();
        });

        canvas.addEventListener('mousedown', function(ev) {
          state.preview.dragging = true;
          state.preview.lastX = ev.clientX;
          state.preview.lastY = ev.clientY;
          state.preview._clickMoved = false;
        });
        window.addEventListener('mouseup', function(ev) {
          if (state.preview.dragging && !state.preview._clickMoved &&
              state.preview.contourSelectEnabled === true) {
            _handleCanvasContourClick(canvas, ev.clientX, ev.clientY);
          }
          state.preview.dragging = false;
        });
        window.addEventListener('mousemove', function(ev) {
          if (!state.preview.dragging) return;
          var dx = ev.clientX - state.preview.lastX;
          var dy = ev.clientY - state.preview.lastY;
          if (Math.abs(dx) > 2 || Math.abs(dy) > 2) state.preview._clickMoved = true;
          state.preview.lastX = ev.clientX;
          state.preview.lastY = ev.clientY;
          state.preview.panX = (state.preview.panX || 0) + dx / Math.max(0.001, state.preview.scale || 1);
          state.preview.panY = (state.preview.panY || 0) + dy / Math.max(0.001, state.preview.scale || 1);
          drawPreview();
        });

        // Crosshair: track the pointer over the canvas while contour select is
        // active so the user sees exactly where a click will land.
        canvas.addEventListener('mousemove', function(ev) {
          if (state.preview.contourSelectEnabled !== true) return;
          var cr = canvas.getBoundingClientRect();
          var nx = ev.clientX - cr.left;
          var ny = ev.clientY - cr.top;
          var prev = state.preview._mousePx;
          if (!prev || Math.abs(prev.x - nx) > 1 || Math.abs(prev.y - ny) > 1) {
            state.preview._mousePx = { x: nx, y: ny };
            drawPreview();
          }
        });
        canvas.addEventListener('mouseleave', function(ev) {
          if (!state.preview._mousePx) return;
          state.preview._mousePx = null;
          drawPreview();
        });

        var pinchDist0 = 0;
        canvas.addEventListener('touchstart', function(ev) {
          if (ev.touches.length === 2) {
            var tdx = ev.touches[0].clientX - ev.touches[1].clientX;
            var tdy = ev.touches[0].clientY - ev.touches[1].clientY;
            pinchDist0 = Math.sqrt(tdx * tdx + tdy * tdy);
            state.preview.dragging = false;
          } else if (ev.touches.length === 1) {
            pinchDist0 = 0;
            state.preview.dragging = true;
            state.preview.lastX = ev.touches[0].clientX;
            state.preview.lastY = ev.touches[0].clientY;
            state.preview._touchMoved = false;
            state.preview._touchStartX = ev.touches[0].clientX;
            state.preview._touchStartY = ev.touches[0].clientY;
          }
        }, { passive: true });
        canvas.addEventListener('touchmove', function(ev) {
          if (ev.touches.length === 2 && pinchDist0 > 0) {
            ev.preventDefault();
            var tdx2 = ev.touches[0].clientX - ev.touches[1].clientX;
            var tdy2 = ev.touches[0].clientY - ev.touches[1].clientY;
            var dist2 = Math.sqrt(tdx2 * tdx2 + tdy2 * tdy2);
            if (dist2 > 0) {
              state.preview.scale = Math.max(0.25, Math.min(5000, (state.preview.scale || 1) * (dist2 / pinchDist0)));
              pinchDist0 = dist2;
              drawPreview();
            }
          } else if (ev.touches.length === 1 && state.preview.dragging) {
            ev.preventDefault();
            var tdx3 = ev.touches[0].clientX - state.preview.lastX;
            var tdy3 = ev.touches[0].clientY - state.preview.lastY;
            if (Math.abs(ev.touches[0].clientX - (state.preview._touchStartX || 0)) > 4 ||
                Math.abs(ev.touches[0].clientY - (state.preview._touchStartY || 0)) > 4) {
              state.preview._touchMoved = true;
            }
            state.preview.lastX = ev.touches[0].clientX;
            state.preview.lastY = ev.touches[0].clientY;
            state.preview.panX = (state.preview.panX || 0) + tdx3 / Math.max(0.001, state.preview.scale || 1);
            state.preview.panY = (state.preview.panY || 0) + tdy3 / Math.max(0.001, state.preview.scale || 1);
            drawPreview();
          }
        }, { passive: false });
        canvas.addEventListener('touchend', function(ev) {
          if (state.preview.contourSelectEnabled === true && !state.preview._touchMoved && ev.changedTouches && ev.changedTouches.length) {
            var tc = ev.changedTouches[0];
            _handleCanvasContourClick(canvas, tc.clientX, tc.clientY);
          }
          state.preview.dragging = false;
          pinchDist0 = 0;
        });

        canvas.addEventListener('keydown', function(ev) {
          var key = ev.key;
          var handled = false;
          if (key === '+' || key === '=') {
            state.preview.scale = Math.min(5000, (state.preview.scale || 1) * 1.1);
            handled = true;
          } else if (key === '-') {
            state.preview.scale = Math.max(0.25, (state.preview.scale || 1) / 1.1);
            handled = true;
          } else if (key === 'ArrowLeft') {
            state.preview.panX = (state.preview.panX || 0) - 10 / Math.max(0.001, state.preview.scale || 1);
            handled = true;
          } else if (key === 'ArrowRight') {
            state.preview.panX = (state.preview.panX || 0) + 10 / Math.max(0.001, state.preview.scale || 1);
            handled = true;
          } else if (key === 'ArrowUp') {
            state.preview.panY = (state.preview.panY || 0) - 10 / Math.max(0.001, state.preview.scale || 1);
            handled = true;
          } else if (key === 'ArrowDown') {
            state.preview.panY = (state.preview.panY || 0) + 10 / Math.max(0.001, state.preview.scale || 1);
            handled = true;
          } else if (key === 'r' || key === 'R') {
            resetPreviewView();
            handled = true;
          } else if (state.preview.contourSelectEnabled === true) {
            if (key === 'Tab') {
              _navContourHighlight(ev.shiftKey ? -1 : 1);
              handled = true;
            } else if (key === ' ') {
              _spaceToggleHighlighted();
              handled = true;
            }
          }
          if (handled) {
            ev.preventDefault();
            drawPreview();
          }
        });
      }

      function updatePreviewInfo() {
        var el = byId('grbl_preview_info');
        if (!el) return;

        var pv = state.preview;

        if (pv.parseBusy) {
          var pp = parseInt(pv.parseProgress || 0, 10);
          if (!isFinite(pp) || pp < 0) pp = 0;
          if (pp > 100) pp = 100;
          el.textContent = wtSafe('grbl2dParsingPrefix', 'Parsing... ') + String(pp) + '%';
          return;
        }

        if (pv.parseTruncated) {
          el.textContent = wtSafe('grbl2dCapped', 'Preview capped: file is large') + ' | ' + wtSafe('grbl2dSegCount', 'seg') + ':' + String(pv.segments.length) + ' pts:' + String(pv.points.length);
          return;
        }

        var lp = pv.lightPreview;
        var hasLight = lp && isFinite(lp.minX) && isFinite(lp.maxX) && isFinite(lp.minY) && isFinite(lp.maxY);
        var hasFull = pv.hasPath && pv.segments && pv.segments.length > 0;

        if (hasFull) {
          var arcCount = 0;
          for (var si = 0; si < pv.segments.length; si++) {
            if (pv.segments[si].arc) arcCount++;
          }
          var wx = Math.max(0, lp ? lp.maxX - lp.minX : 0);
          var wy = Math.max(0, lp ? lp.maxY - lp.minY : 0);
          var shownLine = pv.scrubEnabled ? (pv.scrubLine || 0) : (pv.currentLine || 0);
          el.textContent = String(pv.points.length) + ' pts | ' + String(pv.segments.length) + ' seg | ' + String(arcCount) + ' arc' + (wx > 0.01 ? ' | ' + formatMm(wx) + ' x ' + formatMm(wy) + ' mm' : '') + ' | L' + String(shownLine) + '/' + String(pv.maxLine || 0);
          return;
        }

        if (hasLight) {
          var w = Math.max(0, lp.maxX - lp.minX);
          var hh = Math.max(0, lp.maxY - lp.minY);
          var pieceCount = lp.contours ? lp.contours.length : 0;
          if (lp.wireframePoints && lp.wireframePoints.length) {
            el.textContent = wtSafe('grbl2dWireframe', 'Wireframe: fast scan') + ' | ' + String(lp.wireframePoints.length) + ' pts | ' + formatMm(w) + ' x ' + formatMm(hh) + ' mm';
          } else if (pieceCount > 0) {
            el.textContent = String(pieceCount) + ' pieces | ' + formatMm(w) + ' x ' + formatMm(hh) + ' mm';
          } else {
            el.textContent = wtSafe('grbl2dFrame', 'Frame preview') + ' | ' + formatMm(w) + ' x ' + formatMm(hh) + ' mm';
          }
          return;
        }

        el.textContent = wtSafe('grbl2dEmpty', 'No path loaded');
      }

      function animateProgressLine() {
        if (!state.preview._animTarget) state.preview._animTarget = 0;
        if (!isFinite(state.preview._animCurrent)) state.preview._animCurrent = 0;

        var target = parseInt(state.preview.scrubEnabled ? (state.preview.scrubLine || 0) : (state.preview.currentLine || 0), 10);
        if (!isFinite(target) || target < 0) target = 0;

        if (target !== state.preview._animTarget) {
          state.preview._animTarget = target;
        }

        if (state.preview._animRafId) {
          window.cancelAnimationFrame(state.preview._animRafId);
          state.preview._animRafId = 0;
        }

        function stepAnim() {
          state.preview._animRafId = 0;

          var t = state.preview._animTarget;
          var c = state.preview._animCurrent;
          var diff = t - c;

          if (Math.abs(diff) < 0.5) {
            state.preview._animCurrent = t;
            updatePreviewInfo();
            drawPreview();
            return;
          }

          var speed = 0.15;
          state.preview._animCurrent = c + diff * speed;

          if (state.preview.jobTotal > 0) {
            updatePreviewInfo();
            drawPreview();
            state.preview._animRafId = window.requestAnimationFrame(stepAnim);
          }
        }

        state.preview._animRafId = window.requestAnimationFrame(stepAnim);
      }

      function clearPreview() {
        state.preview.parseJobId = (state.preview.parseJobId || 0) + 1;
        state.preview.parseBusy = false;
        state.preview.parseProgress = 0;
        state.preview.parseTruncated = false;
        state.preview.lightPreview = null;
        state.preview.points = [];
        state.preview.segments = [];
        state.preview.hasPath = false;
        state.preview.currentLine = 0;
        state.preview.maxLine = 0;
        state.preview.jobTotal = 0;
        state.preview.jobAcked = 0;
        state.preview.startInfo = null;
        state.preview.jobStartWPos = null;
        state.preview.scale = 1;
        state.preview.fitScale = 1;
        state.preview.panX = 0;
        state.preview.panY = 0;
        updatePreviewInfo();
        drawPreview();
      }

      function _lightPreviewScalePx(canvas, lp) {
        var rect = canvas.getBoundingClientRect();
        var w = Math.max(20, Math.floor(rect.width || canvas.width || 300));
        var h = Math.max(20, Math.floor(rect.height || canvas.height || 300));
        return _computeTopTransformPx(w, h, state.preview).sc;
      }

      function _screenToMachine(canvas, sx, sy) {
        var pv = state.preview;
        var lp = pv.lightPreview;
        if (!lp || !isFinite(lp.minX) || !isFinite(lp.maxX) || !isFinite(lp.minY) || !isFinite(lp.maxY)) return null;
        var rect = canvas.getBoundingClientRect();
        var w = Math.max(20, Math.floor(rect.width || 300));
        var h = Math.max(20, Math.floor(rect.height || 300));
        var tf = _computeTopTransformPx(w, h, pv);
        var csx = sx - rect.left;
        var csy = sy - rect.top;
        var mx = tf.tb.minX + (csx - tf.ox) / tf.sc;
        var my = -((csy - tf.oy) / tf.sc + tf.tb.minY);
        return { x: mx, y: my };
      }

      function _pointInPolygon(px, py, poly) {
        var inside = false;
        for (var i = 0, j = poly.length - 1; i < poly.length; j = i++) {
          var xi = poly[i].x, yi = poly[i].y;
          var xj = poly[j].x, yj = poly[j].y;
          var intersect = ((yi > py) !== (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi);
          if (intersect) inside = !inside;
        }
        return inside;
      }

      function _distToSegment(px, py, ax, ay, bx, by) {
        var dx = bx - ax, dy = by - ay;
        var len2 = dx * dx + dy * dy;
        if (len2 < 1e-12) {
          var ddx = px - ax, ddy = py - ay;
          return Math.sqrt(ddx * ddx + ddy * ddy);
        }
        var t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = Math.max(0, Math.min(1, t));
        var cx = ax + t * dx, cy = ay + t * dy;
        var ex = px - cx, ey = py - cy;
        return Math.sqrt(ex * ex + ey * ey);
      }

      function _minDistToContour(mx, my, contour) {
        if (!contour || contour.length < 2) return Infinity;
        var best = Infinity;
        for (var i = 0, j = contour.length - 1; i < contour.length; j = i++) {
          var d = _distToSegment(mx, my, contour[i].x, contour[i].y, contour[j].x, contour[j].y);
          if (d < best) best = d;
        }
        return best;
      }

      function _handleCanvasContourClick(canvas, sx, sy) {
        var lp = state.preview.lightPreview;
        if (!lp || !isFinite(lp.minX)) return;
        var sc = _lightPreviewScalePx(canvas, lp);
        var toleranceMm = 5.0 / Math.max(0.01, sc);
        var mp = _screenToMachine(canvas, sx, sy);
        if (!mp) return;
        var conts = lp.contours || [];
        var bestIdx = -1, bestDist = Infinity;
        for (var ci = conts.length - 1; ci >= 0; ci--) {
          var c = conts[ci];
          if (!c || c.length < 2) continue;
          var d = _minDistToContour(mp.x, mp.y, c);
          if (d < bestDist) { bestDist = d; bestIdx = ci; }
        }
        if (bestDist <= toleranceMm) {
          // Click only selects (highlights) the nearest piece. Space marks it.
          state.preview.contourHighlight = bestIdx;
          _updateContourSelectInfo();
          drawPreview();
        }
      }

      function _navContourHighlight(dir) {
        var lp = state.preview.lightPreview;
        var conts = lp && lp.contours ? lp.contours : [];
        if (!conts.length) return;
        var hl = (typeof state.preview.contourHighlight === 'number') ? state.preview.contourHighlight : -1;
        var start = hl + dir;
        if (start >= conts.length) start = 0;
        if (start < 0) start = conts.length - 1;
        state.preview.contourHighlight = start;
        _updateContourSelectInfo();
        drawPreview();
      }

      // Shared toggle core: sets contour ci (plus auto-contained children)
      // to toOff. Returns nothing.
      function _setContourOff(ci, toOff) {
        var disabled = state.preview.contourDisabled;
        if (!disabled) { disabled = []; state.preview.contourDisabled = disabled; }
        disabled[ci] = toOff;
        var autoMap = state.preview.contourAutoChildren;
        if (!autoMap) { autoMap = {}; state.preview.contourAutoChildren = autoMap; }
        if (toOff) {
          var lp = state.preview.lightPreview;
          var conts = lp && lp.contours ? lp.contours : [];
          var kids = _contoursInside(ci, conts);
          var touched = [];
          for (var ki = 0; ki < kids.length; ki++) {
            if (!disabled[kids[ki]]) { disabled[kids[ki]] = true; touched.push(kids[ki]); }
          }
          if (touched.length) autoMap[ci] = touched;
          else if (autoMap[ci]) delete autoMap[ci];
        } else {
          var prev = autoMap[ci] || [];
          for (var pj = 0; pj < prev.length; pj++) disabled[prev[pj]] = false;
          if (autoMap[ci]) delete autoMap[ci];
        }
      }

      function _spaceToggleHighlighted() {
        var lp = state.preview.lightPreview;
        var conts = lp && lp.contours ? lp.contours : [];
        var hl = (typeof state.preview.contourHighlight === 'number') ? state.preview.contourHighlight : -1;
        if (hl < 0 || hl >= conts.length) return;
        var disabled = state.preview.contourDisabled || [];
        _setContourOff(hl, !disabled[hl]);
        var stack = state.preview.contourUndoStack;
        if (!stack) { stack = []; state.preview.contourUndoStack = stack; }
        stack.push(hl);
        if (stack.length > 20) stack.shift();
        _updateContourSelectInfo();
        drawPreview();
      }

      // Indices of contours geometrically contained in contour pi (excluding
      // pi itself). A contour is "inside" when its first point is strictly
      // inside pi and its bounding box is smaller (guards against identical
      // or overlapping loops toggling each other).
      function _contoursInside(pi, conts) {
        var out = [];
        var outer = conts[pi];
        if (!outer || outer.length < 3) return out;
        var ominX = Infinity, omaxX = -Infinity, ominY = Infinity, omaxY = -Infinity;
        for (var oi = 0; oi < outer.length; oi++) {
          var op = outer[oi];
          if (op.x < ominX) ominX = op.x; if (op.x > omaxX) omaxX = op.x;
          if (op.y < ominY) ominY = op.y; if (op.y > omaxY) omaxY = op.y;
        }
        var oArea = Math.max(0, (omaxX - ominX) * (omaxY - ominY));
        for (var ci = 0; ci < conts.length; ci++) {
          if (ci === pi) continue;
          var c = conts[ci];
          if (!c || c.length < 3) continue;
          var p0 = c[0];
          if (p0.x < ominX || p0.x > omaxX || p0.y < ominY || p0.y > omaxY) continue;
          var cminX = Infinity, cmaxX = -Infinity, cminY = Infinity, cmaxY = -Infinity;
          for (var cj = 0; cj < c.length; cj++) {
            var cp = c[cj];
            if (cp.x < cminX) cminX = cp.x; if (cp.x > cmaxX) cmaxX = cp.x;
            if (cp.y < cminY) cminY = cp.y; if (cp.y > cmaxY) cmaxY = cp.y;
          }
          var cArea = Math.max(0, (cmaxX - cminX) * (cmaxY - cminY));
          if (!(cArea < oArea)) continue;
          if (_pointInPolygon(p0.x, p0.y, outer)) out.push(ci);
        }
        return out;
      }

      function _toggleContourSelect() {
        var en = !(state.preview.contourSelectEnabled === true);
        state.preview.contourSelectEnabled = en;
        if (!en) {
          state.preview.contourDisabled = [];
          state.preview.contourUndoStack = [];
          state.preview.contourAutoChildren = {};
          state.preview.contourHighlight = -1;
          state.preview._mousePx = null;
        } else {
          var lp = state.preview.lightPreview;
          var conts = lp && lp.contours ? lp.contours : [];
          // Start with the first piece highlighted so the user immediately
          // sees a selected drawing; Tab/Space step through the rest.
          state.preview.contourHighlight = conts.length ? 0 : -1;
          var cv = byId('grbl_preview_canvas');
          if (cv) cv.focus();
        }
        _updateContourSelectInfo();
        var btn = byId('btn_contour_select');
        if (btn) {
          btn.textContent = en ? (wtSafe('grblContourSelectOn', 'Select:ON') || 'Select:ON')
                              : (wtSafe('grblContourSelectBtn', 'Select') || 'Select');
        }
        drawPreview();
      }

      function _undoContourToggle() {
        var stack = state.preview.contourUndoStack;
        if (!stack || !stack.length) return;
        var ci = stack.pop();
        var disabled = state.preview.contourDisabled || [];
        _setContourOff(ci, !disabled[ci]);
        _updateContourSelectInfo();
        drawPreview();
      }

      function _updateContourSelectInfo() {
        var el = byId('contour_select_info');
        if (!el) return;
        var lp = state.preview.lightPreview;
        var conts = lp && lp.contours ? lp.contours : [];
        var disabled = state.preview.contourDisabled || [];
        var count = 0;
        for (var k = 0; k < disabled.length; k++) { if (disabled[k]) count++; }
        var hl = (typeof state.preview.contourHighlight === 'number') ? state.preview.contourHighlight : -1;
        var parts = [];
        if (state.preview.contourSelectEnabled === true) {
          parts.push(wtSafe('grblContourKeysHint', 'Tab=next Space=toggle') || 'Tab=next Space=toggle');
        }
        if (conts.length && hl >= 0 && hl < conts.length) {
          parts.push((wtSafe('grblContourPiece', 'pc') || 'pc') + ' ' + String(hl + 1) + '/' + String(conts.length));
        }
        if (state.preview.contourSelectEnabled === true && conts.length) {
          parts.push('ON ' + String(conts.length - count) + ' / OFF ' + String(count));
        } else if (count > 0) {
          parts.push((wtSafe('grblContourDisabledCount', 'disabled') || 'disabled') + ': ' + String(count));
        }
        el.textContent = parts.join('  ');
      }
)GRBL"
