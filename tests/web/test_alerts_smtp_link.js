// Alerts page: SMTP cross-link (ALERTS-7) and the disabled-slot save wiring
// (ALERTS-6). The merge rule itself is unit-tested in
// tests/native/test_alerts_slot_merge.cpp; this suite checks that the page
// actually links to /email and that both handlers use the rule before writing.
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const ROOT = path.join(__dirname, '..', '..');
const ASSET = path.join(ROOT, 'chatgpt', 'src', 'webpages', 'assets', 'page_alerts_asset.cpp');
const HANDLER = path.join(ROOT, 'chatgpt', 'src', 'webpages', 'handlers_unified', 'recipients_handler.cpp');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }

// The page head fragment (it opens <html> and carries the H1 + banner).
function headFragment() {
  const src = fs.readFileSync(ASSET, 'utf8');
  const start = src.indexOf('const char PAGE_ALERTS_HEAD');
  ok(start > 0, 'PAGE_ALERTS_HEAD not found');
  const open = src.indexOf('R"ALERTS(', start);
  const close = src.indexOf(')ALERTS"', open);
  ok(open > 0 && close > open, 'head block not found');
  return src.substring(open + 9, close);
}

console.log('\n=== Alerts page: SMTP link + disabled-slot save ===\n');

test('the alerts page links to the email settings page', function () {
  const head = headFragment();
  ok(head.indexOf("href='/email'") > 0, 'link to /email');
  const dom = new JSDOM('<!doctype html><html><body><main>' + head + '</main></body></html>', { url: 'http://localhost/alerts-config' });
  const link = dom.window.document.querySelector("a[href='/email']");
  ok(link, 'anchor present in the rendered DOM');
  is(link.textContent.trim().indexOf('E-mail settings') >= 0, true, 'label: ' + link.textContent.trim());
  dom.window.close();
});

test('the link is above the tabs/cards, so both tabs see it', function () {
  const src = fs.readFileSync(ASSET, 'utf8');
  const headEnd = src.indexOf(')ALERTS"', src.indexOf('const char PAGE_ALERTS_HEAD'));
  const linkAt = src.indexOf("href='/email'");
  const tabsAt = src.indexOf('const char PAGE_ALERTS_TABS');
  ok(linkAt > 0 && headEnd > 0 && tabsAt > 0, 'anchors found');
  ok(linkAt < headEnd, 'link is inside the head fragment');
  ok(headEnd < tabsAt, 'head (with the link) is rendered before the tabs');
});

test('the banner is translatable with an English fallback', function () {
  const head = headFragment();
  ok(head.indexOf("data-i18n='alertsSmtpHint'") > 0, 'hint key');
  ok(head.indexOf("data-i18n='alertsSmtpLink'") > 0, 'link label key');
  ok(head.indexOf("data-i18n-title='alertsSmtpLinkTip'") > 0, 'link tooltip key');
  ok(head.indexOf('Alerts are delivered by e-mail') > 0, 'English fallback text');
});

test('both handlers apply the disabled-slot rule before writing (ALERTS-6)', function () {
  const src = fs.readFileSync(HANDLER, 'utf8');
  // inactivity setter
  const iSet = src.indexOf('configManager.setAlertSlot(');
  ok(iSet > 0, 'setAlertSlot call');
  const iMerge = src.lastIndexOf('alertsMergeInt(', iSet);
  ok(iMerge > 0 && iMerge < iSet, 'merge runs before setAlertSlot');
  ok(src.indexOf('alertsMergeText(', iMerge) > 0, 'emails merged too');
  // OEE setter
  const oSet = src.indexOf('configManager.setOeeAlertSlot(');
  ok(oSet > 0, 'setOeeAlertSlot call');
  const oMerge = src.lastIndexOf('alertsMergeInt(', oSet);
  ok(oMerge > 0 && oMerge < oSet, 'merge runs before setOeeAlertSlot');
});

test('the rule keeps stored values only while the slot is disabled', function () {
  const src = fs.readFileSync(path.join(ROOT, 'chatgpt', 'src', 'alerts_slot_merge.h'), 'utf8');
  ok(/return enabled \? formValue : storedValue;/.test(src), 'enabled ? form : stored');
});

var total = passed + failed;
console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
process.exit(failed > 0 ? 1 : 0);
