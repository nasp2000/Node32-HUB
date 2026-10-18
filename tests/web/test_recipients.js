// Recipients Page Tests
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'recipients.html');
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
  console.log('\n=== Recipients Page Tests ===\n');

  var dom, win, doc;

  // ════════════════════════════════════════════════════════════════
  // Translation
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('rt returns fallback when no translation cache', function() {
      var r = win.__test__.rt('opEqual', 'I (equal)');
      is(r, 'I (equal)', 'falls back to default');
    });

    test('rt returns key as fallback when fb is undefined', function() {
      var r = win.__test__.rt('nonexistent');
      is(r, 'nonexistent', 'returns key itself');
    });

    test('rt reads from localStorage ui_lang_cache', function() {
      var cache = { opEqual: 'IGUAL' };
      win.localStorage.setItem('ui_lang_cache', JSON.stringify(cache));
      var r = win.__test__.rt('opEqual', 'fallback');
      is(r, 'IGUAL', 'reads from cache');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // rowHTML
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window;

    test('rowHTML returns a table row with data-i attribute', function() {
      var html = win.__test__.rowHTML(1);
      ok(html.indexOf('data-i') >= 0, 'has data-i');
      ok(html.indexOf("'1'") >= 0 || html.indexOf('"1"') >= 0, 'contains index 1');
    });

    test('rowHTML includes operator select with option values', function() {
      var html = win.__test__.rowHTML(2);
      ok(html.indexOf("value='I'") >= 0, 'has I option');
      ok(html.indexOf("value='Hy'") >= 0, 'has Hy option');
    });

    test('rowHTML has frequency select with 10m default', function() {
      var html = win.__test__.rowHTML(3);
      ok(html.indexOf("value='600' selected") >= 0, '10m default');
    });

    test('rowHTML includes save button with data-save', function() {
      var html = win.__test__.rowHTML(4);
      ok(html.indexOf("data-save='4'") >= 0, 'save button for slot 4');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // addRow / init
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    test('init creates 10 rows in the table', function() {
      var rows = doc.querySelectorAll('#rec_body tr');
      is(rows.length, 10, '10 rows from init');
    });

    test('addRow adds one more row', function() {
      var before = doc.querySelectorAll('#rec_body tr').length;
      win.__test__.addRow();
      var after = doc.querySelectorAll('#rec_body tr').length;
      is(after, before + 1, 'one row added');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // removeRow — remove + rebuild with contiguous indices
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    test('rowHTML includes a remove (data-del) button', function() {
      var html = win.__test__.rowHTML(7);
      ok(html.indexOf("data-del='7'") >= 0, 'has data-del button');
    });

    test('removeRow keeps remaining rows contiguous', function() {
      var set = function(i, val) {
        var tr = doc.querySelector("#rec_body tr[data-i='" + i + "']");
        var inp = tr.querySelector("input[name='r_name_" + i + "']");
        inp.value = val;
      };
      set(1, 'Sensor A'); set(2, 'Sensor B'); set(3, 'Sensor C');
      win.__test__.removeRow(2);
      var rows = doc.querySelectorAll('#rec_body tr[data-i]');
      is(rows.length, 9, 'one fewer row after remove (10 -> 9)');
      var first = doc.querySelector("#rec_body tr[data-i='1'] input[name='r_name_1']");
      var second = doc.querySelector("#rec_body tr[data-i='2'] input[name='r_name_2']");
      is(first.value, 'Sensor A', 'first kept');
      is(second.value, 'Sensor C', 'third shifted to 2');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // Tab system
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    test('tab bar has 3 buttons', function() {
      var bar = doc.querySelector('.acc-tab-bar');
      ok(bar, 'tab bar exists');
      var btns = bar.querySelectorAll('.acc-tab-btn');
      is(btns.length, 3, 'three tabs');
    });

    test('switchTab toggles active class', function() {
      var bar = doc.querySelector('.acc-tab-bar');
      var btns = bar.querySelectorAll('.acc-tab-btn');
      // Click the second tab (rules)
      btns[1].click();
      ok(btns[1].classList.contains('active'), 'rules tab active');
      ok(!btns[0].classList.contains('active'), 'sender tab inactive');
    });

    test('switchTab toggles panels', function() {
      var bar = doc.querySelector('.acc-tab-bar');
      var btns = bar.querySelectorAll('.acc-tab-btn');
      // Click the third tab (cmds)
      btns[2].click();
      var panels = doc.querySelectorAll('.acc-panel');
      var found = false;
      for (var i = 0; i < panels.length; i++) {
        if (panels[i].getAttribute('data-tab') === 'cmds') {
          ok(panels[i].classList.contains('active'), 'cmds panel active');
          found = true;
        }
      }
      ok(found, 'cmds panel found');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // preload
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    for (var i = 0; i < 3; i++) win.__test__.addRow();

    win.__MOCK_API__ = {
      '/api/recipient-slots': { slots: [
        { name: 'Sensor A', op: '+', emails: 'a@x.com', freq: '300', active: true },
        { name: 'Sensor B', op: 'R', emails: 'b@x.com', freq: '600', active: false },
        { name: 'Sensor C', op: 'Hy', emails: 'c@x.com', freq: '3600', active: true }
      ]}
    };

    win.__test__.preload();
    await wait(dom, 50);

    test('preload loads slot 1 name', function() {
      var rows = doc.querySelectorAll('#rec_body tr');
      var f1 = rows[0].querySelector("input[name='r_name_1']");
      is(f1.value, 'Sensor A', 'slot 1 name');
    });

    test('preload loads slot 2 name', function() {
      var rows = doc.querySelectorAll('#rec_body tr');
      var f2 = rows[1].querySelector("input[name='r_name_2']");
      is(f2.value, 'Sensor B', 'slot 2 name');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // updateNameHint — keyword pattern detection for ALL operator types
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    // Helper: set name/op on row i (1-indexed, from init's 10 rows)
    // then dispatch input event to trigger updateNameHint
    function setRow(i, nameVal, opVal) {
      var tr = doc.querySelector("#rec_body tr[data-i='" + i + "']");
      if (!tr) throw new Error('Row ' + i + ' not found');
      var ni = tr.querySelector("input[name='r_name_" + i + "']");
      ni.value = nameVal;
      var os = tr.querySelector("select[name='r_op_" + i + "']");
      os.value = opVal;
      var ev = doc.createEvent('Event');
      ev.initEvent('input', true, true);
      ni.dispatchEvent(ev);
      return tr;
    }

    function hintText(tr) {
      return (tr.querySelector('.nm-hint') || {}).innerHTML || '';
    }

    function hasHint(tr, keyword) {
      return hintText(tr).indexOf(keyword) >= 0;
    }

    // ── Operator select validation ──
    test('rowHTML has all 13 operator options', function() {
      var html = win.__test__.rowHTML(99);
      var ops = ['I','D','+','-','G','L','R','DR','V','C','P','Cw','Hy'];
      ops.forEach(function(op) {
        ok(html.indexOf("value='" + op + "'") >= 0, 'has op ' + op);
      });
    });

    // ── Frequency select validation ──
    test('rowHTML has all 12 frequency options', function() {
      var html = win.__test__.rowHTML(99);
      [
        ['180','3m'], ['300','5m'], ['600','10m'], ['1200','20m'],
        ['1800','30m'], ['2700','45m'], ['3600','1h'], ['7200','2h'],
        ['14400','4h'], ['21600','6h'], ['43200','12h'], ['86400','24h']
      ].forEach(function(f) {
        ok(html.indexOf("value='" + f[0] + "'") >= 0, 'freq ' + f[0] + ' (' + f[1] + ')');
      });
    });

    // ── Col detection ──
    test('col pattern with op R shows col hint', function() {
      var tr = setRow(1, 'col3, 10..50', 'R');
      ok(hasHint(tr, 'Col'), 'col detected');
    });

    test('col+R/DR without range shows warning', function() {
      var tr = setRow(2, 'col3, 100', 'R');
      ok(hasHint(tr, 'requires range'), 'R warning');
    });

    test('col+DR without range shows warning', function() {
      var tr = setRow(3, 'col2, 50', 'DR');
      ok(hasHint(tr, 'requires range'), 'DR warning');
    });

    test('col+Hy without range shows warning', function() {
      var tr = setRow(4, 'col1, 20', 'Hy');
      ok(hasHint(tr, 'requires range'), 'Hy warning');
    });

    test('col+range with DR works without warning', function() {
      var tr = setRow(5, 'col5, 100..200', 'DR');
      ok(hasHint(tr, 'Col'), 'col ok');
      ok(!hasHint(tr, 'requires'), 'no warning');
    });

    test('col+range with Hy works without warning', function() {
      var tr = setRow(6, 'col2, 30..60', 'Hy');
      ok(hasHint(tr, 'Col'), 'col ok');
      ok(!hasHint(tr, 'requires'), 'no warning');
    });

    // ── Any detection ──
    test('any pattern shows any hint', function() {
      var tr = setRow(7, 'any, 999', '+');
      ok(hasHint(tr, 'any'), 'any detected');
    });

    test('any+R without range shows warning', function() {
      var tr = setRow(8, 'any, 100', 'R');
      ok(hasHint(tr, 'requires range'), 'R warning');
    });

    // ── Key detection ──
    test('key pattern with threshold shows key hint', function() {
      var tr = setRow(9, 'temp_aht20, 25.5', 'I');
      ok(hasHint(tr, 'Key'), 'key detected');
    });

    test('key+R without range shows warning', function() {
      var tr = setRow(10, 'pressure, 10', 'R');
      ok(hasHint(tr, 'requires range'), 'R warning');
    });

    // Add more rows to test additional cases
    for (var _ri = 11; _ri <= 19; _ri++) { win.__test__.addRow(); }

    test('key+P shows watchdog alert', function() {
      var tr = setRow(11, 'heartbeat, 30', 'P');
      ok(hasHint(tr, 'Watchdog'), 'watchdog');
    });

    test('bare range with op R shows range hint', function() {
      var tr = setRow(12, '20.00..45.00', 'R');
      ok(hasHint(tr, 'Range'), 'range');
    });

    test('bare range without op R/DR suggests correct op', function() {
      var tr = setRow(13, '10..50', 'I');
      ok(hasHint(tr, 'Use op'), 'suggests R/DR');
    });

    test('exact digit format shows exact hint', function() {
      var tr = setRow(14, '019.00', 'I');
      ok(hasHint(tr, 'Exact format'), 'exact');
    });

    test('simple text with op I shows text hint', function() {
      var tr = setRow(15, 'FAULT', 'I');
      ok(hasHint(tr, 'Exact text'), 'text+I');
    });

    test('simple text with op C shows count hint', function() {
      var tr = setRow(16, 'ERROR', 'C');
      ok(hasHint(tr, 'Count'), 'text+C');
    });

    test('simple text with op Cw shows count hint', function() {
      var tr = setRow(17, 'EVENT', 'Cw');
      ok(hasHint(tr, 'Count'), 'text+Cw');
    });

    test('simple text with op P shows watchdog hint', function() {
      var tr = setRow(18, 'WATCHDOG', 'P');
      ok(hasHint(tr, 'Watchdog'), 'text+P');
    });

    test('simple text with numeric op shows warning', function() {
      var tr = setRow(19, 'ALERT', '+');
      ok(hasHint(tr, 'Numeric op'), 'numeric op warning');
    });

    // ── Edge case: empty name clears hint ──
    test('empty name clears hint', function() {
      var tr = doc.querySelector("#rec_body tr[data-i='1']");
      var ni = tr.querySelector("input[name='r_name_1']");
      ni.value = '';
      var ev = doc.createEvent('Event');
      ev.initEvent('input', true, true);
      ni.dispatchEvent(ev);
      is(hintText(tr), '', 'empty hint');
    });
  }

  // ════════════════════════════════════════════════════════════════
  // saveDontCopy
  // ════════════════════════════════════════════════════════════════
  {
    dom = loadPage(); await wait(dom, 800);
    win = dom.window; doc = win.document;

    test('saveDontCopy is a function', function() {
      ok(typeof win.__test__.saveDontCopy === 'function', 'saveDontCopy exists');
    });
  } // end saveDontCopy block

  // ════════════════════════════════════════════════════════════════
  // Summary
  // ════════════════════════════════════════════════════════════════
  var total = passed + failed;
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e) { console.error('Fatal:', e); process.exit(1); });
