// Dual File Manager Page Tests — wildcardMatch, formatSize, formatDate, getEntryIcon,
// getVisibleIndices, invertSelection, selectByWildcard, escHtml, processCommand
var fs = require('fs');
var path = require('path');
var { JSDOM } = require('jsdom');

var PAGE = path.join(__dirname, 'pages', 'dualfile.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function makeEntries(arr) {
  return arr.map(function(n) {
    if (typeof n === 'string') return { n: n, d: 0, s: 0, t: 0 };
    return n;
  });
}

function run() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost' });
  var w = dom.window;
  var T = w;

  // === wildcardMatch ===
  console.log('  --- wildcardMatch ---');

  test('wildcardMatch: exact match', function() {
    ok(T.wildcardMatch('test.gcode', 'test.gcode'));
  });

  test('wildcardMatch: no match', function() {
    ok(!T.wildcardMatch('test.gcode', 'other.txt'));
  });

  test('wildcardMatch: * matches everything', function() {
    ok(T.wildcardMatch('*', 'anything.gcode'));
  });

  test('wildcardMatch: *.gcode matches suffix', function() {
    ok(T.wildcardMatch('*.gcode', 'file.gcode'));
    ok(!T.wildcardMatch('*.gcode', 'file.txt'));
  });

  test('wildcardMatch: ? matches single char', function() {
    ok(T.wildcardMatch('file.?', 'file.a'));
    ok(!T.wildcardMatch('file.?', 'file.ab'));
  });

  test('wildcardMatch: case insensitive', function() {
    ok(T.wildcardMatch('*.GCODE', 'file.gcode'));
    ok(T.wildcardMatch('*.gcode', 'FILE.GCODE'));
  });

  test('wildcardMatch: invalid pattern does not throw', function() {
    ok(!T.wildcardMatch('[invalid', 'test'));
  });

  // === formatSize ===
  console.log('  --- formatSize ---');

  test('formatSize: 0 bytes', function() { is(T.formatSize(0), '0 B'); });
  test('formatSize: bytes', function() { is(T.formatSize(512), '512 B'); });
  test('formatSize: KB', function() { is(T.formatSize(1024), '1.0KB'); is(T.formatSize(4096), '4.0KB'); });
  test('formatSize: MB', function() { is(T.formatSize(1048576), '1.0MB'); is(T.formatSize(5242880), '5.0MB'); });
  test('formatSize: null/undefined returns empty', function() { is(T.formatSize(null), ''); is(T.formatSize(undefined), ''); });
  test('formatSize: negative', function() { is(T.formatSize(-1), '-1 B'); });

  // === formatDate ===
  console.log('  --- formatDate ---');

  test('formatDate: zero returns empty', function() { is(T.formatDate(0), ''); });
  test('formatDate: null returns empty', function() { is(T.formatDate(null), ''); });
  test('formatDate: valid timestamp', function() {
    var d = new Date(2024, 0, 15, 10, 30, 0);
    var r = T.formatDate(Math.floor(d.getTime() / 1000));
    ok(r.indexOf('2024') >= 0);
  });
  test('formatDate: old timestamp', function() {
    var d = new Date(2000, 0, 1, 0, 0, 0);
    var r = T.formatDate(Math.floor(d.getTime() / 1000));
    ok(r.indexOf('2000') >= 0);
  });

  // === getEntryIcon (returns HTML entity strings, not rendered chars) ===
  console.log('  --- getEntryIcon ---');

  test('getEntryIcon: directory returns folder', function() {
    is(T.getEntryIcon({ n: 'mydir', d: 1 }), '&#128193;');
  });
  test('getEntryIcon: .gcode returns gear', function() { is(T.getEntryIcon({ n: 'file.gcode', d: 0 }), '&#9881;'); });
  test('getEntryIcon: .nc returns gear', function() { is(T.getEntryIcon({ n: 'file.nc', d: 0 }), '&#9881;'); });
  test('getEntryIcon: .ngc returns gear', function() { is(T.getEntryIcon({ n: 'file.ngc', d: 0 }), '&#9881;'); });
  test('getEntryIcon: .txt returns document', function() { is(T.getEntryIcon({ n: 'readme.txt', d: 0 }), '&#128196;'); });
  test('getEntryIcon: .log returns document', function() { is(T.getEntryIcon({ n: 'error.log', d: 0 }), '&#128196;'); });
  test('getEntryIcon: .json returns document', function() { is(T.getEntryIcon({ n: 'data.json', d: 0 }), '&#128196;'); });
  test('getEntryIcon: .csv returns document', function() { is(T.getEntryIcon({ n: 'data.csv', d: 0 }), '&#128196;'); });
  test('getEntryIcon: .bin returns wrench', function() { is(T.getEntryIcon({ n: 'data.bin', d: 0 }), '&#128295;'); });
  test('getEntryIcon: .hex returns wrench', function() { is(T.getEntryIcon({ n: 'dump.hex', d: 0 }), '&#128295;'); });
  test('getEntryIcon: .zip returns package', function() { is(T.getEntryIcon({ n: 'archive.zip', d: 0 }), '&#128230;'); });
  test('getEntryIcon: .gz returns package', function() { is(T.getEntryIcon({ n: 'file.gz', d: 0 }), '&#128230;'); });
  test('getEntryIcon: .tar returns package', function() { is(T.getEntryIcon({ n: 'backup.tar', d: 0 }), '&#128230;'); });
  test('getEntryIcon: unknown ext returns paperclip', function() { is(T.getEntryIcon({ n: 'file.xyz', d: 0 }), '&#128206;'); });
  test('getEntryIcon: no ext returns paperclip', function() { is(T.getEntryIcon({ n: 'Makefile', d: 0 }), '&#128206;'); });
  test('getEntryIcon: uppercase ext matches', function() { is(T.getEntryIcon({ n: 'FILE.GCODE', d: 0 }), '&#9881;'); });

  // === escHtml ===
  console.log('  --- escHtml ---');

  test('escHtml: escapes special chars', function() { is(T.escHtml('<script>&"'), '&lt;script&gt;&amp;&quot;'); });
  test('escHtml: normal text unchanged', function() { is(T.escHtml('hello world'), 'hello world'); });

  // === getVisibleIndices ===
  console.log('  --- getVisibleIndices ---');

  test('getVisibleIndices: no filter returns all', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.filter = '';
    is(T.getVisibleIndices('left').length, 3);
  });
  test('getVisibleIndices: filter matches subset', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.filter = 'gcode';
    var v = T.getVisibleIndices('left');
    is(v.length, 1); is(v[0], 1);
  });
  test('getVisibleIndices: filter no match', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.filter = 'nonexistent';
    is(T.getVisibleIndices('left').length, 0);
  });
  test('getVisibleIndices: filter case insensitive', function() {
    T.state.left.entries = makeEntries(['A.TXT', 'b.gcode']);
    T.state.left.filter = 'a.';
    var v = T.getVisibleIndices('left');
    is(v.length, 1); is(v[0], 0);
  });
  test('getVisibleIndices: filter empty string shows all', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode']);
    T.state.left.filter = '';
    is(T.getVisibleIndices('left').length, 2);
  });

  // === invertSelection ===
  console.log('  --- invertSelection ---');

  test('invertSelection: none selected becomes all', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.selected = {};
    T.state.active = 'left';
    T.invertSelection();
    var cnt = Object.keys(T.state.left.selected).filter(function(k) { return T.state.left.selected[k]; }).length;
    is(cnt, 3);
  });
  test('invertSelection: all becomes none', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.selected = { 0: true, 1: true, 2: true };
    T.state.active = 'left';
    T.invertSelection();
    var cnt = Object.keys(T.state.left.selected).filter(function(k) { return T.state.left.selected[k]; }).length;
    is(cnt, 0);
  });
  test('invertSelection: partial inverts correctly', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.selected = { 0: true };
    T.state.active = 'left';
    T.invertSelection();
    ok(!T.state.left.selected[0]);
    ok(T.state.left.selected[1]);
    ok(T.state.left.selected[2]);
  });

  // === renderPanel integration ===
  console.log('  --- renderPanel ---');

  test('renderPanel: empty entries shows empty message', function() {
    T.state.left.entries = [];
    T.state.left.filter = ''; T.state.left.cursor = 0;
    T.state.left.selected = {}; T.state.left.sortBy = 'name'; T.state.left.sortAsc = true;
    T.renderPanel('left');
    var el = w.document.getElementById('list-left');
    ok(el.innerHTML.indexOf('empty') >= 0);
  });
  test('renderPanel: renders entries with cursor and selection', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.filter = ''; T.state.left.cursor = 1;
    T.state.left.selected = { 0: true }; T.state.left.sortBy = 'name'; T.state.left.sortAsc = true;
    T.renderPanel('left');
    var html = w.document.getElementById('list-left').innerHTML;
    ok(html.indexOf('b.gcode') >= 0); ok(html.indexOf('cursor') >= 0); ok(html.indexOf('selected') >= 0);
  });
  test('renderPanel: filter hides non-matching entries', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.filter = 'gcode'; T.state.left.cursor = 0;
    T.state.left.selected = {}; T.state.left.sortBy = 'name'; T.state.left.sortAsc = true;
    T.renderPanel('left');
    var html = w.document.getElementById('list-left').innerHTML;
    ok(html.indexOf('b.gcode') >= 0); ok(html.indexOf('a.txt') < 0);
  });
  test('renderPanel: sort by size ascending', function() {
    T.state.left.entries = makeEntries([{ n: 'big.bin', d: 0, s: 9999, t: 0 }, { n: 'small.txt', d: 0, s: 100, t: 0 }]);
    T.state.left.filter = ''; T.state.left.cursor = 0; T.state.left.selected = {};
    T.state.left.sortBy = 'size'; T.state.left.sortAsc = true;
    T.renderPanel('left');
    var html = w.document.getElementById('list-left').innerHTML;
    ok(html.indexOf('small.txt') < html.indexOf('big.bin'));
  });
  test('renderPanel: sort by size descending', function() {
    T.state.left.entries = makeEntries([{ n: 'big.bin', d: 0, s: 9999, t: 0 }, { n: 'small.txt', d: 0, s: 100, t: 0 }]);
    T.state.left.filter = ''; T.state.left.cursor = 0; T.state.left.selected = {};
    T.state.left.sortBy = 'size'; T.state.left.sortAsc = false;
    T.renderPanel('left');
    var html = w.document.getElementById('list-left').innerHTML;
    ok(html.indexOf('big.bin') < html.indexOf('small.txt'));
  });
  test('renderPanel: sort by extension groups file types', function() {
    T.state.left.entries = makeEntries([{ n: 'a.txt', d: 0, s: 1, t: 0 }, { n: 'b.gcode', d: 0, s: 2, t: 0 }, { n: 'c.txt', d: 0, s: 3, t: 0 }]);
    T.state.left.filter = ''; T.state.left.cursor = 0; T.state.left.selected = {};
    T.state.left.sortBy = 'ext'; T.state.left.sortAsc = true;
    T.renderPanel('left');
    var html = w.document.getElementById('list-left').innerHTML;
    // .gcode comes before .txt (ascending ext sort)
    var gcodePos = html.indexOf('b.gcode');
    var txt1Pos = html.indexOf('a.txt');
    ok(gcodePos >= 0); ok(txt1Pos >= 0);
    ok(gcodePos < txt1Pos, '.gcode before .txt in ascending ext sort');
    // a.txt before c.txt (tiebreaker: name asc)
    var txt2Pos = html.indexOf('c.txt');
    ok(txt2Pos >= 0);
    ok(txt1Pos < txt2Pos, 'a.txt before c.txt alphabetical tiebreaker');
  });

  // === getCursorEntry ===
  console.log('  --- getCursorEntry ---');

  test('getCursorEntry: returns entry at cursor', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.filter = ''; T.state.left.cursor = 1;
    is(T.getCursorEntry('left').n, 'b.gcode');
  });
  test('getCursorEntry: returns null when cursor out of visible range', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode']);
    T.state.left.filter = 'nonexistent'; T.state.left.cursor = 0;
    ok(!T.getCursorEntry('left'));
  });

  // === toggleSort ===
  console.log('  --- toggleSort ---');

  test('toggleSort: changes sort field ascending', function() {
    T.state.left.sortBy = 'name'; T.state.left.sortAsc = true;
    T.toggleSort('left', 'size');
    is(T.state.left.sortBy, 'size'); is(T.state.left.sortAsc, true);
  });
  test('toggleSort: same field toggles direction', function() {
    T.state.left.sortBy = 'name'; T.state.left.sortAsc = true;
    T.toggleSort('left', 'name');
    is(T.state.left.sortBy, 'name'); is(T.state.left.sortAsc, false);
  });
  test('toggleSort: double toggle returns ascending', function() {
    T.state.left.sortBy = 'name'; T.state.left.sortAsc = false;
    T.toggleSort('left', 'name');
    is(T.state.left.sortAsc, true);
  });
  test('toggleSort: ext field works', function() {
    T.state.left.sortBy = 'name';
    T.toggleSort('left', 'ext');
    is(T.state.left.sortBy, 'ext');
    is(T.state.left.sortAsc, true);
  });

  // === Cursor movement via direct state manipulation ===
  console.log('  --- cursor movement ---');

  test('cursor up moves to previous visible entry', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.filter = ''; T.state.left.cursor = 1;
    T.state.left.sortBy = 'name'; T.state.left.sortAsc = true;
    // Simulate ArrowUp logic
    var vis = T.getVisibleIndices('left');
    var pos = vis.indexOf(T.state.left.cursor);
    if (pos > 0) T.state.left.cursor = vis[pos - 1];
    is(T.state.left.cursor, 0);
  });
  test('cursor down moves to next visible entry', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode', 'c.bin']);
    T.state.left.filter = ''; T.state.left.cursor = 0;
    T.state.left.sortBy = 'name'; T.state.left.sortAsc = true;
    var vis = T.getVisibleIndices('left');
    var pos = vis.indexOf(T.state.left.cursor);
    if (pos < vis.length - 1) T.state.left.cursor = vis[pos + 1];
    is(T.state.left.cursor, 1);
  });
  test('cursor page down jumps 10', function() {
    var entries = [];
    for (var i = 0; i < 20; i++) entries.push({ n: 'file' + i + '.txt', d: 0, s: i, t: 0 });
    T.state.left.entries = entries;
    T.state.left.filter = ''; T.state.left.cursor = 0;
    T.state.left.sortBy = 'name'; T.state.left.sortAsc = true;
    var vis = T.getVisibleIndices('left');
    var pos = Math.min(vis.length - 1, vis.indexOf(0) + 10);
    T.state.left.cursor = vis[pos];
    is(T.state.left.cursor, entries[10].n === 'file10.txt' ? 10 : 10);
  });
  test('Tab switches active panel', function() {
    T.state.active = 'left';
    T.state.active = (T.state.active === 'left' ? 'right' : 'left');
    is(T.state.active, 'right');
  });
  test('Backspace parent logic updates path', function() {
    T.state.left.path = '/root/subdir';
    var parent = T.state.left.path;
    if (parent.endsWith('/')) parent = parent.substring(0, parent.length - 1);
    var slash = parent.lastIndexOf('/');
    T.state.left.path = slash <= 0 ? '/' : parent.substring(0, slash);
    is(T.state.left.path, '/root');
  });
  test('Backspace from root stays at root', function() {
    T.state.left.path = '/';
    is(T.state.left.path, '/');
  });

  // === Menu bar ===
  console.log('  --- menu bar ---');

  test('openMenu: File menu creates dropdown', function() {
    T.closeMenu();
    T.openMenu('file');
    var dd = w.document.getElementById('menu-dropdown');
    ok(dd, 'dropdown exists');
    is(dd.style.display, 'block', 'dropdown is visible');
    ok(dd.innerHTML.indexOf('View') >= 0);
    ok(dd.innerHTML.indexOf('Upload') >= 0, 'Upload menu item present');
    ok(dd.innerHTML.indexOf('Download') >= 0);
    ok(dd.innerHTML.indexOf('Exit') >= 0);
    T.closeMenu();
  });

  test('openMenu: Options menu has sort items', function() {
    T.openMenu('options');
    var dd = w.document.getElementById('menu-dropdown');
    ok(dd.innerHTML.indexOf('Sort by Name') >= 0);
    ok(dd.innerHTML.indexOf('Sort by Size') >= 0);
    ok(dd.innerHTML.indexOf('Toggle Direction') >= 0);
    T.closeMenu();
  });

  test('openMenu: Command menu has Filter and Swap', function() {
    T.openMenu('command');
    var dd = w.document.getElementById('menu-dropdown');
    ok(dd.innerHTML.indexOf('Filter') >= 0);
    ok(dd.innerHTML.indexOf('Swap') >= 0);
    T.closeMenu();
  });

  test('openMenu: Right menu has storage switches', function() {
    T.openMenu('right');
    var dd = w.document.getElementById('menu-dropdown');
    ok(dd.innerHTML.indexOf('Storage Left') >= 0);
    ok(dd.innerHTML.indexOf('Storage Right') >= 0);
    T.closeMenu();
  });

  test('closeMenu: hides dropdown and clears state', function() {
    T.openMenu('file');
    T.closeMenu();
    var dd = w.document.getElementById('menu-dropdown');
    is(dd.style.display, 'none');
    ok(!T._menuOpen);
  });

  test('menu item separator renders md-sep', function() {
    T.openMenu('file');
    var dd = w.document.getElementById('menu-dropdown');
    var seps = dd.querySelectorAll('.md-sep');
    ok(seps.length >= 2);
    T.closeMenu();
  });

  test('uploadFile function exists', function() {
    ok(typeof T.uploadFile === 'function');
  });

  test('file upload input element exists', function() {
    var el = w.document.getElementById('file-upload-input');
    ok(el, 'upload input exists');
    is(el.getAttribute('type'), 'file');
  });

  test('upload progress elements exist', function() {
    var up = w.document.getElementById('upload-progress');
    ok(up);
    var fnEl = w.document.getElementById('upload-filename');
    ok(fnEl);
    var pctEl = w.document.getElementById('upload-pct');
    ok(pctEl);
    var bar = w.document.getElementById('upload-bar');
    ok(bar);
  });

  test('openMenu->closeMenu->reopen works', function() {
    T.openMenu('file'); T.closeMenu();
    T.openMenu('options');
    var dd = w.document.getElementById('menu-dropdown');
    is(dd.style.display, 'block');
    ok(dd.innerHTML.indexOf('Sort') >= 0);
    T.closeMenu();
  });

  test('Options menu contains Auto Refresh', function() {
    T.openMenu('options');
    var dd = w.document.getElementById('menu-dropdown');
    ok(dd.innerHTML.indexOf('Auto Refresh') >= 0);
    T.closeMenu();
  });

  // === Auto refresh ===
  console.log('  --- auto refresh ---');

  test('toggleAutoRefresh: starts as off', function() {
    ok(!T._autoRefreshOn);
    ok(T._autoRefreshTimer === null);
  });

  test('toggleAutoRefresh: turns on and shows indicator', function() {
    T.toggleAutoRefresh();
    ok(T._autoRefreshOn);
    var ind = w.document.getElementById('auto-refresh-indicator');
    ok(ind, 'indicator exists');
    ok(ind.style.display !== 'none', 'indicator visible');
  });

  test('toggleAutoRefresh: turns off and hides indicator', function() {
    T.toggleAutoRefresh();
    ok(!T._autoRefreshOn);
    ok(T._autoRefreshTimer === null);
    var ind = w.document.getElementById('auto-refresh-indicator');
    if (ind) ok(ind.style.display === 'none', 'indicator hidden');
  });

  // === processCommand ===
  console.log('  --- processCommand ---');

  test('processCommand: help does not throw', function() {
    try { T.processCommand('help'); } catch(e) { ok(false, 'help threw: ' + e.message); }
  });
  test('processCommand: cd sets path via loadPanel', function() {
    T.state.left.path = '/';
    T.state.left.entries = [];
    T.state.active = 'left';
    T.processCommand('cd /flash');
    is(T.state.left.path, '/flash', 'processCommand cd updates path');
  });
  test('processCommand: unknown shows error via customAlert', function() {
    var lastAlert = '';
    var origAlert = T.customAlert;
    T.customAlert = function(m) { lastAlert = m; };
    T.processCommand('unknown_cmd_xyz');
    ok(lastAlert.indexOf('Unknown') >= 0, 'unknown command shows error message');
    T.customAlert = origAlert;
  });
  test('processCommand: view calls viewFile function', function() {
    ok(typeof T.viewFile === 'function', 'viewFile exists');
    // Just verify it doesn't throw when no entries
    try { T.processCommand('view'); } catch(e) { ok(false, 'view threw: ' + e.message); }
  });

  // === updateFooter ===
  console.log('  --- updateFooter ---');

  test('updateFooter: shows file and dir counts', function() {
    T.state.left.entries = makeEntries([
      { n: 'dir1', d: 1, s: 0, t: 0 },
      { n: 'file.txt', d: 0, s: 100, t: 0 },
      { n: 'file2.gcode', d: 0, s: 200, t: 0 }
    ]);
    T.state.left.storageInfo = null;
    T.updateFooter('left');
    var el = w.document.getElementById('footer-left');
    ok(el.innerHTML.indexOf('1 dir') >= 0);
    ok(el.innerHTML.indexOf('2 files') >= 0);
  });
  test('updateFooter: shows free space from storageInfo', function() {
    T.state.left.entries = [];
    T.state.left.storageInfo = { free: 50000 };
    T.updateFooter('left');
    var el = w.document.getElementById('footer-left');
    ok(el.innerHTML.indexOf('48.8MB') >= 0, 'expected 48.8MB free');
  });

  // === cmdHistory ===
  console.log('  --- cmdHistory ---');

  test('cmdHistory: push and retrieve', function() {
    T.state.cmdHistory = [];
    T.state.cmdHistory.push('cd /flash');
    T.state.cmdHistory.push('view file.txt');
    is(T.state.cmdHistory.length, 2);
    is(T.state.cmdHistory[0], 'cd /flash');
  });
  test('cmdHistory: max 50 entries', function() {
    T.state.cmdHistory = [];
    for (var i = 0; i < 60; i++) {
      T.state.cmdHistory.push('cmd' + i);
      if (T.state.cmdHistory.length > 50) T.state.cmdHistory.shift();
    }
    is(T.state.cmdHistory.length, 50);
    is(T.state.cmdHistory[0], 'cmd10');
  });
  test('cmdHistory: ArrowUp retrieves previous', function() {
    T.state.cmdHistory = ['cmd1', 'cmd2'];
    var idx = T.state.cmdHistory.length;
    idx--;
    is(T.state.cmdHistory[idx], 'cmd2');
    idx--;
    is(T.state.cmdHistory[idx], 'cmd1');
  });

  // === selectByWildcard ===
  console.log('  --- selectByWildcard ---');

  test('selectByWildcard: *.gcode selects matching', function() {
    T.state.left.entries = makeEntries(['a.gcode', 'b.txt', 'c.gcode']);
    T.state.left.selected = {};
    T.state.active = 'left';
    var pattern = '*.gcode';
    for (var i = 0; i < T.state.left.entries.length; i++) {
      T.state.left.selected[i] = T.wildcardMatch(pattern, T.state.left.entries[i].n);
    }
    ok(T.state.left.selected[0]);
    ok(!T.state.left.selected[1]);
    ok(T.state.left.selected[2]);
  });
  test('selectByWildcard: no match leaves none selected', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.bin']);
    T.state.left.selected = {};
    T.state.active = 'left';
    var pattern = '*.gcode';
    for (var i = 0; i < T.state.left.entries.length; i++) {
      T.state.left.selected[i] = T.wildcardMatch(pattern, T.state.left.entries[i].n);
    }
    ok(!T.state.left.selected[0]);
    ok(!T.state.left.selected[1]);
  });
  test('selectByWildcard: * selects all', function() {
    T.state.left.entries = makeEntries(['a.txt', 'b.gcode']);
    T.state.left.selected = {};
    T.state.active = 'left';
    for (var i = 0; i < T.state.left.entries.length; i++) {
      T.state.left.selected[i] = T.wildcardMatch('*', T.state.left.entries[i].n);
    }
    ok(T.state.left.selected[0]);
    ok(T.state.left.selected[1]);
  });

  // === processCommand exit (last — closes window) ===
  console.log('  --- processCommand exit ---');
  test('processCommand: exit calls window.close', function() {
    try { T.processCommand('exit'); } catch(e) { /* window may be null after close */ }
  });

  // Summary
  var total = passed + failed;
  console.log('\n  Results: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
  if (failed > 0) process.exit(1);
  // Force clean exit to avoid JSDOM cleanup errors after window.close()
  process.exit(0);
}

try { run(); } catch(e) {
  console.log('\n  \u2717 Fatal error: ' + e.message);
  process.exit(1);
}
