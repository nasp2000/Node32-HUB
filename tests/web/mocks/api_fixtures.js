// Comprehensive mock API responses for GRBL controller page
window.__MOCK_API__ = window.__MOCK_API__ || {};

// Transport selector widget is shown when this flag is true (S3 CNC pack).
window.GRBL_TRANSPORT_SELECTOR = true;

// ---- GRBL status (polled) ----
window.__MOCK_API__['/api/grbl/status'] = {
  connected: true,
  state: 'Idle',
  position: { x: 10.500, y: 20.300, z: -5.000 },
  machine:  { x: 10.500, y: 20.300, z: -5.000 },
  wt: 0, feed: 1000, speed: 5000,
  ov: { feed: 100, rap: 100, spindle: 100 },
  pins:  { probe: false, door: false },
  active: '',
  line: 0,
  events: [],
  recentErrors: [],
  usb: { slot: 1, chip: 'CH340', baud: 115200, transport: 'UART' },
  grbl_transport: 'uart',
  usb_running: true,
  usb_connected: 0,
  usb_backend: 'host',
  control_can_write: true,
  workflow_state: 'idle',
  job_active: false
};

window.__MOCK_API__['/api/grbl/transport'] = { success: true, message: 'ok', grbl_transport: 'uart' };

window.__MOCK_API__['/api/grbl/state'] = window.__MOCK_API__['/api/grbl/status'];

// ---- Control token ----
window.__MOCK_API__['/api/grbl/control-token'] = { token: 'ctrl-token-abc123' };

// ---- Send command (echo) ----
window.__MOCK_API__['/api/grbl/send'] = { status: 'ok', echo: '$J=G91 G21 X10 F500' };

// ---- Override (apply-on-demand, realtime bytes on the machine side) ----
window.__MOCK_API__['/api/grbl/override'] = { status: 'ok', success: true };

// ---- Job streaming ----
window.__MOCK_API__['/api/grbl/stream'] = { status: 'ok' };
window.__MOCK_API__['/api/grbl/pause']   = { status: 'ok' };
window.__MOCK_API__['/api/grbl/resume']  = { status: 'ok' };
window.__MOCK_API__['/api/grbl/cancel']  = { status: 'ok' };

// ---- Poll interval ----
window.__MOCK_API__['/api/grbl/poll-interval'] = { status: 'ok' };

// ---- USB line coding ----
window.__MOCK_API__['/api/grbl/usb-line-coding'] = { status: 'ok' };
window.__MOCK_API__['/api/grbl/usb-restart'] = { status: 'ok' };

// ---- SD files ----
window.__MOCK_API__['/api/grbl/list-gcode'] = {
  files: [
    { name: 'test.gcode', size: 12345, modified: '2025-01-01', isDir: false },
    { name: 'part.gcode', size: 6789, modified: '2025-01-02', isDir: false },
    { name: 'subdir', size: 0, modified: '2025-01-03', isDir: true }
  ],
  dir: '/sd'
};

// ---- SD file read ----
window.__MOCK_API__['/api/grbl/read-gcode-text'] = {
  text: 'G21\nG90\nG0 X0 Y0 Z5\nG1 Z-1 F100\nG1 X10 Y10 F200\nG0 Z5\n',
  path: '/sd/test.gcode',
  size: 12345
};

// ---- SD upload ----
window.__MOCK_API__['/api/grbl/upload-gcode-stream'] = { status: 'ok', bytes: 12345 };

// ---- SD create dir ----
window.__MOCK_API__['/api/grbl/create-dir'] = { status: 'ok' };

// ---- SD delete ----
window.__MOCK_API__['/api/grbl/delete-path'] = { status: 'ok' };

// ---- Frame bounds ----
window.__MOCK_API__['/api/grbl/frame-bbox'] = {
  minX: 0, maxX: 100, minY: 0, maxY: 100, minZ: -10, maxZ: 0
};

// ---- PSRAM ----
window.__MOCK_API__['/api/grbl/psram-file'] = {
  exists: true,
  name: 'psram_job.gcode',
  size: 45678,
  modified: '2025-01-15'
};

window.__MOCK_API__['/api/grbl/psram-upload'] = { status: 'ok', bytes: 45678 };
window.__MOCK_API__['/api/grbl/psram-clear'] = { status: 'ok' };
window.__MOCK_API__['/api/grbl/psram-read-text'] = {
  text: 'G21\nG90\nG0 X0 Y0\nM3 S1000\nG1 X50 Y50 F300\nM5\n',
  size: 45678
};
window.__MOCK_API__['/api/grbl/psram-stream'] = { status: 'ok' };

// ---- Config ----
window.__MOCK_API__['/api/config'] = {};
window.__MOCK_API__['/api/grbl/config'] = {
  jog: { step: 1, feed: 500 },
  frame: { x: 100, y: 100, z: 50 }
};
window.__MOCK_API__['/api/ws-token'] = { token: 'ws-mock-token' };

// ---- Macros ----
window.__MOCK_API__['/api/grbl/macros'] = {
  macros: ['$$', '$X', '$H', 'G10 L20 P1 X0 Y0 Z0']
};

// ---- USB slots ----
window.__MOCK_API__['/api/grbl/usb-slots'] = [
  { slot: 0, connected: false, desc: 'None' },
  { slot: 1, connected: true,  desc: 'CH340 @ 115200', chip: 'CH340' },
  { slot: 2, connected: false, desc: 'Empty' }
];
