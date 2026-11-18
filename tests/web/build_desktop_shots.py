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
    // A seed may pin the HTTP status when the page branches on it (e.g. the /ecra
    // mirror treats 304 as "frame unchanged"): { __http: 304 } (optionally __body).
    var status = 200, body = mock;
    if (body && typeof body === 'object' && body.__http) {
      status = body.__http;
      body = body.__body === undefined ? {} : body.__body;
    }
    return Promise.resolve({ ok: status >= 200 && status < 300, status: status,
      json: function () { return Promise.resolve(body); },
      text: function () { return Promise.resolve(JSON.stringify(body)); } });
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

RAMBOX_SEED = r"""
<script>
(function () {
  // Payloads copied from the real handlers (unikon_handler.cpp): status carries the
  // clock/club/race block, arrivals feed the 8-column table, pombal the 3-column one,
  // contests the race list. Default device in the page is 'unikon' (rmDev).
  function arr(ring, chip, time, date, kind, race) {
    return { ring: ring, chip: chip, time: time, date: date, kind: kind, race: race };
  }
  window.__MOCK_API__ = {
    '/api/unikon-status': {
      connected: true, lastPollAgoSec: 12,
      totalKnown: 1842, raceKnownCount: 1720, trainingKnownCount: 122,
      lastNewCount: 3, registeredCount: 240, contestCount: 4,
      moduleId: 'UK-4200', clubName: 'Clube Columbofilo de Aveiro', soltaName: 'Guarda',
      clubCode: 'PT-0421', fancierName: 'J. Silva',
      raceDate: '2026-11-18', raceName: 'Guarda 2026',
      enabled: true, intervalSec: 60, activeModule: 'rambox',
      manualSerial: false, polling: true, passiveListen: false,
      officialFormat: true, emailDest: 'pombos@example.org'
    },
    '/api/unikon-arrivals': { count: 5, arrivals: [
      arr('PT-0421-24-1187', '1A2B3C4D', '08:41:22', '18/11', 'race',     'Guarda 2026'),
      arr('PT-0421-24-0892', '5E6F7A8B', '08:41:47', '18/11', 'race',     'Guarda 2026'),
      arr('PT-0421-23-0451', '9C0D1E2F', '08:42:03', '18/11', 'race',     'Guarda 2026'),
      arr('PT-0421-24-1503', '3A4B5C6D', '08:42:31', '18/11', 'race',     'Guarda 2026'),
      arr('PT-0421-25-0032', '7E8F9A0B', '08:43:10', '18/11', 'training', 'Treino 18/11')
    ] },
    '/api/unikon-pombal': { count: 6, registrations: [
      { ring: 'PT-0421-24-1187', chip: '1A2B3C4D' },
      { ring: 'PT-0421-24-0892', chip: '5E6F7A8B' },
      { ring: 'PT-0421-23-0451', chip: '9C0D1E2F' },
      { ring: 'PT-0421-24-1503', chip: '3A4B5C6D' },
      { ring: 'PT-0421-25-0032', chip: '7E8F9A0B' },
      { ring: 'PT-0421-24-0726', chip: '0F1E2D3C' }
    ] },
    '/api/unikon-contests': { count: 4, contests: [
      { code: 'GDA26', date: '18/11', name: 'Guarda 2026',        pigeonCount: 240 },
      { code: 'GDN26', date: '15/11', name: 'Guarda Norte 2026',  pigeonCount: 198 },
      { code: 'VRS26', date: '08/11', name: 'Viseu 2026',         pigeonCount: 312 },
      { code: 'TRN26', date: '01/11', name: 'Treino 2026',        pigeonCount: 96 }
    ] },
    '/api/unikon-config': { enabled: true, interval: 60, manual: false, official_format: true }
  };
})();
</script>
"""

HX711_SEED = r"""
<script>
(function () {
  // GET /api/hx711-config == HX711ConfigManager::toJson() (sensor_hx711_config.cpp:112-133),
  // and the measure/tare POSTs return the same document.
  var cfg = {
    sensor_enabled: true,
    data_pin: 4, clk_pin: 5, gain: 128, samples: 8,
    read_interval_sec: 60,
    scale_factor: -432.1500, tare_offset: 84120,
    weight_unit: 0,                  // 0 = g (HX711_UNIT_GRAM)
    inject_to_buffer: true, output_csv: true,
    alert_min: 0, alert_max: 5000,
    last_weight: 1247.3850, last_raw: 898432,
    last_reading_time: Math.floor(Date.now() / 1000) - 45
  };
  window.__MOCK_API__ = {
    '/api/hx711-config':  cfg,
    '/api/hx711-measure': cfg,
    '/api/hx711-tare':    cfg
  };
})();
</script>
"""

INA226_SEED = r"""
<script>
(function () {
  // GET /api/ina226-config == INA226ConfigManager::toJson() (sensor_ina226_config.cpp:123-145);
  // the measure POST returns the same document.
  var cfg = {
    sensor_enabled: true,
    i2c_addr: 64,                    // 0x40 — A0=GND, A1=GND
    shunt_ohm: 0.1000, max_current_a: 10.0000, averaging: 4,
    current_unit: 0,                 // 0 = A
    read_interval_sec: 60,
    inject_to_buffer: true, output_csv: true,
    show_bus_voltage: true, show_shunt_voltage: true, show_current: true, show_power: true,
    alert_current_max: 8.0000, alert_power_max: 100.0000,
    last_bus_v: 24.0130, last_shunt_v: 0.0234,
    last_current_a: 2.3410, last_power_w: 56.2200,
    last_reading_time: Math.floor(Date.now() / 1000) - 30
  };
  window.__MOCK_API__ = {
    '/api/ina226-config':  cfg,
    '/api/ina226-measure': cfg
  };
})();
</script>
"""

MAX31865_SEED = r"""
<script>
(function () {
  // GET /api/max31865-config == MAX31865ConfigManager::toJson()
  // (sensor_max31865_config.cpp:111-137); measure returns the same document.
  var cfg = {
    sensor_enabled: true,
    cs_pin: 15, mosi_pin: 11, miso_pin: 13, clk_pin: 12,
    rtd_type: 0,                    // 0 = PT100
    wiring: 1,                      // 1 = 3-wire
    temp_unit: 0,                   // 0 = Celsius
    calibration_offset: -0.3500,
    read_interval_sec: 60,
    inject_to_buffer: true, output_csv: true,
    alert_low: -20.0000, alert_high: 200.0000,
    last_temp: 87.4200, last_unit: '\u00b0C',
    last_fault: 0,
    last_reading_time: Math.floor(Date.now() / 1000) - 25
  };
  window.__MOCK_API__ = {
    '/api/max31865-config':  cfg,
    '/api/max31865-measure': cfg
  };
})();
</script>
"""

SOUND_SEED = r"""
<script>
(function () {
  // GET /api/sound-config == SoundConfigManager::toJson()
  // (sensor_sound_config.cpp:81-98); measure returns the same document.
  var cfg = {
    sensor_enabled: true,
    adc_pin: 34, sample_window_ms: 50, sample_count: 4096,
    read_interval_sec: 60,
    inject_to_buffer: true, output_csv: true,
    alert_dbfs: -20.0000,
    last_peak_to_peak: 1832, last_peak_mv: 147.9000, last_dbfs: -36.4000,
    last_reading_time: Math.floor(Date.now() / 1000) - 20
  };
  window.__MOCK_API__ = {
    '/api/sound-config':  cfg,
    '/api/sound-measure': cfg
  };
})();
</script>
"""

CAN_SEED = r"""
<script>
(function () {
  // Payloads copied from can_handler.cpp: /api/can/status (state/mode/bitrate +
  // counters, state strings are RUNNING/BUS_OFF/RECOVERING/ERROR/STOPPED),
  // /api/can/frames (ts/id/dlc/ext/rtr/data[]) and /api/can/config (nested "config").
  function frame(ts, id, dlc, ext, rtr, data) {
    return { ts: ts, id: id, dlc: dlc, ext: ext, rtr: rtr, data: data };
  }
  window.__MOCK_API__ = {
    '/api/can/status': {
      success: true, state: 'RUNNING', mode: 0, bitrate_kbps: 500,
      tec: 0, rec: 0, total_rx: 1842, total_tx: 311,
      rx_errors: 2, tx_errors: 0, bus_off_count: 0,
      buffer_drops: 0, buffer_frames: 128, msgs_to_rx: 0
    },
    '/api/can/frames': { success: true, count: 6, frames: [
      frame(1284123, 0x18F, 8, false, false, [0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]),
      frame(1284011, 0x1A2, 8, false, false, [0x22, 0x04, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00]),
      frame(1283897, 0x0C1, 4, false, false, [0xFF, 0x00, 0x0A, 0x00]),
      frame(1283755, 0x18FF50E5, 8, true,  false, [0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00]),
      frame(1283644, 0x2B0, 2, false, false, [0x03, 0x00]),
      frame(1283512, 0x7DF, 8, false, false, [0x02, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00])
    ] },
    '/api/can/config': { success: true, config: {
      enabled: true, bitrate_kbps: 500, mode: 0,
      tx_pin: 5, rx_pin: 4,
      filter_id: 0, filter_mask: 0, filter_ext: false, log_to_sd: true
    } },
    '/api/can/restart': { success: true },
    '/api/can/clear':   { success: true },
    '/api/can/send':    { success: true }
  };
})();
</script>
"""

UDA1334A_SEED = r"""
<script>
(function () {
  // GET /api/uda1334a-config == UDA1334AConfigManager::toJson()
  // (audio_dac_uda1334a_config.cpp:151-162): enabled, sampleRate, bitDepth,
  // autoRoutingEnabled, initialized. POST -test returns {"success":true}.
  window.__MOCK_API__ = {
    '/api/uda1334a-config': {
      enabled: true,
      sampleRate: 44100,
      bitDepth: 16,
      autoRoutingEnabled: true,
      initialized: true
    },
    '/api/uda1334a-test': { success: true }
  };
})();
</script>
"""

RADIO_UI_SEED = r"""
<script>
(function () {
  // Payloads copied from radio_audio_runtime.cpp: /api/radio/status
  // (radioAudioStatusJson(), :2674-2740), /api/radio/queue (:2513-2544),
  // /api/radio/favorites (:2625-2640), /api/radio/library (:3238-3300),
  // /api/radio/resume and /api/radio/schedule. Queue/favorites entries are the
  // firmware's own seeded 80s80s stations (kDefaultRadioQueue, :47-...).
  // artwork/station_favicon are left empty on purpose: the page derives
  // /api/radio/artwork + the station favicon URL from them, and the harness must
  // not fetch external images (it would show as broken images).
  window.__MOCK_API__ = {
    '/api/radio/status': {
      success: true, message: 'Playback active',
      active: true, stream: true,
      volume: 68, current: '', cache_bytes: 1572864, error: '',
      reconnect_pending: false, reconnect_attempt: 0, stream_profile: 1,
      queue_count: 6, queue_index: 2, favorites_count: 4, resume_pending: false,
      shuffle: false, repeat: 1,
      sleep_active: false, sleep_min: 0, sleep_remaining: 0,
      eq_bands: 5, eq_gains: [4, 1, -1, 2, 3],
      wifi_rssi: -57,
      title: 'Blue Monday', artist: 'New Order',
      artwork: '', station_favicon: '', station: '80s80s Alternative',
      active_ms: 1192000, next_reconnect_ms: 0,
      library_available: true, sd_mounted: true, sd_fallback: false
    },
    '/api/radio/queue': {
      success: true, message: 'Queue status', index: 2,
      items: [
        'https://streams.80s80s.de/80s80sDAB/mp3-128/homepage/',
        'https://streams.80s80s.de/80s80sDAB/mp3-192/homepage/',
        'https://streams.80s80s.de/alternative/mp3-128/homepage/',
        'https://streams.80s80s.de/alternative/mp3-192/homepage/',
        'https://streams.80s80s.de/newwave/mp3-128/homepage/',
        'https://streams.80s80s.de/newwave/mp3-192/homepage/'
      ]
    },
    '/api/radio/favorites': {
      success: true, message: 'Favorites status',
      items: [
        { name: '90s90s Dance', url: 'https://streams.90s90s.de/dance/mp3-128/', favicon: '' },
        { name: '80s80s New Wave', url: 'https://streams.80s80s.de/newwave/mp3-128/', favicon: '' },
        { name: '80s80s Alternative', url: 'https://streams.80s80s.de/alternative/mp3-128/', favicon: '' },
        { name: 'Antena 3', url: 'http://radiocast.rtp.pt/antena380a.mp3', favicon: '' }
      ]
    },
    '/api/radio/library': {
      success: true, is_root: true, count: 4,
      folders: [
        { name: 'Depeche Mode', path: '/library/Depeche Mode' },
        { name: 'New Order', path: '/library/New Order' }
      ],
      items: [
        { name: '01 - Enjoy the Silence.mp3', path: '/library/Depeche Mode/01 - Enjoy the Silence.mp3' },
        { name: '02 - Personal Jesus.mp3', path: '/library/Depeche Mode/02 - Personal Jesus.mp3' },
        { name: '01 - Blue Monday.mp3', path: '/library/New Order/01 - Blue Monday.mp3' },
        { name: '02 - Bizarre Love Triangle.mp3', path: '/library/New Order/02 - Bizarre Love Triangle.mp3' }
      ]
    },
    '/api/radio/playlists': { success: true, playlists: ['80s classics', 'Warm-up'] },
    '/api/radio/resume': { success: true, autoResume: true, resumeFirst: false },
    '/api/radio/schedule': { success: true, slots: [] },
    '/api/radio/play-url': { success: true, message: 'Playing' },
    '/api/radio/play-sd': { success: true, message: 'Playing' },
    '/api/radio/volume': { success: true },
    '/api/radio/seek': { success: true },
    '/api/radio/stop': { success: true },
    '/api/radio/next': { success: true },
    '/api/radio/shuffle': { success: true },
    '/api/radio/repeat': { success: true },
    '/api/radio/sleep': { success: true },
    '/api/radio/eq': { success: true },
    '/api/radio/queue-set': { success: true }
  };
})();
</script>
"""

IPG_LASER_SEED = r"""
<script>
(function () {
  // Payloads exactly as the firmware emits them (ENABLE_IPG_LASER=1 build).
  // GET /api/ipg-laser-status (handlers/ipg_laser_handler.cpp:18-46): the current* fields
  // mirror the RS232 section config, required* are the manual's fixed 57600 8N1/no-flow.
  // Seed = a board already set up for the laser: baudrate_index 6 -> 57600
  // (RS232_BAUDRATES, diagnostics_impl.h:149), 8N1, xon/xoff off, and no template applied
  // (current_template is only written by a manual send, runtime_rs232_queue.cpp:174), so
  // renderStatus() shows the green "Compatible profile". An untouched board
  // (baudrate_index 4 -> 19200, ConfigManager.cpp:107-128) shows the amber badge instead.
  // GET /api/ipg-laser-commands (:55-76) returns the module's own kCommandCatalog
  // (ipg_laser_module.cpp:24-119) - all 61 entries copied 1:1, not a sample.
  // GET /api/ipg-laser-log (:78-96) returns the TX/RX ring (ipg_laser_module.cpp:103,
  // :121, :183): replies are the catalog's own "response" strings, oldest entry first.
  // GET /api/config?section=rs232 is the same RS232 profile the status payload reports.
  //
  // NOTE: as shipped, the asset's inline <script> is a SyntaxError (six single-quoted
  // strings carry unescaped data-i18n='...' attributes, page_ipg_laser_asset.cpp:273-538
  // - finding IPG-7), so no handler below ever runs and the capture shows the page in
  // that real "not loading" state. The payloads stay here because they are correct and
  // become visible the moment IPG-7 is fixed; they are never fabricated for the image.
  window.__MOCK_API__ = {
    '/api/ipg-laser-available': { available: true, message: 'IPG Laser module is enabled.' },
    '/api/ipg-laser-status': {
      captureActive: false, captureEndsInMs: 0, lastRxAgeMs: 4200,
      logCount: 14, lastCommand: 'RPW',
      currentBaudrate: 57600, currentDataBits: 8, currentParity: 'none',
      currentStopBits: 1, currentFlowControl: false, currentTemplate: '',
      requiredBaudrate: 57600, requiredDataBits: 8, requiredParity: 'none',
      requiredStopBits: 1, requiredFlowControl: 'none',
      transport: 'RS232 null-modem 3-wire',
      remoteModeNote: 'Laser must be in REMOTE mode with interlocks satisfied before EMON.'
    },
    '/api/ipg-laser-commands': {
      count: 61,
      commands: [
        {"code": "ABN", "category": "Guide", "usage": "ABN", "description": "Enable the aiming beam.", "response": "ABN", "notes": "Fails if external guide control is enabled."},
        {"code": "ABF", "category": "Guide", "usage": "ABF", "description": "Disable the aiming beam.", "response": "ABF", "notes": "Fails if external guide control is enabled."},
        {"code": "DEABC", "category": "Guide", "usage": "DEABC", "description": "Disable external aiming beam control.", "response": "DEABC", "notes": "Required to control guide beam via RS232."},
        {"code": "DEC", "category": "Control", "usage": "DEC", "description": "Disable external analog current control.", "response": "DEC", "notes": "Also disables dynamic scaling in waveform mode."},
        {"code": "DGM", "category": "Control", "usage": "DGM", "description": "Disable gate mode.", "response": "DGM", "notes": "Rejected while emission is on."},
        {"code": "DLE", "category": "Control", "usage": "DLE", "description": "Disable hardware emission control.", "response": "DLE", "notes": "Rejected while emission is on."},
        {"code": "DMOD", "category": "Control", "usage": "DMOD", "description": "Disable modulation mode.", "response": "DMOD", "notes": "Rejected while emission is on."},
        {"code": "DPM", "category": "Control", "usage": "DPM", "description": "Disable pulse mode.", "response": "DPM", "notes": "QCW models only."},
        {"code": "EEABC", "category": "Guide", "usage": "EEABC", "description": "Enable external aiming beam control.", "response": "EEABC", "notes": "Allows hardware guide control input."},
        {"code": "EEC", "category": "Control", "usage": "EEC", "description": "Enable external analog current control.", "response": "EEC", "notes": "Rejected while emission is on."},
        {"code": "EGM", "category": "Control", "usage": "EGM", "description": "Enable gate mode.", "response": "EGM", "notes": "Uses external gate input to trigger internal pulses."},
        {"code": "ELE", "category": "Control", "usage": "ELE", "description": "Enable hardware emission control.", "response": "ELE", "notes": "Rejected while emission is on."},
        {"code": "EMOD", "category": "Control", "usage": "EMOD", "description": "Enable modulation mode.", "response": "EMOD", "notes": "Rejected while emission is on."},
        {"code": "EMOFF", "category": "Emission", "usage": "EMOFF", "description": "Stop laser emission.", "response": "EMOFF", "notes": "Immediate stop command."},
        {"code": "EMON", "category": "Emission", "usage": "EMON", "description": "Start laser emission.", "response": "EMON", "notes": "Requires valid setpoint and remote-ready state."},
        {"code": "EPM", "category": "Control", "usage": "EPM", "description": "Enable pulse mode.", "response": "EPM", "notes": "QCW models only."},
        {"code": "ESTA", "category": "Readback", "usage": "ESTA", "description": "Read extended device status words for diagnostics.", "response": "ESTA: 256;0;...", "notes": "Used mainly for remote troubleshooting."},
        {"code": "HELP", "category": "Utility", "usage": "HELP [CMD]", "description": "List commands or show details for one command.", "response": "HELP: ...", "notes": "Commands are case-insensitive."},
        {"code": "LFP", "category": "Front Panel", "usage": "LFP", "description": "Lock the touch-screen front panel.", "response": "LFP", "notes": "Touch-screen option only."},
        {"code": "RBAUD", "category": "Readback", "usage": "RBAUD", "description": "Read the active RS232 baud-rate index.", "response": "RBAUD: 8", "notes": "Index 8 maps to 57600 bps."},
        {"code": "RCE", "category": "Service", "usage": "RCE <code>", "description": "Clear a critical error using an IPG service code.", "response": "RCE: Code Accepted", "notes": "Service use only."},
        {"code": "RCS", "category": "Readback", "usage": "RCS", "description": "Read current setpoint in percent.", "response": "RCS: 56.7", "notes": "Percent of maximum diode current."},
        {"code": "RCT", "category": "Readback", "usage": "RCT", "description": "Read internal laser temperature in Celsius.", "response": "RCT: 34.5", "notes": "Useful for health monitoring."},
        {"code": "REC", "category": "Readback", "usage": "REC", "description": "Read the critical error counter.", "response": "REC: 37", "notes": "Persistent count."},
        {"code": "RERR", "category": "Service", "usage": "RERR", "description": "Reset resettable errors.", "response": "RERR", "notes": "Equivalent to hardware error reset."},
        {"code": "RET", "category": "Readback", "usage": "RET", "description": "Read elapsed on-time in minutes.", "response": "RET: 1105", "notes": "Cumulative runtime."},
        {"code": "RFV", "category": "Readback", "usage": "RFV", "description": "Read software revision.", "response": "RFV: 7.28;2.83;ND", "notes": "Firmware versions separated by semicolons."},
        {"code": "RMEC", "category": "Readback", "usage": "RMEC", "description": "Read stored module error code.", "response": "RMEC: 0", "notes": "Zero means normal operation."},
        {"code": "RNC", "category": "Readback", "usage": "RNC", "description": "Read minimum allowed current setpoint.", "response": "RNC: 10.0", "notes": "Percent of maximum current."},
        {"code": "ROP", "category": "Readback", "usage": "ROP", "description": "Read average output power in watts.", "response": "ROP: 99.6", "notes": "May also return Off or Low."},
        {"code": "RPP", "category": "Readback", "usage": "RPP", "description": "Read peak output power in watts.", "response": "RPP: 730", "notes": "May also return Off or Low."},
        {"code": "RPRR", "category": "Readback", "usage": "RPRR", "description": "Read pulse repetition rate in Hz.", "response": "RPRR: 10.00", "notes": "Pulse mode only."},
        {"code": "RPW", "category": "Readback", "usage": "RPW", "description": "Read pulse width in ms.", "response": "RPW: 5.550", "notes": "Pulse mode only."},
        {"code": "RSN", "category": "Readback", "usage": "RSN", "description": "Read device serial number.", "response": "RSN: 6103081", "notes": "Useful for inventory and hostname defaults."},
        {"code": "SBAUD", "category": "Network/Serial", "usage": "SBAUD <index>", "description": "Set RS232 baud-rate index.", "response": "SBAUD: 9", "notes": "Via RS232 there is no reply because speed changes immediately."},
        {"code": "SDC", "category": "Setpoint", "usage": "SDC <percent>", "description": "Set diode current in percent.", "response": "SDC: 34.2", "notes": "Rejected if out of range or external control is enabled."},
        {"code": "SPRR", "category": "Setpoint", "usage": "SPRR <hz>", "description": "Set pulse repetition rate.", "response": "SPRR: 10", "notes": "Can fail with duty cycle too high."},
        {"code": "SPW", "category": "Setpoint", "usage": "SPW <ms>", "description": "Set pulse width.", "response": "SPW: 5.5", "notes": "Can fail with argument out of range or duty cycle too high."},
        {"code": "STA", "category": "Readback", "usage": "STA", "description": "Read 32-bit status word.", "response": "STA: 4100", "notes": "Bitfield reports emission, modes and alarms."},
        {"code": "UFP", "category": "Front Panel", "usage": "UFP", "description": "Unlock the touch-screen front panel.", "response": "UFP", "notes": "Touch-screen option only; fails in REMOTE with key restrictions."},
        {"code": "DDHCP", "category": "Ethernet", "usage": "DDHCP", "description": "Disable DHCP.", "response": "DDHCP", "notes": "Rejected while emission is on."},
        {"code": "EDHCP", "category": "Ethernet", "usage": "EDHCP", "description": "Enable DHCP.", "response": "EDHCP", "notes": "Rejected while emission is on."},
        {"code": "RDGW", "category": "Ethernet", "usage": "RDGW", "description": "Read default gateway.", "response": "RDGW: 192.168.1.1", "notes": "Dot-decimal format."},
        {"code": "RDHCP", "category": "Ethernet", "usage": "RDHCP", "description": "Read DHCP status.", "response": "RDHCP: OFF", "notes": "Returns ON or OFF."},
        {"code": "RIP", "category": "Ethernet", "usage": "RIP", "description": "Read IP address.", "response": "RIP: 192.168.1.230", "notes": "Dot-decimal format."},
        {"code": "RLHN", "category": "Ethernet", "usage": "RLHN", "description": "Read local host name.", "response": "RLHN: IPG-12004020", "notes": "Derived from serial by default."},
        {"code": "RMAC", "category": "Ethernet", "usage": "RMAC", "description": "Read MAC address.", "response": "RMAC: A1-B2-C3-D4-E5-F6", "notes": "Six hex groups separated by hyphens."},
        {"code": "RMASK", "category": "Ethernet", "usage": "RMASK", "description": "Read subnet mask.", "response": "RMASK: 255.255.240.0", "notes": "Dot-decimal format."},
        {"code": "RSTIP", "category": "Ethernet", "usage": "RSTIP", "description": "Reset TCP/IP settings to IPG defaults.", "response": "RSTIP", "notes": "Rejected while emission is on."},
        {"code": "SDGW", "category": "Ethernet", "usage": "SDGW <ip>", "description": "Set default gateway.", "response": "SDGW: 192.168.0.1", "notes": "Rejected while emission is on."},
        {"code": "SIP", "category": "Ethernet", "usage": "SIP <ip>", "description": "Set IP address.", "response": "SIP: 192.168.1.231", "notes": "Rejected while emission is on."},
        {"code": "SMAC", "category": "Ethernet", "usage": "SMAC <mac>", "description": "Set MAC address.", "response": "SMAC: 12-34-56-78-EF-EF", "notes": "Rejected while emission is on."},
        {"code": "SMASK", "category": "Ethernet", "usage": "SMASK <mask>", "description": "Set subnet mask.", "response": "SMASK: 255.255.0.0", "notes": "Rejected while emission is on."},
        {"code": "SLHN", "category": "Ethernet", "usage": "SLHN <host>", "description": "Set local host name.", "response": "SLHN: IPG-12004020", "notes": "Rejected while emission is on."},
        {"code": "DWPM", "category": "Waveform", "usage": "DWPM", "description": "Disable waveform pulse mode.", "response": "DWPM", "notes": "Pulse shaping option only."},
        {"code": "EWPM", "category": "Waveform", "usage": "EWPM", "description": "Enable waveform pulse mode.", "response": "EWPM", "notes": "Requires pulse/sequence availability and pulse mode."},
        {"code": "PCFG", "category": "Waveform", "usage": "PCFG", "description": "Read waveform mode configuration.", "response": "PCFG: [] [Status:Disabled]", "notes": "Reports mode and selected ID."},
        {"code": "PRLS", "category": "Waveform", "usage": "PRLS", "description": "List stored pulse profiles.", "response": "PRLS: [] [0:New Shape 0]", "notes": "Pulse shaping option only."},
        {"code": "PRSEL", "category": "Waveform", "usage": "PRSEL <id>", "description": "Select pulse profile ID.", "response": "PRSEL: [2:New Shape 2]", "notes": "Also switches to single pulse profile mode."},
        {"code": "SQLS", "category": "Waveform", "usage": "SQLS", "description": "List stored pulse sequences.", "response": "SQLS: [] [4:New Program 4]", "notes": "Pulse shaping option only."},
        {"code": "SQSEL", "category": "Waveform", "usage": "SQSEL <id>", "description": "Select pulse sequence ID.", "response": "SQSEL: [4:New Program 4]", "notes": "Also switches to sequence mode."},
      ]
    },
    '/api/ipg-laser-log': {
      count: 14,
      entries: [
        {"ageMs": 22400, "direction": "TX", "text": "STA"},
        {"ageMs": 21000, "direction": "RX", "text": "STA: 4100"},
        {"ageMs": 19600, "direction": "TX", "text": "RFV"},
        {"ageMs": 18200, "direction": "RX", "text": "RFV: 7.28;2.83;ND"},
        {"ageMs": 16800, "direction": "TX", "text": "ROP"},
        {"ageMs": 15400, "direction": "RX", "text": "ROP: 99.6"},
        {"ageMs": 14000, "direction": "TX", "text": "RPP"},
        {"ageMs": 12600, "direction": "RX", "text": "RPP: 730"},
        {"ageMs": 11200, "direction": "TX", "text": "RCS"},
        {"ageMs": 9800, "direction": "RX", "text": "RCS: 56.7"},
        {"ageMs": 8400, "direction": "TX", "text": "RPRR"},
        {"ageMs": 7000, "direction": "RX", "text": "RPRR: 10.00"},
        {"ageMs": 5600, "direction": "TX", "text": "RPW"},
        {"ageMs": 4200, "direction": "RX", "text": "RPW: 5.550"},
      ]
    },
    '/api/ipg-laser-send': { success: true, message: 'Command queued. Waiting for laser response.' },
    '/api/ipg-laser-clear': { success: true, message: 'IPG response log cleared.' },
    '/api/config?section=rs232': {
      rs232_baudrate: 57600, rs232_data_bits: 8, rs232_parity: 'none',
      rs232_stop_bits: 1, rs232_enabled: true
    }
  };
})();
</script>
"""

GOOGLE_SCRIPT_SEED = r"""
<script>
(function () {
  // GET /api/config?section=email == handleConfigGet() (config_handler.cpp:143-215):
  // the smtp + email slices of getFullConfig(true) plus the nested google_email block
  // added by the GS-1 fix (that is where the page reads its saved URL from). Passwords
  // arrive masked. The values here are the device's own, never an operator's: the
  // method is the one this page configures ("google"), the URL has the shape of a
  // deployed Web App, and the destination is a placeholder address.
  // POST /api/config (section=email) -> {"success":true,"message":"Config saved"}
  // (config_handler.cpp:452-486); POST /api/email-test -> email_handler.cpp:18-57.
  window.__MOCK_API__ = {
    '/api/config?section=email': {
      smtp: {
        server: 'smtp.gmail.com', port: 465,
        username: 'hub@exemplo.pt', password: '********',
        tls_insecure: true,
        google_script_url: 'https://script.google.com/macros/s/AKfycbxQ2m9dK0n4V1sT7pR3wY8eL6hC5uJ2bN0aF4dE/exec'
      },
      email: {
        destination: 'operador@exemplo.pt',
        auto_send: true, max_lines: 66, min_content_length: 3,
        subject: 'Node32-HUB',
        dont_copy_sender: false, attach_txt: true, include_timestamp: true,
        method: 'google'
      },
      google_email: {
        google_script_url: 'https://script.google.com/macros/s/AKfycbxQ2m9dK0n4V1sT7pR3wY8eL6hC5uJ2bN0aF4dE/exec'
      }
    },
    '/api/config': { success: true, message: 'Config saved' },
    '/api/email-test': { success: true, message: '⚡ Teste iniciado com sucesso! O resultado aparece no monitor em ~30 segundos.' }
  };
})();
</script>
"""

FALAS_SEED = r"""
<script>
(function () {
  // Payloads exactly as the Touch7 handlers emit them.
  // GET /api/falas -> falasListJson() (handlers_unified/touch7_handler.cpp:287-305):
  // {"files":[{"name","size"}]} from the real /falas folder, .wav/.WAV only.
  // GET /api/touch7-config -> Touch7ConfigManager::toJson() (touch7_config.cpp:44-70),
  // full document incl. the 4 peer slots with pass_set (passwords are never returned).
  // POST /api/falas-play?file= / -delete?file= -> {"success":true} (404 missing, 503 busy);
  // POST /api/touch7-config -> {"success":true}; POST /api/touch7-peer-refresh -> same.
  //
  // NOTE: as shipped the page never calls any of this. Its runtime block is guarded by
  // `typeof $ !== 'undefined'` and NO script in the page shell defines `$`
  // (streamPageShellStart, /config-manager.js and /auto.js have no such helper; and both
  // of those files are streamed AFTER the asset, so they could not help anyway) - finding
  // T7-38. The capture therefore shows the real inert state; the payloads here are the
  // correct ones and become visible the moment T7-38 is fixed.
  window.__MOCK_API__ = {
    '/api/falas': {
      files: [
        { name: 'inicio_turno.wav', size: 132094 },
        { name: 'fim_turno.wav', size: 118742 },
        { name: 'pausa.wav', size: 76028 },
        { name: 'retomar.wav', size: 71056 },
        { name: 'manutencao.wav', size: 154318 },
        { name: 'producao_ok.wav', size: 98640 }
      ]
    },
    '/api/touch7-config': {
      sensor_enabled: false, read_interval_sec: 60,
      theme: 1, clock_style: 0, volume: 70,
      t_max: 30, t_min: 5, h_max: 80,
      pir_dim_sec: 120, screensaver_sec: 300,
      backlight_auto: true, backlight_pct: 80,
      owm_key_set: false,
      wx_lat: 38.7167, wx_lon: -9.1333, wx_place: 'Lisboa',
      peers: [
        { name: 'CAM-P4', host: '192.168.1.18', user: 'admin', enabled: true,  pass_set: true },
        { name: 'CAM-M1', host: '192.168.1.9',  user: 'admin', enabled: true,  pass_set: true },
        { name: '',       host: '',             user: '',      enabled: false, pass_set: false },
        { name: '',       host: '',             user: '',      enabled: false, pass_set: false }
      ]
    },
    '/api/falas-play': { success: true },
    '/api/falas-delete': { success: true },
    '/api/touch7-peer-refresh': { success: true }
    // No entry for POST /api/touch7-config: the harness mock is method-blind, so it would
    // answer with the GET document above and peersSave() would report a failure. Harmless
    // while T7-38 keeps every control unbound; revisit the mock if a method-aware seed is
    // ever needed.
  };
})();
</script>
"""

ECRA_SEED = r"""
<script>
(function () {
  // Payloads as the Touch7 handlers emit them.
  // GET /api/touch7-config -> Touch7ConfigManager::toJson() (touch7_config.cpp:44-70);
  // ecraLoadGeo() only reads wx_place/wx_lat/wx_lon.
  // GET /api/touch7-forecast -> handleTouch7ForecastGet() (touch7_handler.cpp:152-186):
  // "today" is L(touch7TodayForecast) filled in (touch7_weather.cpp:158-161) and each
  // "days" entry is "<Day>  min/maxC  <condition>" (touch7_weather.cpp:163-169) for
  // five days after today (2026-11-18 -> Thu..Mon).
  // GET /api/touch7-frame returns a 768 000-byte RGB565 framebuffer plus X-Frame-Hash;
  // the harness cannot carry that, so it answers 304 - the "frame unchanged" branch
  // ecraPoll() takes on a board with a static LCD. The canvas therefore stays black and
  // the badge reads "LIVE · unchanged"; the LCD content itself is device state and is
  // NOT fabricated here.
  window.__MOCK_API__ = {
    '/api/touch7-config': {
      sensor_enabled: false, read_interval_sec: 60,
      theme: 1, clock_style: 0, volume: 70,
      t_max: 30, t_min: 5, h_max: 80,
      pir_dim_sec: 120, screensaver_sec: 300,
      backlight_auto: true, backlight_pct: 80,
      owm_key_set: false,
      wx_lat: 38.7167, wx_lon: -9.1333, wx_place: 'Lisboa',
      peers: [
        { name: 'CAM-P4', host: '192.168.1.18', user: 'admin', enabled: true, pass_set: true },
        { name: 'CAM-M1', host: '192.168.1.9', user: 'admin', enabled: true, pass_set: true },
        { name: '', host: '', user: '', enabled: false, pass_set: false },
        { name: '', host: '', user: '', enabled: false, pass_set: false }
      ]
    },
    '/api/touch7-forecast': {
      has: true, code: 2, isDay: true,
      today: 'Today 11/17C  Partly cloudy  Sunset 17:22',
      days: [
        'Thu  10/16C  Partly cloudy',
        'Fri  12/18C  Sunny',
        'Sat  11/15C  Cloudy',
        'Sun  9/14C  Light rain',
        'Mon  8/13C  Rain'
      ],
      refreshing: false, stale: false, age_sec: 42, last_error: ''
    },
    '/api/touch7-frame': { __http: 304 },
    '/api/touch7-weather-refresh': { success: true, refreshing: true }
  };
})();
</script>
"""

USB_SERVER_SEED = r"""
<script>
(function () {
  // Payloads exactly as sharedWebBuildUsbServer*Json() emit them (web_shared_logic.cpp):
  // config  :306-322  {"success","enabled","port","recv_timeout_ms","in_wait_timeout_ms"}
  // status  :266-305  {"success","compiled","running","enabled","port","recv_timeout_ms",
  //                    "in_wait_timeout_ms","active_clients","accepted_clients",
  //                    "rx_bytes","tx_bytes","rx_bytes_per_sec","tx_bytes_per_sec",
  //                    "devices_dirty"}
  // devices :323-...  {"success","devices":[{slot,dev_addr,description,speed,vid,pid,
  //                    bcd_device,iface_class,iface_subclass,iface_protocol,in_ep_addr,
  //                    out_ep_addr,rx_bytes,tx_bytes}]} for every connected USB-CDC slot.
  // State = the P4 USB-server pack with one CH340 serial adapter exported and one client
  // attached (the bench GRBL sender's adapter: 1a86:7523, High speed).
  window.__MOCK_API__ = {
    '/api/usb-server-config': {
      success: true, enabled: true, port: 3240,
      recv_timeout_ms: 1200, in_wait_timeout_ms: 220
    },
    '/api/usb-server-status': {
      success: true, compiled: true, running: true, enabled: true,
      port: 3240, recv_timeout_ms: 1200, in_wait_timeout_ms: 220,
      active_clients: 1, accepted_clients: 3,
      rx_bytes: 184320, tx_bytes: 96404,
      rx_bytes_per_sec: 512, tx_bytes_per_sec: 268,
      devices_dirty: false
    },
    '/api/usb-server-devices': {
      success: true,
      devices: [
        {
          slot: 0, dev_addr: 1, description: 'USB Serial CH340', speed: 3,
          vid: 6790, pid: 29987, bcd_device: 256,
          iface_class: 255, iface_subclass: 0, iface_protocol: 0,
          in_ep_addr: 129, out_ep_addr: 2,
          rx_bytes: 184320, tx_bytes: 96404
        }
      ]
    }
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
    'rambox': ('page_rambox_asset.h', RAMBOX_SEED, 'fragment'),
    'hx711': ('page_hx711_asset.h', HX711_SEED, 'fragment'),
    'ina226': ('page_ina226_asset.h', INA226_SEED, 'fragment'),
    'max31865': ('page_max31865_asset.h', MAX31865_SEED, 'fragment'),
    'sound': ('page_sound_asset.h', SOUND_SEED, 'fragment'),
    'can': ('page_can_asset.h', CAN_SEED, 'fragment'),
    'uda1334a': ('page_audio_dac_uda1334a_asset.cpp', UDA1334A_SEED, 'fragment'),
    'radio_ui': ('page_radio_ui_asset.cpp', RADIO_UI_SEED, 'fragment'),
    'ipg_laser': ('page_ipg_laser_asset.cpp', IPG_LASER_SEED, 'fragment'),
    'google_script': ('page_google_script_asset.cpp', GOOGLE_SCRIPT_SEED, 'fragment'),
    'falas': ('page_falas_asset.h', FALAS_SEED, 'fragment'),
    'ecra': ('page_ecra_asset.h', ECRA_SEED, 'fragment'),
    'usb_server': ('page_usb_server_asset.cpp', USB_SERVER_SEED, 'fragment'),
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
    # printed before the body by streamCanPageToPrint() — and it is broken on purpose:
    # the string contains C++ (L(activeLang->tabLabelState)) so the browser throws (CAN-7).
    'can': ('chatgpt/src/webpages/renderers/can_page_renderer.cpp', 'JSTABS'),
    # printed BEFORE the body by streamUDA1334APageToPrint(); its IIFE runs while
    # .wrap does not exist yet, so setup() returns early and no tab bar is created (UDA-6).
    'uda1334a': ('chatgpt/src/webpages/renderers/page_audio_dac_uda1334a_renderer.cpp', 'JSTABS'),
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
    'rambox': 'RAMBOX 🐦',
    'hx711': '⚖️ HX711 Weight Sensor',  # L(activeLang->pageTitleHx711), EN value
    # NOT the INA226 title: ina226_page_renderer.cpp:10 passes pageTitleMax31865 (INA-7),
    # so this is exactly what a board ships in <title>.
    'ina226': '🌡️ MAX31865 RTD Sensor',
    # Also PT and also hardcoded: max31865_page_renderer.cpp:10 passes the literal
    # "\xF0\x9F\x8C\xA1\xEF\xB8\x8F Sensor RTD MAX31865" instead of
    # L(activeLang->pageTitleMax31865) — finding MAX-8. This is what ships in <title>.
    'max31865': '🌡️ Sensor RTD MAX31865',
    # L(activeLang->pageTitleSound): EN "🔊 Sound Sensor" (PT "🔊 Sensor de Som")
    'sound': '🔊 Sound Sensor',
    # Hardcoded EN in can_page_renderer.cpp:22 ("\xF0\x9F\x94\x8C CAN Bus (TWAI)");
    # there is no pageTitleCan key yet (CAN-7 notes it).
    'can': '🔌 CAN Bus (TWAI)',
    # L(activeLang->pageTitleUda1334a): EN "🔊 UDA1334A Sound Card" (PT "🔊 Placa de Som UDA1334A")
    'uda1334a': '🔊 UDA1334A Sound Card',
    # Hardcoded in radio_ui_page_renderer.cpp:10 ("\\xF0\\x9F\\x8E\\xB5 Harmonica Box");
    # the page has no i18n at all (radio_ui.md §9) — finding RAD-1.
    'radio_ui': '🎵 Harmonica Box',
    # Hardcoded in ipg_laser_page_renderer.cpp:8 ("\xF0\x9F\x94\xA5 IPG Laser RS232");
    # there is no pageTitleIpgLaser key (IPG-1 covers the whole page's missing i18n).
    'ipg_laser': '🔥 IPG Laser RS232',
    # Google's shell title is not a UTF-8 literal: google_script_page_renderer.cpp:8 passes
    # "&#127760; Google App Script", so <title> holds the entity and the parser turns it into
    # "🌐 Google App Script" (title is RCDATA) — kept byte-identical here, entity included.
    'google_script': '&#127760; Google App Script',
    # L(activeLang->pageTitleFalas): EN "📊 Local Voice Prompts" (PT "📊 Falas locais").
    # Note the page's own H1 is a different string ("🔊 Falas locais", data-i18n
    # 'touch7Falas' -> EN "Local phrases"), so <title> and H1 disagree by design.
    'falas': '📊 Local Voice Prompts',
    # L(activeLang->pageTitleEcra): EN "🖥️ Screen Mirror" (PT "🖥️ Espelho do ecrã").
    'ecra': '🖥️ Screen Mirror',
    # Hardcoded in usb_server_page_renderer.cpp:8 ("\xF0\x9F\xA7\xA9 USB Server").
    'usb_server': '🧩 USB Server',
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
