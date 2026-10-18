// Day-report fallback logic tests (mirror of chatgpt/src/day_report_fallback.h).
// Pure logic, no DOM: verifies the same rules the firmware enforces.
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  ✓ ' + name); }
  catch (e) { failed++; console.log('  ✗ ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// ---- Mirror of dayrep::isDayReportCommand ----
function verbOf(line) {
  var t = String(line || '').replace(/^[ \t]+/, '');
  var verb = t.split(/[ \t]/)[0].toUpperCase();
  if (verb === 'EMAILDAY' || verb === 'LOGDAY' || verb === 'SENSORDAY' || verb === 'SHIFTDAY') return verb;
  return null;
}

// ---- Mirror of dayrep::hasDateParam ----
function hasDateParam(line) {
  var m = String(line || '').match(/(\d{2})-(\d{2})-(\d{4})/);
  if (!m) return false;
  var d = +m[1], mo = +m[2], y = +m[3];
  return d >= 1 && d <= 31 && mo >= 1 && mo <= 12 && y >= 2000 && y <= 2099;
}

// ---- Mirror of dayrep::effectiveDayEpoch (1h tolerance) ----
function effectiveDayEpoch(schedEpoch, schedHour) {
  if (schedEpoch <= 1000) return schedEpoch;
  if (schedHour >= 0 && schedHour < 1) return schedEpoch - 3600;
  return schedEpoch;
}

// ---- Mirror of dayrep::needsRetry ----
var DELAY = 20 * 60, MAXATT = 3, WINDOW = 24 * 3600;
function needsRetry(e, now) {
  if (e.slot < 0 || e.dayKey < 0) return false;
  if (e.status === 'sent') return false;
  if (e.sched <= 1000 || now <= 1000) return false;
  if (now < e.sched) return false;
  if (now - e.sched < DELAY) return false;
  if (now - e.sched > WINDOW) return false;
  if (e.att >= MAXATT) return false;
  if (e.last > 0 && now - e.last < DELAY) return false;
  if (!e.line) return false;
  return true;
}

console.log('\n=== Day-Report Fallback Tests ===\n');

test('verb detection: 4 commands, any case', function() {
  is(verbOf('emailday'), 'EMAILDAY');
  is(verbOf('EMAILDAY 29-12-2025'), 'EMAILDAY');
  is(verbOf('  logday'), 'LOGDAY');
  is(verbOf('SensorDay 01-03-2026'), 'SENSORDAY');
  is(verbOf('shiftday 28-03-2026'), 'SHIFTDAY');
});

test('verb detection rejects others', function() {
  is(verbOf('X10 off'), null);
  is(verbOf('emaildayx'), null);
  is(verbOf(''), null);
  is(verbOf('REBOOT'), null);
});

test('hasDateParam: explicit date vs bare', function() {
  ok(hasDateParam('emailday 29-12-2025'), 'explicit');
  ok(!hasDateParam('emailday'), 'bare');
  ok(!hasDateParam('shiftday '), 'trailing space, no date');
  ok(!hasDateParam('emailday 29-13-2025'), 'month 13 invalid');
});

test('1h tolerance: 00:xx -> previous day epoch', function() {
  var s = 2000000000;
  is(effectiveDayEpoch(s, 0), s - 3600, 'hour 0 steps back');
  is(effectiveDayEpoch(s, 1), s, 'hour 1 unchanged');
  is(effectiveDayEpoch(s, 23), s, 'hour 23 unchanged');
});

test('needsRetry: +20min window, max 3 attempts', function() {
  var s = 2000000000;
  var e = { slot: 3, dayKey: 20260328, sched: s, status: 'failed', att: 0, last: 0, line: 'emailday 28-03-2026' };
  ok(!needsRetry(e, s + 19 * 60), 'too early');
  ok(needsRetry(e, s + 20 * 60), 'first window');
  e.att = 3;
  ok(!needsRetry(e, s + 120 * 60), 'exhausted');
});

test('needsRetry: SENT never refires, spacing + 24h window', function() {
  var s = 2000000000;
  ok(!needsRetry({ slot: 3, dayKey: 20260328, sched: s, status: 'sent', att: 0, last: 0, line: 'emailday 28-03-2026' }, s + 3600), 'sent stays sent');
  var spaced = { slot: 3, dayKey: 20260328, sched: s, status: 'failed', att: 1, last: s + 20 * 60, line: 'logday 28-03-2026' };
  ok(!needsRetry(spaced, s + 30 * 60), 'spacing');
  ok(needsRetry(spaced, s + 41 * 60), 'spacing ok');
  ok(!needsRetry({ slot: 3, dayKey: 20260328, sched: s, status: 'failed', att: 0, last: 0, line: 'emailday 28-03-2026' }, s + 25 * 3600), 'stale');
});

var total = passed + failed;
console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
process.exit(failed > 0 ? 1 : 0);
