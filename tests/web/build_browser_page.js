// Build a browser-testable copy of the GRBL controller page.
// Opens under file:// with a local G-code loader so the contour Select
// feature can be exercised without flashing the device:
//
//   node build_browser_page.js
//   -> open tests/web/pages/grbl_controller_browser.html in Chrome
//
// It reuses the extracted page + the same mock API the jsdom tests inject,
// then appends a small loader panel (file input + hint text).
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const PAGE = path.join(DIR, 'pages', 'grbl_controller.html');
const OUT = path.join(DIR, 'pages', 'grbl_controller_browser.html');
const MARKER = '// ===== MOCK API LAYER =====';

const fixtures = fs.readFileSync(path.join(DIR, 'mocks', 'api_fixtures.js'), 'utf8');
let html = fs.readFileSync(PAGE, 'utf8');

if (html.indexOf(MARKER) < 0) throw new Error('marker not found in extracted page; run extract.py first');

html = html.replace(MARKER, fixtures + '\n' + MARKER);

const loader = `
<script>
(function () {
  'use strict';
  var panel = document.createElement('div');
  panel.style.cssText = 'position:fixed;top:8px;right:8px;z-index:9999;max-width:300px;' +
    'background:rgba(15,23,42,0.95);color:#e2e8f0;border:1px solid #334155;border-radius:8px;' +
    'padding:10px 12px;font:12px/1.5 system-ui,sans-serif;box-shadow:0 4px 14px rgba(0,0,0,.4);';
  panel.innerHTML =
    '<div style="font-weight:700;margin-bottom:6px">Local G-code tester</div>' +
    '<input type="file" accept=".gc,.nc,.gcode,.txt" id="grbl_file_pick" style="width:100%;margin-bottom:6px">' +
    '<button id="grbl_file_load" style="width:100%;margin-bottom:8px;padding:4px;cursor:pointer">Load into preview</button>' +
    '<div style="opacity:.85">Flow: <b>Select</b> button ON &#8594; <b>click</b> a piece (gold) &#8594; ' +
    '<b>Space</b> toggles OFF (gray + red X). <b>Tab</b> moves to the next piece.</div>';
  document.body.appendChild(panel);

  var input = document.getElementById('grbl_file_pick');
  var load = document.getElementById('grbl_file_load');
  load.addEventListener('click', function () {
    var f = input.files && input.files[0];
    if (!f) { alert('Choose a .gc file first.'); return; }
    var reader = new FileReader();
    reader.onload = function () {
      var text = String(reader.result || '');
      var T = window.__test__;
      if (T && typeof T.renderPreviewFromText === 'function') {
        T.renderPreviewFromText(text, f.name);
      } else {
        alert('renderPreviewFromText not exposed on this build.');
      }
    };
    reader.onerror = function () { alert('Failed to read file.'); };
    reader.readAsText(f);
  });
})();
</script>
`;

html = html.replace('</body>', loader + '</body>');
fs.writeFileSync(OUT, html);
console.log('Wrote ' + OUT + ' (' + html.length + ' bytes)');
