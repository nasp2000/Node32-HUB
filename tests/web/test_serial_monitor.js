// Web Serial Monitor sendCmd tests.
// sendCmd posts a command to /api/system-command (or rs232/modbus) and shows the
// response as a "➜ cmd → resp" line. When the firmware already streamed the full
// output to the serial-monitor buffer it sets "streamed":true in the JSON reply;
// the page must NOT echo that response again (it would double every line).
// Run: node test_serial_monitor.js
const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'web_monitor_pages.cpp');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// Extract the minified sendCmd function (balanced braces) from the firmware page.
function extractSendCmd() {
  var text = fs.readFileSync(SRC, 'utf8');
  var re = /function\s+sendCmd\s*\(\s*\)\s*\{/;
  var m = text.match(re);
  if (!m) throw new Error('sendCmd not found in ' + SRC);
  var brace = text.indexOf('{', m.index);
  var depth = 1, i = brace + 1;
  while (i < text.length && depth > 0) {
    if (text[i] === '{') depth++;
    else if (text[i] === '}') depth--;
    i++;
  }
  return text.slice(m.index, i);
}

// Build a sandboxed sendCmd with stubbed DOM/fetch/state dependencies.
function makeHarness(fetchImpl) {
  var allMsgs = [];
  var calls = { schedule: 0, render: 0, alert: [] };
  var cmdEl = { value: 'help' };
  var ctypeEl = { value: 'sys' };
  var document = {
    getElementById: function (id) {
      if (id === 'cmd') return cmdEl;
      if (id === 'ctype') return ctypeEl;
      return { value: '' };
    }
  };
  var deps = {
    document: document,
    fetch: fetchImpl,
    allMsgs: allMsgs,
    scheduleLoad: function () { calls.schedule++; },
    renderMsgs: function () { calls.render++; },
    _retry429: 0,
    customAlert: function (m) { calls.alert.push(String(m)); },
    Date: Date
  };
  var keys = Object.keys(deps);
  var args = keys.map(function (k) { return deps[k]; });
  var ctorArgs = keys.concat(['return ' + extractSendCmd()]);
  var Fn = Function.bind.apply(Function, [null].concat(ctorArgs));
  var sendCmd = new Fn().apply(null, args);
  return { sendCmd: sendCmd, allMsgs: allMsgs, calls: calls, cmdEl: cmdEl, ctypeEl: ctypeEl };
}

function mockFetch(resp) {
  return function () {
    return Promise.resolve({
      ok: true, status: 200,
      json: function () { return Promise.resolve(resp); }
    });
  };
}

function tick() { return new Promise(function (r) { setTimeout(r, 10); }); }

async function run() {
  console.log('\n=== Web Serial Monitor sendCmd Tests ===\n');

  // Variation 1: help produces captured output that is already streamed
  // ("streamed":true). The page must NOT echo it -> no doubling.
  {
    var h = makeHarness(mockFetch({
      success: true, streamed: true,
      response: 'node32-hub help\ngeneral sections\nuse a section to view details'
    }));
    h.sendCmd();
    await tick();
    test('streamed (captured) response is not echoed into allMsgs', function () {
      is(h.allMsgs.length, 0, 'no echo pushed for streamed output');
      is(h.calls.render, 1, 'renderMsgs still called');
      is(h.calls.schedule, 1, 'scheduleLoad still called');
      is(h.cmdEl.value, '', 'cmd input cleared');
    });
  }

  // Variation 2: canned response (hello) with streamed:false -> echoed once.
  {
    var h2 = makeHarness(mockFetch({
      success: true, streamed: false, response: 'ESP32 system active! 5s uptime'
    }));
    h2.sendCmd();
    await tick();
    test('canned response (streamed:false) is echoed once', function () {
      is(h2.allMsgs.length, 1, 'one echo pushed');
      is(h2.allMsgs[0].type, 'COMMAND_RESPONSE', 'echo type is COMMAND_RESPONSE');
      ok(String(h2.allMsgs[0].text).indexOf('\u279c help \u2192 ESP32 system active!') === 0, 'echo has arrow + command + response');
    });
  }

  // Variation 3: rs232/modbus destinations never set "streamed" -> echoed.
  {
    var h3 = makeHarness(mockFetch({ success: true, response: 'Command queued' }));
    h3.ctypeEl.value = 'rs232';
    h3.sendCmd();
    await tick();
    test('rs232 response (no streamed field) is echoed', function () {
      is(h3.allMsgs.length, 1, 'one echo pushed');
      ok(String(h3.allMsgs[0].text).indexOf('Command queued') > 0, 'response appended');
    });
  }

  // Variation 4: HTTP failure path (streamed absent) still echoes a reply.
  {
    var h4 = makeHarness(mockFetch({ success: false, response: 'HTTP 500' }));
    h4.sendCmd();
    await tick();
    test('failed request without streamed flag is echoed', function () {
      is(h4.allMsgs.length, 1, 'echo pushed');
      ok(String(h4.allMsgs[0].text).indexOf('HTTP 500') > 0, 'failure text shown');
    });
  }

  // Variation 5: empty command is ignored (no fetch, no push).
  {
    var fetched = 0;
    var h5 = makeHarness(function () { fetched++; return Promise.resolve({ ok: true, json: function () { return Promise.resolve({}); } }); });
    h5.cmdEl.value = '   ';
    h5.sendCmd();
    await tick();
    test('empty command is ignored (no fetch)', function () {
      is(fetched, 0, 'fetch not called');
      is(h5.allMsgs.length, 0, 'nothing pushed');
    });
  }

  // Variation 6: multiline captured output (help) -> exactly zero duplicates.
  {
    var h6 = makeHarness(mockFetch({
      success: true, streamed: true,
      response: 'node32-hub help\ngeneral sections\n  HELP-BASIC - basic commands\nuse a section to view details'
    }));
    h6.sendCmd();
    await tick();
    test('multiline captured help leaves no duplicate lines', function () {
      is(h6.allMsgs.length, 0, 'no duplicate lines in allMsgs');
      is(h6.calls.schedule, 1, 'reload still scheduled');
    });
  }

  console.log('\n' + passed + '/' + (passed + failed) + ' passed');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) { console.error('FATAL', e); process.exit(1); });
