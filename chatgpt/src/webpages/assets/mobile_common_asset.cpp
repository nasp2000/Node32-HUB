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
  .mp .tiles { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; }
  .mp .tile {
    display: flex; flex-direction: column; gap: 2px; padding: 8px 10px;
    border: 1px solid #1e293b; border-radius: 10px; background: #0f172a; min-width: 0;
  }
  .mp .tile .k { font-size: 10px; letter-spacing: .4px; text-transform: uppercase; color: #64748b; }
  .mp .tile b { font-size: 16px; color: #e6f1ff; word-break: break-all; }

  .mp .subbar { display: flex; gap: 6px; }
  .mp .subt {
    flex: 1 1 0; height: 32px; border-radius: 9px; border: 1px solid #334155;
    background: #0f172a; color: #94a3b8; font-size: 12px; font-weight: 700;
  }
  .mp .subt.on { border-color: #3b82f6; background: #1d2b47; color: #dbeafe; }
  .mp .pane { flex: 1 1 auto; min-height: 0; display: none; flex-direction: column; gap: 6px; }
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

  // Minimum press time for every button/link on the page: a finger that only
  // brushes the glass (or a swipe that ends on a control) must not press it.
  // Touch only, so desktop clicks and the jsdom tests are unaffected.
  var MIN_PRESS_MS = 180;
  var press = { target: null, at: 0, touch: false };

  function installTapGuard() {
    document.addEventListener('touchstart', function (e) {
      var el = e.target && e.target.closest ? e.target.closest('button, a, input[type=file], [data-mu-tap]') : null;
      press.target = el;
      press.touch = !!el;
      press.at = Date.now();
    }, true);
    document.addEventListener('click', function (e) {
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
    minPressMs: MIN_PRESS_MS
  };
})();
</script>
)MCJS";
