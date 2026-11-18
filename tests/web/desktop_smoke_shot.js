#!/usr/bin/env node
// Desktop screenshot of a harness page (file://) — the "smoke case" companion of
// desktop_shot.js for routes no live board serves.
//
//   node desktop_smoke_shot.js shots/desktop_dualfile.html ../../PAGES/desktop/dualfile.png
//
// Same Chromium-over-CDP path as desktop_shot.js, but the URL is local, so there is
// no auth, no /api/lang and no board session. Prints JSON with the metrics that matter
// for the capture log: dimensions, bytes, title, text length, control count, broken images.
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const arg = (name, def) => {
  const i = process.argv.indexOf('--' + name);
  return i > 0 && process.argv[i + 1] ? process.argv[i + 1] : def;
};

const SRC = process.argv[2];
const OUT = process.argv[3];
const W = parseInt(arg('w', '1440'), 10);
const H = parseInt(arg('h', '900'), 10);
const WAIT = parseInt(arg('wait', '5000'), 10);
const EXPAND = process.argv.includes('--expand');  // open every <details> (collapsed = first-visit state)

const fail = (msg) => { console.error(msg); process.exit(1); };
if (!SRC || !OUT) fail('usage: node desktop_smoke_shot.js <file.html> <out.png> [--w 1440] [--h 900] [--wait 5000] [--expand]');

(async () => {
  const port = 19393;
  const chrome = spawn('chromium', ['--headless', '--disable-gpu', '--no-sandbox',
    '--hide-scrollbars', '--remote-debugging-port=' + port, 'about:blank'], { stdio: 'ignore' });
  let tabs = null;
  for (let i = 0; i < 40 && !tabs; i++) {
    await new Promise((r) => setTimeout(r, 500));
    try {
      const res = await fetch('http://127.0.0.1:' + port + '/json');
      tabs = (await res.json()).filter((t) => t.type === 'page' && !String(t.url).startsWith('chrome-extension://'));
      if (!tabs.length) tabs = null;
    } catch (e) { tabs = null; }
  }
  if (!tabs) { chrome.kill(); fail('no debuggable tab'); }

  const ws = new WebSocket(tabs[0].webSocketDebuggerUrl);
  let id = 1;
  const pend = new Map();
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); }
  };
  await new Promise((r) => { ws.onopen = r; });
  const send = (method, params) => new Promise((res) => {
    const i = id++; pend.set(i, res); ws.send(JSON.stringify({ id: i, method, params }));
  });

  const report = { src: path.resolve(SRC), checks: [] };
  const check = (name, ok, detail) => report.checks.push({ name, ok: !!ok, detail });

  try {
    await send('Page.enable', {});
    await send('Runtime.enable', {});
    await send('Emulation.setDeviceMetricsOverride', { width: W, height: H, deviceScaleFactor: 1, mobile: false });
    await send('Page.navigate', { url: 'file://' + path.resolve(SRC) });
    await new Promise((r) => setTimeout(r, WAIT));

    if (EXPAND) {
      const ex = await send('Runtime.evaluate', {
        expression: 'JSON.stringify((function(){var d=document.querySelectorAll("details");for(var i=0;i<d.length;i++)d[i].open=true;return d.length;})())',
        returnByValue: true,
      });
      const exv = ex.result && ex.result.result ? ex.result.result.value : null;
      report.expanded = exv ? JSON.parse(exv) : 0;
      await new Promise((r) => setTimeout(r, 600));
    }

    const probe = await send('Runtime.evaluate', {
      expression: `JSON.stringify((function(){
        // A src-less <img> is a placeholder, not a broken image: it never issued a request
        // (e.g. the radio page's #artImg, display:none until artwork arrives).
        var imgs = Array.prototype.filter.call(document.images, function(i){
          return (i.getAttribute('src') || '').length > 0 && i.complete && i.naturalWidth === 0; });
        var overlays = Array.prototype.filter.call(document.querySelectorAll('#loadingOverlay'),
          function(o){ return getComputedStyle(o).display !== 'none'; });
        return {
          title: document.title,
          ready: document.readyState,
          text_len: (document.body.innerText || '').trim().length,
          buttons: document.querySelectorAll('button, a.btn, input[type=submit]').length,
          tables: document.querySelectorAll('table').length,
          rows: document.querySelectorAll('tr').length,
          failed_imgs: imgs.length,
          overlay_visible: overlays.length > 0,
          scroll_h: Math.max(document.documentElement.scrollHeight, document.body.scrollHeight),
          overflow_x: document.documentElement.scrollWidth > window.innerWidth
        };
      })())`,
      returnByValue: true,
    });
    const p = JSON.parse(probe.result.result.value);
    report.render = p;

    check('title present', p.title && p.title.length > 0, p.title);
    check('page rendered', p.text_len > 200 || p.buttons > 0, `${p.text_len} chars / ${p.buttons} controls`);
    check('no broken images', p.failed_imgs === 0, p.failed_imgs);
    check('loading overlay hidden', !p.overlay_visible, p.overlay_visible);
    check('no horizontal overflow', !p.overflow_x, p.overflow_x);
    check('no raw %%TOKEN%% in text', !/%%[A-Z0-9_]+%%/.test(p.title || ''), p.title);

    const height = Math.max(H, p.scroll_h);
    const shot = await send('Page.captureScreenshot', {
      format: 'png',
      captureBeyondViewport: true,
      clip: { x: 0, y: 0, width: W, height, scale: 1 },
    });
    const buf = Buffer.from(shot.result.data, 'base64');
    fs.mkdirSync(path.dirname(path.resolve(OUT)), { recursive: true });
    fs.writeFileSync(OUT, buf);
    report.out = { width: W, height, bytes: buf.length };
    check('png width ' + W, W === 1440, W);
  } catch (e) {
    report.error = e.message;
  } finally {
    try { ws.close(); } catch (e) {}
    chrome.kill();
  }

  report.pass = report.checks.every((c) => c.ok) && !report.error;
  console.log(JSON.stringify(report, null, 2));
  process.exit(report.pass ? 0 : 1);
})();
