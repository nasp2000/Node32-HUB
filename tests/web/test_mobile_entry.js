// Phone entry for the G-code sender (UID 288): /grbl-controller sends phones
// to the mobile wizard (/m) and keeps the full page for desktop.
//
// The redirect itself lives in the firmware route (only a board can prove it
// end-to-end, and this run has no OTA), so this suite pins the two things that
// can regress silently without hardware:
//   1. the wiring in web_routes_monitor.cpp — UA read, classifier call, /m
//      redirect before the desktop page is streamed, `?desk=1` escape, and the
//      desktop path still streaming the original page;
//   2. both pages still existing and unchanged in shape (the wizard has its
//      three screens, the desktop fixture still carries the controller shell).
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const ROOT = path.join(__dirname, '..', '..');
const ROUTE = path.join(ROOT, 'chatgpt', 'src', 'webpages', 'routes', 'web_routes_monitor.cpp');
const CLASSIFIER = path.join(ROOT, 'chatgpt', 'src', 'ua_classify.h');
const WIZARD = path.join(__dirname, 'pages', 'gcode_mobile.html');
const DESKTOP = path.join(__dirname, 'pages', 'grbl_controller.html');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }

// The /grbl-controller handler block, from its server.on( to the closing "});".
function handlerBlock() {
  const src = fs.readFileSync(ROUTE, 'utf8');
  const start = src.indexOf('server.on("/grbl-controller"');
  ok(start > 0, 'handler not found');
  const endMarker = 'streamGrblControllerPageToPrint(*response);';
  const streamEnd = src.indexOf(endMarker, start);
  ok(streamEnd > start, 'desktop stream not found inside the handler');
  // include the tail of the handler (logServed + closing "});")
  const close = src.indexOf('\n    });', streamEnd);
  ok(close > streamEnd, 'handler close not found');
  return src.slice(start, close);
}

console.log('\n=== Phone entry for the G-code sender (UID 288) ===\n');

test('the handler reads the User-Agent and asks the shared classifier', function () {
  const h = handlerBlock();
  ok(h.indexOf('getHeader("User-Agent")') > 0, 'reads User-Agent');
  ok(h.indexOf('uaWantsMobileWizard(') > 0, 'calls the classifier');
  ok(h.indexOf('uaHdr ? uaHdr->value().c_str() : nullptr') > 0, 'passes the header value (null-safe)');
});

test('phones are redirected to /m before the desktop page is served', function () {
  const h = handlerBlock();
  const redirect = h.indexOf('redirect("/m")');
  const stream = h.indexOf('streamGrblControllerPageToPrint');
  ok(redirect > 0, 'redirects to /m');
  ok(stream > redirect, 'redirect happens before the desktop page is streamed');
  // the redirect path returns, so nothing below it runs
  const after = h.slice(redirect, redirect + 60);
  ok(after.indexOf('return;') > 0, 'returns right after redirecting');
});

test('?desk=1 keeps the full controller reachable from a phone', function () {
  const h = handlerBlock();
  ok(h.indexOf('hasParam("desk")') > 0, 'honours the escape');
  const guard = h.indexOf('hasParam("desk")');
  const redirect = h.indexOf('redirect("/m")');
  ok(guard < redirect, 'escape is checked before the redirect');
});

test('the desktop path is untouched: same auth, cache and page stream', function () {
  const h = handlerBlock();
  ok(h.indexOf('ensureBasicAuth(request)') > 0, 'auth still first');
  ok(h.indexOf('sendNotModifiedIfCached(request)') > 0, 'ETag/304 path intact');
  ok(h.indexOf('allowHtmlClient(request)') > 0, 'session accounting intact');
  ok(h.indexOf('streamGrblControllerPageToPrint(*response)') > 0, 'serves the original renderer');
  ok(h.indexOf('logServed(request, "/grbl-controller"') > 0, 'logging intact');
});

test('the policy lives in one header (no second UA table in the route)', function () {
  const h = handlerBlock();
  const ph = fs.readFileSync(CLASSIFIER, 'utf8');
  ok(ph.indexOf('uaWantsMobileWizard') > 0, 'classifier exists');
  ok(ph.indexOf('iPhone') > 0 && ph.indexOf('Android') > 0 && ph.indexOf('iPad') > 0, 'known tokens');
  ok(ph.indexOf('UA_TABLET') > 0, 'tablets are a separate class');
  ok(h.indexOf('strstr(') < 0, 'route does not re-implement UA parsing');
});

test('the wizard fixture is the three-screen mobile page', function () {
  const html = fs.readFileSync(WIZARD, 'utf8');
  const dom = new JSDOM(html, { url: 'http://localhost/m', runScripts: 'dangerously', pretendToBeVisual: true });
  const doc = dom.window.document;
  is(doc.querySelectorAll('#gm .screen').length, 3, 'three screens');
  ok(doc.getElementById('gmPrimary') !== null, 'primary action');
  ok(doc.querySelectorAll('#gm .steps i').length === 3, 'three step dots');
  dom.window.close();
});

test('the desktop fixture still carries the full controller shell', function () {
  const html = fs.readFileSync(DESKTOP, 'utf8');
  ok(html.indexOf("id='grbl_page_root'") > 0 || html.indexOf('id="grbl_page_root"') > 0, 'controller root');
  ok(html.indexOf('grbl_sd_upload') > 0 || html.indexOf('grbl_console') > 0, 'full page widgets present');
  ok(html.indexOf('id="gm"') < 0 && html.indexOf("id='gm'") < 0, 'wizard markup is not in the desktop page');
});

var total = passed + failed;
console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
process.exit(failed > 0 ? 1 : 0);
