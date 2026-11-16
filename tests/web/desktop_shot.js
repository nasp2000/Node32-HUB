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
const ANON = !!arg('anon', false);
const WAIT = parseInt(arg('wait', '8000'), 10);
if (!HOST) {
  console.error('usage: node desktop_shot.js --host <ip> [--path /] [--out file.png] [--auth u:p] [--verify-only] [--anon] [--wait ms]');
  process.exit(2);
}

// --anon: neutralise personal values in the JSON the page renders (SSID, MAC, IP,
// e-mail addresses, SMTP user, subject, credentials). The HTML/CSS/JS are still the
// board's own; only the operator-configured field values are replaced, in-page and
// before the page's first script runs. Every masked key is reported back.
const ANON_JS = String.raw`(function () {
  var SUB = {
    ssid: 'NODE32-BENCH', mac: '02:00:00:00:00:00', ip: '192.168.1.10',
    email: 'operator@example.com', subject: 'Node32-HUB bench',
    secret: 'bench', user: 'operator', name: 'OPERATOR'
  };
  var RULES = [
    [/^(wifi_ssid|ssid|ap_ssid|bssid)$/i, 'ssid'],
    [/^(wifi_mac|ethernet_mac|mac|mac_address)$/i, 'mac'],
    [/^(ip_address|wifi_ip|ethernet_ip|local_ip|ip)$/i, 'ip'],
    [/^(smtp_username|smtp_user|email_destination|destination|recipient_email|alert_emails)$/i, 'email'],
    [/^(email_subject|subject)$/i, 'subject'],
    [/^(password|smtp_password|token|api_key|secret)$/i, 'secret'],
    [/^(smtp_user_name|username|user|web_user)$/i, 'user'],
    [/^(unikon_fancier_name)$/i, 'name']
  ];
  window.__anonLog = [];
  function walk(o, prefix) {
    if (!o || typeof o !== 'object') return;
    Object.keys(o).forEach(function (k) {
      var v = o[k];
      if (v && typeof v === 'object') { walk(v, prefix + k + '.'); return; }
      if (typeof v !== 'string' || !v) return;
      for (var i = 0; i < RULES.length; i++) {
        if (RULES[i][0].test(k)) {
          o[k] = SUB[RULES[i][1]];
          window.__anonLog.push(prefix + k);
          return;
        }
      }
    });
  }
  var origFetch = window.fetch;
  // Log/terminal pages render operator strings directly (serial logs, MACs, IPs).
  // Exposed so the harness can redact the DOM right before the screenshot.
  window.__anonText = function () {
    var stats = { nodes: 0 };
    var walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null);
    var n;
    while ((n = walker.nextNode())) {
      var t = n.nodeValue;
      var o = t;
      t = t.replace(/\b[0-9A-F]{2}(?::[0-9A-F]{2}){5}\b/gi, SUB.mac);
      t = t.replace(/\b\d{1,3}(?:\.\d{1,3}){3}\b/g, SUB.ip);
      t = t.replace(/[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/g, SUB.email);
      if (t !== o) { n.nodeValue = t; stats.nodes++; }
    }
    window.__anonTextCount = (window.__anonTextCount || 0) + stats.nodes;
    return stats.nodes;
  };  window.fetch = function (input) {
    var url = typeof input === 'string' ? input : ((input && input.url) || '');
    var p = origFetch.apply(this, arguments);
    if (url.indexOf('/api/') < 0 || url.indexOf('/api/lang') >= 0) return p;
    return p.then(function (res) {
      var ct = (res.headers && res.headers.get('content-type')) || '';
      if (ct.indexOf('json') < 0) return res;
      return res.clone().text().then(function (txt) {
        var j;
        try { j = JSON.parse(txt); } catch (e) { return res; }
        walk(j, '');
        return new Response(JSON.stringify(j), {
          status: res.status, statusText: res.statusText, headers: res.headers
        });
      }).catch(function () { return res; });
    });
  };
})();`;

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

// Only some routes stamp Content-Language on the page itself (/status does not);
// the strings a page shows come from GET /api/lang, which always carries it.
async function uiLangCheck() {
  const res = await fetch(base + '/api/lang', { headers: { Authorization: authHeader } });
  return {
    status: res.status,
    content_language: res.headers.get('content-language'),
    x_ui_lang: res.headers.get('x-ui-lang'),
    etag: res.headers.get('etag'),
  };
}

// One persistent CDP session: Page.addScriptToEvaluateOnNewDocument is bound to
// the session that created it, so the socket must stay open for the whole run.
function connect(wsUrl) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    const pending = new Map();
    let nextId = 0;
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && pending.has(msg.id)) {
        const { res, rej } = pending.get(msg.id);
        pending.delete(msg.id);
        if (msg.error) rej(new Error(msg.error.message)); else res(msg.result);
      }
    };
    ws.onerror = reject;
    ws.onopen = () => resolve({
      send(method, params) {
        const id = ++nextId;
        return new Promise((res, rej) => {
          pending.set(id, { res, rej });
          ws.send(JSON.stringify({ id, method, params }));
          setTimeout(() => {
            if (pending.has(id)) { pending.delete(id); rej(new Error('cdp timeout ' + method)); }
          }, 60000);
        });
      },
      close() { try { ws.close(); } catch (e) { /* already gone */ } },
    });
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
  const report = { host: HOST, path: PAGE, anon: ANON, at: new Date().toISOString() };
  report.http = await httpCheck();
  report.lang_api = await uiLangCheck();

  const { spawn } = require('child_process');
  const port = 19324;
  const chrome = spawn('chromium', [
    '--headless', '--disable-gpu', '--no-sandbox', '--hide-scrollbars',
    `--remote-debugging-port=${port}`, 'about:blank',
  ], { stdio: 'ignore' });

  const consoleErrors = [];
  let sess = null;
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
    sess = await connect(wsUrl);
    await sess.send('Emulation.setDeviceMetricsOverride', {
      width: W, height: H, deviceScaleFactor: 1, mobile: false,
    });
    await sess.send('Network.enable', {});
    await sess.send('Network.setExtraHTTPHeaders', {
      headers: { Authorization: authHeader },
    });
    await sess.send('Runtime.enable', {});
    await sess.send('Page.enable', {});
    if (ANON) {
      await sess.send('Page.addScriptToEvaluateOnNewDocument', { source: ANON_JS });
    }
    const nav = await sess.send('Page.navigate', { url });
    report.nav = nav;
    await new Promise((r) => setTimeout(r, WAIT));
    const loc = await sess.send('Runtime.evaluate', {
      expression: 'location.href + " | " + document.readyState', returnByValue: true,
    });
    report.location = loc.result ? loc.result.value : null;

    const ev = await sess.send('Runtime.evaluate', {
      expression: RENDER_EXPR, returnByValue: true,
    });
    report.render = ev.result ? ev.result.value : null;

    if (ANON) {
      const mk = await sess.send('Runtime.evaluate', {
        expression: '(window.__anonLog||[]).slice().sort()', returnByValue: true,
      });
      report.anon_masked = mk.result ? mk.result.value : [];
      const tx = await sess.send('Runtime.evaluate', {
        expression: '(window.__anonText ? window.__anonText() : 0)', returnByValue: true,
      });
      report.anon_text_nodes = tx.result ? tx.result.value : 0;
    }

    if (!VERIFY_ONLY && OUT) {
      const metrics = await sess.send('Page.getLayoutMetrics', {});
      const cs = metrics.cssContentSize || metrics.contentSize;
      const height = Math.max(1, Math.ceil(cs.height));
      const shot = await sess.send('Page.captureScreenshot', {
        format: 'png',
        captureBeyondViewport: true,
        clip: { x: 0, y: 0, width: W, height, scale: 1 },
      });
      const outPath = path.resolve(__dirname, OUT);
      fs.mkdirSync(path.dirname(outPath), { recursive: true });
      fs.writeFileSync(outPath, Buffer.from(shot.data, 'base64'));
      report.out = { file: outPath, width: W, height, bytes: fs.statSync(outPath).size };
      const hdr = fs.readFileSync(outPath);
      report.out.png_width = hdr.readUInt32BE(16);
      report.out.png_height = hdr.readUInt32BE(20);
    }
  } finally {
    if (sess) sess.close();
    chrome.kill();
  }

  const r = report.render || {};
  const h = report.http || {};
  const l = report.lang_api || {};
  const en = (v) => String(v || '').toLowerCase().startsWith('en');
  const checks = [
    ['http 200', h.status === 200],
    ['english ui', en(h.content_language) || en(l.content_language) || en(l.x_ui_lang)],
    ['lang api 200 + en', l.status === 200 && en(l.content_language) && en(l.x_ui_lang)],
    ['no %%TOKEN%% in html', (h.unresolved_tokens || []).length === 0],
    ['no %%TOKEN%% rendered', (r.tokens_in_text || []).length === 0],
    ['assets reachable', (h.assets || []).every((a) => typeof a !== 'object' || a.status === 200)],
    ['page rendered', !!r.text_len && r.text_len > 200 && ((r.buttons || 0) > 0 || r.text_len > 800)],
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
