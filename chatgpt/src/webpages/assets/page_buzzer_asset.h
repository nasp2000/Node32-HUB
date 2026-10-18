// page_buzzer_asset.h
// PROGMEM HTML fragment for the buzzer configuration page.
// Does NOT include DOCTYPE/html/head/body — the shell renderer adds those.

#pragma once
#include <pgmspace.h>

static const char PAGE_BUZZER[] PROGMEM = R"HTML(<main class='page'><div class='wrap'>
<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='btnHome'>&#127968; Home</a></div>
<h1>&#128276; Configure Buzzer</h1>

<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2>&#127897;&#65039; Status and Test</h2>
<div id='buzzerStatus' style='padding:10px 14px;border-radius:6px;background:#1e293b;margin-bottom:14px;font-size:14px'>
  <span id='statusText' style='color:#94a3b8'>Checking...</span>
</div>
<p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0'>Click to test each pattern (buzzer must be active and connected to the configured GPIO):</p>
<div style='display:flex;gap:8px;flex-wrap:wrap'>
  <button onclick='testBeep(1)' class='btn' style='flex:1;min-width:120px' title='Test OK pattern' data-i18n-title='buzzerTipTestOk'>&#10003; OK (1 beep)</button>
  <button onclick='testBeep(2)' class='btn' style='flex:1;min-width:120px' title='Test warning pattern' data-i18n-title='buzzerTipTestWarn'>&#9888; Warning (2 beeps)</button>
  <button onclick='testBeep(3)' class='btn' style='flex:1;min-width:120px' title='Test error pattern' data-i18n-title='buzzerTipTestErr'>&#10006; Error (3 beeps)</button>
  <button onclick='testBeep(5)' class='btn' style='flex:1;min-width:120px' title='Test long beep' data-i18n-title='buzzerTipTestLong'>&#9679; Long (1 beep)</button>
  <button onclick='testBeep(4)' class='btn' style='flex:1;min-width:120px;background:#b91c1c' title='Test alarm (continuous)' data-i18n-title='buzzerTipTestAlarm'>&#9888; Alarm (cont.)</button>
  <button onclick='stopBeep()' class='btn' style='flex:1;min-width:80px;background:#374151' title='Stop all beeps' data-i18n-title='buzzerTipStop'>&#9632; Stop</button>
</div>
</div>

<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
<h2>&#9881;&#65039; Configuration</h2>
<form id='buzzerForm'>

  <div style='margin-bottom:18px'>
    <label style='display:flex;align-items:center;gap:10px;cursor:pointer'>
      <input type='checkbox' id='enabled' name='enabled' title='Enable/disable buzzer' data-i18n-title='buzzerTipEnable'>
      <span><strong>Buzzer active (master)</strong> &mdash; globally disables without changing the options below</span>
    </label>
  </div>

  <div style='margin-bottom:18px'>
    <label style='display:block;margin-bottom:6px;font-weight:600'>Buzzer GPIO pin</label>
    <input type='number' id='pin' name='pin' min='0' max='48' style='width:110px' title='GPIO pin for buzzer' data-i18n-title='buzzerTipPin'>
    <p style='font-size:12px;color:#64748b;margin:4px 0 0 0'>Waveshare 8DI8DO: GPIO 46 (onboard). N16R8: configure the pin connected to the external buzzer.</p>
  </div>

  <h3 style='margin:20px 0 12px 0'>Beep on:</h3>

  <div style='display:flex;flex-direction:column;gap:12px'>
    <label style='display:flex;align-items:flex-start;gap:10px;cursor:pointer'>
      <input type='checkbox' id='on_boot' name='on_boot' style='margin-top:2px' title='Beep on boot complete' data-i18n-title='buzzerTipOnBoot'>
      <span>
        <strong>Boot complete</strong><br>
        <span style='font-size:12px;color:#64748b'>1 beep (OK) when the system finishes booting successfully</span>
      </span>
    </label>
    <label style='display:flex;align-items:flex-start;gap:10px;cursor:pointer'>
      <input type='checkbox' id='on_save' name='on_save' style='margin-top:2px' title='Beep on config saved' data-i18n-title='buzzerTipOnSave'>
      <span>
        <strong>Configuration saved</strong><br>
        <span style='font-size:12px;color:#64748b'>1 beep (OK) when any configuration is saved via web UI</span>
      </span>
    </label>
    <label style='display:flex;align-items:flex-start;gap:10px;cursor:pointer'>
      <input type='checkbox' id='on_alarm' name='on_alarm' style='margin-top:2px' title='Beep on proximity alarm' data-i18n-title='buzzerTipOnAlarm'>
      <span>
        <strong>Proximity alarm active</strong><br>
        <span style='font-size:12px;color:#64748b'>Continuous beeps while a DIN input is in alarm state; stops when resolved</span>
      </span>
    </label>
    <label style='display:flex;align-items:flex-start;gap:10px;cursor:pointer'>
      <input type='checkbox' id='on_sensor_alert' name='on_sensor_alert' style='margin-top:2px' title='Beep on sensor alert' data-i18n-title='buzzerTipOnSensor'>
      <span>
        <strong>Sensor threshold exceeded</strong><br>
        <span style='font-size:12px;color:#64748b'>2 beeps (Warning) when MQ135 or AHT20/BMP280 exceed the configured threshold</span>
      </span>
    </label>
    <label style='display:flex;align-items:flex-start;gap:10px;cursor:pointer'>
      <input type='checkbox' id='on_network' name='on_network' style='margin-top:2px' title='Beep on network change' data-i18n-title='buzzerTipOnNetwork'>
      <span>
        <strong>Network connected / lost</strong><br>
        <span style='font-size:12px;color:#64748b'>1 long beep when WiFi or Ethernet comes online; 2 long beeps when connection is lost</span>
      </span>
    </label>
    <label style='display:flex;align-items:flex-start;gap:10px;cursor:pointer'>
      <input type='checkbox' id='on_error' name='on_error' style='margin-top:2px' title='Beep on critical error' data-i18n-title='buzzerTipOnError'>
      <span>
        <strong>Critical error</strong><br>
        <span style='font-size:12px;color:#64748b'>3 quick beeps (Error) on SD failure, I2C timeout, or other hardware error</span>
      </span>
    </label>
  </div>

  <div class='actions' style='margin-top:24px'>
    <button type='button' onclick='saveBuzzer()' class='btn' title='Save buzzer settings' data-i18n-title='buzzerTipSave'>&#128190; Save</button>
  </div>
</form>
</div>

<script>
(function() {
  fetch('/api/buzzer-config', {cache:'no-store'})
    .then(function(r) { return r.json(); })
    .then(function(d) {
      document.getElementById('enabled').checked = !!d.enabled;
      document.getElementById('pin').value = d.pin !== undefined ? d.pin : 46;
      document.getElementById('on_boot').checked = !!d.on_boot;
      document.getElementById('on_save').checked = !!d.on_save;
      document.getElementById('on_alarm').checked = !!d.on_alarm;
      document.getElementById('on_sensor_alert').checked = !!d.on_sensor_alert;
      document.getElementById('on_network').checked = !!d.on_network;
      document.getElementById('on_error').checked = !!d.on_error;
      var st = document.getElementById('statusText');
      if (d.enabled) {
        st.textContent = 'Buzzer active — GPIO ' + d.pin;
        st.style.color = '#4ade80';
      } else {
        st.textContent = 'Buzzer disabled (master off)';
        st.style.color = '#94a3b8';
      }
    })
    .catch(function() {});
})();

function saveBuzzer() {
  var btn=document.querySelector('[onclick="saveBuzzer()"]');disableBtn(btn);showLoading();
  var body = JSON.stringify({
    enabled: document.getElementById('enabled').checked,
    pin: parseInt(document.getElementById('pin').value, 10) || 46,
    on_boot: document.getElementById('on_boot').checked,
    on_save: document.getElementById('on_save').checked,
    on_alarm: document.getElementById('on_alarm').checked,
    on_sensor_alert: document.getElementById('on_sensor_alert').checked,
    on_network: document.getElementById('on_network').checked,
    on_error: document.getElementById('on_error').checked
  });
  fetch('/api/buzzer-config', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: body
  })
  .then(function(r) { return r.json(); })
  .then(function(d) {
    if (d.success) { customAlert('\u2705 Configuration saved!'); showSavedCard(); }
    else { customAlert('\u274C Error: ' + (d.message || 'Failed to save')); }
  })
  .catch(function(e) { customAlert('Error: ' + e); })
  .finally(function(){hideLoading();enableBtn(btn);});
}

function testBeep(pattern) {
  fetch('/api/buzzer-test', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({pattern: pattern})
  }).catch(function() {});
}

function stopBeep() {
  fetch('/api/buzzer-stop', {method: 'POST'}).catch(function() {});
}
</script>
</div></main>)HTML";
