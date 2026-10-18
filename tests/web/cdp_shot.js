#!/usr/bin/env node
// Headless Chromium screenshots with real mobile emulation via CDP.
// Usage: node cdp_shot.js <url> <out.png> <width> <height> [dpr]

async function cdp(method, params, wsUrl, id) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    ws.onopen = () => ws.send(JSON.stringify({ id, method, params }));
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id === id) { ws.close(); resolve(msg.result); }
    };
    ws.onerror = reject;
    setTimeout(() => reject(new Error('cdp timeout ' + method)), 30000);
  });
}

async function main() {
  const [url, out, w, h, dpr] = process.argv.slice(2);
  const width = parseInt(w), height = parseInt(h), scale = parseFloat(dpr || '2');
  const port = 19322;
  const { spawn } = require('child_process');
  const chrome = spawn('chromium', [
    '--headless', '--disable-gpu', '--no-sandbox',
    `--remote-debugging-port=${port}`, 'about:blank',
  ], { stdio: 'ignore' });
  let tabs = null;
  for (let i = 0; i < 30 && !tabs; i++) {
    await new Promise((r) => setTimeout(r, 500));
    try {
      const res = await fetch(`http://127.0.0.1:${port}/json`);
      tabs = await res.json();
      if (!tabs || !tabs.length) tabs = null;
    } catch (e) { tabs = null; }
  }
  if (!tabs) throw new Error('no debuggable tabs');
  try {
    const wsUrl = tabs[0].webSocketDebuggerUrl;
    let id = 1;
    await cdp('Emulation.setDeviceMetricsOverride', {
      width, height, deviceScaleFactor: scale, mobile: true,
    }, wsUrl, id++);
    await cdp('Page.navigate', { url }, wsUrl, id++);
    await new Promise((r) => setTimeout(r, 9000));
    const shot = await cdp('Page.captureScreenshot', { format: 'png' }, wsUrl, id++);
    require('fs').writeFileSync(out, Buffer.from(shot.data, 'base64'));
    console.log('shot ->', out);
  } finally {
    chrome.kill();
  }
}
main().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
