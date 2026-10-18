// Networks Page Tests — card presence + JS backend gating + route guards
// (mirrors page_networks_asset.cpp, web_routes_networks.cpp,
// home_page_renderer.h, ethernet_bootstrap_routes.h)
var fs = require('fs');
var path = require('path');

var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function src(rel) {
  return fs.readFileSync(path.join(__dirname, '..', '..', 'chatgpt', 'src', rel), 'utf8');
}

function run() {
  console.log('  --- /networks cards (page_networks_asset.cpp) ---');
  test('telnet card present with stable id', function() {
    var asset = src('webpages/assets/page_networks_asset.cpp');
    ok(asset.indexOf("id='netTelnetCard'") >= 0, 'netTelnetCard present');
  });
  test('telnet card visible by default, hidden by JS when backend missing', function() {
    var asset = src('webpages/assets/page_networks_asset.cpp');
    var m = asset.match(/<div[^>]*id='netTelnetCard'[^>]*>/);
    ok(m, 'telnet card div found');
    ok(m[0].indexOf('display:none') < 0, 'telnet card has no inline hide: ' + m[0]);
    ok(asset.indexOf("tc.style.display = (d && d.enabled !== undefined) ? '' : 'none'") >= 0,
      'telnet card gated on d.enabled');
  });
  test('modbus card removed from /networks (moved to home, below FTP)', function() {
    var asset = src('webpages/assets/page_networks_asset.cpp');
    ok(asset.indexOf('netModbusCard') < 0, 'no modbus card in the networks page');
    ok(asset.indexOf('/api/modbus/config') < 0, 'no modbus probe left in page JS');
  });

  console.log('  --- route guards (page exists when either card can show) ---');
  test('WiFi /networks route stays Telnet-only (modbus moved to home)', function() {
    var routes = src('webpages/routes/web_routes_networks.cpp');
    ok(routes.indexOf('#if ENABLE_TELNET==1 || ENABLE_MODBUS_TCP==1') < 0, 'widened guard reverted');
    ok(routes.indexOf('#if ENABLE_TELNET==1') >= 0, 'Telnet-only guard back');
  });
  test('Ethernet /networks route follows the same Telnet-only guard', function() {
    var eth = src('ethernet_bootstrap_routes.h');
    ok(eth.indexOf('#if ENABLE_TELNET==1 || ENABLE_MODBUS_TCP==1') < 0, 'ethernet widened guard reverted');
    var m = eth.match(/path == F\(\"\/networks\"\)[\s\S]{0,200}/);
    ok(m && m[0].indexOf('#if ENABLE_TELNET==1') >= 0, 'ethernet networks route gated on Telnet alone');
  });
  test('home Outras Redes button back to Telnet-only guard', function() {
    var home = src('webpages/renderers/home_page_renderer.h');
    ok(home.indexOf('#if ENABLE_TELNET==1 || ENABLE_MODBUS_TCP==1') < 0, 'widened guard reverted');
  });
  test('modbus-tcp button on home Networks section, right after FTP', function() {
    var home = src('webpages/renderers/home_page_renderer.h');
    ok(home.indexOf("out.print(F(\"<a href='/ftp' class='btn' data-i18n='homeFtp'>&#128229; FTP Upload</a>\"));") >= 0, 'FTP button present');
    ok(home.indexOf("href='/modbus-tcp'") >= 0, 'modbus-tcp button present on home');
    var ftpPos = home.indexOf("href='/ftp'");
    var mbPos = home.indexOf("href='/modbus-tcp'");
    ok(mbPos > ftpPos, 'modbus button comes after the FTP button');
    ok(home.indexOf("#if ENABLE_MODBUS_TCP==1\n                out.print(F(\"<a href='/modbus-tcp'") >= 0,
      'button gated on ENABLE_MODBUS_TCP');
  });
  test('selftest/test manifests expect /networks under ENABLE_TELNET only', function() {
    var man = src('webpages/handlers_unified/test_manifest_handler.cpp');
    var self = src('webpages/handlers_unified/selftest_handler.cpp');
    ok(man.indexOf('PAGE("/networks",            ENABLE_TELNET)') >= 0, 'manifest back to Telnet');
    ok(self.indexOf('PAGE("/networks",            ENABLE_TELNET)') >= 0, 'selftest back to Telnet');
  });

  console.log('  --- i18n keys (PT/EN in sync) ---');
  test('netModbus i18n keys removed from both languages', function() {
    var pt = src('languages/lang_pt_ui.h');
    var en = src('languages/lang_en_ui.h');
    ok(pt.indexOf('"netModbusTitle"') < 0, 'PT clean');
    ok(en.indexOf('"netModbusTitle"') < 0, 'EN clean');
  });

  var total = passed + failed;
  console.log('\n  Networks: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
