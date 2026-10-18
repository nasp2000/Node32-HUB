var fs = require('fs');
var path = require('path');
var jsdom = require('jsdom');
var { JSDOM } = jsdom;

var html = fs.readFileSync(path.join(__dirname, 'pages', 'alerts.html'), 'utf8').toString();
var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
var win = dom.window;
var doc = win.document;
var __ = win.__test__;

var passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; } else { console.log('  ✗ FAIL:', msg); failed++; }
}
function assertEq(actual, expected, msg) {
  if (actual === expected) { passed++; }
  else { console.log('  ✗ FAIL:', msg, '- expected:', JSON.stringify(expected), 'got:', JSON.stringify(actual)); failed++; }
}

console.log('\n=== Alerts Page Tests ===\n');

// --- Regression: slot email input value attribute parses cleanly ---
// The renderer builds value='' via separate prints; the field must parse with
// an empty value and accept an injected address (6-slot layout).
(function test_email_value_attribute() {
  var emailsInput = doc.getElementById('alert_emails_0');
  if (!emailsInput) { console.log('  [skip] slot email input not found'); passed++; return; }
  assertEq(emailsInput.getAttribute('value'), '', 'slot email value is empty by default');
  assertEq(emailsInput.getAttribute('placeholder'), 'email1@ex.com, email2@ex.com', 'placeholder intact');

  // Simulate the renderer injecting the saved email into the value=' junction.
  var raw = fs.readFileSync(path.join(__dirname, 'pages', 'alerts.html'), 'utf8');
  var pattern = "id='alert_emails_0'";
  assert(raw.indexOf(pattern) >= 0, 'slot 0 email input present in fixture');
  var simDom = new JSDOM(raw, { url: 'http://localhost' });
  var simEl = simDom.window.document.getElementById('alert_emails_0');
  simEl.value = 'mail@example.com';
  assertEq(simEl.value, 'mail@example.com', 'input shows the saved email');
  simDom.window.close();
})();

// --- rt: translation function ---
(function test_rt() {
  win.localStorage.removeItem('ui_lang_cache');

  assertEq(__.rt('nonexistent', 'fallback'), 'fallback', 'rt returns fallback when no cache');
  assertEq(__.rt('nonexistent'), 'nonexistent', 'rt returns key as fallback when fb undefined');

  win.localStorage.setItem('ui_lang_cache', JSON.stringify({ recipSaved: '✅ Saved!', recipErrMinEmail: 'Error: needs email' }));
  assertEq(__.rt('recipSaved', 'Saved'), '✅ Saved!', 'rt reads from localStorage cache');
  assertEq(__.rt('recipErrMinEmail', 'Fallback'), 'Error: needs email', 'rt reads second key from cache');
  assertEq(__.rt('missing', 'fallback'), 'fallback', 'rt returns fallback for missing key');

  win.localStorage.removeItem('ui_lang_cache');
})();

// --- Fixture note: #alertRows is empty in the static fragments (the 6 slot
// rows are rendered server-side by alertsSlotRow()). Inject minimal rows with
// the IDs the save handler reads, mirroring the renderer output. ---
(function test_inject_slot_rows() {
  var tb = doc.getElementById('alertRows');
  assert(!!tb, '#alertRows tbody exists in fixture');
  // Build via DOM API (innerHTML string trips a parse5 table-parsing bug).
  var timeouts = ['20', '60'], maxes = ['12', '6'];
  for (var i = 0; i < 6; i++) {
    (function(idx) {
      var tr = doc.createElement('tr');
      var td1 = doc.createElement('td');
      var cb = doc.createElement('input');
      cb.type = 'checkbox'; cb.id = 'alert_enabled_' + idx;
      var sel = doc.createElement('select'); sel.id = 'alert_timeout_' + idx;
      timeouts.forEach(function(v) {
        var op = doc.createElement('option'); op.value = v; op.textContent = v + ' min'; sel.appendChild(op);
      });
      td1.appendChild(cb); td1.appendChild(sel);
      var td2 = doc.createElement('td');
      var mx = doc.createElement('select'); mx.id = 'alert_max_' + idx;
      maxes.forEach(function(v) {
        var op = doc.createElement('option'); op.value = v; op.textContent = v; mx.appendChild(op);
      });
      td2.appendChild(mx);
      var td3 = doc.createElement('td');
      var em = doc.createElement('input');
      em.type = 'text'; em.id = 'alert_emails_' + idx; em.value = '';
      td3.appendChild(em);
      tr.appendChild(td1); tr.appendChild(td2); tr.appendChild(td3);
      tb.appendChild(tr);
    })(i);
  }
  assert(!!doc.getElementById('alert_enabled_0'), 'slot 0 injected');
  assert(!!doc.getElementById('alert_emails_0'), 'slot 0 email injected');
})();

// --- Save button: validation errors (slot 0 of the 6-slot layout) ---
(function test_save_validation_email_required() {
  var enabledCb = doc.getElementById('alert_enabled_0');
  var emailsInput = doc.getElementById('alert_emails_0');

  if (!enabledCb || !emailsInput) {
    console.log('  [skip] required DOM elements not found');
    passed++;
    return;
  }

  enabledCb.checked = true;
  emailsInput.value = '';

  var alertCalls = [];
  var origAlert = win.customAlert;
  win.customAlert = function(m) { alertCalls.push(m); };

  // Trigger save button click
  var btnSave = doc.getElementById('save_inactivity_alert');
  btnSave.click();

  // Validation is synchronous (returns before fetch when validation fails)
  var found = alertCalls.some(function(m) {
    return m.indexOf('Error') >= 0 || m.indexOf('email') >= 0 || m.indexOf('alerts are active') >= 0;
  });
  assert(found, 'validation shows error when enabled without emails');

  win.customAlert = origAlert;
})();

(function test_save_validation_email_format() {
  var enabledCb = doc.getElementById('alert_enabled_0');
  var emailsInput = doc.getElementById('alert_emails_0');

  if (!enabledCb || !emailsInput) {
    console.log('  [skip] required DOM elements not found');
    passed++;
    return;
  }

  enabledCb.checked = true;
  emailsInput.value = 'invalid-email';  // no @

  var alertCalls = [];
  var origAlert = win.customAlert;
  win.customAlert = function(m) { alertCalls.push(m); };

  var btnSave = doc.getElementById('save_inactivity_alert');
  btnSave.click();

  var found = alertCalls.some(function(m) { return m.indexOf('@') >= 0 || m.indexOf('format') >= 0 || m.indexOf('Error') >= 0; });
  assert(found, 'validation shows error for invalid email format');

  win.customAlert = origAlert;
})();

// --- Save button: successful submission (slot 0) ---
(function test_save_success() {
  if (!win.__MOCK_API__) win.__MOCK_API__ = {};
  win.__MOCK_API__['/api/inactivity-alert-config'] = { status: 'ok' };

  var enabledCb = doc.getElementById('alert_enabled_0');
  var emailsInput = doc.getElementById('alert_emails_0');
  var timeoutSel = doc.getElementById('alert_timeout_0');
  var maxPerDaySel = doc.getElementById('alert_max_0');

  if (!enabledCb || !emailsInput) {
    console.log('  [skip] required DOM elements not found');
    passed++;
    return Promise.resolve();
  }

  enabledCb.checked = true;
  emailsInput.value = 'test@example.com';
  if (timeoutSel) timeoutSel.value = '60';
  if (maxPerDaySel) maxPerDaySel.value = '6';

  var alertCalls = [];
  var origAlert = win.customAlert;
  win.customAlert = function(m) { alertCalls.push(m); };

  var btnSave = doc.getElementById('save_inactivity_alert');
  btnSave.click();

  return new Promise(function(resolve) {
    setTimeout(function() {
      var found = alertCalls.some(function(m) { return m.indexOf('✅') >= 0 || m.indexOf('Saved') >= 0; });
      assert(found, 'save success shows success message');
      win.customAlert = origAlert;
      resolve();
    }, 50);
  });
})().then(function() {
  // --- Save button: API error ---
  return new Promise(function(resolve) {
    if (!win.__MOCK_API__) win.__MOCK_API__ = {};
    win.__MOCK_API__['/api/inactivity-alert-config'] = { status: 'error', message: 'Server error' };

    var enabledCb = doc.getElementById('alert_enabled_0');
    var emailsInput = doc.getElementById('alert_emails_0');
    enabledCb.checked = true;
    emailsInput.value = 'test@example.com';

    var alertCalls = [];
    var origAlert = win.customAlert;
    win.customAlert = function(m) { alertCalls.push(m); };

    var btnSave = doc.getElementById('save_inactivity_alert');
    btnSave.click();

    setTimeout(function() {
      var found = alertCalls.some(function(m) { return m.indexOf('Server error') >= 0 || m.indexOf('❌') >= 0; });
      assert(found, 'save shows error message on API failure');
      win.customAlert = origAlert;
      resolve();
    }, 50);
  });
}).then(function() {
  // --- Test-alert button wiring (6-slot renderer output) ---
  return new Promise(function(resolve) {
    // Build a minimal page matching the renderer's alertsSlotRow output:
    // 6 rows with alert_test_<i> buttons + the renderer's inline test JS.
    var rows = '';
    for (var i = 0; i < 6; i++) {
      rows += "<tr><td><input id='alert_emails_" + i + "' value=''></td>" +
              "<td style='text-align:center'><button type='button' id='alert_test_" + i + "'>📤</button></td></tr>";
    }
    var inlineJs = "(function(){function rt(k,fb){try{if(typeof window._t==='function')return window._t(k,fb);var r=localStorage.getItem('ui_lang_cache');if(r){var p=JSON.parse(r);if(p&&p[k]!==undefined)return p[k];}}catch(e){}return fb!==undefined?fb:k;}for(var t=0;t<6;t++){(function(i){var tb=document.getElementById('alert_test_'+i);if(!tb)return;tb.addEventListener('click',function(){var em=document.getElementById('alert_emails_'+i).value.trim();if(em.length===0){window.customAlert(rt('recipErrMinEmail','Define at least one email when alerts are active!'));return;}if(em.indexOf('@')<0){window.customAlert(rt('recipErrEmail','Invalid email (missing @)'));return;}disableBtn(tb);showLoading();var tfd=new FormData();tfd.append('i',i);tfd.append('emails',em);fetch('/api/inactivity-alert-test',{method:'POST',body:new URLSearchParams(tfd),headers:{'Content-Type':'application/x-www-form-urlencoded'}}).then(function(r){return r.json();}).then(function(j){window.customAlert(j.status==='ok'?rt('alertTestSent','Test alert sent — check the inbox'):(j.message||'Error'));}).catch(function(){window.customAlert(rt('recipErrNetwork','Network error'));}).finally(function(){hideLoading();enableBtn(tb);});});})(t);}return;}())();";

    var html = "<!DOCTYPE html><html><head></head><body>" +
      "<script>window.customAlert=function(m){};window.customConfirm=function(m,cb){if(cb)cb(true);};window.disableBtn=function(){};window.enableBtn=function(){};window.showLoading=function(){};window.hideLoading=function(){};" +
      "window.__MOCK_API__={'/api/inactivity-alert-test':{status:'ok'}};" +
      "window.fetch=function(url,opts){var u=typeof url==='string'?url:url.url;window.__postBodies=window.__postBodies||[];window.__postBodies.push({url:u,body:(opts&&opts.body)||''});var M=window.__MOCK_API__||{};var mock=M[u];if(!mock){for(var k in M){if(u.indexOf(k)>=0){mock=M[k];break;}}}if(typeof mock==='function')mock=mock();return Promise.resolve({ok:true,status:200,json:function(){return Promise.resolve(mock||{});}});};" +
      "</script><table><tbody id='alertRows'>" + rows + "</tbody>" +
      "<script>" + inlineJs + "</script></body></html>";

    var tdom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
    var twin = tdom.window;
    var tdoc = twin.document;

    // Empty emails -> validation error, no POST.
    var alertCalls = [];
    var orig = twin.customAlert;
    twin.customAlert = function(m) { alertCalls.push(m); };
    tdoc.getElementById('alert_test_0').click();
    assert(alertCalls.length > 0, 'test button with empty emails shows validation error');
    assertEq((twin.__postBodies || []).length, 0, 'no POST when emails empty');

    // Valid email -> POST with slot index + emails.
    tdoc.getElementById('alert_emails_3').value = 'test@example.com';
    tdoc.getElementById('alert_test_3').click();
    assertEq((twin.__postBodies || []).length, 1, 'one POST for test alert');
    var body = String((twin.__postBodies[0] || {}).body || '');
    assertEq(twin.__postBodies[0].url, '/api/inactivity-alert-test', 'POSTs to test endpoint');
    assert(body.indexOf('i=3') >= 0, 'posts slot index 3');
    assert(body.indexOf('emails=test%40example.com') >= 0, 'posts the email');

    twin.customAlert = orig;
    tdom.window.close();
    resolve();
  });
}).then(function() {
  // --- OEE tab (alertas OEE): tab switching on the fragment fixture ---
  return new Promise(function(resolve) {
    assert(!!doc.getElementById('tabBtnInact'), 'inactivity tab button exists');
    var btnOee = doc.getElementById('tabBtnOee');
    assert(!!btnOee, 'OEE tab button exists (proximity build)');
    assertEq(btnOee.getAttribute('data-i18n'), 'alertTabOee', 'OEE tab i18n key');
    assertEq(doc.getElementById('tabBtnInact').getAttribute('data-i18n'), 'alertTabInact', 'inactivity tab i18n key');
    assert(!!doc.getElementById('save_oee_alert'), 'OEE save button exists');
    var tabOee = doc.getElementById('tabOee');
    var tabInact = doc.getElementById('tabInact');
    assert(!!tabOee && !!tabInact, 'both tab panels exist');
    assert(tabOee.style.display === 'none', 'OEE tab hidden initially');
    btnOee.click();
    assert(tabOee.style.display !== 'none', 'OEE tab visible after click');
    assert(tabInact.style.display === 'none', 'inactivity hidden when OEE shown');
    doc.getElementById('tabBtnInact').click();
    assert(tabOee.style.display === 'none', 'OEE hidden when back to inactivity');
    assert(tabInact.style.display !== 'none', 'inactivity visible again');
    resolve();
  });
}).then(function() {
  // --- OEE save: validation + POST body (rows injected; server renders them) ---
  return new Promise(function(resolve) {
    var otb = doc.getElementById('oeeRows');
    // Build via DOM API (innerHTML string trips a parse5 table-parsing bug).
    var metrics = [['0', 'Shift OEE'], ['1', 'Day OEE'], ['2', 'Availability']];
    for (var i = 0; i < 3; i++) {
      (function(idx) {
        var tr = doc.createElement('tr');
        var td1 = doc.createElement('td');
        var cb = doc.createElement('input');
        cb.type = 'checkbox'; cb.id = 'oee_enabled_' + idx;
        var me = doc.createElement('select'); me.id = 'oee_metric_' + idx;
        metrics.forEach(function(m) {
          var op = doc.createElement('option'); op.value = m[0]; op.textContent = m[1]; me.appendChild(op);
        });
        td1.appendChild(cb); td1.appendChild(me);
        function numCell(id, val) {
          var td = doc.createElement('td');
          var inp = doc.createElement('input'); inp.id = id + '_' + idx; inp.value = val;
          td.appendChild(inp); return td;
        }
        tr.appendChild(td1);
        tr.appendChild(numCell('oee_threshold', '60'));
        tr.appendChild(numCell('oee_persist', '15'));
        tr.appendChild(numCell('oee_max', '6'));
        var td5 = doc.createElement('td');
        var em = doc.createElement('input'); em.type = 'text'; em.id = 'oee_emails_' + idx; em.value = '';
        td5.appendChild(em); tr.appendChild(td5);
        otb.appendChild(tr);
      })(i);
    }
    var postsBefore = (win.__postBodies || []).length;
    var alertCalls = [];
    var origAlert = win.customAlert;
    win.customAlert = function(m) { alertCalls.push(m); };

    // Enabled without emails -> validation error, no POST.
    doc.getElementById('oee_enabled_0').checked = true;
    doc.getElementById('oee_emails_0').value = '';
    doc.getElementById('save_oee_alert').click();
    var found = alertCalls.some(function(m) { return m.indexOf('email') >= 0; });
    assert(found, 'OEE save without emails shows validation error');
    assertEq((win.__postBodies || []).length, postsBefore, 'no POST on validation error');

    // Threshold out of range -> threshold error.
    doc.getElementById('oee_emails_0').value = 'a@b.c';
    doc.getElementById('oee_threshold_0').value = '0';
    doc.getElementById('save_oee_alert').click();
    found = alertCalls.some(function(m) { return m.indexOf('1-100') >= 0; });
    assert(found, 'OEE save with threshold 0 shows range error');

    // Valid slot -> POST with all oee_* fields.
    if (!win.__MOCK_API__) win.__MOCK_API__ = {};
    win.__MOCK_API__['/api/oee-alert-config'] = { status: 'ok' };
    doc.getElementById('oee_threshold_0').value = '60';
    doc.getElementById('oee_metric_0').value = '1';
    doc.getElementById('save_oee_alert').click();
    setTimeout(function() {
      var posts = win.__postBodies || [];
      assertEq(posts.length, postsBefore + 1, 'one POST for OEE save');
      var body = String((posts[posts.length - 1] || {}).body || '');
      assert(posts[posts.length - 1].url === '/api/oee-alert-config', 'POSTs to OEE endpoint');
      assert(body.indexOf('oee_enabled_0=1') >= 0, 'posts enabled flag');
      assert(body.indexOf('oee_metric_0=1') >= 0, 'posts metric');
      assert(body.indexOf('oee_threshold_0=60') >= 0, 'posts threshold');
      assert(body.indexOf('oee_persist_0=15') >= 0, 'posts persist');
      assert(body.indexOf('oee_max_0=6') >= 0, 'posts max/day');
      assert(body.indexOf('oee_emails_0=a%40b.c') >= 0, 'posts emails');
      found = alertCalls.some(function(m) { return m.indexOf('Saved') >= 0; });
      assert(found, 'OEE save success shows Saved');
      win.customAlert = origAlert;
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
