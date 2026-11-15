// Date/Time Commands Page Tests
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'commands_datetime.html');
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
  console.log('\n=== Date/Time Commands Page Tests ===\n');

  var dom, win, doc;

  // ════════════════════════════════════════════════════════════════
  // e — HTML escape
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('e escapes HTML special chars', function() {
      is(win.__test__.e('<>&"\''), '&lt;&gt;&amp;&quot;&#39;', 'escaped');
    });

    test('e returns empty for null/undefined', function() {
      is(win.__test__.e(null), '', 'null');
      is(win.__test__.e(undefined), '', 'undefined');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // t — translation
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('t returns fallback without cache', function() {
      is(win.__test__.t('opYes', 'Yes'), 'Yes', 'fallback');
    });

    test('t returns key if no fallback', function() {
      is(win.__test__.t('missing'), 'missing', 'key');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // r — row HTML
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('r returns a table row with data-i', function() {
      var html = win.__test__.r(1);
      ok(html.indexOf("data-i='1'") >= 0 || html.indexOf('data-i="1"') >= 0, 'data-i');
    });

    test('r includes date input', function() {
      var html = win.__test__.r(2);
      ok(html.indexOf("type='date'") >= 0, 'date input');
    });

    test('r includes time input', function() {
      var html = win.__test__.r(3);
      ok(html.indexOf("type='time'") >= 0, 'time input');
    });

    test('r includes daily checkbox', function() {
      var html = win.__test__.r(4);
      ok(html.indexOf("type='checkbox'") >= 0, 'daily checkbox');
    });

    test('r includes type select with options', function() {
      var html = win.__test__.r(5);
      ok(html.indexOf("value='system'") >= 0, 'system option');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // addRow / init
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    doc = dom.window.document;

    test('init creates 3 rows', function() {
      var rows = doc.querySelectorAll('#sched_dt_body tr');
      is(rows.length, 3, '3 rows');
    });

    test('addRow adds one more row', function() {
      var win2 = dom.window;
      var before = doc.querySelectorAll('#sched_dt_body tr').length;
      win2.__test__.addRow();
      var after = doc.querySelectorAll('#sched_dt_body tr').length;
      is(after, before + 1, 'row added');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // 16 slots (BACKLOG CMD-2): the page covers every slot the server stores
  // and lists — /api/scheduled-datetime-slots serialises exactly these.
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;
    var body = doc.getElementById('sched_dt_body');

    test('the page offers the full 16-slot range', function() {
      var guard = 0;
      while (body.querySelectorAll('tr[data-i]').length < 16 && guard++ < 40) win.__test__.addRow();
      is(body.querySelectorAll('tr[data-i]').length, 16, '16 rows');
      var ids = [];
      body.querySelectorAll('tr[data-i]').forEach(function(tr) { ids.push(tr.getAttribute('data-i')); });
      is(ids.join(','), '1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16', 'contiguous 1..16');
      is(win.__test__.addRow(), false, 'row 17 refused');
      is(body.querySelectorAll('tr[data-i]').length, 16, 'still 16 rows');
    });

    test('slot 16 saves with i=16 (inside the served range)', function() {
      var sent = null;
      win.fetch = function(u, o) {
        if (u === '/api/scheduled-datetime-slot' && o && o.method === 'POST') {
          sent = o.body;
          return Promise.resolve({ok:true, status:200, json:function(){ return Promise.resolve({status:'ok'}); }});
        }
        return Promise.resolve({ok:true, status:200,
          json:function(){ return Promise.resolve({}); },
          text:function(){ return Promise.resolve(''); }});
      };
      var row = body.querySelector("tr[data-i='16']");
      row.querySelector("input[name='sd_cmd_16']").value = 'emailday';
      row.querySelector('[data-save]').click();
      ok(sent, 'POST issued');
      is(sent.get('i'), '16', 'slot index 16 sent');
      is(sent.get('command'), 'emailday', 'command sent');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // rm — remove + rebuild with contiguous indices
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    test('r includes a remove (data-del) button', function() {
      var html = win.__test__.r(5);
      ok(html.indexOf("data-del='5'") >= 0, 'has data-del button');
    });

    test('rm keeps remaining rows contiguous', function() {
      var set = function(i, val) {
        var tr = doc.querySelector("#sched_dt_body tr[data-i='" + i + "']");
        var inp = tr.querySelector("input[name='sd_cmd_" + i + "']");
        inp.value = val;
      };
      set(1, 'dtA'); set(2, 'dtB'); set(3, 'dtC');
      win.__test__.rm(2);
      var rows = doc.querySelectorAll('#sched_dt_body tr[data-i]');
      is(rows.length, 2, 'one fewer row after remove (3 -> 2)');
      var first = doc.querySelector("#sched_dt_body tr[data-i='1'] input[name='sd_cmd_1']");
      var second = doc.querySelector("#sched_dt_body tr[data-i='2'] input[name='sd_cmd_2']");
      is(first.value, 'dtA', 'first kept');
      is(second.value, 'dtC', 'third shifted to 2');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // tt — toggle template field
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    // Add rows manually to use for tt tests
    // Rows 1-3 exist from init, rows 4-13 will be added
    for (var _i = 0; _i < 10; _i++) win.__test__.addRow();

    test('tt disables template select for system type', function() {
      // Row 11 exists after adding 1 row; type defaults to 'system'
      var tr = doc.querySelector("#sched_dt_body tr[data-i='11']");
      ok(tr, 'row 11 exists');
      win.__test__.tt(11);
      var tpl = tr.querySelector("select[name='sd_tpl_11']");
      ok(tpl.disabled, 'template disabled for system');
      is(tpl.style.opacity, '0.3', 'dimmed');
    });

    test('tt enables template select for rs232 type', function() {
      var tr = doc.querySelector("#sched_dt_body tr[data-i='12']");
      ok(tr, 'row 12 exists');
      var typeSel = tr.querySelector("select[name='sd_type_12']");
      if (typeSel) typeSel.value = 'rs232';
      win.__test__.tt(12);
      var tpl = tr.querySelector("select[name='sd_tpl_12']");
      ok(!tpl.disabled, 'template enabled for rs232');
      is(tpl.style.opacity, '1', 'full opacity');
    });

    test('tt enables template for modbus type', function() {
      var tr = doc.querySelector("#sched_dt_body tr[data-i='13']");
      ok(tr, 'row 13 exists');
      var typeSel = tr.querySelector("select[name='sd_type_13']");
      if (typeSel) typeSel.value = 'modbus';
      win.__test__.tt(13);
      var tpl = tr.querySelector("select[name='sd_tpl_13']");
      ok(!tpl.disabled, 'template enabled for modbus');
      is(tpl.style.opacity, '1', 'full opacity');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // Daily reboot row follows OEE shifts
  // ════════════════════════════════════════════════════════════════
  {
    var html = fs.readFileSync(PAGE, 'utf8');
    var domF = new JSDOM(html, {url:'http://localhost', runScripts:'dangerously', pretendToBeVisual:true,
      beforeParse: function(w) {
        w.__MOCK_API__ = {
          '/api/daily-reboot?i=1': {status:'ok', i:1, time:'00:00', enabled:true, follows_shifts:true, effective_time:'22:05'},
          '/api/daily-reboot?i=2': {status:'ok', i:2, time:'', enabled:false}
        };
      }});
    await wait(domF, 800);
    var docF = domF.window.document;

    test('s1 shows effective shift time and locks the row when following shifts', function () {
      var timeIn = docF.querySelector("input[name='rd_time_1']");
      ok(timeIn, 's1 time input exists');
      is(timeIn.value, '22:05', 'effective time displayed');
      is(timeIn.disabled, true, 's1 time locked');
      var enSel = docF.querySelector("select[name='rd_en_1']");
      ok(enSel && enSel.disabled, 's1 enabled locked');
      var svBtn = docF.querySelector("[data-save-rd='1']");
      ok(svBtn && svBtn.disabled, 's1 save locked');
    });

    test('s2 stays manual when s1 follows shifts', function () {
      var timeIn = docF.querySelector("input[name='rd_time_2']");
      ok(timeIn, 's2 time input exists');
      is(timeIn.disabled, false, 's2 time editable');
      var svBtn = docF.querySelector("[data-save-rd='2']");
      ok(svBtn && !svBtn.disabled, 's2 save enabled');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // RS232 templates are only fetched when the build has RS232
  // (crawl 2026-11-11: 404 /api/rs232-templates on packs without it)
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    var calls = [];
    var origFetch = win.fetch;
    win.fetch = function (u, o) {
      calls.push(String(u));
      return origFetch(u, o);
    };

    test('lt() does not fetch templates when RS232 is disabled in the build', function () {
      calls.length = 0;
      win._rs232Enabled = false;
      win.__test__.lt();
      is(calls.filter(function (u) { return u.indexOf('/api/rs232-templates') >= 0; }).length, 0,
        'no /api/rs232-templates request');
    });

    test('lt() still fetches templates when RS232 is enabled', function () {
      calls.length = 0;
      win._rs232Enabled = true;
      win.__test__.lt();
      ok(calls.filter(function (u) { return u.indexOf('/api/rs232-templates') >= 0; }).length === 1,
        'exactly one /api/rs232-templates request');
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
    var saveAllBtn = doc.getElementById('sched_dt_save_all');

    win.fetch = function(url){
      hits.push(String(url||''));
      return Promise.resolve({ok:true,status:200,text:function(){return Promise.resolve('{"status":"ok"}');}});
    };
    saveAllBtn.click();
    await wait(dom, 1200);

    test('dt Save All saves every row and stays silent on success', function() {
      ok(!!saveAllBtn, 'Save All button exists');
      is(alerts.length, 0, 'no alert on success');
      is(hits.length, 3, 'one POST per row (3 rows)');
      ok(hits.every(function(u){ return u.indexOf('/api/scheduled-datetime-slot') >= 0; }), 'all posts hit the slot endpoint');
      is(saveAllBtn.disabled, false, 'button re-enabled');
    });

    alerts = []; hits = []; var n = 0;
    win.fetch = function(url){
      hits.push(String(url||''));
      n++;
      return Promise.resolve(n < 2
        ? {ok:true,status:200,text:function(){return Promise.resolve('{"status":"ok"}');}}
        : {ok:false,status:400,text:function(){return Promise.resolve('{"status":"err","msg":"hora invalida"}');}});
    };
    saveAllBtn.click();
    await wait(dom, 700);

    test('dt a failure in the middle is reported once and stops the run', function() {
      is(alerts.length, 1, 'exactly one alert');
      ok(/Save All failed at row 2\/3/.test(alerts[0]||''), 'names the failing row: ' + (alerts[0]||''));
      ok(/not saved/i.test(alerts[0]||''), 'states that the rest were not saved');
      is(hits.length, 2, 'stopped at the failing row (2 posts, not 3)');
      is(saveAllBtn.disabled, false, 'button re-enabled');
    });

    alerts = []; hits = [];
    win.fetch = function(){ hits.push('x'); return Promise.reject(new Error('offline')); };
    saveAllBtn.click();
    await wait(dom, 400);

    test('dt a network failure is surfaced with its reason', function() {
      is(alerts.length, 1, 'one alert');
      ok(/Save All failed at row 1\/3/.test(alerts[0]||''), 'names the failing row');
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
