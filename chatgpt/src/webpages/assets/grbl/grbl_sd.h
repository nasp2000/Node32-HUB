R"GRBL(
      function sanitizeFileName(name) {
        var safe = String(name || '').trim();
        safe = safe.replace(/\\/g, '/');
        var slash = safe.lastIndexOf('/');
        if (slash >= 0) safe = safe.substring(slash + 1);
        safe = safe.replace(/[^A-Za-z0-9._-]/g, '_');
        if (!safe) safe = 'file.nc';
        return safe;
      }

      function ensureAllowedUploadPath(path, fallbackName) {
        var p = String(path || '').trim();
        var rootDir = String(state.sdRoot || state.sdDir || '').trim();
        var baseDir = String(state.sdDir || rootDir).trim();
        if (!baseDir || !rootDir || baseDir.indexOf(rootDir) !== 0) baseDir = rootDir;
        if (!p) p = baseDir + '/' + sanitizeFileName(fallbackName || 'file.nc');
        p = p.replace(/\\/g, '/');
        if (p.charAt(0) !== '/') p = '/' + p;
        p = p.replace(/\/+/g, '/');
        if (rootDir && p.indexOf(rootDir + '/') !== 0) p = rootDir + '/' + sanitizeFileName(p);
        return p;
      }

      function sdFileNameFromPath(path) {
        var p = String(path || '').replace(/\\/g, '/');
        var ix = p.lastIndexOf('/');
        if (ix >= 0) return p.substring(ix + 1) || p;
        return p;
      }

      function formatSdSize(sizeRaw) {
        var n = parseInt(sizeRaw, 10);
        if (isNaN(n) || n < 0) return '';
        if (n < 1024) return String(n) + ' B';
        if (n < (1024 * 1024)) return (n / 1024).toFixed(1) + ' KB';
        return (n / (1024 * 1024)).toFixed(2) + ' MB';
      }

      function normalizeSdEntry(entry) {
        if (typeof entry === 'string') {
          var p = String(entry || '').trim();
          if (!p) return null;
          return { path: p, name: sdFileNameFromPath(p), size: '', bytes: -1, isDir: false };
        }
        if (!entry || typeof entry !== 'object') return null;
        var path = String(entry.path || entry.file || entry.name || '').trim();
        if (!path) return null;
        var isDir = !!(entry.is_dir || entry.isDir || entry.type === 'dir' || entry.type === 'folder');
        var bytesRaw = parseInt(entry.size || entry.bytes || -1, 10);
        if (isNaN(bytesRaw) || bytesRaw < 0) bytesRaw = -1;
        var modTimeRaw = parseInt(entry.mod_time || entry.modTime || 0, 10);
        if (isNaN(modTimeRaw) || modTimeRaw < 0) modTimeRaw = 0;
        return {
          path: path,
          name: String(entry.name || sdFileNameFromPath(path)),
          size: isDir ? '' : formatSdSize(bytesRaw),
          bytes: bytesRaw,
          isDir: isDir,
          modTime: modTimeRaw
        };
      }

      function getSelectedSdFileEntry() {
        var picked = String(state.selectedSdPath || '').trim();
        if (!picked || state.selectedSdType === 'dir') return null;
        var files = Array.isArray(state.sdFiles) ? state.sdFiles : [];
        var i;
        for (i = 0; i < files.length; i++) {
          var f = files[i];
          if (!f || !!f.isDir) continue;
          if (String(f.path || '') === picked) return f;
        }
        return null;
      }

      function isPreviewSizeBlocked(entry) {
        if (!entry || !!entry.isDir) return false;
        if (typeof entry.bytes !== 'number' || entry.bytes < 0) return false;
        return entry.bytes > GRBL_PREVIEW_MAX_BYTES;
      }

      function isPreviewAutoRenderHeavy(entry) {
        if (!entry || !!entry.isDir) return false;
        if (typeof entry.bytes !== 'number' || entry.bytes < 0) return false;
        return entry.bytes > GRBL_PREVIEW_SOFT_WARN_BYTES;
      }

      function updateSdPreviewLoadButton() {
        var btn = byId('btn_grbl_sd_load');
        if (!btn) return;
        var entry = getSelectedSdFileEntry();
        var noSelection = !entry;
        var tooLarge = isPreviewSizeBlocked(entry);
        // Loading into workspace/preview is read-only and must remain available in observer mode.
        btn.disabled = noSelection || tooLarge;
        if (tooLarge) {
          btn.title = wtSafe('grblSdPreviewLimitHint', 'Preview limited to 50 MB. For larger files use Start File.');
        } else {
          btn.title = '';
        }
      }

      function updateSdDirLabel() {
        var dirEl = byId('grbl_sd_dir');
        if (!dirEl) return;
        dirEl.textContent = wtSafe('grblSdDirNow', 'Directory: ') + String(state.sdDir || state.sdRoot || '--');
      }

      function useSdPath(path, entryType) {
        var picked = String(path || '').trim();
        if (!picked) return;
        var changed = state.selectedSdPath !== picked;
        if (changed) {
          _cachedFrameBounds = null;
          _cachedFramePath   = '';
        }
        var nextType = (entryType === 'dir') ? 'dir' : 'file';
        state.selectedSdPath = picked;
        state.selectedSdType = nextType;
        var selected = byId('grbl_sd_selected');
        if (selected) {
          selected.textContent = (nextType === 'dir')
            ? (wtSafe('grblSdSelectedDirNow', 'Folder: ') + picked)
            : (wtSafe('grblSdSelectedNow', 'Selected: ') + sdFileNameFromPath(picked));
        }
        if (changed) {
          logLine('SD PICK ' + picked);
          renderSdBrowserList();
        }
        updateSdPreviewLoadButton();
        updateFrameInfo();
      }

      function sdParentDir(path) {
        var root = String(state.sdRoot || '').trim();
        var p = String(path || root).trim();
        if (!root) return p;
        if (!p || p === root) return root;
        var slash = p.lastIndexOf('/');
        if (slash <= 0) return root;
        var parent = p.substring(0, slash);
        if (!parent || (parent !== root && parent.indexOf(root + '/') !== 0)) return root;
        return parent;
      }

      function openSdDirectory(path, silent) {
        var root = String(state.sdRoot || '').trim();
        var next = String(path || '').trim();
        if (!next) next = root;
        if (root && next !== root && next.indexOf(root + '/') !== 0) next = root;
        state.sdDir = next;
        state.selectedSdPath = '';
        state.selectedSdType = 'file';
        updateSdDirLabel();
        updateSdPreviewLoadButton();
        return refreshSdFileList(!!silent);
      }

      function renderSdBrowserList() {
        var box = byId('grbl_sd_browser');
        if (!box) return;
        var filterEl = byId('grbl_sd_filter');
        var needle = String(filterEl ? (filterEl.value || '') : '').trim().toLowerCase();
        var current = String(state.selectedSdPath || '').trim();
        var files = Array.isArray(state.sdFiles) ? state.sdFiles : [];
        var shown = 0;
        var i;

        box.innerHTML = '';

        var rootDir = String(state.sdRoot || '').trim();
        if (rootDir && String(state.sdDir || rootDir) !== rootDir) {
          var upRow = document.createElement('div');
          upRow.className = 'grbl-sd-row';
          var upMeta = document.createElement('div');
          upMeta.className = 'grbl-sd-meta';
          var upName = document.createElement('span');
          upName.className = 'grbl-sd-name';
          upName.textContent = '↩ ..';
          var upPath = document.createElement('span');
          upPath.className = 'grbl-sd-path';
          upPath.textContent = wtSafe('grblSdParentDir', 'Parent folder');
          upMeta.appendChild(upName);
          upMeta.appendChild(upPath);
          upRow.appendChild(upMeta);
          upRow.addEventListener('click', function() {
            openSdDirectory(sdParentDir(state.sdDir), false);
          });
          box.appendChild(upRow);
        }

        var sortMode = String(state.sdSortMode || 'name');
        var ordered = files.slice(0).sort(function(a, b) {
          if (!!a.isDir !== !!b.isDir) return a.isDir ? -1 : 1;
          if (sortMode === 'date') {
            var at = parseInt(a.modTime || 0, 10);
            var bt = parseInt(b.modTime || 0, 10);
            if (at !== bt) return bt - at;
          }
          var an = String(a.name || a.path || '').toLowerCase();
          var bn = String(b.name || b.path || '').toLowerCase();
          if (an < bn) return -1;
          if (an > bn) return 1;
          return 0;
        });
        for (i = 0; i < ordered.length; i++) {
          var f = ordered[i];
          var hay = (String(f.name || '') + ' ' + String(f.path || '')).toLowerCase();
          if (needle && hay.indexOf(needle) < 0) continue;

          var isDir = !!f.isDir;
          var row = document.createElement('div');
          row.className = 'grbl-sd-row' + ((!isDir && current && current === f.path) ? ' is-selected' : '');

          var meta = document.createElement('div');
          meta.className = 'grbl-sd-meta';
          var name = document.createElement('span');
          name.className = 'grbl-sd-name';
          name.textContent = (isDir ? '\uD83D\uDCC1 ' : '\uD83D\uDCC4 ') + (f.name || f.path);
          var path = document.createElement('span');
          path.className = 'grbl-sd-path';
          path.textContent = isDir ? f.path : (f.size || '');
          meta.appendChild(name);
          meta.appendChild(path);

          row.appendChild(meta);
          row.addEventListener('click', function(pickedRow, pickedIsDir) {
            return function() {
              if (pickedIsDir) openSdDirectory(pickedRow, false);
              else useSdPath(pickedRow, 'file');
            };
          }(f.path, isDir));
          box.appendChild(row);
          shown++;
        }

        if (shown === 0) {
          var empty = document.createElement('div');
          empty.className = 'grbl-sd-empty';
          empty.textContent = wtSafe('grblSdBrowserEmpty', 'No files found on SD.');
          box.appendChild(empty);
        }
      }

      function refreshSdFileList(silent) {
        updateSdDirLabel();
        return apiGet('/api/grbl/list-gcode?dir=' + encodeURIComponent(String(state.sdDir || state.sdRoot || '')))
          .then(function(data) {
            var prev = String(state.selectedSdPath || '').trim();
            state.sdFiles = [];

            if (!data || data.success !== true || !Array.isArray(data.files)) {
              if (!silent) showAlert('❌ ' + wtSafe('grblSdListError', 'Error listing files on SD.'));
              state.selectedSdPath = '';
              state.selectedSdType = 'file';
              var selectedNone = byId('grbl_sd_selected');
              if (selectedNone) selectedNone.textContent = wtSafe('grblSdSelectedNone', 'No SD file selected.');
              return false;
            }

            if (data.root) state.sdRoot = String(data.root);
            if (!state.sdRoot && data.dir) state.sdRoot = String(data.dir);
            if (data.dir) state.sdDir = String(data.dir);
            updateSdDirLabel();

            var i;
            for (i = 0; i < data.files.length; i++) {
              var item = normalizeSdEntry(data.files[i]);
              if (!item || !item.path) continue;
              state.sdFiles.push(item);
            }

            if (prev && state.sdFiles.some(function(it) { return it.path === prev && !it.isDir; })) {
              state.selectedSdPath = prev;
              state.selectedSdType = 'file';
            } else {
              state.selectedSdPath = '';
              state.selectedSdType = 'file';
            }
            var selected = byId('grbl_sd_selected');
            if (selected) selected.textContent = state.selectedSdPath ? (wtSafe('grblSdSelectedNow', 'Selected: ') + sdFileNameFromPath(state.selectedSdPath)) : wtSafe('grblSdSelectedNone', 'No SD file selected.');
            renderSdBrowserList();
            updateSdPreviewLoadButton();
            return true;
          })
          .catch(function() {
            if (!silent) showAlert('❌ ' + wtSafe('grblSdListError', 'Error listing files on SD.'));
            state.sdFiles = [];
            state.selectedSdPath = '';
            state.selectedSdType = 'file';
            var selectedFail = byId('grbl_sd_selected');
            if (selectedFail) selectedFail.textContent = wtSafe('grblSdSelectedNone', 'No SD file selected.');
            renderSdBrowserList();
            updateSdPreviewLoadButton();
            var psramSection = byId('grbl_psram_section');
            if (psramSection) psramSection.style.display = '';
            var sdSection = byId('grbl_sd_section');
            if (sdSection) sdSection.style.display = 'none';
            return false;
          });
      }

      function refreshPsramFileStatus() {
        return apiGet('/api/grbl/psram-file')
          .then(function(data) {
            var section = byId('grbl_psram_section');
            var statusEl = byId('grbl_psram_status');
            var startBtn = byId('btn_psram_stream_start');
            var clearBtn = byId('btn_grbl_psram_clear');
            var loadBtn = byId('btn_grbl_psram_load');
            if (!data || data.success !== true) {
              state.psramFile = null;
              if (statusEl) statusEl.textContent = wtSafe('grblPsramStatusError', 'Error checking PSRAM.');
              if (startBtn) startBtn.disabled = true;
              if (clearBtn) clearBtn.disabled = true;
              if (loadBtn) loadBtn.disabled = true;
              return;
            }
            if (data.has_file === true && data.name) {
              state.psramFile = { name: String(data.name || ''), size: Number(data.size || 0) };
              if (statusEl) {
                var sizeStr = (state.psramFile.size > 0)
                  ? (' (' + formatFileSize(state.psramFile.size) + ')')
                  : '';
                statusEl.textContent = wtSafe('grblPsramStatusLoaded', 'Loaded in PSRAM: ') + state.psramFile.name + sizeStr;
              }
              if (startBtn) startBtn.disabled = false;
              if (clearBtn) clearBtn.disabled = false;
              if (loadBtn) loadBtn.disabled = false;
            } else {
              state.psramFile = null;
              if (statusEl) statusEl.textContent = wtSafe('grblPsramStatusNone', 'No file in PSRAM.');
              if (startBtn) startBtn.disabled = true;
              if (clearBtn) clearBtn.disabled = true;
              if (loadBtn) loadBtn.disabled = true;
            }
          })
          .catch(function() {
            state.psramFile = null;
            var statusEl = byId('grbl_psram_status');
            if (statusEl) statusEl.textContent = wtSafe('grblPsramStatusError', 'Error checking PSRAM.');
          });
      }

      function promptPsramUpload() {
        var fileInput = byId('grbl_psram_upload_file');
        if (!fileInput) return;
        fileInput.value = '';
        fileInput.click();
      }

      function uploadToPsram() {
        var fileInput = byId('grbl_psram_upload_file');
        if (!fileInput || !fileInput.files || !fileInput.files.length) {
          showAlert('⚠️ ' + wtSafe('grblPsramSelectFile', 'Select a file first.'));
          return;
        }
        var file = fileInput.files[0];
        var statusEl = byId('grbl_psram_status');
        if (statusEl) statusEl.textContent = wtSafe('grblPsramUploading', 'Uploading to PSRAM...');
        var btn = byId('btn_grbl_psram_upload');
        if (btn) btn.disabled = true;

        var query = [];
        query.push('name=' + encodeURIComponent(sanitizeFileName(file.name || 'file.nc')));
        query.push('sid=' + encodeURIComponent(state.sessionId || 'anon'));
        if (state.controlToken) query.push('token=' + encodeURIComponent(state.controlToken));

        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/grbl/psram-upload?' + query.join('&'), true);
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.setRequestHeader('X-Filename', sanitizeFileName(file.name || 'file.nc'));
        xhr.setRequestHeader('X-Filesize', String(file.size || 0));
        xhr.upload.onprogress = function(pe) {
          var pct = (pe.total > 0) ? Math.round(pe.loaded / pe.total * 100) : 0;
          updatePsramUploadUi(true, pct, wtSafe('grblPsramUploadProgress', 'Uploading: ') + String(pct) + '%');
        };
        updatePsramUploadUi(true, 0, wtSafe('grblPsramUploadProgress', 'Uploading: 0%'));
        xhr.onload = function() {
          updatePsramUploadUi(false, 100, '');
          var data = null;
          try {
            data = JSON.parse(xhr.responseText || '{}');
          } catch (err) { data = null; }
          if (data && data.success === true) {
            if (statusEl) statusEl.textContent = wtSafe('grblPsramUploadOk', '✅ Uploaded to PSRAM: ') + (file.name || 'file.nc');
            refreshPsramFileStatus();
          } else {
            var msg = data && data.message ? String(data.message) : wtSafe('grblPsramUploadFail', 'Failed to upload to PSRAM.');
            if (statusEl) statusEl.textContent = '❌ ' + msg;
            showAlert('❌ ' + msg);
          }
          if (btn) btn.disabled = false;
        };
        xhr.onerror = function() {
          updatePsramUploadUi(false, 0, '');
          if (statusEl) statusEl.textContent = '❌ ' + wtSafe('grblPsramUploadFail', 'Failed to upload to PSRAM.');
          if (btn) btn.disabled = false;
        };
        var reader = new FileReader();
        reader.onload = function(ev) {
          var arrayBuffer = ev.target.result;
          xhr.send(new Uint8Array(arrayBuffer));
        };
        reader.readAsArrayBuffer(file);
      }

      function clearPsramFile() {
        runWithCriticalConfirm('grblPsramClearConfirm', 'Clear PSRAM file?', function() {
          postAction('/api/grbl/psram-clear', {}, 'PSRAM CLEAR').then(function(ok) {
            if (ok) refreshPsramFileStatus();
          });
        });
      }

      function loadSelectedPsramToWorkspace() {
        if (!state.psramFile || !state.psramFile.name) {
          showAlert('⚠️ ' + wtSafe('grblPsramNoFile', 'No file in PSRAM.'));
          return;
        }
        var loadBtn = byId('btn_grbl_psram_load');
        if (loadBtn) loadBtn.disabled = true;
        var statusEl = byId('grbl_psram_status');
        if (statusEl) statusEl.textContent = wtSafe('grblSdLoadWorkspaceProgress', 'Loading workspace: ') + '...';
        var url = '/api/grbl/psram-read-text';
        var xhr = new XMLHttpRequest();
        xhr.open('GET', url, true);
        xhr.onload = function() {
          if (xhr.status === 200) {
            var safeText = String(xhr.responseText || '');
            if (!safeText.trim()) {
              if (statusEl) statusEl.textContent = '';
              showAlert('❌ ' + wtSafe('grblPsramReadFail', 'Failed to read PSRAM file.'));
              if (loadBtn) loadBtn.disabled = false;
              refreshPsramFileStatus();
              return;
            }
            state.workspaceText = safeText;
            renderPreviewFromText(safeText);
            updateFrameInfo();
            if (statusEl) statusEl.textContent = wtSafe('grblPsramLoadWorkspaceOk', '✅ PSRAM file loaded into workspace.');
            showAlert(wtSafe('grblPsramLoadWorkspaceOk', '✅ PSRAM file loaded into workspace.'));
            if (loadBtn) loadBtn.disabled = false;
            logLine('PSRAM LOAD WORKSPACE ' + state.psramFile.name + ' bytes=' + String(safeText.length || 0));
            refreshPsramFileStatus();
          } else {
            if (statusEl) statusEl.textContent = '';
            showAlert('❌ ' + wtSafe('grblPsramReadFail', 'Failed to read PSRAM file.'));
            if (loadBtn) loadBtn.disabled = false;
            refreshPsramFileStatus();
          }
        };
        xhr.onerror = function() {
          if (statusEl) statusEl.textContent = '';
          showAlert('❌ ' + wtSafe('grblPsramReadFail', 'Failed to read PSRAM file.'));
          if (loadBtn) loadBtn.disabled = false;
          refreshPsramFileStatus();
        };
        xhr.send();
      }

      function startPsramStream() {
        if (!state.psramFile || !state.psramFile.name) {
          showAlert('⚠️ ' + wtSafe('grblPsramNoFile', 'No file in PSRAM.'));
          return;
        }
        runWithCriticalConfirm('grblConfirmStartPsram', 'Start PSRAM file?', function() {
          requireAxesAtZero(function() {
            setCommandMode('sd-stream');
            postAction('/api/grbl/psram-stream', { slot: selectedSlot() }, 'PSRAM STREAM ' + state.psramFile.name);
          });
        });
      }

      function formatFileSize(bytes) {
        if (!bytes || bytes <= 0) return '0 B';
        if (bytes < 1024) return String(bytes) + ' B';
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
        return (bytes / 1048576).toFixed(1) + ' MB';
      }

      function promptSdUploadFile() {
        if (state.sdUploadActive) return;
        var fileInput = byId('grbl_sd_upload_file');
        if (!fileInput) {
          showAlert('❌ ' + wtSafe('grblSdUploadMissingFile', '⚠️ Select a file first.'));
          return;
        }
        fileInput.value = '';
        fileInput.click();
      }

      function uploadLocalFileToSd() {
        var fileInput = byId('grbl_sd_upload_file') || byId('grbl_local_file');
        if (!fileInput || !fileInput.files || !fileInput.files.length) {
          return;
        }

        var file = fileInput.files[0];
        var targetPath = ensureAllowedUploadPath('', file.name || 'file.nc');
        state.sdUploadActive = true;
        updateSdUploadUi(true, 2, wtSafe('grblSdUploadStarting', 'Starting upload...'));

        function doUploadWithToken(token) {
          var query = [];
          query.push('path=' + encodeURIComponent(targetPath));
          query.push('name=' + encodeURIComponent(sanitizeFileName(file.name || 'file.nc')));
          query.push('overwrite=1');
          query.push('sid=' + encodeURIComponent(state.sessionId || 'anon'));
          if (token) query.push('token=' + encodeURIComponent(token));

          var reader = new FileReader();
          reader.onload = function(ev) {
            var arrayBuffer = ev.target.result;

            var xhr = new XMLHttpRequest();
            xhr.open('POST', '/api/grbl/upload-gcode-stream?' + query.join('&'), true);
            xhr.setRequestHeader('Content-Type', 'application/octet-stream');
            xhr.setRequestHeader('X-Filename', sanitizeFileName(file.name || 'file.nc'));
            xhr.setRequestHeader('X-Filesize', String(file.size || 0));
            xhr.upload.onprogress = function(ev) {
              if (!ev || !ev.lengthComputable) return;
              var pct = Math.round((ev.loaded / ev.total) * 100);
              updateSdUploadUi(true, pct, wtSafe('grblSdUploadProgress', 'Upload: ') + String(pct) + '%');
            };
            xhr.onload = function() {
              var data = null;
              try {
                data = JSON.parse(xhr.responseText || '{}');
              } catch (err) {
                data = null;
              }
              if (!data || data.success !== true) {
                var msg = data && data.message ? String(data.message) : wtSafe('grblSdUploadFail', 'Failed to upload to SD.');
                // Retry once on token expiry
                if (msg === 'auth_token_invalid' && token) {
                  logLine('SD UPLOAD token expired, retrying...');
                  fetchControlToken().then(function(ok) {
                    if (ok) {
                      doUploadWithToken(state.controlToken);
                    } else {
                      failUpload(msg);
                    }
                  });
                  return;
                }
                failUpload(msg);
                return;
              }
              var savedPath = data.file_path ? String(data.file_path) : targetPath;
              useSdPath(savedPath);
              logLine('SD UPLOAD OK ' + savedPath);
              updateSdUploadUi(true, 100, wtSafe('grblSdUploadOk', '✅ File saved to SD successfully.'));
              refreshSdFileList(true).then(function() {
                state.selectedSdPath = savedPath;
                state.selectedSdType = 'file';
                var selected = byId('grbl_sd_selected');
                if (selected) selected.textContent = wtSafe('grblSdSelectedNow', 'Selected: ') + sdFileNameFromPath(savedPath);
                renderSdBrowserList();
                loadSelectedSdToWorkspace();
                state.sdUploadActive = false;
                setTimeout(function() { updateSdUploadUi(false, 0, ''); }, 1800);
              });
              fileInput.value = '';
            };
            xhr.onerror = function() {
              state.sdUploadActive = false;
              updateSdUploadUi(true, 0, '❌ ' + wtSafe('grblSdUploadFail', 'Failed to upload to SD.'));
              showAlert('❌ ' + wtSafe('grblSdUploadFail', 'Failed to upload to SD.'));
              logLine('SD UPLOAD ERR request');
              setTimeout(function() { updateSdUploadUi(false, 0, ''); }, 2200);
              fileInput.value = '';
            };
            xhr.send(new Uint8Array(arrayBuffer));
          };
          reader.readAsArrayBuffer(file);
        }

        function failUpload(msg) {
          state.sdUploadActive = false;
          updateSdUploadUi(true, 0, '❌ ' + msg);
          showAlert('❌ ' + msg);
          logLine('SD UPLOAD ERR ' + msg);
          setTimeout(function() { updateSdUploadUi(false, 0, ''); }, 2200);
          fileInput.value = '';
        }

        doUploadWithToken(state.controlToken);
      }

      function loadSelectedSdToWorkspace(options) {
        var silentLoad = !!(options && options.silent === true);
        var path = String(state.selectedSdPath || '').trim();
        if (!path || state.selectedSdType === 'dir') {
          showAlert('⚠️ ' + wtSafe('grblSdPickFileFirst', 'Select a G-code file first.'));
          return;
        }

        var selectedEntry = getSelectedSdFileEntry();
        if (isPreviewSizeBlocked(selectedEntry)) {
          var sizeLabel = (selectedEntry && selectedEntry.size) ? String(selectedEntry.size) : '';
          var msg = wtSafe('grblSdPreviewLimitMsg', 'Preview limited to 50 MB. For larger files use Start File.');
          if (sizeLabel) msg += ' (' + sizeLabel + ')';
          showAlert('⚠️ ' + msg);
          return;
        }
        var skipAutoPreview = isPreviewAutoRenderHeavy(selectedEntry);
        if (skipAutoPreview && !silentLoad) {
          showAlert('⚠️ ' + wtSafe('grblSdPreviewLargeWarn', 'Large file detected (&gt;10 MB). Showing lightweight preview frame to avoid lag.'));
        }

        // A silent (background) reload of the same file is redundant and, right
        // before starting a stream, races the ESP32 while it loads the file for
        // streaming — which is what produced "silent fail-read". Skip it when
        // the workspace already holds this exact file.
        if (silentLoad && state.workspacePath === path && state.workspaceText) {
          return;
        }

        function applyWorkspaceText(text) {
          var safeText = String(text || '');
          if (!safeText.trim()) {
            if (silentLoad) {
              logLine('SD LOAD WORKSPACE silent fail-empty ' + path);
              return;
            }
            updateSdUploadUi(true, 0, '❌ ' + wtSafe('grblSdReadFail', 'Failed to read SD file.'));
            showAlert('❌ ' + wtSafe('grblSdReadFail', 'Failed to read SD file.'));
            setTimeout(function() { updateSdUploadUi(false, 0, ''); }, 2200);
            return;
          }
          var selectedPreviewMode = state.preview.previewMode;
          var skipAutoPreview = (selectedPreviewMode === 'frame');

          state.workspaceText = safeText;
          state.workspacePath = path;
          if (!skipAutoPreview) {
            renderPreviewFromText(safeText);
          } else {
            fetchFrameBoundsFromSelectedSd(path).then(function(b) {
              applyLightPreviewFromBounds(b, path);
            }).catch(function() {
              applyLightPreviewFromBounds(null, path);
            });
          }
          updateFrameInfo();
          if (!silentLoad) {
            updateSdUploadUi(true, 100, wtSafe('grblSdLoadWorkspaceOk', '✅ G-code loaded into workspace.'));
            showAlert(wtSafe('grblSdLoadWorkspaceOk', '✅ G-code loaded into workspace.'));
          }
          logLine('SD LOAD WORKSPACE ' + path + ' bytes=' + String(safeText.length || 0));
          if (!silentLoad) {
            setTimeout(function() { updateSdUploadUi(false, 0, ''); }, 1800);
          }
        }

        function looksLikeGcodePayload(rawText) {
          var txt = String(rawText || '');
          if (!txt.trim()) return false;
          if (txt.indexOf('file_not_found') === 0) return false;
          if (txt.indexOf('invalid_path') === 0) return false;
          if (txt.indexOf('sd_busy') === 0) return false;
          if (txt.indexOf('sd_unavailable') === 0) return false;
          if (txt.indexOf('file_too_large') === 0) return false;
          return /[GMTXYZFIJRS]\s*-?[0-9]/i.test(txt) || /\n\s*G\d+/i.test(txt);
        }

        function fetchWorkspaceTextViaXhr(requestUrl) {
          return new Promise(function(resolve, reject) {
            var xhr = new XMLHttpRequest();
            xhr.open('GET', requestUrl, true);
            xhr.onprogress = function(ev) {
              if (silentLoad) return;
              if (!ev) return;
              if (ev.lengthComputable && ev.total > 0) {
                var pct = Math.round((ev.loaded / ev.total) * 100);
                if (pct < 2) pct = 2;
                if (pct > 99) pct = 99;
                updateSdUploadUi(true, pct, wtSafe('grblSdLoadWorkspaceProgress', 'Loading workspace: ') + String(pct) + '%');
              }
            };
            xhr.onload = function() {
              if (xhr.status === 200) {
                resolve(String(xhr.responseText || ''));
                return;
              }
              reject(new Error('http_' + String(xhr.status || 0)));
            };
            xhr.onerror = function() {
              reject(new Error('network_error'));
            };
            xhr.send();
          });
        }

        function fetchWorkspaceTextProgressive() {
          var requestUrl = '/api/grbl/read-gcode-text?path=' + encodeURIComponent(path) + '&sid=' + encodeURIComponent(state.sessionId || 'anon');
          var supportsFetch = (typeof window.fetch === 'function');
          var supportsDecoder = (typeof window.TextDecoder === 'function');

          if (!supportsFetch || !supportsDecoder) {
            return fetchWorkspaceTextViaXhr(requestUrl);
          }

          return fetch(requestUrl, { cache: 'no-store' }).then(function(resp) {
            if (!resp || !resp.ok) {
              throw new Error('http_' + String(resp ? (resp.status || 0) : 0));
            }

            var total = -1;
            var headerLen = '';
            if (resp.headers && typeof resp.headers.get === 'function') {
              headerLen = String(resp.headers.get('X-File-Size') || resp.headers.get('Content-Length') || '');
            }
            var parsedTotal = parseInt(headerLen, 10);
            if (!isNaN(parsedTotal) && parsedTotal > 0) total = parsedTotal;

            if (!resp.body || typeof resp.body.getReader !== 'function') {
              return resp.text().then(function(fullText) {
                var approx = String(fullText || '');
                if (!silentLoad) {
                  updateSdUploadUi(true, 99, wtSafe('grblSdLoadWorkspaceProgress', 'Loading workspace: ') + '99%');
                }
                return approx;
              });
            }

            var reader = resp.body.getReader();
            var decoder = new TextDecoder('utf-8');
            var chunks = [];
            var loaded = 0;
            var lastPct = 2;

            function reportProgress() {
              if (silentLoad) return;
              if (total > 0) {
                var pct = Math.round((loaded / total) * 100);
                if (pct < 2) pct = 2;
                if (pct > 99) pct = 99;
                if (pct < lastPct) pct = lastPct;
                lastPct = pct;
                updateSdUploadUi(true, pct, wtSafe('grblSdLoadWorkspaceProgress', 'Loading workspace: ') + String(pct) + '%');
                return;
              }

              var kb = Math.round(loaded / 1024);
              var pseudo = 2 + Math.floor(loaded / (128 * 1024));
              if (pseudo > 95) pseudo = 95;
              if (pseudo < lastPct) pseudo = lastPct;
              lastPct = pseudo;
              updateSdUploadUi(true, pseudo, wtSafe('grblSdLoadWorkspaceProgress', 'Loading workspace: ') + String(kb) + ' KB');
            }

            function readNextChunk() {
              return reader.read().then(function(part) {
                if (!part || part.done) {
                  chunks.push(decoder.decode());
                  return chunks.join('');
                }

                var payload = part.value || new Uint8Array(0);
                loaded += (payload.byteLength || 0);
                chunks.push(decoder.decode(payload, { stream: true }));
                reportProgress();
                return readNextChunk();
              });
            }

            return readNextChunk();
          }).then(function(streamedText) {
            var textOut = String(streamedText || '');
            if (looksLikeGcodePayload(textOut)) return textOut;
            logLine('SD LOAD WORKSPACE stream suspicious, retry xhr ' + path + ' bytes=' + String(textOut.length || 0));
            return fetchWorkspaceTextViaXhr(requestUrl);
          });
        }

        if (!silentLoad) {
          updateSdUploadUi(true, 2, wtSafe('grblSdLoadWorkspaceStart', 'Loading G-code into workspace...'));
        }

        fetchWorkspaceTextProgressive().then(function(textPayload) {
          applyWorkspaceText(textPayload || '');
        }).catch(function() {
          if (silentLoad) {
            logLine('SD LOAD WORKSPACE silent fail-read ' + path);
            return;
          }
          var msg = wtSafe('grblSdReadFail', 'Failed to read SD file.');
          updateSdUploadUi(true, 0, '❌ ' + msg);
          showAlert('❌ ' + msg);
          setTimeout(function() { updateSdUploadUi(false, 0, ''); }, 2200);
        });
      }

      function createSdFolder() {
        var rootDir = String(state.sdRoot || state.sdDir || '').trim();
        var baseDir = String(state.sdDir || rootDir).trim();
        if (!baseDir || !rootDir || baseDir.indexOf(rootDir) !== 0) {
          showAlert('⚠️ ' + wtSafe('grblSdListError', 'Error listing files on SD.'));
          return;
        }
        customPrompt(wtSafe('grblSdCreateDirPrompt', 'New folder name:'), 'new_folder', function(folderName) {
          if (!folderName) return;
          folderName = String(folderName || '').trim().replace(/[\\/]/g, '_').replace(/\s+/g, '_');
          if (!folderName) {
            showAlert('⚠️ ' + wtSafe('grblSdInvalidDirName', 'Invalid folder name.'));
            return;
          }

          var fullPath = baseDir + '/' + folderName;
          apiPost('/api/grbl/create-dir', { path: fullPath })
            .then(function(data) {
              if (!data || data.success !== true) {
                var msg = data && data.message ? String(data.message) : wtSafe('grblSdCreateDirFail', 'Failed to create folder.');
                showAlert('❌ ' + msg);
                return;
              }
              showAlert(wtSafe('grblSdCreateDirOk', '✅ Folder created successfully.'));
              openSdDirectory(fullPath, true);
            })
            .catch(function() {
              showAlert('❌ ' + wtSafe('grblSdCreateDirFail', 'Failed to create folder.'));
            });
        });
      }

      function clearSelectedSdPath() {
        // Hard refresh guarantees preview and transient 3D state are fully reset.
        runWithCriticalConfirm('grblConfirmDeselectSd', 'Deselect SD file? The page will reload.', function() {
          state.selectedSdPath = '';
          state.selectedSdType = 'file';
          var selectedNone = byId('grbl_sd_selected');
          if (selectedNone) selectedNone.textContent = wtSafe('grblSdSelectedNone', 'No SD file selected.');
          window.location.reload();
        });
      }

      function deleteSelectedSdFile() {
        var path = String(state.selectedSdPath || '').trim();
        if (!path || state.selectedSdType === 'dir') {
          showAlert('⚠️ ' + wtSafe('grblSdSelectToDelete', 'Select a file or folder to remove.'));
          return;
        }
        var fileName = sdFileNameFromPath(path);
        customConfirm(wtSafe('grblSdConfirmDeleteFile', 'Remove selected file?') + '\n' + fileName, function(ok) {
          if (!ok) return;
          apiPost('/api/grbl/delete-path', { path: path })
            .then(function(data) {
              if (!data || data.success !== true) {
                var msg = data && data.message ? String(data.message) : wtSafe('grblSdDeleteFail', 'Failed to remove item.');
                showAlert('❌ ' + msg);
                return;
              }
              showAlert(wtSafe('grblSdDeleteOk', '✅ Item removed successfully.'));
              state.selectedSdPath = '';
              state.selectedSdType = 'file';
              var selectedNone = byId('grbl_sd_selected');
              if (selectedNone) selectedNone.textContent = wtSafe('grblSdSelectedNone', 'No SD file selected.');
              refreshSdFileList(false);
            })
            .catch(function() {
              showAlert('❌ ' + wtSafe('grblSdDeleteFail', 'Failed to remove item.'));
            });
        });
      }

      function toggleSdSort() {
        var mode = String(state.sdSortMode || 'name');
        var btn = byId('btn_grbl_sd_sort');
        if (mode === 'name') {
          state.sdSortMode = 'date';
          if (btn) btn.textContent = wtSafe('grblSdSortDateBtn', '\u{1F4CB} Sort: Date');
        } else {
          state.sdSortMode = 'name';
          if (btn) btn.textContent = wtSafe('grblSdSortNameBtn', '\u{1F4CB} Sort: Name');
        }
        renderSdBrowserList();
      }
)GRBL"


