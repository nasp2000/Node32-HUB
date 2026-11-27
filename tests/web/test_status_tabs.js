// /status page: the three tabs (System · Modules & I/O · Storage).
//
// The page used to be one long column. The streamed renderer now routes each
// generated section to a tab by matching the same localized header it printed,
// so this suite takes that block verbatim from status_page_renderer.cpp and
// checks the routing + the tab bar in jsdom.
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages',
                      'renderers', 'status_page_renderer.cpp');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// C string literal -> JS source (only the escapes this file uses).
function decode(lit) {
  return lit.replace(/\\\\/g, '\x00').replace(/\\"/g, '"').replace(/\x00/g, '\\');
}

const src = fs.readFileSync(SRC, 'utf8');
const a = src.indexOf('var sections=[]');
// through the three innerHTML assignments, which follow stInitTabs() in the file
const b = src.indexOf('col3El.innerHTML=htmlCol3;', a) + 'col3El.innerHTML=htmlCol3;'.length;
ok(a > 0 && b > a, 'renderer block not found');
const BLOCK = decode(src.substring(a, b));

// Same labels the page's generator prints; _st resolves the key to them.
const LABELS = {
  stSecSystem: 'SYSTEM', stSecChip: 'CHIP ESP32', stSecResources: 'RESOURCES',
  statusSecGeneral: 'GENERAL SETTINGS', stSecCompiled: 'COMPILED MODULES',
  stSecMicroSD: 'MICRO SD', statusSecFlash: 'FLASH (LittleFS)', statusSecNVS: 'NVS (CONFIGURATION)',
  statusSecNet: 'NETWORK - Ethernet', stSecNetWiFi: 'NETWORK - WiFi',
  statusSecRS232: 'RS232', statusSecEmail: 'EMAIL', stSecPrinter: 'PRINTER'
};

function st(k, fb) { return LABELS[k] !== undefined ? LABELS[k] : (fb !== undefined ? fb : k); }

function boot(lines) {
  const dom = new JSDOM('<!doctype html><html><body>' +
    "<div id='stTabs'></div>" +
    "<div class='status-tab active' id='stTabSys'><div id='col1'></div></div>" +
    "<div class='status-tab' id='stTabIo'><div id='col2'></div></div>" +
    "<div class='status-tab' id='stTabData'><div id='col3'></div></div>" +
    '</body></html>', { runScripts: 'outside-only' });
  const w = dom.window;
  const fn = new w.Function('L', 'col1El', 'col2El', 'col3El', '_st', 'document', BLOCK);
  fn(lines, w.document.getElementById('col1'), w.document.getElementById('col2'),
     w.document.getElementById('col3'), st, w.document);
  return w;
}

// Sections as the generator emits them: '' separator, then the header line.
function lines(kind) {
  if (kind === 'sys') return ['', '\u2550 SYSTEM \u2550', 'FW: v1.120', 'Uptime: 1h',
                              '', '\u2550 CHIP ESP32 \u2550', 'Model: ESP32-S3'];
  if (kind === 'data') return ['', '\u2550 MICRO SD \u2550', 'Mounted: Yes',
                               '', '\u2550 FLASH (LittleFS) \u2550', 'Space: 1MB/2MB'];
  return ['', '\u2550 RS232 \u2550', 'Baudrate: 9600',
          '', '\u2550 EMAIL \u2550', 'Server: smtp'];
}

test('system sections go to the System tab', function () {
  const w = boot(lines('sys'));
  const html = w.document.getElementById('col1').innerHTML;
  ok(html.indexOf('SYSTEM') >= 0 && html.indexOf('CHIP ESP32') >= 0, 'system content');
  is(w.document.getElementById('col2').innerHTML, '', 'modules tab stays empty');
  is(w.document.getElementById('col3').innerHTML, '', 'storage tab stays empty');
});

test('module sections go to the Modules tab', function () {
  const w = boot(lines('io'));
  const html = w.document.getElementById('col2').innerHTML;
  ok(html.indexOf('RS232') >= 0 && html.indexOf('EMAIL') >= 0, 'module content');
  is(w.document.getElementById('col1').innerHTML, '', 'system tab stays empty');
});

test('storage sections go to the Storage tab', function () {
  const w = boot(lines('data'));
  const html = w.document.getElementById('col3').innerHTML;
  ok(html.indexOf('MICRO SD') >= 0 && html.indexOf('FLASH (LittleFS)') >= 0, 'storage content');
  is(w.document.getElementById('col1').innerHTML, '', 'system tab stays empty');
});

test('the three tabs are built once and System starts active', function () {
  const w = boot(lines('sys'));
  const btns = w.document.querySelectorAll('.status-tab-btn');
  is(btns.length, 3, 'tab buttons');
  ok(btns[0].classList.contains('active'), 'first tab active');
  ok(w.document.getElementById('stTabSys').classList.contains('active'), 'system panel visible');
  ok(!w.document.getElementById('stTabData').classList.contains('active'), 'storage panel hidden');
});

test('clicking a tab switches the panel and the highlight', function () {
  const w = boot(lines('data'));
  const btns = w.document.querySelectorAll('.status-tab-btn');
  btns[2].onclick();
  ok(btns[2].classList.contains('active'), 'clicked tab highlighted');
  ok(!btns[0].classList.contains('active'), 'others cleared');
  ok(w.document.getElementById('stTabData').classList.contains('active'), 'storage visible');
  ok(!w.document.getElementById('stTabSys').classList.contains('active'), 'system hidden');
});

test('an unknown section is never lost (falls back to Modules)', function () {
  const w = boot(['', '\u2550 SOMETHING NEW \u2550', 'x: 1']);
  ok(w.document.getElementById('col2').innerHTML.indexOf('SOMETHING NEW') >= 0, 'unknown routed to Modules');
});

test('a second run does not duplicate the tab bar', function () {
  const w = boot(lines('sys'));
  const fn = new w.Function('L', 'col1El', 'col2El', 'col3El', '_st', 'document', BLOCK);
  fn(lines('sys'), w.document.getElementById('col1'), w.document.getElementById('col2'),
     w.document.getElementById('col3'), st, w.document);
  is(w.document.querySelectorAll('.status-tab-btn').length, 3, 'still three tabs');
});

console.log('\n' + passed + '/' + (passed + failed) + ' passed');
process.exit(failed ? 1 : 0);
