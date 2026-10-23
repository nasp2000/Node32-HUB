var fs = require('fs');
var path = require('path');
var jsdom = require('jsdom');
var { JSDOM } = jsdom;

var html = fs.readFileSync(path.join(__dirname, 'pages', 'ftp.html'), 'utf8').toString();
var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
var win = dom.window;
var doc = win.document;

var passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; } else { console.log('  FAIL:', msg); failed++; }
}
function assertEq(actual, expected, msg) {
  if (actual === expected) { passed++; }
  else { console.log('  FAIL:', msg, '- expected:', JSON.stringify(expected), 'got:', JSON.stringify(actual)); failed++; }
}

console.log('\n=== FTP Page Tests ===\n');

// --- ftpText helper: fallback, key, cache (2+ variations) ---
assertEq(typeof win.ftpText, 'function', 'ftpText is a function');
win.localStorage.removeItem('ui_lang_cache');
assertEq(win.ftpText('nonexistent', 'fb'), 'fb', 'ftpText returns fallback');
assertEq(win.ftpText('nonexistent'), 'nonexistent', 'ftpText returns key when no fallback');
win.localStorage.setItem('ui_lang_cache', JSON.stringify({ ftpTestBtn: 'Testar' }));
assertEq(win.ftpText('ftpTestBtn', 'fb'), 'Testar', 'ftpText reads from cache');
win.localStorage.removeItem('ui_lang_cache');

// --- interval selector: saved minutes map to the nearest fixed option ---
assertEq(win.ftpNearestHourMins(60), 5, '60 min -> 5H');
assertEq(win.ftpNearestHourMins(120), 5, '120 min -> 5H (nearest option)');
assertEq(win.ftpNearestHourMins(300), 5, '300 min -> 5H');
assertEq(win.ftpNearestHourMins(660), 12, '660 min -> 12H');
assertEq(win.ftpNearestHourMins(780), 12, '780 min -> 12H');
assertEq(win.ftpNearestHourMins(1380), 24, '1380 min -> 24H');
assertEq(win.ftpNearestHourMins(1500), 24, '1500 min -> 24H');
assertEq(win.ftpNearestHourMins(2100), 36, '2100 min -> 36H');
assertEq(win.ftpNearestHourMins(2160), 36, '2160 min -> 36H');

// --- required inputs exist ---
['ftp_host', 'ftp_port', 'ftp_user', 'ftp_pass', 'ftp_anonymous',
 'ftp_remote_path', 'ftp_local_dir', 'ftp_enabled', 'ftp_auto_upload',
 'ftp_interval_hours', 'ftp_only_new', 'ftp_delete_after', 'ftp_max_files',
 'ftp_ext_checks', 'ftp_ext_custom', 'btn-ftp-test', 'btn-ftp-now', 'ftp-status',
 'ftp-config-form'].forEach(function(id) {
  assert(!!doc.getElementById(id), 'element #' + id + ' exists');
});

// --- load mapping: GET /api/ftp-status fills fields ---
if (!win.__MOCK_API__) win.__MOCK_API__ = {};
win.__MOCK_API__['/api/ftp-folders'] = [
  'SD:/', 'SD:/logs', 'SD:/CAM',
  'PSRAM:/',
  'LFS:/', 'LFS:/config'
];
win.__MOCK_API__['/api/ftp-status'] = {
  host: '192.168.1.10', port: 2121, user: 'u1', anonymous: false,
  remote_path: '/up', local_dir: 'SD:/logs', interval_min: 120, max_files: 5,
  filter: '*.csv', enabled: true, auto_upload: true, only_new: true,
  delete_after: false, source: 'SD', last_file: 'a.csv',
  last_result: 'OK', ok_count: 3, fail_count: 1,
  // persisted summary (new): survives reboot on the device
  last_time_str: '23-10-2026 10:01:05',
  recent: [
    { file: 'a.csv', size: 2048, ok: true, time_str: '23-10-2026 10:01:05' },
    { file: 'b.csv', size: 10, ok: false, time_str: '23-10-2026 09:00:00' }
  ]
};
win.ftpLoad();
setTimeout(function() {
  assertEq(doc.getElementById('ftp_host').value, '192.168.1.10', 'host filled from status');
  assertEq(doc.getElementById('ftp_port').value, '2121', 'port filled from status');
  assertEq(doc.getElementById('ftp_remote_path').value, '/up', 'remote path filled');
  assertEq(doc.getElementById('ftp_interval_hours').value, '5', 'interval_min 120 -> nearest fixed option 5H');
  assert(doc.getElementById('ftp_enabled').checked === true, 'enabled checkbox set');
  assert(doc.getElementById('ftp_auto_upload').checked === true, 'auto checkbox set');
  var csvBox = doc.querySelector('#ftp_ext_checks input[value=".csv"]');
  var logBox = doc.querySelector('#ftp_ext_checks input[value=".log"]');
   assert(!!csvBox && csvBox.checked === true, ".csv checked for filter '*.csv'");
   assert(!!logBox && logBox.checked === false, ".log unchecked for filter '*.csv'");
   // --- token-exact matching: "*.inc" must NOT check the ".nc" box ---
   win.ftpSetFtpExtChecks('*.inc');
   var ncBox = doc.querySelector('#ftp_ext_checks input[value=".nc"]');
   assert(!!ncBox && ncBox.checked === false, '".nc" unchecked for filter "*.inc"');
   assertEq(doc.getElementById('ftp_ext_custom').value, '.inc', 'custom gets ".inc"');
   win.ftpSetFtpExtChecks('*.csv; *.log');
   assert(doc.querySelector('#ftp_ext_checks input[value=".csv"]').checked === true, 'spaced tokens still match .csv');
   assert(doc.querySelector('#ftp_ext_checks input[value=".log"]').checked === true, 'spaced tokens still match .log');
   win.ftpSetFtpExtChecks('*.csv');
  var st = doc.getElementById('ftp-status').textContent;
  assert(st.indexOf('a.csv') >= 0, 'status shows last file');
  // --- persisted summary + recent list (new) ---
  assert(st.indexOf('23-10-2026 10:01:05') >= 0, 'status shows persisted last cycle time');
  assert(st.indexOf('Last cycle') >= 0, 'status labels the last cycle');
  var rows = doc.querySelectorAll('#ftp-recent-body tr');
  assertEq(rows.length, 2, 'recent list renders both uploads');
  assertEq(rows[0].children[0].textContent, 'a.csv', 'newest upload first');
  assertEq(rows[0].children[1].textContent, '2 KB', 'size rendered in KB');
  assertEq(rows[0].children[2].textContent, '23-10-2026 10:01:05', 'upload time rendered');
  assertEq(rows[0].children[3].textContent, 'OK', 'success shown as the word OK');
  assertEq(rows[1].children[3].textContent, 'FAIL', 'failure shown as the word FAIL');
  assert(!!rows[0].children[3].style.color &&
         rows[0].children[3].style.color !== rows[1].children[3].style.color,
         'result keeps distinct colours (green vs red)');
  assertEq(doc.getElementById('ftp-recent-empty').style.display, 'none', 'empty hint hidden when there are rows');
  assertEq(win.ftpFmtSize(500), '500 B', 'ftpFmtSize bytes');
  assertEq(win.ftpFmtSize(1572864), '1.5 MB', 'ftpFmtSize megabytes');
  assertEq(win.ftpFmtSize(undefined), '0 B', 'ftpFmtSize undefined is 0 B');
  // --- local folder dropdown: prefixed values grouped per filesystem, no custom ---
  var sel = doc.getElementById('ftp_local_dir');
  assertEq(sel.value, 'SD:/logs', 'dropdown selects prefixed local_dir');
  var hasCustom = false, hasOptgroups = false, seenGroup = false;
  for (var oi = 0; oi < sel.options.length; oi++) {
    if (String(sel.options[oi].value).indexOf('__custom__') >= 0) hasCustom = true;
    if (sel.options[oi].value === 'SD:/') seenGroup = true;
  }
  for (var gi = 0; gi < sel.children.length; gi++) {
    if (sel.children[gi].tagName === 'OPTGROUP') hasOptgroups = true;
  }
  assert(!sel.value.match(/^\//), 'value is prefixed (not bare path)');
  assert(!hasCustom, 'no custom-path option present');
  assert(hasOptgroups, 'options grouped by filesystem');
  assert(seenGroup, 'SD root present');

  // --- second variation: anonymous + PSRAM source, legacy bare local_dir ---
  win.__MOCK_API__['/api/ftp-status'] = {
    host: 'srv', port: 21, user: '', anonymous: true,
    remote_path: '/', local_dir: '/logs', interval_min: 60, max_files: 10,
    filter: '*.csv;*.log', enabled: false, auto_upload: false,
    only_new: false, delete_after: true, source: 'PSRAM',
    last_file: '-', last_result: '-', ok_count: 0, fail_count: 0,
    last_time_str: '-', recent: []
  };
  win.ftpLoad();
  setTimeout(function() {
    assert(doc.getElementById('ftp_anonymous').checked === true, 'anonymous checkbox set');
    assert(doc.getElementById('ftp_enabled').checked === false, 'enabled unchecked');
    assert(doc.getElementById('ftp_only_new').checked === false, 'only_new unchecked');
    assert(doc.getElementById('ftp-status').textContent.indexOf('PSRAM') >= 0, 'status shows PSRAM source');
    // --- empty persisted list: readable "nothing yet" state, no rows ---
    assertEq(doc.querySelectorAll('#ftp-recent-body tr').length, 0, 'no recent rows when list empty');
    assertEq(doc.getElementById('ftp-recent-empty').style.display, 'block', 'empty hint shown');
    assert(doc.getElementById('ftp-status').textContent.indexOf('-') >= 0, 'empty last cycle renders as dash');
    // legacy "/logs" not in options → re-selected as first option
    assertEq(doc.getElementById('ftp_local_dir').value, '/logs', 'legacy bare path preserved');

    // --- test connection button: success then failure ---
    var alerts = [];
    var orig = win.customAlert;
    win.customAlert = function(m) { alerts.push(m); };
    win.__MOCK_API__['/api/ftp-test'] = { success: true, message: 'OK control+login+PASV' };
    doc.getElementById('btn-ftp-test').click();
    setTimeout(function() {
      assert(alerts.some(function(m) { return m.indexOf('OK control') >= 0; }), 'test button shows success');
      win.__MOCK_API__['/api/ftp-test'] = { success: false, message: 'login failed: 530' };
      doc.getElementById('btn-ftp-test').click();
      setTimeout(function() {
        assert(alerts.some(function(m) { return m.indexOf('530') >= 0; }), 'test button shows failure');
        // --- upload now button ---
        win.__MOCK_API__['/api/ftp-upload-now'] = { success: true, uploaded: 2, message: 'cycle done: 2 file(s)' };
        doc.getElementById('btn-ftp-now').click();
        setTimeout(function() {
          assert(alerts.some(function(m) { return m.indexOf('2 file') >= 0; }), 'upload-now shows count');
          win.customAlert = orig;
          dom.window.close();
          console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed\n');
          if (failed > 0) process.exit(1);
        }, 80);
      }, 80);
    }, 80);
  }, 80);
}, 80);
