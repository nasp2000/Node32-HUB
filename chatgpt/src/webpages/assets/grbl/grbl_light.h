R"GRBL(
      // H3: never lose the derived contours when a light-preview pass replaces
      // the lightPreview object. The full parser's segment fallback
      // (_syncContoursFromSegments) is the only source that closes arc-only
      // outlines; a later light scan/bounds/wireframe rebuild must keep those
      // pieces selectable instead of silently wiping them.
      function _keepDerivedContours(nextLp, prev) {
        if (!prev || !prev.contours || !prev.contours.length) return;
        if (!nextLp.contours || !nextLp.contours.length) {
          nextLp.contours = prev.contours;
          nextLp.contourRanges = prev.contourRanges || [];
        }
      }

      function applyLightPreviewFromText(text, sourcePath) {
        var built = buildLightPieceContoursFromGcode(text);
        if (!built) return false;
        state.preview.parseJobId = (state.preview.parseJobId || 0) + 1;
        state.preview.parseBusy = false;
        state.preview.parseProgress = 0;
        state.preview.parseTruncated = false;
        state.preview.points = [];
        state.preview.segments = [];
        state.preview.segmentBounds = null;
        state.preview.hasPath = false;
        state.preview.currentLine = 0;
        state.preview.maxLine = 0;
        state.preview.scrubLine = 0;
        state.preview.scrubPlaying = false;
        state.preview.startInfo = null;
        var prevLp = state.preview.lightPreview;
        state.preview.lightPreview = {
          minX: Number(built.minX),
          maxX: Number(built.maxX),
          minY: Number(built.minY),
          maxY: Number(built.maxY),
          contours: built.contours || [],
          contourRanges: built.contourRanges || [],
          sourcePath: String(sourcePath || '')
        };
        _keepDerivedContours(state.preview.lightPreview, prevLp);
        // Reset transforms for 2D outline rendering.
        state.preview.panX = 0;
        state.preview.panY = 0;
        state.preview.panZ = 0;
        // Set a baseline fitScale so zoom (state.preview.scale / fitScale) works
        // in the 2D outline render path.
        (function() {
          var c = byId('grbl_preview_canvas');
          if (!c) return;
          var rect = c.getBoundingClientRect();
          var vw = Math.max(10, Math.floor(rect.width || c.width || 10));
          var vh = Math.max(10, Math.floor(rect.height || c.height || 10));
          var spanX = Math.max(1, built.maxX - built.minX);
          var spanY = Math.max(1, built.maxY - built.minY);
          var s = Math.min((vw * 0.82) / spanX, (vh * 0.72) / spanY);
          state.preview.fitScale = Math.max(0.25, Math.min(5000, s));
          state.preview.scale = state.preview.fitScale;
        })();
        state.preview.contourSelectEnabled = false;
        state.preview.contourDisabled = [];
        state.preview.contourUndoStack = [];
        state.preview.contourAutoChildren = {};
        state.preview.contourHighlight = -1;
        updatePreviewInfo('');
        drawPreview();
        ;
        return true;
      }

      function applyPreviewMode() {
        var mode = state.preview.previewMode;
        var text = state.workspaceText || '';
        var path = String(state.selectedSdPath || '');

        if (mode === 'frame') {
          if (path) {
            fetchFrameBoundsFromSelectedSd(path).then(function(b) {
              applyLightPreviewFromBounds(b, path);
            }).catch(function() {
              applyLightPreviewFromBounds(null, path);
            });
            return;
          }
          if (applyLightPreviewFromText(text, path)) {
            updatePreviewInfo('');
            drawPreview();
            return;
          }
          state.preview.lightPreview = null;
          updatePreviewInfo('');
          drawPreview();
          return;
        }

        // 'auto' re-parses the full (downsampled) G-code so every piece shows —
        // not only the closed loops.
        if (text) {
          renderPreviewFromText(text, path);
          return;
        }
        if (path) {
          fetchSdTextForLightPreview(path).then(function(sdText) {
            if (sdText) {
              renderPreviewFromText(sdText, path);
              return;
            }
            fetchFrameBoundsFromSelectedSd(path).then(function(b) {
              applyLightPreviewFromBounds(b, path);
            }).catch(function() {
              applyLightPreviewFromBounds(null, path);
            });
          }).catch(function() {
            fetchFrameBoundsFromSelectedSd(path).then(function(b) {
              applyLightPreviewFromBounds(b, path);
            }).catch(function() {
              applyLightPreviewFromBounds(null, path);
            });
          });
        }
      }

      function applyLightPreviewFromBounds(bounds, sourcePath) {
        if (!bounds || !isFinite(bounds.minX) || !isFinite(bounds.maxX) || !isFinite(bounds.minY) || !isFinite(bounds.maxY)) {
          state.preview.lightPreview = null;
          updatePreviewInfo('');
          drawPreview();
          ;
          return;
        }
        state.preview.parseJobId = (state.preview.parseJobId || 0) + 1;
        state.preview.parseBusy = false;
        state.preview.parseProgress = 0;
        state.preview.parseTruncated = false;
        state.preview.points = [];
        state.preview.segments = [];
        state.preview.segmentBounds = null;
        state.preview.hasPath = false;
        state.preview.currentLine = 0;
        state.preview.maxLine = 0;
        state.preview.scrubLine = 0;
        state.preview.scrubPlaying = false;
        state.preview.startInfo = null;
        var prevLp = state.preview.lightPreview;
        state.preview.lightPreview = {
          minX: Number(bounds.minX),
          maxX: Number(bounds.maxX),
          minY: Number(bounds.minY),
          maxY: Number(bounds.maxY),
          contours: [],
          sourcePath: String(sourcePath || '')
        };
        _keepDerivedContours(state.preview.lightPreview, prevLp);
        updatePreviewInfo('');
        drawPreview();
        ;
      }

      function applyWireframePreview(wf, sourcePath) {
        state.preview.parseJobId = (state.preview.parseJobId || 0) + 1;
        state.preview.parseBusy = false;
        state.preview.parseProgress = 0;
        state.preview.parseTruncated = false;
        state.preview.points = [];
        state.preview.segments = [];
        state.preview.segmentBounds = null;
        state.preview.hasPath = false;
        state.preview.currentLine = 0;
        state.preview.maxLine = 0;
        state.preview.scrubLine = 0;
        state.preview.scrubPlaying = false;
        state.preview.startInfo = null;
        var prevLp = state.preview.lightPreview;
        state.preview.lightPreview = {
          minX: Number(wf.minX),
          maxX: Number(wf.maxX),
          minY: Number(wf.minY),
          maxY: Number(wf.maxY),
          minZ: Number(wf.minZ),
          maxZ: Number(wf.maxZ),
          wireframePoints: wf.wireframePoints || [],
          contours: [],
          sourcePath: String(sourcePath || '')
        };
        _keepDerivedContours(state.preview.lightPreview, prevLp);
        updatePreviewInfo('');
        drawPreview();
        ;
      }

      // Cache for firmware-fetched frame bounds (so repeated frame runs
      // for the same file don't recalculate on the ESP32 each time).
      var _cachedFrameBounds = null;
      var _cachedFramePath  = '';
      function _clearFrameCacheIfPathChanged(path) {
        if (path !== _cachedFramePath) {
          _cachedFrameBounds = null;
          _cachedFramePath   = '';
        }
      }
      function _doRunFrame() {
        var b = getPreviewFrameBounds();
        var selectedPath = String(state.selectedSdPath || '').trim();

        // Try cache first (firmware fetch is expensive).
        if (!b && selectedPath && state.selectedSdType !== 'dir') {
          _clearFrameCacheIfPathChanged(selectedPath);
          if (_cachedFrameBounds) {
            b = _cachedFrameBounds;
          } else {
            try {
              updateSdUploadUi(true, 2, wtSafe('grblSdLoadWorkspaceStart', 'Calculating frame on firmware...'));
              b = null;
              fetchFrameBoundsFromSelectedSd(selectedPath).then(function(result) {
                _cachedFrameBounds = result;
                _cachedFramePath   = selectedPath;
                updateSdUploadUi(false, 0, '');
                if (result) {
                  _doRunFrameWithBounds(result);
                } else {
                  showAlert('&#9888;&#65039; ' + wtSafe('grblFrameNoPath', 'No local path loaded to calculate frame.'));
                }
              });
              return; // async, will call _doRunFrameWithBounds when done
            } catch (e) {
              updateSdUploadUi(false, 0, '');
            }
          }
        }

        if (!b) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblFrameNoPath', 'No local path loaded to calculate frame.'));
          return;
        }
        _doRunFrameWithBounds(b);
      }
      function _doRunFrameWithBounds(b) {
        var powerRaw = byId('grbl_frame_power') ? byId('grbl_frame_power').value : '5';
        var feedRaw = byId('grbl_frame_feed') ? byId('grbl_frame_feed').value : '1200';
        var powerPct = parseInt(powerRaw, 10);
        var feed = parseInt(feedRaw, 10);
        if (isNaN(powerPct)) powerPct = 5;
        if (isNaN(feed)) feed = 1200;
        if (powerPct < 1) powerPct = 1;
        if (powerPct > 100) powerPct = 100;
        if (feed < 1) feed = 1;
        if (feed > 50000) feed = 50000;
        if (byId('grbl_frame_power')) byId('grbl_frame_power').value = String(powerPct);
        if (byId('grbl_frame_feed')) byId('grbl_frame_feed').value = String(feed);

        var maxS = 1000;
        if (state.lastStatus) {
          var candidate = parseInt(state.lastStatus.spindle_max_rpm || state.lastStatus.spindle_max || state.lastStatus.max_s || 0, 10);
          if (!isNaN(candidate) && candidate > 0) maxS = candidate;
        }
        var sVal = Math.max(1, Math.round((maxS * powerPct) / 100));

        var minX = b.minX.toFixed(3);
        var maxX = b.maxX.toFixed(3);
        var minY = b.minY.toFixed(3);
        var maxY = b.maxY.toFixed(3);
        var centerX = ((b.minX + b.maxX) / 2).toFixed(3);
        var centerY = ((b.minY + b.maxY) / 2).toFixed(3);

        var feedStr = String(feed);

        function waitMachineIdle(maxMs, cb) {
          var t0 = Date.now();
          function poll() {
            if (state.lastStatus && String(state.lastStatus.machine_state || '').toUpperCase() === 'IDLE') {
              cb(true);
              return;
            }
            if (Date.now() - t0 > maxMs) { cb(false); return; }
            refreshStatus(true).finally(function() {
              setTimeout(poll, 500);
            });
          }
          poll();
        }

        waitMachineIdle(15000, function(ok) {
          if (!ok) {
            showAlert('&#9888;&#65039; ' + wtSafe('grblFrameNotIdle', 'Machine is not idle. Frame aborted.'));
            return;
          }
          postBatch([
            'M5',
            'G90',
            'G21',
            'G1 X' + minX + ' Y' + minY + ' F' + feedStr,
            'M3 S' + String(sVal),
            'G1 X' + maxX + ' Y' + minY + ' F' + feedStr,
            'G1 X' + maxX + ' Y' + maxY + ' F' + feedStr,
            'G1 X' + minX + ' Y' + maxY + ' F' + feedStr,
            'G1 X' + minX + ' Y' + minY + ' F' + feedStr,
            'M5'
          ], 'FRAME').finally(function() {
            waitMachineIdle(600000, function(ok2) {
              if (!ok2) {
                showAlert('&#9888;&#65039; ' + wtSafe('grblFrameReturnTimeout', 'Frame finished but the machine did not return to idle. Return to center skipped.'));
                return;
              }
              _jogWithToolOff('G90 G21 G1 X' + centerX + ' Y' + centerY + ' F' + feedStr, 'FRAME CENTER');
            });
          });
        });
      }

      function runFrameFromPreview() {
        runWithCriticalConfirm('grblConfirmFrameRun', 'Run frame on the work area?', function() {
          requireAxesAtZero(function() {
            _doRunFrame();
          });
        });
      }

      function defaultMacros() {
        return [
          { name: '$$ View config', cmd: '$$' },
          { name: '$X Unlock', cmd: '$X' },
          { name: '$H Home', cmd: '$H' },
          { name: 'Zero WCS', cmd: 'G92 X0 Y0' + (_hasZAxis() ? ' Z0' : '') }
        ];
      }

      function getMacroEntry(index) {
        return {
          nameInput: byId('macro_name_' + String(index)),
          cmdInput: byId('macro_cmd_' + String(index))
        };
      }

      function runMacro(index) {
        var entry = getMacroEntry(index);
        var cmd = (entry.cmdInput ? entry.cmdInput.value || '' : '').trim();
        if (!cmd) {
          if (typeof customAlert === 'function') customAlert(wtSafe('grblMacroEmpty', 'Select a command first.'));
          return;
        }
        var parts = cmd.split(';');
        var commands = [];
        var i;
        for (i = 0; i < parts.length; i++) {
          var part = parts[i].trim();
          if (!part) continue;
          var sub = part.split('\n');
          var j;
          for (j = 0; j < sub.length; j++) {
            var line = sub[j].trim();
            if (!line) continue;
            commands.push(line);
          }
        }
        if (!commands.length) return;
        postBatch(commands, 'MACRO' + String(index));
      }

      function loadMacrosToForm() {
        var macros = defaultMacros();
        try {
          var raw = localStorage.getItem(state.macroStorageKey);
          if (raw) {
            var parsed = JSON.parse(raw);
            if (parsed && parsed.length) macros = parsed;
          }
        } catch (e) {
          logLine('MACRO LOAD ERR');
        }
        var i;
        for (i = 1; i <= state.macroCount; i++) {
          var entry = getMacroEntry(i);
          var macro = macros[i - 1] || { name: '', cmd: '' };
          if (entry.nameInput) entry.nameInput.value = macro.name || '';
          if (entry.cmdInput) entry.cmdInput.value = macro.cmd || '';
        }
      }

      function saveMacrosFromForm() {
        var out = [];
        var i;
        for (i = 1; i <= state.macroCount; i++) {
          var entry = getMacroEntry(i);
          out.push({
            name: entry.nameInput ? (entry.nameInput.value || '').trim() : '',
            cmd: entry.cmdInput ? (entry.cmdInput.value || '').trim() : ''
          });
        }
        try {
          localStorage.setItem(state.macroStorageKey, JSON.stringify(out));
          if (typeof customAlert === 'function') customAlert(wtSafe('grblMacroSaved', '✅ Configuration saved!'));
          if (typeof showSavedCard === 'function') showSavedCard();
          logLine('MACROS SAVED');
        } catch (e) {
          showAlert('&#10060; ' + wtSafe('grblMacroSaveFailed', 'Failed to save macro.'));
          logLine('MACRO SAVE ERR');
        }
      }
)GRBL"


