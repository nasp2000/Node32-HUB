R"GRBL(
      function postActionOnce(url, payload, onOkLog, silent) {
        return apiPost(url, payload)
          .then(function(data) {
            if (data && data.success) {
              logLine(onOkLog);
              refreshStatus(true);
              return true;
            }
            var msg = (data && data.message) ? String(data.message) : 'request_failed';
            var shownMsg = msg;
            if (msg === 'usb_not_running') {
              shownMsg = wtSafe('grblUsbErrNotRunning', 'USB host is not running. Reconnect USB and try again.');
            } else if (msg === 'usb_slot_not_connected' || msg === 'usb_slot_invalid') {
              shownMsg = wtSafe('grblUsbErrSlotDisconnected', 'Selected USB slot is not connected.');
            } else if (msg === 'line_coding_timeout') {
              shownMsg = wtSafe('grblUsbErrTimeout', 'Timeout while applying USB-CDC baud. Try reconnecting USB.');
            } else if (msg === 'line_coding_transfer_failed' || msg === 'line_coding_submit_failed' || msg === 'line_coding_sync_failed' || msg === 'line_coding_device_unmounted' || msg === 'line_coding_device_inactive' || msg === 'line_coding_lock_timeout' || msg === 'line_coding_lock_missing' || msg === 'line_coding_failed') {
              shownMsg = wtSafe('grblUsbErrApplyFailed', 'Failed to apply USB-CDC baud on this adapter.');
            }
            if (!silent) showAlert('&#10060; ' + shownMsg);
            logLine('ERR ' + url + ' ' + msg);
            return false;
          })
          .catch(function() {
            if (!silent) showAlert('&#10060; ' + wtSafe('grblConnFailed', 'Connection failed.'));
            logLine('ERR ' + url);
            return false;
          });
      }

      function postAction(url, payload, onOkLog, silent) {
        return apiPost(url, payload)
          .then(function(data) {
            if (data && data.success) {
              logLine(onOkLog);
              refreshStatus(true);
              return true;
            }
            var msg = (data && data.message) ? String(data.message) : 'request_failed';
            // When the token expired (e.g. ESP32 reboot cleared the token table in RAM),
            // re-fetch the control token and retry the action once automatically.
            if (msg === 'auth_token_invalid') {
              logLine('token expired, refreshing...');
              return fetchControlToken().then(function(ok) {
                if (!ok) {
                  if (!silent) showAlert('&#10060; ' + wtSafe('grblTokenExpired', 'Control token expired. Reconnect and try again.'));
                  return false;
                }
                return postActionOnce(url, payload, onOkLog, silent);
              });
            }
            var shownMsg = msg;
            if (msg === 'usb_not_running') {
              shownMsg = wtSafe('grblUsbErrNotRunning', 'USB host is not running. Reconnect USB and try again.');
            } else if (msg === 'usb_slot_not_connected' || msg === 'usb_slot_invalid') {
              shownMsg = wtSafe('grblUsbErrSlotDisconnected', 'Selected USB slot is not connected.');
            } else if (msg === 'line_coding_timeout') {
              shownMsg = wtSafe('grblUsbErrTimeout', 'Timeout while applying USB-CDC baud. Try reconnecting USB.');
            } else if (msg === 'line_coding_transfer_failed' || msg === 'line_coding_submit_failed' || msg === 'line_coding_sync_failed' || msg === 'line_coding_device_unmounted' || msg === 'line_coding_device_inactive' || msg === 'line_coding_lock_timeout' || msg === 'line_coding_lock_missing' || msg === 'line_coding_failed') {
              shownMsg = wtSafe('grblUsbErrApplyFailed', 'Failed to apply USB-CDC baud on this adapter.');
            }
            if (!silent) showAlert('&#10060; ' + shownMsg);
            logLine('ERR ' + url + ' ' + msg);
            return false;
          })
          .catch(function() {
            if (!silent) showAlert('&#10060; ' + wtSafe('grblConnFailed', 'Connection failed.'));
            logLine('ERR ' + url);
            return false;
          });
      }

      // Guaranteed filter for EVERY command the page sends: strip Z-axis words
      // when the connected machine is 2-axis. A 3-axis (or unknown) machine
      // passes Z through untouched.
      function _filterAxisCommand(cmd) {
        if (_hasZAxis()) return cmd;
        var stripped = String(cmd || '').replace(/\s*Z[-+]?\d*\.?\d*/gi, '').trim();
        if (!stripped) return '';
        // A $J jog that lost its only axis word would be invalid — drop it.
        if (stripped.indexOf('$J') === 0 && !/[XYZ]/.test(stripped)) return '';
        return stripped;
      }

      function postSend(cmd, tag, silent) {
        cmd = _filterAxisCommand(cmd);
        if (!cmd) return Promise.resolve(false);
        return postAction('/api/grbl/send', { slot: selectedSlot(), cmd: cmd }, tag, silent);
      }

      function postBatch(commands, tag) {
        var chain = Promise.resolve();
        var i;
        for (i = 0; i < commands.length; i++) {
          (function(commandText) {
            chain = chain.then(function() {
              return postSend(commandText, tag + ' ' + commandText, true);
            }).then(function() {
              return new Promise(function(resolve) {
                setTimeout(resolve, 150);
              });
            });
          })(commands[i]);
        }
        return chain;
      }

      function sendManualCommand() {
        var cmd = '';
        var sourceInput = null;
        var consoleCmdInput = byId('grbl_console_cmd');

        if (consoleCmdInput) {
          cmd = String(consoleCmdInput.value || '').trim();
          if (cmd.length > 0) sourceInput = consoleCmdInput;
        }

        if (!cmd) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblEmptyCmd', 'Enter a command to send.'));
          return;
        }
        setCommandMode('manual');
        postSend(cmd, 'SEND').then(function(ok) {
          if (!ok) return;
          if (sourceInput) sourceInput.value = '';
        });
      }

      function startFileStream() {
        var path = String(state.selectedSdPath || '').trim();
        path = path.trim();
        if (!path) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblSdPickFileFirst', 'Select a G-code file first.'));
          return;
        }
        if (state.selectedSdType === 'dir') {
          showAlert('&#9888;&#65039; ' + wtSafe('grblSdPickFileFirst', 'Select a G-code file first.'));
          return;
        }

        var pv = state.preview;
        if (pv.contourSelectEnabled === true && pv.contourDisabled && pv.contourDisabled.length) {
          var hasDisabled = false;
          for (var di = 0; di < pv.contourDisabled.length; di++) {
            if (pv.contourDisabled[di]) { hasDisabled = true; break; }
          }
          if (hasDisabled) {
            _startContourFilteredStream(path);
            return;
          }
        }

        runWithCriticalConfirm('grblConfirmStartFile', 'Start selected file?', function() {
          requireAxesAtZero(function() {
            // Keep 3D preview in sync with the selected SD file even on layouts
            // where the local queue textarea is not present.
            loadSelectedSdToWorkspace({ silent: true });
            setCommandMode('sd-stream');
            postAction('/api/grbl/stream', { slot: selectedSlot(), path: path }, 'STREAM ' + path);
          });
        });
      }

      function pauseStream() {
        postAction('/api/grbl/pause', { slot: selectedSlot() }, 'PAUSE');
      }

      function _startContourFilteredStream(path) {
        var pv = state.preview;
        var confirmMsg = wtSafe('grblContourFilterConfirm', 'Run filtered G-code? Disabled contours will be skipped.') ||
                         'Run filtered G-code? Disabled contours will be skipped.';
        customConfirm(confirmMsg, function(ok) {
          if (!ok) return;
          requireAxesAtZero(function() {
            var disabled = pv.contourDisabled || [];
            var ranges = (pv.lightPreview && pv.lightPreview.contourRanges) || [];
            var src = String(state.workspaceText || '');
            if (!src) {
              showAlert('&#9888;&#65039; ' + wtSafe('grblSdLoadWorkspace', 'Load a file to the workspace first.'));
              return;
            }
            var anyEnabled = false;
            for (var ai = 0; ai < ranges.length; ai++) {
              if (!disabled[ai]) { anyEnabled = true; break; }
            }
            if (!anyEnabled) {
              showAlert('&#9888;&#65039; ' + (wtSafe('grblContourAllDisabled', 'All contours are disabled. Nothing to run.') || 'All contours are disabled. Nothing to run.'));
              return;
            }
            var filteredText = filterContoursPreserveCoords(src, ranges, disabled);
            var blob = new Blob([filteredText], { type: 'text/plain' });
            var file = new File([blob], 'filtered_' + sanitizeFileName(path), { type: 'text/plain' });
            var overlay = document.getElementById('loadingOverlay');
            if (overlay) overlay.style.display = 'flex';
            var xhr = new XMLHttpRequest();
            var query = 'name=' + encodeURIComponent(sanitizeFileName(file.name)) +
                        '&sid=' + encodeURIComponent(state.sessionId || 'anon');
            if (state.controlToken) query += '&token=' + encodeURIComponent(state.controlToken);
            xhr.open('POST', '/api/grbl/psram-upload?' + query, true);
            xhr.setRequestHeader('Content-Type', 'application/octet-stream');
            xhr.setRequestHeader('X-Filename', sanitizeFileName(file.name));
            xhr.setRequestHeader('X-Filesize', String(blob.size || 0));
            xhr.onload = function() {
              if (overlay) overlay.style.display = 'none';
              var data = null;
              try { data = JSON.parse(xhr.responseText || '{}'); } catch (e) { data = null; }
              if (data && data.success === true) {
                setCommandMode('psram-stream');
                postAction('/api/grbl/psram-stream', { slot: selectedSlot() }, 'PSRAM STREAM ' + file.name);
              } else {
                var msg = data && data.message ? String(data.message) : (wtSafe('grblPsramUploadFail', 'Failed to upload to PSRAM.') || 'PSRAM upload failed');
                showAlert('❌ ' + msg);
              }
            };
            xhr.onerror = function() {
              if (overlay) overlay.style.display = 'none';
              showAlert('❌ ' + (wtSafe('grblUploadFile', 'Error uploading file') || 'Upload failed'));
            };
            xhr.send(blob);
          });
        });
      }

      function resumeStream() {
        // If no job is active, ask before starting the selected SD file.
        var workflow = (state.lastStatus && state.lastStatus.workflow_state) ? String(state.lastStatus.workflow_state).toLowerCase() : 'idle';
        var isActive = workflow === 'running' || workflow === 'paused' || workflow === 'hold';
        if (!isActive) {
          runWithCriticalConfirm('grblConfirmResumeAsStart', 'No active job. Start selected file?', function() {
            startFileStream();
          });
          return;
        }
        postAction('/api/grbl/resume', { slot: selectedSlot() }, 'RESUME');
      }

      function cancelStream() {
        postAction('/api/grbl/cancel', { slot: selectedSlot() }, 'CANCEL');
      }

      function applyPollInterval() {
        var raw = byId('grbl_poll_ms').value || '0';
        var ms = parseInt(raw, 10);
        if (isNaN(ms) || ms <= 0) ms = 500;
        if (ms > 10000) ms = 10000;
        byId('grbl_poll_ms').value = String(ms);
        postAction('/api/grbl/poll-interval', { ms: ms }, 'POLL ' + String(ms)).then(function(ok) {
          if (!ok) return;
          if (typeof customAlert === 'function') customAlert(wtSafe('alertSaved', '✅ Configuration saved!'));
          if (typeof showSavedCard === 'function') showSavedCard();
        });
      }

      function applyUsbLineCoding() {
        var baudRaw = byId('grbl_usb_baud') ? byId('grbl_usb_baud').value : '115200';
        var baud = parseInt(baudRaw || '0', 10);
        if (isNaN(baud) || baud <= 0) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblUsbBaudInvalid', 'Invalid baud.'));
          return;
        }

        var btn = byId('btn_usb_apply_baud');
        if (btn) btn.disabled = true;

        savePreferredUsbBaud(getUsbSlotInfo((state.lastStatus && state.lastStatus.usb_slots) ? state.lastStatus.usb_slots : [], selectedSlot()), baud);

        postAction('/api/grbl/usb-line-coding', {
          slot: selectedSlot(),
          baud: baud,
          data_bits: 8,
          stop_bits: 1,
          parity: 0
        }, 'USB BAUD ' + String(baud)).then(function(ok) {
          if (!ok) return;
          if (typeof customAlert === 'function') customAlert(wtSafe('grblUsbBaudAppliedPrefix', '✅ USB-CDC baud applied: ') + String(baud));
          if (typeof showSavedCard === 'function') showSavedCard();
          refreshStatus(true);
        }).finally(function() {
          if (btn) btn.disabled = false;
        });
      }

      // Manual jogging always happens with the tool OFF. The laser/spindle is
      // stopped (M5) before the jog is sent so a jog can never fire the tool —
      // interrupted jobs previously left the laser on during step/center moves.
      function _jogWithToolOff(line, tag) {
        postBatch(['M5', line], tag);
      }

      function runJog(axis, direction) {
        var stepRaw = byId(axis === 'Z' ? 'grbl_step_z_sel' : 'grbl_step_xy_sel');
        stepRaw = (stepRaw ? stepRaw.value : '0') || '0';
        var feedRaw = byId('grbl_feed').value || '0';
        var step = parseFloat(stepRaw);
        var feed = parseInt(feedRaw, 10);
        if (isNaN(step) || step <= 0) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblInvalidJogStep', 'Invalid jog step value.'));
          return;
        }
        if (isNaN(feed) || feed <= 0) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblInvalidFeed', 'Invalid feed rate.'));
          return;
        }

        var signed = (direction < 0 ? '-' : '') + String(step);
        var line = '$J=G91 G21 ' + axis + signed + ' F' + String(feed);
        _jogWithToolOff(line, 'JOG ' + line);
      }

      function runJogXY(xDir, yDir) {
        var stepRaw = byId('grbl_step_xy_sel');
        stepRaw = (stepRaw ? stepRaw.value : '0') || '0';
        var feedRaw = byId('grbl_feed').value || '0';
        var step = parseFloat(stepRaw);
        var feed = parseInt(feedRaw, 10);
        if (isNaN(step) || step <= 0) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblInvalidJogStep', 'Invalid jog step value.'));
          return;
        }
        if (isNaN(feed) || feed <= 0) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblInvalidFeed', 'Invalid feed rate.'));
          return;
        }
        var xs = (xDir < 0 ? '-' : '') + String(step);
        var ys = (yDir < 0 ? '-' : '') + String(step);
        var line = '$J=G91 G21 X' + xs + ' Y' + ys + ' F' + String(feed);
        _jogWithToolOff(line, 'JOG XY ' + line);
      }

      function runJogXYCenter() {
        var feedRaw = byId('grbl_feed') ? byId('grbl_feed').value : '0';
        var feed = parseInt(feedRaw, 10);
        if (isNaN(feed) || feed <= 0) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblInvalidFeed', 'Invalid feed rate.'));
          return;
        }
        var mt = state.maxTravel || {};
        if (!(mt.x > 0) || !(mt.y > 0)) {
          _jogWithToolOff('G90 G1 X0 Y0 F' + String(feed), 'JOG HOME FALLBACK');
          return;
        }
        var line = '$J=G90 G21 X' + fmtGcodeNum(mt.x / 2, 3) + ' Y' + fmtGcodeNum(mt.y / 2, 3) + ' F' + String(feed);
        _jogWithToolOff(line, 'JOG XY CENTER');
      }

      function quickUnlock() {
        runWithCriticalConfirm('grblConfirmUnlock', 'Send $X (unlock) now?', function() {
          postSend('$X', 'UNLOCK');
        });
      }

      function quickHome() {
        runWithCriticalConfirm('grblConfirmHome', 'Execute machine HOME now?', function() {
          postSend('$H', 'HOME');
        });
      }

      // A 2-axis machine (axis_count < 3 from the status) errors on any Z
      // command, so Z must be omitted from generated G-code. Unknown state
      // (no status yet) assumes 3-axis so Z is never wrongly stripped.
      function _hasZAxis() {
        var st = state && state.lastStatus;
        if (!st) return true;
        var c = parseInt(st.axis_count, 10);
        if (isNaN(c) || !isFinite(c)) return true;
        return c >= 3;
      }

      function quickZeroWcs() {
        runWithCriticalConfirm('grblConfirmZeroWCS', 'Zero X Y Z on current WCS?', function() {
          // G92 is supported by every GRBL version (G10 L20 only exists on 1.1
          // and is rejected with "Unsupported GCode command" on 0.9/variants).
          // Z is only included when the machine actually has a Z axis.
          var z = _hasZAxis() ? ' Z0' : '';
          postSend('G92 X0 Y0' + z, 'ZERO_WCS');
        });
      }

      function fmtGcodeNum(v, decimals) {
        var n = parseFloat(v);
        if (!isFinite(n)) return '0';
        var d = parseInt(decimals || 3, 10);
        if (isNaN(d) || d < 0) d = 3;
        if (d > 5) d = 5;
        var s = n.toFixed(d);
        s = s.replace(/\.?0+$/, '');
        if (!s || s === '-0') s = '0';
        return s;
      }

      function runAutoProbeZZero() {
        var data = state.lastStatus || {};
        if (data && data.probe_triggered) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblProbeAlreadyTriggered', 'Probe is already triggered. Check connection/plate before starting.'));
          return;
        }

        var plate = parseFloat(byId('grbl_probe_plate') ? byId('grbl_probe_plate').value : '0');
        var retract = parseFloat(byId('grbl_probe_retract') ? byId('grbl_probe_retract').value : '0');
        var fastFeed = parseFloat(byId('grbl_probe_fast_feed') ? byId('grbl_probe_fast_feed').value : '0');
        var slowFeed = parseFloat(byId('grbl_probe_slow_feed') ? byId('grbl_probe_slow_feed').value : '0');
        var fastDist = parseFloat(byId('grbl_probe_fast_dist') ? byId('grbl_probe_fast_dist').value : '0');
        var slowDist = parseFloat(byId('grbl_probe_slow_dist') ? byId('grbl_probe_slow_dist').value : '0');

        if (!isFinite(plate) || plate <= 0 || plate > 50) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblProbeInvalidPlate', 'Invalid plate thickness.'));
          return;
        }
        if (!isFinite(retract) || retract <= 0 || retract > 20) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblProbeInvalidRetract', 'Invalid retract.'));
          return;
        }
        if (!isFinite(fastFeed) || fastFeed < 20 || fastFeed > 2000) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblProbeInvalidFastFeed', 'Invalid fast feed.'));
          return;
        }
        if (!isFinite(slowFeed) || slowFeed < 5 || slowFeed > 600) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblProbeInvalidSlowFeed', 'Invalid slow feed.'));
          return;
        }
        if (!isFinite(fastDist) || fastDist <= 0 || fastDist > 100) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblProbeInvalidFastDist', 'Invalid fast travel.'));
          return;
        }
        if (!isFinite(slowDist) || slowDist <= 0 || slowDist > 20) {
          showAlert('&#9888;&#65039; ' + wtSafe('grblProbeInvalidSlowDist', 'Invalid slow travel.'));
          return;
        }

        runWithCriticalConfirm('grblConfirmProbeAutoZero', 'Start Auto Z Zero cycle now?', function() {
          showAlert('\u{1F9EA} ' + wtSafe('grblProbeAutoStarted', 'Auto Z Zero started...'));
          setCommandMode('manual');
          // First probe: fast approach
          postSend('G90 ' + 'G38.2 Z-' + fmtGcodeNum(fastDist, 3) + ' F' + fmtGcodeNum(fastFeed, 0), 'PROBE_Z').then(function(ok) {
            if (!ok) {
              showAlert('✔ ' + wtSafe('grblProbeAutoFail', 'Failed in Auto Z Zero cycle (fast probe).'));
              return;
            }
            // Retract
            return postSend('G91 G0 Z' + fmtGcodeNum(retract, 3), 'PROBE_Z RETRACT').then(function(ok2) {
              if (!ok2) return false;
              // Second probe: slow approach
              return postSend('G90 G38.2 Z-' + fmtGcodeNum(slowDist, 3) + ' F' + fmtGcodeNum(slowFeed, 0), 'PROBE_Z SLOW');
            }).then(function(ok3) {
              if (!ok3) {
                showAlert('✔ ' + wtSafe('grblProbeAutoFail', 'Auto Z Zero cycle failed (slow probe).'));
                return false;
              }
              // Retract again
              return postSend('G91 G0 Z' + fmtGcodeNum(retract, 3), 'PROBE_Z RETRACT2');
            }).then(function(ok4) {
              if (!ok4) return false;
              // Set Z zero with plate compensation
              return postSend('G90 G92 Z' + fmtGcodeNum(plate, 3), 'PROBE_Z SET');
            }).then(function(ok5) {
              if (!ok5) {
                showAlert('✔ ' + wtSafe('grblProbeAutoFail', 'Auto Z Zero cycle failed (set zero).'));
                return false;
              }
              showAlert('✅ ' + wtSafe('grblProbeAutoDone', 'Auto Z Zero completed.'));
              refreshStatus(true);
            });
          });
        });
      }

      function quickReset() {
        runWithCriticalConfirm('grblConfirmReset', 'Send GRBL reset now?', function() {
          postSend(String.fromCharCode(24), 'RESET');
        });
      }

      function quickStatus() {
        postSend('?', 'STATUS');
      }

      function getPreviewFrameBounds() {
        var pts = state.preview.points && state.preview.points.length ? state.preview.points : [];
        if (!pts || pts.length < 2) return null;
        var minX = Infinity;
        var maxX = -Infinity;
        var minY = Infinity;
        var maxY = -Infinity;
        var i;
        for (i = 0; i < pts.length; i++) {
          var p = pts[i];
          if (!p) continue;
          if (typeof p.x === 'number') {
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
          }
          if (typeof p.y === 'number') {
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
          }
        }
        if (!isFinite(minX) || !isFinite(maxX) || !isFinite(minY) || !isFinite(maxY)) return null;
        if ((maxX - minX) <= 0 || (maxY - minY) <= 0) return null;
        return { minX: minX, maxX: maxX, minY: minY, maxY: maxY };
      }

      function updateFrameInfo() {
        var out = byId('grbl_frame_bbox');
        if (!out) return;
        var b = getPreviewFrameBounds();
        if (!b) {
          if (state.selectedSdType === 'file' && String(state.selectedSdPath || '').trim()) {
            out.textContent = wtSafe('grblFrameNoPath', 'No local preview. Frame will use selected SD file.');
          } else {
            out.textContent = wtSafe('grblFrameNoPath', 'No local path loaded to calculate frame.');
          }
          return;
        }
        var w = (b.maxX - b.minX).toFixed(2);
        var h = (b.maxY - b.minY).toFixed(2);
        out.textContent = wtSafe('grblFrameBboxLbl', 'Area') + ': X[' + b.minX.toFixed(2) + ' .. ' + b.maxX.toFixed(2) + ']  Y[' + b.minY.toFixed(2) + ' .. ' + b.maxY.toFixed(2) + ']  W=' + w + '  H=' + h;
      }

      function fetchFrameBoundsFromSelectedSd(path) {
        var requestUrl = '/api/grbl/frame-bbox?path=' + encodeURIComponent(path) + '&sid=' + encodeURIComponent(state.sessionId || 'anon');
        return fetch(requestUrl, { cache: 'no-store' })
          .then(function(resp) {
            if (!resp || !resp.ok) throw new Error('http_' + String(resp ? (resp.status || 0) : 0));
            return resp.json();
          })
          .then(function(data) {
            if (!data || data.success !== true) return null;
            var minX = parseFloat(data.min_x);
            var maxX = parseFloat(data.max_x);
            var minY = parseFloat(data.min_y);
            var maxY = parseFloat(data.max_y);
            if (!isFinite(minX) || !isFinite(maxX) || !isFinite(minY) || !isFinite(maxY)) return null;
            return {
              minX: minX,
              maxX: maxX,
              minY: minY,
              maxY: maxY
            };
          });
      }

      // ── Height Map / Probe Grid ───────────────────────────────────

      function startProbeGrid() {
        var cfg = {
          startX: parseFloat((byId('hm_startX') || {}).value || 0),
          startY: parseFloat((byId('hm_startY') || {}).value || 0),
          endX:   parseFloat((byId('hm_endX') || {}).value || 100),
          endY:   parseFloat((byId('hm_endY') || {}).value || 100),
          spacing: parseFloat((byId('hm_spacing') || {}).value || 10),
          probeFeed: parseFloat((byId('hm_probeFeed') || {}).value || 50),
          probeMaxDepth: parseFloat((byId('hm_probeMaxDepth') || {}).value || 20),
          safeZ: parseFloat((byId('hm_safeZ') || {}).value || 5),
          clearHeight: parseFloat((byId('hm_clearHeight') || {}).value || 10)
        };
        var payload = 'slot=' + selectedSlot() + '&sid=' + encodeURIComponent(state.sessionId || 'anon') +
          '&token=' + encodeURIComponent(state.controlToken || '') +
          '&startX=' + cfg.startX + '&startY=' + cfg.startY +
          '&endX=' + cfg.endX + '&endY=' + cfg.endY +
          '&spacing=' + cfg.spacing + '&probeFeed=' + cfg.probeFeed +
          '&probeMaxDepth=' + cfg.probeMaxDepth + '&safeZ=' + cfg.safeZ +
          '&clearHeight=' + (cfg.clearHeight || cfg.safeZ + 5);
        return apiPost('/api/grbl/probe-grid', payload)
          .then(function(d) {
            if (d && d.success) { logLine('Probe grid started'); refreshStatus(true); return true; }
            showAlert('Failed: ' + ((d && d.message) || 'unknown'));
            return false;
          });
      }

      function cancelProbeGrid() {
        var payload = 'sid=' + encodeURIComponent(state.sessionId || 'anon') +
          '&token=' + encodeURIComponent(state.controlToken || '');
        return apiPost('/api/grbl/probe-grid-cancel', payload)
          .then(function(d) {
            if (d && d.success) { logLine('Probe grid cancelled'); refreshStatus(true); return true; }
            return false;
          });
      }

      function toggleHeightMap(enable) {
        var payload = 'sid=' + encodeURIComponent(state.sessionId || 'anon') +
          '&token=' + encodeURIComponent(state.controlToken || '') +
          '&enabled=' + (enable ? '1' : '0');
        return apiPost('/api/grbl/height-map-enable', payload)
          .then(function(d) {
            if (d && d.success) { logLine('Height map ' + (enable ? 'enabled' : 'disabled')); refreshStatus(true); }
          });
      }

      function clearHeightMap() {
        var payload = 'sid=' + encodeURIComponent(state.sessionId || 'anon') +
          '&token=' + encodeURIComponent(state.controlToken || '');
        return apiPost('/api/grbl/height-map-clear', payload)
          .then(function(d) {
            if (d && d.success) { logLine('Height map cleared'); refreshStatus(true); }
          });
      }

      function fetchSdTextForLightPreview(path) {
        var requestUrl = '/api/grbl/read-gcode-text?path=' + encodeURIComponent(path) + '&sid=' + encodeURIComponent(state.sessionId || 'anon');
        return fetch(requestUrl, { cache: 'no-store' })
          .then(function(resp) {
            if (!resp || !resp.ok) throw new Error('http_' + String(resp ? (resp.status || 0) : 0));
            return resp.text();
          })
          .then(function(txt) {
            return String(txt || '');
          });
      }
)GRBL"


