// /monitor i18n tests (BACKLOG MON-1).
//
// The page used an ad-hoc _rxT(key, pt, en) with a browser-language heuristic
// and hardcoded PT/EN branches, bypassing data-i18n//api/lang — an EN browser
// could still get PT text. It now uses the standard data-i18n mechanism like
// every other page, so the active language dictionary is the only source.
//
// These cases run the real page markup (extracted from web_monitor_pages.cpp)
// against the real EN and PT dictionaries, applying them the same way auto.js
// does.

const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'web_monitor_pages.cpp');
const EN = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'languages', 'lang_en_ui.h');
const PT = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'languages', 'lang_pt_ui.h');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function source() { return fs.readFileSync(SRC, 'utf8'); }

// All out.print(F("...")) string literals = the page markup (the inline JS is
// a R"js(...)js" block and is not matched).
function markup() {
  var text = source();
  var re = /out\.print\(F\("((?:[^"\\]|\\.)*)"\)\);/g;
  var m, out = [];
  while ((m = re.exec(text)) !== null) {
    out.push(m[1].replace(/\\"/g, '"').replace(/\\\\/g, '\\'));
  }
  return out.join('');
}

// The real dictionary: JSON inside R"JSON(...)JSON".
function langPack(file) {
  var text = fs.readFileSync(file, 'utf8');
  var a = text.indexOf('R"JSON(');
  var b = text.indexOf(')JSON"', a);
  if (a < 0 || b < 0) throw new Error('dictionary not found in ' + file);
  return JSON.parse(text.slice(a + 7, b));
}

// Same application rules as auto.js applyI18nMap (text/i18n-html/title/placeholder).
function applyI18n(doc, strings) {
  doc.querySelectorAll('[data-i18n],[data-i18n-html]').forEach(function (el) {
    var k = el.getAttribute('data-i18n') || el.getAttribute('data-i18n-html');
    if (strings[k] === undefined) return;
    if (el.hasAttribute('data-i18n-html')) el.innerHTML = strings[k];
    else el.textContent = strings[k];
  });
  doc.querySelectorAll('[data-i18n-title]').forEach(function (el) {
    var k = el.getAttribute('data-i18n-title');
    if (k && strings[k] !== undefined) el.setAttribute('title', strings[k]);
  });
  doc.querySelectorAll('[data-i18n-placeholder]').forEach(function (el) {
    var k = el.getAttribute('data-i18n-placeholder');
    if (k && strings[k] !== undefined) el.setAttribute('placeholder', strings[k]);
  });
}

function pageWith(strings) {
  var html = markup();
  var dom = new JSDOM('<!DOCTYPE html><html lang="en"><head></head><body>' + html + '</body></html>');
  applyI18n(dom.window.document, strings);
  return dom.window.document;
}

function run() {
  console.log('\n=== /monitor i18n (data-i18n) Tests ===\n');

  var src = source();

  test('the ad-hoc browser-language heuristic is gone', function () {
    ok(src.indexOf('_rxT(') < 0, '_rxT removed');
    ok(src.indexOf('_rxLangCode') < 0, '_rxLangCode removed');
    ok(src.indexOf('applyMonitorTexts') < 0, 'applyMonitorTexts removed');
  });

  test('every visible text is a data-i18n target', function () {
    var html = markup();
    ['data-i18n=\'monActionsTitle\'', 'data-i18n=\'monRefresh\'', 'data-i18n=\'monClear\'',
     'data-i18n=\'monOpenViewer\'', 'data-i18n=\'monSendTitle\'', 'data-i18n=\'monSystem\'',
     'data-i18n=\'monSend\'', 'data-i18n=\'monBufferTitle\'', 'data-i18n=\'monBufferDesc\'',
     'data-i18n=\'homeDataRecv\'', 'data-i18n=\'btnHome\'', 'data-i18n=\'loading\'',
     'data-i18n-html=\'monActionsInfo\'', 'data-i18n-html=\'monSendInfo\'',
     'data-i18n-placeholder=\'monEnterCommand\''].forEach(function (a) {
      ok(html.indexOf(a) >= 0, 'missing ' + a);
    });
  });

  test('every referenced key exists in both dictionaries', function () {
    var en = langPack(EN), pt = langPack(PT);
    var html = markup();
    var re = /data-i18n(?:-html|-title|-placeholder)?='([A-Za-z0-9_]+)'/g, m, keys = {};
    while ((m = re.exec(html)) !== null) keys[m[1]] = true;
    Object.keys(keys).forEach(function (k) {
      ok(en[k] !== undefined, 'EN missing ' + k);
      ok(pt[k] !== undefined, 'PT missing ' + k);
    });
  });

  var enDoc = pageWith(langPack(EN));
  test('EN browsers get the EN texts', function () {
    is(enDoc.getElementById('btn_refresh').textContent, '🔄 Refresh');
    is(enDoc.getElementById('rx_actions_title').textContent, '⚙️ Actions');
    is(enDoc.getElementById('rx_buffer_title').textContent, '📊 RX Buffer Content');
    is(enDoc.getElementById('cmd_dest_sys').textContent, 'System');
    is(enDoc.getElementById('cmd').getAttribute('placeholder'), 'Enter command');
    is(enDoc.title, '📥 Data Reception');
  });

  var ptDoc = pageWith(langPack(PT));
  test('PT browsers get the PT texts', function () {
    is(ptDoc.getElementById('btn_refresh').textContent, '🔄 Atualizar');
    is(ptDoc.getElementById('rx_actions_title').textContent, '⚙️ Ações');
    is(ptDoc.getElementById('rx_buffer_title').textContent, '📊 Conteúdo do Buffer RX');
    is(ptDoc.getElementById('cmd_dest_sys').textContent, 'Sistema');
    is(ptDoc.getElementById('cmd').getAttribute('placeholder'), 'Introduza o comando');
    is(ptDoc.title, '📥 Receção de Dados');
  });

  console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run();
