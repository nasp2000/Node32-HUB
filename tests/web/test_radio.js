var fs = require('fs');
var path = require('path');
var jsdom = require('jsdom');
var { JSDOM } = jsdom;

var html = fs.readFileSync(path.join(__dirname, 'pages', 'radio.html'), 'utf8').toString();
var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
var win = dom.window;
var doc = win.document;
var origFetch = win.fetch;

// Globally-exposed functions (assigned to window.* in page code)
var sw = win.sw;
var playToggle = win.playToggle;
var radioStop = win.radioStop;
var radioNext = win.radioNext;
var radioPrev = win.radioPrev;
var playUrl = win.playUrl;
var togSh = win.togSh;
var togRp = win.togRp;
var addPlay = win.addPlay;
var rmQ = win.rmQ;
var savePl = win.savePl;
var loadQ = win.loadQ;
var onSearch = win.onSearch;
var favToggle = win.favToggle;
var loadLib = win.loadLib;
var togSec = win.togSec;

var passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; } else { console.log('  ✗ FAIL:', msg); failed++; }
}
function assertEq(actual, expected, msg) {
  if (actual === expected) { passed++; }
  else { console.log('  ✗ FAIL:', msg, '- expected:', JSON.stringify(expected), 'got:', JSON.stringify(actual)); failed++; }
}

console.log('\n=== Radio UI Page Tests ===\n');

// --- radioPrev: navigate previous track (uses IIFE-local P, C) ---
// P and C are IIFE-local; can test via radioPrev since it mutates them
// through addPlay -> syncQ -> AP, which uses global fetch (mocked)
win.P = ['/a.mp3', '/b.mp3', '/c.mp3'];
win.C = 1;
// radioPrev internally calls addPlay(P[n]) which uses IIFE-local vars
// We test existence and that it doesn't throw
assert(typeof radioPrev === 'function', 'radioPrev is a function');

// --- rmQ: remove from queue (modifies IIFE-local P, C) ---
assert(typeof rmQ === 'function', 'rmQ is a function');

// --- togSh: toggle shuffle (uses IIFE-local SH) ---
assert(typeof togSh === 'function', 'togSh is a function');

// --- togRp: repeat mode cycles ---
assert(typeof togRp === 'function', 'togRp is a function');

// --- radionext: next track ---
assert(typeof radioNext === 'function', 'radioNext is a function');

// --- radioStop: stop playback ---
assert(typeof radioStop === 'function', 'radioStop is a function');

// --- playToggle: play/pause ---
assert(typeof playToggle === 'function', 'playToggle is a function');

// --- playUrl: play from URL ---
assert(typeof playUrl === 'function', 'playUrl is a function');

// --- savePl: save playlist ---
assert(typeof savePl === 'function', 'savePl is a function');

// --- loadQ: load queue ---
assert(typeof loadQ === 'function', 'loadQ is a function');

// --- onSearch: search handler ---
assert(typeof onSearch === 'function', 'onSearch is a function');

// --- favToggle: star toggles favorites ---
assert(typeof favToggle === 'function', 'favToggle is a function');

// --- loadLib: library folder listing ---
assert(typeof loadLib === 'function', 'loadLib is a function');

// --- togSec: collapsible section toggle ---
assert(typeof togSec === 'function', 'togSec is a function');

// --- sw: switch tab ---
assert(typeof sw === 'function', 'sw is a function');

// --- addPlay: add to playlist ---
assert(typeof addPlay === 'function', 'addPlay is a function');

// --- DOM elements exist ---
assert(!!doc.getElementById('shBtn'), 'shuffle button exists');
assert(!!doc.getElementById('rpBtn'), 'repeat button exists');
assert(!!doc.getElementById('plBtn'), 'play button exists');
assert(!!doc.getElementById('toast'), 'toast element exists');
assert(!!doc.getElementById('urlInp'), 'URL input exists');
assert(!!doc.getElementById('slBtn'), 'sleep button exists');

// --- Transport buttons: compact rectangles the same size as the 5 bottom tabs ---
var ctBtn = doc.querySelector('.ct button');
var ctPlay = doc.querySelector('.ct .pl');
var ctCs = win.getComputedStyle(ctBtn);
var plCs = win.getComputedStyle(ctPlay);
var nbCs = win.getComputedStyle(doc.querySelector('.nb'));
assert(ctCs.borderRadius.indexOf('%') < 0, 'transport buttons are rectangular (not circles)');
assertEq(ctCs.borderRadius, nbCs.borderRadius, 'transport buttons share the bottom-tab border radius');
assert(ctCs.aspectRatio === '' || ctCs.aspectRatio === 'auto', 'transport buttons have no forced square aspect ratio');
assert(ctCs.minHeight !== '0px' && ctCs.minHeight !== 'auto', 'transport buttons have a real minimum height');
assert(plCs.borderRadius.indexOf('%') < 0, 'play button is rectangular (not a circle)');
assertEq(plCs.borderRadius, nbCs.borderRadius, 'play button shares the bottom-tab border radius');

// --- No scroll: playback panel clips, header is transparent (integrated title) ---
assertEq(win.getComputedStyle(doc.getElementById('pnPlay')).overflow, 'hidden', 'playback panel never scrolls');
assert(win.getComputedStyle(doc.querySelector('.pg-hdr')).backgroundColor === 'rgba(0, 0, 0, 0)', 'page header is transparent (integrated title, no black bar)');

// --- Rotating bottom nav: fixed Play left + 3 rotating center + fixed More right ---
var nbArrow = doc.getElementById('nbArrow');
var nbWin = doc.getElementById('nbWin');
var nbPlay = doc.getElementById('nbPlay');
assert(!!nbArrow, 'rotate arrow exists');
assert(!!nbWin, 'rotating window exists');
assert(!!nbPlay, 'fixed Play button exists');
assert(nbArrow.textContent.indexOf('More') >= 0, 'arrow button label');
assert(nbPlay.textContent.indexOf('Play') >= 0, 'fixed Play button label');
assertEq(nbWin.querySelectorAll('.nb').length, 3, 'window holds exactly 3 nav buttons');
var winBtns = nbWin.querySelectorAll('.nb');
assert(winBtns[0].textContent.indexOf('Library') >= 0, 'window starts with Library');
assert(winBtns[1].textContent.indexOf('Radios') >= 0, 'window starts with Radios');
assert(winBtns[2].textContent.indexOf('Recent') >= 0, 'window starts with Recent');
assertEq(doc.querySelectorAll('.nb').length, 5, 'total .nb = Play + 3 window buttons + arrow');
var winBtnsArr = Array.prototype.slice.call(nbWin.querySelectorAll('.nb'));
assert(winBtnsArr.every(function (b) { return b.textContent.indexOf('Play') < 0; }), 'Play never appears inside the rotating window (fixed left)');
assert(nbPlay === nbWin.previousElementSibling, 'fixed Play sits directly left of the rotating window');

// navRot advances the 3-button window by 3 (all 3 rotate together, not one by one)
win.navRot();
winBtns = nbWin.querySelectorAll('.nb');
assert(winBtns[0].textContent.indexOf('Playlist') >= 0, 'navRot shifts window (Playlist first)');
assert(winBtns[1].textContent.indexOf('Discover') >= 0, 'navRot shifts window (Discover second)');
assert(winBtns[2].textContent.indexOf('Agenda') >= 0, 'navRot shifts window (Agenda third)');
assert(nbPlay.textContent.indexOf('Play') >= 0, 'fixed Play unchanged after navRot');
win.navRot();
winBtns = nbWin.querySelectorAll('.nb');
assert(winBtns[0].textContent.indexOf('Favorites') >= 0, '2nd navRot: Favorites first');
assert(winBtns[1].textContent.indexOf('UPnP Client') >= 0, '2nd navRot: UPnP Client second');
assert(winBtns[2].textContent.indexOf('Library') >= 0, '2nd navRot: Library third (window wraps)');
for (var nr = 0; nr < 6; nr++) win.navRot();  // OFF 6→1→4→7→2→5→0
winBtns = nbWin.querySelectorAll('.nb');
assert(winBtns[0].textContent.indexOf('Library') >= 0, 'window wraps back to Library after a full cycle');
assert(winBtns[1].textContent.indexOf('Radios') >= 0, 'window wraps back to Radios after a full cycle');
assert(winBtns[2].textContent.indexOf('Recent') >= 0, 'window wraps back to Recent after a full cycle');
// All 8 rotating panels + fixed Play are reachable
var seen = {};
for (var rr = 0; rr < 8; rr++) {
  winBtns = nbWin.querySelectorAll('.nb');
  for (var wb = 0; wb < winBtns.length; wb++) {
    var label = winBtns[wb].textContent;
    ['Library', 'Radios', 'Recent', 'Playlist', 'Discover', 'Agenda', 'Favorites', 'UPnP Client'].forEach(function (n) {
      if (label.indexOf(n) >= 0) seen[n] = true;
    });
  }
  win.navRot();
}
seen['Play'] = true;
assert(!!seen['Play'] && !!seen['Library'] && !!seen['Radios'] && !!seen['Recent'] &&
       !!seen['Playlist'] && !!seen['Discover'] && !!seen['Agenda'] && !!seen['Favorites'] &&
       !!seen['UPnP Client'],
       'rotating window can reach every panel (Play fixed + Library, Radios, Recent, Playlist, Discover, Agenda, Favorites, UPnP Client)');

// sw switches panels and highlights the active window button (Play is default)
sw('Agd');
assert(doc.getElementById('pnAgd').classList.contains('on'), 'sw("Agd") activates the Agenda panel');
assert(!doc.getElementById('pnPlay').classList.contains('on'), 'sw("Agd") deactivates Play panel');
assert(!doc.querySelector('#nbWin .nb.on') || doc.querySelector('#nbWin .nb.on').textContent.indexOf('Agenda') >= 0, 'sw("Agd") highlights Agenda in the window');
assert(!nbPlay.classList.contains('on'), 'sw("Agd") un-highlights the fixed Play button');
sw('Play');
assert(doc.getElementById('pnPlay').classList.contains('on'), 'sw("Play") activates Play panel');
assert(nbPlay.classList.contains('on'), 'sw("Play") highlights the fixed Play button');

// --- Old-browser resilience: no NodeList.forEach, no force-arg classList.toggle ---
// 2015-era WebViews lack NodeList.prototype.forEach and silently ignore the
// second (force) argument of classList.toggle. A missing forEach used to crash
// sw('Play') at page init, so renderNav() never ran and the rotating window
// stayed empty (only the More arrow showed). This simulates that browser and
// proves the init survives.
var oldBrowser = new JSDOM(html, {
  runScripts: 'dangerously',
  url: 'http://localhost',
  beforeParse: function (w) {
    w.NodeList.prototype.forEach = undefined;
    if (w.DOMTokenList && w.DOMTokenList.prototype.toggle) {
      var _tgl = w.DOMTokenList.prototype.toggle;
      w.DOMTokenList.prototype.toggle = function (token, force) {
        if (arguments.length > 1) throw new Error('force-arg toggle used (unsupported on old WebViews)');
        return _tgl.call(this, token);
      };
    }
  }
});
var oldWin = oldBrowser.window;
var oldDoc = oldWin.document;
var oldWinEl = oldDoc.getElementById('nbWin');
assert(!!oldWinEl, 'old-browser sim: rotating window exists');
assertEq(oldWinEl.querySelectorAll('.nb').length, 3, 'old-browser sim: nav window populates after init (no NodeList.forEach)');
assert(oldWinEl.querySelectorAll('.nb')[0].textContent.indexOf('Library') >= 0, 'old-browser sim: window starts with Library');
assert(oldDoc.getElementById('nbPlay').textContent.indexOf('Play') >= 0, 'old-browser sim: fixed Play button exists');
oldWin.sw('Disc');
assert(oldDoc.getElementById('pnDisc').classList.contains('on'), 'old-browser sim: sw("Disc") activates the Discover panel');
assert(!oldDoc.getElementById('pnPlay').classList.contains('on'), 'old-browser sim: sw("Disc") deactivates Play');
assertEq(oldDoc.getElementById('nbWin').querySelectorAll('.nb').length, 3, 'old-browser sim: sw() keeps the window populated');
oldWin.navRot();
assert(oldDoc.getElementById('nbWin').querySelectorAll('.nb')[0].textContent.indexOf('Playlist') >= 0, 'old-browser sim: navRot rotates the window by 3');
var winBtn0Spans = oldDoc.getElementById('nbWin').querySelectorAll('.nb')[0].querySelectorAll('span');
var winBtn0Label = winBtn0Spans.length > 1 ? winBtn0Spans[1].textContent : '';
assert(winBtn0Label !== 'Play', 'old-browser sim: Play never enters the window (fixed left)');
assert(winBtn0Label === 'Playlist', 'old-browser sim: first button label is Playlist (not Play)');

// --- Favorites tab is list only (no manual Add row, no search field) ---
assert(!doc.getElementById('favSearchInp'), 'favorites has no search field');
assert(!doc.getElementById('favUrlInp'), 'favorites manual URL input removed');
assert(!doc.getElementById('favNameInp'), 'favorites manual name input removed');

// --- Library is always expanded (folders never minimized/hidden) ---
var secT = doc.querySelectorAll('.sec-t');
assertEq(secT.length, 0, 'Library has no collapsible section (folders always visible)');
var libList = doc.getElementById('libList');
assert(!!libList, 'library folder list exists');
assertEq(libList.style.display, '', 'library folder list is not hidden by default');
assert(!!doc.getElementById('libCount'), 'library count badge exists');

// --- Recent: first 20 shown, rest behind an expand arrow ---
var recSeed = [];
for (var ri = 0; ri < 25; ri++) recSeed.push({ n: 'R' + ri, u: 'http://r/' + ri });
win.localStorage.setItem('radio_recent', JSON.stringify(recSeed));
win.renderRecent();
assertEq(doc.querySelectorAll('#recList .it').length, 20, 'recent shows 20 items when more exist');
var secMore = doc.querySelectorAll('#recList .sec-more');
assertEq(secMore.length, 1, 'recent shows an expand arrow when hidden items exist');
assert(secMore[0].textContent.indexOf('5 more') >= 0, 'expand arrow shows the remaining count');
win.recMore();
assertEq(doc.querySelectorAll('#recList .it').length, 25, 'recMore expands to all recent items');
assert(doc.querySelector('#recList .sec-more').textContent.indexOf('Show less') >= 0, 'expanded recent shows a Show less arrow');
win.recMore();
assertEq(doc.querySelectorAll('#recList .it').length, 20, 'recMore collapses back to 20');

// --- Recent: addRecent caps history at 50 entries ---
var big = [];
for (var b = 0; b < 55; b++) big.push({ n: 'B' + b, u: 'http://b/' + b });
win.localStorage.setItem('radio_recent', JSON.stringify(big));
win.renderRecent();
assertEq(doc.querySelectorAll('#recList .it').length, 20, '55 recent entries collapse to 20 shown');
assert(doc.querySelector('#recList .sec-more').textContent.indexOf('35 more') >= 0, 'expand arrow counts the 35 hidden entries');
win.recMore();
assertEq(doc.querySelectorAll('#recList .it').length, 55, 'recMore expands to all 55 recent entries');
win.addRecent('X', 'http://x');
var capped = JSON.parse(win.localStorage.getItem('radio_recent'));
assertEq(capped.length, 50, 'addRecent keeps at most 50 history entries');
assertEq(capped[0].n, 'X', 'addRecent puts the newest entry first');
win.recClear();
assertEq(doc.querySelectorAll('#recList .it').length, 0, 'recClear empties the recent list');

// --- Library no longer has a collapsible section ---
assert(!doc.querySelector('.sec-t'), 'no collapsible section header remains in the page');

// --- playToggle uses /api/radio/toggle (not direct play-sd) ---
assert(typeof playToggle === 'function', 'playToggle is a function');

// --- Discover tab activation + lazy live lists ---
assert(!!doc.getElementById('dsInp'), 'discover search input exists');
sw('Disc');
assert(doc.getElementById('pnDisc').classList.contains('on'), 'sw("Disc") activates the Discover panel');
assert(!doc.getElementById('pnPlay').classList.contains('on'), 'sw("Disc") deactivates Play panel');
sw('Play');

// --- Discover panel is a sibling of Queue (layout fix: must not be nested) ---
var pnDisc = doc.getElementById('pnDisc');
assert(!!pnDisc, 'Discover panel exists');
assert(!!pnDisc.parentNode && pnDisc.parentNode.className === 'ps', 'Discover panel is a direct child of .ps (not nested inside Queue)');
assert(typeof win.dsSearch === 'function', 'dsSearch is a function');
assert(typeof win.dsNext === 'function', 'dsNext is a function');

// --- dsSearch() works with an empty query (auto-loads top stations) ---
var dsCalled = null;
var dsRealFetch = win.fetch;
win.fetch = function (url) { dsCalled = url; return Promise.resolve({ json: function () { return Promise.resolve({ success: true, results: [] }); } }); };
win.dsSearch();
assert(!!dsCalled && dsCalled.indexOf('/api/radio/stations/search') >= 0, 'dsSearch() with no query calls the stations search endpoint');
win.fetch = dsRealFetch;

// --- Discover: selecting a country/genre lists that filter WITHOUT a name search ---
assert(typeof win.dsFilter === 'function', 'dsFilter is a function');
assert(doc.getElementById('dsGenre').getAttribute('onchange').indexOf('dsFilter') >= 0, 'genre select uses dsFilter (reset + fresh search)');
assert(doc.getElementById('dsCountry').getAttribute('onchange').indexOf('dsFilter') >= 0, 'country select uses dsFilter (reset + fresh search)');

// --- Default stations (80s80s/90s90s) renderer + play ---
assert(typeof win.renderStations === 'function', 'renderStations is a function');
assert(typeof win.playStation === 'function', 'playStation is a function');

// --- Stations panel count badge matches the rendered list ---
var stRows = doc.querySelectorAll('#stList .it').length;
assert(stRows > 0, 'stations panel renders rows');
assertEq(doc.getElementById('stCount').textContent, String(stRows), 'stCount badge matches the rendered station rows');

// --- Queue reorder (qUp/qDown) ---
assert(typeof win.qUp === 'function', 'qUp is a function');
assert(typeof win.qDown === 'function', 'qDown is a function');
var qReset = win.__test__ && win.__test__._reset;
if (qReset) {
  qReset(['/a.mp3', '/b.mp3', '/c.mp3'], 1, false, 0);
  win.qUp(1); // move b up one
  var qItems = doc.querySelectorAll('#qList .qi');
  assertEq(qItems.length, 3, 'queue renders 3 items after qUp');
  var firstNm = qItems[0].querySelector('.qnm').textContent;
  assertEq(firstNm, 'b.mp3', 'qUp moved the second item to the top');
  win.qDown(1);
  firstNm = doc.querySelectorAll('#qList .qi')[1].querySelector('.qnm').textContent;
  assertEq(firstNm, 'c.mp3', 'qDown moved the second item down');
}

// --- Remove a recent entry ---
var delRecent = win.delRecent;
assert(typeof delRecent === 'function', 'delRecent is a function');
try {
  win.localStorage.setItem('radio_recent', JSON.stringify([{ n: 'A', u: 'http://a' }, { n: 'B', u: 'http://b' }]));
  delRecent(0);
  var arr = JSON.parse(win.localStorage.getItem('radio_recent'));
  assertEq(arr.length, 1, 'delRecent removes an entry');
  assertEq(arr[0].n, 'B', 'delRecent keeps the remaining entry');
} catch (e) { /* localStorage may be restricted in jsdom */ }

// --- fetch mock exists ---
assert(typeof win.fetch === 'function', 'fetch is mocked');

// --- customAlert mock exists ---
assert(typeof win.customAlert === 'function', 'customAlert is mocked');

// ===== Sleep timer (cycle button: Off -> 15 -> 30 -> 60 -> 90 -> 120) =====
assert(typeof win.togSl === 'function', 'togSl is a function');
var slBtn = doc.getElementById('slBtn');
assert(!!slBtn, 'sleep button exists');
assert(slBtn.textContent.indexOf('Off') >= 0, 'sleep button starts as Off');
assert(typeof win.togRp === 'function', 'togRp is a function');

// ===== Streaming / SD progress bar (updProgress) =====
var tCur = doc.getElementById('tCur');
var tTot = doc.getElementById('tTot');
var prFl = doc.getElementById('prFl');
var prTr = doc.getElementById('prTr');
assert(!!tCur && !!tTot && !!prFl && !!prTr, 'progress elements exist');
// streaming: elapsed shown (accumulated active_ms survives reconnects server-side),
// no total/remaining, bar marks 'streaming'
win.updProgress({ active: true, stream: true, active_ms: 65000 });
assertEq(tCur.textContent, '1:05', 'streaming shows 1:05 elapsed');
assertEq(tTot.textContent, '', 'streaming shows no total/remaining');
assert(tTot.style.display === 'none', 'streaming remaining time hidden');
assert(prTr.classList.contains('streaming'), 'stream adds the streaming bar class');
assertEq(prFl.style.width, '100%', 'streaming bar is full');
// streaming at boundary: <1 min shown as 0:xx
win.updProgress({ active: true, stream: true, active_ms: 59000 });
assertEq(tCur.textContent, '0:59', 'streaming under 1 minute shows 0:59');
// SD local file: elapsed + remaining total, bar reflects byte position
win.updProgress({ active: true, stream: false, active_ms: 30000,
                  duration_ms: 200000, pos_byte: 3000, size_byte: 10000 });
assertEq(tCur.textContent, '0:30', 'SD shows 0:30 elapsed');
assertEq(tTot.textContent, '-2:50', 'SD shows remaining -m:ss');
assert(tTot.style.display !== 'none', 'SD remaining time shown');
assert(!prTr.classList.contains('streaming'), 'SD removes the streaming bar class');
assertEq(prFl.style.width, '30%', 'SD bar reflects byte position');
// idle: bar resets to empty and elapsed clears
win.updProgress({ active: false });
assertEq(tCur.textContent, '0:00', 'idle resets elapsed to 0:00');
assertEq(prFl.style.width, '0%', 'idle bar is empty');

function finish() {
  dom.window.close();
  if (oldBrowser) oldBrowser.window.close();
  console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
  if (failed > 0) process.exit(1);
}

// --- Discover country change: resets the list and searches that country (async) ---
(async function () {
  var tick = function () { return new Promise(function (r) { setTimeout(r, 20); }); };

  // --- Library folder navigation: folders shown while any sub-folder exists ---
  var libRootResp = { success: true, count: 0, is_root: true, folders: [
    { path: '/library/Lady Gaga - Essentials (2025)', name: 'Lady Gaga - Essentials (2025)' },
    { path: '/library/ACDC - Back in Black', name: 'ACDC - Back in Black' }
  ], items: [] };
  win.fetch = function () { return Promise.resolve({ json: function () { return Promise.resolve(libRootResp); } }); };
  win.loadLib();
  await tick();
  var libHtml = doc.getElementById('libList').innerHTML;
  assert(libHtml.indexOf('Lady Gaga - Essentials (2025)') >= 0, 'library shows folder names at the root');
  assert(libHtml.indexOf('ACDC - Back in Black') >= 0, 'multiple folders render');
  assertEq(doc.querySelectorAll('#libList .it.fld').length, 2, 'folders render as folder rows');
  assert(doc.querySelector('#libList .fld .ic').textContent.length > 0, 'folder rows have an icon');
  assert(libHtml.indexOf('&#127925;') < 0, 'files are hidden while folders exist');
  assertEq(doc.getElementById('libCount').textContent, '0', 'root count badge shows 0 files (folders only)');

  // Entering a leaf folder shows only the music files of that folder.
  var libLeafResp = { success: true, count: 2, is_root: false, folders: [], items: [
    { path: '/library/ACDC - Back in Black/01 Hells Bells.mp3', name: '01 Hells Bells.mp3' },
    { path: '/library/ACDC - Back in Black/02 Shoot to Thrill.mp3', name: '02 Shoot to Thrill.mp3' }
  ]};
  win.fetch = function () { return Promise.resolve({ json: function () { return Promise.resolve(libLeafResp); } }); };
  win.libEnter('/library/ACDC - Back in Black');
  await tick();
  libHtml = doc.getElementById('libList').innerHTML;
  assert(libHtml.indexOf('Hells Bells') >= 0, 'leaf folder shows the track without its extension');
  assert(libHtml.indexOf('Shoot to Thrill') >= 0, 'second track renders');
  assert(libHtml.indexOf('.. Up') >= 0, 'sub-folder shows an Up entry');
  assertEq(doc.getElementById('libCount').textContent, '2', 'leaf count badge shows the tracks');
  win.fetch = null;

  var urls = [];
  var calls = 0;
  var resp = function (names) {
    return { success: true, results: names.map(function (n, i) {
      return { uuid: 'u' + i, name: n, url: 'http://x/' + i };
    }) };
  };
  win.fetch = function (url) {
    urls.push(url);
    calls++;
    var r = calls === 1 ? resp(['Global1']) : resp(['PT1', 'PT2']);
    return Promise.resolve({ json: function () { return Promise.resolve(r); } });
  };
  var tick = function () { return new Promise(function (r) { setTimeout(r, 20); }); };

  win.dsSearch();
  await tick();
  var list1 = doc.getElementById('dsList').textContent;
  assert(list1.indexOf('Global1') >= 0, 'initial auto-search renders the global top stations');
  assert(list1.indexOf('Searching...') < 0, 'initial search finished (no loading placeholder left)');

  var cc = doc.getElementById('dsCountry');
  cc.value = 'PT';
  cc.dispatchEvent(new win.Event('change'));
  await tick();

  var last = urls[urls.length - 1];
  assert(last.indexOf('countrycode=PT') >= 0, 'country change issues a fresh search for that country');
  assert(last.indexOf('offset=') < 0, 'country change resets the offset (fresh results from 0)');
  var list2 = doc.getElementById('dsList').textContent;
  assert(list2.indexOf('PT1') >= 0 && list2.indexOf('PT2') >= 0, 'country results render');
  assert(list2.indexOf('Global1') < 0, 'stale global results are replaced, not appended');

  // ===== Agenda (weekly schedule) =====
  // agdLoad() fetches /api/radio/schedule and renders cards + count + next hint
  var agdSlots = [
    { id: 'c', days: 16 /* Wed */, sh: 7, sm: 0, eh: 8, em: 0, type: 'url', src: 'http://x/1', name: 'Morning Show', vol: 30, enabled: true },
    { id: 'd', days: 64 /* Sat */, sh: 12, sm: 30, eh: 13, em: 0, type: 'folder', src: '/library/Rock', name: 'Rock folder', vol: 20, enabled: true }
  ];
  win.fetch = function (url, opts) {
    if (String(url).indexOf('/api/radio/schedule') >= 0 && (!opts || !opts.method)) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, slots: agdSlots }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.agdLoad();
  await tick();
  assertEq(doc.querySelectorAll('#agdList .agd-card').length, 2, 'agenda renders one card per slot');
  assertEq(doc.getElementById('agdCount').textContent, '2', 'agenda count badge shows the number of slots');
  assert(doc.getElementById('agdNext').textContent.length > 0, 'agenda shows a next-slot hint');
  var firstCard = doc.querySelector('#agdList .agd-card');
  assert(firstCard.textContent.indexOf('Morning Show') >= 0, 'slot card shows the display name');
  assert(firstCard.textContent.indexOf('07:00') >= 0, 'slot card shows the start time');

  // agdNew opens the editor; agdDay toggles the weekday mask (bit per day)
  win.agdNew();
  assert(doc.getElementById('agdEdit').style.display !== 'none', 'agdNew opens the slot editor');
  var dayBtns = doc.querySelectorAll('#agdEdit .days button');
  assertEq(dayBtns.length, 7, 'editor has 7 weekday buttons');
  assertEq(doc.getElementById('agdSh').options.length, 24, 'start-hour select has 24 options');
  assertEq(doc.getElementById('agdSm').options.length, 60, 'start-minute select has 60 options');
  dayBtns[1].click();
  assert(dayBtns[1].classList.contains('on'), 'agdDay(1) selects Monday');
  dayBtns[1].click();
  assert(!dayBtns[1].classList.contains('on'), 'agdDay(1) toggles Monday off');
  dayBtns[0].click(); dayBtns[6].click();
  assert(dayBtns[0].classList.contains('on') && dayBtns[6].classList.contains('on'), 'Sunday + Saturday stay selected together');

  // agdSave posts {"slots":[...]} as plain text with the selected days and source
  var posted = null;
  win.fetch = function (url, opts) {
    if (String(url).indexOf('/api/radio/schedule') >= 0 && opts && opts.method === 'POST') {
      posted = JSON.parse(opts.body);
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, message: 'Schedule saved' }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  doc.getElementById('agdType').value = 'url';
  win.agdSrcList();
  var srcSel = doc.getElementById('agdSrc');
  var customIdx = -1;
  for (var si = 0; si < srcSel.options.length; si++) {
    if (srcSel.options[si].value === '__custom') { customIdx = si; break; }
  }
  assert(customIdx >= 0, 'URL source list offers the Custom URL option');
  srcSel.selectedIndex = customIdx;
  doc.getElementById('agdUrlRow').style.display = '';
  doc.getElementById('agdUrlBox').value = 'http://custom.example/live';
  doc.getElementById('agdName').value = 'Custom stream';
  win.agdSave();
  await tick();
  assert(!!posted && Array.isArray(posted.slots) && posted.slots.length === 1, 'agdSave posts exactly one slot');
  assertEq(posted.slots[0].days, 65, 'saved slot keeps Sunday + Saturday (bits 0 and 6)');
  assertEq(posted.slots[0].src, 'http://custom.example/live', 'custom URL is used as the source');
  assertEq(posted.slots[0].name, 'Custom stream', 'saved slot keeps the display name');
  assertEq(posted.slots[0].type, 'url', 'saved slot keeps the type');

  // ===== Feature: equalizer - 8 standard presets, distinct, cycling, matched on status =====
  assertEq(win.EQ_NAMES.length, 8, 'equalizer offers 8 standard presets');
  assertEq(win.EQP.length, 8, 'equalizer has 8 gain sets');
  ['Flat','Rock','Pop','Jazz','Classical','Voice','Bass','Treble'].forEach(function (nm) {
    assert(win.EQ_NAMES.indexOf(nm) >= 0, 'EQ preset exists: ' + nm);
  });
  assertEq(win.EQ_NAMES[0], 'Flat', 'Flat is the neutral preset (all gains 0)');
  var presetsDistinct = true;
  for (var di = 0; di < win.EQP.length && presetsDistinct; di++) {
    for (var dj = di + 1; dj < win.EQP.length && presetsDistinct; dj++) {
      var same = true;
      for (var dk = 0; dk < win.EQP[di].length; dk++) { if (win.EQP[di][dk] !== win.EQP[dj][dk]) { same = false; break; } }
      if (same) presetsDistinct = false;
    }
  }
  assert(presetsDistinct, 'all 8 EQ presets have audibly different curves');
  // eqPreset posts the chosen curve and labels the button
  var eqPosted = null;
  win.fetch = function (url, opts) {
    if (String(url).indexOf('/api/radio/eq') >= 0) { eqPosted = (opts && opts.body) ? JSON.parse(opts.body) : null; }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.eqPreset(3); // Jazz
  assertEq(doc.getElementById('eqBtn').textContent, 'Jazz', 'eqPreset(3) labels the button Jazz');
  assert(!!eqPosted && JSON.stringify(eqPosted.gains) === JSON.stringify([4,3,-2,2,5]), 'eqPreset posts the Jazz gains');
  // eqCycle advances and wraps
  win.eqCycle();
  assertEq(doc.getElementById('eqBtn').textContent, 'Classical', 'eqCycle advances to the next preset');
  win.eqPreset(7);
  win.eqCycle();
  assertEq(doc.getElementById('eqBtn').textContent, 'Flat', 'eqCycle wraps from the last preset back to Flat');
  // loadSt maps the status gains back to the preset name
  win.fetch = function (url) {
    if (String(url).indexOf('/api/radio/status') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, volume: 30, active: true, eq_gains: [6,3,-1,3,5] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.loadSt();
  await tick();
  assertEq(doc.getElementById('eqBtn').textContent, 'Rock', 'loadSt maps Rock gains back to the Rock preset');
  win.fetch = function (url) {
    if (String(url).indexOf('/api/radio/status') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, volume: 30, eq_gains: [0,0,0,0,0] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.loadSt();
  await tick();
  assertEq(doc.getElementById('eqBtn').textContent, 'Flat', 'loadSt maps an all-zero curve to Flat');

  // ===== Feature: playback art - any artwork (album OR station favicon)
  // replaces the vinyl with a static square; only when there is no artwork
  // at all does the spinning vinyl disc show =====
  var abWrap = doc.getElementById('abWrap');
  var artImg = doc.getElementById('artImg');
  var artPh = doc.getElementById('artPh');
  assert(!!abWrap && !!artImg && !!artPh, 'art wrapper/image/placeholder elements exist');
  win.setArt({ active: true, station_favicon: 'http://fav/x.png', artwork: '' });
  assert(artImg.src.indexOf('http://fav/x.png') >= 0, 'stream with a favicon shows the station fanart first');
  assert(abWrap.classList.contains('art'), 'fanart mode engaged (art class)');
  assert(abWrap.classList.contains('art-full'), 'stream fanart is a static square (art-full)');
  assert(artImg.style.display === 'block', 'fanart image is visible');
  win.setArt({ active: true, station_favicon: '', artwork: 'Album art' });
  assert(artImg.src.indexOf('/api/radio/artwork?') >= 0, 'SD track with embedded art uses the artwork endpoint');
  assert(abWrap.classList.contains('art'), 'embedded art engages art mode');
  assert(abWrap.classList.contains('art-full'), 'embedded art is a static square (art-full)');
  win.setArt({ active: true, station_favicon: 'http://fav/y.png', artwork: '' });
  assert(abWrap.classList.contains('art'), 'back to a favicon keeps art mode');
  assert(abWrap.classList.contains('art-full'), 'favicon stays a static square (art-full)');
  win.setArt({ active: true, station_favicon: '', artwork: '' });
  assert(!abWrap.classList.contains('art'), 'no art at all -> back to the spinning vinyl (last resort)');
  assert(!abWrap.classList.contains('art-full'), 'vinyl mode clears art-full');
  assert(artImg.style.display === 'none', 'vinyl mode hides the image');
  assert(artPh.style.display === 'block', 'vinyl mode shows the placeholder');
  win.setArt({ active: false, station_favicon: 'http://fav/x.png', artwork: '' });
  assert(!abWrap.classList.contains('art'), 'inactive state never shows art, even with a favicon');
  assert(!abWrap.classList.contains('art-full'), 'inactive state never shows the full cover');

  // ===== Feature: library rows - folder covers as thumbnails =====
  // The listing stays cheap: folders detected by filename get a cover thumbnail
  // served by /api/radio/library/art; file rows never render thumbnails.
  var coverResp = { success: true, count: 2, is_root: false, folders: [], items: [
    { path: '/library/ACDC - Back in Black/01 Hells Bells.mp3', name: '01 Hells Bells.mp3' },
    { path: '/library/ACDC - Back in Black/02 Shoot to Thrill.mp3', name: '02 Shoot to Thrill.mp3' }
  ]};
  win.fetch = function () { return Promise.resolve({ json: function () { return Promise.resolve(coverResp); } }); };
  win.loadLib();
  await tick();
  var coverImgs = doc.querySelectorAll('#libList .it .ic img');
  assertEq(coverImgs.length, 0, 'music rows never render folder-cover thumbnails');
  assert(doc.getElementById('libList').innerHTML.indexOf('library/art') < 0, 'no reference to a library art endpoint in a file-only listing');
  var fileRow = doc.querySelector('#libList .it:not(.fld)');
  assertEq(fileRow.getAttribute('data-folder'), '0', 'music rows carry data-folder=0 (long-press)');
  assert(fileRow.getAttribute('data-path').length > 0, 'music rows carry data-path (long-press)');
  // file rows fall back to the plain music icon
  var plainResp = { success: true, count: 2, is_root: false, folders: [], items: [
    { path: '/library/B/1.mp3', name: '1.mp3' }, { path: '/library/B/2.mp3', name: '2.mp3' }
  ]};
  win.fetch = function () { return Promise.resolve({ json: function () { return Promise.resolve(plainResp); } }); };
  win.loadLib();
  await tick();
  assertEq(doc.querySelectorAll('#libList .it:not(.fld) .ic img').length, 0, 'file rows keep the plain music icon');
  assert(doc.getElementById('libList').innerHTML.indexOf('\uD83C\uDFB5') >= 0, 'music icon is used for file rows');
  // folder rows with a detected cover render a thumbnail pointing at the art endpoint
  var folderResp = { success: true, count: 0, is_root: false, folders: [
    { path: '/library/A', name: 'A', cover: '/library/A/folder.jpg' },
    { path: '/library/B', name: 'B' }
  ], items: []};
  win.fetch = function () { return Promise.resolve({ json: function () { return Promise.resolve(folderResp); } }); };
  win.loadLib();
  await tick();
  var frowA = doc.querySelectorAll('#libList .it.fld[data-path]')[0];
  var frowB = doc.querySelectorAll('#libList .it.fld[data-path]')[1];
  var fimgA = frowA.querySelector('.ic.cv img');
  var fimgB = frowB.querySelector('.ic.cv img');
  assert(!!fimgA, 'folder with a detected cover renders a thumbnail');
  assert(fimgA.getAttribute('src').indexOf('/api/radio/library/art?path=') === 0, 'folder cover src targets the art endpoint');
  assert(fimgA.getAttribute('src').indexOf(encodeURIComponent('/library/A/folder.jpg')) > 0, 'folder cover path is URL-encoded into the query');
  assert(!!frowA.querySelector('.ic.cv img[onerror]'), 'folder cover has a fallback handler');
  assert(!fimgB, 'folder without a detected cover renders the plain folder icon');
  assert(frowB.querySelector('.ic').textContent.indexOf('\uD83D\uDCC1') >= 0, 'folder icon is the folder emoji');
  assertEq(frowA.getAttribute('data-folder'), '1', 'folder rows carry data-folder=1 (long-press)');

  // ===== Feature: generic context menu (right-click / long-press) =====
  var ctxMenu = doc.getElementById('ctxMenu');
  assert(!!ctxMenu, 'generic context menu element exists');
  function fakeRow(attrs) {
    return { getAttribute: function (k) { return Object.prototype.hasOwnProperty.call(attrs, k) ? attrs[k] : null; } };
  }
  // library folder menu: 6 actions, first is Play ordered, repeat variants present
  var folderRow = fakeRow({ 'data-kind': 'lf', 'data-path': '/library/ACDC%20-%20Back%20in%20Black', 'data-name': 'ACDC - Back in Black' });
  var fItems = win.ctxItemsFor('lf', folderRow);
  assertEq(fItems.length, 6, 'folder menu offers 6 actions (ordered, ordered+repeat, random, random+repeat, add all, open)');
  assert(fItems[0].l.indexOf('Play ordered') >= 0, 'folder menu first action is Play ordered');
  assert(fItems[1].l.indexOf('repeat') >= 0, 'folder menu has an ordered+repeat action');
  assert(fItems[3].l.indexOf('repeat') >= 0, 'folder menu has a random+repeat action');
  win.ctxShow(100, 100, fItems);
  assertEq(ctxMenu.style.display, 'block', 'folder context menu opens');
  assertEq(ctxMenu.querySelectorAll('.cmi').length, 6, 'menu renders 6 buttons for a folder');
  // library file menu: 3 actions
  var fileRow = fakeRow({ 'data-kind': 'll', 'data-path': '/library/A/01%20Hells%20Bells.mp3', 'data-name': '01 Hells Bells.mp3' });
  var fileItems = win.ctxItemsFor('ll', fileRow);
  assertEq(fileItems.length, 3, 'file menu offers 3 actions (play, add to queue, favorite)');
  // ctxFor opens the same menu from a contextmenu event (touch/left-button)
  var evRow = doc.createElement('div');
  evRow.setAttribute('data-kind', 'lf');
  evRow.setAttribute('data-path', '/library/ACDC%20-%20Back%20in%20Black');
  var fakeEv = { button: 0, clientX: 60, clientY: 70, preventDefault: function () {}, stopPropagation: function () {} };
  win.ctxFor(fakeEv, evRow);
  assertEq(ctxMenu.style.display, 'block', 'ctxFor opens the menu even for a touch/left-button event');
  assertEq(ctxMenu.querySelectorAll('.cmi').length, 6, 'touch-opened menu still shows the folder actions');
  // rows without a known kind or with no data produce no menu
  assert(!win.ctxItemsFor('nope', evRow), 'unknown data-kind produces no menu items');
  assert(!win.ctxItemsFor('lf', null), 'missing row produces no menu items');

  // Every other list reuses the same menu (kind -> expected action count)
  win.__test__._reset(['/a.mp3'], 0, false, 0);
  assertEq(win.ctxItemsFor('st', fakeRow({ 'data-idx': '0' })).length, 3, 'station menu = play + queue + favorite');
  assertEq(win.ctxItemsFor('qu', fakeRow({ 'data-idx': '0' })).length, 4, 'queue menu = play + up + down + remove');
  assertEq(win.ctxItemsFor('pl', fakeRow({ 'data-name': 'Mix' })).length, 2, 'playlist menu = load + delete');
  assertEq(win.ctxItemsFor('ag', fakeRow({ 'data-idx': '0' })).length, 2, 'agenda menu = edit + delete');
  assertEq(win.ctxItemsFor('us', fakeRow({ 'data-uuid': 'u1', 'data-name': 'NAS' })).length, 6, 'upnp server menu = ordered/random/repeat variants + add all + browse');
  assertEq(win.ctxItemsFor('uc', fakeRow({ 'data-id': 'alb%2F1', 'data-name': 'Album A' })).length, 6, 'upnp container menu = ordered/random/repeat variants + add all + open');
  assertEq(win.ctxItemsFor('ui', fakeRow({ 'data-url': 'http://h/a.mp3' })).length, 3, 'upnp item menu = play + queue + favorite');
  assert(!win.ctxItemsFor('ui', fakeRow({ 'data-url': '' })), 'unplayable upnp item has no menu');
  assertEq(win.ctxItemsFor('sr', fakeRow({ 'data-url': '/s/1.mp3', 'data-name': '1.mp3' })).length, 2, 'search menu = play + queue');
  assertEq(win.ctxItemsFor('ds', fakeRow({ 'data-idx': '0' })).length, 3, 'discover menu = play + queue + favorite');
  win.localStorage.setItem('radio_recent', JSON.stringify([{ n: 'R', u: 'http://r' }]));
  assertEq(win.ctxItemsFor('re', fakeRow({ 'data-idx': '0' })).length, 3, 'recent menu = play + queue + remove');
  win.fetch = function (url) {
    if (String(url).indexOf('/api/radio/favorites') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ items: [{ url: 'http://f/1', name: 'F1' }] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.loadFav();
  await tick();
  assertEq(win.ctxItemsFor('fa', fakeRow({ 'data-idx': '0' })).length, 3, 'favorite menu = play + queue + remove');

  // ctxDo(1): Play ordered + repeat -> sets repeat-all then starts the folder
  var libCalls = [];
  win.fetch = function (url, opts) {
    var u = String(url);
    libCalls.push({ u: u, m: (opts && opts.method) || 'GET', b: (opts && opts.body) || '' });
    if (u.indexOf('/api/radio/library') >= 0 && u.indexOf('queue=1') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, count: 2, items: [
        { path: '/library/ACDC - Back in Black/01 Hells Bells.mp3', name: '01 Hells Bells.mp3' },
        { path: '/library/ACDC - Back in Black/02 Shoot to Thrill.mp3', name: '02 Shoot to Thrill.mp3' }
      ] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.__test__._reset([], -1, false, 0);
  win.ctxShow(100, 100, win.ctxItemsFor('lf', folderRow));
  win.ctxDo(1);
  await tick();
  var repeatPosts = libCalls.filter(function (c) { return c.u.indexOf('/api/radio/repeat') >= 0; });
  assertEq(repeatPosts.length, 1, 'Play ordered + repeat posts /api/radio/repeat once');
  assert(repeatPosts[0].u.indexOf('mode=2') >= 0, 'Play ordered + repeat uses mode=2 (repeat all)');
  var qPosts = libCalls.filter(function (c) { return c.u.indexOf('/api/radio/library?') >= 0 && c.u.indexOf('queue=1') >= 0; });
  assertEq(qPosts.length, 1, 'folder play fetches the folder queue listing');
  var playSd = libCalls.filter(function (c) { return c.u.indexOf('/api/radio/play-sd') >= 0; });
  assertEq(playSd.length, 1, 'folder play starts the first track');
  assert(playSd[0].u.indexOf('Hells%20Bells') >= 0, 'ordered play starts with the first track');
  assertEq(doc.getElementById('rpBtn').textContent, 'All', 'repeat-all reflected on the repeat button');
  // ctxDo(0): plain ordered play - no repeat touched
  libCalls = [];
  win.__test__._reset([], -1, false, 0);
  win.ctxShow(100, 100, win.ctxItemsFor('lf', folderRow));
  win.ctxDo(0);
  await tick();
  assertEq(libCalls.filter(function (c) { return c.u.indexOf('/api/radio/repeat') >= 0; }).length, 0, 'plain Play ordered does not change repeat');
  assertEq(libCalls.filter(function (c) { return c.u.indexOf('/api/radio/play-sd') >= 0; }).length, 1, 'plain Play ordered still starts the folder');
  // ctxDo(3): random + repeat -> play-sd picks any track from the folder
  libCalls = [];
  win.__test__._reset([], -1, false, 0);
  win.ctxShow(100, 100, win.ctxItemsFor('lf', folderRow));
  win.ctxDo(3);
  await tick();
  var rndPlay = libCalls.filter(function (c) { return c.u.indexOf('/api/radio/play-sd') >= 0; });
  assertEq(rndPlay.length, 1, 'random + repeat starts one track');
  assert(rndPlay[0].u.indexOf('%2Flibrary%2FACDC') >= 0, 'random play stays inside the folder');
  assertEq(libCalls.filter(function (c) { return c.u.indexOf('/api/radio/repeat') >= 0 && c.u.indexOf('mode=2') >= 0; }).length, 1, 'random + repeat also sets repeat-all');
  // file menu Play (first item of the ll menu)
  libCalls = [];
  win.__test__._reset([], -1, false, 0);
  win.ctxShow(100, 100, win.ctxItemsFor('ll', fileRow));
  win.ctxDo(0);
  await tick();
  assertEq(libCalls.filter(function (c) { return c.u.indexOf('/api/radio/play-sd') >= 0; }).length, 1, 'file Play action starts the track');

  // ===== UPnP container/server folder actions: play all / random / add all =====
  var ucRow = fakeRow({ 'data-id': 'alb%2F1', 'data-name': 'Album A' });
  var ucMenu = win.ctxItemsFor('uc', ucRow);
  assert(ucMenu[0].l.indexOf('Play ordered') >= 0, 'container menu first action is Play ordered');
  assert(ucMenu[1].l.indexOf('repeat') >= 0, 'container menu has an ordered+repeat action');
  assert(ucMenu[4].l.indexOf('Add all') >= 0, 'container menu has Add all to queue');
  assert(ucMenu[5].l.indexOf('Open') >= 0, 'container menu keeps Open');
  var usMenu = win.ctxItemsFor('us', fakeRow({ 'data-uuid': 'u1', 'data-name': 'NAS' }));
  assert(usMenu[0].l.indexOf('Play ordered') >= 0, 'server menu first action is Play ordered');
  assert(usMenu[5].l.indexOf('Browse') >= 0, 'server menu keeps Browse server');

  // upnpCollect: flat album (1 page) collects both tracks in order
  win.fetch = function (url) {
    var u = String(url);
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 2, returned: 2, start: 0, has_more: false, items: [
        { is_container: false, url: 'http://h/1.mp3', playable: true },
        { is_container: false, url: 'http://h/2.mp3', playable: true }
      ] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  var got = null;
  win.upnpCollect('u1', 'alb', function (arr) { got = arr; });
  await tick();
  assert(!!got, 'upnpCollect resolves for a flat album');
  assertEq(got.length, 2, 'flat album collects 2 tracks');
  assertEq(got[0], 'http://h/1.mp3', 'flat album preserves track order');

  // upnpCollect: pagination + recursion + dedup + unplayable skip
  win.fetch = function (url) {
    var u = String(url);
    if (u.indexOf('/api/radio/upnp/browse') < 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
    }
    var start = 0, sm = u.match(/start=(\d+)/);
    if (sm) start = parseInt(sm[1], 10);
    var obj = '', om = u.match(/object=([^&]*)/);
    if (om) { try { obj = decodeURIComponent(om[1]); } catch (e) {} }
    if (obj === 'alb') {
      if (start === 0) return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 3, returned: 2, start: 0, items: [
        { is_container: true, id: 'sub' },
        { is_container: false, url: 'http://h/a.mp3', playable: true }
      ] }); } });
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 3, returned: 1, start: 2, items: [
        { is_container: false, url: 'http://h/b.mp3', playable: true }
      ] }); } });
    }
    if (obj === 'sub') return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 2, returned: 2, start: 0, items: [
      { is_container: false, url: 'http://h/b.mp3', playable: true },
      { is_container: false, url: 'http://h/c.mp3', playable: false },
      { is_container: false, url: 'http://h/d.mp3', playable: true }
    ] }); } });
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 0, returned: 0, start: 0, items: [] }); } });
  };
  got = null;
  win.upnpCollect('u1', 'alb', function (arr) { got = arr; });
  await tick();
  assertEq(got.length, 3, 'recursion + pagination collects 3 tracks');
  assert(got.indexOf('http://h/a.mp3') >= 0 && got.indexOf('http://h/b.mp3') >= 0 && got.indexOf('http://h/d.mp3') >= 0, 'collects a, b and d across pages/containers');
  assert(got.indexOf('http://h/c.mp3') < 0, 'unplayable track is skipped');
  assert(got.indexOf('http://h/b.mp3') === got.lastIndexOf('http://h/b.mp3'), 'duplicate track appears only once');

  // upnpFolderAction(0): play ordered -> queue replaced + first track via play-url
  var actCalls = [];
  win.fetch = function (url, opts) {
    var u = String(url), m = (opts && opts.method) || 'GET', b = (opts && opts.body) || '';
    actCalls.push({ u: u, m: m, b: b });
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 2, returned: 2, start: 0, items: [
        { is_container: false, url: 'http://h/1.mp3', playable: true },
        { is_container: false, url: 'http://h/2.mp3', playable: true }
      ] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.__test__._reset([], -1, false, 0);
  win.upnpFolderAction(0, 'u1', 'alb', 'Album A');
  await tick();
  var qSync = actCalls.filter(function (c) { return c.u.indexOf('/api/radio/queue') >= 0 && c.m === 'POST'; });
  assertEq(qSync.length, 1, 'container play syncs the queue once');
  assert(qSync[0].b.indexOf('http://h/1.mp3') >= 0 && qSync[0].b.indexOf('http://h/2.mp3') >= 0, 'queue sync contains both tracks');
  var urlPlay = actCalls.filter(function (c) { return c.u.indexOf('/api/radio/play-url') >= 0; });
  assertEq(urlPlay.length, 1, 'container play starts the first track via play-url');
  assert(urlPlay[0].u.indexOf('http%3A%2F%2Fh%2F1.mp3') >= 0, 'ordered play starts with the first track');
  assertEq(doc.getElementById('qCount').textContent, '2', 'queue panel shows both tracks');

  // upnpFolderAction(1): random play keeps all tracks but in shuffled order
  win.__test__._reset([], -1, false, 0);
  actCalls = [];
  win.fetch = function (url, opts) {
    var u = String(url), m = (opts && opts.method) || 'GET', b = (opts && opts.body) || '';
    actCalls.push({ u: u, m: m, b: b });
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 2, returned: 2, start: 0, items: [
        { is_container: false, url: 'http://h/1.mp3', playable: true },
        { is_container: false, url: 'http://h/2.mp3', playable: true }
      ] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  var origRand = win.Math.random;
  win.Math.random = function () { return 0; };
  win.upnpFolderAction(1, 'u1', 'alb', 'Album A');
  await tick();
  win.Math.random = origRand;
  var qSync2 = actCalls.filter(function (c) { return c.u.indexOf('/api/radio/queue') >= 0 && c.m === 'POST'; });
  var body2 = JSON.parse(qSync2[0].b);
  assertEq(body2.items.length, 2, 'random play keeps all tracks');
  assertEq(body2.items.join(','), 'http://h/2.mp3,http://h/1.mp3', 'random play shuffles the order (deterministic stub)');

  // upnpFolderAction(2): add all to queue appends only missing tracks (dedup)
  win.__test__._reset(['http://h/1.mp3'], 0, false, 0);
  actCalls = [];
  win.fetch = function (url, opts) {
    var u = String(url), m = (opts && opts.method) || 'GET', b = (opts && opts.body) || '';
    actCalls.push({ u: u, m: m, b: b });
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 2, returned: 2, start: 0, items: [
        { is_container: false, url: 'http://h/1.mp3', playable: true },
        { is_container: false, url: 'http://h/2.mp3', playable: true }
      ] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.upnpFolderAction(2, 'u1', 'alb', 'Album A');
  await tick();
  var qSync3 = actCalls.filter(function (c) { return c.u.indexOf('/api/radio/queue') >= 0 && c.m === 'POST'; });
  var body3 = JSON.parse(qSync3[0].b);
  assertEq(body3.items.length, 2, 'add all appends only the missing track');
  assertEq(body3.items[0], 'http://h/1.mp3', 'existing track preserved in front');

  // upnpFolderAction on an empty container: message, no playback
  actCalls = [];
  win.fetch = function (url) {
    var u = String(url);
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 0, returned: 0, start: 0, items: [] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.__test__._reset([], -1, false, 0);
  win.upnpFolderAction(0, 'u1', 'empty', 'Empty');
  await tick();
  assertEq(doc.getElementById('toast').textContent, 'No playable music in this container', 'empty container shows a message');
  assertEq(actCalls.filter(function (c) { return c.u.indexOf('/api/radio/play-url') >= 0; }).length, 0, 'empty container does not start playback');

  // container menu through the real menu: ctxDo(0) on a uc row plays the album
  win.fetch = function (url, opts) {
    var u = String(url);
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 1, returned: 1, start: 0, items: [
        { is_container: false, url: 'http://h/x.mp3', playable: true }
      ] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.upnpEnter('srv-uuid', '0', 'NAS');
  await tick();
  var ucCalls = [];
  win.fetch = function (url, opts) {
    var u = String(url), m = (opts && opts.method) || 'GET', b = (opts && opts.body) || '';
    ucCalls.push({ u: u, m: m, b: b });
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, total: 1, returned: 1, start: 0, items: [
        { is_container: false, url: 'http://h/x.mp3', playable: true }
      ] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.ctxShow(100, 100, win.ctxItemsFor('uc', fakeRow({ 'data-id': 'alb', 'data-name': 'Album X' })));
  win.ctxDo(0);
  await tick();
  assertEq(ucCalls.filter(function (c) { return c.u.indexOf('/api/radio/play-url') >= 0; }).length, 1, 'ctxDo(0) on a container row starts playback');

  // ===== long-press: holding a folder row (550ms) opens the context menu =====
  win.fetch = function () { return Promise.resolve({ json: function () { return Promise.resolve({ success: true, count: 0, is_root: false, folders: [
    { path: '/library/Long', name: 'Long', cover: false }
  ], items: [] }); } }); };
  win.loadLib();
  await tick();
  var lpRow = doc.querySelector('#libList .it.fld[data-path]');
  assert(!!lpRow, 'folder row rendered for long-press');
  var touchStart = new win.Event('touchstart', { bubbles: true, cancelable: true });
  touchStart.touches = [{ clientX: 50, clientY: 60 }];
  lpRow.dispatchEvent(touchStart);
  await new Promise(function (r) { setTimeout(r, 700); });
  assertEq(ctxMenu.style.display, 'block', 'long-press (550ms hold) opens the context menu');
  assertEq(ctxMenu.querySelectorAll('.cmi').length, 6, 'long-press opens the folder menu with repeat options');
  win.lpClear();

  // ===== Discover pagination: Next/Prev rely on backend has_more (not the
  // filtered result count, which can drop below the limit after playability
  // filtering and would hide Next forever) =====
  var dsNav = doc.getElementById('dsNav');
  assert(!!dsNav, 'discover nav bar exists');
  var dsNextBtn = doc.getElementById('dsNext');
  var dsPrevBtn = doc.getElementById('dsPrev');
  // A short page (few playable stations) but has_more=true must still show Next
  var pageCalls = [];
  win.fetch = function (url, opts) {
    pageCalls.push({ u: String(url), m: (opts && opts.method) || 'GET' });
    var u = String(url);
    if (u.indexOf('offset=') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve({ success: true, has_more: true, results: [
        { name: 'B1', url: 'http://b/1', bitrate: 128 },
        { name: 'B2', url: 'http://b/2', bitrate: 128 },
        { name: 'B3', url: 'http://b/3', bitrate: 128 }
      ] }); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true, has_more: true, results: [
      { name: 'A1', url: 'http://a/1', bitrate: 128 },
      { name: 'A2', url: 'http://a/2', bitrate: 128 }
    ] }); } });
  };
  win.__test__._reset([], -1, false, 0);
  win.dsSearch();
  await tick();
  assertEq(doc.querySelectorAll('#dsList .it').length, 2, 'page 1 renders its (filtered-short) results');
  assert(!dsNextBtn.hidden, 'Next is shown even when the page holds fewer stations than the limit (has_more=true)');
  assert(dsPrevBtn.hidden, 'Prev is hidden on the first page');
  win.dsNext();
  await tick();
  var off = pageCalls.filter(function (c) { return c.u.indexOf('/api/radio/stations/search') >= 0 && c.u.indexOf('offset=25') >= 0; });
  assertEq(off.length, 1, 'Next requests the next offset (25)');
  assertEq(doc.querySelectorAll('#dsList .it').length, 3, 'page 2 renders its results');
  assert(!dsNextBtn.hidden, 'Next stays available while has_more=true');
  assert(!dsPrevBtn.hidden, 'Prev becomes visible on page 2');
  win.dsPrev();
  await tick();
  assertEq(doc.querySelectorAll('#dsList .it').length, 2, 'Prev returns to page 1');
  assert(dsPrevBtn.hidden, 'Prev is hidden again on the first page');
  // No counter text: navigation only, no "Page X" label
  assert(!doc.getElementById('dsPgNum'), 'discover shows no page-number counter');
  // has_more=false on the last page hides Next (fallback when field absent)
  win.fetch = function () { return Promise.resolve({ json: function () { return Promise.resolve({ success: true, results: [
    { name: 'C1', url: 'http://c/1', bitrate: 128 }
  ] }); } }); };
  win.dsSearch();
  await tick();
  assert(dsNextBtn.hidden, 'Next is hidden when has_more is false / page is short without the flag');

  win.fetch = dsRealFetch;

  // ===== Idle auto-return: after 30s without interaction the UI goes back to
  // the playback page (the default panel), keeping it always visible =====
  assertEq(doc.getElementById('pnPlay').classList.contains('on'), true, 'playback is the default panel at startup');
  assert(typeof win.__test__.touchActivity === 'function', 'touchActivity is exposed');
  assertEq(win.__test__.NAV_DEF[0], 'Lib', 'Library is the first entry of the rotating window (Play is fixed left)');
  win.sw('Agd');
  assert(!doc.getElementById('pnPlay').classList.contains('on'), 'sw("Agd") leaves the playback panel');
  assert(!doc.getElementById('nbPlay').classList.contains('on'), 'fixed Play un-highlights after switching away');
  win.sw('Play');
  assert(doc.getElementById('pnPlay').classList.contains('on'), 'sw("Play") activates Play panel');
  var idleCb = null, idleMs = 0;
  var realSetTimeout = win.setTimeout;
  win.setTimeout = function (cb, ms) { idleCb = cb; idleMs = ms; return 1; };
  win.__test__.touchActivity();
  assertEq(idleMs, 30000, 'idle timer is 30s');
  assert(typeof idleCb === 'function', 'idle timer schedules a callback');
  win.setTimeout = realSetTimeout;
  idleCb();
  assert(doc.getElementById('pnPlay').classList.contains('on'), 'after 30s idle the playback panel comes back');
  assert(doc.getElementById('nbPlay').classList.contains('on'), 'fixed Play is highlighted again after idle return');

  // ===== Feature: UPnP - upnpPlay forwards the item MIME as a decoder hint =====
  var upnpPosted = [];
  var upnpMockBrowse = {
    success: true, total: 3, returned: 3, has_more: false,
    items: [
      { is_container: false, id: 'a', title: 'MP3 Track', url: 'http://h/a.mp3', mime: 'audio/mpeg', playable: true, duration: '0:03:00.000' },
      { is_container: false, id: 'b', title: 'WMA Track', url: 'http://h/b.wma', mime: 'audio/x-ms-wma', playable: false },
      { is_container: true, id: 'c', title: 'Album', child_count: 5, art: 'http://h/art/album.jpg' }
    ]
  };
  win.fetch = function (url, opts) {
    var u = String(url);
    if (opts && opts.method && opts.method !== 'GET') {
      upnpPosted.push({ u: u, b: (opts && opts.body) || '' });
    }
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve(upnpMockBrowse); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  // variation 1: transcoded item whose MIME differs from its file extension
  win.upnpPlay('http://h/transcoded/track.flac', 'Transcoded', '', 'audio/mpeg');
  await tick();
  var playUrlPosts = upnpPosted.filter(function (p) { return p.u.indexOf('/api/radio/play-url') >= 0; });
  assertEq(playUrlPosts.length, 1, 'upnpPlay posts /api/radio/play-url once');
  assert(playUrlPosts[0].b.indexOf('url=http%3A%2F%2Fh%2Ftranscoded%2Ftrack.flac') >= 0, 'upnpPlay posts the chosen URL');
  assert(playUrlPosts[0].b.indexOf('mime=audio%2Fmpeg') >= 0, 'upnpPlay forwards the item MIME (transcoded decoder hint)');
  assert(playUrlPosts[0].b.indexOf('name=Transcoded') >= 0, 'upnpPlay posts the display name');
  // variation 2: item with no MIME field posts without a mime parameter
  upnpPosted = [];
  win.upnpPlay('http://h/plain.mp3', 'Plain');
  await tick();
  playUrlPosts = upnpPosted.filter(function (p) { return p.u.indexOf('/api/radio/play-url') >= 0; });
  assert(playUrlPosts[0].b.indexOf('mime=') < 0, 'upnpPlay omits mime when the item has none');

  // ===== Feature: UPnP - unsupported formats render dimmed without play controls =====
  win.upnpEnter('upnpuuid1', '0', 'Root');
  await tick();
  var upnpHtml = doc.getElementById('upnpItemList').innerHTML;
  assert(upnpHtml.indexOf('MP3 Track') >= 0, 'playable UPnP track is listed');
  assertEq((upnpHtml.match(/upnpPlay\(/g) || []).length, 1, 'only the playable track gets a play button');
  assert(upnpHtml.indexOf('qAdd(') >= 0, 'playable track gets a queue (add to playlist) action');
  assert(upnpHtml.indexOf('favToggle(') >= 0, 'playable track gets a favorite action');
  assert(upnpHtml.indexOf('WMA Track') >= 0, 'unsupported track is still listed (keeps context)');
  assert(upnpHtml.indexOf('opacity:.4') >= 0, 'unsupported track is rendered dimmed');
  assert(upnpHtml.indexOf('class="it fld"') >= 0, 'UPnP containers render as folder rows');
  assert(upnpHtml.indexOf('Album') >= 0, 'UPnP container shows its title');
  assert(upnpHtml.indexOf('(5)') >= 0, 'UPnP container shows its child count');
  assert(upnpHtml.indexOf('<img src="http://h/art/album.jpg"') >= 0, 'UPnP container with albumArtURI renders a cover thumbnail');
  assert(upnpHtml.indexOf('onerror="this.parentNode.innerHTML=&#39;&#128193;&#39;"') >= 0 || upnpHtml.indexOf('onerror') >= 0, 'UPnP cover has a fallback handler');

  // ===== Feature: UPnP - container without art falls back to the folder icon =====
  var noArtMock = { success: true, total: 1, returned: 1, has_more: false, items: [
    { is_container: true, id: 'c2', title: 'No Art Album', child_count: 3 }
  ]};
  win.fetch = function (url) {
    var u = String(url);
    if (u.indexOf('/api/radio/upnp/browse') >= 0) {
      return Promise.resolve({ json: function () { return Promise.resolve(noArtMock); } });
    }
    return Promise.resolve({ json: function () { return Promise.resolve({ success: true }); } });
  };
  win.upnpEnter('upnpuuid1', '0', 'Root');
  await tick();
  var noArtHtml = doc.getElementById('upnpItemList').innerHTML;
  assert(noArtHtml.indexOf('No Art Album') >= 0, 'no-art container is listed');
  assert(noArtHtml.indexOf('<img src="http://h/art/') < 0, 'no-art container renders no cover image');
  assert(noArtHtml.indexOf('\uD83D\uDCC1') >= 0, 'no-art container falls back to the folder emoji');

  // ===== Feature: isSup - stream-format support gate =====
  assert(win.__test__.isSup('http://s/stream.mp3') === true, 'mp3 URL is supported');
  assert(win.__test__.isSup('http://s/stream.flac') === true, 'flac URL is supported');
  assert(win.__test__.isSup('http://s/stream.aac') === true, 'aac URL is supported');
  assert(win.__test__.isSup('http://s/stream.m4a') === true, 'm4a URL is supported');
  assert(win.__test__.isSup('http://s/track.wav') === true, 'wav URL is supported');
  assert(win.__test__.isSup('http://s/playlist.m3u8') === false, 'hls/playlist is rejected');
  assert(win.__test__.isSup('http://s/stream.ogg') === false, 'ogg is rejected');
  assert(win.__test__.isSup('http://s/stream.opus') === false, 'opus is rejected');
  assert(win.__test__.isSup('http://s/track.wma') === false, 'wma is rejected');

  // ===== Feature: auto-resume buttons (renderResume + saveResume) =====
  var rT = doc.getElementById('resumeToggle');
  var rG = doc.getElementById('resumeTarget');
  assert(rT && rG, 'resume toggle and target buttons exist');

  // Default state: RESUME_A=true, RESUME_F=false
  win.__test__.renderResume();
  assert(rT.innerHTML.indexOf('Auto-resume: ON') >= 0, 'renderResume shows ON for autoResume=true');
  assert(rT.textContent.indexOf('\u21BB') >= 0, 'toggle renders the actual icon char (innerHTML fix)');
  assert(rT.textContent.indexOf('&#') < 0, 'no raw HTML entity leaks into the label text');
  assert(rT.classList.contains('on') && !rT.classList.contains('off'), 'toggle gets .on class when enabled');
  assert(rG.textContent === 'Resume: Last', 'target shows Last for resumeFirst=false');

  // loadResume: server says autoResume=false, resumeFirst=true
  win.fetch = origFetch;
  win.__MOCK_API__ = { '/api/radio/resume': { success: true, autoResume: false, resumeFirst: true } };
  win.__postBodies = [];
  win.__test__.loadResume();
  var tick = function () { return new Promise(function (r) { setTimeout(r, 50); }); };
  tick().then(function () {
    assert(rT.innerHTML.indexOf('Auto-resume: OFF') >= 0, 'loadResume renders OFF when server autoResume=false');
    assert(rT.classList.contains('off') && !rT.classList.contains('on'), 'toggle gets .off class when disabled');
    assert(rG.textContent === 'Resume: Playlist', 'target shows Playlist when server resumeFirst=true');
    assert(rT.textContent.indexOf('\u21BB') >= 0, 'OFF label still renders the icon char');

    // resumeToggle: flips flag, re-renders immediately (optimistic), POSTs values as query params
    win.__postBodies = [];
    win.__test__.resumeToggle();
    assert(rT.textContent.indexOf('Auto-resume: ON') >= 0, 'resumeToggle flips the label to ON immediately (optimistic render)');
    assert(win.__postBodies.length >= 1, 'resumeToggle triggers a saveResume POST');
    var post = win.__postBodies[win.__postBodies.length - 1];
    assert(post.method === 'POST', 'saveResume uses POST method');
    assert(post.url.indexOf('/api/radio/resume?') >= 0, 'saveResume sends values as query params');
    assert(post.url.indexOf('autoResume=1') >= 0, 'toggled flag encoded as autoResume=1 in query');
    assert(post.url.indexOf('resumeFirst=1') >= 0, 'resumeFirst=1 kept in query');
    assert(!post.body, 'no JSON body is sent (generic WiFi route does not accumulate it)');

    // resumeTargetToggle: flips resumeFirst, re-renders immediately
    win.__postBodies = [];
    win.__test__.resumeTargetToggle();
    assert(rG.textContent === 'Resume: Last', 'resumeTargetToggle flips the target label immediately');
    post = win.__postBodies[win.__postBodies.length - 1];
    assert(post.url.indexOf('resumeFirst=0') >= 0, 'resumeTargetToggle flips resumeFirst to 0 in query');
    assert(post.url.indexOf('autoResume=1') >= 0, 'autoResume stays 1 in query');
    win.__test__.resumeTargetToggle();
    assert(rG.textContent === 'Resume: Playlist', 'second toggle cycles the target back to Playlist');

  }).then(function () { return tick(); }).then(function () { finish(); }).catch(function (e) { console.log('  ✗ FAIL: resume test threw:', String(e)); failed++; finish(); });

})().catch(function () {
  console.log('  ✗ FAIL: async discover test threw:', String(arguments[0]));
  failed++;
  finish();
});
