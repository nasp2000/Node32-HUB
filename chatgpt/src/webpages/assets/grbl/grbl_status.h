R"GRBL(
      function applyRuntimeControls(data) {
        var workflow = String(data.workflow_state || 'idle').toLowerCase();
        var source = String(data.job_source || 'none').toLowerCase();
        var running = workflow === 'running';
        var paused = workflow === 'paused';
        var active = !!data.job_active;
        var queueActive = source === 'local_queue' && active;
        var canWrite = (data.control_can_write !== false);
        state.canWrite = !!canWrite;
        // A safe stop (ack_stall / rx_stall) puts the job in HOLD state; resume is blocked.
        var holdReason = String(data.hold_reason || '');
        var safeStop = holdReason === 'ack_stall_safe_stop' || holdReason === 'rx_stall';

        setDisabled('btn_stream_start', !canWrite || active);
        setDisabled('btn_quick_cancel', !canWrite || !active);
        setDisabled('btn_console_send', !canWrite);
        setDisabled('grbl_console_cmd', !canWrite);
        setDisabled('btn_apply_poll', !canWrite);
        setDisabled('btn_quick_unlock', !canWrite);
        setDisabled('btn_quick_zero_wcs', !canWrite);
        setDisabled('btn_quick_reset', !canWrite);
        setDisabled('btn_quick_hold', !canWrite);
        setDisabled('btn_quick_resume', !canWrite || safeStop || running);
        setDisabled('btn_jog_x_neg', !canWrite);
        setDisabled('btn_jog_x_pos', !canWrite);
        setDisabled('btn_jog_y_neg', !canWrite);
        setDisabled('btn_jog_y_pos', !canWrite);
        setDisabled('btn_jog_xy_nw', !canWrite);
        setDisabled('btn_jog_xy_ne', !canWrite);
        setDisabled('btn_jog_xy_sw', !canWrite);
        setDisabled('btn_jog_xy_se', !canWrite);
        setDisabled('btn_jog_z_neg', !canWrite);
        setDisabled('btn_jog_z_pos', !canWrite);
        setDisabled('btn_jog_home', !canWrite);
        setDisabled('btn_jog_xy_center', !canWrite);
        setDisabled('btn_zero_xy', !canWrite || active);
        setDisabled('btn_zero_z', !canWrite || active);
        setDisabled('grbl_wcs_sel', !canWrite || active);
        setDisabled('btn_probe_auto_zero', !canWrite || active);
        setDisabled('grbl_step', !canWrite);
        setDisabled('grbl_feed', !canWrite);
        setDisabled('grbl_poll_ms', !canWrite);
        setDisabled('grbl_probe_plate', !canWrite || active);
        setDisabled('grbl_probe_retract', !canWrite || active);
        setDisabled('grbl_probe_fast_feed', !canWrite || active);
        setDisabled('grbl_probe_slow_feed', !canWrite || active);
        setDisabled('grbl_probe_fast_dist', !canWrite || active);
        setDisabled('grbl_probe_slow_dist', !canWrite || active);
        setDisabled('btn_backplot_toggle', !state.preview.hasPath && !state.preview.lightPreview);
        // Height map buttons
        setDisabled('btn_hm_start', !canWrite || active || state.probeGridPhase === 'probing' || state.probeGridPhase === 'moving_xy');
        setDisabled('btn_hm_cancel', !canWrite || state.probeGridPhase === 'idle' || state.probeGridPhase === 'complete' || state.probeGridPhase === 'cancelled');
        setDisabled('btn_hm_clear', !canWrite || state.probeGridPhase === 'probing' || state.probeGridPhase === 'moving_xy');
        // SD refresh is a read-only operation — always enabled.
        setDisabled('btn_grbl_sd_refresh', false);
        setDisabled('btn_grbl_sd_upload', !canWrite || active || queueActive || state.sdUploadActive);
        setDisabled('grbl_frame_power', !canWrite || active);
        setDisabled('grbl_frame_feed', !canWrite || active);
        setDisabled('btn_frame_run', !canWrite || active);
        // Transport switch is blocked while a job is active or without control.
        setDisabled('grbl_transport_toggle', !canWrite || active);
        updateSdPreviewLoadButton();
      }

      function applyStatusSnapshot(data) {
        var pollMs = parseInt(data.poll_interval_ms || 0, 10);
        if (!isNaN(pollMs) && pollMs >= 0) {
          var pollInput = byId('grbl_poll_ms');
          if (pollInput) pollInput.value = String(pollMs);
        }

        applyRuntimeControls(data);
        setCommandMode(deriveRuntimeMode(data));

        var controlMode = String(data.control_mode || 'free');
        if (controlMode !== state.lastControlMode) {
          state.lastControlMode = controlMode;
          if (controlMode === 'observer') {
            logLine(wtSafe('grblControlModeObserver', 'CONTROL MODE observer'));
          } else if (controlMode === 'operator') {
            logLine(wtSafe('grblControlModeOperator', 'CONTROL MODE operator'));
          } else {
            logLine(wtSafe('grblControlModeFree', 'CONTROL MODE free'));
          }
        }

        if (!state.rehydrated) {
          state.rehydrated = true;
          if (data.job_active) {
            var source = data.job_source || 'none';
            var wf = data.workflow_state || 'idle';
            logLine(wtSafe('grblSessionRehydratedJobPrefix', 'SESSION REHYDRATED job=') + source + ' workflow=' + wf);
          } else {
            logLine(wtSafe('grblSessionRehydratedIdle', 'SESSION REHYDRATED idle'));
          }
        }

        if (data.alarm_active === true) {
          var alarmText = String(data.alarm_message || 'ALARM');
          if (alarmText !== state.lastAlarmText) {
            state.lastAlarmText = alarmText;
            logLine(wtSafe('grblAlarmPrefix', 'ALARM ') + alarmText);
          }
        } else {
          state.lastAlarmText = '';
        }

        var holdReasonNow = String(data.hold_reason || '');
        if (holdReasonNow !== state.lastHoldReason) {
          state.lastHoldReason = holdReasonNow;
          if (holdReasonNow === 'grbl_error') {
            logLine(wtSafe('grblHoldErrorLog', 'HOLD grbl_error (paused after controller error)'));
          } else if (holdReasonNow === 'ack_stall_safe_stop') {
            logLine('⚠️ ' + wtSafe('grblHoldAckStall', 'GRBL stopped responding (ACK timeout 25s). Job paused. Check G-code and cancel if needed.'));
          } else if (holdReasonNow === 'rx_stall') {
            logLine('⚠️ ' + wtSafe('grblHoldRxStall', 'GRBL RX stall (no data). Job paused.'));
          } else if (holdReasonNow === 'feed_stall') {
            logLine('⚠️ ' + wtSafe('grblHoldFeedStall', 'GRBL feed stalled (no data flowing). Job paused.'));
          }
        }
      }

      function parseNumber3(v) {
        var n = parseFloat(v);
        if (!isFinite(n)) return null;
        return n;
      }

      function parsePosTriplet(str) {
        var raw = String(str || '').trim();
        if (!raw) return null;
        var arr = raw.split(',');
        if (arr.length < 2) return null;
        var x = parseNumber3(arr[0]);
        var y = parseNumber3(arr[1]);
        var z = arr.length >= 3 ? parseNumber3(arr[2]) : null;
        if (x === null || y === null) return null;
        return { x: x, y: y, z: z };
      }

      function firstNumber3() {
        var i;
        for (i = 0; i < arguments.length; i++) {
          if (arguments[i] === undefined || arguments[i] === null || arguments[i] === '') continue;
          var n = parseNumber3(arguments[i]);
          if (n !== null) return n;
        }
        return null;
      }

      function extractDroFromStatus(data) {
        var out = {
          m: { x: null, y: null, z: null },
          w: { x: null, y: null, z: null }
        };

        var mposValid = !!(data && (data.mpos_valid === true || data.mpos_valid === 1 || data.mpos_valid === '1'));
        var wposValid = !!(data && (data.wpos_valid === true || data.wpos_valid === 1 || data.wpos_valid === '1'));

        if (mposValid) {
          out.m.x = firstNumber3(data.mpos_x, data.machine_x, data.mx);
          out.m.y = firstNumber3(data.mpos_y, data.machine_y, data.my);
          out.m.z = firstNumber3(data.mpos_z, data.machine_z, data.mz);
        }
        if (wposValid) {
          out.w.x = firstNumber3(data.wpos_x, data.work_x, data.wx);
          out.w.y = firstNumber3(data.wpos_y, data.work_y, data.wy);
          out.w.z = firstNumber3(data.wpos_z, data.work_z, data.wz);
        }

        var raw = String(data.status_raw || data.grbl_status || data.state || '');
        if (raw) {
          var mm = raw.match(/MPos:([-0-9.]+,[-0-9.]+(?:,[-0-9.]+)?)/i);
          var ww = raw.match(/WPos:([-0-9.]+,[-0-9.]+(?:,[-0-9.]+)?)/i);
          var mtrip = mm ? parsePosTriplet(mm[1]) : null;
          var wtrip = ww ? parsePosTriplet(ww[1]) : null;
          if (mtrip) out.m = mtrip;
          if (wtrip) out.w = wtrip;
        }
        return out;
      }

      function fmtDro(v) {
        // Explicit null/undefined check is required because isFinite(null)===true in JS (null coerces to 0).
        if (v === null || v === undefined || !isFinite(v)) return '--';
        return (Math.round(v * 1000) / 1000).toFixed(3);
      }

      function hasValidDroValue(v) {
        // Explicit null/undefined check: isFinite(null) returns true, which would incorrectly show 0.000.
        return v !== null && v !== undefined && isFinite(v);
      }

      function renderDro(data) {
        var axisCount = parseInt((data && data.axis_count) || 3, 10);
        if (!axisCount || axisCount < 2) axisCount = 3;
        var d = extractDroFromStatus(data || {});
        var panel = byId('grbl_dro_panel');
        var mx = byId('grbl_dro_mx');
        var my = byId('grbl_dro_my');
        var mz = byId('grbl_dro_mz');
        var wx = byId('grbl_dro_wx');
        var wy = byId('grbl_dro_wy');
        var wz = byId('grbl_dro_wz');
        // DRO panel is always visible; values show '--' when no telemetry is available.
        if (panel) panel.style.display = '';

        // Sync WCS selector to active WCS from server
        if (data && data.active_wcs) {
          var wcsEl = byId('grbl_wcs_sel');
          if (wcsEl && wcsEl.value !== data.active_wcs) {
            wcsEl.value = data.active_wcs;
          }
        }

        if (mx) mx.textContent = fmtDro(d.m.x);
        if (my) my.textContent = fmtDro(d.m.y);
        if (mz) mz.textContent = axisCount >= 3 ? fmtDro(d.m.z) : '--';
        if (wx) wx.textContent = fmtDro(d.w.x);
        if (wy) wy.textContent = fmtDro(d.w.y);
        if (wz) wz.textContent = axisCount >= 3 ? fmtDro(d.w.z) : '--';

        // Show/hide Z row based on detected axis count
        var zAx = byId('grbl_dro_z_ax');
        if (zAx) zAx.style.display = axisCount >= 3 ? '' : 'none';
        var zm = byId('grbl_dro_mz');
        if (zm) zm.style.display = axisCount >= 3 ? '' : 'none';
        var zw = byId('grbl_dro_wz');
        if (zw) zw.style.display = axisCount >= 3 ? '' : 'none';
      }

      function renderStatus(data) {
        state.lastStatus = data;
        renderDro(data);

        var mtX = parseFloat(data.max_travel_x || 0);
        var mtY = parseFloat(data.max_travel_y || 0);
        var mtZ = parseFloat(data.max_travel_z || 0);
        if (mtX > 0 || mtY > 0) {
          var changed = (state.maxTravel.x !== mtX || state.maxTravel.y !== mtY || state.maxTravel.z !== mtZ);
          state.maxTravel.x = mtX;
          state.maxTravel.y = mtY;
          state.maxTravel.z = mtZ;
          if (changed) {
            // Auto-fit to machine area when bounds arrive and no toolpath is loaded.
            if (!state.preview.hasPath) fitPreviewToBounds();
            drawPreview();
          }
        }

        var usbConnected = parseInt(data.usb_connected || 0, 10);
        // S3 CNC (transport selector shown): single OTG port, no 4-port hub.
        // The USB-CDC hub sections (slot dropdown, chip info, baud, reconnect)
        // are P4-only — hide them on S3.
        var isS3 = (window.GRBL_TRANSPORT_SELECTOR === true);
        // Runtime transport wins; fall back to the legacy usb_backend field
        // for status payloads that predate the runtime transport selector.
        var isUartMode = (typeof data.grbl_transport === 'string') ? (data.grbl_transport === 'uart') : (data.usb_backend === 'stub');
        var hostOn = !!data.usb_running;
        var usbColor = isS3 ? (hostOn ? (usbConnected > 0 ? '#22c55e' : '#f59e0b') : '#ef4444') : (isUartMode ? '#22c55e' : (hostOn ? (usbConnected > 0 ? '#22c55e' : '#f59e0b') : '#ef4444'));
        var usbLabel = isS3 ? (hostOn ? (usbConnected > 0 ? 'ON' : 'WAIT') : 'OFF') : (isUartMode ? 'UART' : (hostOn ? (usbConnected > 0 ? 'ON' : 'WAIT') : 'OFF'));
        var usbDetail = isS3 ? (hostOn ? String(usbConnected) : 'press USB OTG') : (isUartMode ? 'Serial2 GPIO16/17' : String(usbConnected));
        var usbEl = byId('grbl_usb');
        if (usbEl) {
          usbEl.innerHTML = '<span style="display:inline-block;padding:2px 8px;border-radius:12px;font-weight:700;font-size:12px;background:' + usbColor + ';color:#fff">' + usbLabel + '</span> ' + usbDetail;
        }
        // Update widget labels for UART mode
        var transportLbl = byId('grbl_transport_label');
        if (transportLbl) transportLbl.textContent = isS3 ? 'USB OTG' : (isUartMode ? 'UART' : 'USB Host');
        var connTitle = byId('grbl_conn_title');
        if (connTitle) connTitle.textContent = isS3 ? 'USB OTG' : (isUartMode ? 'UART' : 'USB-CDC');
        var slotLbl = byId('grbl_slot_label');
        if (slotLbl) slotLbl.textContent = isUartMode ? 'UART' : 'USB';
        var baudLbl = byId('grbl_baud_label');
        if (baudLbl) baudLbl.textContent = isUartMode ? 'UART' : 'USB-CDC';
        // S3 CNC: hide the 4-port hub sections (P4-only). Also hidden in UART mode.
        var slotSection = byId('grbl_usb_slot_section');
        if (slotSection) slotSection.style.display = (isS3 || isUartMode) ? 'none' : '';
        // Hide reconnect button on S3 (host toggled via USB OTG button) and in UART mode.
        var reconnectBtn = byId('btn_usb_restart');
        if (reconnectBtn) reconnectBtn.style.display = (isS3 || isUartMode) ? 'none' : '';
        // Hide USB baud section on S3 (single OTG port at fixed 115200).
        var baudSection = byId('grbl_usb_baud_section');
        if (baudSection) baudSection.style.display = isS3 ? 'none' : '';
        // Update slot dropdown label for UART
        var chipInfoEl = byId('grbl_usb_chip_info');
        if (chipInfoEl && isUartMode) chipInfoEl.textContent = 'UART (Serial2) 115200 8N1';
        // Sync transport toggle from the real host state (no persistence:
        // server is source of truth after every poll/reboot).
        setTransportToggleState(hostOn);
        // Sync alarm-resume panel (assisted "restart from line N").
        var resumeSec = byId('grbl_resume_section');
        if (resumeSec) {
          var showResume = !!data.resume_available;
          resumeSec.style.display = showResume ? '' : 'none';
          if (showResume) {
            var rInfo = byId('grbl_resume_info');
            if (rInfo) {
              var rFile = String(data.resume_file || '?');
              var rLine = parseInt(data.resume_line || 0, 10);
              var rTot = parseInt(data.resume_total || 0, 10);
              var rAlarm = String(data.resume_alarm || '');
              rInfo.textContent = rFile + ' — ' +
                wtSafe('grblResumeLineLbl', 'line') + ' ' + String(rLine + 1) + '/' + String(rTot) +
                (rAlarm ? ' — ' + rAlarm : '') +
                (data.resume_busy ? ' (' + wtSafe('grblResumeWorking', 'working…') + ')' : '');
            }
            var rErr = byId('grbl_resume_error');
            if (rErr) {
              var rErrMsg = String(data.resume_error || '');
              rErr.style.display = rErrMsg ? '' : 'none';
              rErr.textContent = rErrMsg;
            }
            var rBtn = byId('btn_grbl_resume_alarm');
            if (rBtn) rBtn.disabled = !!data.resume_busy;
          }
        }
        var elState = byId('grbl_state');
        if (elState) elState.textContent = data.state || '--';

        var total = parseInt(data.lines_total || 0, 10);
        var sent = parseInt(data.lines_sent || 0, 10);
        var acked = parseInt(data.lines_acked || 0, 10);
        var pct = 0;
        if (total > 0) pct = Math.floor((acked * 100) / total);
        var ackGap = sent - acked;
        if (ackGap < 0 || !isFinite(ackGap)) ackGap = 0;
        var elProgress = byId('grbl_progress');
        if (elProgress) elProgress.textContent = String(acked) + '/' + String(total) + ' (' + String(pct) + '%) · ' + wtSafe('grblProgressSentLbl', 'sent') + ' ' + String(sent) + ' · ' + wtSafe('grblProgressGapLbl', 'gap') + ' ' + String(ackGap);
        var elSent = byId('grbl_sent');
        if (elSent) elSent.textContent = String(sent);
        var elReceived = byId('grbl_received');
        if (elReceived) elReceived.textContent = String(acked);
        var elInflight = byId('grbl_inflight');
        if (elInflight) elInflight.textContent = String(data.bytes_in_flight || 0);
        // Planner buffer bar
        var elPlanner = byId('grbl_planner');
        if (elPlanner) {
            var pFree = parseInt(data.planner_buffer_free || 0, 10);
            var rFree = parseInt(data.rx_buffer_free || 0, 10);
            elPlanner.textContent = 'P' + String(pFree) + ' / R' + String(rFree);
        }
        var plannerFill = byId('grbl_planner_fill');
        if (plannerFill) {
            var pTotal = 100; // GRBL planner slots
            var pUsed = pTotal - Math.max(0, parseInt(data.planner_buffer_free || 0, 10));
            plannerFill.style.width = String(Math.min(100, Math.round((pUsed / pTotal) * 100))) + '%';
        }
        var elErrors = byId('grbl_errors');
        if (elErrors) elErrors.textContent = String(data.error_count || 0);
        var curCpm = parseInt(data.cmd_per_min || 0, 10);
        var avgCpm = parseInt(data.avg_cmd_per_min || 0, 10);
        var elCpm = byId('grbl_cmdpm');
        if (elCpm) elCpm.textContent = String(curCpm) + ' / ' + String(avgCpm);
        var cpmFill = byId('grbl_cmdpm_fill');
        if (cpmFill) {
          var cpmMax = Math.max(1, avgCpm > 0 ? Math.round(avgCpm * 1.25) : 60);
          cpmFill.style.width = String(Math.min(100, Math.round((curCpm / cpmMax) * 100))) + '%';
        }
        var elEta = byId('grbl_eta');
        if (elEta) elEta.textContent = fmtEta(data.eta_sec);
        state.preview.jobTotal = parseInt(data.lines_total || 0, 10);
        state.preview.jobAcked = parseInt(data.lines_acked || 0, 10);
        var jsObj = data.job_session || {};
        if (jsObj.start_wpos_valid) {
          state.preview.jobStartWPos = { x: parseFloat(jsObj.start_wx || 0), y: parseFloat(jsObj.start_wy || 0), z: parseFloat(jsObj.start_wz || 0) };
        } else {
          state.preview.jobStartWPos = null;
        }
        var pollOutstanding = parseInt(data.poll_outstanding || 0, 10);
        if (isNaN(pollOutstanding) || pollOutstanding < 0) pollOutstanding = 0;
        var pollHealth = String(data.poll_health || '').toLowerCase();
        var probeKnown = !!data.machine_status_known;
        var probeTriggered = !!data.probe_triggered;
        var probeStateEl = byId('grbl_probe_state');
        var probeSignalEl = byId('grbl_probe_signal');
        var probeOpenDraw = byId('grbl_probe_draw_open');
        var probeTouchDraw = byId('grbl_probe_draw_touch');
        if (probeStateEl) {
          if (!probeKnown) {
            probeStateEl.innerHTML = '<span style="display:inline-block;padding:2px 8px;border-radius:12px;font-weight:700;font-size:11px;background:#475569;color:#fff">--</span>';
          } else {
            probeStateEl.innerHTML = probeTriggered
              ? '<span style="display:inline-block;padding:2px 8px;border-radius:12px;font-weight:700;font-size:11px;background:#ef4444;color:#fff">1</span>'
              : '<span style="display:inline-block;padding:2px 8px;border-radius:12px;font-weight:700;font-size:11px;background:#22c55e;color:#fff">0</span>';
          }
        }
        if (probeSignalEl) {
          if (!probeKnown) probeSignalEl.textContent = '--';
          else probeSignalEl.textContent = probeTriggered ? 'LOW' : 'HIGH';
        }
        if (probeOpenDraw) {
          var openActive = probeKnown && !probeTriggered;
          probeOpenDraw.style.opacity = openActive ? '1' : '0.55';
          probeOpenDraw.style.boxShadow = openActive ? '0 0 0 2px rgba(34,197,94,0.45) inset' : 'none';
        }
        if (probeTouchDraw) {
          var touchActive = probeKnown && probeTriggered;
          probeTouchDraw.style.opacity = touchActive ? '1' : '0.55';
          probeTouchDraw.style.boxShadow = touchActive ? '0 0 0 2px rgba(239,68,68,0.45) inset' : 'none';
        }

        // ── Height Map status ──────────────────────────────────────
        var hm = data.height_map || {};
        state.probeGridPhase = hm.phase || 'idle';
        var hmStatusEl = byId('grbl_heightmap_status');
        var hmProgressEl = byId('grbl_heightmap_progress');
        var hmEnableEl = byId('hm_enabled');
        if (hmStatusEl) {
          var phaseLabels = { idle:'Idle', moving_xy:'Moving', probing:'Probing', retracting:'Retracting', complete:'Complete', cancelled:'Cancelled', error:'Error' };
          var phase = phaseLabels[hm.phase] || hm.phase || 'Idle';
          var total = parseInt(hm.total || 0, 10);
          var completed = parseInt(hm.completed || 0, 10);
          hmStatusEl.textContent = 'Phase: ' + phase + ' | ' + completed + '/' + total;
        }
        if (hmProgressEl && hm.phase === 'probing') {
          hmProgressEl.textContent = 'Probing point ' + (parseInt(hm.completed || 0, 10) + 1) + ' of ' + (parseInt(hm.total || 0, 10));
        } else if (hmProgressEl) {
          hmProgressEl.textContent = '';
        }
        if (hmEnableEl) {
          hmEnableEl.checked = !!hm.enabled;
        }

        renderRecentErrors(data.recent_errors || []);

        var badge = byId('grbl_machine_badge');
        if (badge) {
          var st = String(data.machine_state || data.state || '').toLowerCase();
          badge.textContent = data.machine_state || data.state || '--';
          badge.className = '';
          if (st.indexOf('idle')>=0) badge.className = 'st-idle';
          else if (st.indexOf('run')>=0) badge.className = 'st-run';
          else if (st.indexOf('hold')>=0||st.indexOf('door')>=0) badge.className = 'st-hold';
          else if (st.indexOf('alarm')>=0) badge.className = 'st-alarm';
          else if (st.indexOf('home')>=0||st.indexOf('sleep')>=0) badge.className = 'st-home';
          else badge.className = 'st-offline';
        }
        var toolChangeEl = byId('grbl_tool_change_badge');
        if (toolChangeEl) {
          if (data.tool_change_active) {
            toolChangeEl.style.display = '';
            toolChangeEl.textContent = wtSafe('grblToolChangeLbl', '\u{1F527} Tool Change');
          } else {
            toolChangeEl.style.display = 'none';
          }
        }
        var activeFile = byId('grbl_active_file');
        if (activeFile) activeFile.textContent = (data.file_path && data.file_path.length) ? data.file_path : '--';

        var previewActive = !!(data && (data.job_active || (total > 0 && acked > 0 && acked < total)));
        if (previewActive) {
          state.preview._jobWasActive = true;
          var ack = parseInt(data.lines_acked || 0, 10);
          if (!isNaN(ack) && ack >= 0 && ack !== state.preview.currentLine) {
            state.preview.currentLine = ack;
            if (!state.preview.scrubEnabled) state.preview.scrubLine = ack;
            updatePreviewInfo('');
            drawPreview();
          }
        } else if (state.preview._jobWasActive) {
          // Job ended — restore full view so all segments are visible again.
          state.preview._jobWasActive = false;
          if (state.preview.renderMode === 'runline') {
            state.preview.renderMode = 'all';
            }
          state.preview.currentLine = 0;
          state.preview.scrubLine = 0;
          updatePreviewInfo('');
          drawPreview();
        }

        renderSlots(data.usb_slots || []);
        syncSelectedUsbBaud(data.usb_slots || []);
        ingestConsoleEvents(data.console_events || []);
        applyStatusSnapshot(data);

      }

      function refreshStatus(silent) {
        var url = '/api/grbl/status?last_id=' + encodeURIComponent(String(state.lastConsoleEventId || 0));
        return apiGet(url)
          .then(function(data) {
            if (!data || data.success !== true) {
              if (!silent) showAlert('&#10060; ' + wtSafe('grblStatusFail', 'Failed to fetch status.'));
              return;
            }
            renderStatus(data);
          })
          .catch(function(err) {
            if (!silent) showAlert('&#10060; ' + wtSafe('grblStatusConnFail', 'Connection to GRBL lost.'));
          });
      }

      // postActionOnce: same as postAction but does NOT auto-retry on auth_token_invalid.
      // Used as the retry target to prevent infinite retry loops.
)GRBL"




