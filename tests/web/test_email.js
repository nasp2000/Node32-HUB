var fs = require('fs');
var path = require('path');
var jsdom = require('jsdom');
var { JSDOM } = jsdom;

var html = fs.readFileSync(path.join(__dirname, 'pages', 'email.html'), 'utf8').toString();
var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
var win = dom.window;
var doc = win.document;

// Global functions (not inside IIFE) — accessible via win
var et = win.et;
var testEmail = win.testEmail;

var passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; } else { console.log('  ✗ FAIL:', msg); failed++; }
}
function assertEq(actual, expected, msg) {
  if (actual === expected) { passed++; }
  else { console.log('  ✗ FAIL:', msg, '- expected:', JSON.stringify(expected), 'got:', JSON.stringify(actual)); failed++; }
}

console.log('\n=== Email Page Tests ===\n');

// --- et: translation function ---
(function test_et() {
  win.localStorage.removeItem('ui_lang_cache');

  assertEq(et('nonexistent', 'fallback'), 'fallback', 'et returns fallback when no cache');
  assertEq(et('nonexistent'), 'nonexistent', 'et returns key as fallback when fb undefined');

  win.localStorage.setItem('ui_lang_cache', JSON.stringify({ emailPageTitle: 'E-mail Title', emailTestBtn: 'Send Test' }));
  assertEq(et('emailPageTitle', 'fallback'), 'E-mail Title', 'et reads from localStorage cache');
  assertEq(et('emailTestBtn', 'Test'), 'Send Test', 'et reads second key from cache');
  assertEq(et('missing_key', 'fallback'), 'fallback', 'et returns fallback for missing key');

  win.localStorage.removeItem('ui_lang_cache');
})();

// --- testEmail: success case ---
(function test_testEmail_success() {
  if (!win.__MOCK_API__) win.__MOCK_API__ = {};
  win.__MOCK_API__['/api/email-test'] = { success: true, message: 'Email enviado com sucesso!' };

  var alertCalls = [];
  var origAlert = win.customAlert;
  win.customAlert = function(m) { alertCalls.push(m); };

  testEmail();

  return new Promise(function(resolve) {
    setTimeout(function() {
      var found = alertCalls.some(function(m) { return m.indexOf('Email enviado') >= 0; });
      assert(found, 'testEmail shows success message');
      win.customAlert = origAlert;
      resolve();
    }, 50);
  });
})().then(function() {
  // --- testEmail: failure case ---
  return new Promise(function(resolve) {
    if (!win.__MOCK_API__) win.__MOCK_API__ = {};
  win.__MOCK_API__['/api/email-test'] = { success: false, message: 'SMTP connection failed' };
    var alertCalls2 = [];
    var origAlert2 = win.customAlert;
    win.customAlert = function(m) { alertCalls2.push(m); };

    testEmail();
    setTimeout(function() {
      var found = alertCalls2.some(function(m) { return m.indexOf('SMTP connection failed') >= 0 || m.indexOf('Erro') >= 0; });
      assert(found, 'testEmail shows error message on failure');
      // Also check that the button was re-enabled
      var btn = doc.getElementById('btn-test-email');
      assert(btn.disabled === false, 'button re-enabled after testEmail failure');
      win.customAlert = origAlert2;
      resolve();
    }, 50);
  });
}).then(function() {
  // --- Form fields exist ---
  assert(!!doc.getElementById('smtp_server'), 'smtp_server input exists');
  assert(!!doc.getElementById('smtp_port'), 'smtp_port input exists');
  assert(!!doc.getElementById('smtp_user'), 'smtp_user input exists');
  assert(!!doc.getElementById('email_destination'), 'email_destination input exists');
  assert(!!doc.getElementById('smtp_pass'), 'smtp_pass input exists');
  assert(!!doc.getElementById('smtp_tls_insecure'), 'tls checkbox exists');
  assert(!!doc.getElementById('btn-test-email'), 'test email button exists');

  // --- Form submission: save config ---
  return new Promise(function(resolve) {
    var serverInput = doc.getElementById('smtp_server');
    var portInput = doc.getElementById('smtp_port');
    var userInput = doc.getElementById('smtp_user');
    var destInput = doc.getElementById('email_destination');
    var passInput = doc.getElementById('smtp_pass');

    serverInput.value = 'smtp.example.com';
    portInput.value = '587';
    userInput.value = 'user@example.com';
    destInput.value = 'dest@example.com';
    passInput.value = 'secret';

    if (!win.__MOCK_API__) win.__MOCK_API__ = {};
    win.__MOCK_API__['/api/config'] = { success: true };

    var alertCalls4 = [];
    var origAlert4 = win.customAlert;
    win.customAlert = function(m) { alertCalls4.push(m); };

    var form = doc.getElementById('send-config-form');
    form.dispatchEvent(new win.Event('submit'));

    setTimeout(function() {
      var saved = alertCalls4.some(function(m) { return m.indexOf('Configuração guardada') >= 0 || m.indexOf('✅') >= 0; });
      assert(saved, 'form submit shows save success message');
      win.customAlert = origAlert4;
      resolve();
    }, 50);
  });
}).then(function() {
  dom.window.close();
  console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
  if (failed > 0) process.exit(1);
}).catch(function(e) {
  dom.window.close();
  console.log('  ✗ UNCAUGHT:', e.message);
  console.log(e.stack);
  failed++;
  console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
  process.exit(1);
});
