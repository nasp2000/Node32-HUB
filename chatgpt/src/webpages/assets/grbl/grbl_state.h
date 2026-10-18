R"GRBL(

      var GRBL_LOCAL_WORKSPACE_MAX_BYTES = 20 * 1024 * 1024;
      var GRBL_PREVIEW_MAX_BYTES = 50 * 1024 * 1024;
      var GRBL_PREVIEW_SOFT_WARN_BYTES = 10 * 1024 * 1024;
      var GRBL_LIGHT_PREVIEW_MAX_SCAN_BYTES = 20 * 1024 * 1024;
      var GRBL_LIGHT_PREVIEW_MAX_CONTOURS = 2000;
      var GRBL_WIREFRAME_MAX_SCAN_BYTES = 512 * 1024;

      var _isEsp32Browser = (typeof navigator !== 'undefined') && (/ESP32|ESP8266/i.test(navigator.userAgent));

      // Low-resource devices (Raspberry Pi, small SBCs, old phones): fewer CPU
      // cores / less RAM. The parser uses smaller budgets and yields more often
      // so the page stays responsive with less memory.
      function _isLowResourceDevice() {
        if (_isEsp32Browser) return true;
        if (typeof navigator === 'undefined') return false;
        var hc = navigator.hardwareConcurrency || 0;
        var dm = navigator.deviceMemory || 0;
        return (hc > 0 && hc <= 4) || (dm > 0 && dm < 4);
      }
      var _resizeTimer = 0;
      var _previewCanvas = null;
      function _getPreviewCanvas() {
        if (!_previewCanvas) _previewCanvas = byId('grbl_preview_canvas');
        return _previewCanvas;
      }

      var state = {
        pollTimer: 0,
        ws: null,
        wsReconnectTimer: 0,
        macroCount: 4,
        macroStorageKey: 'node32_grbl_macros_v1',
        sessionStorageKey: 'node32_grbl_tab_session_v1',
        uiPrefsKey: 'node32_grbl_ui_prefs_v1',
        formPrefsKey: 'node32_grbl_form_prefs_v1',
        usbBaudPrefsKey: 'node32_grbl_usb_baud_prefs_v1',
        dockPrefsKey: 'node32_grbl_dock_layout_v1',
        sessionId: '',
        controlToken: '',
        lastStatus: null,
        lastConsoleEventId: 0,
        rehydrated: false,
        lastAlarmText: '',
        lastHoldReason: '',
        lastControlMode: '',
        canWrite: false,
        confirmCritical: true,
        keyboardJogEnabled: true,
        consolePaused: false,
        consoleAutoScroll: true,
        workspaceText: '',
        workspacePath: '',
        sdUploadActive: false,
        sdFiles: [],
        sdRoot: '',
        sdDir: '',
        psramFile: null,
        selectedSdPath: '',
        selectedSdType: 'file',
        sdSortMode: 'date',
        preview: {
          points: [],
          segments: [],
          lightPreview: null,
          parseJobId: 0,
          parseBusy: false,
          parseProgress: 0,
          parseTruncated: false,
          scale: 1,
          fitScale: 1,
          panX: 0,
          panY: 0,
          viewMode: 'top',
          previewMode: 'auto',
          showCut: true,
          showRapid: true,
          showEngrave: true,
          hasPath: false,
          currentLine: 0,
          maxLine: 0,
          scrubEnabled: false,
          scrubLine: 0,
          scrubPlaying: false,
          scrubSpeed: 10,
          renderMode: 'all',
          jobTotal: 0,
          jobAcked: 0,
          _jobWasActive: false,
          startInfo: null,
          jobStartWPos: null,
          dragging: false,
          lastX: 0,
          lastY: 0,
          contourSelectEnabled: false,
          contourDisabled: [],
          contourUndoStack: [],
          contourAutoChildren: {},
          contourHighlight: -1,
        },
        maxTravel: { x: 0, y: 0, z: 0 },
        formPrefs: {},
        usbBaudPrefs: {},
        formPersistTimer: 0,
        _lastWorker: null,
        _lastWorkerApi: null
      };

      var grblFormPersistExclude = {
        grbl_confirm_critical: true,
        grbl_keyboard_jog: true,
        grbl_usb_baud: true,
        grbl_step_xy_sel: true,
        grbl_step_z_sel: true,
        grbl_sd_upload_file: true,
        // Overrides are never persisted in the browser: they reset to 100% on
        // reload so a previously applied value cannot silently survive a page
        // refresh (the machine is the source of truth after a reboot).
        grbl_feed_override: true,
        grbl_spindle_override: true
        // Transport toggle: never persisted (session-only, no localStorage).
      };

      function byId(id) {
        return document.getElementById(id);
      }

      function qsa(sel, root) {
        return Array.prototype.slice.call((root || document).querySelectorAll(sel));
      }

      function wt(k, fb) {
        try {
          if (typeof window._t === 'function') return window._t(k, fb);
          var raw = localStorage.getItem('ui_lang_cache');
          if (raw) {
            var parsed = JSON.parse(raw);
            if (parsed && parsed[k] !== undefined) return parsed[k];
          }
        } catch(err) {}
        return fb !== undefined ? fb : k;
      }
      function wtSafe(key, fallback) {
        if (typeof wt === 'function') return wt(key, fallback);
        return fallback;
      }

      function showAlert(msg) {
        if (typeof customAlert === 'function') customAlert(msg);
      }

      function bindClick(id, handler) {
        var el = byId(id);
        if (!el) return;
        el.addEventListener('click', handler);
      }

      function bindInput(id, eventName, handler) {
        var el = byId(id);
        if (!el) return;
        el.addEventListener(eventName, handler);
      }
)GRBL"
