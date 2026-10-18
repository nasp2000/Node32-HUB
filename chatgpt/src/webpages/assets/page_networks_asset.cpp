// page_networks_asset.cpp
// PROGMEM HTML asset for the /networks page (Varias Redes).
// Contains only the page body fragment — no DOCTYPE/html/head/body tags.
// The shell (head, nav, footer) is injected by buildPageFrame() at first request.
#include "page_networks_asset.h"

const char PAGE_NETWORKS[] PROGMEM = R"HTML(
<div class='page networks'>
<div class='wrap'>
<div class='actions nav-inicio'>
  <a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='netTipHome'>&#127968; Home</a>
</div>
<h1 data-i18n='netTelnetPageTitle'>&#127760; Other Networks</h1>

<!-- ── TELNET ──────────────────────────────────────────────── -->
<div id='netTelnetCard' style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2 style='margin-top:0;color:#60a5fa' data-i18n='netTelnetTitle'>&#128187; Telnet</h2>
<div style='font-size:13px;color:#94a3b8;margin-bottom:14px' data-i18n='netTelnetDesc'>Remote console access for debugging and monitoring.</div>
<form id='telnet-form'>
<label style='display:flex;align-items:center;gap:10px;margin-bottom:14px'>
  <input type='hidden' name='telnet_enabled_present' value='1'>
  <input type='checkbox' name='telnet_enabled' id='telnet_enabled_chk' title='Enable telnet remote console' data-i18n-title='netTipTelnetEnable'>
  <span data-i18n='netTelnetEnable'>Enable Telnet</span>
</label>
<label style='display:block;margin-bottom:4px'>
  <span><span data-i18n='netTelnetPwdLbl'>Telnet Password</span> <span style='font-size:12px;color:#94a3b8' data-i18n='netTelnetPwdHint'>(leave empty to keep current)</span></span>
  <input type='password' name='telnet_password_new' id='telnet_password_new' autocomplete='new-password'
    placeholder='(unchanged)' title='New telnet password' data-i18n-title='netTipTelnetPwd'
    style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</label>
<div style='text-align:center;margin-top:18px'>
  <button type='submit' class='btn' style='padding:10px 32px' data-i18n='btnSave' title='Save telnet configuration' data-i18n-title='netTipSave'>&#128190; Save</button>
</div>
</form>
</div>

</div>
</div>

<script>
function t(k,fb){try{if(typeof window._t==='function')return window._t(k,fb);var raw=localStorage.getItem('ui_lang_cache');if(raw){var p=JSON.parse(raw);if(p&&p[k]!==undefined)return p[k];}}catch(e){}return fb!==undefined?fb:k;}
function loadTelnetState() {
  var pp = document.getElementById('telnet_password_new');
  if (pp) pp.placeholder = t('netTelnetPwdPh', '(unchanged)');
  // Telnet card only exists where ENABLE_TELNET=1 (status carries `enabled`).
  // When the backend is absent (501 {"status":"disabled"}) the card is hidden.
  fetch('/api/telnet-status', {cache:'no-cache'})
    .then(function(r) { return r.json(); })
    .then(function(d) {
      var tc = document.getElementById('netTelnetCard');
      if (tc) tc.style.display = (d && d.enabled !== undefined) ? '' : 'none';
      var cb = document.getElementById('telnet_enabled_chk');
      if (cb && d.enabled !== undefined) cb.checked = d.enabled;
    })
    .catch(function() {
      var tc = document.getElementById('netTelnetCard');
      if (tc) tc.style.display = 'none';
    });
}

document.getElementById('telnet-form').addEventListener('submit', function(e) {
  e.preventDefault();
  var btn=this.querySelector('button[type="submit"]');disableBtn(btn);showLoading();
  var fd = new FormData(this);
  fetch('/api/telnet-config', {method:'POST', body:new URLSearchParams(fd), headers:{'Content-Type':'application/x-www-form-urlencoded'}})
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (d.success) {
        if (typeof customAlert === 'function') customAlert(d.message || t('netConfigSaved','\u2705 Configuration saved!'));
        if (typeof showSavedCard === 'function') showSavedCard();
      } else {
        if (typeof customAlert === 'function') customAlert(t('netErrSaving','\u274c Error: ') + (d.message || t('netUnknown','unknown')));
      }
    })
    .catch(function(err) { if (typeof customAlert === 'function') customAlert(t('netErrSaving','\u274c Error: ') + (err.message || err)); })
    .finally(function(){hideLoading();enableBtn(btn);});
});

window.addEventListener('load', loadTelnetState);
</script>
)HTML";
