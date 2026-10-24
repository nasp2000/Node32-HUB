// test_rs232.js — /rs232 always shows the serial configuration in force right now.
// The firmware returns the live runtime values (module profile included) plus
// active_module / active_module_manual; the page never locks the fields and the
// module profile is never written from here (bench mode only reads the page).
process.on('unhandledRejection', function(){});
var fs = require('fs'), path = require('path'), { JSDOM } = require('jsdom');
var html = fs.readFileSync(path.join(__dirname, 'pages', 'rs232_asset.html'), 'utf8').toString();
var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost/rs232' });
var win = dom.window, doc = win.document;
var passed = 0, failed = 0;

function assert(c, m) { if (c) passed++; else { console.log('  FAIL:', m); failed++; } }
function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }

function cfg(activeModule, manual, live) {
    live = live || {};
    return {
        '/api/config?section=rs232': {
            sniffer_mode: false,
            active_module: activeModule,
            active_module_manual: !!manual,
            rs232: {
                enabled: true,
                baudrate_index: live.idx !== undefined ? live.idx : 3,
                data_bits: live.data || 8,
                parity: live.parity || 'none',
                stop_bits: live.stop || 2,
                invert_pins: false, read_timeout_ms: 1000, use_xon_xoff: false,
                baudrate_list: [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]
            }
        }
    };
}

function anyFieldDisabled() {
    var els = doc.getElementById('rs232-form').querySelectorAll('input,select,button');
    for (var i = 0; i < els.length; i++) {
        if (String(els[i].className).indexOf('rs232-tab-btn') >= 0) continue;
        if (els[i].disabled) return true;
    }
    return false;
}

(async function () {
    console.log('\n=== RS232 Page Tests ===\n');
    await sleep(120);

    // ---- variation 1: Bricon owns the port, own profile (not bench mode) ----
    win.__MOCK_API__ = cfg('bricon', false, { stop: 2 });
    win.loadCurrentConfig(); await sleep(300);

    var box = doc.getElementById('rs232-module');
    assert(box.style.display !== 'none', 'module box shown when a module is enabled');
    assert(doc.getElementById('rs232-module-name').textContent.indexOf('Bricon') >= 0,
           'module name is Bricon');
    assert(doc.getElementById('rs232-module-profile').textContent.indexOf('9600 8N2') >= 0,
           'profile shows the live Bricon 9600 8N2 (got "' +
           doc.getElementById('rs232-module-profile').textContent + '")');
    assert(doc.getElementById('rs232-status').innerHTML.indexOf('Bricon') >= 0,
           'status line names the connected module');
    assert(doc.getElementById('stop_bits').value === '2', 'stop bits show the live profile');
    assert(!anyFieldDisabled(), 'fields are never locked');
    var note = doc.getElementById('rs232-module-note').textContent;
    assert(note.indexOf('right now') >= 0, 'note says the values are the ones in force');
    assert(note.indexOf('Bench') < 0, 'note is not the bench text outside bench mode');

    // ---- variation 2: live values are actually the ones rendered ----
    win.__MOCK_API__ = cfg('bricon', false, { idx: 5, data: 7, parity: 'even', stop: 1 });
    win.loadCurrentConfig(); await sleep(300);
    assert(doc.getElementById('baudrate').value === '38400', 'baud comes from the live values');
    assert(doc.getElementById('data_bits').value === '7', 'data bits come from the live values');
    assert(doc.getElementById('parity').value === 'even', 'parity comes from the live values');
    assert(doc.getElementById('stop_bits').value === '1', 'stop bits come from the live values');

    // ---- variation 3: bench mode (module uses the RS232 page settings) ----
    win.__MOCK_API__ = cfg('bricon', true, { idx: 5, data: 7, parity: 'even', stop: 1 });
    win.loadCurrentConfig(); await sleep(300);
    note = doc.getElementById('rs232-module-note').textContent;
    assert(note.indexOf('Bench') >= 0, 'bench mode note shown');
    assert(!anyFieldDisabled(), 'bench mode leaves the fields editable');

    // ---- variation 4: Unikon RAMBOX ----
    win.__MOCK_API__ = cfg('rambox', false, { stop: 1 });
    win.loadCurrentConfig(); await sleep(300);
    assert(doc.getElementById('rs232-module-name').textContent.indexOf('Unikon') >= 0,
           'module name is Unikon RAMBOX');
    assert(doc.getElementById('rs232-module-profile').textContent.indexOf('9600 8N1') >= 0,
           'profile shows the live Unikon 9600 8N1');

    // ---- variation 5: no module -> box hidden, no suffix ----
    win.__MOCK_API__ = cfg('none', false, { stop: 1 });
    win.loadCurrentConfig(); await sleep(300);
    assert(doc.getElementById('rs232-module').style.display === 'none',
           'module box hidden when no module is enabled');
    assert(doc.getElementById('rs232-status').innerHTML.indexOf('Unikon') < 0 &&
           doc.getElementById('rs232-status').innerHTML.indexOf('Bricon') < 0,
           'status line has no module suffix without a module');
    assert(!anyFieldDisabled(), 'fields editable with no module enabled');

    console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed');
    process.exit(failed ? 1 : 0);
})();
