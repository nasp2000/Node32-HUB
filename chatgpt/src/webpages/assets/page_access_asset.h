#pragma once
#include <pgmspace.h>

const char PAGE_ACCESS[] PROGMEM = R"HTML(
<div class='page access'>
<div class='wrap'>
<div class='actions nav-inicio'>
  <a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='accessTipHome'>&#127968; Home</a>
</div>
<h1 data-i18n='accessPageTitle'>&#128274; Access</h1>

<div class='acc-tab-bar' id='accTabBar'></div>

<form id='access-form'>

<div class='acc-panel' data-tab='tabAccessSessions'>
<div class='acc-panel-inner'>
<h2 data-i18n='accessSessionsTitle'>&#128101; Web Sessions</h2>
<div class='acc-desc' data-i18n='accessSessionsDesc'>Controls how many browsers can be connected at the same time and for how long.</div>

<label class='acc-field'>
  <span data-i18n='accessMaxClientsLbl'>Maximum simultaneous clients</span>
  <span class='acc-hint' data-i18n='accessMaxClientsHint'>When the limit is reached, new browsers receive HTTP 429.</span>
  <input type='number' name='web_max_sessions' id='web_max_sessions' min='1' max='5' value='3' title='Max simultaneous clients' data-i18n-title='accessTipMaxClients'>
</label>

<label class='acc-field'>
  <span data-i18n='accessIdleTimeoutLbl'>Inactivity timeout (minutes)</span>
  <span class='acc-hint' data-i18n='accessIdleTimeoutHint'>Session is terminated after this period without requests.</span>
  <select name='web_idle_timeout_min' id='web_idle_timeout_min' title='Inactivity timeout' data-i18n-title='accessTipIdleTimeout'>
    <option value='5'>5 min</option>
    <option value='10' selected>10 min</option>
    <option value='15'>15 min</option>
    <option value='20'>20 min</option>
    <option value='30'>30 min</option>
    <option value='60'>60 min</option>
  </select>
</label>

<label class='acc-field'>
  <span data-i18n='accessAbsTimeoutLbl'>Absolute timeout (minutes)</span>
  <span class='acc-hint' data-i18n='accessAbsTimeoutHint'>Session is terminated regardless of activity.</span>
  <select name='web_abs_timeout_min' id='web_abs_timeout_min' title='Absolute timeout' data-i18n-title='accessTipAbsTimeout'>
    <option value='15'>15 min</option>
    <option value='20' selected>20 min</option>
    <option value='30'>30 min</option>
    <option value='60'>60 min</option>
    <option value='120'>120 min</option>
  </select>
</label>

<!-- Sessions list + terminate-all (ACC-1). Buttons are type=button so they
     never submit the configuration form. -->
<div class='acc-session-box' id='acc_sessions_box' style='margin-top:16px;padding-top:12px;border-top:1px solid rgba(148,163,184,.25)'>
  <div style='display:flex;align-items:center;gap:8px;flex-wrap:wrap'>
    <span style='font-weight:600' data-i18n='accessActiveSessions'>&#128101; Active sessions</span>
    <button type='button' class='btn' id='acc_sessions_refresh' style='margin-left:auto;padding:6px 12px;font-size:12px' data-i18n='accessSessionsRefresh' title='Reload the session history' data-i18n-title='accessTipSessionsRefresh'>&#128260; Refresh</button>
  </div>
  <div class='acc-desc' data-i18n='accessSessionsListDesc'>Last connection events recorded by the firmware (newest first).</div>
  <div id='acc_sessions_list' class='acc-session-list' style='margin:8px 0;font-size:12px;max-height:180px;overflow:auto' role='list'>--</div>
  <button type='button' class='btn' id='acc_kick_all' style='background:#7f1d1d;border:1px solid #dc2626;color:#fff;padding:8px 14px;font-size:13px' data-i18n='accessKickAll' title='Terminate every web session' data-i18n-title='accessTipKickAll'>&#10060; Terminate all sessions</button>
  <div id='acc_sessions_msg' class='acc-hint' role='status' aria-live='polite'></div>
</div>
</div>
</div>

<div class='acc-panel' data-tab='tabAccessCharts'>
<div class='acc-panel-inner'>
<h2 data-i18n='accessLocalPagesTitle'>&#128196; Local Processing Pages</h2>
<div class='acc-desc' data-i18n='accessLocalPagesDesc'>Pages that run processing locally in your browser.</div>
<input type='hidden' name='charts_exempt_present' value='1'>
<label class='acc-check'>
  <input type='checkbox' name='charts_exempt' id='charts_exempt' title='Local processing pages never expire' data-i18n-title='accessTipLocalPages'>
  <span data-i18n='accessLocalPagesNeverExpireLbl'>Local processing page sessions never expire</span>
</label>
<div class='acc-hint' style='margin-left:26px' data-i18n-html='accessLocalPagesNeverExpireHint'>When active, sessions on local processing pages are not removed by timeout. They still count toward the session limit.</div>
</div>
</div>

<div class='acc-panel' data-tab='tabAccessMonitor'>
<div class='acc-panel-inner'>
<h2 data-i18n='accessMonitorTitle'>&#128223; Monitoring</h2>
<div class='acc-desc' data-i18n='accessMonitorDesc'>Controls visibility of monitoring features on the main page.</div>
<input type='hidden' name='serial_monitor_enabled_present' value='1'>
<label class='acc-check'>
  <input type='checkbox' name='serial_monitor_enabled' id='serial_monitor_enabled' title='Show monitor button on home' data-i18n-title='accessTipMonitorBtn'>
  <span data-i18n-html='accessMonitorBtnLbl'>Show <strong>Serial Monitor</strong> button on the main page</span>
</label>
<div class='acc-hint' style='margin-left:26px' data-i18n-html='accessMonitorBtnHint'>When active, the &#128223; Serial Monitor button appears in the Monitoring section of the home page.</div>
</div>
</div>

<div class='acc-panel' data-tab='tabAccessUsers' data-tab-default='1'>
<div class='acc-panel-inner'>
<h2 data-i18n='accessUsersTitle'>&#128100;&#65039; Users</h2>
<div class='acc-desc' data-i18n='accessUsersDesc'>Manage administrator and viewer accounts.</div>

<div class='acc-user-section'>
<h3 data-i18n='accessAdminTitle'>&#128272; Administrator</h3>
<div class='acc-desc' data-i18n='accessAdminDesc'>Full access to all pages and configurations.</div>

<input type='hidden' name='auth_enabled_present' value='1'>
<label class='acc-check'>
  <input type='checkbox' name='auth_enabled' id='auth_enabled' title='Require login' data-i18n-title='accessTipAuthEnable'>
  <span data-i18n='accessAuthEnableLbl'>Require login (password protection)</span>
</label>
<div class='acc-hint' style='margin-left:26px' data-i18n-html='accessAuthEnableHint'>When active, browsers must log in with the administrator credentials below. Disabled by default — access is open.</div>

<label class='acc-field'>
  <span data-i18n='accessUsernameLbl'>Username</span>
  <input type='text' name='web_username' id='web_username' autocomplete='username' title='Admin username' data-i18n-title='accessTipAdminUser'>
</label>

<label class='acc-field'>
  <span data-i18n-html='accessNewPasswordLbl'>New password <span class='acc-subtle'>(leave empty to keep current)</span></span>
  <input type='password' name='web_password_new' id='web_password_new' autocomplete='new-password' title='Admin password' data-i18n-title='accessTipAdminPwd'>
</label>

<label class='acc-field'>
  <span data-i18n='accessConfirmPasswordLbl'>Confirm password</span>
  <input type='password' name='web_password_confirm' id='web_password_confirm' autocomplete='new-password'>
</label>
<div id='pwd-match-msg' class='acc-field acc-err' style='display:none' data-i18n='accessPwdMismatch'>Passwords do not match.</div>
</div>

<div class='acc-user-section'>
<h3 data-i18n='accessViewerTitle'>&#128065;&#65039; Viewer</h3>
<div class='acc-desc' data-i18n='accessViewerDesc'>Read-only access to a limited set of pages. Cannot change settings.</div>

<label class='acc-check'>
  <input type='hidden' name='viewer_enabled_present' value='1'>
  <input type='checkbox' name='viewer_enabled' id='viewer_enabled' title='Enable viewer account' data-i18n-title='accessTipViewerEnable'>
  <span data-i18n='accessViewerEnableLbl'>Enable viewer user</span>
</label>

<div id='viewer_fields'>
<label class='acc-field'>
  <span data-i18n='accessViewerUsernameLbl'>Viewer username</span>
  <input type='text' name='viewer_username' id='viewer_username' autocomplete='off' title='Viewer username' data-i18n-title='accessTipViewerUser'>
</label>

<label class='acc-field'>
  <span data-i18n-html='accessViewerPasswordLbl'>Viewer password <span class='acc-subtle'>(leave empty to keep current)</span></span>
  <input type='password' name='viewer_password' id='viewer_password' autocomplete='new-password' title='Viewer password' data-i18n-title='accessTipViewerPwd'>
</label>
</div>

<div class='acc-routes-hint' data-i18n-html='accessViewerRoutesHint'>
  &#128274; The viewer can access: <strong>/</strong>, <strong>/status</strong>, <strong>/sensor-charts</strong>, <strong>/monitor</strong>, <strong>/files</strong> — view only, cannot change settings or execute actions.
</div>
</div>
</div>
</div>

<div style='text-align:center;margin-top:16px'>
  <button type='submit' class='btn' style='padding:10px 32px' data-i18n='btnSave' title='Save configuration' data-i18n-title='accessTipSave'>&#128190; Save</button>
</div>

</form>

</div>
</div>

<script>
var ACC_TABS = [
  { key:'tabAccessSessions', label:'' },
  { key:'tabAccessCharts',   label:'' },
  { key:'tabAccessMonitor',  label:'' },
  { key:'tabAccessUsers',    label:'' }
];

function accSwitchTab(btns, panels, idx) {
  for (var i = 0; i < btns.length; i++) {
    btns[i].classList.toggle('active', i === idx);
  }
  for (var i = 0; i < panels.length; i++) {
    panels[i].classList.toggle('active', i === idx);
  }
}

function accInitTabs() {
  var form = document.getElementById('access-form');
  var bar = document.getElementById('accTabBar');
  if (!form || !bar) return;
  var panels = form.querySelectorAll('.acc-panel[data-tab]');
  if (panels.length < 2) return;

  var t = ACC_TABS;
  var btns = [];
  var defaultIdx = 0;

  for (var i = 0; i < panels.length; i++) {
    var key = panels[i].getAttribute('data-tab');
    if (panels[i].hasAttribute('data-tab-default')) defaultIdx = i;
    var label = key;
    for (var j = 0; j < t.length; j++) {
      if (t[j].key === key) { label = _t(key, key); break; }
    }
    var b = document.createElement('button');
    b.type = 'button';
    b.className = 'acc-tab-btn';
    b.textContent = label;
    (function(ix) {
      b.onclick = function() { accSwitchTab(btns, panels, ix); };
    })(i);
    btns.push(b);
    bar.appendChild(b);
  }
  accSwitchTab(btns, panels, defaultIdx);
}

function loadAccessConfig() {
  var npEl = document.getElementById('web_password_new');
  if (npEl) npEl.placeholder = _t('accessPwdNoChangePlaceholder', '(unchanged)');
  var cpEl = document.getElementById('web_password_confirm');
  if (cpEl) cpEl.placeholder = _t('accessPwdRepeatPlaceholder', 'Repeat password');
  var vpEl = document.getElementById('viewer_password');
  if (vpEl) vpEl.placeholder = _t('accessPwdNoChangePlaceholder', '(unchanged)');

  fetch('/api/access-config', {cache:'no-store'})
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (!d || !d.web) return;
      var w = d.web;

      var ms = document.getElementById('web_max_sessions');
      if (ms) ms.value = (w.max_sessions != null) ? w.max_sessions : 3;

      var it = document.getElementById('web_idle_timeout_min');
      if (it) it.value = (w.idle_timeout_min != null) ? w.idle_timeout_min : 10;

      var at = document.getElementById('web_abs_timeout_min');
      if (at) at.value = (w.abs_timeout_min != null) ? w.abs_timeout_min : 20;

      var ceEl = document.getElementById('charts_exempt');
      if (ceEl) ceEl.checked = !!d.charts_exempt;

      var smeEl = document.getElementById('serial_monitor_enabled');
      if (smeEl) smeEl.checked = (d.serial_monitor_enabled !== undefined) ? !!d.serial_monitor_enabled : false;

      var usr = document.getElementById('web_username');
      if (usr) usr.value = w.username || '';

      var aeEl = document.getElementById('auth_enabled');
      if (aeEl) aeEl.checked = !!w.auth_enabled;

      var ve = !!w.viewer_enabled;
      var veEl = document.getElementById('viewer_enabled');
      if (veEl) veEl.checked = ve;

      var vu = document.getElementById('viewer_username');
      if (vu) vu.value = w.viewer_username || 'viewer';
    })
    .catch(function() {});
}

document.getElementById('access-form').addEventListener('submit', function(e) {
  e.preventDefault();

  var np = document.getElementById('web_password_new').value;
  var cp = document.getElementById('web_password_confirm').value;
  var mmEl = document.getElementById('pwd-match-msg');
  if (np && np !== cp) {
    if (mmEl) mmEl.style.display = 'block';
    return;
  }
  if (mmEl) mmEl.style.display = 'none';

  var btn=this.querySelector('button[type="submit"]');disableBtn(btn);showLoading();
  var fd = new FormData(this);
  fetch('/api/access-config', {method:'POST', body:new URLSearchParams(fd), headers:{'Content-Type':'application/x-www-form-urlencoded'}})
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (d.success) {
        customAlert('&#10003; ' + (d.message || 'Configuration saved!'));
        if (typeof showSavedCard === 'function') showSavedCard();
      } else {
        customAlert('&#10008; ' + _t('accessErrorPrefix', 'Error:') + ' ' + (d.message || _t('accessUnknown', 'unknown')));
      }
    })
    .catch(function(err) { customAlert('&#10008; ' + _t('accessErrorPrefix', 'Error:') + ' ' + (err.message || err)); })
    .finally(function(){hideLoading();enableBtn(btn);});
});

function accEsc(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, function(c) {
    return ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c];
  });
}

function accSessionLabel(kind) {
  if (kind === 'kick') return _t('accessSessionKick', 'terminated');
  if (kind === 'expired') return _t('accessSessionExpired', 'expired');
  return _t('accessSessionConnect', 'connected');
}

// Rows for /api/session-history (newest first, capped). Pure: used by the page
// and by the jsdom test, which asserts the escaping and the ordering.
function accSessionRows(events, maxRows) {
  var lim = maxRows || 20;
  if (!events || !events.length) return '';
  var list = events.slice();
  list.sort(function(a, b) { return (Number(b && b.ts) || 0) - (Number(a && a.ts) || 0); });
  var out = [];
  for (var i = 0; i < list.length && out.length < lim; i++) {
    var e = list[i] || {};
    var kind = String(e.type || 'connect');
    out.push("<div class='acc-session-row' role='listitem' style='display:flex;gap:8px;padding:2px 0'>" +
      "<span class='acc-session-ip' style='flex:1;font-family:monospace'>" + accEsc(e.ip || '?') + "</span>" +
      "<span class='acc-session-type acc-session-" + accEsc(kind) + "' style='flex:0 0 auto'>" + accEsc(accSessionLabel(kind)) + "</span>" +
      "<span class='acc-session-when' style='flex:0 0 auto;color:#94a3b8'>" + accEsc(e.when || '--') + "</span>" +
      "</div>");
  }
  if (list.length > out.length) {
    out.push("<div class='acc-session-more' style='color:#94a3b8'>" + accEsc(_t('accessSessionsMore', 'older events')) + ": " + (list.length - out.length) + "</div>");
  }
  return out.join('');
}

function accLoadSessions() {
  var box = document.getElementById('acc_sessions_list');
  return fetch('/api/session-history', {cache: 'no-store'})
    .then(function(r) { return r.json(); })
    .then(function(d) {
      var events = (d && d.events) ? d.events : [];
      if (box) {
        box.innerHTML = events.length
          ? accSessionRows(events, 20)
          : "<div class='acc-session-empty' style='color:#94a3b8'>" + accEsc(_t('accessSessionsEmpty', 'No sessions recorded')) + "</div>";
      }
      return events.length;
    })
    .catch(function() {
      if (box) box.innerHTML = "<div class='acc-session-empty' style='color:#fca5a5'>" + accEsc(_t('accessSessionsLoadFail', 'Could not load the session history')) + "</div>";
      return 0;
    });
}

function accKickAll() {
  customConfirm(_t('accessKickAllConfirm', 'Terminate every web session? Browsers will have to reconnect.'), function(ok) {
    if (!ok) return;
    var btn = document.getElementById('acc_kick_all');
    var msg = document.getElementById('acc_sessions_msg');
    if (btn) disableBtn(btn);
    return fetch('/api/access-kick-all', {method: 'POST'})
      .then(function(r) { return r.json(); })
      .then(function(d) {
        var ok2 = !!(d && d.success);
        if (msg) msg.textContent = _t(ok2 ? 'accessKickAllDone' : 'accessKickAllFail',
                                       ok2 ? 'All sessions terminated' : 'Could not terminate the sessions');
        if (typeof customAlert === 'function') customAlert((ok2 ? '\u2705 ' : '\u274C ') + (msg ? msg.textContent : ''));
        accLoadSessions();
        return ok2;
      })
      .catch(function() {
        if (msg) msg.textContent = _t('accessKickAllFail', 'Could not terminate the sessions');
        return false;
      })
      .finally(function() { if (btn) enableBtn(btn); });
  });
}

window.addEventListener('load', function() {
  accInitTabs();
  loadAccessConfig();
  accLoadSessions();
  var rb = document.getElementById('acc_sessions_refresh');
  if (rb) rb.addEventListener('click', function() { accLoadSessions(); });
  var kb = document.getElementById('acc_kick_all');
  if (kb) kb.addEventListener('click', accKickAll);
});
</script>
)HTML";
