#include "page_ftp_asset.h"

const char PAGE_FTP[] PROGMEM = R"HTML(
<div class='page ftp'>
<div class='wrap'>
<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='ftpTipHome'>&#127968; Home</a></div>
<h1 data-i18n='ftpPageTitle'>&#128229; FTP Upload</h1>

<form id='ftp-config-form' action='/api/config' method='POST'>
<input type='hidden' name='_html' value='1'>
<style>.ftp .fgrid{display:grid;grid-template-columns:1fr 1fr;gap:6px}.ftp .fgrid .section{margin:0}.ftp .section h3{font-size:12px;margin:0 0 2px}.ftp .section-desc{font-size:11px;margin-bottom:2px}.ftp input[type=text],.ftp input[type=password],.ftp input[type=number]{padding:5px 8px!important;font-size:12px!important;min-height:26px!important}@media(max-width:560px){.ftp .fgrid{grid-template-columns:1fr}}</style>
<div style='border:2px solid #3b82f6;border-radius:8px;padding:8px;background:#0f172a;margin-top:8px'>
<h2 style='margin-top:0;margin-bottom:4px;font-size:16px;color:#60a5fa' data-i18n='ftpConnTitle'>&#128268; FTP Connection</h2>
<div class='fgrid'>
<div class='section'>
<h3 data-i18n='ftpHostLbl'>FTP Host</h3>
<div class='section-desc' data-i18n='ftpHostDesc'>Windows FTP server address (IP or hostname)</div>
<input name='ftp_host' id='ftp_host' placeholder='192.168.1.10' title='FTP server host' data-i18n-title='ftpTipHost' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</div>
<div class='section'>
<h3 data-i18n='ftpPortLbl'>Port</h3>
<div class='section-desc'>&nbsp;</div>
<input name='ftp_port' id='ftp_port' type='number' min='1' max='65535' placeholder='21' title='FTP port' data-i18n-title='ftpTipPort' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</div>
<div class='section'>
<h3 data-i18n='ftpUserLbl'>User</h3>
<input name='ftp_user' id='ftp_user' placeholder='ftpuser' title='FTP username' data-i18n-title='ftpTipUser' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</div>
<div class='section'>
<h3 data-i18n='ftpPassLbl'>Password</h3>
<input name='ftp_pass' id='ftp_pass' type='password' placeholder='••••••••' title='FTP password' data-i18n-title='ftpTipPass' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</div>
<div class='section'>
<label style='display:flex;gap:8px;align-items:center;color:#e0e7ff;font-size:14px'><input type='checkbox' name='ftp_anonymous' id='ftp_anonymous' value='1'> <span data-i18n='ftpAnonLbl'>Anonymous login (no password)</span></label>
</div>
<div class='section'>
<h3 data-i18n='ftpRemoteLbl'>Remote path</h3>
<div class='section-desc' data-i18n='ftpRemoteDesc'>Destination folder on the FTP server</div>
<input name='ftp_remote_path' id='ftp_remote_path' placeholder='/' title='Remote path' data-i18n-title='ftpTipRemote' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</div>
<div class='section'>
<h3 data-i18n='ftpLocalLbl'>Local folder</h3>
<div class='section-desc' data-i18n='ftpLocalDesc'>Source folder on SD, PSRAM VFS or internal flash (LittleFS)</div>
<select id='ftp_local_dir' name='ftp_local_dir' title='Local folder' data-i18n-title='ftpTipLocal' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
<option value=''>Loading...</option>
</select>
</div>
</div>
</div>

<div style='border:2px solid #10b981;border-radius:8px;padding:8px;margin-top:8px'>
<h2 style='margin-top:0;margin-bottom:4px;font-size:16px;color:#10b981' data-i18n='ftpAutoTitle'>&#9201; Automatic upload</h2>
<div class='fgrid'>
<div class='section'>
<label style='display:flex;gap:8px;align-items:center;color:#e0e7ff;font-size:14px'><input type='checkbox' name='ftp_enabled' id='ftp_enabled' value='1'> <span data-i18n='ftpEnabledLbl'>Enable FTP client</span></label>
</div>
<div class='section'>
<label style='display:flex;gap:8px;align-items:center;color:#e0e7ff;font-size:14px'><input type='checkbox' name='ftp_auto_upload' id='ftp_auto_upload' value='1'> <span data-i18n='ftpAutoLbl'>Monitor folder and upload regularly</span></label>
</div>
<div class='section'>
<h3 data-i18n='ftpIntervalLbl'>Interval (hours)</h3>
<select name='ftp_interval_hours' id='ftp_interval_hours' title='Upload interval in fixed hours: 5H, 12H, 24H or 36H' data-i18n-title='ftpTipInterval' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
<option value='5'>5H</option>
<option value='12' selected>12H</option>
<option value='24'>24H</option>
<option value='36'>36H</option>
</select>
</div>
<div class='section'>
<h3 data-i18n='ftpMaxLbl'>Max files per cycle</h3>
<input name='ftp_max_files' id='ftp_max_files' type='number' min='1' max='50' placeholder='10' title='Max files' data-i18n-title='ftpTipMax' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</div>
<div class='section'>
<label style='display:flex;gap:8px;align-items:center;color:#e0e7ff;font-size:14px'><input type='checkbox' name='ftp_only_new' id='ftp_only_new' value='1'> <span data-i18n='ftpOnlyNewLbl'>Only new/changed files (skip already sent)</span></label>
</div>
<div class='section'>
<label style='display:flex;gap:8px;align-items:center;color:#e0e7ff;font-size:14px'><input type='checkbox' name='ftp_delete_after' id='ftp_delete_after' value='1'> <span data-i18n='ftpDeleteLbl'>Delete local file after verified upload</span></label>
</div>
<div class='section' style='grid-column:1/-1'>
<h3 data-i18n='ftpFilterLbl'>File filter</h3>
<div class='section-desc' data-i18n='ftpFilterDesc'>Select file extensions to upload</div>
<div id='ftp_ext_checks' style='display:flex;flex-wrap:wrap;align-items:center;gap:10px;width:100%'>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' id='ftp_ext_all' onclick='ftpExtAllToggle(this)' title='Select/deselect all extensions' data-i18n-title='ftpTipExtAll'> <span data-i18n='ftpExtAll'>All</span></label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.csv' checked onchange='ftpExtSyncAll()' title='Proximity/sensor data files' data-i18n-title='ftpTipExtCsv'> .csv</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.log' checked onchange='ftpExtSyncAll()' title='Log files' data-i18n-title='ftpTipExtLog'> .log</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.txt' checked onchange='ftpExtSyncAll()' title='Text data files' data-i18n-title='ftpTipExtTxt'> .txt</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.bin' checked onchange='ftpExtSyncAll()' title='Firmware/GRBL binary files' data-i18n-title='ftpTipExtBin'> .bin</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.wav' checked onchange='ftpExtSyncAll()' title='Audio WAV files' data-i18n-title='ftpTipExtWav'> .wav</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.gcode' checked onchange='ftpExtSyncAll()' title='GRBL G-code files' data-i18n-title='ftpTipExtGcode'> .gcode</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.nc' checked onchange='ftpExtSyncAll()' title='GRBL NC files' data-i18n-title='ftpTipExtNc'> .nc</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.avi' checked onchange='ftpExtSyncAll()' title='Camera recording files' data-i18n-title='ftpTipExtAvi'> .avi</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.jpg' checked onchange='ftpExtSyncAll()' title='Camera snapshot files' data-i18n-title='ftpTipExtJpg'> .jpg</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.mp3' checked onchange='ftpExtSyncAll()' title='Audio MP3 files' data-i18n-title='ftpTipExtMp3'> .mp3</label>
<label style='display:flex;align-items:center;gap:4px;color:#e0e7ff;font-size:13px;white-space:nowrap'><input type='checkbox' name='ftp_ext' value='.flac' checked onchange='ftpExtSyncAll()' title='Audio FLAC files' data-i18n-title='ftpTipExtFlac'> .flac</label>
</div>
<div style='margin-top:6px'>
<label style='display:flex;align-items:center;gap:6px;color:#e0e7ff;font-size:12px'><span data-i18n='ftpExtCustom'>Custom:</span> <input id='ftp_ext_custom' type='text' placeholder='.m3u;.iso' style='width:100%;padding:5px 8px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:12px' title='Additional extensions separated by semicolons (e.g. .m3u;.iso)' data-i18n-title='ftpTipExtCustom'></label>
</div>
</div>
</div>
</div>

<div style='text-align:center;margin-top:10px'>
<button type='submit' form='ftp-config-form' class='btn' data-i18n='btnSave' title='Save FTP configuration' data-i18n-title='ftpTipSave'>&#128190; Save</button>
</div>
</form>

<div style='border:2px solid #8b5cf6;border-radius:8px;padding:10px;background:#0f172a;margin-top:10px'>
<h2 style='margin-top:0;margin-bottom:4px;font-size:16px;color:#8b5cf6' data-i18n='ftpStatusTitle'>&#128202; Status</h2>
<div id='ftp-status' style='font-size:13px;color:#94a3b8;line-height:1.8'>Loading...</div>
<div style='display:flex;gap:10px;flex-wrap:wrap;margin-top:8px'>
<button id='btn-ftp-test' type='button' class='btn' style='flex:1;background:#7c3aed;border:1px solid #7c3aed' data-i18n='ftpTestBtn' title='Test FTP connection' data-i18n-title='ftpTipTest'>&#128268; Test connection</button>
<button id='btn-ftp-now' type='button' class='btn' style='flex:1;background:#059669;border:1px solid #059669' data-i18n='ftpNowBtn' title='Upload now' data-i18n-title='ftpTipNow'>&#11014; Upload now</button>
</div>
</div>
</div>
</div>

<script>
function ftpText(k, fb) {
  try {
    if (typeof window._t === 'function') return window._t(k, fb);
    var raw = localStorage.getItem('ui_lang_cache');
    if (raw) {
      var parsed = JSON.parse(raw);
      if (parsed && parsed[k] !== undefined) return parsed[k];
    }
  } catch (err) {}
  return fb !== undefined ? fb : k;
}

function ftpSetCheck(id, v) { var el = document.getElementById(id); if (el) el.checked = !!v; }
function ftpGetCheck(id) { var el = document.getElementById(id); return el ? (el.checked ? '1' : '0') : '0'; }

// Map the saved interval (minutes) to the nearest fixed option (5/12/24/36 H).
function ftpNearestHourMins(mins) {
  var hrs = Math.round(mins / 60);
  var opts = [5, 12, 24, 36], best = opts[0];
  for (var i = 1; i < opts.length; i++) {
    if (Math.abs(opts[i] - hrs) < Math.abs(best - hrs)) best = opts[i];
  }
  return best;
}

function ftpGetFtpExtFilter() {
  var checks = document.querySelectorAll('#ftp_ext_checks input[name="ftp_ext"]');
  var parts = [];
  for (var i = 0; i < checks.length; i++) {
    if (checks[i].checked) parts.push('*' + checks[i].value);
  }
  var custom = document.getElementById('ftp_ext_custom');
  if (custom && custom.value.trim()) {
    var customs = custom.value.trim().split(';');
    for (var j = 0; j < customs.length; j++) {
      var c = customs[j].trim();
      if (!c) continue;
      if (c.charAt(0) === '*') c = c.substring(1);  // drop leading glob
      var dot = c.lastIndexOf('.');
      if (dot >= 0) c = c.substring(dot);           // keep only ".ext"
      if (c && c.charAt(0) !== '.') c = '.' + c;
      if (c && c !== '.' && parts.indexOf('*' + c) < 0) parts.push('*' + c);
    }
  }
  return parts.join(';');
}

function ftpSetFtpExtChecks(filterStr) {
  var checks = document.querySelectorAll('#ftp_ext_checks input[name="ftp_ext"]');
  var knownExts = {};
  var parts = filterStr.split(';');
  function partTokens() {
    var out = [];
    for (var t = 0; t < parts.length; t++) {
      var pt = parts[t].trim();
      if (pt) out.push(pt);
    }
    return out;
  }
  var toks = partTokens();
  for (var i = 0; i < checks.length; i++) {
    // Token-exact match (a substring test would check ".nc" inside "*.inc").
    var hit = false;
    for (var k = 0; k < toks.length; k++) {
      if (toks[k] === checks[i].value || toks[k] === '*' + checks[i].value) { hit = true; break; }
    }
    checks[i].checked = hit;
    knownExts[checks[i].value] = true;
    knownExts['*' + checks[i].value] = true;
  }
  // Restore custom extensions not in the preset list, normalized to ".ext"
  // (so a legacy "*.json"/"*.*.*.json" collapses back to ".json" instead of
  //  growing a new ".*" on every save).
  var customs = [];
  for (var j = 0; j < parts.length; j++) {
    var p = parts[j].trim();
    if (!p || knownExts[p]) continue;
    if (p.charAt(0) === '*') p = p.substring(1);
    var dot = p.lastIndexOf('.');
    if (dot < 0) continue;
    var ext = p.substring(dot);
    if (ext && ext !== '.' && !knownExts[ext] && customs.indexOf(ext) < 0) customs.push(ext);
  }
  var customEl = document.getElementById('ftp_ext_custom');
  if (customEl) customEl.value = customs.join(';');
  ftpExtSyncAll();
}

function ftpExtAllToggle(allBox) {
  var checks = document.querySelectorAll('#ftp_ext_checks input[name="ftp_ext"]');
  for (var i = 0; i < checks.length; i++) {
    checks[i].checked = allBox.checked;
  }
}

function ftpExtSyncAll() {
  var all = document.getElementById('ftp_ext_all');
  var checks = document.querySelectorAll('#ftp_ext_checks input[name="ftp_ext"]');
  var allChecked = true;
  for (var i = 0; i < checks.length; i++) {
    if (!checks[i].checked) { allChecked = false; break; }
  }
  all.checked = allChecked;
}

function ftpLoad() {
  fetch('/api/ftp-status', {cache:'no-store'})
    .then(function(r) { return r.json(); })
    .then(function(d) {
      document.getElementById('ftp_host').value = d.host || '';
      document.getElementById('ftp_port').value = d.port || 21;
      document.getElementById('ftp_user').value = d.user || '';
      document.getElementById('ftp_remote_path').value = d.remote_path || '/';
      var localDir = d.local_dir || 'SD:/';
      document.getElementById('ftp_local_dir').value = localDir;
      var mins = parseInt(d.interval_min || 720, 10);
      if (isNaN(mins) || mins < 300) mins = 300;
      document.getElementById('ftp_interval_hours').value = String(ftpNearestHourMins(mins));
      document.getElementById('ftp_max_files').value = d.max_files || 10;
      ftpSetFtpExtChecks(d.filter || '*.csv;*.log;*.txt;*.bin;*.wav;*.gcode;*.nc;*.avi;*.jpg;*.mp3;*.flac');
      ftpSetCheck('ftp_anonymous', d.anonymous);
      ftpSetCheck('ftp_enabled', d.enabled);
      ftpSetCheck('ftp_auto_upload', d.auto_upload);
      ftpSetCheck('ftp_only_new', d.only_new !== false);
      ftpSetCheck('ftp_delete_after', d.delete_after);
      var st = document.getElementById('ftp-status');
      var lf = d.last_file || '-';
      var lr = d.last_result || '-';
      st.textContent = 'Source: ' + (d.source || '?') + ' | Last: ' + lf + ' | ' + lr + ' | OK ' + (d.ok_count || 0) + ' / FAIL ' + (d.fail_count || 0);
      ftpLoadFolders(localDir);
    })
    .catch(function() { ftpLoadFolders(); });
}

function ftpLoadFolders(want) {
  var sel = document.getElementById('ftp_local_dir');
  var wantVal = want || sel.value;
  fetch('/api/ftp-folders', {cache:'no-store'})
    .then(function(r) { return r.json(); })
    .then(function(arr) {
      if (!Array.isArray(arr) || !arr.length) return;
      var groups = { 'SD:/':'SD card', 'PSRAM:/':'PSRAM VFS', 'LFS:/':'Internal flash (LittleFS)' };
      sel.innerHTML = '';
      var seenGroup = {};
      var selected = false;
      var prefer = null;
      for (var i = 0; i < arr.length; i++) {
        var v = arr[i];
        var prefix = null;
        for (var g in groups) {
          if (v === g || v.indexOf(g) >= 0) { prefix = g; break; }
        }
        if (prefix && !seenGroup[prefix]) {
          seenGroup[prefix] = true;
          var og = document.createElement('optgroup');
          og.label = groups[prefix];
          sel.appendChild(og);
          if (!prefer) prefer = prefix;
        }
        var opt = document.createElement('option');
        opt.value = v;
        opt.textContent = v;
        sel.appendChild(opt);
        // Auto-select: exact match, or (no exact) the root of a filesystem group.
        if (wantVal === v) { sel.value = v; selected = true; }
        else if (!selected && prefer && v === prefer) sel.value = v;
      }
      // Legacy bare path (no FS prefix, e.g. old "/logs") → keep it as first option.
      if (!selected && wantVal && wantVal.indexOf(':') < 0) {
        var legacy = document.createElement('option');
        legacy.value = wantVal;
        legacy.textContent = wantVal;
        sel.insertBefore(legacy, sel.firstChild);
        sel.value = wantVal;
      }
    })
    .catch(function() {});
}
ftpLoad();

document.getElementById('btn-ftp-test').addEventListener('click', function() {
  var btn = this; disableBtn(btn); showLoading();
  fetch('/api/ftp-test', { method: 'POST', headers: { 'Content-Type': 'application/json' } })
    .then(function(r) { return r.json(); })
    .then(function(d) { customAlert(d.message || ftpText('ftpTestDone', 'Test finished')); ftpLoad(); })
    .catch(function(e) { customAlert(ftpText('emailErrorPrefix', 'Error: ') + (e && e.message ? e.message : e)); })
    .finally(function(){hideLoading();enableBtn(btn);});
});

document.getElementById('btn-ftp-now').addEventListener('click', function() {
  var btn = this; disableBtn(btn); showLoading();
  fetch('/api/ftp-upload-now', { method: 'POST', headers: { 'Content-Type': 'application/json' } })
    .then(function(r) { return r.json(); })
    .then(function(d) { customAlert(d.message || ftpText('ftpNowDone', 'Upload cycle finished')); ftpLoad(); })
    .catch(function(e) { customAlert(ftpText('emailErrorPrefix', 'Error: ') + (e && e.message ? e.message : e)); })
    .finally(function(){hideLoading();enableBtn(btn);});
});

document.getElementById('ftp-config-form').addEventListener('submit', function(e) {
  e.preventDefault();
  var btn = this.querySelector('button[type="submit"]'); disableBtn(btn); showLoading();
  var fd = new FormData(this);
  fd.set('section', 'ftp');
  fd.set('ftp_local_dir', document.getElementById('ftp_local_dir').value);
  var hrs = parseInt(document.getElementById('ftp_interval_hours').value, 10);
  if (isNaN(hrs) || [5, 12, 24, 36].indexOf(hrs) < 0) hrs = 12;
  fd.set('ftp_interval_min', String(hrs * 60));
  fd.delete('ftp_interval_hours');
  fd.set('ftp_anonymous', ftpGetCheck('ftp_anonymous'));
  fd.set('ftp_enabled', ftpGetCheck('ftp_enabled'));
  fd.set('ftp_auto_upload', ftpGetCheck('ftp_auto_upload'));
  fd.set('ftp_only_new', ftpGetCheck('ftp_only_new'));
  fd.set('ftp_delete_after', ftpGetCheck('ftp_delete_after'));
  fd.set('ftp_filter', ftpGetFtpExtFilter());
  var pass = document.getElementById('ftp_pass').value;
  if (!pass) fd.delete('ftp_pass');
  fetch(this.action, {
    method: 'POST',
    body: new URLSearchParams(fd),
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
  })
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (d.success) {
        customAlert('✅ ' + ftpText('alertSaved', 'Configuration saved!'));
        if (typeof showSavedCard === 'function') showSavedCard();
        ftpLoad();
      } else {
        customAlert('❌ ' + ftpText('emailErrorPrefix', 'Error: ') + (d.message || ftpText('accessUnknown', 'unknown')));
      }
    })
    .catch(function(err) {
      customAlert('❌ ' + ftpText('emailErrorPrefix', 'Error: ') + (err && err.message ? err.message : err));
    })
    .finally(function(){hideLoading();enableBtn(btn);});
});
</script>
)HTML";
