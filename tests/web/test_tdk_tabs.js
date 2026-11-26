// /tdk-lambda page: the tab bar and the catalogue shortcut.
//
// The markup contract is covered natively (tests/native/test_tdk_lambda_page.cpp);
// this suite runs the shipped functions in jsdom and checks behaviour:
//   - five buttons built from .tdk-panel[data-tab], one panel active;
//   - the last tab used survives a reload (localStorage);
//   - a second tdkInitTabs() never duplicates the bar;
//   - tdkShowTab()/the catalogue "use" button land on the Console;
//   - labels and tooltips come from the i18n map when it is loaded.
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets',
                      'page_tdk_lambda_asset.cpp');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function extractFn(src, name) {
  const start = src.indexOf('function ' + name + '(');
  if (start < 0) throw new Error(name + ' not found');
  let i = src.indexOf('{', start), depth = 0, end = -1;
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}') { depth--; if (depth === 0) { end = i + 1; break; } }
  }
  if (end < 0) throw new Error('unbalanced braces in ' + name);
  return src.substring(start, end);
}

const asset = fs.readFileSync(SRC, 'utf8');

// Markup only: the raw-string wrapper is dropped and the <script> block is
// replaced by the functions under test.
const htmlStart = asset.indexOf("<div class='page tdk-lambda'>");
const scriptAt = asset.indexOf('<script>', htmlStart);
let markup = asset.substring(htmlStart, scriptAt);

const CODE = ['_tdkQ', 'tdkSwitchTab', 'tdkShowTab', 'tdkInitTabs', 'tdkUse', 'TDK_TABS']
  .map(function (n) {
    if (n === 'TDK_TABS') {
      const at = asset.indexOf('var TDK_TABS');
      const end = asset.indexOf('];', at);
      return asset.substring(at, end + 2);
    }
    return extractFn(asset, n);
  }).join('\n');

function boot(opts) {
  opts = opts || {};
  const dom = new JSDOM('<!doctype html><html><body>' + markup + '</body></html>',
                        { url: 'http://device/tdk-lambda', runScripts: 'outside-only' });
  const w = dom.window;
  w.eval(CODE);
  // Same helper auto.js exposes (window._t): key -> translation, else fallback.
  w._t = function (k, fb) {
    return (w._lang && w._lang[k] !== undefined) ? w._lang[k] : fb;
  };
  if (opts.lang) w._lang = opts.lang;
  if (opts.stored !== undefined) w.localStorage.setItem('tdk_tab', String(opts.stored));
  return w;
}

function activePanel(w) {
  const panels = w.document.querySelectorAll('.tdk-panel[data-tab]');
  for (var i = 0; i < panels.length; i++) {
    if (panels[i].classList.contains('active')) return panels[i].getAttribute('data-tab');
  }
  return null;
}

function activeBtn(w) {
  const btns = w.document.querySelectorAll('.tdk-tab-btn');
  for (var i = 0; i < btns.length; i++) if (btns[i].classList.contains('active')) return btns[i];
  return null;
}

test('five tabs are built, one per panel, with a default active', function () {
  const w = boot();
  w.tdkInitTabs();
  const btns = w.document.querySelectorAll('.tdk-tab-btn');
  const panels = w.document.querySelectorAll('.tdk-panel[data-tab]');
  is(panels.length, 5, 'panels');
  is(btns.length, 5, 'buttons');
  is(activePanel(w), 'live', 'default panel (data-tab-default)');
  is(activeBtn(w).textContent, 'Live', 'default button');
});

test('labels and tooltips use the i18n map when it is loaded', function () {
  const w = boot({ lang: { tdkTabLive: 'Ao vivo', tdkTipTabLive: 'Estado e leituras' } });
  w.tdkInitTabs();
  const first = w.document.querySelectorAll('.tdk-tab-btn')[0];
  is(first.textContent, 'Ao vivo', 'translated label');
  is(first.title, 'Estado e leituras', 'translated tooltip');
  ok(first.getAttribute('data-i18n') === 'tdkTabLive', 'label stays translatable');
  ok(first.getAttribute('data-i18n-title') === 'tdkTipTabLive', 'tooltip stays translatable');
});

test('every tab button carries a tooltip even before /api/lang', function () {
  const w = boot();
  w.tdkInitTabs();
  w.document.querySelectorAll('.tdk-tab-btn').forEach(function (b) {
    ok(b.title && b.title.length > 5, 'missing tooltip on ' + b.textContent);
  });
});

test('clicking a tab switches panel and remembers the choice', function () {
  const w = boot();
  w.tdkInitTabs();
  const btns = w.document.querySelectorAll('.tdk-tab-btn');
  btns[2].onclick();
  is(activePanel(w), 'csv', 'csv panel after clicking tab 3');
  is(w.localStorage.getItem('tdk_tab'), '2', 'stored index');
  // Reload: the stored tab wins over data-tab-default.
  const w2 = boot({ stored: 2 });
  w2.tdkInitTabs();
  is(activePanel(w2), 'csv', 'restored panel');
});

test('a second init never duplicates the bar', function () {
  const w = boot();
  w.tdkInitTabs();
  w.tdkInitTabs();
  is(w.document.querySelectorAll('.tdk-tab-btn').length, 5, 'buttons after two inits');
});

test('tdkShowTab() and the catalogue shortcut land on the Console', function () {
  const w = boot();
  w.tdkInitTabs();
  w.tdkShowTab('profile');
  is(activePanel(w), 'profile', 'showTab profile');
  w.tdkUse('DVC?');
  is(activePanel(w), 'console', 'use switches to console');
  is(w.document.getElementById('tdkCmd').value, 'DVC?', 'command loaded');
});

test('an unknown tab key leaves the current panel alone', function () {
  const w = boot();
  w.tdkInitTabs();
  w.tdkShowTab('nope');
  is(activePanel(w), 'live', 'panel unchanged');
});

console.log('\n' + passed + '/' + (passed + failed) + ' passed');
process.exit(failed ? 1 : 0);
