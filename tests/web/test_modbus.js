var fs = require('fs');
var path = require('path');
var jsdom = require('jsdom');
var { JSDOM } = jsdom;

var html = fs.readFileSync(path.join(__dirname, 'pages', 'modbus.html'), 'utf8').toString();
var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
var win = dom.window;
var doc = win.document;

// Global functions (defined in TCP and Live scripts)
var mbRenderLive = win.mbRenderLive;
var mbFetchLive = win.mbFetchLive;
var mbPollNow = win.mbPollNow;
var mbToggleAuto = win.mbToggleAuto;
var mbWrPopulateDevices = win.mbWrPopulateDevices;
var mbWrDevChanged = win.mbWrDevChanged;
var mbWrSubmit = win.mbWrSubmit;
var initModbusLivePage = win.initModbusLivePage;

var passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; } else { console.log('  ✗ FAIL:', msg); failed++; }
}
function assertEq(actual, expected, msg) {
  if (actual === expected) { passed++; }
  else { console.log('  ✗ FAIL:', msg, '- expected:', JSON.stringify(expected), 'got:', JSON.stringify(actual)); failed++; }
}
function assertMatch(actual, regex, msg) {
  if (regex.test(actual)) { passed++; }
  else { console.log('  ✗ FAIL:', msg, '- expected match:', regex, 'got:', JSON.stringify(actual)); failed++; }
}

console.log('\n=== Modbus Page Tests ===\n');

// --- mbRenderLive: empty state ---
(function test_mbRenderLive_empty() {
  var html = mbRenderLive([]);
  assert(html.indexOf('Nenhum dispositivo configurado') >= 0, 'mbRenderLive shows empty state');
  assert(html.indexOf('/modbus-rtu') >= 0, 'mbRenderLive links to RTU config');
  assert(html.indexOf('/modbus-tcp') >= 0, 'mbRenderLive links to TCP config');

  var html2 = mbRenderLive(null);
  assert(html2.indexOf('Nenhum dispositivo configurado') >= 0, 'mbRenderLive handles null');
})();

// --- mbRenderLive: single device with registers ---
(function test_mbRenderLive_single_device() {
  var devs = [{
    name: 'Temperature',
    enabled: true,
    pollCount: 42,
    errorCount: 3,
    slaveId: 1,
    regs: [
      { label: 'Temp', val: 25.3, unit: '°C', ok: true },
      { label: 'Humidity', val: 60.1, unit: '%', ok: true }
    ]
  }];
  var html = mbRenderLive(devs);
  assert(html.indexOf('Temperature') >= 0, 'mbRenderLive shows device name');
  assert(html.indexOf('activo') >= 0, 'mbRenderLive shows active status');
  assert(html.indexOf('42') >= 0, 'mbRenderLive shows poll count');
  assert(html.indexOf('3') >= 0, 'mbRenderLive shows error count');
  assert(html.indexOf('Temp') >= 0, 'mbRenderLive shows register label');

  // Check value formatting
  assert(html.indexOf('25.3000') >= 0 || html.indexOf('25.3') >= 0, 'mbRenderLive formats register value');

  // Check ToggleAuto button
  var autoBtn = doc.getElementById('mb_auto_btn');
  assert(!!autoBtn, 'auto toggle button exists');
})();

// --- mbRenderLive: disabled device ---
(function test_mbRenderLive_disabled() {
  var devs = [{
    name: 'Disabled Sensor',
    enabled: false,
    pollCount: 0,
    errorCount: 0,
    slaveId: 2,
    regs: []
  }];
  var html = mbRenderLive(devs);
  assert(html.indexOf('inactivo') >= 0, 'mbRenderLive shows inactive status');
})();

// --- mbRenderLive: no registers ---
(function test_mbRenderLive_no_regs() {
  var devs = [{
    name: 'Empty Device',
    enabled: true,
    pollCount: 10,
    errorCount: 0,
    slaveId: 3,
    regs: []
  }];
  var html = mbRenderLive(devs);
  assert(html.indexOf('Sem registos configurados') >= 0, 'mbRenderLive shows no regs message');
})();

// --- mbRenderLive: register with error ---
(function test_mbRenderLive_error_register() {
  var devs = [{
    name: 'Faulty',
    enabled: true,
    pollCount: 5,
    errorCount: 10,
    slaveId: 4,
    regs: [
      { label: 'Bad Sensor', val: null, unit: '', ok: false }
    ]
  }];
  var html = mbRenderLive(devs);
  assert(html.indexOf('Bad Sensor') >= 0, 'mbRenderLive shows faulty register label');
  assert(html.indexOf('N/A') >= 0, 'mbRenderLive shows N/A for null value');
})();

// --- mbToggleAuto: toggles state ---
(function test_mbToggleAuto() {
  win._mbAutoRefresh = true;  // initial state
  var btn = doc.getElementById('mb_auto_btn');
  assert(btn.textContent.indexOf('Parar Auto') >= 0, 'initial auto button shows Parar Auto');

  mbToggleAuto();
  assert(win._mbAutoRefresh === false, 'mbToggleAuto sets _mbAutoRefresh to false');
  assert(btn.textContent.indexOf('Iniciar Auto') >= 0, 'auto button shows Iniciar Auto after toggle');

  mbToggleAuto();
  assert(win._mbAutoRefresh === true, 'mbToggleAuto sets _mbAutoRefresh back to true');
  assert(btn.textContent.indexOf('Parar Auto') >= 0, 'auto button shows Parar Auto after second toggle');
})();

// --- mbWrPopulateDevices: populates device select ---
(function test_mbWrPopulateDevices() {
  var devs = [
    { name: 'Sensor1', enabled: true, slaveId: 1, regs: [] },
    { name: 'Sensor2', enabled: true, slaveId: 2, regs: [] },
    { name: 'Sensor3', enabled: false, slaveId: 3, regs: [] }  // disabled
  ];
  mbWrPopulateDevices(devs);
  var sel = doc.getElementById('wr_device');
  assert(sel.options.length === 3, 'mbWrPopulateDevices adds placeholder + 2 enabled devices');
  assert(sel.options[1].textContent.indexOf('Sensor1') >= 0, 'first device option text includes name');
  assert(sel.options[2].textContent.indexOf('Sensor2') >= 0, 'second device option text includes name');
  assert(sel.options[0].value === '', 'first option is placeholder with empty value');

  // Check reg select was reset
  var regSel = doc.getElementById('wr_reg');
  assert(regSel.options.length === 1, 'reg select reset to placeholder only');
  assert(regSel.options[0].value === '', 'reg select placeholder has empty value');
})();

// --- mbWrDevChanged: populates reg select for writable registers ---
(function test_mbWrDevChanged() {
  // Populate devices first (this sets _mbLiveDevs)
  var devs = [{
    name: 'WritableDev',
    enabled: true,
    slaveId: 10,
    regs: [
      { label: 'ReadOnly', fc: 3, unit: 'V' },   // FC3 = not writable
      { label: 'CoilWrite', fc: 5, unit: '' },     // FC5 = writable coil
      { label: 'RegWrite', fc: 6, unit: 'Hz' },    // FC6 = writable reg
    ]
  }];
  mbWrPopulateDevices(devs);

  var sel = doc.getElementById('wr_device');
  sel.value = '0';  // select first (index 0 in options = value "0")
  mbWrDevChanged();

  var regSel = doc.getElementById('wr_reg');
  assert(regSel.options.length === 3, 'mbWrDevChanged shows placeholder + 2 writable regs');
  var texts = [];
  for (var i = 0; i < regSel.options.length; i++) texts.push(regSel.options[i].textContent);
  var hasCoil = texts.some(function(t) { return t.indexOf('CoilWrite') >= 0; });
  var hasReg = texts.some(function(t) { return t.indexOf('RegWrite') >= 0; });
  var hasReadOnly = texts.some(function(t) { return t.indexOf('ReadOnly') >= 0; });
  assert(hasCoil, 'CoilWrite reg appears in options');
  assert(hasReg, 'RegWrite reg appears in options');
  assert(!hasReadOnly, 'ReadOnly FC3 reg is excluded');
})();

// --- mbWrDevChanged: invalid selection resets ---
(function test_mbWrDevChanged_invalid() {
  var sel = doc.getElementById('wr_device');
  sel.value = '';  // invalid
  mbWrDevChanged();
  var regSel = doc.getElementById('wr_reg');
  assert(regSel.options.length === 1, 'mbWrDevChanged resets reg select for invalid device');
})();

// --- mbWrSubmit: validates device/reg selection ---
(function test_mbWrSubmit_validation() {
  var sel = doc.getElementById('wr_device');
  var regSel = doc.getElementById('wr_reg');
  sel.value = '';
  regSel.value = '';
  doc.getElementById('wr_value').value = '10';

  mbWrSubmit();
  var resultEl = doc.getElementById('wr_result');
  assert(resultEl.textContent.indexOf('Selecione') >= 0 || resultEl.textContent.indexOf('⚠') >= 0, 'mbWrSubmit shows validation error');

  // Clear result
  resultEl.textContent = '';
})();

// --- mbWrSubmit: successful write ---
(function test_mbWrSubmit_success() {
  // Set up devices
  var devs = [{
    name: 'Dev1',
    enabled: true,
    slaveId: 1,
    regs: [
      { label: 'WriteReg', fc: 6, unit: 'V' }
    ]
  }];
  mbWrPopulateDevices(devs);

  var devSel = doc.getElementById('wr_device');
  devSel.value = '0';
  mbWrDevChanged();

  var regSel = doc.getElementById('wr_reg');
  regSel.value = '0';
  doc.getElementById('wr_value').value = '42';

  // Mock the write API
  if (!win.__MOCK_API__) win.__MOCK_API__ = {};
  win.__MOCK_API__['/api/modbus/write'] = { success: true };

  var origConfirm = win.customConfirm;
  win.customConfirm = function(msg, cb) { if (cb) cb(true); };

  mbWrSubmit();

  return new Promise(function(resolve) {
    setTimeout(function() {
      var resultEl = doc.getElementById('wr_result');
      assert(resultEl.textContent.indexOf('OK') >= 0 || resultEl.textContent.indexOf('✅') >= 0, 'mbWrSubmit shows success after write');
      win.customConfirm = origConfirm;
      resolve();
    }, 50);
  });
})().then(function() {
  // --- Functions exist ---
  assert(typeof mbPollNow === 'function', 'mbPollNow is a function');
  assert(typeof mbFetchLive === 'function', 'mbFetchLive is a function');
  assert(typeof mbToggleAuto === 'function', 'mbToggleAuto is a function');
  assert(typeof initModbusLivePage === 'function', 'initModbusLivePage is a function');

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
