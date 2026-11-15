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

// Extract a named function (balanced braces) from a source text.
function extractFn(text, name, where) {
  var re = new RegExp('function\\s+' + name + '\\s*\\([^)]*\\)\\s*\\{');
  var m = text.match(re);
  if (!m) throw new Error(name + ' not found in ' + where);
  var brace = text.indexOf('{', m.index);
  var depth = 1, i = brace + 1;
  while (i < text.length && depth > 0) {
    if (text[i] === '{') depth++;
    else if (text[i] === '}') depth--;
    i++;
  }
  return text.slice(m.index, i);
}

// The minified sendCmd of the /serial-monitor page.
function extractSendCmd() {
  return extractFn(fs.readFileSync(SRC, 'utf8'), 'sendCmd', SRC);
}

// The real reply helpers from auto.js (window._readApiReply / _apiReplyText).
// They are what sendCmd uses, so the tests exercise the shipped code and not a
// copy of it.
function autoReplyHelpers() {
  var autoSrc = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'auto_js.h');
  var text = fs.readFileSync(autoSrc, 'utf8');
  var make = function (name) {
    return new Function('return ' + extractFn(text, name, autoSrc))();
  };
  return { read: make('readApiReply'), text: make('apiReplyText') };
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
  var reply = autoReplyHelpers();
  var deps = {
    document: document,
    fetch: fetchImpl,
    allMsgs: allMsgs,
    scheduleLoad: function () { calls.schedule++; },
    renderMsgs: function () { calls.render++; },
    _readApiReply: reply.read,
    _apiReplyText: reply.text,
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

// JSON reply (the normal path): the body is the serialized object.
function mockFetch(resp) {
  return function () {
    var body = JSON.stringify(resp);
    return Promise.resolve({
      ok: true, status: 200,
      text: function () { return Promise.resolve(body); },
      json: function () { return Promise.resolve(resp); }
    });
  };
}

// Raw reply: status + body exactly as the device/proxy sent it (MON-4).
function mockRaw(status, contentType, body) {
  return function () {
    return Promise.resolve({
      ok: status >= 200 && status < 300,
      status: status,
      headers: { get: function () { return contentType; } },
      text: function () { return Promise.resolve(body); },
      json: function () { return Promise.reject(new SyntaxError('Unexpected token <')); }
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

  // Variation 7 (MON-4): a 503 answered with an HTML error page is not JSON.
  // The page must show the real HTTP status and an excerpt of the body instead
  // of failing inside r.json() and reporting a generic network error.
  {
    var h7 = makeHarness(mockRaw(503, 'text/html',
      '<!DOCTYPE html><html><head><title>503 Service Unavailable</title></head>' +
      '<body><h1>Service Unavailable</h1></body></html>'));
    h7.sendCmd();
    await tick();
    test('503 HTML reply shows the HTTP status and the reason', function () {
      is(h7.allMsgs.length, 1, 'one echo pushed');
      is(h7.allMsgs[0].type, 'ERROR', 'typed as ERROR');
      var t = String(h7.allMsgs[0].text);
      ok(t.indexOf('\u279c help') === 0, 'starts with arrow + command');
      ok(t.indexOf('HTTP 503') > 0, 'HTTP status shown');
      ok(t.indexOf('Service Unavailable') > 0, 'body text shown');
      ok(t.indexOf('<') < 0, 'no raw HTML in the line');
      is(h7.calls.alert.length, 0, 'no generic error alert');
      is(h7.calls.schedule, 1, 'reload still scheduled');
      is(h7.cmdEl.value, '', 'input still cleared');
    });
  }

  // Variation 8 (MON-4): plain-text 503 body (proxy / async-server message).
  {
    var h8 = makeHarness(mockRaw(503, 'text/plain', 'Command queue full'));
    h8.sendCmd();
    await tick();
    test('503 plain-text reply keeps the message', function () {
      var t = String(h8.allMsgs[0].text);
      ok(t.indexOf('HTTP 503') > 0, 'HTTP status shown');
      ok(t.indexOf('Command queue full') > 0, 'body message shown');
      is(h8.calls.alert.length, 0, 'no alert');
    });
  }

  // Variation 9 (MON-4): a 200 with a truncated body is handled the same way.
  {
    var h9 = makeHarness(mockRaw(200, 'application/json', '{"success":tr'));
    h9.sendCmd();
    await tick();
    test('unparsable 200 body is reported, not thrown', function () {
      is(h9.allMsgs.length, 1, 'one echo pushed');
      var t = String(h9.allMsgs[0].text);
      ok(t.indexOf('HTTP 200') > 0, 'HTTP status shown');
      ok(t.indexOf('{"success":tr') > 0, 'body excerpt shown');
      is(h9.calls.alert.length, 0, 'no alert');
    });
  }

  // Variation 10 (MON-4): the JSON happy path is untouched.
  {
    var h10 = makeHarness(mockFetch({ success: true, streamed: false, response: 'OK' }));
    h10.sendCmd();
    await tick();
    test('a JSON reply is still echoed as COMMAND_RESPONSE', function () {
      is(h10.allMsgs[0].type, 'COMMAND_RESPONSE', 'type unchanged');
      ok(String(h10.allMsgs[0].text).indexOf('\u279c help \u2192 OK') === 0, 'unchanged text');
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
