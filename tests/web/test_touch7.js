// Falas (TouchMeteo-7) Page Tests — __test__ hooks (falasRow, falasSafe) + DOM wiring
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var PAGE = path.join(__dirname, 'pages', 'falas_asset.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function run() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
  var w = dom.window;
  var T = w.__test__;
  ok(T, '__test__ hooks must be installed');

  console.log('  --- falasRow ---');
  test('falasRow: label format', function() {
    var r = T.falasRow('quente.wav', 32000);
    is(r.name, 'quente.wav');
    is(r.size, 32000);
    is(r.label, 'quente.wav (32000 B)');
  });
  test('falasRow: zero size', function() {
    var r = T.falasRow('x.wav', 0);
    is(r.label, 'x.wav (0 B)');
  });

  console.log('  --- falasSafe ---');
  test('falasSafe: valid names', function() {
    ok(T.falasSafe('quente.wav'));
    ok(T.falasSafe('geada.WAV'));
  });
  test('falasSafe: rejects traversal', function() {
    ok(!T.falasSafe('../x.wav'));
    ok(!T.falasSafe('a/b.wav'));
  });
  test('falasSafe: rejects extension and length', function() {
    ok(!T.falasSafe('musica.mp3'));
    ok(!T.falasSafe(''));
    ok(!T.falasSafe(new Array(70).join('a') + '.wav'));
  });

  console.log('  --- DOM ---');
  test('DOM: buttons and table exist', function() {
    ok(w.document.getElementById('bFalasRef'), 'bFalasRef');
    ok(w.document.getElementById('bPeersSave'), 'bPeersSave');
    ok(w.document.getElementById('bPeersTest'), 'bPeersTest');
    ok(w.document.getElementById('falasBody'), 'falasBody');
    ok(w.document.getElementById('peersJson'), 'peersJson');
  });

  var total = passed + failed;
  console.log('\n  Falas: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
