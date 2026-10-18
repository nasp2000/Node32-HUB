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
  // Summary
  // ════════════════════════════════════════════════════════════════
  var total = passed + failed;
  console.log('\nResults: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e) { console.error('Fatal:', e); process.exit(1); });
