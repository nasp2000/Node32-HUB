#include "mobile_common_asset.h"

// Shared CSS + JS for the phone pages. Pack-agnostic on purpose: the pack page
// only has to put its content inside a container with class="mp" and include
// MOBILE_COMMON_CSS before it and MOBILE_COMMON_JS after it.
//
// Scoped classes (all under .mp):
//   .tiles/.tile        big 2-column readout grid
//   .subbar/.subt       small segmented switch inside a screen
//   .bar/.msg           shared progress bar and one-line message
//   .btn/.primary       shared buttons
//   .log                scrolling log view
//   .reboot             destructive button (armed with a second tap)
const char MOBILE_COMMON_CSS[] PROGMEM = R"MCCSS(
<style>

  /* ── page root: fixed column, never scrolls (every pack) ─────────────── */
  html, body { height: 100%; margin: 0; overflow: hidden; background: #0b1220; }
  body { color: #e6f1ff; font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  #gm {
    position: fixed; inset: 0; display: flex; flex-direction: column; gap: 8px;
    padding: 10px 12px calc(10px + env(safe-area-inset-bottom));
    box-sizing: border-box; height: 100dvh; overflow: hidden;
  }

  /* ── generic page chrome (any pack) ───────────────────────────────── */
  .mp header { display: flex; flex-direction: column; gap: 4px; }
  .mp .navstrip { display: grid; grid-auto-flow: column; grid-auto-columns: 1fr; gap: 4px; }
  .mp .ncol { display: flex; flex-direction: column; gap: 4px; min-width: 0; background: transparent; border: 0; padding: 0; }
  .mp .ncol .ntab {
    display: block; height: 30px; line-height: 30px; border-radius: 8px; border: 1px solid transparent;
    background: transparent; color: #64748b; font-size: 12px; font-weight: 700; text-align: center;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .mp .ncol .nrail { display: block; height: 3px; border-radius: 2px; background: #24344d; }
  .mp .ncol.done .ntab { color: #bfdbfe; }
  .mp .ncol.done .nrail { background: #1d4ed8; }
  .mp .ncol.on .ntab { border-color: #3b82f6; background: #1d2b47; color: #e6f1ff; }
  .mp .ncol.on .nrail { background: #3b82f6; }
  .mp .hrow { display: flex; align-items: center; gap: 6px; min-height: 26px; }
  .mp .hrow .no { font-size: 11px; color: #94a3b8; }
  .mp .hrow .pack {
    font-size: 12px; font-weight: 700; color: #93c5fd; letter-spacing: .2px;
    max-width: 55%; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .mp .sr { position: absolute; width: 1px; height: 1px; padding: 0; margin: -1px; overflow: hidden; clip: rect(0,0,0,0); white-space: nowrap; border: 0; }
  .mp .badge {
    margin-left: auto; font-size: 11px; padding: 3px 8px; border-radius: 999px;
    border: 1px solid #334155; color: #94a3b8; white-space: nowrap;
  }
  .mp .badge.ok { border-color: #10b981; color: #6ee7b7; }
  .mp .badge.warn { border-color: #f59e0b; color: #fcd34d; }

  .mp .screen {
    flex: 1 1 auto; min-height: 0; display: none; flex-direction: column; gap: 6px;
    overflow: hidden; justify-content: center; justify-content: safe center;
  }
  .mp .screen.on { display: flex; }
  .mp .screen.fill.on { justify-content: flex-start; }

  .mp .rowhdr { display: flex; align-items: center; gap: 8px; font-size: 11px; color: #94a3b8; }
  .mp .rowhdr .dir { flex: 1 1 auto; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .mp .mini {
    width: 32px; height: 32px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #cbd5e1; font-size: 15px; line-height: 1;
  }
  .mp .list {
    flex: 1 1 auto; min-height: 0; overflow-y: auto; -webkit-overflow-scrolling: touch;
    border: 1px solid #1e293b; border-radius: 12px; background: #0f172a;
  }
  .mp .row {
    display: flex; align-items: center; gap: 8px; padding: 13px 12px;
    border-bottom: 1px solid #16213a; font-size: 14px; color: #e6f1ff;
  }
  .mp .row:last-child { border-bottom: 0; }
  .mp .row.sel { background: #1d2b47; }
  .mp .row .n { flex: 1 1 auto; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .mp .row .s { flex: 0 0 auto; font-size: 11px; color: #94a3b8; }

  .mp .acts { display: flex; gap: 8px; }
  .mp .nav { display: flex; align-items: center; gap: 10px; }
  .mp .nav .no { font-size: 12px; color: #94a3b8; }
  .mp .nav .arrow {
    width: 46px; height: 46px; border-radius: 50%; border: 1px solid #3b82f6;
    background: #0f172a; color: #e6f1ff; font-size: 20px;
  }
  .mp .nav .arrow:disabled { opacity: .3; border-color: #334155; color: #64748b; }
  .mp .nav .back { margin-left: 0; }
  .mp .nav .next { margin-left: auto; }

  .mp .chips { display: flex; gap: 6px; }
  .mp .chip {
    flex: 1 1 0; height: 32px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #94a3b8; font-size: 12px; font-weight: 700;
  }
  .mp .chip.on { border-color: #3b82f6; background: #1d2b47; color: #dbeafe; }
  .mp .acts3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 6px; }
  .mp .acts3 .actbtn {
    height: 40px; border-radius: 10px; border: 1px solid #334155; background: #0f172a;
    color: #cbd5e1; font-size: 11px; font-weight: 700; letter-spacing: .3px;
  }

  .mp .tiles { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; }
  .mp .tile {
    display: flex; flex-direction: column; gap: 2px; padding: 8px 10px;
    border: 1px solid #1e293b; border-radius: 10px; background: #0f172a; min-width: 0;
  }
  .mp .tile .k { font-size: 10px; letter-spacing: .4px; text-transform: uppercase; color: #64748b; }
  .mp .tile b { font-size: 16px; color: #e6f1ff; overflow-wrap: anywhere; word-break: normal; }

  .mp .subbar { display: flex; gap: 6px; }
  .mp .subt {
    flex: 1 1 0; height: 32px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #94a3b8; font-size: 12px; font-weight: 700;
  }
  .mp .subt.on { border-color: #3b82f6; background: #1d2b47; color: #dbeafe; }
  .mp .pane { flex: 0 1 auto; min-height: 0; display: none; flex-direction: column; gap: 6px; }
  .mp #muPaneLog { flex: 1 1 auto; }   /* the log fills, the rest centres */
  .mp .pane.on { display: flex; }

  .mp .bar { height: 6px; border-radius: 3px; background: #1e293b; overflow: hidden; display: none; }
  .mp .bar.on { display: block; }
  .mp .bar span { display: block; height: 100%; width: 0%; background: #3b82f6; transition: width .15s linear; }
  .mp .msg { font-size: 12px; line-height: 1.3; color: #94a3b8; min-height: 2.4em; }
  .mp .msg.err { color: #fca5a5; }
  .mp .msg.ok { color: #6ee7b7; }

  .mp .btn {
    height: 42px; border-radius: 10px; border: 1px solid #334155; background: #0f172a;
    color: #cbd5e1; font-size: 13px; font-weight: 700;
  }
  .mp .btn:active { background: #1d2b47; }
  .mp .btn:disabled { opacity: .45; }
  .mp .primary {
    height: 50px; border: 0; border-radius: 12px; background: #2563eb; color: #fff;
    font-size: 16px; font-weight: 700;
  }
  .mp .primary:disabled { background: #1e293b; color: #64748b; }

  .mp .log {
    flex: 1 1 auto; min-height: 0; margin: 0; padding: 8px 10px; overflow-y: auto;
    border: 1px solid #1e293b; border-radius: 10px; background: #0f172a; color: #cbd5e1;
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12px; line-height: 1.5;
    white-space: pre-wrap; word-break: break-word; -webkit-overflow-scrolling: touch;
  }

  .mp .reboot {
    height: 46px; border-radius: 11px; border: 1px solid #dc2626; background: #7f1d1d;
    color: #fff; font-size: 13px; font-weight: 700; letter-spacing: .3px;
  }
  .mp .reboot.arm { background: #b91c1c; }
  .mp .reboot:disabled { background: #0f172a; color: #64748b; border-color: #334155; }
  .mp .note { font-size: 11px; color: #64748b; }
  /* option rows (any pack: camera modes, RS232, clock…) */
  .mp .opts { display: flex; flex-direction: column; gap: 6px; }
  .mp .opt { display: flex; align-items: center; gap: 10px; font-size: 13px; color: #cbd5e1; }
  .mp .opt > span { flex: 0 0 96px; color: #94a3b8; font-size: 12px; }
  .mp .opt select {
    flex: 1 1 auto; height: 34px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #e6f1ff; font-size: 13px; padding: 0 8px;
  }
  .mp .opt input[type=range] { flex: 1 1 auto; accent-color: #3b82f6; }
  .mp .opt b { flex: 0 0 28px; text-align: center; color: #93c5fd; font-size: 13px; }
  .mp .stepper { flex: 1 1 auto; display: flex; align-items: center; gap: 8px; }
  .mp .stepper button {
    width: 40px; height: 34px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #cbd5e1; font-size: 16px;
  }
  .mp .stepper b { flex: 1 1 auto; text-align: center; color: #93c5fd; }
  /* one-line command input (any pack: MDI, RS232, ...) */
  .mp .mdi { display: flex; gap: 6px; }
  .mp .mdi input {
    flex: 1 1 auto; min-width: 0; height: 36px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #e6f1ff; font-size: 13px; padding: 0 10px;
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  }
  .mp .mdi button {
    flex: 0 0 auto; width: 48px; height: 36px; border-radius: 9px;
    border: 1px solid #3b82f6; background: #1d2b47; color: #dbeafe; font-size: 15px;
  }

  /* visible while a finger is down (the 250 ms gate needs feedback) */
  .mp button.pressing, .mp a.pressing { opacity: .6; transform: scale(.97); }
  .mp button, .mp a { transition: opacity .08s linear, transform .08s linear; }
</style>
)MCCSS";

const char MOBILE_COMMON_JS[] PROGMEM = R"MCJS(
<script>
// window.MU — helpers shared by every phone page. No DOM assumptions besides
// the ids each page passes in; the host page owns its own state.
window.MU = (function () {
  'use strict';
  var armedKey = '', armedUntil = 0;

  function t(k, fb) {
    try {
      if (typeof window._t === 'function') {
        var v = window._t(k, fb);
        if (v !== undefined && v !== null && v !== '' && v !== k) return v;
      }
      var raw = localStorage.getItem('ui_lang_cache');
      if (raw) {
        var p = JSON.parse(raw);
        if (p && p[k] !== undefined) return p[k];
      }
    } catch (e) {}
    return fb !== undefined ? fb : k;
  }
  function tf(k, fb, vars) {
    var s = String(t(k, fb));
    for (var key in (vars || {})) {
      if (Object.prototype.hasOwnProperty.call(vars, key)) s = s.replace('{' + key + '}', String(vars[key]));
    }
    return s;
  }

  function get(url) {
    return fetch(url, { cache: 'no-store' }).then(function (r) {
      return r.json().then(function (d) { return { status: r.status, body: d || {} }; },
                         function () { return { status: r.status, body: {} }; });
    });
  }

  function post(url, payload) {
    var body = payload || {};
    var enc = [];
    for (var k in body) {
      if (Object.prototype.hasOwnProperty.call(body, k)) {
        enc.push(encodeURIComponent(k) + '=' + encodeURIComponent(body[k]));
      }
    }
    return fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8' },
      body: enc.join('&')
    }).then(function (r) {
      return r.json().then(function (d) { return { status: r.status, body: d || {} }; },
                         function () { return { status: r.status, body: {} }; });
    });
  }

  function fmtSize(bytes) {
    var b = Number(bytes) || 0;
    if (b < 1024) return b + ' B';
    var kb = b / 1024;
    if (kb < 1024) return kb.toFixed(kb < 10 ? 1 : 0) + ' kB';
    var mb = kb / 1024;
    if (mb < 1024) return mb.toFixed(1) + ' MB';
    return (mb / 1024).toFixed(1) + ' GB';
  }

  function fmtEta(sec) {
    sec = Math.max(0, Math.round(Number(sec) || 0));
    var m = Math.floor(sec / 60), s = sec % 60;
    return tf('gmEta', 'left {m}m {s}s', { m: m, s: (s < 10 ? '0' : '') + s });
  }

  function msg(el, text, kind) {
    if (!el) return;
    el.textContent = text || '';
    el.className = 'msg' + (kind === 'err' ? ' err' : kind === 'ok' ? ' ok' : '');
  }

  // Two-tap arming for destructive actions (no modal on a phone).
  function arm(key, windowMs) {
    var now = Date.now();
    if (armedKey === key && now < armedUntil) {
      armedKey = '';
      return true;
    }
    armedKey = key;
    armedUntil = now + (windowMs || 4000);
    return false;
  }
  function armed(key) { return armedKey === key && Date.now() < armedUntil; }
  function disarm() { armedKey = ''; }

  // ── page strip (one column per screen) + swipe ───────────────────────────
  // The host page owns the markup; MU only reflects the current page and turns
  // clicks/swipes into step changes.
  function stripActive(root, step) {
    if (!root) return;
    var cols = root.querySelectorAll('.ncol');
    for (var i = 0; i < cols.length; i++) {
      var n = parseInt(String(cols[i].getAttribute('data-step-go') || '0'), 10);
      cols[i].className = 'ncol' + (n === step ? ' on' : (n < step ? ' done' : ''));
    }
  }

  function strip(root, onGo) {
    if (!root) return;
    root.addEventListener('click', function (ev) {
      var b = ev.target && ev.target.closest ? ev.target.closest('.ncol[data-step-go]') : null;
      if (!b) return;
      onGo(parseInt(String(b.getAttribute('data-step-go') || '1'), 10));
    });
  }

  // Horizontal swipe anywhere in `root`. A swipe never presses the control
  // under the finger (the click the browser fires afterwards is swallowed).
  function swipe(root, onNext, onPrev) {
    if (!root) return;
    var t = { active: false, x: 0, y: 0, swiped: false, at: 0 };
    function pt(e) {
      var list = (e.touches && e.touches.length) ? e.touches : (e.changedTouches || []);
      return (list && list[0]) ? list[0] : null;
    }
    root.addEventListener('touchstart', function (e) {
      if (e.touches && e.touches.length > 1) { t.active = false; return; }
      var p = pt(e);
      if (!p) { t.active = false; return; }
      t.active = true; t.x = p.clientX; t.y = p.clientY;
    }, { passive: true });
    root.addEventListener('touchmove', function (e) {
      if (!t.active) return;
      var p = pt(e);
      if (!p) return;
      if (Math.abs(p.clientX - t.x) > 40 && Math.abs(p.clientX - t.x) > Math.abs(p.clientY - t.y)) t.swiped = true;
    }, { passive: true });
    root.addEventListener('touchend', function (e) {
      if (!t.active) return;
      t.active = false;
      var p = pt(e);
      if (!p) return;
      var dx = p.clientX - t.x, dy = p.clientY - t.y;
      if (Math.abs(dx) < 45 || Math.abs(dx) < Math.abs(dy)) { t.swiped = false; return; }
      t.swiped = true; t.at = Date.now();
      if (dx < 0) onNext(); else onPrev();
    }, { passive: true });
    root.addEventListener('touchcancel', function () { t.active = false; }, { passive: true });
    document.addEventListener('click', function (e) {
      if (t.swiped && (Date.now() - t.at) < 600) {
        t.swiped = false;
        e.stopPropagation();
        e.preventDefault();
      }
    }, true);
  }

  // Minimum press time for every button/link on the page: a finger that only
  // brushes the glass (or a swipe that ends on a control) must not press it.
  // Touch only, so desktop clicks and the jsdom tests are unaffected.
  var MIN_PRESS_MS = 250;   // deliberado: um toque acidental dura < 150 ms
  var press = { target: null, at: 0, touch: false };

  function clearPressMark() {
    if (press.target && press.target.classList) press.target.classList.remove('pressing');
  }

  function installTapGuard() {
    document.addEventListener('touchstart', function (e) {
      var el = e.target && e.target.closest ? e.target.closest('button, a, input[type=file], [data-mu-tap]') : null;
      clearPressMark();
      press.target = el;
      press.touch = !!el;
      press.at = Date.now();
      // visual feedback while the finger is down: without it a 250 ms gate
      // feels broken ("tap and nothing happens").
      if (el && el.classList) el.classList.add('pressing');
    }, true);
    document.addEventListener('touchend', clearPressMark, true);
    document.addEventListener('touchcancel', clearPressMark, true);
    document.addEventListener('click', function (e) {
      clearPressMark();
      if (!press.touch) return;
      var dt = Date.now() - press.at;
      press.touch = false;
      if (dt < MIN_PRESS_MS) {
        e.stopPropagation();
        e.preventDefault();
      }
    }, true);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', installTapGuard);
  else installTapGuard();

  return {
    t: t, tf: tf, get: get, post: post,
    fmtSize: fmtSize, fmtEta: fmtEta, msg: msg,
    arm: arm, armed: armed, disarm: disarm,
    strip: strip, stripActive: stripActive, swipe: swipe,
    minPressMs: MIN_PRESS_MS
  };
})();
</script>
)MCJS";
