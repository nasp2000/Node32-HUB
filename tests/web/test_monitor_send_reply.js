// /monitor command-send reply tests (BACKLOG MON-4).
//
// send() assumed a JSON reply: `r.ok ? r.json() : null`. A 503 answered with a
// text/HTML error page therefore surfaced as a generic "❌ Error: Failed" (and a
// 200 with a truncated body rejected into the network-error branch), hiding the
// real HTTP status. It now reads the body with the shared auto.js helpers and
// shows "HTTP <status> — <excerpt>" for a non-JSON reply, leaving the JSON happy
// path untouched.
//
// The real send() and the real auto.js helpers are exercised here.

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'web_monitor_pages.cpp');
const AUTO = path.join(__dirname, '..', '..', 'chatgpt', 'src', 'webpages', 'auto_js.h');

var passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

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

function autoHelpers() {
  var text = fs.readFileSync(AUTO, 'utf8');
  var make = function (name) { return new Function('return ' + extractFn(text, name, AUTO))(); };
  return { read: make('readApiReply'), text: make('apiReplyText') };
}

function tick() { return new Promise(function (r) { setTimeout(r, 20); }); }

// Sandboxed send() with the real reply helpers and stubbed DOM/fetch/timers.
function harness(fetchImpl, destValue) {
  var el = { value: 'help', placeholder: 'Enter command', disabled: false, focused: 0,
             focus: function () { this.focused++; } };
  var timers = [];
  var helpers = autoHelpers();
  var deps = {
    document: { getElementById: function (id) {
      if (id === 'cmd') return el;
      if (id === 'cmd_dest') return { value: destValue || 'sys' };
      return null;
    } },
    fetch: fetchImpl,
    _readApiReply: helpers.read,
    _apiReplyText: helpers.text,
    _t: function (k, d) { return d; },
    setTimeout: function (fn, ms) { timers.push(ms); return timers.length; },
    JSON: JSON,
    Date: Date
  };
  var keys = Object.keys(deps);
  var Fn = Function.bind.apply(Function, [null].concat(keys, ['return ' + extractFn(fs.readFileSync(SRC, 'utf8'), 'send', SRC)]));
  var send = new Fn().apply(null, keys.map(function (k) { return deps[k]; }));
  return { send: send, el: el, timers: timers };
}

function rawReply(status, body) {
  return function () {
    return Promise.resolve({
      ok: status >= 200 && status < 300,
      status: status,
      text: function () { return Promise.resolve(body); },
      json: function () { return Promise.reject(new SyntaxError('Unexpected token <')); }
    });
  };
}

async function run() {
  console.log('\n=== /monitor send() reply handling (MON-4) ===\n');

  // 1. 503 + HTML body -> status and reason shown, placeholder restored on a timer.
  {
    var h = harness(rawReply(503, '<html><body><h1>Service Unavailable</h1><p>Command queue full</p></body></html>'));
    h.send();
    await tick();
    test('a 503 HTML reply shows the HTTP status and the body excerpt', function () {
      var p = String(h.el.placeholder);
      ok(p.indexOf('HTTP 503') >= 0, 'HTTP status shown: ' + p);
      ok(p.indexOf('Service Unavailable') >= 0, 'body excerpt shown');
      ok(p.indexOf('<') < 0, 'no raw HTML');
      ok(p.indexOf('Failed') < 0, 'no generic "Failed"');
      is(h.el.value, '', 'input cleared');
      is(h.el.disabled, false, 'input re-enabled');
      is(h.el.focused, 1, 'input refocused');
      ok(h.timers.indexOf(4000) >= 0, 'placeholder restored after 4s');
    });
  }

  // 2. JSON happy path unchanged.
  {
    var body = JSON.stringify({ success: true, response: 'ESP32 system active! 5s uptime' });
    var h2 = harness(function () {
      return Promise.resolve({ ok: true, status: 200,
        text: function () { return Promise.resolve(body); },
        json: function () { return Promise.resolve(JSON.parse(body)); } });
    });
    h2.send();
    await tick();
    test('a JSON success reply keeps the ✅ + response text', function () {
      var p = String(h2.el.placeholder);
      ok(p.indexOf('\u2705') === 0, 'success marker: ' + p);
      ok(p.indexOf('ESP32 system active!') > 0, 'response text shown');
      ok(h2.timers.indexOf(3000) >= 0, 'restored after 3s');
    });
  }

  console.log('\n' + passed + '/' + (passed + failed) + ' passed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function (e) { console.error('FATAL', e); process.exit(1); });
