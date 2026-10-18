// page_bricon_asset.h
// Bricon Rambox page — same look & tab structure as the Unikon RAMBOX page.
// Driver pending hardware tap: shows status + empty tables; actions answer
// honestly ("awaiting hardware") until bricon_protocol is wired.
#pragma once
#include <Arduino.h>

// NOTE: no ENABLE_BRICON guard here (twin of page_unikon_asset.h) — the
// routes TU gates usage; guarding the symbol breaks include order.
static const char PAGE_BRICON[] PROGMEM = R"rawliteral(
    <main class='page'>
        <style>
            .stat-card{background:#1e293b;border-radius:8px;padding:12px 10px;text-align:center;border:1px solid #334155}
            .stat-label{font-size:11px;color:#94a3b8;text-transform:uppercase;letter-spacing:.5px;margin-bottom:4px}
            .stat-value{font-size:22px;font-weight:700;color:#fbbf24}
            .uk-tabs{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0 14px}
            .uk-tab-btn{background:#1e293b;border:1px solid #334155;color:#cbd5e1;border-radius:8px;padding:9px 16px;font-size:13px;font-weight:700;cursor:pointer}
            .uk-tab-btn.active{background:#15803d;border-color:#4ade80;color:#fff}
            .uk-tab-panel{display:none}
            .uk-tab-panel.active{display:block}
            .uk-savebar{position:sticky;bottom:0;background:#0f172a;border-top:1px solid #334155;padding:12px;text-align:center;margin-top:14px}
            .checkbox-label{display:flex!important;align-items:center;gap:10px}
            .checkbox-label input[type=checkbox]{flex:none;width:18px;height:18px;margin:0}
        </style>
        <div class='wrap'>
            <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page'>&#127968; Home</a></div>
            <h1>&#128330; <span data-i18n='briconTitle'>Bricon Rambox</span></h1>

            <!-- Tabs (save bar at the bottom is common to all tabs) -->
            <div class='uk-tabs' id='briconTabBar'>
                <button type='button' class='uk-tab-btn active' onclick='bkSwitchTab(0)'><span data-i18n='briconTab1'>📊 Status & Config</span></button>
                <button type='button' class='uk-tab-btn' onclick='bkSwitchTab(1)'><span data-i18n='briconTab2'>🏁 Arrivals & Contests</span></button>
                <button type='button' class='uk-tab-btn' onclick='bkSwitchTab(2)'><span data-i18n='briconTab3'>🐦 Pigeon Loft</span></button>
                <button type='button' class='uk-tab-btn' onclick='bkSwitchTab(3)'><span data-i18n='briconTab4'>🏋️ Training</span></button>
            </div>

            <!-- Tab 1: Status + Configuration -->
            <div class='uk-tab-panel active' id='bk-tab-0'>
            <div id='bk-sec-status' class='section'>
                <h3>&#128200; <span data-i18n='briconStatus'>Status</span></h3>
                <div id='briconStatus' style='background:#1e293b;border-radius:6px;padding:14px;font-size:14px;color:#94a3b8;margin-bottom:14px' data-i18n='ukLoading'>Loading...</div>
                <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:8px;margin-bottom:14px'>
                    <div class='stat-card'><div class='stat-label' data-i18n='briconStatArrivals'>Arrivals</div><div class='stat-value' id='bkStatTotal'>--</div></div>
                    <div class='stat-card'><div class='stat-label' data-i18n='briconStatContests'>Contests</div><div class='stat-value' id='bkStatContests'>--</div></div>
                    <div class='stat-card'><div class='stat-label' data-i18n='briconStatLoft'>Loft</div><div class='stat-value' id='bkStatPombal'>--</div></div>
                    <div class='stat-card'><div class='stat-label' data-i18n='briconStatRx'>Last RX</div><div class='stat-value' id='bkStatAgo'>--</div></div>
                </div>
                <div style='display:flex;gap:8px;flex-wrap:wrap'>
                    <button onclick='briconPoll()' class='btn' style='flex:1;min-width:160px' data-i18n='briconBtnPoll'>&#128270; Update Arrivals</button>
                    <button onclick='briconReset()' class='btn' style='flex:1;min-width:160px;background:#374151;border-color:#374151' data-i18n='briconBtnReset'>&#128256; Reset</button>
                </div>
                <div id='bricon-poll-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#1e3a8a;border-left:4px solid #3b82f6;border-radius:6px;font-size:13px;color:#e0e7ff'></div>
            </div>

            <div id='bk-sec-config' class='section'>
                <h3>&#9881;&#65039; <span data-i18n='briconConfig'>Configuration</span></h3>
                <div id='bkRs232Info' style='background:#1e293b;border-radius:6px;padding:8px 14px;margin-bottom:14px;font-size:12px;color:#94a3b8;display:flex;align-items:center;gap:8px;flex-wrap:wrap'><span style='color:#60a5fa' data-i18n='ukRs232Active'>RS232 active:</span> <span id='bkRs232InfoText' data-i18n='ukRs232Loading'>loading...</span></div>
                <form id='briconForm' onsubmit='return false;'>
                    <div style='display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:16px'>
                        <label class='checkbox-label' style='background:#14532d'>
                            <input type='checkbox' id='briconEnabled' name='enabled'>
                            <span><strong data-i18n='briconModuleActive'>Module active</strong></span>
                        </label>
                    </div>
                    <div style='display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:16px'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='briconInterval'>&#9200; Polling (s, 0=manual):</label>
                        <select id='briconInterval' name='interval' style='max-width:150px'>
                            <option value='0' data-i18n='ukPollingManual'>Manual</option>
                            <option value='60'>60</option>
                            <option value='180' selected>180</option>
                            <option value='300'>300</option>
                            <option value='600'>600</option>
                            <option value='900'>900</option>
                            <option value='1200'>1200</option>
                            <option value='1800'>1800</option>
                            <option value='3600'>3600</option>
                        </select>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap;margin-left:12px'><span data-i18n='rs232BaudLbl'>Baud</span>:</label>
                        <select id='briconBaud' name='baud' style='max-width:150px'>
                            <option value='1200'>1200</option>
                            <option value='2400'>2400</option>
                            <option value='4800'>4800</option>
                            <option value='9600' selected>9600</option>
                            <option value='19200'>19200</option>
                            <option value='38400'>38400</option>
                            <option value='57600'>57600</option>
                            <option value='115200'>115200</option>
                        </select>
                    </div>
                </form>
                <div id='bricon-config-status'></div>
            </div>
            </div><!-- /bk-tab-0 -->

            <!-- Tab 2: Race Arrivals + Contests -->
            <div class='uk-tab-panel' id='bk-tab-1'>
            <div id='bk-sec-arrivals' class='section'>
                <h3>&#128330; <span data-i18n='briconArrivals'>Race Arrivals</span></h3>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='briconArrivalsHint'>Only records with valid time (HHMMSS) appear in this table.</p>
                <div class='table-wrapper'>
                    <table style='width:100%;border-collapse:collapse;font-size:13px'>
                        <thead><tr>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa'>#</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColRing'>Ring</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColChip'>Chip ETS</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColTime'>Time</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColDate'>Date</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColRace'>Race</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='bkColCat'>Cat</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='bkColSpeed'>Vel</th>
                        </tr></thead>
                        <tbody id='briconArrivalsBody'><tr><td colspan='8' style='padding:12px;color:#4b5563;text-align:center' data-i18n='briconNoArrivals'>No race arrivals</td></tr></tbody>
                    </table>
                </div>
            </div>

            <div id='bk-sec-contests' class='section'>
                <div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px'>
                    <h3 style='margin:0'>📅 <span data-i18n='briconContests'>Annual Contest Calendar</span></h3>
                    <button onclick='briconSyncCalendar()' class='btn' style='background:#1e3a8a;border-color:#1d4ed8;padding:8px 16px;font-size:12px' data-i18n='briconBtnSyncCalendar'>🗓️ Sync Calendar</button>
                </div>
                <div class='table-wrapper'>
                    <table style='width:100%;border-collapse:collapse;font-size:13px'>
                        <thead><tr>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa'>#</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColCode'>Code</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColDate'>Date</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColName'>Name</th>
                        </tr></thead>
                        <tbody id='briconContestsBody'><tr><td colspan='4' style='padding:12px;color:#4b5563;text-align:center' data-i18n='briconNoContests'>No contests read</td></tr></tbody>
                    </table>
                </div>
                <div id='bricon-calendar-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#1e3a8a;border-left:4px solid #3b82f6;border-radius:6px;font-size:13px;color:#e0e7ff'></div>
            </div>
            </div><!-- /bk-tab-1 -->

            <!-- Tab 3: Pigeon Loft -->
            <div class='uk-tab-panel' id='bk-tab-2'>
            <div id='bk-sec-pombal' class='section'>
                <div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px'>
                    <h3 style='margin:0' data-i18n='briconPombal'>🕊 Pigeon Loft</h3>
                    <button onclick='briconSyncPombal()' class='btn' style='background:linear-gradient(180deg,#22c55e,#16a34a);border-color:#4ade80;color:#fff;font-weight:800;text-shadow:0 1px 2px rgba(0,0,0,.55);padding:8px 16px;font-size:12px' data-i18n='briconBtnSyncPombal'>🔄 Sync Pigeon Loft</button>
                </div>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='briconPombalHint'>Registered pigeons (ring + chip) read from the Bricon loft.</p>
                <div class='table-wrapper'>
                    <table style='width:100%;border-collapse:collapse;font-size:13px'>
                        <thead><tr>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #15803d;color:#86efac'>#</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #15803d;color:#86efac' data-i18n='ukColRing'>Ring</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #15803d;color:#86efac' data-i18n='ukColChip'>Chip</th>
                        </tr></thead>
                        <tbody id='briconPombalBody'><tr><td colspan='3' style='padding:12px;color:#4b5563;text-align:center' data-i18n='briconNoPigeons'>No registered pigeons</td></tr></tbody>
                    </table>
                </div>
                <div id='bricon-pombal-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#16a34a;border-left:4px solid #22c55e;border-radius:6px;font-size:13px;color:#dcfce7'></div>
            </div>
            </div><!-- /bk-tab-2 -->

            <!-- Tab 4: Training -->
            <div class='uk-tab-panel' id='bk-tab-3'>
            <div id='bk-sec-training' class='section'>
                <div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px'>
                    <h3 style='margin:0' data-i18n='briconTraining'>🏋️ Training Session</h3>
                    <div style='display:flex;gap:8px;flex-wrap:wrap'>
                        <button onclick='briconSendTraining()' class='btn' style='background:#2563eb;border-color:#1d4ed8;padding:8px 16px;font-size:12px' data-i18n='briconBtnSendTraining'>📡 Send Training</button>
                        <button onclick='briconClearTraining()' class='btn' style='background:#991b1b;border-color:#ef4444;padding:8px 16px;font-size:12px' data-i18n='briconBtnClearTraining'>🗑️ Clear Training</button>
                    </div>
                </div>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='briconTrainingHint'>Writes a training session to the Bricon so it records training arrivals.</p>
                <div id='bricon-training-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#1e3a8a;border-left:4px solid #2563eb;border-radius:6px;font-size:13px;color:#bfdbfe'></div>
            </div>
            </div><!-- /bk-tab-3 -->

            <!-- Common save bar (all tabs) -->
            <div class='uk-savebar'>
                <button type='button' onclick='saveBriconConfig()' class='btn' style='padding:10px 24px' data-i18n='briconBtnSave'>&#128190; Save Configuration</button>
            </div>

            <div id='bricon-config-status' style='margin-top:15px;padding:12px;border-radius:6px;display:none'></div>
        </div>
    </main>
<script>
    var bkConfigLoaded = false;
    function bt(s, fb) { try { if (window._lang && window._lang[s] !== undefined) return window._lang[s]; var el = document.querySelector("[data-i18n='" + s + "']"); if (el && el.textContent && el.textContent.trim().length > 0 && el.textContent !== s) return el.textContent; } catch (e) {} return fb; }
    function besc(s) { s = (s === null || s === undefined) ? '' : String(s); return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }

    var bkTabs = [['bk-tab-0', 'Status & Config'], ['bk-tab-1', 'Arrivals & Contests'], ['bk-tab-2', 'Pigeon Loft'], ['bk-tab-3', 'Training']];
    var bkTabGroups = [['bk-sec-status', 'bk-sec-config'], ['bk-sec-arrivals', 'bk-sec-contests'], ['bk-sec-pombal'], ['bk-sec-training']];
    function bkSwitchTab(idx) {
        var btns = document.querySelectorAll('#briconTabBar .uk-tab-btn');
        for (var i = 0; i < btns.length; i++) btns[i].classList.toggle('active', i === idx);
        for (var p = 0; p < 4; p++) {
            var el = document.getElementById('bk-tab-' + p);
            if (el) el.classList.toggle('active', p === idx);
        }
        try { localStorage.setItem('bk_tab', String(idx)); } catch (e) {}
    }
    function bkShowTab(idx) { bkSwitchTab(idx); }
    function bkBuildTabs() {
        try {
            var s = parseInt(localStorage.getItem('bk_tab'), 10);
            if (s >= 0 && s < 4) bkSwitchTab(s);
        } catch (e) {}
    }

    function loadBriconStatus() {
        fetch('/api/bricon-status', {cache: 'no-store'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var el = document.getElementById('briconStatus');
                if (el) {
                    var badge = (d.connected)
                        ? '<span style="color:#4ade80;font-weight:700">' + besc(bt('ukStatusConnected', 'Connected')) + '</span>'
                        : '<span style="color:#f87171;font-weight:700">' + besc(bt('ukStatusDisconnected', 'Disconnected')) + '</span>';
                    var html = '<div style="margin-bottom:8px">' + badge + '</div>';
                    if (d.activeModule) html += '<div><strong>' + besc(bt('ukStatusModule', 'Module:')) + '</strong> ' + besc(d.activeModule) + '</div>';
                    if (d.hardwarePending) html += '<div>' + besc(bt('briconHardwarePending', 'Waiting for hardware (driver not connected yet).')) + '</div>';
                    html += '<div>' + besc(bt('briconRxBytes', 'RX bytes')) + ': ' + (d.bytesRx || 0) + ' &middot; ' + besc(bt('briconFrames', 'Frames')) + ': ' + (d.framesSeen || 0) + '</div>';
                    if (d.baud) html += '<div>' + d.baud + ' ' + besc(bt('rs232BaudLbl', 'Baud')) + '</div>';
                    el.innerHTML = html;
                }
                var ago = (d.lastRxAgoSec > 0) ? d.lastRxAgoSec + 's' : '--';
                var stAgo = document.getElementById('bkStatAgo');
                if (stAgo) stAgo.textContent = ago;
                if (!bkConfigLoaded) {
                    bkConfigLoaded = true;
                    var en = document.getElementById('briconEnabled');
                    if (en && d.enabled !== undefined) en.checked = !!d.enabled;
                    var iv = document.getElementById('briconInterval');
                    if (iv && d.intervalSec !== undefined) iv.value = String(d.intervalSec);
                    var bd = document.getElementById('briconBaud');
                    if (bd && d.baud) bd.value = String(d.baud);
                }
                loadBriconArrivals();
                loadBriconPombal();
                loadBriconContests();
                loadBkRs232Info();
            })
            .catch(function() {});
    }
    function briconCat(c) {
        return (c === 1) ? 'OLD' : (c === 2) ? 'YEA' : (c === 3) ? 'YOU' : '';
    }
    function briconRow(a, n) {
        var h = (a.time && a.time.length === 6 && a.time !== '000000')
            ? a.time.substring(0,2) + ':' + a.time.substring(2,4) + ':' + a.time.substring(4,6) : '-';
        var dt = (a.date && a.date.length >= 4 && a.date.substring(0,4) !== '0000')
            ? a.date.substring(0,2) + '/' + a.date.substring(2,4) : '-';
        var vel = (a.speed > 0) ? a.speed : '';
        return '<tr style="border-bottom:1px solid #1e293b">'
            + '<td style="padding:7px 10px;color:#64748b">' + n + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace">' + besc(a.ring) + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace">' + besc(a.chip) + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace;color:#4ade80">' + h + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace;color:#93c5fd">' + dt + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace;color:#fbbf24">' + besc(a.race || '') + '</td>'
            + '<td style="padding:7px 10px">' + briconCat(a.category) + '</td>'
            + '<td style="padding:7px 10px">' + vel + '</td></tr>';
    }
    function loadBriconArrivals() {
        fetch('/api/bricon-arrivals', {cache: 'no-store'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var tb = document.getElementById('briconArrivalsBody');
                if (!tb) return;
                var arr = (d && d.arrivals) ? d.arrivals : [];
                var st = document.getElementById('bkStatTotal');
                if (st) st.textContent = String(arr.length);
                if (arr.length === 0) { tb.innerHTML = '<tr><td colspan="8" style="padding:12px;color:#4b5563;text-align:center">' + bt('briconNoArrivals', 'No race arrivals') + '</td></tr>'; return; }
                var html = '';
                for (var i = 0; i < arr.length; i++) {
                    html += briconRow(arr[i], i + 1);
                }
                tb.innerHTML = html;
            })
            .catch(function() {});
    }
    function loadBriconPombal() {
        fetch('/api/bricon-pombal', {cache: 'no-store'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var tb = document.getElementById('briconPombalBody');
                if (!tb) return;
                var arr = (d && (d.pombal || d.registrations)) ? (d.pombal || d.registrations) : [];
                var st = document.getElementById('bkStatPombal');
                if (st) st.textContent = String(arr.length);
                if (arr.length === 0) { tb.innerHTML = '<tr><td colspan="3" style="padding:12px;color:#4b5563;text-align:center">' + bt('briconNoPigeons', 'No registered pigeons') + '</td></tr>'; return; }
                var html = '';
                for (var i = 0; i < arr.length; i++) {
                    html += '<tr><td style="padding:8px 10px;border-bottom:1px solid #1e293b;color:#cbd5e1">' + (i + 1) + '</td>';
                    html += '<td style="padding:8px 10px;border-bottom:1px solid #1e293b;font-family:monospace;color:#a5f3fc">' + besc(arr[i].ring) + '</td>';
                    html += '<td style="padding:8px 10px;border-bottom:1px solid #1e293b;font-family:monospace;color:#86efac">' + besc(arr[i].chip) + '</td></tr>';
                }
                tb.innerHTML = html;
            })
            .catch(function() {});
    }
    function loadBriconContests() {
        fetch('/api/bricon-contests', {cache: 'no-store'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var tb = document.getElementById('briconContestsBody');
                if (!tb) return;
                var arr = (d && d.contests) ? d.contests : [];
                var st = document.getElementById('bkStatContests');
                if (st) st.textContent = String(arr.length);
                if (arr.length === 0) { tb.innerHTML = '<tr><td colspan="4" style="padding:12px;color:#4b5563;text-align:center">' + bt('briconNoContests', 'No contests read') + '</td></tr>'; return; }
                var html = '';
                for (var i = 0; i < arr.length; i++) {
                    html += '<tr style="border-bottom:1px solid #1e293b">'
                        + '<td style="padding:7px 10px;color:#64748b">' + (i + 1) + '</td>'
                        + '<td style="padding:7px 10px;font-family:monospace">' + besc(arr[i].code) + '</td>'
                        + '<td style="padding:7px 10px;font-family:monospace;color:#93c5fd">' + besc(arr[i].date) + '</td>'
                        + '<td style="padding:7px 10px">' + besc(arr[i].name) + '</td></tr>';
                }
                tb.innerHTML = html;
            })
            .catch(function() {});
    }
    function briconSyncCalendar() {
        var el = document.getElementById('bricon-calendar-toast');
        fetch('/api/bricon-sync-calendar', { method: 'POST' })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                if (el) {
                    el.style.display = 'block';
                    el.textContent = (d && d.message) || '';
                    setTimeout(function() { el.style.display = 'none'; loadBriconContests(); loadBriconStatus(); }, 2000);
                }
            })
            .catch(function() {});
    }
    function briconSyncPombal() {
        var el = document.getElementById('bricon-pombal-toast');
        fetch('/api/bricon-sync-pombal', { method: 'POST' })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                if (el) {
                    el.style.display = 'block';
                    el.textContent = (d && d.message) || '';
                    setTimeout(function() { el.style.display = 'none'; loadBriconPombal(); loadBriconStatus(); }, 2000);
                }
            })
            .catch(function() {});
    }
    function briconPoll() {
        fetch('/api/bricon-poll', { method: 'POST' })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var el = document.getElementById('bricon-poll-toast');
                if (el) { el.style.display = 'block'; el.textContent = (d && d.message) || ''; }
                setTimeout(loadBriconStatus, 2000);
            })
            .catch(function() {});
    }
    function briconReset() {
        fetch('/api/bricon-reset', { method: 'POST' })
            .then(function(r) { return r.json(); })
            .then(function() { setTimeout(loadBriconStatus, 1000); })
            .catch(function() {});
    }
    function briconSendTraining() {
        fetch('/api/bricon-send-training', { method: 'POST' })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var el = document.getElementById('bricon-training-toast');
                if (el) { el.style.display = 'block'; el.textContent = (d && d.message) || ''; }
            })
            .catch(function() {});
    }
    function briconClearTraining() {
        fetch('/api/bricon-clear-training', { method: 'POST' })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var el = document.getElementById('bricon-training-toast');
                if (el) { el.style.display = 'block'; el.textContent = (d && d.message) || ''; }
            })
            .catch(function() {});
    }
    function saveBriconConfig() {
        var params = 'enabled=' + (document.getElementById('briconEnabled').checked ? '1' : '0');
        params += '&interval=' + document.getElementById('briconInterval').value;
        params += '&baud=' + document.getElementById('briconBaud').value;
        fetch('/api/bricon-config', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: params })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var el = document.getElementById('bricon-config-status');
                if (el) { el.style.display = 'block'; el.textContent = (d && d.message) || ''; }
                setTimeout(loadBriconStatus, 1000);
            })
            .catch(function() {});
    }

    function loadBkRs232Info() {
        fetch('/api/config?section=rs232', {cache: 'no-cache'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var el = document.getElementById('bkRs232InfoText');
                if (!el) return;
                var sc = (d && d.rs232) ? d.rs232 : {};
                var baudList = Array.isArray(sc.baudrate_list) ? sc.baudrate_list : [1200,2400,4800,9600,19200,38400,57600,115200];
                var baud = baudList[sc.baudrate_index !== undefined ? sc.baudrate_index : 3] || 9600;
                var db = sc.data_bits || 8;
                var par = (sc.parity || 'none').charAt(0).toUpperCase();
                var sb = sc.stop_bits || 1;
                var status = sc.enabled
                    ? '<span style="color:#4ade80">&#9679; ' + besc(bt('statusActive', 'active')) + '</span>'
                    : '<span style="color:#f87171">&#9679; ' + besc(bt('statusInactive', 'inactive')) + '</span>';
                el.innerHTML = '<strong style="color:#e0e7ff">' + baud + ' baud &middot; ' + db + par + sb + '</strong> &mdash; ' + status;
            })
            .catch(function() {});
    }

    bkBuildTabs();
    loadBriconStatus();
    setInterval(function() { loadBriconStatus(); }, 8000);
</script>
)rawliteral";
