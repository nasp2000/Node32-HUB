#!/usr/bin/env python3
"""Build desktop "smoke" harness pages for routes no live board serves.

The page is taken **exactly as the firmware compiles it**: the PROGMEM R-string from
`chatgpt/src/webpages/assets/page_<name>_asset.h`, with its own `<style>` and scripts
untouched. Only two harness-only additions are made:

1. `core.css` inlined when the asset links `/core.css` (from `file://` that request
   would 404);
2. a mock API layer + `window.__MOCK_API__` seed, so the page renders with realistic
   data instead of an empty shell (same spirit as `batch_pack_shots.py` for `/m`).

Nothing is written to any board and no firmware is involved. Output:
`tests/web/shots/desktop_<name>.html`, captured with `desktop_smoke_shot.js`.

Usage:
  python3 build_desktop_shots.py dualfile
  python3 build_desktop_shots.py --all
"""
import pathlib
import re
import sys

WEB = pathlib.Path(__file__).parent
ASSETS = (WEB / '../../chatgpt/src/webpages/assets').resolve()
SHOTS = WEB / 'shots'

# Harness API layer: same shapes the pages expect, no network at all.
MOCK_JS = r"""
<script>
(function () {
  window.__postBodies = [];
  window.fetch = function (url, opts) {
    var u = typeof url === 'string' ? url : (url && url.url ? url.url : '');
    if (opts && opts.method && opts.method !== 'GET') window.__postBodies.push({ url: u, method: opts.method });
    var MOCKS = window.__MOCK_API__ || {}, mock = MOCKS[u];
    if (mock === undefined) {
      for (var k in MOCKS) { if (u.indexOf(k) >= 0) { mock = MOCKS[k]; break; } }
    }
    if (typeof mock === 'function') mock = mock(u);
    if (mock === undefined) mock = {};
    return Promise.resolve({ ok: true, status: 200,
      json: function () { return Promise.resolve(mock); },
      text: function () { return Promise.resolve(JSON.stringify(mock)); } });
  };
  window.customAlert = function (m) { (window.__alerts = window.__alerts || []).push(String(m)); };
  window.customConfirm = function (m, cb) { if (cb) cb(true); };
  window.showLoading = function () {};
  window.hideLoading = function () {};
  window.disableBtn = function (b) { if (b) b.disabled = true; };
  window.enableBtn = function (b) { if (b) b.disabled = false; };
  window.showSavedCard = function () {};
  window._rs232Enabled = false;
  window._modbusEnabled = false;
})();
</script>
"""

DUALFILE_SEED = r"""
<script>
(function () {
  var now = Math.floor(Date.now() / 1000);
  var day = 86400;
  function listing(entries, total, used, storage) {
    return { success: true, path: '/', separator: '/', parent: null, storage: storage,
      total_kb: total, used_kb: used, free_kb: total - used, entries: entries };
  }
  var SD = listing([
    { n: 'gcode',              d: 1, s: 0,     t: now - 3600 },
    { n: 'logs',               d: 1, s: 0,     t: now - 1800 },
    { n: 'photos',             d: 1, s: 0,     t: now - 7200 },
    { n: 'reports',            d: 1, s: 0,     t: now - 2 * day },
    { n: 'job_1779_amada.nc',  d: 0, s: 48213, t: now - 2400 },
    { n: 'job_1780_parts.nc',  d: 0, s: 12044, t: now - 900 },
    { n: 'sd_test.txt',        d: 0, s: 412,   t: now - 5 * day }
  ], 30474, 1822, 'sd');
  var FLASH = listing([
    { n: 'config',             d: 1, s: 0,    t: now - 7200 },
    { n: 'charts',             d: 1, s: 0,    t: now - 600 },
    { n: 'reports',            d: 1, s: 0,    t: now - day },
    { n: 'system.json',        d: 0, s: 2048, t: now - 7200 },
    { n: 'wifi.json',          d: 0, s: 512,  t: now - day },
    { n: 'mq135_config.json',  d: 0, s: 640,  t: now - 3 * day }
  ], 4032, 1180, 'flash');
  // The page opens both panes on path=/ with storage sd (left) and flash (right).
  window.__MOCK_API__ = {
    '/api/dualfile/list?path=%2F&storage=sd': SD,
    '/api/dualfile/list?path=%2F&storage=flash': FLASH,
    '/api/dualfile/list?path=%2F&storage=psram': listing([], 4096, 0, 'psram'),
    '/api/dualfile/list': SD
  };
})();
</script>
"""

SEEDS = {
    # name: (asset file, seed script)
    'dualfile': ('page_dualfile_asset.h', DUALFILE_SEED),
}

RAW = re.compile(r'R"([A-Z0-9]+)\((.*)\)\1"', re.S)


def core_css():
    txt = (WEB / '../../chatgpt/src/webpages/core_css.h').resolve().read_text(encoding='utf-8')
    m = re.search(r'R"CSS\((.*)\)CSS"', txt, re.S)
    return m.group(1) if m else ''


def build(name):
    asset_name, seed = SEEDS[name]
    src = (ASSETS / asset_name).read_text(encoding='utf-8', errors='replace')
    m = RAW.search(src)
    if not m:
        raise SystemExit('no PROGMEM R-string in ' + asset_name)
    html = m.group(2)

    if "'/core.css'" in html or '"/core.css"' in html:
        css = core_css()
        if css and '</head>' in html:
            html = html.replace('</head>',
                                '<style>\n/* core.css inlined for the harness */\n' + css + '\n</style>\n</head>', 1)

    html = re.sub(r'(<head[^>]*>)', lambda mm: mm.group(1) + MOCK_JS + seed, html, count=1)

    SHOTS.mkdir(exist_ok=True)
    out = SHOTS / ('desktop_' + name + '.html')
    out.write_text(html, encoding='utf-8')
    print('built', out.relative_to(WEB), '|', len(html), 'bytes')
    return out


if __name__ == '__main__':
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(0)
    names = list(SEEDS) if args[0] == '--all' else args
    for n in names:
        if n not in SEEDS:
            print('no seed for', n, '- add it to SEEDS')
            sys.exit(1)
        build(n)
