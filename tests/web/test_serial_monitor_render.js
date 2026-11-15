// /serial-monitor render + poll tests (BACKLOG MON-3).
//
// The page rebuilt all (up to 500) rows on every 3 s poll, even when the log had
// not changed, and kept the fixed cadence while idle. The fix:
//   - only new rows are appended, in one DocumentFragment insertion (a stalled
//     poll does not touch the DOM at all);
//   - the 500-message cap removes exactly the rows it drops from the array;
//   - the poll backs off 3s -> 6s -> 12s -> 15s while `total` (buffer_seq) does
//     not advance and snaps back to 3 s on new data.
//
// These cases run the real page script in jsdom with a controlled fetch and
// captured timers.

const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'web_monitor_pages.cpp');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// The serial-monitor page is split across several out.print(R"HTML(...)HTML")
// literals (the RS232/Modbus options are conditional prints). Collect the parts
// from the one carrying the <title>Serial Monitor</title> to the end.
function pageHtml() {
  var text = fs.readFileSync(SRC, 'utf8');
  var re = /out\.print\(R"HTML\(([\s\S]*?)\)HTML"\);/g, m, parts = [], started = false;
  while ((m = re.exec(text)) !== null) {
    if (!started && m[1].indexOf('<title>Serial Monitor</title>') >= 0) started = true;
    if (started) parts.push(m[1]);
  }
  ok(parts.length > 0, 'serial-monitor R"HTML block not found');
  return parts.join('');
}

function msg(i, text) {
  return { ts: 1700000000 + i, type: 'INFO', text: text || ('line ' + i) };
}

async function tick() { return new Promise(function (r) { setTimeout(r, 30); }); }

async function boot(first) {
  var timers = [], intervals = [], fetches = [];
  var queue = [first];
  var dom = new JSDOM(pageHtml(), {
    url: 'http://localhost/serial-monitor',
    runScripts: 'dangerously',
    beforeParse: function (win) {
      win.fetch = function (url) {
        fetches.push(String(url));
        if (String(url).indexOf('/api/serial-monitor') >= 0) {
          var d = queue.length ? queue.shift() : { total: 0, messages: [] };
          return Promise.resolve({ ok: true, status: 200, json: function () { return Promise.resolve(d); } });
        }
        return Promise.resolve({ ok: true, status: 200, json: function () { return Promise.resolve({}); } });
      };
      // Capture scheduling instead of running it, so the test drives load().
      win.setTimeout = function (fn, ms) { timers.push({ fn: fn, ms: ms }); return timers.length; };
      win.clearTimeout = function () {};
      win.setInterval = function (fn, ms) { intervals.push(ms); return intervals.length; };
      win.clearInterval = function () {};
    }
  });
  var win = dom.window;
  await tick();                     // the page calls load() on boot
  return {
    win: win, doc: win.document, timers: timers, intervals: intervals, queue: queue,
    fetches: fetches,
    push: function (d) { queue.push(d); },
    load: async function () { win.load(); await tick(); }
  };
}

function lastDelay(t) { return t.timers.length ? t.timers[t.timers.length - 1].ms : -1; }
function rowCount(doc) { return doc.querySelectorAll('#buf .msg').length; }

async function run() {
  console.log('\n=== /serial-monitor render + poll (MON-3) ===\n');

  var first = { total: 10, messages: Array.from({ length: 10 }, function (_, i) { return msg(i); }) };
  var app = await boot(first);

  test('the first load renders the rows once and uses the 3s cadence', function () {
    is(rowCount(app.doc), 10, 'rows rendered');
    is(app.win._pollDelay, 3000, 'delay starts at 3s');
    is(lastDelay(app), 3000, 'next poll scheduled at 3s');
  });

  test('the old fixed 3s interval is gone', function () {
    is(app.intervals.length, 0, 'no setInterval left in the serial page');
    is(app.doc.querySelectorAll('#buf .msg').length, 10);
  });

  // Stall: same total, no messages -> no DOM work, delay doubles.
  var keep = app.doc.querySelector('#buf .msg');
  app.push({ total: 10, messages: [] });
  await app.load();
  test('a stalled poll does not rebuild the list', function () {
    ok(app.doc.querySelector('#buf .msg') === keep, 'first row is the same node');
    is(rowCount(app.doc), 10, 'row count unchanged');
    is(app.win._pollDelay, 6000, 'delay doubled to 6s');
    is(lastDelay(app), 6000, 'next poll at 6s');
  });

  app.push({ total: 10, messages: [] });
  await app.load();
  app.push({ total: 10, messages: [] });
  await app.load();
  app.push({ total: 10, messages: [] });
  await app.load();
  test('the stall backoff is capped at 15s', function () {
    is(app.win._pollDelay, 15000, 'delay capped');
    is(lastDelay(app), 15000, 'next poll at the cap');
  });

  // New data: only the delta is appended and the cadence resets.
  app.push({ total: 12, messages: [msg(10), msg(11)] });
  await app.load();
  test('new rows are appended in place and the poll resets to 3s', function () {
    is(rowCount(app.doc), 12, 'two rows appended');
    ok(app.doc.querySelector('#buf .msg') === keep, 'existing rows were not rebuilt');
    ok(app.doc.querySelector('#buf').textContent.indexOf('line 11') >= 0, 'new row content present');
    is(app.win._pollDelay, 3000, 'delay back to 3s');
  });

  // Cap: 500 rows, then 3 more -> the 3 oldest nodes leave the DOM.
  var big = { total: 500, messages: Array.from({ length: 500 }, function (_, i) { return msg(i); }) };
  var app2 = await boot(big);
  var oldest = app2.doc.querySelector('#buf .msg');
  app2.push({ total: 503, messages: [msg(500), msg(501), msg(502)] });
  await app2.load();
  test('the 500-message cap trims exactly the dropped rows', function () {
    is(rowCount(app2.doc), 500, 'still 500 rows');
    ok(app2.doc.querySelector('#buf .msg') !== oldest, 'the oldest row was removed');
    ok(app2.doc.querySelector('#buf').textContent.indexOf('line 499') >= 0, 'newest row present');
    ok(app2.doc.querySelector('#buf').textContent.indexOf('line 0') < 0, 'oldest content gone');
  });

  // A filter active at growth time falls back to a full render (correctness).
  var app3 = await boot({ total: 3, messages: [msg(0, 'ALPHA'), msg(1, 'BETA'), msg(2, 'ALPHA')] });
  app3.doc.getElementById('srch').value = 'ALPHA';
  app3.win.renderMsgs();
  app3.push({ total: 4, messages: [msg(3, 'ALPHA')] });
  await app3.load();
  test('with a filter active the growth path re-renders the filtered set', function () {
    is(rowCount(app3.doc), 3, 'three matching rows');
    is(app3.win._pollDelay, 3000, 'new data keeps the 3s cadence');
  });

  // MON-5: the search box re-filters as you type, without waiting for a poll,
  // and the clear button restores the full list.
  var app4 = await boot({ total: 6, messages: [
    msg(0, 'ALPHA one'), msg(1, 'BETA two'), msg(2, 'ALPHA three'),
    msg(3, 'BETA four'), msg(4, 'ALPHA five'), msg(5, 'BETA six')
  ] });
  test('the search box is wired to an input handler', function () {
    var srch = app4.doc.getElementById('srch');
    ok(srch, 'search box present');
    is(srch.getAttribute('oninput'), 'renderMsgs()', 'oninput -> renderMsgs()');
  });
  test('typing re-filters immediately and issues no request', function () {
    var srch = app4.doc.getElementById('srch');
    var before = app4.fetches.length;
    srch.value = 'ALPHA';
    srch.dispatchEvent(new app4.win.Event('input'));
    is(rowCount(app4.doc), 3, 'only the matching rows are shown');
    is(app4.doc.getElementById('buf').textContent.indexOf('BETA'), -1, 'no non-matching row');
    is(app4.fetches.length, before, 'filtering did not hit the network');
    is(app4.win._pollDelay, 3000, 'the poll cadence is untouched');
  });
  test('the clear button restores the full list', function () {
    app4.doc.getElementById('srch-x').dispatchEvent(new app4.win.Event('click'));
    is(app4.doc.getElementById('srch').value, '', 'field cleared');
    is(rowCount(app4.doc), 6, 'all rows are back');
  });

  console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) { console.error('Fatal:', e); process.exit(1); });
