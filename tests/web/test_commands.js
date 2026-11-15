// Scheduled Commands Page Tests
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'commands.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function loadPage() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var orig = console.error; console.error = function() {
    var m = arguments[0]||''; if(typeof m!=='string'||m.indexOf('Not implemented')<0) orig.apply(console,arguments);
  };
  var dom = new JSDOM(html, {url:'http://localhost', runScripts:'dangerously', pretendToBeVisual:true});
  console.error = orig;
  return dom;
}

function wait(dom, ms) {
  return new Promise(function(r){setTimeout(function(){r(dom);}, ms || 500);});
}

async function run() {
  console.log('\n=== Scheduled Commands Page Tests ===\n');

  var dom, win, doc;

  // ════════════════════════════════════════════════════════════════
  // esc — HTML escaping
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('esc escapes HTML special chars', function() {
      is(win.__test__.esc('<>&"\''), '&lt;&gt;&amp;&quot;&#39;', 'special chars escaped');
    });

    test('esc returns empty string for null/undefined', function() {
      is(win.__test__.esc(null), '', 'null escaped');
      is(win.__test__.esc(undefined), '', 'undefined escaped');
    });

    test('esc passes through normal text', function() {
      is(win.__test__.esc('hello world'), 'hello world', 'plain text');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // t — translation with fallback
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('t returns fallback without cache', function() {
      is(win.__test__.t('opYes', 'Yes'), 'Yes', 'fallback returned');
    });

    test('t returns key if no fallback', function() {
      is(win.__test__.t('missingKey'), 'missingKey', 'key returned');
    });

    test('t reads from localStorage', function() {
      var cache = { opYes: 'SIM' };
      win.localStorage.setItem('ui_lang_cache', JSON.stringify(cache));
      is(win.__test__.t('opYes', 'fallback'), 'SIM', 'reads cache');
      win.localStorage.removeItem('ui_lang_cache');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // unitSelect — HTML generation
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('unitSelect returns a select with name attribute', function() {
      var html = win.__test__.unitSelect(1);
      ok(html.indexOf("name='unit_1'") >= 0 || html.indexOf('name="unit_1"') >= 0, 'has name');
    });

    test('unitSelect has s/m/h options', function() {
      var html = win.__test__.unitSelect(2);
      ok(html.indexOf("value='s'") >= 0, 'has seconds');
      ok(html.indexOf("value='m'") >= 0, 'has minutes');
      ok(html.indexOf("value='h'") >= 0, 'has hours');
    });

    test('unitSelect has minutes selected by default', function() {
      var html = win.__test__.unitSelect(3);
      ok(html.indexOf("value='m' selected") >= 0, 'minutes default');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // daysSelect
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('daysSelect has 10 day options', function() {
      var html = win.__test__.daysSelect(1);
      var count = (html.match(/<option/g) || []).length;
      is(count, 10, '10 options');
    });

    test('daysSelect includes everyday and specific days', function() {
      var html = win.__test__.daysSelect(2);
      ok(html.indexOf("value='everyday'") >= 0, 'has everyday');
      ok(html.indexOf("value='monday'") >= 0, 'has monday');
      ok(html.indexOf("value='sunday'") >= 0, 'has sunday');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // rowHTML
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('rowHTML returns a table row with data-i', function() {
      var html = win.__test__.rowHTML(1);
      ok(html.indexOf("data-i='1'") >= 0 || html.indexOf('data-i="1"') >= 0, 'has data-i');
    });

    test('rowHTML includes type select with System/RS232/Modbus', function() {
      var html = win.__test__.rowHTML(2);
      ok(html.indexOf("value='system'") >= 0, 'has system type');
    });

    test('rowHTML includes save button', function() {
      var html = win.__test__.rowHTML(3);
      ok(html.indexOf("data-save='3'") >= 0, 'has save button');
    });

    test('rowHTML includes command input', function() {
      var html = win.__test__.rowHTML(4);
      ok(html.indexOf("name='cmd_4'") >= 0, 'has cmd input');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // addRow / init
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    doc = dom.window.document;

    test('init creates 10 rows in the table', function() {
      var rows = doc.querySelectorAll('#sched_cmd_body tr');
      is(rows.length, 10, '10 rows from init');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // removeRow — remove + rebuild with contiguous indices
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    test('rowHTML includes a remove (data-del) button', function() {
      var html = win.__test__.rowHTML(5);
      ok(html.indexOf("data-del='5'") >= 0, 'has data-del button');
    });

    test('removeRow keeps remaining rows contiguous', function() {
      // Fill rows 1..3 then remove row 2. Mock customConfirm auto-confirms
      // synchronously, so the table is rebuilt before the next line.
      var set = function(i, val) {
        var tr = doc.querySelector("#sched_cmd_body tr[data-i='" + i + "']");
        var inp = tr.querySelector("input[name='cmd_" + i + "']");
        inp.value = val;
      };
      set(1, 'cmdA'); set(2, 'cmdB'); set(3, 'cmdC');
      win.__test__.removeRow(2);
      var rows = doc.querySelectorAll('#sched_cmd_body tr[data-i]');
      is(rows.length, 9, 'one fewer row after remove (10 -> 9)');
      var first = doc.querySelector("#sched_cmd_body tr[data-i='1'] input[name='cmd_1']");
      var second = doc.querySelector("#sched_cmd_body tr[data-i='2'] input[name='cmd_2']");
      is(first.value, 'cmdA', 'first kept');
      is(second.value, 'cmdC', 'third shifted to 2');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // Save All (CMD-4): one alert naming the failed row, stop there
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;
    var alerts = [], hits = [];
    win.customAlert = function(m){ alerts.push(String(m)); };
    var saveAllBtn = doc.getElementById('cmd_save_all');

    // Success: every row posts, one request at a time, no alert.
    win.fetch = function(url){
      hits.push(String(url||''));
      return Promise.resolve({ok:true,status:200,text:function(){return Promise.resolve('{"status":"ok"}');}});
    };
    saveAllBtn.click();
    await wait(dom, 2600);

    test('Save All saves every row and stays silent on success', function() {
      ok(!!saveAllBtn, 'Save All button exists');
      is(alerts.length, 0, 'no alert on success');
      is(hits.length, 10, 'one POST per row (10 rows)');
      ok(hits.every(function(u){ return u.indexOf('/api/scheduled-command-slot') >= 0; }), 'all posts hit the slot endpoint');
      is(saveAllBtn.disabled, false, 'button re-enabled');
    });

    // Mid-loop failure: rows 1-2 saved, row 3 rejected -> one visible error
    // naming the row, and the run stops there.
    alerts = []; hits = []; var n = 0;
    win.fetch = function(url){
      hits.push(String(url||''));
      n++;
      return Promise.resolve(n < 3
        ? {ok:true,status:200,text:function(){return Promise.resolve('{"status":"ok"}');}}
        : {ok:false,status:400,text:function(){return Promise.resolve('{"status":"err","msg":"interval"}');}});
    };
    saveAllBtn.click();
    await wait(dom, 900);

    test('a failure in the middle is reported once and stops the run', function() {
      is(alerts.length, 1, 'exactly one alert');
      ok(/Save All failed at row 3\/10/.test(alerts[0]||''), 'names the failing row: ' + (alerts[0]||''));
      ok(/not saved/i.test(alerts[0]||''), 'states that the rest were not saved');
      is(hits.length, 3, 'stopped at the failing row (3 posts, not 10)');
      is(saveAllBtn.disabled, false, 'button re-enabled');
    });

    // Network failure on the first row.
    alerts = []; hits = [];
    win.fetch = function(){ hits.push('x'); return Promise.reject(new Error('offline')); };
    saveAllBtn.click();
    await wait(dom, 400);

    test('a network failure is surfaced with its reason', function() {
      is(alerts.length, 1, 'one alert');
      ok(/Save All failed at row 1\/10/.test(alerts[0]||''), 'names the failing row');
      ok(/Network error/i.test(alerts[0]||''), 'names the network reason');
      is(hits.length, 1, 'no further posts');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // Summary
  // ════════════════════════════════════════════════════════════════
  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e) { console.error('Fatal:', e); process.exit(1); });
