// Proximity Shifts Round-Trip Test
// Catches: user edits shift config, clicks Save, returns to page, config lost.
const { JSDOM } = require('jsdom');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, 'pages', 'proximity.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

// Mock API: the extract.py MOCK_LAYER already reads window.__MOCK_API__ and
// wraps fetch; here we just provide per-URL fixtures and capture POST bodies.
function makeMock(api) {
  var win = this;
  win.__MOCK_API__ = api || {};
  win.__postBodies = [];
  win.customAlert = function(m) { win.__alerts = win.__alerts || []; win.__alerts.push(m); };
  win.customConfirm = function(m, cb) { if (cb) cb(true); };
  win.showLoading = function() {};
  win.hideLoading = function() {};
  win.disableBtn = function(b) {};
  win.enableBtn = function(b) {};
  win.showSavedCard = function() {};
  win._rs232Enabled = false;
  win._modbusEnabled = false;
}

function loadPage() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var orig = console.error; console.error = function() {
    var m = arguments[0]||''; if(typeof m!=='string'||m.indexOf('Not implemented')<0) orig.apply(console,arguments);
  };
  var dom = new JSDOM(html, {url:'http://localhost', runScripts:'dangerously', pretendToBeVisual:true, beforeParse: function(w){ makeMock.call(w); }});
  console.error = orig;
  return dom;
}

function wait(dom, ms) {
  return new Promise(function(r){setTimeout(function(){r(dom);}, ms || 600);});
}

async function run() {
  console.log('\n=== Proximity Shifts Round-Trip Tests ===\n');

  // Fixture: a page whose shift-config GET returns saved values.
  {
    var html = fs.readFileSync(PAGE, 'utf8');
    var dom = new JSDOM(html, {
      url:'http://localhost', runScripts:'dangerously', pretendToBeVisual:true,
      beforeParse: function(w) {
        w.window = w;
        makeMock.call(w, {
          '/api/proximity/save-all': { ok: true },
          '/api/proximity/shift-config': {
            en: true, ar: true, nSlots: 2,
            slots: [{h:7, m:30}, {h:19, m:0}],
            emails: 'shift@test.com'
          },
          '/api/proximity/shiftday-emails': { emails: 'shift@test.com' },
          '/api/proximity/config': { channels: [
            { n:'Sensor A', m:0, gi:255, gi2:255, go:255, db:50, gl:0, pd:0, pu:0, mo:false }
          ]},
          '/api/proximity': { channels: [
            { name:'Sensor A', mode:0, count_in:0, count_out:0, total_in:0, total_out:0, cycle_ms:0, ppm:0, state:false, goal:0, dir:0 }
          ]}
        });
      }
    });
    var doc = dom.window.document;
    var win = dom.window;
    await wait(dom, 800);
    console.log('DIAG win===win.window: ' + (win===win.window) + ' fetchType=' + typeof win.fetch + ' postArrSame=' + (win.__postBodies===win.window.__postBodies));

    console.log('DIAG cfgLoaded=' + win.cfgLoaded + ' __test__.loadCfg=' + typeof win.__test__.loadCfg + ' shen=' + (doc.getElementById('shen')?doc.getElementById('shen').checked:'missing'));
    test('loadShiftCfg fills shift enable/auto-reset', function () {
      is(doc.getElementById('shen').checked, true, 'en checkbox');
      is(doc.getElementById('shar').checked, true, 'ar checkbox');
    });

    test('loadShiftCfg sets nSlots select and renders slot inputs', function () {
      is(doc.getElementById('shnsl').value, '2', 'nSlots select value');
      var inputs = doc.querySelectorAll('#shift-slots input');
      ok(inputs.length >= 4, 'should render 2 slots x (h + m) = 4 inputs, got ' + inputs.length);
    });

    test('loadShiftCfg fills slot hour/minute values', function () {
      is(doc.getElementById('sh_h0').value, '7', 'slot0 hour');
      is(doc.getElementById('sh_m0').value, '30', 'slot0 minute');
      is(doc.getElementById('sh_h1').value, '19', 'slot1 hour');
      is(doc.getElementById('sh_m1').value, '0', 'slot1 minute');
    });

    // Simulate user editing slot0 hour to 8, then Save.
    test('saveAll POSTs the edited shift config', async function () {
      doc.getElementById('sh_h0').value = '8';
      var captured = [];
      var realFetch = win.fetch;
      win.fetch = function(url, opts) {
        captured.push({ url: url, method: (opts&&opts.method)||'GET', body: (opts&&opts.body)||'' });
        return realFetch(url, opts);
      };
      win.saveAll();
      await wait(dom, 400);
      win.fetch = realFetch;
      var after = captured.filter(function(p){ return String(p.url).indexOf('save-all') >= 0; });
      ok(after.length === 1, 'exactly one save-all POST, got ' + after.length + ' captured=' + JSON.stringify(captured));
      var payload = JSON.parse(after[0].body);
      var sh = payload.shift;
      ok(sh, 'payload.shift exists');
      is(sh.en, true, 'shift.en');
      is(sh.ar, true, 'shift.ar');
      is(sh.nSlots, 2, 'shift.nSlots');
      ok(Array.isArray(sh.slots) && sh.slots.length === 2, 'shift.slots has 2 entries');
      is(sh.slots[0].h, 8, 'edited slot0 hour = 8');
      is(sh.slots[0].m, 30, 'slot0 minute = 30');
      is(sh.slots[1].h, 19, 'slot1 hour = 19');
      is(payload.emails, 'shift@test.com', 'payload.emails');
    });
  }

  // Round-trip: load config, then the page's own loadShiftCfg re-reads saved values.
  {
    var html = fs.readFileSync(PAGE, 'utf8');
    var dom = new JSDOM(html, {
      url:'http://localhost', runScripts:'dangerously', pretendToBeVisual:true,
      beforeParse: function(w) {
        w.window = w;
        makeMock.call(w, {
          '/api/proximity/shift-config': {
            en: true, ar: false, nSlots: 3,
            slots: [{h:6, m:0}, {h:14, m:0}, {h:22, m:30}],
            emails: 'a@b.com, c@d.com'
          },
          '/api/proximity/shiftday-emails': { emails: 'a@b.com, c@d.com' },
          '/api/proximity/config': { channels: [] },
          '/api/proximity': { channels: [] }
        });
      }
    });
    var win = dom.window;
    var doc = dom.window.document;
    await wait(dom, 800);

    test('reload preserves 3-slot config', function () {
      is(doc.getElementById('shnsl').value, '3', 'nSlots');
      is(doc.getElementById('sh_h0').value, '6', 'h0');
      is(doc.getElementById('sh_h2').value, '22', 'h2');
      is(doc.getElementById('sh_m2').value, '30', 'm2');
    });
  }

  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e){console.error('Fatal:',e);process.exit(1);});



