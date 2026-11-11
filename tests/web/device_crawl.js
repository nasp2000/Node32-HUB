#!/usr/bin/env node
/*
 * device_crawl.js — per-pack device crawler with evidence report.
 *
 * Drives a real board with headless Chromium over raw CDP (no npm deps).
 * For every page the device itself declares in /api/test-manifest it:
 *   - navigates with a cache-busting query (PROGMEM pages are ETag-cached)
 *   - captures console errors, exceptions, failed requests (4xx/5xx)
 *   - asserts the rendered text has no escaped junk (&#NNN; , \u{...} , mojibake)
 *   - takes a full-page screenshot
 *   - clicks every tab/section it can find (one level) and screenshots each
 * and writes an evidence bundle:
 *
 *   artifacts/device_crawl/<timestamp>/<pack>_<ip>/
 *       report.json      machine-readable results
 *       summary.txt      one line per page (PASS/FAIL + reason)
 *       index.html       gallery, opens with a browser
 *       console.log      raw console/network log
 *       manifest.json    the device's own page manifest (audit trail)
 *       screenshots/*.png
 *
 * Usage (on demand — no IP file, no automatic chain):
 *   node tests/web/device_crawl.js --pack rambox32 --host 192.168.1.99
 *   node tests/web/device_crawl.js --pack s3_cnc --host 192.168.1.38
 *   node tests/web/device_crawl.js --pack p4_cam --host 192.168.1.18 --auth admin:admin
 *   node tests/web/device_crawl.js --pack waveshare --host 192.168.1.50 --mobile
 *
 * Exit code 0 = every target passed, 1 = at least one FAIL (CI friendly).
 */
'use strict';

const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

// ---------------------------------------------------------------- CLI ------
function parseArgs(argv) {
  const a = { viewport: 'desktop', timeout: 25000, tabs: true, auth: '' };
  for (let i = 0; i < argv.length; i++) {
    const k = argv[i];
    if (k === '--pack') a.pack = argv[++i];
    else if (k === '--host') a.host = argv[++i];
    else if (k === '--auth') a.auth = argv[++i];
    else if (k === '--out') a.out = argv[++i];
    else if (k === '--desktop') a.viewport = 'desktop';
    else if (k === '--mobile') a.viewport = 'mobile';
    else if (k === '--no-tabs') a.tabs = false;
    else if (k === '--timeout') a.timeout = parseInt(argv[++i], 10);
    else if (k === '--help' || k === '-h') a.help = true;
    else throw new Error('unknown option: ' + k);
  }
  return a;
}

const HELP = `device_crawl.js — crawl every page of a Node32-HUB pack and store evidence
(on demand: you say the pack and the IP, it runs; nothing is stored or scheduled)

  --pack <name>        pack label used in the report (e.g. rambox32, s3_cnc)
  --host <ip>          board address (full IP, e.g. 192.168.1.99)
  --auth <user:pass>   HTTP basic auth (optional)
  --out <dir>          output root (default artifacts/device_crawl)
  --mobile             use 390x844 (phone) instead of the default 1280x800 desktop
  --no-tabs            skip the menu/tab pass
  --timeout <ms>       per-page timeout (default 25000)

Evidence is written under <out>/<timestamp>/<pack>_<ip>/ (report.json,
summary.txt, index.html, console.log, manifest.json, screenshots/).`;

// --------------------------------------------------------------- utils -----
const now = () => Date.now();
const pad = (n) => String(n).padStart(2, '0');
function stamp(d = new Date()) {
  return `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}_${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
}
function slug(s) {
  return String(s || 'unknown').toLowerCase().replace(/[^a-z0-9_.-]+/g, '_').replace(/^_+|_+$/g, '');
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function httpJson(url, auth, timeout = 8000) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeout);
  try {
    const headers = {};
    if (auth && auth.includes(':')) headers.Authorization = 'Basic ' + Buffer.from(auth).toString('base64');
    const r = await fetch(url, { headers, signal: ctrl.signal, cache: 'no-store' });
    const text = await r.text();
    let json = null;
    try { json = JSON.parse(text); } catch (_) {}
    return { ok: r.ok, status: r.status, text, json };
  } finally {
    clearTimeout(t);
  }
}

// ----------------------------------------------------------------- CDP -----
class CDP {
  constructor(wsUrl) {
    this.ws = new WebSocket(wsUrl);
    this.id = 0;
    this.pending = new Map();
    this.listeners = new Map();
    this.ready = new Promise((resolve, reject) => {
      this.ws.onopen = () => resolve();
      this.ws.onerror = (e) => reject(new Error('cdp connect failed'));
    });
    this.ws.onmessage = (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch (_) { return; }
      if (msg.id && this.pending.has(msg.id)) {
        const { resolve, reject } = this.pending.get(msg.id);
        this.pending.delete(msg.id);
        if (msg.error) reject(new Error(msg.error.message || 'cdp error'));
        else resolve(msg.result);
        return;
      }
      if (msg.method) {
        const fns = this.listeners.get(msg.method);
        if (fns) fns.forEach((fn) => { try { fn(msg.params, msg.sessionId); } catch (_) {} });
      }
    };
  }
  on(method, fn) {
    if (!this.listeners.has(method)) this.listeners.set(method, []);
    this.listeners.get(method).push(fn);
  }
  send(method, params = {}, sessionId) {
    const id = ++this.id;
    const payload = { id, method, params };
    if (sessionId) payload.sessionId = sessionId;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      try { this.ws.send(JSON.stringify(payload)); } catch (e) { reject(e); }
      setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          reject(new Error('cdp timeout: ' + method));
        }
      }, 40000);
    });
  }
  close() { try { this.ws.close(); } catch (_) {} }
}

function chromeBinary() {
  if (process.env.CHROME_BIN && fs.existsSync(process.env.CHROME_BIN)) return process.env.CHROME_BIN;
  const candidates = [
    '/usr/bin/chromium',
    '/usr/bin/chromium-browser',
    '/usr/bin/google-chrome',
    path.join(os.homedir(), '.cache/ms-playwright/chromium-1208/chrome-linux/chrome'),
  ];
  for (const c of candidates) { if (fs.existsSync(c)) return c; }
  throw new Error('Chromium not found (set CHROME_BIN)');
}

async function launchChromium(port) {
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'n32crawl-'));
  const child = spawn(chromeBinary(), [
    '--headless',
    '--no-sandbox',
    '--disable-gpu',
    '--disable-dev-shm-usage',
    '--hide-scrollbars',
    '--disable-extensions',
    '--no-first-run',
    '--disable-background-networking',
    '--force-device-scale-factor=1',
    '--remote-debugging-port=' + port,
    '--user-data-dir=' + userDataDir,
    'about:blank',
  ], { stdio: 'ignore' });

  let version = null;
  for (let i = 0; i < 40 && !version; i++) {
    await sleep(250);
    try {
      const r = await fetch(`http://127.0.0.1:${port}/json/version`);
      if (r.ok) version = await r.json();
    } catch (_) {}
  }
  if (!version) { try { child.kill(); } catch (_) {} throw new Error('Chromium did not expose CDP on port ' + port); }
  return { child, userDataDir, version };
}

// ----------------------------------------------------------- assertions ----
const BAD_TEXT = /&#\d+;|\\u\{[0-9a-f]{2,6}\}|â€|Ã[\u0080-\u00bf]/gi;

const CHECK_JS = `(() => {
  const body = document.body ? (document.body.innerText || '') : '';
  const m = body.match(/&#\\d+;|\\\\u\\{[0-9a-f]{2,6}\\}|\\u00e2\\u20ac|\\u00c3[\\u0080-\\u00bf]/gi);
  return {
    title: document.title || '',
    text_len: body.length,
    bad: m ? Array.from(new Set(m.map(s => s.slice(0, 24)))).slice(0, 12) : [],
    i18n: document.documentElement.getAttribute('data-i18n-ready'),
    h1: ((document.querySelector('h1') || {}).innerText || '').trim().slice(0, 60),
    forms: document.querySelectorAll('input,select,textarea').length,
    buttons: document.querySelectorAll('button').length
  };
})()`;

// Finds the page's own tab/section switchers, tags them so the click step is
// stable even if the DOM is rebuilt, and skips anything destructive.
const TABS_JS = `(() => {
  document.querySelectorAll('[data-crawl-tab]').forEach(e => e.removeAttribute('data-crawl-tab'));
  const DESTRUCTIVE = /(delete|erase|format|reset|reboot|restart|ota|factory|stop|cancel|clear|wipe|remove|logout|desligar|apagar)/i;
  const cands = new Set();
  document.querySelectorAll('[class*="tab" i], [id*="tab" i], nav, [role="tablist"], .tabs').forEach(c => {
    c.querySelectorAll('button, a[href^="#"], [onclick], [role=tab]').forEach(e => cands.add(e));
  });
  document.querySelectorAll('[role=tab], summary, .tab, .nav-tab, [aria-controls]').forEach(e => cands.add(e));
  const out = [];
  for (const e of cands) {
    const t = ((e.innerText || e.textContent || '').trim().replace(/\\s+/g, ' '));
    if (!t || t.length > 40) continue;
    if (!(e.offsetParent || (e.getClientRects && e.getClientRects().length))) continue;
    if (DESTRUCTIVE.test(t)) continue;
    if (e.closest('form')) continue;
    e.setAttribute('data-crawl-tab', String(out.length));
    out.push({ i: out.length, label: t.slice(0, 40) });
  }
  return out;   // 100%: every tab/section found, no sampling (safety cap below)
})()`;

const CLICK_TAB = (i, open) => `(() => {
  const el = document.querySelector('[data-crawl-tab="${i}"]');
  if (!el) return false;
  if (${open}) el.click();
  else { const d = el.closest('details'); if (d) d.open = false; }
  return true;
})()`;

// ------------------------------------------------------------ target run ---
async function runTarget(target, cdp, opts, outRoot, log) {
  const { pack, host, auth } = target;
  const base = `http://${host}`;
  const label = `${slug(pack)}_${host.replace(/[^0-9a-z.]/gi, '')}`;
  const dir = path.join(outRoot, label);
  const shotsDir = path.join(dir, 'screenshots');
  fs.mkdirSync(shotsDir, { recursive: true });

  const started = now();
  const report = {
    target: { pack, host, auth: !!auth },
    firmware: null, device: null, viewport: opts.viewport,
    started_at: new Date().toISOString(),
    pages: [], skipped: [], summary: { total: 0, pass: 0, fail: 0, warn: 0, skipped: 0 }, errors: [],
  };

  // --- manifest (authoritative page list for this pack) ---
  let manifest = null;
  const mf = await httpJson(base + '/api/test-manifest', auth);
  if (mf.ok && mf.json) {
    manifest = mf.json;
    fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify(manifest, null, 1));
    report.firmware = (manifest.device && manifest.device.firmware) || null;
    report.device = manifest.device || null;
  } else {
    report.errors.push(`test-manifest unavailable (HTTP ${mf.status})`);
  }

  const st = await httpJson(base + '/api/status', auth);
  if (st.ok && st.json) {
    report.firmware = report.firmware || st.json.firmware_version || null;
    report.status = {
      firmware_version: st.json.firmware_version,
      firmware_build: st.json.firmware_build,
      chip_model: st.json.chip_model,
      uptime: st.json.uptime,
    };
  }
  const cacheBust = encodeURIComponent(report.status && report.status.firmware_build || report.firmware || String(now()));

  // --- pages ---
  let pages = [];
  const skipped = [];
  if (manifest && Array.isArray(manifest.pages)) {
    for (const p of manifest.pages) {
      if (!p || !p.path) continue;
      if (p.enabled === false) { skipped.push({ path: p.path, reason: 'disabled in this build' }); continue; }
      if (p.testable === false) { skipped.push({ path: p.path, reason: 'not testable' }); continue; }
      pages.push(p.path);
    }
  }
  if (!pages.length) {
    pages = ['/']; // fallback: at least the home page
    report.errors.push('manifest listed no testable pages; crawled / only');
  }
  pages = Array.from(new Set(pages));
  report.skipped = skipped;

  // --- viewport ---
  const vp = opts.viewport === 'desktop'
    ? { width: 1280, height: 800, dpr: 1, mobile: false }
    : { width: 390, height: 844, dpr: 2, mobile: true };

  // --- one tab reused for all pages ---
  const created = await cdp.send('Target.createTarget', { url: 'about:blank' });
  const targetId = created.targetId;
  const attached = await cdp.send('Target.attachToTarget', { targetId, flatten: true });
  const S = attached.sessionId;

  let current = { console_errors: [], exceptions: [], failed: [], bad_http: [], doc_status: 0 };

  cdp.on('Runtime.consoleAPICalled', (p, sid) => {
    if (sid !== S) return;
    if (p.type !== 'error') return;
    const txt = (p.args || []).map((a) => a.value !== undefined ? String(a.value) : (a.description || a.type)).join(' ').slice(0, 300);
    current.console_errors.push(txt);
    log(`[console.error] ${txt}`);
  });
  cdp.on('Runtime.exceptionThrown', (p, sid) => {
    if (sid !== S) return;
    const d = p.exceptionDetails || {};
    const txt = (d.exception && (d.exception.description || d.exception.value)) || d.text || 'exception';
    current.exceptions.push(String(txt).slice(0, 300));
    log(`[exception] ${String(txt).slice(0, 300)}`);
  });
  cdp.on('Network.responseReceived', (p, sid) => {
    if (sid !== S) return;
    const r = p.response || {};
    if (p.type === 'Document' && p.frameId) current.doc_status = r.status || 0;
    if (r.status >= 400) current.bad_http.push(`${r.status} ${String(r.url).slice(0, 120)}`);
  });
  cdp.on('Network.loadingFailed', (p, sid) => {
    if (sid !== S) return;
    if (p.canceled) return;
    current.failed.push(`${p.errorText || 'failed'} ${String(p.type || '').slice(0, 20)}`);
  });

  await cdp.send('Page.enable', {}, S);
  await cdp.send('Runtime.enable', {}, S);
  await cdp.send('Network.enable', {}, S);
  await cdp.send('Emulation.setDeviceMetricsOverride', {
    width: vp.width, height: vp.height, deviceScaleFactor: vp.dpr, mobile: vp.mobile,
  }, S);
  if (auth && auth.includes(':')) {
    await cdp.send('Network.setExtraHTTPHeaders', {
      headers: { Authorization: 'Basic ' + Buffer.from(auth).toString('base64') },
    }, S);
  }

  async function evaluate(expr) {
    const r = await cdp.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }, S);
    return r && r.result ? r.result.value : undefined;
  }

  async function shot(file) {
    const metrics = await cdp.send('Page.getLayoutMetrics', {}, S);
    const cs = (metrics && (metrics.cssContentSize || metrics.contentSize)) || { width: vp.width, height: vp.height };
    const width = Math.max(1, Math.min(Math.ceil(cs.width), 2000));
    const height = Math.max(1, Math.min(Math.ceil(cs.height), 12000));
    const png = await cdp.send('Page.captureScreenshot', {
      format: 'png', captureBeyondViewport: true,
      clip: { x: 0, y: 0, width, height, scale: 1 },
    }, S);
    fs.writeFileSync(path.join(shotsDir, file), Buffer.from(png.data, 'base64'));
    return `screenshots/${file}`;
  }

  async function waitReady(timeoutMs) {
    const t0 = now();
    while (now() - t0 < timeoutMs) {
      const r = await evaluate(`({s:document.readyState, i:document.documentElement.getAttribute('data-i18n-ready')})`);
      if (r && r.s === 'complete' && r.i === '1') return true;
      await sleep(200);
    }
    const r = await evaluate(`({s:document.readyState, i:document.documentElement.getAttribute('data-i18n-ready')})`);
    return !!(r && r.s === 'complete');
  }

  for (const p of pages) {
    current = { console_errors: [], exceptions: [], failed: [], bad_http: [], doc_status: 0 };
    const url = `${base}${p}${p.includes('?') ? '&' : '?'}v=${cacheBust}`;
    const page = { path: p, url, status: 'PASS', findings: [], tabs: [] };
    log(`\n=== ${pack} ${p} ===`);

    try {
      const nav = await cdp.send('Page.navigate', { url }, S);
      if (nav && nav.errorText) { page.status = 'FAIL'; page.findings.push('navigate: ' + nav.errorText); }
      await waitReady(opts.timeout);
      await sleep(600); // let charts/canvas settle
      const chk = await evaluate(CHECK_JS) || {};
      page.title = chk.title; page.h1 = chk.h1; page.i18n = chk.i18n;
      page.controls = { inputs: chk.forms, buttons: chk.buttons };
      page.http = current.doc_status;

      if (current.doc_status !== 200) { page.status = 'FAIL'; page.findings.push('HTTP ' + current.doc_status); }
      if (current.console_errors.length) { page.status = 'FAIL'; page.findings.push(current.console_errors.length + ' console error(s)'); }
      if (current.exceptions.length) { page.status = 'FAIL'; page.findings.push(current.exceptions.length + ' JS exception(s)'); }
      if (current.failed.length) { page.status = 'WARN'; page.findings.push(current.failed.length + ' failed request(s)'); }
      if (current.bad_http.length) { page.status = 'WARN'; page.findings.push(current.bad_http.length + ' HTTP >=400'); }
      if (chk.bad && chk.bad.length) { page.status = 'FAIL'; page.findings.push('escaped text: ' + chk.bad.join(' ')); }
      if (chk.i18n && chk.i18n !== '1') { page.findings.push('i18n not ready'); }

      page.console_errors = current.console_errors.slice(0, 10);
      page.exceptions = current.exceptions.slice(0, 10);
      page.failed_requests = current.failed.slice(0, 10);
      page.bad_http = current.bad_http.slice(0, 10);
      page.bad_text = chk.bad || [];

      const name = slug(p === '/' ? 'home' : p.replace(/^\//, ''));
      page.screenshot = await shot(`${name}.png`);

      // --- one-level menu/tab pass ---
      if (opts.tabs) {
        const tabs = (await evaluate(TABS_JS)) || [];
        for (let ti = 0; ti < tabs.length && ti < 200; ti++) {
          const tab = tabs[ti];
          const before = current.console_errors.length + current.exceptions.length;
          await evaluate(CLICK_TAB(tab.i, true));
          await sleep(700);
          const tchk = await evaluate(CHECK_JS) || {};
          const entry = { label: tab.label, bad_text: tchk.bad || [] };
          entry.screenshot = await shot(`${name}__tab${ti}.png`);
          if ((tchk.bad || []).length) {
            page.findings.push(`tab "${tab.label}" escaped text: ${tchk.bad.join(' ')}`);
            if (page.status === 'PASS') page.status = 'FAIL';
          }
          const after = current.console_errors.length + current.exceptions.length;
          if (after > before) {
            entry.errors = current.console_errors.slice(before).concat(current.exceptions.slice(before));
            page.findings.push(`tab "${tab.label}" raised ${after - before} error(s)`);
            if (page.status === 'PASS') page.status = 'WARN';
          }
          page.tabs.push(entry);
          // collapse accordions so the next tab starts from a clean view
          await evaluate(CLICK_TAB(tab.i, false));
          await sleep(200);
        }
      }
    } catch (e) {
      page.status = 'FAIL';
      page.findings.push('crawl error: ' + String(e.message || e).slice(0, 200));
    }

    report.pages.push(page);
    log(`${page.status} ${p} ${page.findings.length ? '— ' + page.findings.join('; ') : ''}`);
  }

  await cdp.send('Target.closeTarget', { targetId }).catch(() => {});

  report.summary.total = report.pages.length;
  report.summary.pass = report.pages.filter((p) => p.status === 'PASS').length;
  report.summary.fail = report.pages.filter((p) => p.status === 'FAIL').length;
  report.summary.warn = report.pages.filter((p) => p.status === 'WARN').length;
  report.summary.skipped = skipped.length;
  report.duration_ms = now() - started;
  report.finished_at = new Date().toISOString();

  // --- evidence files ---
  fs.writeFileSync(path.join(dir, 'report.json'), JSON.stringify(report, null, 1));
  const lines = [];
  lines.push(`pack=${pack} host=${host} fw=${report.firmware || '?'} viewport=${opts.viewport}`);
  lines.push(`pages=${report.summary.total} pass=${report.summary.pass} fail=${report.summary.fail} warn=${report.summary.warn} skipped=${report.summary.skipped}`);
  for (const p of report.pages) {
    lines.push(`${p.status}\t${p.path}\t${(p.findings || []).join(' | ')}`);
  }
  for (const s of skipped) lines.push(`SKIP\t${s.path}\t${s.reason}`);
  fs.writeFileSync(path.join(dir, 'summary.txt'), lines.join('\n') + '\n');

  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const cards = report.pages.map((p) => {
    const color = p.status === 'PASS' ? '#22c55e' : p.status === 'FAIL' ? '#ef4444' : '#f59e0b';
    const tabs = (p.tabs || []).map((t) =>
      `<figure><img src="${esc(t.screenshot)}" loading="lazy"><figcaption>${esc(t.label)}</figcaption></figure>`).join('');
    return `<section><h2 style="color:${color}">${esc(p.status)} · ${esc(p.path)}</h2>
      <p>${esc(p.findings.join(' | ') || 'sem problemas')}</p>
      <figure><img src="${esc(p.screenshot)}" loading="lazy"><figcaption>${esc(p.title || '')}</figcaption></figure>
      ${tabs ? `<details><summary>${(p.tabs || []).length} menus/tabs</summary><div class="tabs">${tabs}</div></details>` : ''}
    </section>`;
  }).join('\n');
  fs.writeFileSync(path.join(dir, 'index.html'), `<!DOCTYPE html><html><head><meta charset="utf-8">
<title>${esc(pack)} ${esc(host)} — evidence</title>
<style>body{background:#0b1220;color:#e2e8f0;font-family:system-ui,sans-serif;padding:16px;max-width:1100px;margin:auto}
img{max-width:100%;border:1px solid #334155;border-radius:8px;background:#000}
section{border:1px solid #1e293b;border-radius:10px;padding:10px;margin:0 0 14px}
figure{margin:0 0 8px}.tabs{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:8px}
figcaption{font-size:12px;color:#94a3b8}h1{font-size:18px}code{color:#93c5fd}</style></head>
<body><h1>Device crawl — ${esc(pack)} <code>${esc(host)}</code></h1>
<p>fw ${esc(report.firmware || '?')} · ${report.summary.pass}/${report.summary.total} PASS ·
${report.summary.fail} FAIL · ${report.summary.warn} WARN · ${report.summary.skipped} SKIP ·
${Math.round(report.duration_ms / 1000)}s · ${esc(report.started_at)}</p>
${report.skipped && report.skipped.length ? `<p style="color:#94a3b8">SKIP: ${report.skipped.map(s => esc(s.path) + ' (' + esc(s.reason) + ')').join(', ')}</p>` : ''}${cards}</body></html>`);

  log(`report: ${path.join(dir, 'report.json')}`);
  return report;
}

// ---------------------------------------------------------------- main -----
async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) { console.log(HELP); return 0; }

  if (!opts.host) { console.log(HELP); return 2; }
  const targets = [{ pack: opts.pack || 'unknown', host: String(opts.host).trim(), auth: opts.auth }];

  const outRoot = path.resolve(opts.out || path.join(__dirname, '..', '..', 'artifacts', 'device_crawl'), stamp());
  fs.mkdirSync(outRoot, { recursive: true });
  const logLines = [];
  const log = (m) => { const s = String(m); console.log(s); logLines.push(s); };

  const port = 19300 + Math.floor(Math.random() * 90);
  const { child, userDataDir } = await launchChromium(port);
  const browser = await fetch(`http://127.0.0.1:${port}/json/version`).then((r) => r.json());
  const cdp = new CDP(browser.webSocketDebuggerUrl);
  await cdp.ready;

  let failed = 0;
  const results = [];
  try {
    for (const t of targets) {
      if (!t || !t.host) continue;
      log(`\n########## ${t.pack || 'unknown'} @ ${t.host} ##########`);
      try {
        const rep = await runTarget(t, cdp, opts, outRoot, log);
        results.push({ pack: t.pack, host: t.host, ...rep.summary, dir: path.join(outRoot, `${slug(t.pack)}_${t.host}`) });
        failed += rep.summary.fail;
      } catch (e) {
        failed++;
        log(`TARGET FAILED ${t.host}: ${e.message}`);
        results.push({ pack: t.pack, host: t.host, error: String(e.message) });
      }
    }
  } finally {
    cdp.close();
    try { child.kill(); } catch (_) {}
    try { fs.rmSync(userDataDir, { recursive: true, force: true }); } catch (_) {}
  }

  fs.writeFileSync(path.join(outRoot, 'run.log'), logLines.join('\n') + '\n');
  console.log('\n=== summary ===');
  for (const r of results) console.log(r.error ? `${r.pack}@${r.host}: ERROR ${r.error}` : `${r.pack}@${r.host}: ${r.pass}/${r.total} pass, ${r.fail} fail, ${r.warn} warn`);
  console.log('evidence root: ' + outRoot);
  return failed > 0 ? 1 : 0;
}

main()
  .then((code) => process.exit(code))
  .catch((e) => { console.error(e); process.exit(2); });
