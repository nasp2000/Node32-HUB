// Recipients page — row-action alignment contract (T26).
//
// The page is CSS/HTML only here: the per-row 💾/🗑️ buttons must be the SAME size and
// share one axis (like the commands page), and the help descriptions must be
// left-aligned. jsdom does not cascade the board's core.css reliably, so the contract is
// checked against the shipped asset text — the exact bytes compiled into the firmware —
// plus the real row markup produced by rowHTML().
var fs = require('fs');
var path = require('path');
var { JSDOM } = require('jsdom');

var ASSET = path.join(__dirname, '../../chatgpt/src/webpages/assets/page_recipients_asset.cpp');
var FIXTURE = path.join(__dirname, 'pages', 'recipients.html');
var src = fs.readFileSync(ASSET, 'utf8');

var passed = 0, failed = 0;
function assert(c, m) { if (c) passed++; else { console.log('  ✗ ' + m); failed++; } }
function assertEq(a, e, m) {
  if (a === e) passed++;
  else { console.log('  ✗ ' + m + ' (expected ' + JSON.stringify(e) + ', got ' + JSON.stringify(a) + ')'); failed++; }
}

console.log('\n=== Recipients Page Layout Tests ===\n');

// ── CSS contract ────────────────────────────────────────────────────────────
var actionRule = (src.match(/#rec_body td:last-child button\{[^}]*\}/) || [''])[0];

assert(actionRule.length > 0, 'one rule sizes BOTH row buttons (no per-button selector)');
assert(/width:32px!important/.test(actionRule), 'row buttons width 32px (same as commands)');
assert(/height:32px!important/.test(actionRule), 'row buttons height 32px (same as commands)');
assert(/min-width:32px!important/.test(actionRule), 'min-width pinned so the emoji cannot stretch it');
assert(/padding:0!important/.test(actionRule), 'padding 0 (core.css button padding must not win)');
assert(/border-radius:16px!important/.test(actionRule), 'round 16px radius');
assert(/margin:0 auto!important/.test(actionRule), 'both buttons centred on the same axis');
assert(!/#rec_body button\[data-save\]\{/.test(src), 'the old save-only rule (asymmetric sizes) is gone');
assert(!/#rec_body button\[data-save\]/.test(src), 'no CSS targets only the save button');
assert(/#rec_body td:last-child\{[^}]*padding:0/.test(src), 'action cell has no padding (buttons keep their 32px)');
assert(/#rec_body td:last-child\{[^}]*white-space:nowrap/.test(src), 'action cell never wraps a button label');

// ── description alignment ───────────────────────────────────────────────────
assert(/\.recip-desc,\.recip-desc>div\{text-align:left\}/.test(src), 'descriptions force text-align:left');
var descBlocks = src.match(/class='recip-desc'/g) || [];
assertEq(descBlocks.length, 3, 'keyword + operator help and the day-command note carry .recip-desc');

// ── real row markup ─────────────────────────────────────────────────────────
var dom = new JSDOM(fs.readFileSync(FIXTURE, 'utf8'), {
  url: 'http://localhost', runScripts: 'dangerously', pretendToBeVisual: true,
});
var win = dom.window;
var doc = win.document;

function inlineStyle(btn) { return btn.getAttribute('style') || ''; }

var row = new JSDOM('<table><tbody></tbody></table>').window.document;
row.querySelector('tbody').innerHTML = win.__test__.rowHTML(1);
var saveBtn = row.querySelector("[data-save='1']");
var delBtn = row.querySelector("[data-del='1']");

assert(saveBtn && delBtn, 'row markup has both action buttons');
assert(!/width:|height:|min-width:|padding:|border-radius:|margin-left:/.test(inlineStyle(saveBtn)),
  'save button carries no inline size/offset (CSS is the single source)');
assert(!/width:|height:|min-width:|padding:|border-radius:|margin-left:/.test(inlineStyle(delBtn)),
  'delete button carries no inline size/offset (CSS is the single source)');
assertEq(inlineStyle(saveBtn).replace(/background:[^;]+;|border:[^;]+;|color:[^;]+;|cursor:[^;]+;/g, ''),
  inlineStyle(delBtn).replace(/background:[^;]+;|border:[^;]+;|color:[^;]+;|cursor:[^;]+;/g, ''),
  'both buttons keep an identical inline style apart from their colours');
assertEq(doc.querySelectorAll("#rec_body tr[data-i='1'] button").length, 2,
  'each rendered row carries exactly the two action buttons');

win.close();
dom.window.close();
console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
process.exit(failed > 0 ? 1 : 0);
