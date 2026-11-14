#include "page_rambox_mobile_asset.h"
#include "../../feature_flags.h"

// Rambox32 phone page — four screens, no page scroll, all vertical.
//
//   1 Status    — clock (Bricon/Unikon) + RS232 + arrivals summary, poll button.
//   2 Arrivals  — the rings the clock has read (newest first) + reset.
//   3 Command   — one RS232 line with quick template chips.
//   4 System    — the shared System screen (summary/OTA/log/restart).
//
// Same standard as the CNC/CAM pages: shared CSS/JS + System block, header
// strip, arrows/swipe, 250 ms tap guard.
#if ENABLE_UNIKON == 1 || ENABLE_BRICON == 1
const char PAGE_RAMBOX_MOBILE[] PROGMEM = R"RBMOBILE(
<div id="gm" class="mp">
  <header>
    <nav class="navstrip" id="gmNavStrip">
      <button type="button" class="ncol on" data-step-go="1"><span class="ntab" data-i18n="rbNavStatus">Status</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="2"><span class="ntab" data-i18n="rbNavArrivals">Arrivals</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="3"><span class="ntab" data-i18n="rbNavCfg">Config</span><i class="nrail"></i></button>
      <button type="button" class="ncol" data-step-go="4"><span class="ntab" data-i18n="rbNavSys">System</span><i class="nrail"></i></button>
    </nav>
    <div class="hrow">
      <span class="pack" id="gmPack"></span>
      <h1 id="gmTitle" class="sr" data-i18n="rbTitleStatus">Status</h1>
      <span class="badge" id="gmLink">--</span>
    </div>
  </header>

  <!-- ── 1: status ───────────────────────────────────────────────── -->
  <section class="screen on" id="gmS1">
    <div class="tiles">
      <div class="tile"><span class="k" data-i18n="rbClock">Clock</span><b id="rbClock">--</b></div>
      <div class="tile"><span class="k" data-i18n="rbFancier">Fancier</span><b id="rbFancier">--</b></div>
      <div class="tile"><span class="k" data-i18n="rbRace">Race</span><b id="rbRace">--</b></div>
      <div class="tile"><span class="k" data-i18n="rbArrivalsN">Arrivals</span><b id="rbCount">--</b></div>
      <div class="tile"><span class="k">RS232</span><b id="rbRs">--</b></div>
      <div class="tile"><span class="k" data-i18n="rbLastPoll">Last poll</span><b id="rbPoll">--</b></div>
    </div>
    <div class="chips">
      <button type="button" class="chip" id="rbSync" data-i18n="rbSync">SYNC CLOCK</button>
    </div>
  </section>

  <!-- ── 2: clock data (arrivals / pombal / races) ───────────────── -->
  <section class="screen fill" id="gmS2">
    <div class="subbar">
      <button type="button" class="subt on" id="rbTabArr" data-arr="arr" data-i18n="rbArrivals">Arrivals</button>
      <button type="button" class="subt" id="rbTabPombal" data-arr="pombal" data-i18n="rbPombal">Pombal</button>
      <button type="button" class="subt" id="rbTabRaces" data-arr="races" data-i18n="rbRaces">Races</button>
    </div>
    <div class="rowhdr">
      <span class="dir" id="rbArrTitle" data-i18n="rbArrivals">Arrivals</span>
      <button type="button" class="mini" id="rbReload" data-i18n-title="rbReload" title="Reload">&#8635;</button>
      <button type="button" class="mini" id="rbEmail" data-i18n-title="rbEmailResend" title="Resend the email">&#9993;</button>
      <button type="button" class="mini" id="rbReset" data-i18n-title="rbReset" title="Clear">&#10005;</button>
    </div>
    <div class="list" id="rbList"></div>
  </section>

  <!-- ── 3: config (clock + rs232) ───────────────────────────────── -->
  <section class="screen" id="gmS3">
    <div class="rowhdr"><span class="dir" data-i18n="rbCfgTitle">Clock and RS232</span></div>
    <div class="chips" id="rbClocks">
      <button type="button" class="chip" data-clock="bricon">BRICON</button>
      <button type="button" class="chip" data-clock="unikon">UNIKON</button>
    </div>
    <div class="opt"><span data-i18n="rbClockOn">Clock polling</span>
      <div class="chips" style="flex:1 1 auto">
        <button type="button" class="chip" id="rbPollOn" data-i18n="rbOn">ON</button>
        <button type="button" class="chip" id="rbPollOff" data-i18n="rbOff">OFF</button>
      </div>
    </div>
    <label class="opt"><span data-i18n="rbInterval">Every</span><select id="rbInterval"></select></label>
    <div class="opt"><span data-i18n="rbSerial">Serial</span>
      <div class="chips" style="flex:1 1 auto">
        <button type="button" class="chip" id="rbSerialClock" data-i18n="rbSerialClock">CLOCK</button>
        <button type="button" class="chip" id="rbSerialManual" data-i18n="rbSerialManual">MANUAL</button>
      </div>
    </div>
    <div class="note" id="rbSerialHint">--</div>
    <label class="opt"><span>RS232</span>
      <div class="chips" style="flex:1 1 auto">
        <button type="button" class="chip" id="rbRsOn" data-i18n="rbOn">ON</button>
        <button type="button" class="chip" id="rbRsOff" data-i18n="rbOff">OFF</button>
      </div>
    </label>
    <label class="opt"><span data-i18n="rbBaud">Baud</span><select id="rbBaud"></select></label>
    <label class="opt"><span data-i18n="rbParity">Parity</span><select id="rbParity">
      <option value="none">none</option><option value="even">even</option><option value="odd">odd</option>
    </select></label>
    <label class="opt"><span data-i18n="rbBits">Data / stop</span>
      <div class="chips" style="flex:1 1 auto">
        <select id="rbData"><option value="8">8</option><option value="7">7</option></select>
        <select id="rbStop"><option value="1">1</option><option value="2">2</option></select>
      </div>
    </label>
  </section>

)RBMOBILE";

// Tail: message bar, progress, primary action, footer navigation and the script.
// The shared System screen is printed between HEAD and TAIL.
const char PAGE_RAMBOX_MOBILE_TAIL[] PROGMEM = R"RBTAIL(  <div class="msg" id="gmMsg" data-i18n="rbHint">Clock status, arrivals and RS232 commands.</div>
  <div class="bar" id="gmBar"><span id="gmFill"></span></div>

  <div class="acts">
    <button type="button" class="primary" id="gmPrimary" data-i18n="rbPollNow">POLL NOW</button>
  </div>

  <div class="nav">
    <button type="button" class="arrow back" id="gmBack" disabled title="Back">&#8592;</button>
    <span class="no" id="gmNo">1 / 4</span>
    <button type="button" class="arrow next" id="gmNext" data-i18n-title="gmNextTip">&#8594;</button>
  </div>
</div>

<script>
(function () {
  'use strict';
  var STEPS = 4;
  var STEP_STATUS = 1, STEP_ARR = 2, STEP_CFG = 3, STEP_SYS = 4;

  var el = {
    title: document.getElementById('gmTitle'),
    navStrip: document.getElementById('gmNavStrip'),
    screens: [document.getElementById('gmS1'), document.getElementById('gmS2'),
              document.getElementById('gmS3'), document.getElementById('gmSYS')],
    pack: document.getElementById('gmPack'),
    link: document.getElementById('gmLink'),
    clock: document.getElementById('rbClock'),
    fancier: document.getElementById('rbFancier'),
    race: document.getElementById('rbRace'),
    count: document.getElementById('rbCount'),
    rs: document.getElementById('rbRs'),
    poll: document.getElementById('rbPoll'),
    sync: document.getElementById('rbSync'),
    clocks: document.getElementById('rbClocks'),
    pollOn: document.getElementById('rbPollOn'),
    pollOff: document.getElementById('rbPollOff'),
    interval: document.getElementById('rbInterval'),
    serialClock: document.getElementById('rbSerialClock'),
    serialManual: document.getElementById('rbSerialManual'),
    serialHint: document.getElementById('rbSerialHint'),
    rsOn: document.getElementById('rbRsOn'),
    rsOff: document.getElementById('rbRsOff'),
    baud: document.getElementById('rbBaud'),
    parity: document.getElementById('rbParity'),
    data: document.getElementById('rbData'),
    stop: document.getElementById('rbStop'),
    arrTitle: document.getElementById('rbArrTitle'),
    list: document.getElementById('rbList'),
    tabArr: document.getElementById('rbTabArr'),
    tabPombal: document.getElementById('rbTabPombal'),
    tabRaces: document.getElementById('rbTabRaces'),
    reload: document.getElementById('rbReload'),
    email: document.getElementById('rbEmail'),
    reset: document.getElementById('rbReset'),
    msg: document.getElementById('gmMsg'),
    bar: document.getElementById('gmBar'),
    fill: document.getElementById('gmFill'),
    primary: document.getElementById('gmPrimary'),
    acts: document.querySelector('#gm .acts'),
    back: document.getElementById('gmBack'),
    next: document.getElementById('gmNext'),
    no: document.getElementById('gmNo')
  };

  // The pack can carry a Bricon clock, a Unikon clock or both: the page asks
  // Bricon first and falls back to Unikon (same JSON shape) when it 404s.
  var st = { step: 1, busy: false, arrivals: [], status: null, rs: null, timer: 0,
             clock: 'bricon', clocks: { bricon: false, unikon: false },
             tab: 'arr', pombal: [], races: [], intervalSec: 15, rsEnabled: true,
             manualSerial: false };

  function t(k, fb) { return window.MU ? MU.t(k, fb) : (fb !== undefined ? fb : k); }
  function tf(k, fb, v) { return window.MU ? MU.tf(k, fb, v) : (fb !== undefined ? fb : k); }
  function msg(text, kind) { if (window.MU) MU.msg(el.msg, text, kind); }
  function set(node, txt) { if (node) node.textContent = (txt === undefined || txt === null || txt === '') ? '--' : String(txt); }

  function linkBadge(on, text) {
    if (!el.link) return;
    el.link.textContent = text;
    el.link.className = 'badge' + (on ? ' ok' : ' warn');
  }

  function clockGet(path) {
    var url = '/api/' + st.clock + '-' + path;
    return MU.get(url).then(function (res) {
      if (res.status === 200 && res.body && (res.body.connected !== undefined || res.body.count !== undefined ||
          res.body.enabled !== undefined || res.body.arrivals !== undefined)) {
        return res;
      }
      if (st.clock === 'bricon') {
        st.clock = 'unikon';
        return MU.get('/api/unikon-' + path);
      }
      return res;
    }).catch(function () {
      if (st.clock === 'bricon') {
        st.clock = 'unikon';
        return MU.get('/api/unikon-' + path).catch(function () { return { status: 0, body: {} }; });
      }
      return { status: 0, body: {} };
    });
  }

  function loadStatus() {
    if (!window.MU) return;
    Promise.all([
      MU.get('/api/bricon-status').catch(function () { return { status: 0, body: {} }; }),
      MU.get('/api/unikon-status').catch(function () { return { status: 0, body: {} }; }),
      MU.get('/api/rs232-status').catch(function () { return { status: 0, body: {} }; })
    ]).then(function (r) {
      var br = r[0].body || {}, un = r[1].body || {}, rs = r[2].body || {};
      st.clocks.bricon = (br.enabled === true) || (br.connected === true);
      st.clocks.unikon = (un.enabled === true) || (un.connected === true);
      st.clock = st.clocks.unikon && !st.clocks.bricon ? 'unikon' : 'bricon';
      var c = (st.clock === 'unikon') ? un : br;
      st.status = c;
      st.rs = rs;
      if (el.clocks) {
        var chips = el.clocks.querySelectorAll('button[data-clock]');
        for (var i = 0; i < chips.length; i++) {
          var name = String(chips[i].getAttribute('data-clock'));
          chips[i].className = 'chip' + (st.clocks[name] ? ' on' : '');
          chips[i].disabled = st.busy;
        }
      }
      set(el.clock, c.connected ? t('rbConnected', 'connected')
                                : (c.hardwarePending ? t('rbPending', 'hardware pending')
                                                     : t('rbOffline', 'offline')));
      set(el.fancier, c.fancierName || c.clubId);
      set(el.race, c.flightId || c.race);
      set(el.count, c.lastNewCount !== undefined ? String(c.lastNewCount) : '--');
      set(el.rs, rs.enabled ? (String(rs.baudrate || '') + ' 8N1') : t('rbDisabled', 'disabled'));
      set(el.poll, c.lastPollAgoSec !== undefined ? (String(c.lastPollAgoSec) + ' s') : '--');
      linkBadge(!!c.connected, t(c.connected ? 'gmOnline' : 'gmOffline', c.connected ? 'online' : 'offline'));
      render();
    }).catch(function () { linkBadge(false, t('gmOffline', 'offline')); });
  }

  function setArrTab(tab) {
    st.tab = (tab === 'pombal' || tab === 'races') ? tab : 'arr';
    if (el.tabArr) el.tabArr.className = 'subt' + (st.tab === 'arr' ? ' on' : '');
    if (el.tabPombal) el.tabPombal.className = 'subt' + (st.tab === 'pombal' ? ' on' : '');
    if (el.tabRaces) el.tabRaces.className = 'subt' + (st.tab === 'races' ? ' on' : '');
    if (st.tab === 'arr') loadArrivals();
    else if (st.tab === 'pombal') loadPombal();
    else loadRaces();
  }

  function renderRows(rows, title) {
    if (!el.list || typeof document === 'undefined' || !document) return;
    el.list.innerHTML = '';
    if (el.arrTitle) el.arrTitle.textContent = title;
    if (!rows.length) {
      var none = document.createElement('div');
      none.className = 'note';
      none.textContent = t('rbEmpty', 'Nothing to show yet.');
      el.list.appendChild(none);
      return;
    }
    for (var i = 0; i < rows.length; i++) {
      var row = document.createElement('div');
      row.className = 'row';
      var n = document.createElement('span');
      n.className = 'n';
      n.textContent = String(rows[i][0] || '--');
      var s2 = document.createElement('span');
      s2.className = 's';
      s2.textContent = String(rows[i][1] || '');
      row.appendChild(n);
      row.appendChild(s2);
      el.list.appendChild(row);
    }
  }

  function loadPombal() {
    if (!window.MU) return;
    return clockGet('pombal').then(function (r) {
      var d = r.body || {};
      var list = Array.isArray(d.registrations) ? d.registrations : [];
      var rows = [];
      for (var i = 0; i < list.length; i++) rows.push([list[i].ring, list[i].chip || '']);
      renderRows(rows, t('rbPombal', 'Pombal') + ' (' + rows.length + ')');
    }).catch(function () { renderRows([], t('rbPombal', 'Pombal')); });
  }

  function loadRaces() {
    if (!window.MU) return;
    return clockGet('contests').then(function (r) {
      var d = r.body || {};
      var list = Array.isArray(d.contests) ? d.contests : [];
      var rows = [];
      for (var i = 0; i < list.length; i++) {
        var c = list[i];
        rows.push([c.name || c.code, [c.date, (c.distance ? c.distance + ' km' : ''), (c.pigeonCount || 0) + ' pombos']
          .filter(function (x) { return !!x; }).join(' · ')]);
      }
      renderRows(rows, t('rbRaces', 'Races') + ' (' + rows.length + ')');
    }).catch(function () { renderRows([], t('rbRaces', 'Races')); });
  }

  function renderArrivals() {
    if (!el.list || typeof document === 'undefined' || !document) return;
    el.list.innerHTML = '';
    if (!st.arrivals.length) {
      var none = document.createElement('div');
      none.className = 'note';
      none.textContent = t('rbNoArrivals', 'No arrivals yet.');
      el.list.appendChild(none);
      return;
    }
    for (var i = 0; i < st.arrivals.length; i++) {
      var a = st.arrivals[i] || {};
      var row = document.createElement('div');
      row.className = 'row';
      var n = document.createElement('span');
      n.className = 'n';
      n.textContent = String(a.ring || '--');
      var s = document.createElement('span');
      s.className = 's';
      s.textContent = String(a.time || '') + ' · ' + String(a.race || a.kind || '');
      row.appendChild(n);
      row.appendChild(s);
      el.list.appendChild(row);
    }
  }

  function loadArrivals() {
    if (!window.MU) return;
    return clockGet('arrivals').then(function (r) {
      var d = r.body || {};
      st.arrivals = Array.isArray(d.arrivals) ? d.arrivals : [];
      if (el.arrTitle && st.tab === 'arr') {
        el.arrTitle.textContent = t('rbArrivals', 'Arrivals') + ' (' + String(d.count !== undefined ? d.count : st.arrivals.length) + ')';
      }
      renderArrivals();
    }).catch(function () { st.arrivals = []; renderArrivals(); });
  }

  // Only one clock can be active at a time (shared RS232): enabling one turns
  // the other off in the same request pair.
  function switchClock(name) {
    if (st.busy || (name !== 'bricon' && name !== 'unikon')) return false;
    var other = (name === 'bricon') ? 'unikon' : 'bricon';
    var run = function () {
      st.busy = true; render();
      msg(tf('rbSwitching', 'Switching to {name}\u2026', { name: name.toUpperCase() }), '');
      return Promise.all([
        MU.post('/api/' + name + '-config', { enabled: 1 }),
        MU.post('/api/' + other + '-config', { enabled: 0 })
      ]).then(function () {
        st.busy = false;
        st.clock = name;
        msg(tf('rbSwitched', '{name} is the active clock.', { name: name.toUpperCase() }), 'ok');
        loadStatus();
        return true;
      }).catch(function () {
        st.busy = false; render();
        msg(t('rbFail', 'The device refused the request.'), 'err');
        return false;
      });
    };
    if (typeof window.customConfirm === 'function') {
      window.customConfirm(tf('rbSwitchConfirm', 'Make {name} the active clock? The other one is turned off.',
                              { name: name.toUpperCase() }), function (ok) { if (ok) run(); });
      return true;
    }
    if (MU.arm('clock:' + name)) run();
    return true;
  }

  // The clock already emails the arrivals/report on its own; this asks for a
  // resend (only the Unikon side exposes it today).
  function resendEmail() {
    if (st.busy || !window.MU) return false;
    st.busy = true; render();
    msg(t('rbEmailSending', 'Asking for the email\u2026'), '');
    return MU.post('/api/' + st.clock + '-email-pombal', {}).then(function (res) {
      st.busy = false;
      var ok = !!(res.body && res.body.success === true);
      msg(ok ? t('rbEmailQueued', 'Email queued to be resent.')
             : t('rbEmailUnavailable', 'The active clock has no email resend.'),
          ok ? 'ok' : 'err');
      render();
      return ok;
    }).catch(function () {
      st.busy = false; render();
      msg(t('rbEmailUnavailable', 'The active clock has no email resend.'), 'err');
      return false;
    });
  }

  function action(kind) {
    if (st.busy) return false;
    var url = kind === 'poll' ? ('/api/' + st.clock + '-poll')
            : kind === 'sync' ? ('/api/' + st.clock + '-sync-calendar')
            : ('/api/' + st.clock + '-reset');
    st.busy = true; render();
    msg(t(kind === 'poll' ? 'rbPolling' : kind === 'reset' ? 'rbResetting' : 'rbSending',
          kind === 'poll' ? 'Polling the clock…' : kind === 'reset' ? 'Clearing…' : 'Sending…'), '');
    return MU.post(url, {}).then(function (res) {
      st.busy = false;
      var ok = !!(res.body && (res.body.success !== false));
      msg(ok ? t(kind === 'poll' ? 'rbPollOk' : kind === 'reset' ? 'rbResetOk' : 'rbSent',
                 kind === 'poll' ? 'Poll requested.' : kind === 'reset' ? 'Arrivals cleared.' : 'Command queued.')
             : t('rbFail', 'The device refused the request.'), ok ? 'ok' : 'err');
      loadStatus(); loadArrivals();
      render();
      return ok;
    }).catch(function () {
      st.busy = false; render();
      msg(t('rbFail', 'The device refused the request.'), 'err');
      return false;
    });
  }

  var INTERVALS = [5, 10, 15, 30, 60, 120];
  var BAUDS = [9600, 19200, 38400, 57600, 115200];

  function fillSelect(node, values, current) {
    if (!node) return;
    node.innerHTML = '';
    for (var i = 0; i < values.length; i++) {
      var o = document.createElement('option');
      o.value = String(values[i]);
      o.textContent = String(values[i]);
      node.appendChild(o);
    }
    if (current !== undefined) node.value = String(current);
  }

  function paintToggle(onBtn, offBtn, on) {
    if (onBtn) onBtn.className = 'chip' + (on ? ' on' : '');
    if (offBtn) offBtn.className = 'chip' + (on ? '' : ' on');
  }

  // Each clock has its own serial profile (Bricon 9600 8N2, Unikon 9600 8N1) and
  // applies it when enabled; `manual_serial` makes it use the RS232 page values.
  function clockProfile(clock) {
    return (clock === 'unikon') ? { baud: 9600, data: 8, parity: 'none', stop: 1 }
                                : { baud: 9600, data: 8, parity: 'none', stop: 2 };
  }

  function paintSerial() {
    var manual = !!st.manualSerial;
    if (el.serialClock) el.serialClock.className = 'chip' + (manual ? '' : ' on');
    if (el.serialManual) el.serialManual.className = 'chip' + (manual ? ' on' : '');
    if (el.serialHint) {
      var prof = clockProfile(st.clock);
      el.serialHint.textContent = manual
        ? t('rbSerialManualHint', 'Uses the RS232 settings below.')
        : tf('rbSerialClockHint', '{name} profile: {baud} 8{bp}', {
            name: st.clock.toUpperCase(), baud: String(prof.baud), bp: (prof.parity === 'none' ? 'N' : prof.parity.charAt(0).toUpperCase()) + String(prof.stop)
          });
    }
    var locked = !manual;
    [el.baud, el.parity, el.data, el.stop].forEach(function (node) {
      if (node) node.disabled = locked;
    });
    if (locked) {
      var pr = clockProfile(st.clock);
      if (el.baud) el.baud.value = String(pr.baud);
      if (el.parity) el.parity.value = pr.parity;
      if (el.data) el.data.value = String(pr.data);
      if (el.stop) el.stop.value = String(pr.stop);
    }
  }

  function loadConfig() {
    if (!window.MU) return;
    return Promise.all([
      MU.get('/api/bricon-status').catch(function () { return { body: {} }; }),
      MU.get('/api/unikon-status').catch(function () { return { body: {} }; }),
      MU.get('/api/rs232-status').catch(function () { return { body: {} }; })
    ]).then(function (r) {
      var br = r[0].body || {}, un = r[1].body || {}, rs = r[2].body || {};
      st.clocks.bricon = (br.enabled === true);
      st.clocks.unikon = (un.enabled === true);
      st.clock = st.clocks.unikon && !st.clocks.bricon ? 'unikon' : 'bricon';
      var act = (st.clock === 'unikon') ? un : br;
      st.intervalSec = Number(act.intervalSec) || 15;
      st.rsEnabled = (rs.enabled !== false);
      st.manualSerial = (act.manualSerial === true);

      if (el.clocks) {
        var chips = el.clocks.querySelectorAll('button[data-clock]');
        for (var i = 0; i < chips.length; i++) {
          var nm = String(chips[i].getAttribute('data-clock'));
          chips[i].className = 'chip' + (st.clocks[nm] ? ' on' : '');
        }
      }
      paintToggle(el.pollOn, el.pollOff, !!(br.enabled || un.enabled));
      paintToggle(el.rsOn, el.rsOff, st.rsEnabled);
      fillSelect(el.interval, INTERVALS, st.intervalSec);
      fillSelect(el.baud, BAUDS, Number(rs.baudrate) || 9600);
      if (el.parity) el.parity.value = String(rs.parity || 'none');
      if (el.data) el.data.value = String(rs.data_bits || 8);
      if (el.stop) el.stop.value = String(rs.stop_bits || 1);
      paintSerial();
      render();
    }).catch(function () {});
  }

  function saveConfig() {
    if (st.busy || !window.MU) return false;
    var onChip = el.clocks ? el.clocks.querySelector('.chip.on') : null;
    var clock = onChip ? String(onChip.getAttribute('data-clock')) : st.clock;
    var other = (clock === 'bricon') ? 'unikon' : 'bricon';
    var clockOn = el.pollOn ? el.pollOn.className.indexOf('on') >= 0 : true;
    var interval = el.interval ? el.interval.value : st.intervalSec;
    var rsOn = el.rsOn ? el.rsOn.className.indexOf('on') >= 0 : true;
    st.busy = true; render();
    msg(t('rbSaving', 'Saving\u2026'), '');
    var manual = !!st.manualSerial;
    var jobs = [
      MU.post('/api/' + clock + '-config', {
        enabled: clockOn ? 1 : 0, interval: interval, manual: manual ? 1 : 0
      }),
      MU.post('/api/' + other + '-config', { enabled: 0 })
    ];
    if (manual) {
      jobs.push(MU.post('/api/config?section=rs232', {
        rs232_enabled: rsOn ? 1 : 0,
        baudrate: el.baud ? el.baud.value : 9600,
        parity: el.parity ? el.parity.value : 'none',
        data_bits: el.data ? el.data.value : 8,
        stop_bits: el.stop ? el.stop.value : 1
      }));
    }
    return Promise.all(jobs).then(function () {
      st.busy = false;
      msg(t('rbSaved', 'Settings saved.'), 'ok');
      loadConfig();
      loadStatus();
      return true;
    }).catch(function () {
      st.busy = false; render();
      msg(t('rbFail', 'The device refused the request.'), 'err');
      return false;
    });
  }

  function setStep(n) {
    var next = Math.max(1, Math.min(STEPS, Number(n) || 1));
    st.step = next;
    el.screens.forEach(function (sec, i) {
      if (sec) sec.className = 'screen' + (i + 1 === st.step ? ' on' : '') + (i === 1 ? ' fill' : '');
    });
    el.no.textContent = st.step + ' / ' + STEPS;
    if (window.MU && MU.stripActive) MU.stripActive(el.navStrip, st.step);
    if (window.MPSystem) {
      if (st.step === STEP_SYS) MPSystem.open();
      else MPSystem.close();
    }
    if (st.step === STEP_STATUS) loadStatus();
    if (st.step === STEP_ARR) setArrTab(st.tab);
    if (st.step === STEP_CFG) loadConfig();
    try { history.replaceState(null, '', '#' + st.step); } catch (e) {}
    render();
    return st.step;
  }

  function goNext() { if (st.step < STEPS) setStep(st.step + 1); }
  function goPrev() { if (st.step > 1) setStep(st.step - 1); }

  function render() {
    if (typeof document === 'undefined' || !document) return;
    var is1 = st.step === STEP_STATUS, is2 = st.step === STEP_ARR, is3 = st.step === STEP_CFG;
    el.title.textContent = t(is1 ? 'rbTitleStatus' : is2 ? 'rbTitleArrivals' : is3 ? 'rbTitleCfg' : 'gmTitleSys',
                             is1 ? 'Status' : is2 ? 'Arrivals' : is3 ? 'Config' : 'System');
    el.primary.className = 'primary' + ((is1 || is3) ? '' : ' off');
    el.primary.textContent = is3 ? t('rbSave', 'SAVE') : t('rbPollNow', 'POLL NOW');
    el.primary.disabled = st.busy || !(is1 || is3);
    if (el.acts) el.acts.style.display = (is1 || is3) ? 'flex' : 'none';
    if (el.sync) el.sync.disabled = st.busy;
    el.back.disabled = st.busy || st.step === 1;
    el.next.disabled = st.busy || st.step === STEPS;
  }

  function init() {
    if (el.pack && window.PACK_NAME) el.pack.textContent = String(window.PACK_NAME);
    setStep(parseInt(String(location.hash).replace('#', ''), 10) || 1);
    loadStatus();
    if (!st.timer) {
      st.timer = setInterval(function () { if (st.step === STEP_STATUS) loadStatus(); }, 3000);
    }
    el.primary.addEventListener('click', function () {
      if (st.step === STEP_CFG) saveConfig();
      else if (st.step === STEP_STATUS) action('poll');
    });
    if (el.sync) el.sync.addEventListener('click', function () { action('sync'); });
    [el.pollOn, el.pollOff].forEach(function (b) { if (b) b.addEventListener('click', function () { paintToggle(el.pollOn, el.pollOff, b === el.pollOn); }); });
    [el.serialClock, el.serialManual].forEach(function (b) {
      if (!b) return;
      b.addEventListener('click', function () {
        st.manualSerial = (b === el.serialManual);
        paintSerial();
      });
    });
    [el.rsOn, el.rsOff].forEach(function (b) { if (b) b.addEventListener('click', function () { paintToggle(el.rsOn, el.rsOff, b === el.rsOn); }); });
    if (el.clocks) {
      el.clocks.addEventListener('click', function (ev) {
        var b = ev.target && ev.target.closest ? ev.target.closest('button[data-clock]') : null;
        if (b && !b.disabled) {
          var chips = el.clocks.querySelectorAll('button[data-clock]');
          for (var k = 0; k < chips.length; k++) {
            chips[k].className = 'chip' + (chips[k] === b ? ' on' : '');
          }
        }
      });
    }
    if (el.reload) el.reload.addEventListener('click', function () { setArrTab(st.tab); });
    [el.tabArr, el.tabPombal, el.tabRaces].forEach(function (b) {
      if (b) b.addEventListener('click', function () { setArrTab(String(b.getAttribute('data-arr'))); });
    });
    if (el.email) el.email.addEventListener('click', resendEmail);
    if (el.reset) el.reset.addEventListener('click', function () {
      if (typeof window.customConfirm === 'function') {
        window.customConfirm(t('rbResetConfirm', 'Clear the arrivals list?'), function (ok) {
          if (ok) action('reset');
        });
      } else if (MU.arm('reset')) { action('reset'); }
    });
    el.back.addEventListener('click', function () { if (!el.back.disabled) goPrev(); });
    el.next.addEventListener('click', function () { if (!el.next.disabled) goNext(); });
    if (window.MU) {
      MU.strip(el.navStrip, setStep);
      MU.swipe(document.getElementById('gm'), goNext, goPrev);
    }
  }

  window.__test__ = window.__test__ || {};
  window.__test__.rbInit = init;
  window.__test__.rbSetStep = setStep;
  window.__test__.rbStatus = loadStatus;
  window.__test__.rbArrivals = loadArrivals;
  window.__test__.rbAction = action;
  window.__test__.rbClock = switchClock;
  window.__test__.rbConfig = loadConfig;
  window.__test__.rbSave = saveConfig;
  window.__test__.rbTab = setArrTab;
  window.__test__.rbEmail = resendEmail;
  window.__test__.rbState = st;

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
</script>
)RBTAIL";
#endif  // ENABLE_UNIKON || ENABLE_BRICON
