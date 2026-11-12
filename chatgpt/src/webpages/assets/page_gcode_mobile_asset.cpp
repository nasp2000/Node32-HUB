#include "page_gcode_mobile_asset.h"

// Mobile G-code sender — wizard, step 1 of 3 ("Send G-code").
//
// Vertical-only, no page scroll: one action per screen, the arrow advances
// (steps 2 "Frame" and 3 "Play" are separate pages, not built yet, so the
// arrow stays disabled here and the step counter says 1/3).
//
// Step 1 does exactly one thing: put a job on the controller -- pick a file on
// the phone and upload it to the PSRAM workspace via /api/grbl/psram-upload
// (the same endpoint the PC page uses). Starting the job is step 3.
//
// The PC page (/grbl-controller) is untouched by design: this is a separate
// asset, renderer and route.
const char PAGE_GCODE_MOBILE[] PROGMEM = R"GMOBILE(
<style>
  /* No scroll anywhere on this page: the wizard is one screen. */
  html, body { height: 100%; margin: 0; overflow: hidden; background: #0b1220; }
  body { color: #e6f1ff; font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  #gm {
    position: fixed; inset: 0; display: flex; flex-direction: column;
    gap: 10px; padding: 12px 14px calc(12px + env(safe-area-inset-bottom));
    box-sizing: border-box; height: 100dvh; overflow: hidden;
  }
  #gm header { flex: 0 0 auto; display: flex; align-items: center; gap: 10px; }
  #gm .steps { display: flex; gap: 6px; }
  #gm .steps i { width: 22px; height: 4px; border-radius: 2px; background: #24344d; display: block; }
  #gm .steps i.on { background: #3b82f6; }
  #gm h1 { font-size: 16px; margin: 0; font-weight: 700; letter-spacing: .2px; }
  #gm .badge {
    margin-left: auto; font-size: 11px; padding: 3px 8px; border-radius: 999px;
    border: 1px solid #334155; color: #94a3b8; white-space: nowrap;
  }
  #gm .badge.ok { border-color: #10b981; color: #6ee7b7; }
  #gm .badge.warn { border-color: #f59e0b; color: #fcd34d; }
  /* The pick zone takes all the leftover height, so the page can never need a scrollbar. */
  #gm .pick {
    flex: 1 1 auto; min-height: 0; width: 100%; display: flex; flex-direction: column;
    align-items: center; justify-content: center; gap: 8px; cursor: pointer;
    border: 2px dashed #3b82f6; border-radius: 14px; background: #0f172a;
    color: #dbeafe; font-size: 15px; padding: 16px; text-align: center; box-sizing: border-box;
  }
  #gm .pick:active { background: #14213a; }
  #gm .pick .ico { font-size: 40px; line-height: 1; }
  #gm .pick .nm { font-weight: 700; word-break: break-all; max-height: 3.2em; overflow: hidden; }
  #gm .pick .sz { font-size: 12px; color: #94a3b8; }
  #gm .msg { flex: 0 0 auto; font-size: 12px; line-height: 1.35; color: #94a3b8; min-height: 2.7em; }
  #gm .msg.err { color: #fca5a5; }
  #gm .msg.ok { color: #6ee7b7; }
  #gm .bar { flex: 0 0 auto; height: 6px; border-radius: 3px; background: #1e293b; overflow: hidden; display: none; }
  #gm .bar.on { display: block; }
  #gm .bar span { display: block; height: 100%; width: 0%; background: #3b82f6; transition: width .15s linear; }
  #gm .send {
    flex: 0 0 auto; width: 100%; height: 54px; border: 0; border-radius: 12px;
    background: #2563eb; color: #fff; font-size: 17px; font-weight: 700;
  }
  #gm .send:disabled { background: #1e293b; color: #64748b; }
  #gm .nav { flex: 0 0 auto; display: flex; align-items: center; gap: 10px; }
  #gm .nav .no { font-size: 12px; color: #94a3b8; }
  #gm .nav .nxt {
    margin-left: auto; width: 46px; height: 46px; border-radius: 50%;
    border: 1px solid #334155; background: #0f172a; color: #64748b; font-size: 20px;
  }
  #gm .nav .nxt:disabled { opacity: .55; }
  #gm .nav .cap { font-size: 11px; color: #64748b; }
  @media (max-height: 560px) {
    #gm h1 { font-size: 14px; }
    #gm .pick .ico { font-size: 28px; }
    #gm .send { height: 46px; font-size: 15px; }
  }
</style>

<div id="gm">
  <header>
    <div class="steps"><i class="on"></i><i></i><i></i></div>
    <h1>Send G-code</h1>
    <span class="badge" id="gmLink">--</span>
  </header>

  <button type="button" class="pick" id="gmPick">
    <span class="ico">&#128196;</span>
    <span class="nm" id="gmName">Choose a file</span>
    <span class="sz" id="gmSize">.gcode &middot; .nc &middot; .txt</span>
  </button>
  <input type="file" id="gmFile" accept=".gcode,.nc,.ngc,.txt,text/plain" hidden>

  <div class="msg" id="gmMsg">The file is stored on the controller; starting is step 3.</div>
  <div class="bar" id="gmBar"><span id="gmFill"></span></div>

  <button type="button" class="send" id="gmSend" disabled>SEND</button>

  <div class="nav">
    <span class="no" id="gmNo">1 / 3</span>
    <span class="cap">2 &middot; Frame</span>
    <button type="button" class="nxt" id="gmNext" disabled title="Next step: frame">&#8594;</button>
  </div>
</div>

<script>
(function () {
  'use strict';
  var MAX_BYTES = 4 * 1024 * 1024;   // same cap as the controller's PSRAM workspace
  var STORE_KEY = 'gm.sid';
  var el = {
    pick: document.getElementById('gmPick'),
    file: document.getElementById('gmFile'),
    name: document.getElementById('gmName'),
    size: document.getElementById('gmSize'),
    msg: document.getElementById('gmMsg'),
    bar: document.getElementById('gmBar'),
    fill: document.getElementById('gmFill'),
    send: document.getElementById('gmSend'),
    link: document.getElementById('gmLink')
  };
  var st = { sid: '', token: '', canWrite: true, file: null, busy: false, ready: false };

  function sessionId() {
    if (st.sid) return st.sid;
    var v = '';
    try { v = sessionStorage.getItem(STORE_KEY) || ''; } catch (e) {}
    if (!v) {
      v = 'm-' + String(Date.now()) + '-' + String(Math.floor(Math.random() * 1000000));
      try { sessionStorage.setItem(STORE_KEY, v); } catch (e2) {}
    }
    st.sid = v;
    return v;
  }

  function cleanName(n) {
    var s = String(n || '').replace(/^.*[\\\/]/, '').replace(/[^\w.\-]/g, '_');
    if (!s) s = 'job.nc';
    if (!/\.[a-z0-9]+$/i.test(s)) s += '.nc';
    return s.slice(0, 48);
  }

  function fmtBytes(b) {
    b = Number(b) || 0;
    if (b < 1024) return b + ' B';
    if (b < 1024 * 1024) return (b / 1024).toFixed(b < 10240 ? 1 : 0) + ' kB';
    return (b / (1024 * 1024)).toFixed(1) + ' MB';
  }

  function setMsg(text, kind) {
    el.msg.textContent = text || '';
    el.msg.className = 'msg' + (kind === 'err' ? ' err' : kind === 'ok' ? ' ok' : '');
  }

  function errorText(code) {
    var map = {
      file_too_large: 'File is larger than 4 MB.',
      upload_empty: 'The file is empty.',
      psram_store_failed: 'The controller could not store the file.',
      vfs_budget_exceeded: 'No space left on the controller.',
      alloc_failed: 'Not enough memory on the controller.',
      no_control: 'Another client holds control. Close the PC page and retry.',
      network: 'Network error. Try again.'
    };
    return map[code] || ('Error: ' + code);
  }

  function refreshSend() {
    el.send.disabled = !(st.file && st.canWrite && !st.busy);
  }

  function setFile(f) {
    st.file = f || null;
    st.ready = false;
    if (!f) {
      el.name.textContent = 'Choose a file';
      el.size.textContent = '.gcode \u00b7 .nc \u00b7 .txt';
    } else {
      el.name.textContent = cleanName(f.name);
      el.size.textContent = fmtBytes(f.size);
    }
    refreshSend();
    return !!f;
  }

  function progress(pct) {
    var on = pct > 0 && pct < 100;
    el.bar.className = 'bar' + (on ? ' on' : '');
    el.fill.style.width = Math.max(0, Math.min(100, pct)) + '%';
  }

  function send() {
    if (!st.file || st.busy) return false;
    if (!st.canWrite) { setMsg(errorText('no_control'), 'err'); return false; }
    if (st.file.size > MAX_BYTES) { setMsg(errorText('file_too_large'), 'err'); return false; }
    var name = cleanName(st.file.name);
    var query = 'name=' + encodeURIComponent(name) + '&sid=' + encodeURIComponent(sessionId());
    if (st.token) query += '&token=' + encodeURIComponent(st.token);
    var xhr = new XMLHttpRequest();
    st.busy = true; refreshSend();
    setMsg('Uploading ' + name + '\u2026', '');
    progress(1);
    xhr.open('POST', '/api/grbl/psram-upload?' + query, true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-Filename', name);
    xhr.setRequestHeader('X-Filesize', String(st.file.size || 0));
    xhr.upload.onprogress = function (e) {
      if (e && e.lengthComputable && e.total) progress(Math.round((e.loaded / e.total) * 100));
    };
    xhr.onload = function () {
      st.busy = false;
      var d = null;
      try { d = JSON.parse(xhr.responseText || '{}'); } catch (e) { d = null; }
      if (xhr.status === 200 && d && d.success === true) {
        st.ready = true;
        progress(100);
        el.bar.className = 'bar';
        setMsg('Job ready: ' + (d.file_path || name) + ' (' + fmtBytes(d.bytes) + ')', 'ok');
      } else {
        progress(0);
        setMsg(errorText((d && d.message) ? String(d.message) : ('http_' + xhr.status)), 'err');
      }
      refreshSend();
    };
    xhr.onerror = function () {
      st.busy = false; progress(0);
      setMsg(errorText('network'), 'err');
      refreshSend();
    };
    xhr.send(st.file);
    return true;
  }

  function linkBadge(on, text) {
    el.link.textContent = text;
    el.link.className = 'badge' + (on ? ' ok' : ' warn');
  }

  function init() {
    sessionId();
    refreshSend();
    // Control token: without it the controller refuses writes. One shot is
    // enough for step 1 (subsequent steps re-issue it).
    fetch('/api/grbl/control-token?sid=' + encodeURIComponent(st.sid), { cache: 'no-store' })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (d && d.success === true) {
          st.token = String(d.token || '');
          st.canWrite = (d.can_write !== false);
          if (!st.canWrite) setMsg(errorText('no_control'), 'err');
        }
        refreshSend();
      })
      .catch(function () { refreshSend(); });
    // One-shot machine state (no polling on this screen).
    fetch('/api/grbl/status', { cache: 'no-store' })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        if (d && d.usb_running) linkBadge(true, 'online');
        else linkBadge(false, 'offline');
      })
      .catch(function () { linkBadge(false, 'offline'); });

    el.pick.addEventListener('click', function () { el.file.click(); });
    el.file.addEventListener('change', function () {
      var f = el.file.files && el.file.files[0];
      setFile(f || null);
      if (f && f.size > MAX_BYTES) setMsg(errorText('file_too_large'), 'err');
      else if (f) setMsg('Ready to send.', '');
    });
    el.send.addEventListener('click', send);
    // The arrow is the wizard's advance; steps 2 and 3 are separate pages.
    el.next = document.getElementById('gmNext');
    el.next.addEventListener('click', function () {
      if (el.next.disabled) return;
    });
  }

  window.__test__ = window.__test__ || {};
  window.__test__.gmInit = init;
  window.__test__.gmSend = send;
  window.__test__.gmSetFile = setFile;
  window.__test__.gmCleanName = cleanName;
  window.__test__.gmFmtBytes = fmtBytes;
  window.__test__.gmErrorText = errorText;
  window.__test__.gmState = st;
  window.__test__.gmLinkBadge = linkBadge;

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
</script>
)GMOBILE";
