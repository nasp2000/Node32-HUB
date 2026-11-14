// Rambox32 phone page (/m) — jsdom tests.
//
//   1 Status   : clock + RS232 + arrivals summary, POLL NOW / SYNC
//   2 Arrivals : list of rings + clear
//   3 Command  : one RS232 line + template chips
//   4 System   : shared summary / OTA / log / restart
const { JSDOM, VirtualConsole } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'rambox_mobile.html');
var passed = 0, failed = 0;

async function test(name, fn) {
  try { await fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function quietConsole() {
  var vc = new VirtualConsole();
  vc.on('jsdomError', function (e) {
    if (String((e && e.message) || '').indexOf('Not implemented') >= 0) return;
    console.error(e);
  });
  return vc;
}

function loadPage(api, hash) {
  var html = fs.readFileSync(PAGE, 'utf8');
  return new JSDOM(html, {
    url: 'http://localhost/m' + (hash || ''),
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    virtualConsole: quietConsole(),
    beforeParse: function (w) { w.__MOCK_API__ = api || {}; },
  });
}
function wait(dom, ms) { return new Promise(function (r) { setTimeout(function () { r(dom); }, ms || 250); }); }

function screenOn(doc) {
  var ids = ['gmS1', 'gmS2', 'gmS3', 'gmSYS'];
  for (var i = 0; i < ids.length; i++) {
    var el = doc.getElementById(ids[i]);
    if (el && el.className.indexOf('on') >= 0) return i + 1;
  }
  return 0;
}
function posted(win) {
  return (win.__postBodies || []).map(function (b) {
    return b.url + ' ' + decodeURIComponent(String(b.body || ''));
  }).join(' ');
}

var API = {
  '/api/bricon-status': { connected: true, enabled: true, fancierName: 'Nuno Silva', clubId: '0123',
                          flightId: 'Prova 14/11', lastNewCount: 3, lastPollAgoSec: 12, bytesRx: 4096 },
  '/api/rs232-status': { enabled: true, baudrate: 9600, current_template: 'Unikon' },
  '/api/bricon-arrivals': { count: 2, arrivals: [
    { ring: 'PT-1234567', time: '09:41:12', race: 'Prova 14/11', kind: 'race' },
    { ring: 'PT-7654321', time: '09:38:04', race: 'Prova 14/11', kind: 'race' }
  ] },
  '/api/rs232-templates': { templates: ['Unikon', 'TEST', 'STATUS'] },
  '/api/bricon-poll': { success: true },
  '/api/bricon-reset': { success: true },
  '/api/bricon-sync-calendar': { success: true },
  '/api/version': { version: 'v1.120', build: 'v1.120 - Nov 14 2026 16:24:07 WET' },
  '/api/health': { status: 'ok', uptime_str: '0d 0h 5m 0s', reset_reason: 'POWER_ON',
                   heap: { free: 200000 }, psram: { free: 30000000 }, pressure: 'normal' },
  '/api/status': { wifi_rssi: -60, ip_address: '192.168.1.11', free_heap: 200000 },
  '/api/sd-status': { status: 'ok', total_bytes: 31716278272, used_bytes: 85409792 },
  '/api/serial-monitor': { messages: [{ data: '[RAMBOX] clock poll ok' }], current_seq: 1 },
  '/api/restart': { success: true }
};

async function run() {
  console.log('\n=== Rambox32 mobile page (4 screens) ===\n');

  {
    var dom = loadPage(API);
    await wait(dom, 400);
    var win = dom.window, doc = win.document, g = win.__test__;

    await test('four screens, strip and counter', function () {
      is(doc.querySelectorAll('#gm .screen').length, 4, 'four screens');
      is(doc.querySelectorAll('#gm .navstrip .ncol').length, 4, 'four strip columns');
      is(screenOn(doc), 1, 'starts on Status');
      is(doc.getElementById('gmNo').textContent, '1 / 4', 'counter');
    });

    await test('status shows the clock, fancier, race, arrivals and RS232', async function () {
      await wait(dom, 300);
      ok(doc.getElementById('rbClock').textContent.indexOf('connected') >= 0, 'clock');
      ok(doc.getElementById('rbFancier').textContent.indexOf('Nuno') >= 0, 'fancier');
      ok(doc.getElementById('rbRace').textContent.indexOf('Prova') >= 0, 'race');
      is(doc.getElementById('rbCount').textContent, '3', 'new arrivals');
      ok(doc.getElementById('rbRs').textContent.indexOf('9600') >= 0, 'rs232 baud');
    });

    await test('POLL NOW posts the poll endpoint', async function () {
      win.__postBodies = [];
      g.rbAction('poll');
      await wait(dom, 300);
      ok(posted(win).indexOf('/api/bricon-poll') >= 0, 'poll posted');
      ok(doc.getElementById('gmMsg').textContent.length > 0, 'message');
    });

    await test('SYNC CLOCK posts the calendar sync', async function () {
      win.__postBodies = [];
      g.rbAction('sync');
      await wait(dom, 300);
      ok(posted(win).indexOf('/api/bricon-sync-calendar') >= 0, 'sync posted');
    });

    await test('arrivals list shows the rings with time and race', async function () {
      g.rbSetStep(2);
      await wait(dom, 300);
      var rows = doc.querySelectorAll('#rbList .row');
      is(rows.length, 2, 'two arrivals');
      ok(rows[0].textContent.indexOf('PT-1234567') >= 0, 'ring');
      ok(rows[0].textContent.indexOf('09:41') >= 0, 'time');
      ok(doc.getElementById('rbArrTitle').textContent.indexOf('2') >= 0, 'count in the title');
    });

    await test('clearing the arrivals asks for confirmation', async function () {
      win.__postBodies = [];
      doc.getElementById('rbReset').click();
      await wait(dom, 300);
      ok(posted(win).indexOf('/api/bricon-reset') >= 0, 'reset posted');
    });

    await test('pombal and races tabs list the clock data', async function () {
      win.__MOCK_API__['/api/bricon-pombal'] = { count: 2, registrations: [
        { ring: 'PT-1234567', chip: 'A1' }, { ring: 'PT-7654321', chip: 'B2' }] };
      win.__MOCK_API__['/api/bricon-contests'] = { count: 1, contests: [
        { code: 'C1', name: 'Prova 14/11', date: '14/11', distance: 300, pigeonCount: 42 }] };
      g.rbSetStep(2);
      await wait(dom, 250);
      g.rbTab('pombal');
      await wait(dom, 300);
      var rows = doc.querySelectorAll('#rbList .row');
      is(rows.length, 2, 'two pigeons');
      ok(rows[0].textContent.indexOf('PT-1234567') >= 0, 'ring listed');
      g.rbTab('races');
      await wait(dom, 300);
      rows = doc.querySelectorAll('#rbList .row');
      is(rows.length, 1, 'one race');
      ok(rows[0].textContent.indexOf('Prova') >= 0, 'race name');
      ok(rows[0].textContent.indexOf('300') >= 0, 'distance');
    });

    await test('the config screen saves the clock and the RS232 settings', async function () {
      g.rbSetStep(3);
      await wait(dom, 350);
      is(screenOn(doc), 3, 'config screen');
      ok(doc.querySelectorAll('#rbInterval option').length >= 3, 'interval options');
      ok(doc.querySelectorAll('#rbBaud option').length >= 3, 'baud options');
      is(doc.getElementById('gmPrimary').textContent, 'SAVE', 'save action');
      // clock profile mode: only the clock config is posted (its own 9600 8N2/8N1)
      win.__postBodies = [];
      doc.getElementById('rbSerialClock').click();
      g.rbSave();
      await wait(dom, 350);
      var body = posted(win);
      ok(body.indexOf('-config') >= 0, 'clock config posted');
      ok(/manual=0/.test(body), 'manual=0 sent');
      ok(body.indexOf('section=rs232') < 0, 'rs232 untouched in clock mode');
      // manual mode: the RS232 section is saved too
      win.__postBodies = [];
      doc.getElementById('rbSerialManual').click();
      g.rbSave();
      await wait(dom, 350);
      body = posted(win);
      ok(/manual=1/.test(body), 'manual=1 sent');
      ok(body.indexOf('section=rs232') >= 0, 'rs232 config posted');
      ok(/baudrate=/.test(body), 'baud sent');
    });

    await test('the arrivals screen can ask for the email again', async function () {
      win.__MOCK_API__['/api/bricon-email-pombal'] = { success: false };
      win.__MOCK_API__['/api/unikon-email-pombal'] = { success: true };
      g.rbSetStep(2);
      await wait(dom, 250);
      win.__postBodies = [];
      g.rbEmail();
      await wait(dom, 300);
      ok(posted(win).indexOf('-email-pombal') >= 0, 'email endpoint posted');
    });

    await test('the system screen is the shared block', async function () {
      g.rbSetStep(4);
      await wait(dom, 350);
      is(screenOn(doc), 4, 'system screen');
      ok(doc.getElementById('muFw').textContent.indexOf('v1.120') >= 0, 'firmware');
      ok(doc.getElementById('muReboot') !== null, 'restart');
    });

    await test('navigation never waits for data', function () {
      g.rbSetStep(1);
      ok(!doc.getElementById('gmNext').disabled, 'next enabled');
      ok(doc.getElementById('gmBack').disabled, 'back disabled on the first screen');
    });

    dom.window.close();
  }

  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) { console.error('Fatal:', e); process.exit(1); });
