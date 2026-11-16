#!/usr/bin/env node
// Desktop reference screenshot + serving verification for PAGES/desktop/.
// Captures a page as the FIRMWARE SERVES IT (real board over HTTP), then
// verifies that the same response is complete: 200, English, no unresolved
// %%TOKEN%%, every stylesheet/script reachable, page rendered with content.
//
// Usage:
//   node desktop_shot.js --host 192.168.1.29 --path / --out ../../PAGES/desktop/home.png
//                        [--auth admin:admin] [--w 1440] [--h 900]
//   node desktop_shot.js --host ... --verify-only        # no screenshot
//
// Exit 0 = verified (and PNG written, unless --verify-only); 1 = FAIL.

const fs = require('fs');
const path = require('path');

function arg(name, dflt) {
  const i = process.argv.indexOf('--' + name);
  if (i < 0) return dflt;
  const v = process.argv[i + 1];
  return v && !v.startsWith('--') ? v : true;
}

const HOST = arg('host', null);
const PAGE = arg('path', '/');
const AUTH = arg('auth', 'admin:admin');
const OUT = arg('out', null);
const W = parseInt(arg('w', '1440'), 10);
const H = parseInt(arg('h', '900'), 10);
const VERIFY_ONLY = !!arg('verify-only', false);
if (!HOST) {
  console.error('usage: node desktop_shot.js --host <ip> [--path /] [--out file.png] [--auth u:p] [--verify-only]');
  process.exit(2);
}

const base = `http://${HOST}`;
const url = `http://${HOST}${PAGE}`;
const authHeader = 'Basic ' + Buffer.from(AUTH).toString('base64');

async function httpCheck() {
  const res = await fetch(base + PAGE, { headers: { Authorization: authHeader } });
  const html = await res.text();
  const assets = [];
  const re = /(?:href|src)='([^']+)'/g;
  let m;
  while ((m = re.exec(html)) !== null) {
    const u = m[1];
    if (/^\/(core\.css|auto\.js|config-manager\.js)/.test(u)) assets.push(u);
  }
  const unique = [...new Set(assets)];
  for (const a of unique) {
    const r = await fetch(base + a, { headers: { Authorization: authHeader } });
    let body = '';
    try { body = await r.text(); } catch (e) { body = ''; }
    assets[assets.indexOf(a)] = { asset: a, status: r.status, bytes: body.length };
  }
  return {
    status: res.status,
    content_type: res.headers.get('content-type'),
    content_language: res.headers.get('content-language'),
    etag: res.headers.get('etag'),
    html_bytes: html.length,
    unresolved_tokens: (html.match(/%%[A-Z_]+%%/g) || []),
    assets: unique.map((a) => assets.find((x) => x && x.asset === a) || a),
  };
}

async function cdp(method, params, wsUrl, id) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    ws.onopen = () => ws.send(JSON.stringify({ id, method, params }));
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id === id) { ws.close(); resolve(msg.result); }
    };
    ws.onerror = reject;
    setTimeout(() => reject(new Error('cdp timeout ' + method)), 60000);
  });
}

const RENDER_EXPR = `(() => {
  const body = document.body;
  const txt = body ? body.innerText : '';
  const failedImgs = Array.from(document.images || []).filter(i => i.complete && i.naturalWidth === 0).length;
  const sections = Array.from(document.querySelectorAll('.home-section, [class*=section]')).length;
  const buttons = document.querySelectorAll('a.btn, button.btn').length;
  const css = getComputedStyle(document.body);
  const sheets = Array.from(document.styleSheets || []).map(s => s.href || 'inline');
  return {
    title: document.title,
    lang: document.documentElement.lang,
    hero: (document.querySelector('.home-hero') ? document.querySelector('.home-hero').innerText : '').trim(),
    sections, buttons,
    text_len: txt.length,
    tokens_in_text: (txt.match(/%%[A-Z_]+%%/g) || []),
    loading_overlay_visible: (() => { const o = document.getElementById('loadingOverlay'); return !!o && getComputedStyle(o).display !== 'none'; })(),
    body_bg: css.backgroundColor,
    doc_height: Math.ceil(document.documentElement.scrollHeight),
    doc_width: Math.ceil(document.documentElement.scrollWidth),
    sheets, partial_sheets: sheets.filter(h => h !== 'inline' && !String(h).startsWith(location.origin)).length,
    failed_imgs: failedImgs,
  };
})()`;

async function main() {
  const report = { host: HOST, path: PAGE, at: new Date().toISOString() };
  report.http = await httpCheck();

  const { spawn } = require('child_process');
  const port = 19324;
  const chrome = spawn('chromium', [
    '--headless', '--disable-gpu', '--no-sandbox', '--hide-scrollbars',
    `--remote-debugging-port=${port}`, 'about:blank',
  ], { stdio: 'ignore' });

  const consoleErrors = [];
  try {
    let tabs = null;
    for (let i = 0; i < 40 && !tabs; i++) {
      await new Promise((r) => setTimeout(r, 500));
      try {
        const res = await fetch(`http://127.0.0.1:${port}/json`);
        tabs = await res.json();
        tabs = (tabs || []).filter((t) => t.type === 'page' && !String(t.url).startsWith('chrome-extension://'));
        if (!tabs.length) tabs = null;
      } catch (e) { tabs = null; }
    }
    if (!tabs) throw new Error('no debuggable tabs');
    const wsUrl = tabs[0].webSocketDebuggerUrl;
    let id = 1;
    await cdp('Emulation.setDeviceMetricsOverride', {
      width: W, height: H, deviceScaleFactor: 1, mobile: false,
    }, wsUrl, id++);
    await cdp('Network.enable', {}, wsUrl, id++);
    await cdp('Network.setExtraHTTPHeaders', {
      headers: { Authorization: authHeader },
    }, wsUrl, id++);
    await cdp('Runtime.enable', {}, wsUrl, id++);
    await cdp('Page.enable', {}, wsUrl, id++);
    const nav = await cdp('Page.navigate', { url }, wsUrl, id++);
    report.nav = nav;
    await new Promise((r) => setTimeout(r, 8000));
    const loc = await cdp('Runtime.evaluate', {
      expression: 'location.href + " | " + document.readyState', returnByValue: true,
    }, wsUrl, id++);
    report.location = loc.result ? loc.result.value : null;

    const ev = await cdp('Runtime.evaluate', {
      expression: RENDER_EXPR, returnByValue: true,
    }, wsUrl, id++);
    report.render = ev.result ? ev.result.value : null;

    if (!VERIFY_ONLY && OUT) {
      const metrics = await cdp('Page.getLayoutMetrics', {}, wsUrl, id++);
      const cs = metrics.cssContentSize || metrics.contentSize;
      const height = Math.max(1, Math.ceil(cs.height));
      const shot = await cdp('Page.captureScreenshot', {
        format: 'png',
        captureBeyondViewport: true,
        clip: { x: 0, y: 0, width: W, height, scale: 1 },
      }, wsUrl, id++);
      const outPath = path.resolve(__dirname, OUT);
      fs.mkdirSync(path.dirname(outPath), { recursive: true });
      fs.writeFileSync(outPath, Buffer.from(shot.data, 'base64'));
      report.out = { file: outPath, width: W, height, bytes: fs.statSync(outPath).size };
      const hdr = fs.readFileSync(outPath);
      report.out.png_width = hdr.readUInt32BE(16);
      report.out.png_height = hdr.readUInt32BE(20);
    }
  } finally {
    chrome.kill();
  }

  const r = report.render || {};
  const h = report.http || {};
  const checks = [
    ['http 200', h.status === 200],
    ['english served', String(h.content_language || '').toLowerCase().startsWith('en')],
    ['no %%TOKEN%% in html', (h.unresolved_tokens || []).length === 0],
    ['no %%TOKEN%% rendered', (r.tokens_in_text || []).length === 0],
    ['assets reachable', (h.assets || []).every((a) => typeof a !== 'object' || a.status === 200)],
    ['page rendered', !!r.text_len && r.text_len > 200 && r.buttons > 0],
    ['no broken images', r.failed_imgs === 0],
    ['loading overlay hidden', r.loading_overlay_visible === false],
    ['no cross-origin sheet', r.partial_sheets === 0],
  ];
  if (!VERIFY_ONLY && OUT) checks.push(['png width ' + W, report.out.png_width === W]);
  report.checks = checks.map(([name, ok]) => ({ name, ok }));
  report.pass = checks.every(([, ok]) => ok);
  console.log(JSON.stringify(report, null, 2));
  process.exit(report.pass ? 0 : 1);
}

main().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
