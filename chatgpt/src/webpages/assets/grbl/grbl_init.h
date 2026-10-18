R"GRBL(
      function bindHoldJog(btnId, jogFn) {
        var el = byId(btnId);
        if (!el) return;
        var timer = null;
        function startHold() {
          if (timer) return;
          if (!state.canWrite) return;
          jogFn();
          timer = setInterval(function() {
            if (!state.canWrite) { stopHold(); return; }
            jogFn();
          }, 240);
        }
        function stopHold() {
          if (timer) { clearInterval(timer); timer = null; }
        }
        el.addEventListener('mousedown', startHold);
        el.addEventListener('mouseup', stopHold);
        el.addEventListener('mouseleave', stopHold);
        el.addEventListener('touchstart', function(ev) {
          ev.preventDefault();
          startHold();
        }, { passive: false });
        el.addEventListener('touchend', stopHold);
        el.addEventListener('touchcancel', stopHold);
        el.addEventListener('keydown', function(ev) {
          if (ev.key === 'Enter' || ev.key === ' ' || ev.key === 'Space') {
            ev.preventDefault();
            startHold();
          }
        });
        el.addEventListener('keyup', function(ev) {
          if (ev.key === 'Enter' || ev.key === ' ' || ev.key === 'Space') {
            ev.preventDefault();
            stopHold();
          }
        });
      }

      function setTransportToggleState(on) {
        var btn = byId('grbl_transport_toggle');
        if (!btn) return;
        btn.dataset.on = on ? '1' : '0';
        btn.textContent = on ? '\u{1F50C} USB OTG: ON' : '\u{1F50C} USB OTG: OFF';
        btn.style.background = on ? '#064e3b' : '#1e293b';
        btn.style.color = on ? '#6ee7b7' : '#94a3b8';
        btn.style.borderColor = on ? '#10b981' : '#334155';
      }

      // USB OTG auto-start (S3 CNC, session-only, NO persistence): every page
      // visit starts the host — boot is always OFF. No localStorage: each
      // visit re-fires; a rejection (e.g. job active) simply stays off until
      // the next visit. Other packs remove the toggle, so this never runs.
      function autoStartOtg() {
        if (window.GRBL_TRANSPORT_SELECTOR !== true) return;
        var btn = byId('grbl_transport_toggle');
        if (!btn || btn.offsetParent === null) return;
        if (btn.dataset.on === '1') return;  // already on
        apiPost('/api/grbl/transport', { mode: 'usb' })
          .then(function(data) {
            if (data && data.success && data.grbl_transport === 'usb') {
              setTransportToggleState(true);
              showAlert(wtSafe('grblTransportUsbOn', 'USB OTG started — plug the adapter to connect GRBL'));
            }
            refreshStatus(true);
          })
          .catch(function() { refreshStatus(true); });
      }

      function bindEvents() {
        bindClick('btn_refresh_status', function() { refreshStatus(false); });
        bindClick('btn_usb_apply_baud', applyUsbLineCoding);
        bindClick('btn_usb_restart', function() {
          var btn = byId('btn_usb_restart');
          if (btn) btn.disabled = true;
          apiPost('/api/grbl/usb-restart', {})
            .then(function(data) {
              showAlert(data && data.success
                ? wtSafe('grblUsbReconnectProgress', '\u{1F50C} Reconnecting USB...')
                : wtSafe('grblUsbReconnectError', '❌ USB reconnect error'));
              setTimeout(function() { refreshStatus(true); }, 1500);
            })
            .catch(function() { showAlert(wtSafe('grblUsbReconnectLinkFail', '❌ Connection failed')); })
            .finally(function() { if (btn) btn.disabled = false; });
        });
        bindInput('usb_slot', 'change', function() {
          _manualSlot = byId('usb_slot') ? byId('usb_slot').value : '';
          syncSelectedUsbBaud((state.lastStatus && state.lastStatus.usb_slots) ? state.lastStatus.usb_slots : []);
          scheduleSaveFormPrefs();
          // Refresh chip info on slot change
          var slots = (state.lastStatus && state.lastStatus.usb_slots) ? state.lastStatus.usb_slots : [];
          var sel = byId('usb_slot');
          var chipEl = byId('grbl_usb_chip_info');
          if (chipEl && sel) {
            var val = sel.value;
            var active = null;
            for (var i = 0; i < slots.length; i++) {
              if (String(slots[i].slot) === val) { active = slots[i]; break; }
            }
            if (active && active.connected) {
              var t = 'Slot ' + active.slot;
              if (active.chip) t += ' - ' + active.chip;
              if (active.name) {
                var p = active.name.indexOf('(');
                var short = p >= 0 ? active.name.substring(p) : '';
                t += ' ' + short;
              }
              chipEl.textContent = t;
            } else {
              chipEl.textContent = '';
            }
          }
        });
        bindInput('grbl_transport_toggle', 'click', function() {
          var btn = byId('grbl_transport_toggle');
          if (!btn) return;
          var isOn = btn.dataset.on === '1';
          var mode = isOn ? 'uart' : 'usb';
          btn.disabled = true;
          apiPost('/api/grbl/transport', { mode: mode })
            .then(function(data) {
              btn.disabled = false;
              if (data && data.success) {
                if (data.starting) {
                  // Host still coming up in a background task: don't force
                  // the toggle or the preference yet; confirm after a beat
                  // and let status polls own the button state meanwhile.
                  showAlert(wtSafe('grblTransportUsbStarting', 'USB OTG starting — the button follows the host state'));
                  setTimeout(function() {
                    var st = state.lastStatus;
                    if (st && st.usb_running) {
                      setTransportToggleState(true);
                    }
                    refreshStatus(true);
                  }, 15000);
                } else {
                  var nowOn = (data.grbl_transport === 'usb');
                  setTransportToggleState(nowOn);
                  showAlert(nowOn
                    ? wtSafe('grblTransportUsbOn', 'USB OTG started — plug the adapter to connect GRBL')
                    : wtSafe('grblTransportUsbOff', 'USB OTG stopped'));
                }
                refreshStatus(true);
              } else {
                var msg = (data && data.message) ? String(data.message) : 'switch_rejected';
                if (msg === 'job_active') {
                  showAlert(wtSafe('grblTransportJobActive', 'Cannot switch transport while a job is running'));
                } else {
                  showAlert(wtSafe('grblTransportRejected', 'Transport switch rejected'));
                }
                refreshStatus(true);
              }
            })
            .catch(function() {
              btn.disabled = false;
              showAlert(wtSafe('grblTransportFail', 'Transport switch failed'));
              refreshStatus(true);
            });
        });
        bindInput('btn_grbl_resume_alarm', 'click', function() {
          var btn = byId('btn_grbl_resume_alarm');
          var chk = byId('grbl_resume_confirm');
          if (!btn) return;
          if (!chk || !chk.checked) {
            showAlert(wtSafe('grblResumeConfirmFirst', 'Tick the confirmation box first'));
            return;
          }
          btn.disabled = true;
          apiPost('/api/grbl/resume-alarm', { confirm: '1' })
            .then(function(data) {
              btn.disabled = false;
              if (data && data.success) {
                showAlert(wtSafe('grblResumeStarted', 'Resume started: unlock, home, then streaming'));
              } else {
                var msg = (data && data.message) ? String(data.message) : 'rejected';
                if (msg === 'no_resume') {
                  showAlert(wtSafe('grblResumeNone', 'Nothing to resume'));
                } else if (msg === 'resume_busy') {
                  showAlert(wtSafe('grblResumeBusyMsg', 'A resume is already running'));
                } else if (msg === 'confirm_required') {
                  showAlert(wtSafe('grblResumeConfirmFirst', 'Tick the confirmation box first'));
                } else {
                  showAlert(wtSafe('grblResumeRejected', 'Resume rejected'));
                }
              }
              refreshStatus(true);
            })
            .catch(function() {
              btn.disabled = false;
              showAlert(wtSafe('grblResumeFail', 'Resume request failed'));
              refreshStatus(true);
            });
        });
        bindClick('btn_console_send', sendManualCommand);
        bindHoldJog('btn_jog_x_neg', function() { runJog('X', -1); });
        bindHoldJog('btn_jog_x_pos', function() { runJog('X', 1); });
        bindHoldJog('btn_jog_y_neg', function() { runJog('Y', -1); });
        bindHoldJog('btn_jog_y_pos', function() { runJog('Y', 1); });
        bindHoldJog('btn_jog_xy_nw', function() { runJogXY(-1, 1); });
        bindHoldJog('btn_jog_xy_ne', function() { runJogXY(1, 1); });
        bindHoldJog('btn_jog_xy_sw', function() { runJogXY(-1, -1); });
        bindHoldJog('btn_jog_xy_se', function() { runJogXY(1, -1); });
        bindHoldJog('btn_jog_z_neg', function() { runJog('Z', -1); });
        bindHoldJog('btn_jog_z_pos', function() { runJog('Z', 1); });
        bindClick('btn_jog_xy_center', runJogXYCenter);
        bindClick('btn_jog_home', quickHome);
        bindClick('btn_quick_unlock', quickUnlock);
        bindClick('btn_quick_zero_wcs', quickZeroWcs);
        // Override sliders — moving them only updates the % label (UI is free
        // to change while a job streams). The values are pushed to GRBL ONLY
        // when the user presses Apply Override, via the /api/grbl/override
        // endpoint which uses GRBL real-time override bytes (feed 0x90/0x93/0x94,
        // spindle 0x99/0x9C/0x9D) — never M220/M221 g-code, which some firmware
        // builds reject and which would desync the file stream line/ack counting.
        var _lastSentFeed = '';
        var _lastSentSpin = '';
        function applyOverrides(attempt) {
          var feed = byId('grbl_feed_override') ? byId('grbl_feed_override').value : '100';
          var spin = byId('grbl_spindle_override') ? byId('grbl_spindle_override').value : '100';
          if (!attempt && feed === _lastSentFeed && spin === _lastSentSpin) return Promise.resolve(true);
          // Mark synchronously so a fast second Apply with the same values is deduped.
          _lastSentFeed = feed;
          _lastSentSpin = spin;
          var doPost = function() {
            // apiPost directly (no popup on transient failure) — retried once below.
            return apiPost('/api/grbl/override', { slot: selectedSlot(), feed: feed, spin: spin })
              .then(function(data) {
                if (data && data.success) {
                  logLine('OVERRIDE ' + feed + '%/' + spin + '%');
                  refreshStatus(true);
                  return true;
                }
                return false;
              })
              .catch(function() { return false; });
          };
          return doPost().then(function(ok) {
            if (ok) return true;
            // Failed — forget the "sent" mark so the user can retry, then retry once.
            _lastSentFeed = '';
            _lastSentSpin = '';
            if (attempt) {
              logLine('OVERRIDE FAILED ' + feed + '%/' + spin + '%');
              return false;
            }
            // One silent retry — the ESP32 may be busy streaming.
            return new Promise(function(resolve) {
              setTimeout(function() { resolve(applyOverrides(true)); }, 400);
            });
          });
        }
        bindInput('grbl_feed_override', 'input', function() {
          var val = byId('grbl_feed_override') ? byId('grbl_feed_override').value : '100';
          var el = byId('grbl_feed_override_val');
          if (el) el.textContent = val + '%';
        });
        bindInput('grbl_spindle_override', 'input', function() {
          var val = byId('grbl_spindle_override') ? byId('grbl_spindle_override').value : '100';
          var el = byId('grbl_spindle_override_val');
          if (el) el.textContent = val + '%';
        });
        bindClick('btn_apply_override', function() { applyOverrides(false); });
        function stepOverride(id, delta) {
          var el = byId(id);
          if (!el) return;
          var v = parseInt(el.value || '100', 10);
          if (isNaN(v)) v = 100;
          v = Math.max(10, Math.min(200, v + delta));
          el.value = String(v);
          el.dispatchEvent(new Event('input'));
        }
        bindClick('btn_feed_override_dec', function() { stepOverride('grbl_feed_override', -1); });
        bindClick('btn_feed_override_inc', function() { stepOverride('grbl_feed_override', 1); });
        bindClick('btn_spindle_override_dec', function() { stepOverride('grbl_spindle_override', -1); });
        bindClick('btn_spindle_override_inc', function() { stepOverride('grbl_spindle_override', 1); });
        bindClick('btn_zero_xy', function() { runWithCriticalConfirm('grblConfirmZeroXY', 'Zero XY now?', function() { postSend('G92 X0 Y0', 'ZERO_XY'); }); });
        bindClick('btn_zero_z', function() { runWithCriticalConfirm('grblConfirmZeroZ', 'Zero Z now?', function() { if (_hasZAxis()) postSend('G92 Z0', 'ZERO_Z'); }); });
        var _wcsDebounce = null;
        bindInput('grbl_wcs_sel', 'change', function() {
          if (_wcsDebounce) { clearTimeout(_wcsDebounce); _wcsDebounce = null; }
          var wcs = byId('grbl_wcs_sel').value || 'G54';
          _wcsDebounce = setTimeout(function() {
            _wcsDebounce = null;
            postSend(wcs, 'WCS ' + wcs);
          }, 200);
        });
        bindClick('btn_quick_reset', quickReset);
        bindClick('btn_quick_hold', pauseStream);
        bindClick('btn_quick_resume', resumeStream);
        bindClick('btn_quick_cancel', function() {
          runWithCriticalConfirm('grblConfirmCancelStream', 'Cancel running job?', cancelStream);
        });
        bindClick('btn_quick_status', quickStatus);
        bindClick('btn_probe_auto_zero', runAutoProbeZZero);
        bindClick('btn_hm_start', startProbeGrid);
        bindClick('btn_hm_cancel', cancelProbeGrid);
        bindClick('btn_hm_clear', clearHeightMap);
        var _hmEl = byId('hm_enabled');
        if (_hmEl) _hmEl.addEventListener('change', function() {
          toggleHeightMap(_hmEl.checked);
        });
        bindClick('btn_stream_start', startFileStream);
        bindClick('btn_frame_run', runFrameFromPreview);
        bindClick('btn_grbl_sd_refresh', function() { refreshSdFileList(false); });
        bindClick('btn_grbl_sd_upload', promptSdUploadFile);
        bindInput('grbl_sd_upload_file', 'change', uploadLocalFileToSd);
        bindClick('btn_grbl_sd_load', loadSelectedSdToWorkspace);
        bindClick('btn_grbl_sd_mkdir', createSdFolder);
        bindClick('btn_grbl_sd_sort', toggleSdSort);
        bindClick('btn_grbl_sd_delete_file', deleteSelectedSdFile);
        bindClick('btn_grbl_sd_deselect', clearSelectedSdPath);
        bindInput('grbl_sd_filter', 'input', renderSdBrowserList);
        bindClick('btn_grbl_psram_upload', promptPsramUpload);
        bindInput('grbl_psram_upload_file', 'change', uploadToPsram);
        bindClick('btn_grbl_psram_clear', clearPsramFile);
        bindClick('btn_grbl_psram_load', loadSelectedPsramToWorkspace);
        bindClick('btn_psram_stream_start', startPsramStream);
        bindClick('btn_macro_save', saveMacrosFromForm);
        bindClick('btn_macro_run_1', function() { runMacro(1); });
        bindClick('btn_macro_run_2', function() { runMacro(2); });
        bindClick('btn_macro_run_3', function() { runMacro(3); });
        bindClick('btn_macro_run_4', function() { runMacro(4); });
        bindClick('btn_toggle_fullscreen', toggleBrowserFullscreen);
        bindClick('btn_contour_select', _toggleContourSelect);
        bindClick('btn_contour_undo', _undoContourToggle);
        bindClick('btn_console_clear', clearConsole);
        bindClick('btn_console_fullscreen', toggleConsoleFullscreen);
        bindClick('btn_console_scroll_toggle', function() {
          state.consoleAutoScroll = state.consoleAutoScroll === false ? true : false;
          if (state.consoleAutoScroll) {
            state.consolePaused = false;
            var box = byId('grbl_console');
            if (box) box.scrollTop = box.scrollHeight;
          }
          updateConsoleScrollBtn();
        });
        bindClick('btn_console_scroll_end', function() {
          state.consolePaused = false;
          var box = byId('grbl_console');
          if (box) box.scrollTop = box.scrollHeight;
          updateConsoleScrollBtn();
        });
        var consoleBox = byId('grbl_console');
        if (consoleBox) {
          consoleBox.addEventListener('click', function() {
            var cmdInput = byId('grbl_console_cmd');
            if (cmdInput && !cmdInput.disabled) cmdInput.focus();
          });
          consoleBox.addEventListener('touchstart', function() {
            var cmdInput = byId('grbl_console_cmd');
            if (cmdInput && !cmdInput.disabled) cmdInput.focus();
          });
          consoleBox.addEventListener('scroll', function() {
            var thresh = Math.max(30, Math.round(consoleBox.clientHeight * 0.1));
            var atBottom = (consoleBox.scrollHeight - consoleBox.scrollTop - consoleBox.clientHeight) < thresh;
            state.consolePaused = !atBottom;
            updateConsoleScrollBtn();
          });
        }
        bindClick('btn_3d_scrub_play', toggleSimPlay);
        bindClick('btn_3d_scrub_reset', resetPreviewSim);
        bindClick('btn_3d_proj', function() {
          if (state.preview.mode === '2d') {
            state.preview.projection = 'ortho';
            var b2d = byId('btn_3d_proj');
            if (b2d) b2d.textContent = wtSafe('grbl3dProjBtnLocked2d', 'Projection: 2D Locked');
            updatePreviewInfo('');
            drawPreview();
            return;
          }
          state.preview.projection = (state.preview.projection === 'ortho') ? 'perspective' : 'ortho';
          var b = byId('btn_3d_proj');
          if (b) b.textContent = (state.preview.projection === 'perspective') ? wtSafe('grbl3dProjBtnPerspective', 'Projection: Perspective') : wtSafe('grbl3dProjBtnOrtho', 'Projection: Ortho');
          updatePreviewInfo('');
          drawPreview();
        });
        bindClick('btn_3d_motion', function() {
          state.preview.colorByMotion = !state.preview.colorByMotion;
          var b = byId('btn_3d_motion');
          if (b) b.textContent = state.preview.colorByMotion ? wtSafe('grbl3dMotionBtnMulti', 'Colors: G0/G1') : wtSafe('grbl3dMotionBtnSingle', 'Color: Single');
          updatePreviewInfo('');
          drawPreview();
        });
        bindInput('grbl_view_mode', 'change', function() {
          var vm = byId('grbl_view_mode').value || 'top';
          state.preview.viewMode = (vm === 'iso' || vm === 'side') ? vm : 'top';
          if (state.preview.points && state.preview.points.length > 1) fitPreviewToBounds(state.preview.points);
          drawPreview();
        });
        bindInput('grbl_show_cut', 'change', function() { state.preview.showCut = !!byId('grbl_show_cut').checked; drawPreview(); });
        bindInput('grbl_show_rapid', 'change', function() { state.preview.showRapid = !!byId('grbl_show_rapid').checked; drawPreview(); });
        bindInput('grbl_show_engrave', 'change', function() { state.preview.showEngrave = !!byId('grbl_show_engrave').checked; drawPreview(); });
        bindInput('grbl_preview_mode', 'change', function() {
          var sel = byId('grbl_preview_mode');
          var v = String(sel && sel.value ? sel.value : 'auto').toLowerCase();
          if (v !== 'auto' && v !== 'frame') v = 'auto';
          state.preview.previewMode = v;
          applyPreviewMode();
        });
        bindInput('grbl_3d_quality', 'change', function() {
          state.preview.arcQuality = byId('grbl_3d_quality').value || 'med';
          var sel = byId('grbl_preview_mode');
          if (sel) state.preview.previewMode = String(sel.value || 'auto').toLowerCase();
          applyPreviewMode();
        });
        bindInput('grbl_3d_scrub_enable', 'change', function() {
          state.preview.scrubEnabled = !!byId('grbl_3d_scrub_enable').checked;
          state.preview.scrubPlaying = false;
          if (!state.preview.scrubEnabled) {
            state.preview.scrubLine = state.preview.currentLine || 0;
          }
          updatePreviewInfo('');
          updateScrubControls();
          drawPreview();
        });
        bindInput('grbl_3d_line_scrub', 'input', function() {
          var v = parseInt(byId('grbl_3d_line_scrub').value || '0', 10);
          if (isNaN(v) || v < 0) v = 0;
          state.preview.scrubEnabled = true;
          state.preview.scrubLine = v;
          state.preview.scrubPlaying = false;
          updatePreviewInfo('');
          updateScrubControls();
          drawPreview();
        });
        bindInput('grbl_3d_scrub_speed', 'change', function() {
          var v = parseInt(byId('grbl_3d_scrub_speed').value || '10', 10);
          if (isNaN(v) || v < 1) v = 1;
          if (v > 40) v = 40;
          state.preview.scrubSpeed = v;
          scheduleSaveFormPrefs();
        });

        bindInput('grbl_confirm_critical', 'change', function() {
          state.confirmCritical = !!byId('grbl_confirm_critical').checked;
          saveUiPrefs();
        });
        bindInput('grbl_keyboard_jog', 'change', function() {
          state.keyboardJogEnabled = !!byId('grbl_keyboard_jog').checked;
          saveUiPrefs();
        });
        bindInput('grbl_step_xy_sel', 'change', function() {
          updateXyStepButtons();
          syncMainStepFromSelectors('X');
          saveUiPrefs();
        });
        bindInput('grbl_step_z_sel', 'change', function() {
          syncMainStepFromSelectors('Z');
          saveUiPrefs();
        });
        qsa('.grbl-step-xy-btn').forEach(function(btn) {
          btn.addEventListener('click', function() {
            setXyStepFromPreset(btn.getAttribute('data-step') || '');
          });
        });

        var consoleCmdInput = byId('grbl_console_cmd');
        if (consoleCmdInput) consoleCmdInput.addEventListener('keydown', function(ev) {
          if (ev.key === 'Enter') {
            ev.preventDefault();
            sendManualCommand();
          }
        });
        bindClick('btn_toggle_dock', function() {
          var grid = document.querySelector('.cncjs-shell-grid');
          if (!grid) return;
          var leftHidden = grid.classList.toggle('dock-left-hidden');
          grid.classList.toggle('dock-right-hidden', leftHidden);
          var btn = byId('btn_toggle_dock');
          if (btn) btn.textContent = leftHidden ? '\u2630 Panels' : '\u2716 Close';
          localStorage.setItem('node32_grbl_dock_hidden', leftHidden ? '1' : '');
          window.dispatchEvent(new Event('resize'));
        });
      }

      function applyPlaceholders() {
        var setPlaceholderIfExists = function(id, text) {
          var el = byId(id);
          if (el) el.placeholder = text;
        };
        setPlaceholderIfExists('grbl_console_cmd', wtSafe('grblCmdPlaceholder', 'Ex: G0 X10 Y10'));
        setPlaceholderIfExists('grbl_sd_filter', wtSafe('grblSdFilterPlaceholder', 'Filter SD files...'));
        setPlaceholderIfExists('macro_name_1', wtSafe('grblMacroNamePlaceholder', 'Macro 1'));
        setPlaceholderIfExists('macro_name_2', wtSafe('grblMacroNamePlaceholder', 'Macro 2'));
        setPlaceholderIfExists('macro_name_3', wtSafe('grblMacroNamePlaceholder', 'Macro 3'));
        setPlaceholderIfExists('macro_name_4', wtSafe('grblMacroNamePlaceholder', 'Macro 4'));
        setPlaceholderIfExists('macro_cmd_1', wtSafe('grblMacroCmdPlaceholder', 'Ex: G0 X0 Y0;M3 S5000'));
        setPlaceholderIfExists('macro_cmd_2', wtSafe('grblMacroCmdPlaceholder', 'Ex: G0 X0 Y0;M3 S5000'));
        setPlaceholderIfExists('macro_cmd_3', wtSafe('grblMacroCmdPlaceholder', 'Ex: G0 X0 Y0;M3 S5000'));
        setPlaceholderIfExists('macro_cmd_4', wtSafe('grblMacroCmdPlaceholder', 'Ex: G0 X0 Y0;M3 S5000'));
        updateFrameInfo();
      }

      function init() {
        ensureSessionId();
        // USB OTG toggle lives in the Connection widget (where P4 shows
        // Reconnect USB). S3 CNC only: on other packs the button is removed.
        // Boot is always OFF; autoStartOtg() fires on every visit and status
        // polls then sync the button with the real host state.
        var otgBtn = byId('grbl_transport_toggle');
        if (otgBtn) {
          if (window.GRBL_TRANSPORT_SELECTOR === true) {
            setTransportToggleState(false);
          } else {
            otgBtn.parentNode.removeChild(otgBtn);
          }
        }
        readUiPrefs();
        readFormPrefs();
        readUsbBaudPrefs();
        bindEvents();
        bindFormPersistenceAutoSave();
        applyFormPrefsToVisibleFields();
        restoreWidgetPrefs();
        restoreDockLayout();
        renderWidgetSidebar();
        // Restore dock collapsed state
        if (localStorage.getItem('node32_grbl_dock_hidden') === '1') {
          var grid = document.querySelector('.cncjs-shell-grid');
          if (grid) {
            grid.classList.add('dock-left-hidden', 'dock-right-hidden');
            var btn = byId('btn_toggle_dock');
            if (btn) btn.textContent = '\u2630 Panels';
          }
        }
        initLeftPanel();
        applyPlaceholders();
        if (byId('grbl_confirm_critical')) byId('grbl_confirm_critical').checked = !!state.confirmCritical;
        if (byId('grbl_keyboard_jog')) byId('grbl_keyboard_jog').checked = !!state.keyboardJogEnabled;
        updateSdPreviewLoadButton();
        loadMacrosToForm();
        refreshSdFileList(true).then(function(sdOk) {
          var sdSection = byId('grbl_sd_section');
          var psramSection = byId('grbl_psram_section');
          if (sdSection && psramSection) {
            if (sdOk === true) {
              sdSection.style.display = '';
              psramSection.style.display = 'none';
            } else {
              sdSection.style.display = 'none';
              psramSection.style.display = '';
            }
          }
        });
        refreshPsramFileStatus();
        bindPreviewInteractions();
        if (byId('grbl_view_mode')) state.preview.viewMode = (byId('grbl_view_mode').value === 'iso') ? 'iso' : 'top';
        if (byId('grbl_preview_mode')) state.preview.previewMode = byId('grbl_preview_mode').value || state.preview.previewMode;
        if (byId('grbl_3d_scrub_speed')) state.preview.scrubSpeed = parseInt(byId('grbl_3d_scrub_speed').value || '10', 10) || 10;
        if (byId('grbl_3d_quality')) state.preview.arcQuality = byId('grbl_3d_quality').value || state.preview.arcQuality;
        if (byId('grbl_show_cut')) state.preview.showCut = !!byId('grbl_show_cut').checked;
        if (byId('grbl_show_rapid')) state.preview.showRapid = !!byId('grbl_show_rapid').checked;
        if (byId('grbl_show_engrave')) state.preview.showEngrave = !!byId('grbl_show_engrave').checked;
        updateXyStepButtons();
        if (byId('btn_3d_proj')) byId('btn_3d_proj').textContent = wtSafe('grbl3dProjBtnOrtho', 'Projection: Ortho');
        if (byId('btn_3d_motion')) byId('btn_3d_motion').textContent = wtSafe('grbl3dMotionBtnMulti', 'Colors: G0/G1');
        clearPreview();
        logLine('GRBL READY');
        fetchControlToken().then(function() {
          refreshStatus(true);
          connectGrblWebSocket();
          autoStartOtg();  // S3 CNC: auto-start host if toggle was left ON
        }).catch(function(err) {
          setTimeout(function() {
            fetchControlToken().then(function() {
              refreshStatus(true);
              connectGrblWebSocket();
              autoStartOtg();
            });
          }, 2000);
        });

        // --- Focus-to-interact: prevent scroll/zoom hijacking ---
        // 3D canvas and console always accept input — no focus-to-interact gate.
        (function setupFocusToInteract() {
          var canvas = byId('grbl_preview_canvas');
          var consoleBox = byId('grbl_console');
          [canvas, consoleBox].forEach(function(el) {
            if (!el) return;
            el.style.pointerEvents = 'auto';
            el.style.touchAction = 'auto';
            if (el.tagName === 'CANVAS') {
              el.style.touchAction = 'none';
            }
          });
          // Keyboard controls for 3D canvas
          if (canvas) {
            canvas.addEventListener('keydown', function(ev) {
              var key = ev.key;
              var handled = false;
              if (key === '+' || key === '=') {
                state.preview.scale = Math.min(5000, (state.preview.scale || 1) * 1.2);
                handled = true;
              } else if (key === '-') {
                state.preview.scale = Math.max(0.25, (state.preview.scale || 1) / 1.2);
                handled = true;
              } else if (key === 'ArrowLeft') {
                state.preview.panX = (state.preview.panX || 0) - 10;
                handled = true;
              } else if (key === 'ArrowRight') {
                state.preview.panX = (state.preview.panX || 0) + 10;
                handled = true;
              } else if (key === 'ArrowUp') {
                state.preview.panY = (state.preview.panY || 0) - 10;
                handled = true;
              } else if (key === 'ArrowDown') {
                state.preview.panY = (state.preview.panY || 0) + 10;
                handled = true;
              } else if (key === 'r' || key === 'R') {
                resetPreviewView();
                handled = true;
              }
              if (handled) {
                ev.preventDefault();
                updatePreviewHud();
                drawPreview();
              }
            });
          }
        })();

        window.addEventListener('resize', function() {
          clearTimeout(_resizeTimer);
          _resizeTimer = setTimeout(drawPreview, 150);
        });

        window.addEventListener('beforeunload', function() {
          saveFormPrefsNow();
          stopGrblWebSocket();
        });

        document.addEventListener('keydown', function(ev) {
          if (ev.key === 'Escape') {
            var c = byId('wid_console');
            if (c && c.classList.contains('console-fullscreen')) {
              c.classList.remove('console-fullscreen');
              ev.preventDefault();
            }
            return;
          }

          if (isEditableTarget(ev.target)) return;
          if (!state.canWrite) return;
          if (!state.keyboardJogEnabled) return;
          if (ev.ctrlKey || ev.altKey || ev.metaKey) return;

          var key = String(ev.key || '').toLowerCase();
          var handled = false;
          if (key === 'arrowup') {
            ev.preventDefault();
            runJog('Y', 1);
            handled = true;
          } else if (key === 'arrowdown') {
            ev.preventDefault();
            runJog('Y', -1);
            handled = true;
          } else if (key === 'arrowleft') {
            ev.preventDefault();
            runJog('X', -1);
            handled = true;
          } else if (key === 'arrowright') {
            ev.preventDefault();
            runJog('X', 1);
            handled = true;
          } else if (key === 'pageup') {
            ev.preventDefault();
            runJog('Z', 1);
            handled = true;
          } else if (key === 'pagedown') {
            ev.preventDefault();
            runJog('Z', -1);
            handled = true;
          } else if (key === 'q') {
            ev.preventDefault();
            runJogXY(-1, 1);
            handled = true;
          } else if (key === 'e') {
            ev.preventDefault();
            runJogXY(1, 1);
            handled = true;
          } else if (key === 'z') {
            ev.preventDefault();
            runJogXY(-1, -1);
            handled = true;
          } else if (key === 'c') {
            ev.preventDefault();
            runJogXY(1, -1);
            handled = true;
          } else if (key === 'h') {
            ev.preventDefault();
            quickHome();
            handled = true;
          }
        });
      }

      if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
      } else {
        init();
      }
    })();
    </script>
)GRBL"





