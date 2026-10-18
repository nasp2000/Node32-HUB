#include "page_email_asset.h"

const char PAGE_EMAIL_BODY[] PROGMEM = R"EMAIL(
<div class='page'>
<div class='wrap'>
<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' data-i18n-title='emailTipHome' title='&#127968; Go to home page'>&#127968; Home</a></div>
<h1 data-i18n='emailPageTitle'>&#9993;&#65039; E-mail</h1>
<style>.section{margin-bottom:6px}.section h3{margin:0 0 2px 0;font-size:13px;color:#e0e7ff}.section-desc{font-size:11px;color:#94a3b8;margin-bottom:2px}.section input,.section input[type=number]{width:100%;padding:7px 10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px;box-sizing:border-box;margin-top:0}.section label{font-size:13px;color:#e0e7ff}</style>

<div style='border:2px solid #3b82f6;border-radius:8px;padding:10px 14px;background:#0f172a;margin-top:14px'>
<h2 style='margin:0;color:#60a5fa;font-size:14px' data-i18n='infoTitle'>&#8505;&#65039; Information</h2>
<div style='font-size:13px;line-height:1.5;color:#e0e7ff;margin-top:4px'>
<div data-i18n-html='emailSmtpInfo'>&#128231; This page configures <strong>Direct SMTP</strong> only and lets you test sending.</div>
</div>
</div>

<form id='send-config-form' action='/api/config' method='POST'>
<input type='hidden' name='_html' value='1'>
<div style='border:2px solid #10b981;border-radius:8px;padding:12px 14px;background:#0f172a;margin-top:14px'>
<h2 style='margin:0 0 6px 0;color:#10b981;font-size:14px' data-i18n='emailSmtpTitle'>&#128231; Direct SMTP Configuration</h2>
<div style='font-size:12px;color:#94a3b8;margin-bottom:8px;padding:6px 10px;background:#0a1628;border-left:3px solid #10b981;border-radius:4px' data-i18n='emailSmtpDesc'>Configure here if using direct SMTP. For Gmail with 2FA, use an App Password.</div>

<div class='section'>
<h3 data-i18n='emailSmtpServerLbl'>Servidor SMTP</h3>
<div class='section-desc' data-i18n='emailSmtpServerDesc'>&#128231; Email server address</div>
<input name='smtp_server' id='smtp_server' placeholder='smtp.gmail.com' data-i18n-title='emailTipServer' title='SMTP server address'>
</div>
<div class='section'>
<h3 data-i18n='emailPortLbl'>Porta</h3>
<div class='section-desc' data-i18n='emailPortDesc'>&#128268; Porta do servidor SMTP</div>
<input type='number' name='smtp_port' id='smtp_port' placeholder='465' data-i18n-title='emailTipPort' title='SMTP port (usually 465 or 587)'>
</div>
<div class='section'>
<h3 data-i18n='emailUserLbl'>User</h3>
<div class='section-desc' data-i18n='emailUserDesc'>&#128100; Email address used for SMTP authentication</div>
<input name='smtp_user' id='smtp_user' placeholder='user@gmail.com' data-i18n-title='emailTipUser' title='SMTP username'>
</div>
<div class='section'>
<h3 data-i18n='emailDestLbl'>Email Destino</h3>
<div class='section-desc' data-i18n='emailDestDesc'>&#128238; Email that will receive the data sent by the device</div>
<input name='email_destination' id='email_destination' placeholder='sender@example.com' data-i18n-title='emailTipSender' title='Sender email address'>
</div>
<div class='section'>
<h3 data-i18n='emailPassLbl'>Senha</h3>
<div class='section-desc' data-i18n='emailPassDesc'>&#128273; Password do email (Gmail com 2FA: App Password)</div>
<input type='password' name='smtp_pass' id='smtp_pass' placeholder='Password' data-i18n-title='emailTipPassword' title='SMTP password'>
</div>
<div class='section'>
<h3 data-i18n='emailTlsLbl'>TLS Inseguro</h3>
<div class='section-desc' data-i18n='emailTlsDesc'>&#128274; Allows TLS connections without certificate verification</div>
<label><input type='checkbox' name='smtp_tls_insecure' id='smtp_tls_insecure' checked data-i18n-title='emailTipTls' title='Allow unverified TLS connections'> <span data-i18n='emailTlsEnable'>Ativar TLS inseguro</span></label>
</div>
</div>

<div style='text-align:center;margin-top:10px'>
<button type='submit' form='send-config-form' class='btn' data-i18n='btnSave' data-i18n-title='emailTipSave' title='Save email settings'>&#128190; Save</button>
</div>
</form>

<div style='border:2px solid #8b5cf6;border-radius:8px;padding:10px 14px;background:#0f172a;margin-top:14px'>
<h2 style='margin:0 0 4px 0;color:#8b5cf6;font-size:14px' data-i18n='emailTestTitle'>&#129514; Teste de Email</h2>
<div style='font-size:12px;color:#94a3b8;margin-bottom:8px;padding:6px 10px;background:#0a1628;border-left:3px solid #8b5cf6;border-radius:4px' data-i18n='emailTestDesc'>Test email sending with the settings above.</div>
<button id='btn-test-email' type='button' class='btn' style='width:100%;background:#7c3aed;border:1px solid #7c3aed;padding:8px' data-i18n='emailTestBtn' data-i18n-title='emailTipTest' title='Send test email'>&#9993;&#65039; Send Test Email</button>
</div>
</div>
</div>

<script>
function et(k, fb) {
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

function testEmail() {
	var btn=document.getElementById('btn-test-email');disableBtn(btn);showLoading();
	fetch('/api/email-test', { method: 'POST', headers: { 'Content-Type': 'application/json' } })
		.then(function(r) { return r.json(); })
		.then(function(d) {
			if (d.success) {
				customAlert(d.message || et('emailTestStarted', 'Teste iniciado com sucesso!'));
			} else {
				customAlert(d.message || et('emailTestStartError', 'Error starting test'));
			}
		})
		.catch(function(e) {
			customAlert(et('emailConnectionErrorPrefix', 'Connection error: ') + (e && e.message ? e.message : e));
		})
		.finally(function(){hideLoading();enableBtn(btn);});
}

document.getElementById('btn-test-email').addEventListener('click', testEmail);

fetch('/api/config?section=email',{cache:'no-store'})
	.then(function(r) { return r.json(); })
	.then(function(d) {
		var smtp = d.smtp || {};
		var email = d.email || {};
		document.getElementById('smtp_server').value = smtp.server || '';
		document.getElementById('smtp_port').value = smtp.port || '465';
		document.getElementById('smtp_user').value = smtp.username || '';
		document.getElementById('email_destination').value = email.destination || d.email_destination || '';
		document.getElementById('smtp_tls_insecure').checked = (smtp.tls_insecure !== false);
	});

document.getElementById('send-config-form').addEventListener('submit', function(e) {
	e.preventDefault();
	var btn=this.querySelector('button[type="submit"]');disableBtn(btn);showLoading();
	var fd = new FormData(this);
	fd.set('section', 'email');
	var tlsCb = document.getElementById('smtp_tls_insecure');
	if (tlsCb) fd.set('smtp_tls_insecure', tlsCb.checked ? '1' : '0');

	fetch(this.action, {
		method: 'POST',
		body: new URLSearchParams(fd),
		headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
	})
		.then(function(r) { return r.json(); })
		.then(function(d) {
			if (d.success) {
				customAlert(et('emailConfigSaved', '✅ Configuration saved!'));
				if (typeof showSavedCard === 'function') showSavedCard();
			} else {
				customAlert(et('emailErrorPrefix', '❌ Error: ') + (d.message || et('emailUnknown', 'unknown')));
			}
		})
		.catch(function(err) {
			customAlert(et('emailErrorPrefix', '❌ Error: ') + (err && err.message ? err.message : err));
		})
		.finally(function(){hideLoading();enableBtn(btn);});
});
</script>
)EMAIL";