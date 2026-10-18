// Proximity Sensors Page Tests
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
  console.log('\n=== Proximity Sensor Page Tests ===\n');

  // ====== Mock data ======
  var mockChannels = [
    { name: 'Porta', mode: 0, count_in: 42, count_out: 7, cycle_ms: 120, ppm: 0.35 },
    { name: 'Janela', mode: 1, state: true, count_in: 1, count_out: 0, cycle_ms: 0, ppm: 0 },
    { name: 'Esteira', mode: 2, count_in: 150, count_out: 148, cycle_ms: 340, ppm: 2.94 }
  ];

  // ====== Render ======
  {
    var dom = loadPage(); await wait(dom, 800);
    var win = dom.window;
    var doc = dom.window.document;

    test('render populates status table', function () {
      win.render({ channels: mockChannels });
      var sb = doc.getElementById('sb');
      ok(!!sb, 'status body exists');
      var rows = sb.querySelectorAll('tr');
      is(rows.length, 3, 'should render 3 channel rows');
    });

    test('render shows channel names', function () {
      win.render({ channels: mockChannels });
      var sb = doc.getElementById('sb');
      ok(sb.textContent.indexOf('Porta') >= 0, 'should contain Porta');
      ok(sb.textContent.indexOf('Janela') >= 0, 'should contain Janela');
      ok(sb.textContent.indexOf('Esteira') >= 0, 'should contain Esteira');
    });

    test('render shows no-data for empty channels', function () {
      win.render({ channels: [] });
      var sb = doc.getElementById('sb');
      ok(sb.textContent.indexOf('No channels configured') >= 0, 'should show empty message');
    });

    test('render shows correct mode badge', function () {
      win.render({ channels: mockChannels });
      var sb = doc.getElementById('sb');
      ok(sb.textContent.indexOf('COUNTER') >= 0, 'mode 0 shows COUNTER');
      ok(sb.textContent.indexOf('PRESENCE') >= 0, 'mode 1 shows PRESENCE');
      ok(sb.textContent.indexOf('SPEED') >= 0, 'mode 2 shows SPEED');
    });

    test('render shows counts and cycle time', function () {
      win.render({ channels: mockChannels });
      var sb = doc.getElementById('sb');
      // ppm 0.35 → toFixed(1) rounds to "0.4", so search for "0."
      ok(sb.textContent.indexOf('42') >= 0, 'should show count_in 42');
      ok(sb.textContent.indexOf('120') >= 0, 'should show cycle_ms 120');
      ok(sb.textContent.indexOf('0.') >= 0, 'should show ppm value');
    });

    test('render keeps cycle but hides ppm for PRESENCE channels', function () {
      win.render({ channels: [
        { name: 'Vib', mode: 1, state: true, count_in: 9, count_out: 0, cycle_ms: 10799940, ppm: 884.7 }
      ]});
      var sb = doc.getElementById('sb');
      ok(sb.textContent.indexOf('10799940') >= 0, 'cycle_ms (ON/OFF interval) still shown in PRESENCE');
      is(sb.textContent.indexOf('884.7') >= 0, false, 'ppm hidden in PRESENCE');
      ok(sb.textContent.indexOf('● ON') >= 0, 'state shown in PRESENCE');
    });

    test('render shows elapsed time formatted as H:MM:SS', function () {
      win.render({ channels: [
        { name: 'A', mode: 0, count_in: 10, count_out: 0, cycle_ms: 0, ppm: 0, elapsed_sec: 0 },
        { name: 'B', mode: 0, count_in: 10, count_out: 0, cycle_ms: 0, ppm: 0, elapsed_sec: 65 },
        { name: 'C', mode: 0, count_in: 10, count_out: 0, cycle_ms: 0, ppm: 0, elapsed_sec: 3725 }
      ]});
      var sb = doc.getElementById('sb');
      is(sb.textContent.indexOf('0:01:05') >= 0, true, '65s formats as 0:01:05');
      is(sb.textContent.indexOf('1:02:05') >= 0, true, '3725s formats as 1:02:05');
    });

    test('render tolerates missing elapsed_sec (old payloads default to 0)', function () {
      win.render({ channels: mockChannels });
      var sb = doc.getElementById('sb');
      is(sb.textContent.indexOf('0:00:00') >= 0, true, 'no elapsed_sec field renders 0:00:00');
    });
  }

  // ====== Config render ======
  {
    var dom = loadPage(); await wait(dom, 800);
    var win = dom.window;
    var doc = dom.window.document;

    test('renderCfg populates config table', function () {
      var cfgData = { channels: [
        { n: 'Sensor A', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false },
        { n: 'Sensor B', m: 1, gi: 128, gi2: 200, go: 10, db: 100, gl: 5, pd: 0, pu: 0, mo: true }
      ]};
      win.renderCfg(cfgData);
      var cb = doc.getElementById('cb');
      ok(!!cb, 'config body exists');
      var rows = cb.querySelectorAll('tr');
      is(rows.length, 2, 'should render 2 config rows');
    });

    test('renderCfg fills input values', function () {
      var cfgData = { channels: [
        { n: 'Sensor A', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false }
      ]};
      win.renderCfg(cfgData);
      var cn0 = doc.getElementById('cn0');
      ok(!!cn0 && cn0.value === 'Sensor A', 'name input should be Sensor A');
      var cm0 = doc.getElementById('cm0');
      ok(!!cm0 && cm0.value === '0', 'mode select should be 0');
    });

    test('renderCfg multiplier accepts manual values (200x/4000x)', function () {
      var cfgData = { channels: [
        { n: 'A', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 200 },
        { n: 'B', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 4000 }
      ]};
      win.renderCfg(cfgData);
      var m0 = doc.getElementById('cmul0');
      var m1 = doc.getElementById('cmul1');
      ok(!!m0 && m0.tagName === 'INPUT', 'cmul0 should be a numeric input');
      is(m0.value, '200', 'manual 200x preserved');
      is(m1.value, '4000', 'manual 4000x preserved');
      var dl = doc.getElementById('mulPresets');
      ok(!dl, 'mulPresets datalist removed');
    });

    test('renderCfg divider input shows dv values, defaults to 1', function () {
      var cfgData = { channels: [
        { n: 'A', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 1, dv: 3 },
        { n: 'B', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 1 }
      ]};
      win.renderCfg(cfgData);
      var d0 = doc.getElementById('cdiv0');
      var d1 = doc.getElementById('cdiv1');
      ok(!!d0 && d0.tagName === 'INPUT', 'cdiv0 should be a numeric input');
      is(d0.value, '3', 'dv=3 preserved (2-3 moves per part)');
      is(d1.value, '1', 'missing dv defaults to 1');
      var th = doc.querySelector('th[data-i18n="proxCfgColDiv"]');
      ok(!!th, 'Div. header with i18n key exists');
    });

    test('saveAll reads dv inputs (payload asserted below)', function () {
      var cfgData = { channels: [
        { n: 'A', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 1, dv: 1 },
        { n: 'B', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 1, dv: 1 }
      ]};
      win.renderCfg(cfgData);
      win.renderShiftSlots();
      doc.getElementById('cdiv0').value = '3';
      doc.getElementById('cdiv1').value = '0';
      win.__postBodies = [];
      win.saveAll();
      ok(true, 'saveAll issued without throwing');
    });

    await wait(dom, 800);
    try {
      var posts = (win.__postBodies || []).filter(function (p) { return p.url.indexOf('/api/proximity/save-all') >= 0; });
      ok(posts.length > 0, 'save-all POST was issued');
      var body = JSON.parse(posts[posts.length - 1].body);
      is(body.channels[0].dv, 3, 'dv=3 sent for ch0');
      is(body.channels[1].dv, 1, 'dv=0 clamped to 1 for ch1');
      passed++; console.log('  ✓ saveAll payload carries dv with clamping');
    } catch (e) { failed++; console.log('  ✗ saveAll payload carries dv with clamping: ' + e.message); }

    test('renderCfg shows no-data for empty', function () {
      win.renderCfg({ channels: [] });
      var cb = doc.getElementById('cb');
      ok(cb.textContent.indexOf('No channels') >= 0, 'should show empty message');
    });

    test('renderCfg triggers side effects after valid load', function () {
      win.renderCfg({ channels: [{ n: 'T', m: 0, gi: 255, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false }]});
      var cb = doc.getElementById('cb');
      var rows = cb.querySelectorAll('tr');
      ok(rows.length > 0, 'config table should have rows after renderCfg');
    });
  }

  // ====== Lock fields ======
  {
    var dom = loadPage(); await wait(dom, 800);
    var win = dom.window;
    var doc = dom.window.document;

    test('_lockOutFields disables field group', function () {
      // Set up minimal DOM
      ['cgo0','cgl0','cpd0','cpu0'].forEach(function(id) {
        var el = doc.createElement('input'); el.id = id;
        doc.body.appendChild(el);
      });
      win._lockOutFields(0, true);
      is(doc.getElementById('cgo0').disabled, true);
      is(doc.getElementById('cgl0').disabled, true);
      is(doc.getElementById('cpd0').disabled, true);
      is(doc.getElementById('cpu0').disabled, true);
      ok(doc.getElementById('cgo0').style.opacity === '0.35');
    });

    test('_lockOutFields enables field group', function () {
      win._lockOutFields(0, false);
      is(doc.getElementById('cgo0').disabled, false);
      is(doc.getElementById('cgl0').disabled, false);
      is(doc.getElementById('cpd0').disabled, false);
      is(doc.getElementById('cpu0').disabled, false);
    });
  }

  // ====== Shift slots ======
  {
    var dom = loadPage(); await wait(dom, 800);
    var win = dom.window;
    var doc = dom.window.document;

    test('renderShiftSlots renders slot inputs', function () {
      var nsl = doc.getElementById('shnsl');
      if (!nsl) {
        nsl = doc.createElement('input'); nsl.id = 'shnsl'; nsl.type = 'number'; nsl.value = '3';
        doc.body.appendChild(nsl);
      }
      var cont = doc.getElementById('shift-slots');
      if (!cont) {
        cont = doc.createElement('div'); cont.id = 'shift-slots';
        doc.body.appendChild(cont);
      }
      nsl.value = '3';
      win.renderShiftSlots();
      var inputs = cont.querySelectorAll('input');
      ok(inputs.length >= 3, 'should have at least 3 slot time inputs');
    });

    test('renderShiftSlots adapts to slot count change', function () {
      var nsl = doc.getElementById('shnsl');
      // shnsl is a <select> with options 1-3; add option 5
      var opt5 = doc.createElement('option'); opt5.value = '5';
      nsl.appendChild(opt5);
      nsl.value = '5';
      win.renderShiftSlots();
      var cont = doc.getElementById('shift-slots');
      var inputs = cont.querySelectorAll('input');
      ok(inputs.length >= 5, 'should have at least 5 slot inputs for 5 slots');
    });
  }

  // ====== OEE status render (Running/Stopped columns) ======
  {
    var dom = loadPage(); await wait(dom, 800);
    var win = dom.window;
    var doc = dom.window.document;

    test('renderOeeStatus shows running/stopped times as H:MM:SS', function () {
      win.renderOeeStatus([
        { ch: 0, name: 'DI1', availability: 37.5, performance: 100, quality: 100,
          oee: 37.5, scrap_count: 0, running_sec: 10800, downtime_sec: 18000 }
      ]);
      var sb = doc.getElementById('oee-sb');
      ok(!!sb, 'oee status body exists');
      is(sb.textContent.indexOf('3:00:00') >= 0, true, '10800s running formats as 3:00:00');
      is(sb.textContent.indexOf('5:00:00') >= 0, true, '18000s stopped formats as 5:00:00');
      is(sb.textContent.indexOf('37.5') >= 0, true, 'shows availability value');
    });

    test('renderOeeStatus tolerates missing running/downtime fields', function () {
      win.renderOeeStatus([
        { ch: 0, name: 'DI1', availability: 0, performance: 0, quality: 100,
          oee: 0, scrap_count: 0 }
      ]);
      var sb = doc.getElementById('oee-sb');
      is(sb.textContent.indexOf('0:00:00') >= 0, true, 'missing fields render 0:00:00');
    });

    test('renderOeeStatus colors OEE by target and shows gap title', function () {
      win.renderOeeStatus([
        { ch: 0, name: 'A', availability: 100, performance: 100, quality: 100,
          oee: 90, scrap_count: 0, oee_manual: 85, running_sec: 100, downtime_sec: 0 },
        { ch: 1, name: 'B', availability: 37.5, performance: 100, quality: 90,
          oee: 33.8, scrap_count: 5, oee_manual: 85, running_sec: 100, downtime_sec: 100 }
      ]);
      var sb = doc.getElementById('oee-sb');
      var html = sb.innerHTML;
      ok(html.indexOf('#4ade80') >= 0, 'oee above target renders green');
      ok(html.indexOf('#f87171') >= 0, 'oee 50pp+ below target renders red');
      ok(html.indexOf('Target 85%') >= 0, 'gap title shows target value');
      ok(html.indexOf('-51.2pp') >= 0, 'gap title shows negative gap');
    });

    test('renderOeeStatus defaults target to 85 when oee_manual is 0', function () {
      win.renderOeeStatus([
        { ch: 0, name: 'A', availability: 100, performance: 100, quality: 100,
          oee: 100, scrap_count: 0, oee_manual: 0, running_sec: 100, downtime_sec: 0 }
      ]);
      var sb = doc.getElementById('oee-sb');
      ok(sb.innerHTML.indexOf('#4ade80') >= 0, 'oee 100 with no target renders green (default 85)');
      ok(sb.innerHTML.indexOf('Target 85%') >= 0, 'gap title falls back to 85');
    });
  }

  // ====== Utility functions ======
  {
    var dom = loadPage(); await wait(dom, 800);
    var win = dom.window;

    test('start/stop poll functions exist', function () {
      ok(typeof win._startProxPoll === 'function');
      ok(typeof win._stopProxPoll === 'function');
    });

    test('fetchData and reset functions exist', function () {
      ok(typeof win.fetchData === 'function');
      ok(typeof win.resetCh === 'function');
      ok(typeof win.resetAll === 'function');
    });
  }

  // ------ Summary ------
  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed\n');
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(function(e){console.error('Fatal:',e);process.exit(1);});
