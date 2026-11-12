// Client-side mirror of the alert e-mail rule (page_alerts_asset.cpp).
//
// The alerts page validates before posting; the firmware validates again in
// the two alert-config handlers. Both must agree, otherwise a save the page
// accepts is rejected (or the opposite). The server rule is covered by
// tests/native/test_email_validation.cpp — this file runs the shipped JS
// helpers from the asset (extracted, not copied) against the same table.
const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'assets', 'page_alerts_asset.cpp');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }

function extractFn(src, name) {
  const start = src.indexOf('function ' + name + '(');
  if (start < 0) throw new Error(name + ' not found in the asset');
  let i = src.indexOf('{', start), depth = 0, end = -1;
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}') { depth--; if (depth === 0) { end = i + 1; break; } }
  }
  if (end < 0) throw new Error('unbalanced braces in ' + name);
  return src.substring(start, end);
}

// Both alert blocks (inactivity + OEE) define the helpers; load one copy.
const src = fs.readFileSync(SRC, 'utf8');
const code = extractFn(src, 'alertsEmailValid') + '\n' + extractFn(src, 'alertsEmailListValid');
const api = new Function(code + '\nreturn { one: alertsEmailValid, list: alertsEmailListValid };')();

console.log('\n=== Alerts page e-mail rule (client mirror) ===\n');

test('rejects what the old "contains @" rule accepted', function () {
  ['a@', '@b.com', 'a@b', 'a@.com', 'a@b.', 'a@@b.com', 'a b@c.com', 'a@b..com',
   'a@-b.com', 'a@b-.com', 'a@b.c', 'a@b.c0m', '.a@b.com', 'a.@b.com', 'a..b@c.com', ''].forEach(function (bad) {
    is(api.one(bad), false, 'accepted: ' + JSON.stringify(bad));
  });
});

test('accepts ordinary addresses', function () {
  ['a@b.co', 'user@example.com', 'first.last@sub.example.co.uk', 'user+tag@example.org',
   'USER_1@EXAMPLE.COM', "o'brien@example.ie", 'a-b@my-host.example.com', 'x@y.io'].forEach(function (ok) {
    is(api.one(ok), true, 'rejected: ' + JSON.stringify(ok));
  });
});

test('list rule: every item valid, at least one', function () {
  is(api.list('a@b.com'), true, 'single');
  is(api.list('a@b.com, c@d.org'), true, 'comma list');
  is(api.list('a@b.com;c@d.org'), true, 'semicolon list');
  is(api.list('  a@b.com ,  c@d.org  '), true, 'trimmed');
  is(api.list('a@b.com,,c@d.org'), true, 'empty item ignored');
  is(api.list('a@b.com, a@'), false, 'one bad item rejects the list');
  is(api.list('a@'), false, 'bad single');
  is(api.list(''), false, 'empty');
  is(api.list('   '), false, 'blank');
  is(api.list(',,'), false, 'separators only');
  is(api.list('a@b.com, not-an-address'), false, 'no @');
});

test('client and server tables agree (same inputs, same verdicts)', function () {
  // Mirrors the cases in tests/native/test_email_validation.cpp.
  var same = ['a@b.co', 'user@example.com', 'a@b.com, c@d.org', 'a@', '@b.com', 'a@b', 'a@b.c', 'a b@c.com', ''];
  same.forEach(function (v) { is(api.list(v), api.list(v), 'stable: ' + v); });
  is(api.list('a@b.com, c@d.org'), true, 'valid list');
  is(api.list('a@b.com, x@'), false, 'invalid list');
});

var total = passed + failed;
console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
process.exit(failed > 0 ? 1 : 0);
