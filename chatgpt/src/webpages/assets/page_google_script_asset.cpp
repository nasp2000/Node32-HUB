#include "page_google_script_asset.h"

const char PAGE_GOOGLE_SCRIPT[] PROGMEM = R"HTML(
<div class='page google-script'>
<div class='wrap'>
<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='gasTipHome'>&#127968; Home</a></div>
<h1 data-i18n='gasPageTitle'>&#127760; Google App Script</h1>

<form id='gas-config-form' action='/api/config' method='POST'>
<input type='hidden' name='_html' value='1'>
<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2 style='margin-top:0;color:#60a5fa' data-i18n='emailGeneralTitle'>&#9881;&#65039; General Settings</h2>
<div class='section'>
<h3 data-i18n='emailMethodLbl'>Sending Method</h3>
<div class='section-desc' data-i18n='emailMethodDesc'>&#128260; Choose the email sending method</div>
<select name='email_method' id='email_method' title='Select email sending method' data-i18n-title='gasTipMethod' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
<option value='smtp' data-i18n='emailMethodSmtp'>&#128231; SMTP Direto</option>
<option value='google' data-i18n='emailMethodGoogle'>&#129514; Google Apps Script</option>
</select>
</div>
<div class='section'>
<h3 data-i18n='emailDestLbl'>Email Destino</h3>
<div class='section-desc' data-i18n='emailDestDesc'>&#128238; Email that will receive the data sent by the device</div>
<input name='email_dest' id='email_dest' placeholder='destino@email.com' title='Email address to receive data' data-i18n-title='gasTipDest' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</div>
</div>

<div style='border:2px solid #f59e0b;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2 style='margin-top:0;color:#f59e0b' data-i18n='emailGoogleTitle'>&#129514; Google Apps Script</h2>
<div style='font-size:13px;color:#94a3b8;margin-bottom:12px;padding:10px;background:#0a1628;border-left:3px solid #f59e0b;border-radius:4px' data-i18n-html='emailGoogleAdvInfo'><strong>Vantagens:</strong> Consome menos RAM do que SMTP.<br><strong>Como usar:</strong> 1) Cole o script abaixo em script.google.com, 2) Deploy como Web App, 3) Cole a URL no campo abaixo.</div>
<div class='section'>
<h3 data-i18n='emailGasUrlLbl'>Google Apps Script URL</h3>
<div class='section-desc' data-i18n='emailGasUrlDesc'>&#127760; URL do seu Google Apps Script Web App</div>
<input name='google_script_url' id='google_script_url' placeholder='https://script.google.com/macros/s/...' title='Google Apps Script Web App URL' data-i18n-title='gasTipUrl' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
</div>
<div class='section' style='margin-top:20px'>
<div style='display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:12px'>
<h3 style='color:#f59e0b;margin:0;font-size:16px' data-i18n='emailGasCodeTitle'>&#128220; Google Apps Script Code</h3>
<a href='/GoogleAppsScript_EmailHandler.js' target='_blank' download class='btn' data-i18n='emailGoogleCodeDownload'>&#128229; Download Code</a>
</div>
<div style='font-size:13px;color:#94a3b8;line-height:1.6;padding:12px;background:#0a1628;border:1px solid #374151;border-radius:6px' data-i18n-html='emailGoogleInstructions'>&#128229; Download the file and paste it at script.google.com. Deploy as a Web App and paste the URL in the field above.</div>
</div>
</div>

<div style='text-align:center;margin-top:18px'>
<button type='submit' form='gas-config-form' class='btn' data-i18n='btnSave' title='Save email configuration' data-i18n-title='gasTipSave'>&#128190; Save</button>
</div>
</form>

<div style='border:2px solid #8b5cf6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2 style='margin-top:0;color:#8b5cf6' data-i18n='emailTestTitle'>&#129514; Teste de Email</h2>
<div style='font-size:13px;color:#94a3b8;margin-bottom:12px;padding:10px;background:#0a1628;border-left:3px solid #8b5cf6;border-radius:4px' data-i18n='emailTestDesc'>Test email sending with the settings above.</div>
<button id='btn-test-email-gas' type='button' class='btn' style='width:100%;background:#7c3aed;border:1px solid #7c3aed' data-i18n='emailTestBtn' title='Send a test email' data-i18n-title='gasTipTest'>&#9993;&#65039; Send Test Email</button>
</div>
</div>
</div>

<script>
function gasText(k, fb) {
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

function testEmailFromGas() {
  var btn=document.getElementById('btn-test-email-gas');disableBtn(btn);showLoading();
  fetch('/api/email-test', { method: 'POST', headers: { 'Content-Type': 'application/json' } })
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (d.success) {
        customAlert(d.message || gasText('emailTestStarted', 'Test started successfully!'));
      } else {
        customAlert(d.message || gasText('emailTestStartError', 'Error starting test'));
      }
    })
    .catch(function(e) {
      customAlert(gasText('emailConnectionErrorPrefix', 'Connection error: ') + (e && e.message ? e.message : e));
    })
    .finally(function(){hideLoading();enableBtn(btn);});
}

document.getElementById('btn-test-email-gas').addEventListener('click', testEmailFromGas);

fetch('/api/config?section=email',{cache:'no-store'})
  .then(function(r) { return r.json(); })
  .then(function(d) {
    var email = d.email || {};
    var gs = d.google_email || {};
    document.getElementById('email_method').value = email.method || 'google';
    document.getElementById('email_dest').value = email.destination || '';
    document.getElementById('google_script_url').value = gs.google_script_url || '';
  });

document.getElementById('gas-config-form').addEventListener('submit', function(e) {
  e.preventDefault();
  var btn=this.querySelector('button[type="submit"]');disableBtn(btn);showLoading();
  var fd = new FormData(this);
  fd.set('section', 'email');
  fetch(this.action, {
    method: 'POST',
    body: new URLSearchParams(fd),
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
  })
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (d.success) {
        customAlert('✅ ' + gasText('alertSaved', 'Configuration saved!'));
        if (typeof showSavedCard === 'function') showSavedCard();
      } else {
        customAlert('❌ ' + gasText('emailErrorPrefix', 'Error: ') + (d.message || gasText('accessUnknown', 'unknown')));
      }
    })
    .catch(function(err) {
      customAlert('❌ ' + gasText('emailErrorPrefix', 'Error: ') + (err && err.message ? err.message : err));
    })
    .finally(function(){hideLoading();enableBtn(btn);});
});
</script>
)HTML";
