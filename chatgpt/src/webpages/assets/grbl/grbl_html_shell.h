R"GRBL(

    <div class='grbl-rotate-warning' style='display:none;position:fixed;inset:0;z-index:99999;background:#0f172a;color:#e2e8f0;align-items:center;justify-content:center;flex-direction:column;gap:16px;font-size:18px;text-align:center;padding:24px'>
      <div style='font-size:48px'>\u{1F4F1}</div>
      <div data-i18n='grblRotateHint'>Please rotate the device to portrait mode to use the GcodeSender.</div>
    </div>
    <div class='grbl-controller-content'>
    <div class='grbl-banner'>
      <h1 data-i18n='grblPageTitle'>&#128736;&#65039; GcodeSender</h1>
      <p data-i18n='grblBannerSubtitle'>This page runs on the local computer (browser)</p>
    </div>

    <div class='grbl-ctrlbar'>

      <div class='grbl-cb-main'>
        <div class='grbl-cb-row'>
          <button id='btn_quick_status' class='btn' data-i18n='grblQuickStatusBtn' data-i18n-title='grblTipStatus' title='Request machine status'>&#10067; Status</button>
          <button id='btn_quick_unlock' class='btn' data-i18n='grblQuickUnlockBtn' data-i18n-title='grblTipUnlock' title='Unlock after alarm'>&#128275; Unlock</button>
          <button id='btn_quick_hold' class='btn' data-i18n='grblQuickHoldBtn' data-i18n-title='grblTipHold' title='Pause running job'>&#9208;&#65039; Hold</button>
          <button id='btn_quick_resume' class='btn' data-i18n='grblQuickResumeBtn' data-i18n-title='grblTipResume' title='Resume paused job'>&#9654;&#65039; Resume</button>
          <button id='btn_quick_cancel' class='btn' data-i18n='grblCancelBtn' data-i18n-title='grblTipCancel' title='Cancel running job'>&#9209;&#65039; Cancel</button>
          <button id='btn_jog_home' class='btn' data-i18n='grblJogHomeBtn' data-i18n-title='grblTipHome' title='Home all axes'>HOME</button>
          <button id='btn_quick_zero_wcs' class='btn' data-i18n='grblQuickZeroWcsBtn' data-i18n-title='grblTipZeroWcs' title='Zero work coordinates'>&#127919; Zero WCS</button>
          <button id='btn_quick_reset' class='btn' data-i18n='grblQuickResetBtn' data-i18n-title='grblTipReset' title='Reset GRBL controller'>&#8635; Reset</button>
        </div>
      </div>
      <div class='grbl-cb-actions'>
        <button id='btn_toggle_dock' class='btn' data-i18n-title='grblTipPanels' title='Toggle side panels'>&#9776; Panels</button>
        <button id='btn_toggle_fullscreen' class='btn' data-i18n='grblActionFullscreenBtn' data-i18n-title='grblTipFullscreen' title='Toggle fullscreen mode'>&#9974; Fullscreen</button>
      </div>
    </div>

    <!-- Widget Settings Side Panel -->
    <div id='grbl_lp_trigger'></div>
    <div id='grbl_left_panel'>
      <div class='glp-header'>
        <button id='glp_close_btn' class='glp-close' title='Close' data-i18n-title='grblTipClosePanel'>&#8592;</button>
        <div><h2>WIDGET LAYOUT</h2><span class='glp-sub'>Arrange your panels</span></div>
      </div>
      <div class='glp-body'>
        <div class='glp-section' id='glp_widget_list_section'>
          <div class='glp-section-title'>PANELS</div>
          <div id='glp_widget_list'></div>
        </div>
        <div class='glp-section'>
          <div class='glp-section-title' data-i18n='grblBehaviorTitle'>Behavior</div>
          <div class='glp-toggle-row'><label for='grbl_confirm_critical' data-i18n='grblConfirmCriticalLbl'>&#9888;&#65039; Confirm critical actions</label><input type='checkbox' id='grbl_confirm_critical' checked data-i18n-title='grblTipConfirmCritical' title='Ask before critical actions'></div>
          <div class='glp-toggle-row'><label for='grbl_keyboard_jog' data-i18n='grblKeyboardJogLbl'>&#9000;&#65039; Keyboard jog</label><input type='checkbox' id='grbl_keyboard_jog' checked data-i18n-title='grblTipKeyboardJog' title='Enable arrow key jogging'></div>
        </div>
      </div>
    </div>

    <div class='cncjs-shell-grid'>
       <div id='grbl_left_dock' class='grbl-side-column grbl-left-column grbl-widget-dock'>

        <div id='wid_job' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblSectionJobTitle'>&#9654;&#65039; G-code Job</h3>
          <div id='grbl_resume_section' style='display:none;margin-bottom:10px;padding:10px;border-radius:6px;border:2px solid #f59e0b;background:#1c1206'>
            <div style='font-size:13px;font-weight:700;color:#fbbf24;margin-bottom:4px' data-i18n='grblResumeTitle'>&#9888;&#65039; Resume after alarm</div>
            <div class='note' id='grbl_resume_info' style='margin-bottom:6px'>--</div>
            <label style='display:flex;gap:8px;align-items:center;font-size:12px;color:#e0e7ff;margin-bottom:8px'><input type='checkbox' id='grbl_resume_confirm' style='width:16px;height:16px'> <span data-i18n='grblResumeConfirmLbl'>Machine clear, spindle safe — unlock, home and resume</span></label>
            <button id='btn_grbl_resume_alarm' class='btn' style='width:100%;background:#b45309;border:1px solid #f59e0b' data-i18n='grblResumeAlarmBtn' data-i18n-title='grblTipResumeAlarm' title='Unlock, home and resume from the interrupted line'>&#9654;&#65039; Resume from line</button>
            <div class='note' id='grbl_resume_error' style='display:none;margin-top:6px;color:#f87171'></div>
          </div>
           <div id='grbl_sd_section' class='grbl-job-widget'>
            <div class='grbl-job-row-2'>
              <button id='btn_grbl_sd_refresh' class='btn' data-i18n='grblSdRefreshBtn' data-i18n-title='grblTipSdRefresh' title='Refresh SD file list'>&#128260; Refresh SD List</button>
              <button id='btn_grbl_sd_upload' class='btn' data-i18n='grblSdUploadBtn' data-i18n-title='grblTipSdUpload' title='Upload file to SD card'>&#11014;&#65039; Upload to SD</button>
            </div>
            <div class='grbl-job-row-2'>
              <button id='btn_grbl_sd_mkdir' class='btn' data-i18n='grblSdMkdirBtn' data-i18n-title='grblTipSdMkdir' title='Create new folder on SD'>\u{1F4C1} New Folder</button>
              <button id='btn_grbl_sd_sort' class='btn' data-i18n='grblSdSortNameBtn' data-i18n-title='grblTipSdSort' title='Toggle sort order'>&#128451;&#65039; Sort: Name</button>
            </div>
            <div class='grbl-job-row-2'>
              <button id='btn_grbl_sd_delete_file' class='btn' style='background:#991b1b' data-i18n='grblSdDeleteFileBtn' data-i18n-title='grblTipSdDelete' title='Delete selected file'>&#128465;&#65039; Delete</button>
              <button id='btn_grbl_sd_deselect' class='btn' data-i18n='grblSdDeleteBtn' data-i18n-title='grblTipSdDeselect' title='Clear file selection'>✖️ Deselect</button>
            </div>
            <input id='grbl_sd_upload_file' type='file' accept='.nc,.gcode,.tap,.txt,.gc,.ngc,.cnc,.code,.cg,.cgo,.cgode' style='display:none'>
            <div id='grbl_sd_upload_progress' class='grbl-sd-upload-progress'>
              <div id='grbl_sd_upload_text' class='grbl-sd-upload-text' data-i18n='grblSdUploadInitial'>Upload 0%</div>
              <div class='grbl-sd-upload-track'><span id='grbl_sd_upload_fill' class='grbl-sd-upload-fill'></span></div>
            </div>
            <div class='note' id='grbl_sd_selected' style='margin-top:6px' data-i18n='grblSdSelectedNone'>No SD file selected.</div>
            <div class='note' id='grbl_sd_dir' style='margin-top:2px' data-i18n='grblSdDirUnknown'>Directory: --</div>
            <input id='grbl_sd_filter' class='grbl-sd-filter' type='text' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px' placeholder='Filter SD files...' data-i18n-placeholder='grblSdFilterPlaceholder' data-i18n-title='grblTipSdFilter' title='Filter files by name'>
            <div id='grbl_sd_browser' class='grbl-sd-browser'></div>
            <div class='grbl-job-actions'>
              <button id='btn_grbl_sd_load' class='btn' data-i18n='grblSdLoadWorkspaceBtn' data-i18n-title='grblTipSdLoad' title='Load file into 3D preview'>\u{1F4E5} Load into Workspace</button>
              <button id='btn_stream_start' class='btn' data-i18n='grblStartBtn' data-i18n-title='grblTipSdStream' title='Start streaming to machine'>&#9654;&#65039; Start File</button>
            </div>
          </div>
          <div id='grbl_psram_section' style='display:none;margin-top:14px;padding-top:12px;border-top:1px solid var(--g-bdr)'>
            <div style='font-size:13px;font-weight:600;margin-bottom:8px;color:#10b981' data-i18n='grblPsramTitle'>⬆️ PSRAM Upload (no SD)</div>
            <div class='grbl-job-row-2'>
              <input id='grbl_psram_upload_file' type='file' accept='.nc,.gcode,.tap,.txt,.gc,.ngc,.cnc,.code,.cg,.cgo,.cgode' style='display:none'>
              <button id='btn_grbl_psram_upload' class='btn' data-i18n='grblPsramUploadBtn' data-i18n-title='grblTipPsramUpload' title='Upload file to PSRAM'>\u{1F4E4} Upload to PSRAM</button>
              <button id='btn_grbl_psram_clear' class='btn' style='background:#991b1b' data-i18n='grblPsramClearBtn' data-i18n-title='grblTipPsramClear' title='Clear PSRAM file'>\u{1F5D1}️ Clear PSRAM</button>
            </div>
            <div id='grbl_psram_upload_progress' class='grbl-sd-upload-progress'>
              <div id='grbl_psram_upload_text' class='grbl-sd-upload-text' data-i18n='grblPsramUploadInitial'>Upload 0%</div>
              <div class='grbl-sd-upload-track'><span id='grbl_psram_upload_fill' class='grbl-sd-upload-fill'></span></div>
            </div>
            <div id='grbl_psram_status' class='note' style='margin-top:6px' data-i18n='grblPsramStatusNone'>No file in PSRAM.</div>
            <div class='grbl-job-actions'>
              <button id='btn_grbl_psram_load' class='btn' data-i18n='grblPsramLoadWorkspaceBtn' data-i18n-title='grblTipPsramLoad' title='Load PSRAM into preview'>\u{1F4E5} Load into Workspace (PSRAM)</button>
              <button id='btn_psram_stream_start' class='btn' data-i18n='grblPsramStartBtn' data-i18n-title='grblTipPsramStart' title='Stream PSRAM file to machine'>▶️ Start PSRAM File</button>
            </div>
          </div>
        </div>

        <div id='wid_connection' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblSectionConnectionTitle'>&#128268; <span id='grbl_conn_title'>USB-CDC</span> Connection</h3>
          <div style='display:grid;grid-template-columns:1fr;gap:8px'>
            <div id='grbl_usb_slot_section'>
              <label for='usb_slot' data-i18n='grblSlotLbl'><span id='grbl_slot_label'>USB</span> Slot</label>
              <select id='usb_slot' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px' data-i18n-title='grblTipUsbSlot' title='Select USB port slot'></select>
              <div id='grbl_usb_chip_info' style='font-size:11px;opacity:.82;margin-top:4px'></div>
            </div>
            <div class='grbl-usb-actions'>
              <button id='btn_refresh_status' class='btn' data-i18n='grblRefreshBtn' data-i18n-title='grblTipRefreshStatus' title='Poll machine status now'>&#128260; Refresh Status</button>
              <button id='btn_usb_restart' class='btn' style='background:#7c3aed' data-i18n='grblUsbRestartBtn' data-i18n-title='grblTipUsbRestart' title='Reconnect USB host'>&#128268; Reconnect USB</button>
              <button id='grbl_transport_toggle' class='btn' style='width:100%;padding:10px;border-radius:6px;font-size:14px;font-weight:bold;background:#1e293b;color:#94a3b8;border:2px solid #334155' data-i18n-title='grblTransportNote' title='Session-only: turns off on reboot. Plug OTG adapter then press to start.'>&#128268; USB OTG: OFF</button>
            </div>
            <div id='grbl_usb_baud_section' style='margin-top:10px;padding-top:8px;border-top:1px solid var(--g-bdr)'>
              <div style='font-size:11px;opacity:.82;margin-bottom:6px;font-style:italic' data-i18n='grblUsbBaudAdapterNote'>USB-UART adapteur baudrate</div>
              <div style='display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:end'>
              <div>
                <label for='grbl_usb_baud' data-i18n='grblUsbBaudLbl'><span id='grbl_baud_label'>USB-CDC</span> Baud</label>
                <select id='grbl_usb_baud' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px' data-i18n-title='grblTipUsbBaud' title='Set adapter baudrate'>
                  <option value='9600'>9600</option>
                  <option value='19200'>19200</option>
                  <option value='38400'>38400</option>
                  <option value='57600'>57600</option>
                  <option value='115200' selected>115200</option>
                  <option value='230400'>230400</option>
                </select>
              </div>
              <button id='btn_usb_apply_baud' class='btn' style='min-width:132px;height:46px;margin-bottom:1px' data-i18n='grblUsbApplyBaudBtn' data-i18n-title='grblTipUsbApplyBaud' title='Apply baudrate setting'>&#9881;&#65039; Apply</button>
            </div>
            </div>
          </div>
          <div class='cncjs-metrics grbl-conn-metrics' style='grid-template-columns:repeat(2,minmax(76px,1fr));margin-top:8px'>
            <div class='cncjs-metric'><strong><span id='grbl_transport_label'>USB Host</span></strong><div id='grbl_usb'>--</div></div>
            <div class='cncjs-metric'><strong data-i18n='grblStateLbl'>State</strong><div id='grbl_state'>--</div></div>
            <div class='cncjs-metric'><strong data-i18n='grblProgressLbl'>Progress</strong><div id='grbl_progress'>--</div></div>
            <div class='cncjs-metric'><strong data-i18n='grblErrorsLbl'>Errors</strong><div id='grbl_errors'>--</div></div>
            <div class='cncjs-metric'><strong data-i18n='grblCmdPerMinLbl'>Cmd/min</strong><div id='grbl_cmdpm'>--</div><div style='margin-top:3px;height:3px;border-radius:2px;background:#1e3a5f;overflow:hidden'><div id='grbl_cmdpm_fill' style='height:100%;width:0%;background:#3b82f6;transition:width 0.4s ease'></div></div></div>
            <div class='cncjs-metric'><strong data-i18n='grblPlannerLbl'>Planner</strong><div id='grbl_planner'>--</div><div style='margin-top:3px;height:3px;border-radius:2px;background:#1e3a5f;overflow:hidden'><div id='grbl_planner_fill' style='height:100%;width:0%;background:#3b82f6;transition:width 0.4s ease'></div></div></div>
            <div class='cncjs-metric'><strong data-i18n='grblInflightLbl'>Inflight</strong><div id='grbl_inflight'>--</div></div>
            <div class='cncjs-metric'><strong data-i18n='grblEtaLbl'>ETA</strong><div id='grbl_eta'>--</div></div>
          </div>
        </div>

        <div id='wid_override' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblOverrideTitle'>&#128200; Override</h3>
          <div style='display:grid;gap:10px'>
            <div>
              <label style='display:block;font-size:11px;color:var(--g-txt2);font-weight:700;margin-bottom:4px' data-i18n='grblFeedOverrideLbl'>Feed rate</label>
              <div style='display:grid;grid-template-columns:36px 1fr 36px 44px;gap:4px;align-items:center'>
                <button id='btn_feed_override_dec' class='btn override-step' style='font-size:16px;padding:0' data-i18n-title='grblTipFeedDec' title='Decrease feed rate'>-1</button>
                <input id='grbl_feed_override' type='range' min='10' max='200' value='100' step='1' style='width:100%' data-i18n-title='grblTipFeedSlider' title='Adjust feed rate'>
                <button id='btn_feed_override_inc' class='btn override-step' style='font-size:16px;padding:0' data-i18n-title='grblTipFeedInc' title='Increase feed rate'>+1</button>
                <strong id='grbl_feed_override_val' style='font-size:14px'>100%</strong>
              </div>
            </div>
            <div>
              <label style='display:block;font-size:11px;color:var(--g-txt2);font-weight:700;margin-bottom:4px' data-i18n='grblSpindleOverrideLbl'>Spindle/Laser</label>
              <div style='display:grid;grid-template-columns:36px 1fr 36px 44px;gap:4px;align-items:center'>
                <button id='btn_spindle_override_dec' class='btn override-step' style='font-size:16px;padding:0' data-i18n-title='grblTipSpinDec' title='Decrease spindle speed'>-1</button>
                <input id='grbl_spindle_override' type='range' min='10' max='200' value='100' step='1' style='width:100%' data-i18n-title='grblTipSpinSlider' title='Adjust spindle speed'>
                <button id='btn_spindle_override_inc' class='btn override-step' style='font-size:16px;padding:0' data-i18n-title='grblTipSpinInc' title='Increase spindle speed'>+1</button>
                <strong id='grbl_spindle_override_val' style='font-size:14px'>100%</strong>
              </div>
            </div>
            <button id='btn_apply_override' class='btn' data-i18n='grblApplyOverrideBtn' data-i18n-title='grblTipApplyOverride' title='Apply override values'>&#9889; Apply Override</button>
          </div>
        </div>

        <div id='wid_recent_errors' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblRecentErrorsTitle'>&#9888;&#65039; Recent Errors</h3>
          <div id='grbl_recent_errors' class='grbl-recent-errors'>--</div>
        </div>
      </div>

      <div class='grbl-center-column'>
        <div id='wid_workspace' class='grbl-section-block grbl-run-focus' style='padding:6px 14px 12px'>
        <h2 class='grbl-section-title' style='margin:0 0 8px 0' data-i18n='grblWorkspaceTitle'>&#129513; Workspace</h2>
        <div class='cncjs-workspace-header'>
          <div class='cncjs-status-pill'><span data-i18n='grblMachineStateLbl'>Machine State</span>: <strong id='grbl_machine_badge'>--</strong></div>
          <div class='cncjs-status-pill'><span data-i18n='grblActiveFileLbl'>Active File</span>: <span id='grbl_active_file'>--</span></div>
          <div class='cncjs-status-pill grbl-mode-pill'><span data-i18n='grblModeLbl'>Mode</span>: <strong id='grbl_cmd_mode'>MANUAL</strong></div>
          <div id='grbl_tool_change_badge' class='cncjs-status-pill' style='display:none;border-color:#f59e0b;background:rgba(245,158,11,0.15)'>&#128295; <span data-i18n='grblToolChangeLbl'>Tool Change</span></div>
        </div>

        <div id='grbl_dro_panel' class='grbl-dro-panel'>
          <div class='grbl-dro-grid'>
            <div class='grbl-dro-cell hdr'></div>
            <div class='grbl-dro-cell hdr' data-i18n='grblDroMachineHdr' data-i18n-title='grblDroMachineHdrTitle' title='Machine'>Machine</div>
            <div class='grbl-dro-cell hdr' data-i18n='grblDroWorkHdr' data-i18n-title='grblDroWorkHdrTitle' title='Work'>Work</div>
            <div class='grbl-dro-cell ax'>X</div>
            <div id='grbl_dro_mx' class='grbl-dro-cell val'>--</div>
            <div id='grbl_dro_wx' class='grbl-dro-cell val'>--</div>
            <div class='grbl-dro-cell ax'>Y</div>
            <div id='grbl_dro_my' class='grbl-dro-cell val'>--</div>
            <div id='grbl_dro_wy' class='grbl-dro-cell val'>--</div>
            <div class='grbl-dro-cell ax' id='grbl_dro_z_ax'>Z</div>
            <div id='grbl_dro_mz' class='grbl-dro-cell val'>--</div>
            <div id='grbl_dro_wz' class='grbl-dro-cell val'>--</div>
          </div>
        </div>
        <div class='grbl-dro-actions'>
          <select id='grbl_wcs_sel' style='font-size:10px;padding:2px 4px;border-radius:4px;border:1px solid var(--g-bdr);background:var(--g-sel);color:var(--g-text);font-weight:700' data-i18n-title='grblTipWcsSel' title='Select work coordinate system'>
            <option value='G54'>G54</option>
            <option value='G55'>G55</option>
            <option value='G56'>G56</option>
            <option value='G57'>G57</option>
            <option value='G58'>G58</option>
            <option value='G59'>G59</option>
          </select>
          <button id='btn_zero_xy' class='btn' data-i18n='grblZeroXyBtn' data-i18n-title='grblTipZeroXY' title='Set X and Y to zero'>Zero XY</button>
          <button id='btn_zero_z' class='btn' data-i18n='grblZeroZBtn' data-i18n-title='grblTipZeroZ' title='Set Z to zero'>Zero Z</button>
        </div>

         <div class='cncjs-widget grbl-2d-widget'>
           <div class='grbl-2d-toolbar'>
             <h3 data-i18n='grblPreviewTitle'>&#128202; Preview</h3>
             <div class='grbl-2d-actions'>
               <select id='grbl_preview_mode' data-i18n-title='grblTipPreviewMode' title='Preview render mode'>
                 <option value='auto' selected data-i18n='grblPreviewModeAuto'>Auto</option>
                 <option value='frame' data-i18n='grblPreviewModeFrame'>Frame</option>
               </select>
               <select id='grbl_view_mode' data-i18n-title='grblTipViewMode' title='Camera view'>
                 <option value='top' selected data-i18n='grblViewModeTop'>Top (XY)</option>
                 <option value='iso' data-i18n='grblViewModeIso'>Isometric</option>
                 <option value='side' data-i18n='grblViewModeSide'>Side (XZ)</option>
               </select>
               <label class='grbl-2d-filter' data-i18n-title='grblTipShowCut' title='Show/hide cut lines'><input type='checkbox' id='grbl_show_cut' checked><span>Cut</span></label>
               <label class='grbl-2d-filter' data-i18n-title='grblTipShowRapid' title='Show/hide rapids'><input type='checkbox' id='grbl_show_rapid' checked><span>Rapid</span></label>
               <label class='grbl-2d-filter' data-i18n-title='grblTipShowEngrave' title='Show/hide engraving'><input type='checkbox' id='grbl_show_engrave' checked><span>Engrave</span></label>
                <button id='btn_contour_select' class='btn' data-i18n='grblContourSelectBtn' data-i18n-title='grblContourSelectTip' title='Toggle contour selection mode'>Select</button>
                <button id='btn_contour_undo' class='btn' data-i18n='grblContourUndoBtn' data-i18n-title='grblContourUndoTip' title='Undo last deselection'>Undo</button>
                <span id='contour_select_info' class='grbl-2d-filter-info'></span>
             </div>
           </div>
         <div class='grbl-2d-wrap'>
           <canvas id='grbl_preview_canvas' class='grbl-2d-canvas' tabindex='0' role='img' aria-label='Preview'></canvas>
           <div id='grbl_preview_info' class='grbl-2d-overlay' data-i18n='grblPreviewEmpty'>No path loaded</div>
           <div id='grbl_preview_hud' class='grbl-2d-hud'></div>
            <div id='grbl_preview_legend' class='grbl-2d-legend'><span><span class='grbl-2d-dot cut'></span>Cut</span><span><span class='grbl-2d-dot engrave'></span>Engrave</span><span><span class='grbl-2d-dot rapid'></span>Rapid</span></div>
          </div>
          <div class='grbl-2d-scrub'>
            <button id='btn_3d_scrub_play' class='btn' data-i18n='grblSimPlayBtn' data-i18n-title='grblTipSimPlay' title='Play/pause path simulation'>&#9654; Sim</button>
            <button id='btn_3d_scrub_reset' class='btn' data-i18n='grblPreviewResetBtn' data-i18n-title='grblTipPreviewReset' title='Reset preview: show full path and reset the view'>&#8635;</button>
            <input type='range' id='grbl_3d_line_scrub' min='0' max='1' value='0' disabled data-i18n-title='grblTipScrubLine' title='Simulation progress'>
            <span id='grbl_3d_scrub_info' class='line-info'>L0/0</span>
            <label data-i18n-title='grblTipScrubEnable' title='Enable simulation scrubber'>
              <input type='checkbox' id='grbl_3d_scrub_enable' data-i18n-title='grblTipScrubEnable' title='Enable simulation scrubber'><span data-i18n='grblScrubEnableLbl'>Sim</span>
            </label>
            <select id='grbl_3d_scrub_speed' data-i18n-title='grblTipScrubSpeed' title='Simulation speed'>
              <option value='1'>1x</option>
              <option value='2'>2x</option>
              <option value='5'>5x</option>
              <option value='10' selected>10x</option>
              <option value='20'>20x</option>
              <option value='40'>40x</option>
            </select>
          </div>
        </div>

        </div>

        <div id='wid_console' class='grbl-section-block grbl-run-focus grbl-center-console' style='padding:6px 14px 12px'>
          <h3 data-i18n='grblTerminalTitle'>&#128220; Console</h3>
          <div class='grbl-console-toolbar'>
            <button id='btn_console_clear' class='btn' data-i18n='grblConsoleClearBtn' data-i18n-title='grblTipConsoleClear' title='Clear console output'>&#129529; Clear</button>
            <button id='btn_console_scroll_toggle' class='btn' data-i18n='grblConsoleScrollBtn' data-i18n-title='grblTipConsoleAutoScroll' title='Toggle auto-scroll'>&#8595; Auto-scroll</button>
            <button id='btn_console_fullscreen' class='btn' data-i18n='grblConsoleFullscreenBtn' data-i18n-title='grblTipConsoleExpand' title='Expand console view'>&#9974; Expand</button>
          </div>
          <pre id='grbl_console' tabindex='0' aria-live='polite' aria-relevant='additions' style='margin:0;min-height:260px;max-height:420px;overflow-x:auto;overflow-y:scroll;padding:10px;border:1px solid var(--g-bdr);border-radius:8px;background:var(--g-bg);color:var(--g-txt2);font-size:10px;line-height:1.35;white-space:pre;word-break:normal;text-align:left;font-family:Consolas,monospace'></pre>
          <div class='grbl-console-send-row'>
            <input id='grbl_console_cmd' type='text' style='width:100%;padding:8px;border-radius:6px;border:1px solid var(--g-bdr);background:var(--g-bg);color:var(--g-text);font-size:13px' placeholder='Ex: ?' data-i18n-title='grblTipConsole' title='Type GRBL command here'>
            <button id='btn_console_send' class='btn' data-i18n='grblSendBtn' data-i18n-title='grblTipSend' title='Send command to GRBL'>&#128228; Send Line</button>
          </div>
          <div style='display:flex;justify-content:space-between;align-items:center;margin-top:3px'>
            <button id='btn_console_scroll_end' class='btn' style='display:none;font-size:11px;padding:3px 10px' data-i18n='grblConsoleScrollEndBtn' data-i18n-title='grblTipConsoleScrollEnd' title='Jump to latest output'>&#8595; Scroll to end</button>
          </div>
        </div>
      </div>

      <div class='grbl-side-column grbl-right-column'>
      <div id='grbl_right_dock' class='grbl-widget-dock'>

        <div id='wid_jog' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblSectionJogTitle'>&#128377;&#65039; Jog & Shortcuts</h3>
          <div class='cncjs-jog-fields' style='justify-content:center'>
            <div class='cncjs-jog-field' style='flex:0 1 auto;min-width:120px'>
              <label for='grbl_feed' data-i18n='grblFeedLbl'>Feed (mm/min)</label>
              <select id='grbl_feed' data-i18n-title='grblTipFeed' title='Jog feed rate'>
                <option value='100'>100</option>
                <option value='200'>200</option>
                <option value='500'>500</option>
                <option value='800' selected>800</option>
                <option value='1000'>1000</option>
                <option value='1500'>1500</option>
                <option value='2000'>2000</option>
                <option value='3000'>3000</option>
                <option value='5000'>5000</option>
                <option value='8000'>8000</option>
                <option value='10000'>10000</option>
                <option value='15000'>15000</option>
                <option value='20000'>20000</option>
              </select>
            </div>
          </div>
          <div class='cncjs-jog-layout'>
            <div class='cncjs-jog-cross'>
              <button id='btn_jog_xy_nw' class='btn cncjs-jog-diag' title='X- Y+' data-i18n-title='grblTipJogXYNW'>&#8598;</button>
              <button id='btn_jog_y_pos' class='btn' data-i18n='grblJogYPosBtn' data-i18n-title='grblTipJogYPlus' title='Jog Y axis positive'>Y+</button>
              <button id='btn_jog_xy_ne' class='btn cncjs-jog-diag' title='X+ Y+' data-i18n-title='grblTipJogXYNE'>&#8599;</button>
              <button id='btn_jog_x_neg' class='btn' data-i18n='grblJogXNegBtn' data-i18n-title='grblTipJogXMinus' title='Jog X axis negative'>X-</button>
              <button id='btn_jog_xy_center' class='btn cncjs-jog-home' title='XY Center' data-i18n-title='grblTipJogXYCenter'>X/Y</button>
              <button id='btn_jog_x_pos' class='btn' data-i18n='grblJogXPosBtn' data-i18n-title='grblTipJogXPlus' title='Jog X axis positive'>X+</button>
              <button id='btn_jog_xy_sw' class='btn cncjs-jog-diag' title='X- Y-' data-i18n-title='grblTipJogXYSW'>&#8601;</button>
              <button id='btn_jog_y_neg' class='btn' data-i18n='grblJogYNegBtn' data-i18n-title='grblTipJogYMinus' title='Jog Y axis negative'>Y-</button>
              <button id='btn_jog_xy_se' class='btn cncjs-jog-diag' title='X+ Y-' data-i18n-title='grblTipJogXYSE'>&#8600;</button>
            </div>
            <div class='cncjs-jog-z-stack'>
              <button id='btn_jog_z_pos' class='btn' data-i18n='grblJogZPosBtn' data-i18n-title='grblTipJogZPlus' title='Jog Z axis positive'>Z+</button>
              <div class='cncjs-jog-z-spacer' aria-hidden='true'></div>
              <button id='btn_jog_z_neg' class='btn' data-i18n='grblJogZNegBtn' data-i18n-title='grblTipJogZMinus' title='Jog Z axis negative'>Z-</button>
            </div>
          </div>
          <div class='cncjs-jog-step-selectors'>
            <div>
              <label for='grbl_step_xy_sel' data-i18n='grblJogIncXyLbl'>XY Increment (mm)</label>
              <div class='cncjs-step-mini'>
                <select id='grbl_step_xy_sel' data-i18n-title='grblTipStepXY' title='XY jog step distance'>
                  <option value='1.0'>1.0</option>
                  <option value='2.0'>2.0</option>
                  <option value='5.0'>5.0</option>
                  <option value='10.0' selected>10.0</option>
                  <option value='20.0'>20.0</option>
                  <option value='50.0'>50.0</option>
                  <option value='100.0'>100.0</option>
                  <option value='200.0'>200.0</option>
                </select>
              </div>
            </div>
            <div>
              <label for='grbl_step_z_sel' data-i18n='grblJogIncZLbl'>Z Increment (mm)</label>
              <div class='cncjs-step-mini'>
                <select id='grbl_step_z_sel' data-i18n-title='grblTipStepZ' title='Z jog step distance'>
                  <option value='0.1' selected>0.1</option>
                  <option value='0.2'>0.2</option>
                  <option value='0.3'>0.3</option>
                  <option value='0.5'>0.5</option>
                  <option value='0.8'>0.8</option>
                  <option value='1.0'>1.0</option>
                  <option value='1.5'>1.5</option>
                  <option value='2.0'>2.0</option>
                </select>
              </div>
            </div>
          </div>
        </div>

        <div id='wid_frame' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblSectionFrameTitle'>&#11034; Frame</h3>
          <div class='grbl-frame-grid'>
            <div class='note' data-i18n='grblFrameNote'>Use the loaded local G-code to mark the work area rectangle.</div>
            <div class='grbl-frame-params'>
              <div class='grbl-frame-field'>
                <label for='grbl_frame_power' data-i18n='grblFramePowerLbl'>Power (%)</label>
                <select id='grbl_frame_power' class='grbl-frame-input' data-i18n-title='grblTipFramePower' title='Frame laser power'>
                  <option value='1'>1%</option>
                  <option value='2'>2%</option>
                  <option value='3'>3%</option>
                  <option value='5' selected>5%</option>
                  <option value='10'>10%</option>
                  <option value='15'>15%</option>
                  <option value='20'>20%</option>
                  <option value='30'>30%</option>
                  <option value='50'>50%</option>
                  <option value='75'>75%</option>
                  <option value='100'>100%</option>
                </select>
              </div>
              <div class='grbl-frame-field'>
                <label for='grbl_frame_feed' data-i18n='grblFrameSpeedLbl'>Speed (mm/min)</label>
                <select id='grbl_frame_feed' class='grbl-frame-input' data-i18n-title='grblTipFrameSpeed' title='Frame movement speed'>
                  <option value='100'>100</option>
                  <option value='200'>200</option>
                  <option value='300'>300</option>
                  <option value='500'>500</option>
                  <option value='800'>800</option>
                  <option value='1000'>1000</option>
                  <option value='1200' selected>1200</option>
                  <option value='1500'>1500</option>
                  <option value='2000'>2000</option>
                  <option value='3000'>3000</option>
                </select>
              </div>
            </div>
            <div class='grbl-frame-actions'>
              <button id='btn_frame_run' class='btn' data-i18n='grblFrameRunBtn' data-i18n-title='grblTipFrameRun' title='Run work area frame'>&#11034; Frame 5%</button>
            </div>
            <div id='grbl_frame_bbox' class='grbl-frame-bbox'>--</div>
          </div>
        </div>

        <div id='wid_macros' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblSectionMacroTitle'>&#9889; Macros</h3>
          <div class='note' style='margin-bottom:8px' data-i18n='grblMacroNote'>Macros are stored in this device browser (localStorage).</div>
            <div class='cncjs-macro-grid'>
              <div class='cncjs-macro-card'>
                <input id='macro_name_1' type='text' placeholder='Name' data-i18n-placeholder='grblMacroNameLbl' data-i18n-title='grblTipMacroName' title='Macro display name'>
                <input id='macro_cmd_1' type='text' placeholder='Command' data-i18n-placeholder='grblMacroCmdLbl' data-i18n-title='grblTipMacroCmd' title='Command to execute'>
                <button id='btn_macro_run_1' class='btn' data-i18n='grblMacroRunBtn' data-i18n-title='grblTipMacroRun' title='Execute this macro'>&#9654;&#65039; Run</button>
              </div>
              <div class='cncjs-macro-card'>
                <input id='macro_name_2' type='text' placeholder='Name' data-i18n-placeholder='grblMacroNameLbl' data-i18n-title='grblTipMacroName' title='Macro display name'>
                <input id='macro_cmd_2' type='text' placeholder='Command' data-i18n-placeholder='grblMacroCmdLbl' data-i18n-title='grblTipMacroCmd' title='Command to execute'>
                <button id='btn_macro_run_2' class='btn' data-i18n='grblMacroRunBtn' data-i18n-title='grblTipMacroRun' title='Execute this macro'>&#9654;&#65039; Run</button>
              </div>
              <div class='cncjs-macro-card'>
                <input id='macro_name_3' type='text' placeholder='Name' data-i18n-placeholder='grblMacroNameLbl' data-i18n-title='grblTipMacroName' title='Macro display name'>
                <input id='macro_cmd_3' type='text' placeholder='Command' data-i18n-placeholder='grblMacroCmdLbl' data-i18n-title='grblTipMacroCmd' title='Command to execute'>
                <button id='btn_macro_run_3' class='btn' data-i18n='grblMacroRunBtn' data-i18n-title='grblTipMacroRun' title='Execute this macro'>&#9654;&#65039; Run</button>
              </div>
              <div class='cncjs-macro-card'>
                <input id='macro_name_4' type='text' placeholder='Name' data-i18n-placeholder='grblMacroNameLbl' data-i18n-title='grblTipMacroName' title='Macro display name'>
                <input id='macro_cmd_4' type='text' placeholder='Command' data-i18n-placeholder='grblMacroCmdLbl' data-i18n-title='grblTipMacroCmd' title='Command to execute'>
                <button id='btn_macro_run_4' class='btn' data-i18n='grblMacroRunBtn' data-i18n-title='grblTipMacroRun' title='Execute this macro'>&#9654;&#65039; Run</button>
              </div>
              <div class='cncjs-macro-save-row'>
                <button id='btn_macro_save' class='btn' data-i18n='grblMacroSaveBtn' data-i18n-title='grblTipMacroSave' title='Save all macros'>&#128190; Save Macros</button>
              </div>
            </div>
        </div>

        <div id='wid_probe' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblProbeTitle'>&#129514; Z Probe</h3>
          <div class='cncjs-metrics grbl-conn-metrics' style='grid-template-columns:repeat(2,minmax(76px,1fr));margin-top:6px'>
            <div class='cncjs-metric'>
              <strong data-i18n='grblProbeStateLbl'>State</strong>
              <div id='grbl_probe_state'>--</div>
            </div>
            <div class='cncjs-metric'>
              <strong data-i18n='grblProbeSignalLbl'>Signal</strong>
              <div id='grbl_probe_signal'>--</div>
            </div>
          </div>
          <div style='display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px'>
            <div id='grbl_probe_draw_open' style='border:1px solid #334155;border-radius:8px;padding:6px;background:#020617;opacity:0.55;transition:opacity 0.2s ease, box-shadow 0.2s ease'>
              <svg viewBox='0 0 120 78' width='100%' height='64' role='img' aria-label='Z probe open'>
                <line x1='30' y1='8' x2='30' y2='36' stroke='#60a5fa' stroke-width='4'/>
                <line x1='30' y1='36' x2='56' y2='36' stroke='#60a5fa' stroke-width='4'/>
                <line x1='60' y1='42' x2='60' y2='64' stroke='#e2e8f0' stroke-width='5'/>
                <rect x='14' y='64' width='92' height='8' rx='3' fill='#334155'/>
                <circle cx='96' cy='20' r='8' fill='#22c55e'/>
              </svg>
              <div style='text-align:center;font-size:11px;opacity:.9' data-i18n='grblProbeLegendOpen'>Open</div>
            </div>
            <div id='grbl_probe_draw_touch' style='border:1px solid #334155;border-radius:8px;padding:6px;background:#020617;opacity:0.55;transition:opacity 0.2s ease, box-shadow 0.2s ease'>
              <svg viewBox='0 0 120 78' width='100%' height='64' role='img' aria-label='Z probe touch'>
                <line x1='30' y1='8' x2='30' y2='36' stroke='#60a5fa' stroke-width='4'/>
                <line x1='30' y1='36' x2='60' y2='36' stroke='#60a5fa' stroke-width='4'/>
                <line x1='60' y1='36' x2='60' y2='64' stroke='#f87171' stroke-width='5'/>
                <rect x='14' y='64' width='92' height='8' rx='3' fill='#7f1d1d'/>
                <circle cx='96' cy='20' r='8' fill='#ef4444'/>
              </svg>
              <div style='text-align:center;font-size:11px;opacity:.9' data-i18n='grblProbeLegendTouch'>Touch</div>
            </div>
          </div>
          <div style='display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;margin-top:8px'>
            <div>
              <label for='grbl_probe_plate' data-i18n='grblProbePlateLbl'>Plate thickness (mm)</label>
              <input id='grbl_probe_plate' type='number' min='0.01' max='50' step='0.01' value='20.00' style='width:100%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:12px' data-i18n-title='grblTipProbePlate' title='Thickness of touch plate'>
            </div>
            <div>
              <label for='grbl_probe_retract' data-i18n='grblProbeRetractLbl'>Retract (mm)</label>
              <input id='grbl_probe_retract' type='number' min='0.2' max='20' step='0.1' value='2.0' style='width:100%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:12px' data-i18n-title='grblTipProbeRetract' title='Retract distance after probe'>
            </div>
            <div>
              <label for='grbl_probe_fast_feed' data-i18n='grblProbeFastFeedLbl'>Fast feed (mm/min)</label>
              <input id='grbl_probe_fast_feed' type='number' min='20' max='2000' step='1' value='250' style='width:100%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:12px' data-i18n-title='grblTipProbeFastFeed' title='Fast probe speed'>
            </div>
            <div>
              <label for='grbl_probe_slow_feed' data-i18n='grblProbeSlowFeedLbl'>Slow feed (mm/min)</label>
              <input id='grbl_probe_slow_feed' type='number' min='5' max='600' step='1' value='80' style='width:100%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:12px' data-i18n-title='grblTipProbeSlowFeed' title='Slow probe speed'>
            </div>
            <div>
              <label for='grbl_probe_fast_dist' data-i18n='grblProbeFastDistLbl'>Fast travel (mm)</label>
              <input id='grbl_probe_fast_dist' type='number' min='1' max='100' step='0.1' value='35.0' style='width:100%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:12px' data-i18n-title='grblTipProbeFastDist' title='Max fast probe distance'>
            </div>
            <div>
              <label for='grbl_probe_slow_dist' data-i18n='grblProbeSlowDistLbl'>Slow travel (mm)</label>
              <input id='grbl_probe_slow_dist' type='number' min='0.3' max='20' step='0.1' value='3.0' style='width:100%;padding:8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:12px' data-i18n-title='grblTipProbeSlowDist' title='Max slow probe distance'>
            </div>
          </div>
          <div class='note' style='margin-top:6px' data-i18n='grblProbeAutoHint'>Cycle: G38.2 fast, retract, G38.2 slow, sets Z0 with plate thickness and safety lift.</div>
          <div style='display:grid;grid-template-columns:1fr;gap:6px;margin-top:6px'>
            <button id='btn_probe_auto_zero' class='btn' data-i18n='grblProbeAutoBtn' data-i18n-title='grblTipProbeAutoZero' title='Run auto Z zero cycle'>\u{1F9EA} Auto Zero Z</button>
          </div>
        </div>

        <div id='wid_heightmap' class='cncjs-widget grbl-dock-widget'>
          <h3 data-i18n='grblHeightMapTitle'>&#127758; Height Map</h3>
          <div id='grbl_heightmap_status' style='font-size:11px;color:#94a3b8;margin-bottom:6px'>--</div>
          <div class='grid-params' style='display:grid;grid-template-columns:1fr 1fr;gap:4px;margin-bottom:6px'>
            <div><label for='hm_startX' style='font-size:11px'>X start</label><input id='hm_startX' type='number' min='-500' max='500' step='1' value='0' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmStartX' title='Grid start X coordinate'></div>
            <div><label for='hm_startY' style='font-size:11px'>Y start</label><input id='hm_startY' type='number' min='-500' max='500' step='1' value='0' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmStartY' title='Grid start Y coordinate'></div>
            <div><label for='hm_endX' style='font-size:11px'>X end</label><input id='hm_endX' type='number' min='-500' max='500' step='1' value='100' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmEndX' title='Grid end X coordinate'></div>
            <div><label for='hm_endY' style='font-size:11px'>Y end</label><input id='hm_endY' type='number' min='-500' max='500' step='1' value='100' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmEndY' title='Grid end Y coordinate'></div>
            <div><label for='hm_spacing' style='font-size:11px'>Spacing</label><input id='hm_spacing' type='number' min='0.5' max='100' step='0.5' value='10' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmSpacing' title='Grid point spacing'></div>
            <div><label for='hm_probeFeed' style='font-size:11px'>Feed</label><input id='hm_probeFeed' type='number' min='10' max='2000' step='1' value='50' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmFeed' title='Height map probe speed'></div>
            <div><label for='hm_probeMaxDepth' style='font-size:11px'>Max Z</label><input id='hm_probeMaxDepth' type='number' min='1' max='200' step='0.5' value='20' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmMaxZ' title='Max probe depth'></div>
            <div><label for='hm_safeZ' style='font-size:11px'>Safe Z</label><input id='hm_safeZ' type='number' min='0' max='200' step='0.5' value='5' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmSafeZ' title='Safe travel height'></div>
            <div><label for='hm_clearHeight' style='font-size:11px'>Clear Z</label><input id='hm_clearHeight' type='number' min='1' max='200' step='0.5' value='10' style='width:100%;padding:4px;border-radius:4px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:11px' data-i18n-title='grblTipHmClearZ' title='Retract height after each probe point'></div>
          </div>
          <div style='display:flex;gap:4px;margin-bottom:4px'>
            <button id='btn_hm_start' class='btn' style='flex:1;padding:6px' data-i18n='grblHmStartBtn' data-i18n-title='grblTipHmStart' title='Start height map probing'>&#9654; Start</button>
            <button id='btn_hm_cancel' class='btn' style='flex:1;padding:6px;background:#dc2626;border-color:#dc2626' data-i18n='grblHmCancelBtn' data-i18n-title='grblTipHmCancel' title='Cancel probing'>&#9632; Cancel</button>
          </div>
          <div id='grbl_heightmap_progress' style='font-size:11px;color:#60a5fa;margin-bottom:4px'></div>
          <div style='display:flex;align-items:center;gap:6px;margin-top:4px'>
            <input type='checkbox' id='hm_enabled' style='width:16px;height:16px' data-i18n-title='grblTipHmEnable' title='Enable height compensation'>
            <label for='hm_enabled' style='font-size:12px' data-i18n='grblHmEnableLbl'>Apply compensation</label>
          </div>
          <button id='btn_hm_clear' class='btn' style='width:100%;margin-top:4px;padding:6px' data-i18n='grblHmClearBtn' data-i18n-title='grblTipHmClear' title='Clear height map data'>&#128465; Clear</button>
        </div>
      </div>
      </div>
    </div>

    <script>
)GRBL"

