// page_bricon_asset.h
// Bricon Rambox page — same look & tab structure as the Unikon RAMBOX page.
// Wire driver implemented from the REA capture (framing/CK, activation scan,
// GetFancier -> GetPigeon chain -> GetTimer, GetFlightNames, GetBandTable).
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
                <button type='button' class='uk-tab-btn active' onclick='bkSwitchTab(0)'><span title='Bricon status and configuration' data-i18n-title='briconTipTabStatus' data-i18n='briconTab1'>📊 Status & Config</span></button>
                <button type='button' class='uk-tab-btn' onclick='bkSwitchTab(1)'><span title='Arrivals and contest list read from the Bricon' data-i18n-title='briconTipTabArrivals' data-i18n='briconTab2'>🏁 Arrivals & Contests</span></button>
                <button type='button' class='uk-tab-btn' onclick='bkSwitchTab(2)'><span title='Pigeon loft read from the Bricon (read-only)' data-i18n-title='briconTipTabLoft' data-i18n='briconTab3'>🐦 Pigeon Loft</span></button>
                <button type='button' class='uk-tab-btn' onclick='bkSwitchTab(3)'><span title='Write a training race to the Bricon' data-i18n-title='briconTipTabTraining' data-i18n='briconTab4'>🏋️ Training</span></button>
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
                    <button onclick='briconPoll()' class='btn' style='flex:1;min-width:160px' title='Poll the arrivals from the Bricon' data-i18n-title='briconTipPoll' data-i18n='briconBtnPoll'>&#128270; GetPigeon</button>
                    <button onclick='briconReset()' class='btn' style='flex:1;min-width:160px;background:#374151;border-color:#374151' title='Reset the arrival counters (does not touch the Bricon)' data-i18n-title='briconTipReset' data-i18n='briconBtnReset'>&#128256; Reset</button>
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
                    <div style='display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:16px'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='briconCountry'>Country (layout)</label>
                        <select id='briconCountry' name='country' style='max-width:110px'>
                            <option value='PT' selected>PT</option>
                            <option value='ES'>ES</option>
                            <option value='FR'>FR</option>
                            <option value='BE'>BE</option>
                            <option value='NL'>NL</option>
                            <option value='UK'>UK</option>
                            <option value='HU'>HU</option>
                            <option value='RO'>RO</option>
                            <option value='PL'>PL</option>
                        </select>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap;margin-left:12px' data-i18n='briconBaAddr'>Clock address</label>
                        <select id='briconBa' name='ba' style='max-width:190px'>
                            <option value='0' selected data-i18n='briconBaAuto'>auto-scan</option>
                            <option value='130'>0x82 Bricon/Little/DUO</option>
                            <option value='129'>0x81 Benzing</option>
                            <option value='131'>0x83 Tauris</option>
                            <option value='132'>0x84 Tipes</option>
                            <option value='133'>0x85 Unikon</option>
                            <option value='16'>0x10 Master</option>
                            <option value='0'>0x00 club</option>
                        </select>
                    </div>
                    <div style='display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:16px'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='briconDataBits'>Data</label>
                        <select id='briconData' name='data' style='max-width:80px'>
                            <option value='8' selected>8</option><option value='7'>7</option>
                        </select>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='briconParity'>Parity</label>
                        <select id='briconParity' name='parity' style='max-width:100px'>
                            <option value='none' selected>none</option><option value='even'>even</option><option value='odd'>odd</option>
                        </select>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='briconStopBits'>Stop</label>
                        <select id='briconStop' name='stop' style='max-width:80px'>
                            <option value='2' selected>2</option><option value='1'>1</option>
                        </select>
                        <span style='font-size:12px;color:#64748b' data-i18n='briconFramingHint'>Bricon uses 9600 8N2 (Unikon is 8N1).</span>
                    </div>
                    <div style='display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:16px'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='briconClubId'>Club code</label>
                        <input id='briconClub' name='club' type='text' maxlength='4' value='0000' style='max-width:80px;font-family:monospace'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='briconFlightId'>Flight code</label>
                        <input id='briconFlight' name='flight' type='text' maxlength='4' value='0000' style='max-width:80px;font-family:monospace'>
                        <span style='font-size:12px;color:#64748b' data-i18n='briconCodesHint'>Used in the GetPigeon request; 0000 is the wildcard.</span>
                    </div>
                </form>
                <div id='bricon-config-status'></div>
            </div>
            </div><!-- /bk-tab-0 -->

            <!-- Tab 2: Race Arrivals + Contests -->
            <div class='uk-tab-panel' id='bk-tab-1'>
            <div id='bk-sec-arrivals' class='section'>
                <h3>&#128330; <span data-i18n='briconArrivals'>Race Arrivals</span></h3>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='briconArrivalsHint'>Only records with a valid time appear. Columns are the RS-232 record fields: chip (ElBand), ring (FedBand), time, date, contest, position and evaluation.</p>
                <div class='table-wrapper'>
                    <table style='width:100%;border-collapse:collapse;font-size:13px'>
                        <thead><tr>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa'>#</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColRing'>Ring</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColChip'>Chip ETS</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColTime'>Time</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColDate'>Date</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColRace'>Race</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='bkColPos'>Pos</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='bkColEval'>Eval</th>
                        </tr></thead>
                        <tbody id='briconArrivalsBody'><tr><td colspan='8' style='padding:12px;color:#4b5563;text-align:center' data-i18n='briconNoArrivals'>No race arrivals</td></tr></tbody>
                    </table>
                </div>
            </div>

            <div id='bk-sec-contests' class='section'>
                <div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px'>
                    <h3 style='margin:0'>📅 <span data-i18n='briconContests'>Annual Contest Calendar</span></h3>
                    <button onclick='briconSyncCalendar()' class='btn' style='background:#1e3a8a;border-color:#1d4ed8;padding:8px 16px;font-size:12px' title='Read the contest calendar from the Bricon' data-i18n-title='briconTipSyncCal' data-i18n='briconBtnSyncCalendar'>🗓️ GetFlightNames</button>
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
                    <button onclick='briconSyncPombal()' class='btn' style='background:linear-gradient(180deg,#22c55e,#16a34a);border-color:#4ade80;color:#fff;font-weight:800;text-shadow:0 1px 2px rgba(0,0,0,.55);padding:8px 16px;font-size:12px' title='Read the pigeon loft from the Bricon' data-i18n-title='briconTipSyncPombal' data-i18n='briconBtnSyncPombal'>🔄 GetFancier</button>
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
                        <button onclick='briconSendTraining()' class='btn' style='background:#2563eb;border-color:#1d4ed8;padding:8px 16px;font-size:12px' title='SendDesignated: designate pigeons for the race (the captured Bricon write for allocating pigeons)' data-i18n-title='briconTipSendTraining' data-i18n='briconBtnSendTraining'>📡 Designate (SendDesignated)</button>
                    </div>
                </div>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='briconTrainingHint'>Bricon has no Unikon-style training session: StartReadout/StartBasket/StartTempReadout are unreferenced in BriconLib. The captured way to allocate pigeons is SendDesignated.</p>
                <div id='bricon-training-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#1e3a8a;border-left:4px solid #2563eb;border-radius:6px;font-size:13px;color:#bfdbfe'></div>
            </div>
            </div><!-- /bk-tab-3 -->

            <!-- Common save bar (all tabs) -->
            <div class='uk-savebar'>
                <button type='button' onclick='saveBriconConfig()' class='btn' style='padding:10px 24px' title='Save the Bricon configuration' data-i18n-title='briconTipSave' data-i18n='briconBtnSave'>&#128190; Save Configuration</button>
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
                    // REA-derived profile fields (framing + layout country + clock address)
                    var dt = document.getElementById('briconData');
                    if (dt && d.dataBits) dt.value = String(d.dataBits);
                    var pa = document.getElementById('briconParity');
                    if (pa && d.parity) pa.value = d.parity;
                    var sb = document.getElementById('briconStop');
                    if (sb && d.stopBits) sb.value = String(d.stopBits);
                    var cy = document.getElementById('briconCountry');
                    if (cy && d.country) cy.value = d.country;
                    var ba = document.getElementById('briconBa');
                    if (ba && d.baAddress !== undefined) ba.value = String(d.baAddress);
                    var cl = document.getElementById('briconClub');
                    if (cl && d.clubId) cl.value = d.clubId;
                    var fl2 = document.getElementById('briconFlight');
                    if (fl2 && d.flightId) fl2.value = d.flightId;
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
        // Only wire fields are shown: the RS-232 record carries Position1..4 and the
        // Evaluation byte; category/speed live in the club database, not on the wire.
        var pos = (a.posArriveClub > 0) ? a.posArriveClub : '';
        var ev = (a.evaluation === '1') ? '1' : (a.evaluation === '0') ? '0' : '';
        return '<tr style="border-bottom:1px solid #1e293b">'
            + '<td style="padding:7px 10px;color:#64748b">' + n + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace">' + besc(a.ring) + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace">' + besc(a.chip) + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace;color:#4ade80">' + h + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace;color:#93c5fd">' + dt + '</td>'
            + '<td style="padding:7px 10px;font-family:monospace;color:#fbbf24">' + besc(a.race || '') + '</td>'
            + '<td style="padding:7px 10px;color:#a5b4fc">' + pos + '</td>'
            + '<td style="padding:7px 10px;color:#f87171">' + ev + '</td></tr>';
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
    function saveBriconConfig() {
        var params = 'enabled=' + (document.getElementById('briconEnabled').checked ? '1' : '0');
        params += '&interval=' + document.getElementById('briconInterval').value;
        params += '&baud=' + document.getElementById('briconBaud').value;
        params += '&data=' + document.getElementById('briconData').value;
        params += '&parity=' + document.getElementById('briconParity').value;
        params += '&stop=' + document.getElementById('briconStop').value;
        params += '&country=' + document.getElementById('briconCountry').value;
        params += '&ba=' + document.getElementById('briconBa').value;
        params += '&club=' + document.getElementById('briconClub').value;
        params += '&flight=' + document.getElementById('briconFlight').value;
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
