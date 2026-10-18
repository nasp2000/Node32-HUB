R"GRBL(
      function isPersistableGrblField(el) {
        if (!el || !el.id) return false;
        var id = String(el.id);
        if (grblFormPersistExclude[id]) return false;
        if (id !== 'usb_slot' && id.indexOf('grbl_') !== 0) return false;
        if (String(el.tagName || '').toLowerCase() === 'input' && String(el.type || '').toLowerCase() === 'file') return false;
        return true;
      }

      function collectFormPrefs() {
        var out = {};
        qsa('input[id],select[id],textarea[id]').forEach(function(el) {
          if (!isPersistableGrblField(el)) return;
          var key = String(el.id);
          var type = String(el.type || '').toLowerCase();
          if (type === 'checkbox' || type === 'radio') out[key] = !!el.checked;
          else out[key] = String(el.value || '');
        });
        return out;
      }

      function saveFormPrefsNow() {
        try {
          state.formPrefs = collectFormPrefs();
          localStorage.setItem(state.formPrefsKey, JSON.stringify(state.formPrefs));
        } catch (e) {
          // Ignore localStorage failures.
        }
      }

      function scheduleSaveFormPrefs() {
        if (state.formPersistTimer) {
          window.clearTimeout(state.formPersistTimer);
          state.formPersistTimer = 0;
        }
        state.formPersistTimer = window.setTimeout(function() {
          state.formPersistTimer = 0;
          saveFormPrefsNow();
        }, 500);
      }

      function readFormPrefs() {
        var raw = '';
        try {
          raw = localStorage.getItem(state.formPrefsKey) || '';
        } catch (e) {
          raw = '';
        }
        if (!raw) {
          state.formPrefs = {};
          return;
        }
        try {
          var obj = JSON.parse(raw);
          state.formPrefs = (obj && typeof obj === 'object') ? obj : {};
        } catch (e2) {
          state.formPrefs = {};
        }
      }

      function readUsbBaudPrefs() {
        var raw = '';
        try {
          raw = localStorage.getItem(state.usbBaudPrefsKey) || '';
        } catch (e) {
          raw = '';
        }
        if (!raw) {
          state.usbBaudPrefs = {};
          return;
        }
        try {
          var obj = JSON.parse(raw);
          state.usbBaudPrefs = (obj && typeof obj === 'object') ? obj : {};
        } catch (e2) {
          state.usbBaudPrefs = {};
        }
      }

      function saveUsbBaudPrefs() {
        try {
          localStorage.setItem(state.usbBaudPrefsKey, JSON.stringify(state.usbBaudPrefs || {}));
        } catch (e) {
          // Ignore localStorage failures.
        }
      }

      function applyFormPrefsToVisibleFields() {
        if (!state.formPrefs || typeof state.formPrefs !== 'object') return;
        var keys = Object.keys(state.formPrefs);
        var i;
        for (i = 0; i < keys.length; i++) {
          var id = keys[i];
          // usb_slot is restored by renderSlots after the first status fetch, when options are available.
          if (id === 'usb_slot') continue;
          var el = byId(id);
          if (!el || !isPersistableGrblField(el)) continue;
          var type = String(el.type || '').toLowerCase();
          if (type === 'checkbox' || type === 'radio') {
            el.checked = !!state.formPrefs[id];
          } else {
            el.value = String(state.formPrefs[id] || '');
          }
        }
      }

      function bindFormPersistenceAutoSave() {
        qsa('input[id],select[id],textarea[id]').forEach(function(el) {
          if (!isPersistableGrblField(el)) return;
          var tag = String(el.tagName || '').toLowerCase();
          var type = String(el.type || '').toLowerCase();
          if (tag === 'select' || type === 'checkbox' || type === 'radio') {
            el.addEventListener('change', scheduleSaveFormPrefs);
            return;
          }
          el.addEventListener('input', scheduleSaveFormPrefs);
          el.addEventListener('change', scheduleSaveFormPrefs);
        });
      }

      function saveDockLayout() {
        var left = byId('grbl_left_dock');
        var right = byId('grbl_right_dock');
        if (!left || !right) return;
        var payload = {
          left: qsa('.grbl-dock-widget', left).map(function(el) { return el.id; }),
          right: qsa('.grbl-dock-widget', right).map(function(el) { return el.id; })
        };
        try {
          localStorage.setItem(state.dockPrefsKey, JSON.stringify(payload));
        } catch (e) {
          // Ignore localStorage failures.
        }
      }

      function restoreDockLayout() {
        var left = byId('grbl_left_dock');
        var right = byId('grbl_right_dock');
        if (!left || !right) return;

        var raw = '';
        try {
          raw = localStorage.getItem(state.dockPrefsKey) || '';
        } catch (e) {
          raw = '';
        }
        if (!raw) return;

        var parsed;
        try {
          parsed = JSON.parse(raw);
        } catch (e2) {
          return;
        }
        if (!parsed || !Array.isArray(parsed.left) || !Array.isArray(parsed.right)) return;

        var used = {};
        parsed.left.forEach(function(id) {
          var el = byId(id);
          if (!el || !el.classList.contains('grbl-dock-widget')) return;
          left.appendChild(el);
          used[id] = true;
        });
        parsed.right.forEach(function(id) {
          var el = byId(id);
          if (!el || !el.classList.contains('grbl-dock-widget')) return;
          right.appendChild(el);
          used[id] = true;
        });

        qsa('.grbl-dock-widget').forEach(function(el) {
          if (used[el.id]) return;
          if (left.children.length <= right.children.length) left.appendChild(el);
          else right.appendChild(el);
        });
      }

      // ---- WIDGET SETTINGS PANEL ----

      var widgetVisKey = 'node32_grbl_widget_vis_v1';

      function toggleWidgetVis(id, visible) {
        var el = byId(id);
        if (!el) return;
        el.style.display = visible ? '' : 'none';
        // Mirror the visibility into the sidebar rows so hidden widgets look
        // muted even before the sidebar re-renders.
        var rows = document.querySelectorAll('.glp-widget-row[data-wid="' + id + '"]');
        for (var ri = 0; ri < rows.length; ri++) {
          var nm = rows[ri].querySelector('.glp-widget-name');
          if (nm) nm.classList.toggle('muted', !visible);
        }
        saveWidgetPrefs();
      }

      function saveWidgetPrefs() {
        var vis = {};
        var ids = ['wid_connection','wid_workspace','wid_console','wid_job','wid_jog','wid_frame','wid_override','wid_macros','wid_probe','wid_heightmap','wid_recent_errors'];
        ids.forEach(function(id) {
          var el = byId(id);
          vis[id] = el ? el.style.display !== 'none' : true;
        });
        try {
          localStorage.setItem(widgetVisKey, JSON.stringify(vis));
        } catch (e) {
          // Ignore localStorage failures.
        }
      }

      function restoreWidgetPrefs() {
        var ids = ['wid_connection','wid_workspace','wid_console','wid_job','wid_jog','wid_frame','wid_override','wid_macros','wid_probe','wid_heightmap','wid_recent_errors'];
        try {
          var raw = localStorage.getItem(widgetVisKey) || '';
          if (raw) {
            var vis = JSON.parse(raw);
            ids.forEach(function(id) {
              var el = byId(id);
              var visible = (vis[id] !== false);
              if (el) el.style.display = visible ? '' : 'none';
            });
          }
        } catch (e) {
          // Ignore localStorage failures.
        }
      }

      function initLeftPanel() {
        var trigger = byId('grbl_lp_trigger');
        var panel = byId('grbl_left_panel');
        var closeBtn = byId('glp_close_btn');
        if (!trigger || !panel) return;

        var hoverTimer = null;
        trigger.addEventListener('mouseenter', function() {
          hoverTimer = setTimeout(function() {
            panel.classList.add('open');
          }, 1000);
        });
        trigger.addEventListener('mouseleave', function() {
          if (hoverTimer) { clearTimeout(hoverTimer); hoverTimer = null; }
        });
        panel.addEventListener('mouseleave', function() {
          panel.classList.remove('open');
        });

        if (closeBtn) {
          closeBtn.addEventListener('click', function() {
            panel.classList.remove('open');
          });
        }

        renderWidgetSidebar();
      }

      function renderWidgetSidebar() {
        var container = byId('glp_widget_list');
        if (!container) return;
        var leftDock = byId('grbl_left_dock');
        var rightDock = byId('grbl_right_dock');
        if (!leftDock || !rightDock) return;

        var CENTER_IDS = ['wid_workspace', 'wid_console'];
        var WIDGET_NAMES = {
          wid_job: 'G-code Job',
          wid_connection: 'USB Connection',
          wid_override: 'Override',
          wid_recent_errors: 'Recent Errors',
          wid_jog: 'Manual Jog',
          wid_frame: 'Frame (work area)',
          wid_macros: 'Macros',
          wid_probe: 'Probe',
wid_heightmap: 'Height Map',
          wid_workspace: 'Workspace (DRO + 3D)',
          wid_console: 'GRBL Console'
        };

        function widgetRowHtml(id, name, visible, showUp, showDown, showSide, otherSide) {
          var h = '<div class="glp-widget-row" data-wid="' + id + '">';
          h += '<span class="glp-widget-name' + (visible ? '' : ' muted') + '">' + name + '</span>';
          if (showUp) h += '<button class="glp-move-up" data-wid="' + id + '" title="Move up">\u25B2</button>';
          if (showDown) h += '<button class="glp-move-down" data-wid="' + id + '" title="Move down">\u25BC</button>';
          if (showSide) {
            var sideArrow = otherSide === 'right' ? '\u25B6' : '\u25C0';
            h += '<button class="glp-move-side" data-wid="' + id + '" data-side="' + otherSide + '" title="Move to ' + otherSide + '">' + sideArrow + '</button>';
          }
          if (!showUp && !showDown && !showSide) h += '<span style="display:inline-flex;width:18px;flex-shrink:0"></span>';
          h += '<input type="checkbox" ' + (visible ? 'checked' : '') + ' data-wid="' + id + '" data-i18n-title="grblTipWidgetToggle" title="Toggle widget visibility">';
          h += '</div>';
          return h;
        }

        function renderGroup(title, dockElm, side) {
          var widgets = qsa('.grbl-dock-widget', dockElm);
          if (!widgets.length) return '';
          var h = '<div class="glp-side-label">' + (side === 'left' ? 'LEFT SIDE' : 'RIGHT SIDE') + '</div>';
          widgets.forEach(function(el, idx) {
            var id = el.id;
            var name = WIDGET_NAMES[id] || id;
            var visible = el.style.display !== 'none';
            h += widgetRowHtml(id, name, visible, idx > 0, idx < widgets.length - 1, true, side === 'left' ? 'right' : 'left');
          });
          return h;
        }

        function renderCenterGroup() {
          var h = '<div class="glp-side-label">Center</div>';
          CENTER_IDS.forEach(function(id) {
            var el = byId(id);
            if (!el) return;
            var name = WIDGET_NAMES[id] || id;
            var visible = el.style.display !== 'none';
            h += widgetRowHtml(id, name, visible, false, false, false, '');
          });
          return h;
        }

        var html = renderGroup('Left side', leftDock, 'left');
        html += renderGroup('Right side', rightDock, 'right');
        html += renderCenterGroup();
        container.innerHTML = html;

        qsa('.glp-move-up', container).forEach(function(btn) {
          btn.addEventListener('click', function() { moveWidgetUp(btn.getAttribute('data-wid')); });
        });
        qsa('.glp-move-down', container).forEach(function(btn) {
          btn.addEventListener('click', function() { moveWidgetDown(btn.getAttribute('data-wid')); });
        });
        qsa('.glp-move-side', container).forEach(function(btn) {
          btn.addEventListener('click', function() { moveWidgetToSide(btn.getAttribute('data-wid'), btn.getAttribute('data-side')); });
        });
        qsa('input[type=checkbox][data-wid]', container).forEach(function(chk) {
          chk.addEventListener('change', function() {
            toggleWidgetVis(chk.getAttribute('data-wid'), chk.checked);
          });
        });
      }

      function moveWidgetUp(id) {
        var el = byId(id);
        if (!el) return;
        var parent = el.parentNode;
        if (!parent || !el.previousElementSibling) return;
        parent.insertBefore(el, el.previousElementSibling);
        saveDockLayout();
        renderWidgetSidebar();
      }

      function moveWidgetDown(id) {
        var el = byId(id);
        if (!el) return;
        var parent = el.parentNode;
        if (!parent || !el.nextElementSibling) return;
        parent.insertBefore(el.nextElementSibling, el);
        saveDockLayout();
        renderWidgetSidebar();
      }

      function moveWidgetToSide(id, targetSide) {
        var el = byId(id);
        if (!el) return;
        var target = targetSide === 'left' ? byId('grbl_left_dock') : byId('grbl_right_dock');
        if (!target) return;
        target.appendChild(el);
        saveDockLayout();
        renderWidgetSidebar();
      }

      function readUiPrefs() {
        var raw = '';
        try {
          raw = localStorage.getItem(state.uiPrefsKey) || '';
        } catch (e) {
          raw = '';
        }
        if (!raw) return;
        try {
          var obj = JSON.parse(raw);
          if (!obj) return;
          if (obj.confirmCritical === false) state.confirmCritical = false;
          if (obj.keyboardJogEnabled === false) state.keyboardJogEnabled = false;
          if (obj.jogStepXy && byId('grbl_step_xy_sel')) byId('grbl_step_xy_sel').value = String(obj.jogStepXy);
          if (obj.jogStepZ && byId('grbl_step_z_sel')) {
            byId('grbl_step_z_sel').value = String(normalizeZStepValue(parseFloat(String(obj.jogStepZ))));
          }
        } catch (e2) {
          // Ignore malformed local preference payload.
        }
      }

      function normalizeZStepValue(rawStep) {
        var allowed = [0.1, 0.2, 0.3, 0.5, 0.8, 1.0, 1.5, 2.0];
        var step = Number(rawStep || 0);
        if (isNaN(step) || step <= 0) step = 0.1;
        if (step > 2.0) step = 2.0;

        var best = allowed[0];
        var bestDist = Math.abs(step - best);
        var i;
        for (i = 1; i < allowed.length; i++) {
          var d = Math.abs(step - allowed[i]);
          if (d < bestDist) {
            best = allowed[i];
            bestDist = d;
          }
        }
        return best;
      }

      function saveUiPrefs() {
        var payload = {
          confirmCritical: state.confirmCritical,
          keyboardJogEnabled: state.keyboardJogEnabled,
          jogStepXy: byId('grbl_step_xy_sel') ? byId('grbl_step_xy_sel').value : '',
          jogStepZ: byId('grbl_step_z_sel') ? byId('grbl_step_z_sel').value : ''
        };
        try {
          localStorage.setItem(state.uiPrefsKey, JSON.stringify(payload));
        } catch (e) {
          // Ignore localStorage persistence failures.
        }
      }

      function syncMainStepFromSelectors(axis) {
        var mainStep = byId('grbl_step');
        if (!mainStep) return;
        var sel = byId(axis === 'Z' ? 'grbl_step_z_sel' : 'grbl_step_xy_sel');
        if (!sel) return;
        var step = parseFloat(sel.value || '0');
        if (isNaN(step) || step <= 0) return;
        if (axis === 'Z') {
          step = normalizeZStepValue(step);
          sel.value = String(step);
        }
        mainStep.value = String(step);
      }

      function updateXyStepButtons() {
        var sel = byId('grbl_step_xy_sel');
        var current = sel ? String(sel.value || '') : '';
        qsa('.grbl-step-xy-btn').forEach(function(btn) {
          var isOn = String(btn.getAttribute('data-step') || '') === current;
          if (isOn) btn.classList.add('is-active');
          else btn.classList.remove('is-active');
        });
      }

      function setXyStepFromPreset(rawStep) {
        var sel = byId('grbl_step_xy_sel');
        if (!sel) return;
        var step = String(rawStep || '').trim();
        if (!step) return;
        var ok = Array.prototype.some.call(sel.options, function(o) { return o.value === step; });
        if (!ok) return;
        sel.value = step;
        updateXyStepButtons();
        syncMainStepFromSelectors('X');
        saveUiPrefs();
      }

      function isEditableTarget(target) {
        if (!target || !target.tagName) return false;
        var tag = String(target.tagName || '').toLowerCase();
        if (tag === 'input' || tag === 'textarea' || tag === 'select' || tag === 'button') return true;
        if (target.isContentEditable) return true;
        return false;
      }

      function setCommandMode(modeKey) {
        var key = String(modeKey || 'manual').toLowerCase();
        var label = wtSafe('grblModeManual', 'MANUAL');
        if (key === 'queue-local') label = wtSafe('grblModeBackendQueue', 'BACKEND QUEUE');
        else if (key === 'sd-stream') label = wtSafe('grblModeSdStream', 'SD STREAM');
        else if (key === 'runtime') label = wtSafe('grblModeRuntime', 'RUNTIME');
        var el = byId('grbl_cmd_mode');
        if (el) el.textContent = label;
      }

      function deriveRuntimeMode(data) {
        if (!data || !data.job_active) return 'manual';
        var source = String(data.job_source || '').toLowerCase();
        if (source === 'local_queue') return 'queue-local';
        if (source.indexOf('sd') >= 0 || source.indexOf('file') >= 0 || source.indexOf('stream') >= 0) return 'sd-stream';
        return 'runtime';
      }

      function runWithCriticalConfirm(msgKey, fallback, onOk) {
        if (typeof onOk !== 'function') return;
        if (!state.confirmCritical) {
          onOk();
          return;
        }
        if (typeof customConfirm === 'function') {
          customConfirm(wtSafe(msgKey, fallback), function(ok) {
            if (ok) onOk();
          });
          return;
        }
        onOk();
      }

      function requireAxesAtZero(callback) {
        if (typeof callback !== 'function') return;
        var data = state.lastStatus || {};
        var wx = data.wpos_x, wy = data.wpos_y, wz = data.wpos_z;
        if (wx === undefined || wy === undefined || wz === undefined) { callback(); return; }
        var tolerance = 0.01;
        if (Math.abs(wx) > tolerance || Math.abs(wy) > tolerance || Math.abs(wz) > tolerance) {
          var msg = '⚠️ ' + wtSafe('grblAxesNotZero', 'Axes not at zero!') + ' WPos: X=' + wx.toFixed(1) + ' Y=' + wy.toFixed(1) + ' Z=' + wz.toFixed(1) + '\n\n' + wtSafe('grblAxesConfirmStart', 'Start anyway?');
          if (typeof customConfirm === 'function') {
            customConfirm(msg, function(ok) { if (ok) callback(); });
            return;
          }
        }
        callback();
      }

      function toggleBrowserFullscreen() {
        var doc = document;
        if (!doc.fullscreenElement) {
          if (doc.documentElement && doc.documentElement.requestFullscreen) {
            doc.documentElement.requestFullscreen();
          }
          return;
        }
        if (doc.exitFullscreen) doc.exitFullscreen();
      }

      function toggleConsoleFullscreen() {
        var box = byId('wid_console');
        if (!box) return;
        box.classList.toggle('console-fullscreen');
      }

      function clearConsole() {
        var box = byId('grbl_console');
        if (!box) return;
        box.innerHTML = '';
        state.consolePaused = false;
        updateConsoleScrollBtn();
      }

      function updateConsoleScrollBtn() {
        var box = byId('grbl_console');
        var btn = byId('btn_console_scroll_end');
        var toggle = byId('btn_console_scroll_toggle');
        if (!btn || !box) return;
        var atBottom = (box.scrollHeight - box.scrollTop - box.clientHeight) < 30;
        if (atBottom) { state.consolePaused = false; btn.style.display = 'none'; }
        else { btn.style.display = ''; }
        // Update toggle button appearance
        if (toggle) {
          toggle.style.opacity = state.consoleAutoScroll !== false ? '1' : '0.5';
          toggle.textContent = state.consoleAutoScroll !== false ? '\u25BC Auto-scroll ON' : '\u25B6 Auto-scroll OFF';
        }
      }

      function fmtEta(sec) {
        var s = parseInt(sec, 10);
        if (!isFinite(s) || s <= 0) return '--';
        var m = Math.floor(s / 60);
        var r = s % 60;
        return (m > 0 ? String(m) + 'm ' : '') + String(r) + 's';
      }

      function fmtLagMs(ms) {
        var n = parseInt(ms, 10);
        if (!isFinite(n) || n <= 0) return '--';
        if (n < 1000) return String(n) + 'ms';
        return (Math.round((n / 1000) * 10) / 10).toFixed(1) + 's';
      }

      function recentErrorMeta(item) {
        var sent = parseInt(item && item.line_sent, 10);
        var ack = parseInt(item && item.line_acked, 10);
        var cnt = parseInt(item && item.error_count, 10);
        if (!isFinite(sent)) sent = 0;
        if (!isFinite(ack)) ack = 0;
        if (!isFinite(cnt)) cnt = 0;
        return wtSafe('grblRecentErrorMetaSent', 'sent') + ' ' + String(sent) + ' | ' + wtSafe('grblRecentErrorMetaAck', 'ack') + ' ' + String(ack) + ' | #' + String(cnt);
      }

      function renderRecentErrors(list) {
        var host = byId('grbl_recent_errors');
        if (!host) return;

        if (!Array.isArray(list) || list.length === 0) {
          host.textContent = wtSafe('grblRecentErrorsEmpty', 'No recent errors.');
          return;
        }

        host.innerHTML = '';
        var maxItems = Math.min(6, list.length);
        var i;
        for (i = 0; i < maxItems; i++) {
          var item = list[i] || {};
          var row = document.createElement('div');
          row.className = 'grbl-recent-error-row';

          var top = document.createElement('div');
          top.className = 'top';

          var src = document.createElement('div');
          src.className = 'src';
          src.textContent = String(item.source || wtSafe('grblRecentErrorSourceUnknown', 'unknown'));
          top.appendChild(src);

          var meta = document.createElement('div');
          meta.className = 'meta';
          meta.textContent = recentErrorMeta(item);
          top.appendChild(meta);
          row.appendChild(top);

          var msg = document.createElement('div');
          msg.className = 'msg';
          msg.textContent = String(item.message || wtSafe('grblRecentErrorMessageDefault', 'error'));
          row.appendChild(msg);

          var preview = String(item.line_preview || '');
          if (preview.length > 0) {
            var pre = document.createElement('div');
            pre.className = 'preview';
            pre.textContent = preview;
            row.appendChild(pre);
          }

          host.appendChild(row);
        }
      }

      function ingestConsoleEvents(events) {
        if (!Array.isArray(events)) return;
        if (events.length === 0) return;
        var i;
        for (i = 0; i < events.length; i++) {
          var ev = events[i] || {};
          var id = parseInt(ev.id || 0, 10);
          var text = String(ev.text || '');
          if (!text) continue;
          if (isFinite(id) && id > 0) {
            if (id <= state.lastConsoleEventId) continue;
            state.lastConsoleEventId = id;
          }
          logLine(text);
        }
      }

      function logLine(msg) {
        var box = byId('grbl_console');
        if (!box) return;
        var now = new Date();
        var hh = String(now.getHours()).padStart(2, '0');
        var mm = String(now.getMinutes()).padStart(2, '0');
        var ss = String(now.getSeconds()).padStart(2, '0');
        var GRBL_CONSOLE_MAX = 4000;
        // Trim oldest lines to keep memory bounded.
        while (box.children.length >= GRBL_CONSOLE_MAX) {
          box.removeChild(box.firstChild);
        }
        var span = document.createElement('span');
        span.textContent = '[' + hh + ':' + mm + ':' + ss + '] ' + msg + '\n';
        var upper = String(msg || '').toUpperCase();
        if (upper.indexOf('ALARM') >= 0) {
          span.style.color = '#FF3D52';
          span.style.fontWeight = 'bold';
        } else if (upper.indexOf('ERROR') >= 0 || upper.indexOf('ERR ') >= 0 || upper.indexOf('HOLD') >= 0) {
          span.style.color = '#F59E0B';
        } else if (msg === 'ok' || msg === 'OK') {
          span.style.color = '#27E89A';
          span.style.opacity = '0.7';
        } else if (upper.charAt(0) === '<') {
          span.style.color = 'var(--g-txt3)';
        } else {
          span.style.color = 'var(--g-txt2)';
        }
        box.appendChild(span);
        if (state.consoleAutoScroll !== false && !state.consolePaused) box.scrollTop = box.scrollHeight;
        updateConsoleScrollBtn();
      }

      function toBody(data) {
        var p = new URLSearchParams();
        var key;
        for (key in data) {
          if (Object.prototype.hasOwnProperty.call(data, key)) p.append(key, String(data[key]));
        }
        return p.toString();
      }

      function apiGet(url) {
        var sep = (url.indexOf('?') >= 0) ? '&' : '?';
        var full = url + sep + 'sid=' + encodeURIComponent(state.sessionId || 'anon');
        return fetch(full, { cache: 'no-store' }).then(function(r) {
          if (!r.ok) throw new Error('HTTP ' + r.status);
          return r.json();
        });
      }

      function apiPost(url, payload) {
        var bodyPayload = payload || {};
        bodyPayload.sid = state.sessionId || 'anon';
        if (state.controlToken) bodyPayload.token = state.controlToken;
        return fetch(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8' },
          body: toBody(bodyPayload)
        }).then(function(r) {
          if (!r.ok) throw new Error('HTTP ' + r.status);
          return r.json();
        });
      }

      function updatePsramUploadUi(active, percent, text) {
        var box = byId('grbl_psram_upload_progress');
        var fill = byId('grbl_psram_upload_fill');
        var label = byId('grbl_psram_upload_text');
        var pct = parseInt(percent, 10);
        if (isNaN(pct) || pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        if (box) box.className = active ? 'grbl-sd-upload-progress is-active' : 'grbl-sd-upload-progress';
        if (fill) fill.style.width = String(pct) + '%';
        if (label) label.textContent = text || (wtSafe('grblPsramUploadProgress', 'Upload: ') + String(pct) + '%');
      }

      function updateSdUploadUi(active, percent, text) {
        var box = byId('grbl_sd_upload_progress');
        var fill = byId('grbl_sd_upload_fill');
        var label = byId('grbl_sd_upload_text');
        var uploadBtn = byId('btn_grbl_sd_upload');
        var pct = parseInt(percent, 10);
        if (isNaN(pct) || pct < 0) pct = 0;
        if (pct > 100) pct = 100;

        if (box) box.className = active ? 'grbl-sd-upload-progress is-active' : 'grbl-sd-upload-progress';
        if (fill) fill.style.width = String(pct) + '%';
        if (label) label.textContent = text || (wtSafe('grblSdUploadProgress', 'Upload: ') + String(pct) + '%');
        if (uploadBtn) uploadBtn.disabled = !!state.sdUploadActive;
      }

      function fetchControlToken() {
        return apiGet('/api/grbl/control-token')
          .then(function(data) {
            if (data && data.success === true && data.token) {
              state.controlToken = String(data.token);
              return true;
            }
            state.controlToken = '';
            return false;
          })
          .catch(function() {
            state.controlToken = '';
            return false;
          });
      }

      function startPollingFallback(intervalMs) {
        if (state.pollTimer) window.clearInterval(state.pollTimer);
        state.pollTimer = window.setInterval(function() {
          refreshStatus(true);
        }, intervalMs || 800);
      }

      function stopPolling() {
        if (state.pollTimer) {
          window.clearInterval(state.pollTimer);
          state.pollTimer = 0;
        }
      }

      function connectGrblWebSocket() {
        if (typeof WebSocket === 'undefined') {
          startPollingFallback(300);
          return;
        }
        if (state.ws && state.ws.readyState === WebSocket.OPEN) return;
        var proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        var url = proto + '//' + window.location.host + '/ws/grbl';
        var ws;
        try {
          ws = new WebSocket(url);
        } catch (e) {
          startPollingFallback(300);
          return;
        }
        state.ws = ws;
        if (state.wsReconnectTimer) {
          window.clearTimeout(state.wsReconnectTimer);
          state.wsReconnectTimer = 0;
        }

        ws.onopen = function() {
          logLine('WS connected');
          // Backup polling at 500ms — WS pushes every 100ms.
          startPollingFallback(500);
        };

        ws.onmessage = function(ev) {
          try {
            var msg = JSON.parse(ev.data);
            if (!msg || !msg.type) {
              if (msg && msg.success === true) renderStatus(msg);
              return;
            }
            var t = msg.type;
            if (t === 'status') {
              if (msg.success === true) renderStatus(msg);
            } else if (t === 'console') {
              var text = msg.text || '';
              if (text) logLine(text);
            } else if (t === 'production') {
              var type = msg.eventType || 'event';
              var msgText = msg.message || '';
              if (msgText) logLine('PROD ' + type + ' ' + msgText);
            }
          } catch (e) {
            // Ignore malformed WS payload.
          }
        };

        ws.onclose = function() {
          state.ws = null;
          // Switch to faster polling while WS is down
          startPollingFallback(300);
          if (state.wsReconnectTimer) window.clearTimeout(state.wsReconnectTimer);
          state.wsReconnectTimer = window.setTimeout(function() {
            connectGrblWebSocket();
          }, 3000);
        };

        ws.onerror = function() {
          ws.close();
        };
      }

      function stopGrblWebSocket() {
        stopPolling();
        if (state.wsReconnectTimer) {
          window.clearTimeout(state.wsReconnectTimer);
          state.wsReconnectTimer = 0;
        }
        if (state.ws) {
          state.ws.onclose = null;
          state.ws.close();
          state.ws = null;
        }
      }

      function ensureSessionId() {
        var token = '';
        try {
          token = sessionStorage.getItem(state.sessionStorageKey) || '';
        } catch (e) {
          token = '';
        }
        if (!token) {
          token = 'tab-' + String(Date.now()) + '-' + String(Math.floor(Math.random() * 1000000));
          try {
            sessionStorage.setItem(state.sessionStorageKey, token);
          } catch (e2) {
            // Ignore sessionStorage failures and keep in-memory token.
          }
        }
        state.sessionId = token;
      }

      function selectedSlot() {
        var el = byId('usb_slot');
        if (!el || !el.value) { var s = _manualSlot || '0'; return parseInt(s, 10) || 0; }
        var v = parseInt(el.value, 10);
        if (isNaN(v) || v < 0) return parseInt(_manualSlot || '0', 10) || 0;
        return v;
      }

      // Tracks user's last manual slot selection; reset when slots change.
      var _manualSlot = '';
      function normalizeUsbSlots(slots) {
        var list = Array.isArray(slots) ? slots.slice(0) : [];
        var count = 4;
        if (state.lastStatus && state.lastStatus.usb_slot_count !== undefined) {
          var n = parseInt(state.lastStatus.usb_slot_count || 0, 10);
          if (!isNaN(n) && n > 0 && n < 16) count = n;
        }

        var seen = {};
        var out = [];
        var i;
        for (i = 0; i < list.length; i++) {
          var src = list[i] || {};
          var slotNo = parseInt(src.slot, 10);
          if (isNaN(slotNo) || slotNo < 0) continue;
          seen[String(slotNo)] = true;
          out.push(src);
          if (slotNo + 1 > count && slotNo < 16) count = slotNo + 1;
        }

        for (i = 0; i < count; i++) {
          if (seen[String(i)]) continue;
          out.push({ slot: i, connected: false, rx: 0, tx: 0, baud: 0, name: '' });
        }
        out.sort(function(a, b) { return (parseInt(a.slot, 10) || 0) - (parseInt(b.slot, 10) || 0); });
        return out;
      }

      function renderSlots(slots) {
        var sel = byId('usb_slot');
        if (!sel) return;
        slots = normalizeUsbSlots(slots);
        if (!slots.length) return;

        var slotCount = slots.length;
        var connectedCount = 0;
        var firstConnected = '';
        var i;

        for (i = 0; i < slots.length; i++) {
          if (slots[i] && slots[i].connected) {
            connectedCount++;
            if (!firstConnected) firstConnected = String(slots[i].slot);
          }
        }

        // Rebuild all options — never filter out slots so the user always sees status.
        sel.innerHTML = '';
        for (i = 0; i < slots.length; i++) {
          var slot = slots[i];
          var slotValue = String(slot.slot);
          var label = 'Slot ' + slot.slot;
          var jp = 'JP HOST';
          if (slot.connected) {
            label += ' - ON ' + jp;
            if (slot.chip) label += ' ' + slot.chip;
            if (slot.baud) label += ' (' + slot.baud + ')';
          } else {
            label += ' - OFF ' + jp;
          }
          var option = document.createElement('option');
          option.value = slotValue;
          option.textContent = label;
          sel.appendChild(option);
        }

        // Determine which slot to select.
        // Priority: 1) user's manual override (_manualSlot), 2) firmware active slot (if connected),
        //           3) localStorage preference, 4) single connected slot,
        //           5) first connected, 6) first slot.
        var chosen = '';
        // Priority 1: user manual override
        if (_manualSlot && Array.prototype.some.call(sel.options, function(o) { return o.value === _manualSlot; })) {
          chosen = _manualSlot;
        }
        // Priority 2: firmware active slot (only if its slot is connected)
        if (!chosen && state.lastStatus && state.lastStatus.usb_slot !== undefined && state.lastStatus.usb_slot !== null) {
          var fwSlot = String(state.lastStatus.usb_slot);
          if (Array.prototype.some.call(sel.options, function(o) { return o.value === fwSlot; })) {
            var fwConnected = false;
            for (var si = 0; si < slots.length; si++) {
              if (String(slots[si].slot) === fwSlot && slots[si].connected) { fwConnected = true; break; }
            }
            if (fwConnected || slotCount === 1) chosen = fwSlot;
          }
        }
        // Priority 3: localStorage preference
        if (!chosen && state.formPrefs && state.formPrefs.usb_slot !== undefined && state.formPrefs.usb_slot !== null) {
          var pref = String(state.formPrefs.usb_slot);
          if (Array.prototype.some.call(sel.options, function(o) { return o.value === pref; })) {
            chosen = pref;
          }
        }
        if (!chosen && slotCount === 1) {
          chosen = String(slots[0].slot);
        } else if (!chosen && connectedCount === 1 && firstConnected) {
          chosen = firstConnected;
        } else if (!chosen && firstConnected) {
          chosen = firstConnected;
        } else if (!chosen) {
          chosen = String(slots[0].slot);
        }
        sel.value = chosen;

        // Update chip info below the slot selector
        var chipEl = byId('grbl_usb_chip_info');
        if (chipEl) {
          var selVal = sel.value;
          var activeSlot = null;
          for (i = 0; i < slots.length; i++) {
            if (String(slots[i].slot) === selVal) { activeSlot = slots[i]; break; }
          }
          if (activeSlot && activeSlot.connected) {
            var chipText = 'Slot ' + activeSlot.slot;
            if (activeSlot.chip) chipText += ' - ' + activeSlot.chip;
            if (activeSlot.name) {
              var short = activeSlot.name;
              var p = short.indexOf('(');
              if (p >= 0) short = short.substring(p);
              chipText += ' ' + short;
            }
            chipEl.textContent = chipText;
          } else {
            chipEl.textContent = '';
          }
        }
      }

      function getUsbSlotSnapshot(slots, wantedSlot) {
        if (!slots || !slots.length) return null;
        var slotNo = parseInt(wantedSlot, 10);
        if (isNaN(slotNo) || slotNo < 0) slotNo = 0;
        var i;
        for (i = 0; i < slots.length; i++) {
          if (parseInt(slots[i].slot, 10) === slotNo) {
            return slots[i];
          }
        }
        return null;
      }

      function getUsbSlotInfo(slots, wantedSlot) {
        var active = getUsbSlotSnapshot(slots, wantedSlot);
        var i;
        if (active && active.connected) return active;
        if (!slots || !slots.length) return active;
        for (i = 0; i < slots.length; i++) {
          if (slots[i] && slots[i].connected) {
            return slots[i];
          }
        }
        return active;
      }

      function ensureUsbBaudOption(selectEl, baudText) {
        if (!selectEl || !baudText) return;
        var exists = Array.prototype.some.call(selectEl.options, function(opt) { return opt.value === baudText; });
        if (exists) return;
        var opt = document.createElement('option');
        opt.value = baudText;
        opt.textContent = baudText;
        selectEl.appendChild(opt);
      }

      function usbBaudPrefId(slotInfo) {
        if (!slotInfo) return '';
        var slotPart = 'slot:' + String(slotInfo.slot || 0);
        var namePart = String(slotInfo.name || '').replace(/\s+/g, ' ').trim();
        return namePart ? (slotPart + '|name:' + namePart) : slotPart;
      }

      function getPreferredUsbBaud(slotInfo) {
        var prefId = usbBaudPrefId(slotInfo);
        if (!prefId || !state.usbBaudPrefs) return '';
        var raw = state.usbBaudPrefs[prefId];
        var baud = parseInt(raw || '0', 10);
        return (isNaN(baud) || baud <= 0) ? '' : String(baud);
      }

      function savePreferredUsbBaud(slotInfo, baudValue) {
        var prefId = usbBaudPrefId(slotInfo);
        var baud = parseInt(String(baudValue || ''), 10);
        if (!prefId || isNaN(baud) || baud <= 0) return;
        if (!state.usbBaudPrefs || typeof state.usbBaudPrefs !== 'object') state.usbBaudPrefs = {};
        state.usbBaudPrefs[prefId] = String(baud);
        saveUsbBaudPrefs();
      }

      function syncSelectedUsbBaud(slots) {
        var baudSel = byId('grbl_usb_baud');
        if (!baudSel) return;
        slots = normalizeUsbSlots(slots);
        if (!slots.length) return;
        if (document.activeElement === baudSel) return;

        var wantedSlot = selectedSlot();
        var active = getUsbSlotInfo(slots, wantedSlot);
        if (!active) return;

        var preferredBaud = getPreferredUsbBaud(active);
        if (preferredBaud) {
          ensureUsbBaudOption(baudSel, preferredBaud);
          baudSel.value = preferredBaud;
          return;
        }

        var baud = parseInt(active.baud || 0, 10);
        if (isNaN(baud) || baud <= 0) return;

        var baudText = String(baud);
        ensureUsbBaudOption(baudSel, baudText);
        baudSel.value = baudText;
      }

      var _disabledCache = {};
      function setDisabled(id, disabled) {
        var val = !!disabled;
        if (_disabledCache[id] === val) return;
        _disabledCache[id] = val;
        var el = byId(id);
        if (!el) return;
        el.disabled = val;
      }

      // Cache compiled axis RegExps to avoid recreating them on every call (called millions of times for large files).
      var _axisRxCache = {};
      function extractAxisValue(line, axis) {
        var rx = _axisRxCache[axis];
        if (!rx) {
          rx = new RegExp(axis + '\\s*(-?[0-9]*\\.?[0-9]+)', 'i');
          _axisRxCache[axis] = rx;
        }
        var m = line.match(rx);
        if (!m || m.length < 2) return null;
        var v = parseFloat(m[1]);
        if (isNaN(v)) return null;
        return v;
      }
)GRBL"
