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
  // core.css hides [data-i18n] until auto.js flips this flag; without auto.js the
  // page would render as empty boxes, so the harness marks the pack as applied
  // (the strings stay the asset's own fallbacks — there is no /api/lang offline).
  document.documentElement.setAttribute('data-i18n-ready', '1');
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

NETWORKS_SEED = r"""
<script>
(function () {
  // The page only exists where ENABLE_TELNET=1 (the route is inside that guard), so the
  // telnet card is the whole page: answer the status probe the way that build does.
  window.__MOCK_API__ = {
    '/api/telnet-status': { enabled: true },
    '/api/telnet-config': { success: true, message: 'Configuration saved!' }
  };
})();
</script>
"""

STRUCTURES_SEED = r"""
<script>
(function () {
  // /api/config?section=system carries the AMADA block the page reads, plus the
  // rs232_* summary its diagnostic card prints.
  window.__MOCK_API__ = {
    '/api/config?section=system': {
      structures: {
        auto_detect: true,
        amada_isq20_mtf1: { enabled: false },
        amada_isq20_awse: { enabled: true },
        amada_filter_headers: true,
        amada_header_every: 20
      },
      saved_configs: {
        rs232_enabled: true,
        rs232_baudrate_list: [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200],
        rs232_baudrate_index: 4,
        rs232_data_bits: 8,
        rs232_parity: 'none',
        rs232_stop_bits: 1
      }
    },
    '/api/rs232-cols': { cols: 12, structure: 'amada_isq20_awse' }
  };
})();
</script>
"""

BUZZER_SEED = r"""
<script>
(function () {
  // GET /api/buzzer-config: master enable + GPIO + the six event toggles.
  window.__MOCK_API__ = {
    '/api/buzzer-config': { enabled: true, pin: 46, on_boot: true, on_save: false,
      on_alarm: true, on_sensor_alert: false, on_network: false, on_error: true },
    '/api/buzzer-test': { success: true },
    '/api/buzzer-stop': { success: true }
  };
})();
</script>
"""

LPT_DB25_SEED = r"""
<script>
(function () {
  // GET /api/config?section=system now carries the printer block (LPT-1 fix), the
  // e-mail block and both auto-send timeouts; values chosen non-default so the
  // capture shows the page actually loaded its state.
  window.__MOCK_API__ = {
    '/api/config?section=system': {
      printer_emulation_mode: 1,          // Epson ESC/P (Experimental)
      charset_mode: 1,                    // Latin-1 (Experimental)
      email: { auto_send: true, max_lines: 180, min_content_length: 3 },
      autoEmailEnabledTimeout: 20000,     // 20 s
      autoEmailDisabledTimeout: 600000    // 10 min
    }
  };
})();
</script>
"""

GRBL_SEED = r"""
<script>
(function () {
  // Same shape the firmware returns from GET /api/grbl/status (polled every
  // poll_interval_ms): DRO needs mpos_*/wpos_* with the *_valid flags, the hub
  // needs usb_slots, the console needs console_events.
  window.__MOCK_API__ = {
    '/api/grbl/status': {
      success: true,
      state: 'Idle', grbl_status: 'Idle', status_raw: '<Idle|MPos:12.340,45.670,-1.250|FS:0,0>',
      axis_count: 3, active_wcs: 'G54',
      mpos_valid: true, wpos_valid: true,
      mpos_x: 12.340, mpos_y: 45.670, mpos_z: -1.250,
      wpos_x: 12.340, wpos_y: 45.670, wpos_z: -1.250,
      machine_x: 12.340, machine_y: 45.670, machine_z: -1.250,
      work_x: 12.340, work_y: 45.670, work_z: -1.250,
      max_travel_x: 400, max_travel_y: 300, max_travel_z: 120,
      poll_interval_ms: 800, control_mode: 'free',
      machine_status_known: true, probe_triggered: false, poll_health: 'ok',
      job_active: false, workflow_state: 'idle', alarm_active: false, hold_reason: '',
      usb_running: true, usb_connected: 1, usb_backend: 'host', grbl_transport: 'uart',
      control_can_write: true,
      usb_slots: [{ slot: 1, chip: 'CH340', baud: 115200, connected: true }],
      console_events: []
    },
    '/api/grbl/transport': { success: true, message: 'ok', grbl_transport: 'uart' },
    '/api/grbl/control-token': { token: 'ctrl-token' },
    '/api/grbl/list-gcode': { success: true, path: '/', entries: [] },
    '/api/grbl/poll-interval': { success: true }
  };
})();
</script>
"""

MODBUS_RTU_SEED = r"""
<script>
(function () {
  // GET /api/modbus/config — the RTU side of the shared Modbus config. Two devices
  // (only the !isTcp ones are rendered here) with a few registers each, all values
  // in the shape mbCollectForm() writes back.
  function reg(addr, fc, type, order, label, unit, scale) {
    return { addr: addr, fc: fc, type: type, order: order || 0, bit: 0,
      label: label, unit: unit, scale: scale || 1, offset: 0, log: true, pretty: true };
  }
  window.__MOCK_API__ = {
    '/api/modbus/config': {
      enabled: true, rtu_enabled: true, tcp_enabled: false,
      rtu_baud: 19200, rtu_parity: 'N', rtu_stop_bits: 1, rtu_de_pin: -1, slave_id: 1,
      devices: [
        { slaveId: 1, name: 'Inversor', enabled: true, isTcp: false, tcpHost: '', tcpPort: 502,
          pollMs: 2000, retries: 3, timeoutMs: 500, regs: [
            reg(0, 3, 0, 0, 'Tensao', 'V', 0.1),
            reg(1, 3, 0, 0, 'Corrente', 'A', 0.01),
            reg(2, 3, 2, 0, 'Frequencia', 'Hz', 0.01)
          ] },
        { slaveId: 2, name: 'Contador', enabled: true, isTcp: false, tcpHost: '', tcpPort: 502,
          pollMs: 5000, retries: 3, timeoutMs: 500, regs: [
            reg(100, 3, 2, 0, 'Energia', 'kWh', 0.1),
            reg(200, 3, 0, 0, 'Estado', '', 1)
          ] }
      ]
    },
    '/api/modbus/scan-status': { success: true, running: false, found: [] },
    '/api/modbus/scan-rtu': { success: true },
    '/api/modbus/scan-stop': { success: true }
  };
})();
</script>
"""

MODBUS_LIVE_SEED = r"""
<script>
(function () {
  // GET /api/modbus/live returns an ARRAY of devices (mbRenderLive maps it directly).
  // Keys are exactly what handleModbusLiveGet() emits: name, enabled, pollCount,
  // errorCount, regs[{label,unit,val,ok}] — it does NOT send slaveId/isTcp.
  window.__MOCK_API__ = {
    '/api/modbus/live': [
      { name: 'Inversor', enabled: true,
        pollCount: 482, errorCount: 0, regs: [
          { label: 'Tensao',    val: 229.7000, unit: 'V',  ok: true },
          { label: 'Corrente',  val: 4.3100,   unit: 'A',  ok: true },
          { label: 'Frequencia', val: 50.0200, unit: 'Hz', ok: true }
        ] },
      { name: 'Contador', enabled: true,
        pollCount: 120, errorCount: 2, regs: [
          { label: 'Energia', val: 12345.6000, unit: 'kWh', ok: true },
          { label: 'Estado',  val: null,        unit: '—',   ok: false }
        ] }
    ],
    '/api/modbus/poll-now': { success: true },
    '/api/modbus/write': { success: true }
  };
})();
</script>
"""

API_REFERENCE_FLAGS = (
    # The flag object `streamApiReferencePage()` emits, with the Waveshare 8DI8DO
    # values (pack_waveshare.h) — the pack used as the reference board everywhere in
    # PAGES/desktop/. No pack enables the page itself, so this is "how it would look
    # on that board with ENABLE_API_REFERENCE_PAGE=1".
    "{modbus:0,proximity:1,sdcard:1,mq135:0,ota:1,sniffer:0,db25:0,debug_api:0,"
    "ethernet:1,rs232:0,telnet:0,sensors:0,do_out:1,grbl:0,camera:0,uda1334a:0}"
)

# Page shell, byte-identical to streamPageShellStart() (page_render_common.h).
SHELL_HEAD = (
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'><title>{title}</title>"
    "<link rel='stylesheet' href='/core.css'></head><body>"
    "<div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;"
    "background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'>"
    "<div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;"
    "border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div>"
    "<p style='color:#e6f1ff;margin-top:15px;font-size:14px' data-i18n='loading'>Processing...</p>"
    "</div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style>"
)

PAGES = {
    # name: (asset file, seed script, page kind)
    'dualfile': ('page_dualfile_asset.h', DUALFILE_SEED, 'raw'),
    'api_reference': ('page_api_reference_asset.h', '', 'fragment'),
    'networks': ('page_networks_asset.cpp', NETWORKS_SEED, 'fragment'),
    'structures': ('page_structures_asset.cpp', STRUCTURES_SEED, 'fragment'),
    'buzzer': ('page_buzzer_asset.h', BUZZER_SEED, 'fragment'),
    'lpt_db25': ('page_lpt_db25_asset.cpp', LPT_DB25_SEED, 'fragment'),
    'grbl_controller': ('page_grbl_controller_asset.cpp', GRBL_SEED, 'built'),
    'modbus_rtu': ('page_modbus_asset.cpp', MODBUS_RTU_SEED, 'fragment'),
    'modbus_live': ('page_modbus_asset.cpp', MODBUS_LIVE_SEED, 'fragment'),
}

# Some fragments rely on a PROGMEM block that lives in the renderer, printed before the
# body — the structures tab engine is one. (file, R-string tag)
FOOTER = {
    # emulated by the RTU routes' buildPageFrame(..., extraFooterScript)
    'modbus_rtu': "<script>if(document.readyState==='loading')"
                  "{document.addEventListener('DOMContentLoaded',initModbusRtuPage);}"
                  "else{initModbusRtuPage();}</script>",
    'modbus_live': "<script>if(document.readyState==='loading')"
                   "{document.addEventListener('DOMContentLoaded',initModbusLivePage);}"
                   "else{initModbusLivePage();}</script>",
}

# Assets that hold several pages: pick the R-string that belongs to this route.
RSTRING = {
    'modbus_rtu': 'PAGE_MODBUS_RTU',
    'modbus_live': 'PAGE_MODBUS_LIVE',
}

PRELUDE = {
    'structures': ('chatgpt/src/webpages/renderers/structures_page_renderer.cpp', 'JSTABS'),
}

FRAGMENT_TITLES = {
    # same string the renderer passes to streamPageShellStart()
    'api_reference': '🔌 Referência de API',
    'networks': '🌐 Other Networks',
    'structures': '🏭 Amada Welding',
    'buzzer': '🔔 Buzzer',
    'lpt_db25': '🖨️ LPT DB25',
    'grbl_controller': '🛠️ GcodeSender',
    'modbus_rtu': 'Modbus RTU',
    'modbus_live': '📈 Modbus Live',
}

RAW = re.compile(r'R"([A-Za-z0-9]+)\((.*)\)\1"', re.S)



def assemble_parts(cpp_path):
    """Rebuild a PROGMEM page that is #include-assembled from fragments.

    `page_grbl_controller_asset.cpp` is a sequence of R"GRBL(...)GRBL" literals
    interleaved with `#include "grbl/<part>.h"`, and each of those headers is one
    more R-string. Concatenating them in order yields exactly what
    `strlen_P(PAGE_GRBL_CONTROLLER)` streams to the browser.
    """
    text = cpp_path.read_text(encoding='utf-8', errors='replace')
    token = re.compile(r'#include\s+"(grbl/[^"]+)"|R"GRBL\((.*?)\)GRBL"', re.S)
    out = []
    for m in token.finditer(text):
        if m.group(1):
            part = (cpp_path.parent / m.group(1)).read_text(encoding='utf-8', errors='replace')
            pm = re.search(r'R"GRBL\((.*?)\)GRBL"', part, re.S)
            out.append(pm.group(1) if pm else '')
        else:
            out.append(m.group(2))
    return ''.join(out)


def core_css():
    txt = (WEB / '../../chatgpt/src/webpages/core_css.h').resolve().read_text(encoding='utf-8')
    m = re.search(r'R"CSS\((.*)\)CSS"', txt, re.S)
    return m.group(1) if m else ''


def build(name):
    asset_name, seed, kind = PAGES[name]
    src = (ASSETS / asset_name).read_text(encoding='utf-8', errors='replace')
    if name in RSTRING:
        mm = re.search(r'const char ' + RSTRING[name] + r'\[\] PROGMEM = R"([A-Za-z0-9]+)\((.*?)\)\1";', src, re.S)
        if not mm:
            raise SystemExit('R-string ' + RSTRING[name] + ' not found in ' + asset_name)
        body = mm.group(2)
    else:
        m = RAW.search(src)
        if not m:
            raise SystemExit('no PROGMEM R-string in ' + asset_name)
        body = m.group(2)

    if kind == 'built':
        body = assemble_parts(ASSETS / asset_name)

    if kind in ('fragment', 'built'):
        # The renderer wraps this asset: shell head -> _F flags -> body -> auto.js -> close.
        # auto.js is deliberately left out of the harness (it is the live board's runtime:
        # /api/lang, session keepalive, Home/Back footer); the collapse/expand and the
        # route-test buttons are the asset's own JS.
        css = core_css()
        html = SHELL_HEAD.replace('{title}', FRAGMENT_TITLES[name])
        if css:
            html += '<style>\n/* core.css inlined for the harness */\n' + css + '\n</style>'
        if name == 'api_reference':
            html += '<script>var _F=' + API_REFERENCE_FLAGS + ';</script>'
        if kind == 'built':
            # emitted by streamGrblControllerPageToPrint() before the body
            html += ('<script>window._localPagesNeverExpire=true;</script>'
                     '<script>window.GRBL_TRANSPORT_SELECTOR=false;</script>')
        if name in PRELUDE:
            src_file, tag = PRELUDE[name]
            pre_src = (WEB / '..' / '..' / src_file).resolve().read_text(encoding='utf-8', errors='replace')
            pm = re.search(r'R"' + tag + r'\((.*?)\)' + tag + r'"', pre_src, re.S)
            assert pm, 'prelude ' + tag + ' not found in ' + src_file
            html += pm.group(1)
        html += body
        if name in FOOTER:
            html += FOOTER[name]
        html += '</body></html>'
        html = re.sub(r'(<head[^>]*>)', lambda mm: mm.group(1) + MOCK_JS + seed, html, count=1)
    else:
        html = body
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
    names = list(PAGES) if args[0] == '--all' else args
    for n in names:
        if n not in PAGES:
            print('no entry for', n, '- add it to PAGES')
            sys.exit(1)
        build(n)
