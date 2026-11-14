// Camera phone page (/m) — jsdom tests.
//
//   1 Live   : MJPEG view, PHOTO and REC
//   2 Files  : recordings list + delete
//   3 System : shared summary / OTA / log / restart
const { JSDOM, VirtualConsole } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'camera_mobile.html');
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
  var ids = ['gmS1', 'gmS2', 'gmSYS'];
  for (var i = 0; i < ids.length; i++) {
    var el = doc.getElementById(ids[i]);
    if (el && el.className.indexOf('on') >= 0) return i + 1;
  }
  return 0;
}
function posted(win) {
  return (win.__postBodies || []).map(function (b) { return b.url + ' ' + decodeURIComponent(String(b.body || '')); }).join(' ');
}

var API = {
  '/api/camera/status': { ok: true, fps: 15, resolution: '1280x720', sdFree: 29000,
                          streamClients: 1, recording: false, motion: false },
  '/api/camera/files': { files: [
    { n: '/camera/20261114/rec_1.avi', s: 5242880 },
    { n: '/camera/20261114/rec_2.264', s: 1048576 }
  ] },
  '/api/camera/photo': { success: true },
  '/api/camera/recording/start': { success: true },
  '/api/camera/recording/stop': { success: true },
  '/api/camera/delete': { success: true },
  '/api/version': { version: 'v1.120', build: 'v1.120 - Nov 14 2026 16:24:07 WET' },
  '/api/health': { status: 'ok', uptime_str: '0d 0h 5m 0s', reset_reason: 'POWER_ON',
                   heap: { free: 210000 }, psram: { free: 30000000 }, pressure: 'normal' },
  '/api/status': { wifi_rssi: -55, ip_address: '192.168.1.9', free_heap: 210000 },
  '/api/sd-status': { status: 'ok', total_bytes: 31716278272, used_bytes: 85409792 },
  '/api/serial-monitor': { messages: [{ data: '[CAM] stream client 1' }], current_seq: 1 },
  '/api/restart': { success: true }
};

async function run() {
  console.log('\n=== Camera mobile page (3 screens) ===\n');

  {
    var dom = loadPage(API);
    await wait(dom, 400);
    var win = dom.window, doc = win.document;
    var g = win.__test__;

    await test('three screens with the strip and one visible at a time', function () {
      is(doc.querySelectorAll('#gm .screen').length, 3, 'three screens');
      is(doc.querySelectorAll('#gm .navstrip .ncol').length, 3, 'three strip columns');
      is(screenOn(doc), 1, 'starts on Live');
      is(doc.getElementById('gmNo').textContent, '1 / 3', 'counter');
    });

    await test('live view opens the MJPEG stream and shows the camera info', function () {
      ok(doc.getElementById('cmImg').getAttribute('src').indexOf('/cam/stream') === 0, 'stream src');
      ok(doc.getElementById('cmInfo').textContent.indexOf('15 fps') >= 0,
         'info: ' + doc.getElementById('cmInfo').textContent);
    });

    await test('PHOTO posts and reports the result', async function () {
      win.__postBodies = [];
      g.cmPhoto();
      await wait(dom, 250);
      ok(posted(win).indexOf('/api/camera/photo') >= 0, 'photo endpoint');
      ok(doc.getElementById('gmMsg').textContent.indexOf('Photo saved') >= 0, 'saved message');
    });

    await test('REC toggles the recording endpoint and the button state', async function () {
      win.__postBodies = [];
      g.cmRec();
      await wait(dom, 250);
      ok(posted(win).indexOf('/api/camera/recording/start') >= 0, 'start requested');
      is(g.cmState.recording, true, 'recording flag');
      is(doc.getElementById('cmRec').className.indexOf('on') >= 0, true, 'button active');
      win.__postBodies = [];
      g.cmRec();
      await wait(dom, 250);
      ok(posted(win).indexOf('/api/camera/recording/stop') >= 0, 'stop requested');
      is(g.cmState.recording, false, 'recording stopped');
    });

    await test('the files screen lists the recordings with sizes and delete', async function () {
      g.cmSetStep(2);
      await wait(dom, 300);
      var rows = doc.querySelectorAll('#cmList .row');
      is(rows.length, 2, 'two recordings');
      ok(rows[0].textContent.indexOf('rec_1.avi') >= 0, 'name shown');
      ok(rows[0].textContent.indexOf('5.0 MB') >= 0, 'size shown: ' + rows[0].textContent);
      ok(rows[0].querySelector('a').getAttribute('href').indexOf('/cam/playback') === 0, 'playback link');
      ok(rows[0].querySelector('button') !== null, 'delete button');
    });

    await test('deleting asks for confirmation and posts the path', async function () {
      win.__postBodies = [];
      g.cmDelete('/camera/20261114/rec_1.avi');
      await wait(dom, 300);
      ok(posted(win).indexOf('/api/camera/delete') >= 0, 'delete posted');
      ok(decodeURIComponent(posted(win)).indexOf('rec_1.avi') >= 0, 'path sent');
    });

    await test('the system screen is the shared block', async function () {
      g.cmSetStep(3);
      await wait(dom, 300);
      is(screenOn(doc), 3, 'system screen');
      ok(doc.getElementById('muFw').textContent.indexOf('v1.120') >= 0, 'firmware');
      is(doc.querySelectorAll('#gmSYS .subt').length, 6, '3 tabs + 3 log controls');
      ok(doc.getElementById('muReboot') !== null, 'restart button');
    });

    await test('navigation works with no recording selected', function () {
      g.cmSetStep(1);
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
