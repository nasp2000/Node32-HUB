// page_unikon_asset.h
// PROGMEM HTML fragment for the Unikon RAMBOX configuration and monitoring page.
// Does NOT include DOCTYPE/html/head/body — the shell renderer adds those.

#pragma once
#include <pgmspace.h>

static const char PAGE_UNIKON[] PROGMEM = R"rawliteral(
    <main class='page'>
        <style>
            .stat-card{background:#1e293b;border-radius:8px;padding:12px 10px;text-align:center;border:1px solid #334155}
            .stat-label{font-size:11px;color:#94a3b8;text-transform:uppercase;letter-spacing:.5px;margin-bottom:4px}
            .stat-value{font-size:22px;font-weight:700;color:#fbbf24}
            .uk-tabs{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0 14px}
            .uk-tab-btn{background:#1e293b;border:1px solid #334155;color:#cbd5e1;border-radius:8px;padding:9px 16px;font-size:13px;font-weight:700;cursor:pointer}
            .uk-tab-btn.active{background:#1d4ed8;border-color:#60a5fa;color:#fff}
            .uk-tab-panel{display:none}
            .uk-tab-panel.active{display:block}
            .uk-savebar{position:sticky;bottom:0;background:#0f172a;border-top:1px solid #334155;padding:12px;text-align:center;margin-top:14px}
            .checkbox-label{display:flex!important;align-items:center;gap:10px}
            .checkbox-label input[type=checkbox]{flex:none;width:18px;height:18px;margin:0}
        </style>
        <div class='wrap'>
            <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='unikonTipHome'>&#127968; Home</a></div>
            <h1>&#128330; Unikon RAMBOX</h1>

            <!-- Tabs (save bar at the bottom is common to all tabs) -->
            <div class='uk-tabs' id='ukTabs'>
                <button type='button' class='uk-tab-btn active' onclick='ukSwitchTab(0)' data-i18n='ukTab1'>📊 Status & Race Arrivals</button>
                <button type='button' class='uk-tab-btn' onclick='ukSwitchTab(1)' data-i18n='ukTab2'>📅 Calendar & Pigeon Loft</button>
                <button type='button' class='uk-tab-btn' onclick='ukSwitchTab(2)' data-i18n='ukTab3'>🏋️ Training Season</button>
                <button type='button' class='uk-tab-btn' onclick='ukSwitchTab(3)' data-i18n='ukTab4'>🔌 Connection & Configuration</button>
            </div>

            <!-- Tab 1: Status + Race Arrivals -->
            <div class='uk-tab-panel active' id='uk-tab-0'>
            <!-- Section 1: Status -->
            <div id='uk-sec-status' class='section'>
                <h3>&#128200; <span data-i18n='ukStatus'>Status</span></h3>
                <div id='unikonStatus' style='background:#1e293b;border-radius:6px;padding:14px;font-size:14px;color:#94a3b8;margin-bottom:14px' data-i18n='ukLoading'>Loading...</div>
                <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:8px;margin-bottom:14px'>
                    <div class='stat-card'><div class='stat-label' data-i18n='ukStatTimed'>Timed</div><div class='stat-value' id='statTotal'>--</div></div>
                    <div class='stat-card'><div class='stat-label' data-i18n='ukStatRace'>Races</div><div class='stat-value' id='statRace'>--</div></div>
                    <div class='stat-card'><div class='stat-label' data-i18n='ukStatNew'>New arrivals</div><div class='stat-value' id='statNew'>--</div></div>
                    <div class='stat-card'><div class='stat-label' data-i18n='ukStatLastPoll'>Last poll arrivals</div><div class='stat-value' id='statAgo'>--</div></div>
                </div>
                <div style='display:flex;gap:8px;flex-wrap:wrap'>
                    <button onclick='pollNow()' class='btn' title='Poll arrivals from RAMBOX' data-i18n-title='unikonTipPoll' style='flex:1;min-width:160px' data-i18n='ukBtnPollArrivals'>&#128270; Update Arrivals (W)</button>
                    <button onclick='resetState()' class='btn' title='Reset arrival counters' data-i18n-title='unikonTipReset' style='flex:1;min-width:160px;background:#374151;border-color:#374151' data-i18n='ukBtnResetCount'>&#128256; Reset Count</button>
                </div>
                <div id='poll-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#1e3a8a;border-left:4px solid #3b82f6;border-radius:6px;font-size:13px;color:#e0e7ff'></div>
            </div>

            <!-- Section 2: Arrivals table -->
            <div id='uk-sec-arrivals' class='section'>
                <h3>&#128330; <span data-i18n='ukArrivals'>Race Arrivals</span></h3>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='ukArrivalsHint'>Only records with valid time (HHMMSS) appear in this table.</p>
                <div class='table-wrapper'>
                    <table style='width:100%;border-collapse:collapse;font-size:13px'>
                        <thead><tr>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa'>#</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColRing'>Ring</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColChip'>Chip ETS</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColTime'>Time</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColDate'>Date</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColRace'>Race</th>
                        </tr></thead>
                        <tbody id='raceArrivalsBody'><tr><td colspan='6' style='padding:12px;color:#4b5563;text-align:center' data-i18n='ukNoArrivals'>No race arrivals</td></tr></tbody>
                    </table>
                </div>
            </div>
            </div><!-- /uk-tab-0 -->

            <!-- Tab 2: Sync Calendar + Season Pigeon Loft -->
            <div class='uk-tab-panel' id='uk-tab-1'>
            <!-- Section 3: Contest schedule (calendar) -->
            <div id='uk-sec-contests' class='section'>
                <div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px'>
                    <h3 style='margin:0'>📅 <span data-i18n='ukContests'>Annual Contest Calendar (IC)</span></h3>
                    <button onclick='syncCalendar()' class='btn' title='Sync contest calendar from RAMBOX' data-i18n-title='unikonTipSyncCal' style='background:#1e3a8a;border-color:#1d4ed8;padding:8px 16px;font-size:12px' data-i18n='ukBtnSyncCalendar'>🗓️ Sync Calendar</button>
                </div>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='ukContestsHint'>Read from session IC0/I of the original RAMBOX (this data only).</p>
                <div class='table-wrapper'>
                    <table style='width:100%;border-collapse:collapse;font-size:13px'>
                        <thead><tr>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa'>#</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColCode'>Code</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColDate'>Date</th>
                            <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ukColName'>Name</th>
                        </tr></thead>
                        <tbody id='contestsBody'><tr><td colspan='4' style='padding:12px;color:#4b5563;text-align:center' data-i18n='ukNoContests'>No contests read</td></tr></tbody>
                    </table>
                </div>
                <div id='calendar-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#1e3a8a;border-left:4px solid #3b82f6;border-radius:6px;font-size:13px;color:#e0e7ff'></div>
            </div>

            <!-- Section 3.5: Pombal/Registrations (seasonal pigeons) -->
            <div id='uk-sec-pombal' class='section'>
                <div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px'>
                    <h3 style='margin:0' data-i18n='ukPombalTitle'>🐦 Season Pigeon Loft (X-phase)</h3>
                    <div style='display:flex;gap:8px;flex-wrap:wrap'>
                        <button onclick='syncPombal()' class='btn' title='Sync pigeon loft registrations' data-i18n-title='unikonTipSyncPombal' style='background:linear-gradient(180deg,#22c55e,#16a34a);border-color:#4ade80;color:#fff;font-weight:800;text-shadow:0 1px 2px rgba(0,0,0,.55);padding:8px 16px;font-size:12px' data-i18n='ukBtnSyncPombal'>🔄 Sync Pigeon Loft</button>
                        <button id='btnEmailPombal' onclick='emailPombal()' class='btn' title='Email pigeon loft data' data-i18n-title='unikonTipEmailPombal' style='background:linear-gradient(180deg,#38bdf8,#0284c7);border-color:#7dd3fc;color:#fff;font-weight:800;text-shadow:0 1px 2px rgba(0,0,0,.55);padding:8px 16px;font-size:12px' data-i18n='ukBtnEmailPombal'>✉️ Email Pigeon Loft</button>
                    </div>
                </div>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='ukPombalHint'>Registered pigeons (ring + chip) read from session X (ZP frames) of the RAMBOX.</p>
                <details id='pombalListToggle' style='background:#0b1220;border:1px solid #1e293b;border-radius:8px;padding:10px 12px'>
                    <summary style='cursor:pointer;color:#93c5fd;font-weight:600' data-i18n='ukPombalListTitle'>📋 View pigeon loft list</summary>
                    <div class='table-wrapper'>
                        <table style='width:100%;border-collapse:collapse;font-size:13px'>
                            <thead><tr>
                                <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #15803d;color:#86efac'>#</th>
                                <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #15803d;color:#86efac' data-i18n='ukColRing'>Ring</th>
                                <th style='text-align:left;padding:8px 10px;border-bottom:1px solid #15803d;color:#86efac' data-i18n='ukColChip'>Chip</th>
                            </tr></thead>
                            <tbody id='pombalBody'><tr><td colspan='3' style='padding:12px;color:#4b5563;text-align:center' data-i18n='ukNoPigeons'>No registered pigeons</td></tr></tbody>
                        </table>
                    </div>
                </details>
                <div id='pombal-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#16a34a;border-left:4px solid #22c55e;border-radius:6px;font-size:13px;color:#dcfce7'></div>
            </div>
            </div><!-- /uk-tab-1 -->

            <!-- Tab 3: Training Season -->
            <div class='uk-tab-panel' id='uk-tab-2'>
            <!-- Section 3.6: Training (sends fictitious race 990 + pombal ZPs) -->
            <div id='uk-sec-training' class='section'>
                <div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;flex-wrap:wrap;gap:8px'>
                    <h3 style='margin:0' data-i18n='ukTrainingTitle'>🏋️ Training Session</h3>
                    <div style='display:flex;gap:8px;flex-wrap:wrap'>
                        <button onclick='sendTraining()' class='btn' title='Write training contest to RAMBOX' data-i18n-title='unikonTipSendTraining' style='background:#2563eb;border-color:#1d4ed8;padding:8px 16px;font-size:12px' data-i18n='ukBtnSendTraining'>📡 Send Training</button>
                        <button onclick='clearTraining()' class='btn' title='Remove training from RAMBOX' data-i18n-title='unikonTipClearTraining' style='background:#991b1b;border-color:#ef4444;padding:8px 16px;font-size:12px' data-i18n='ukBtnClearTraining'>🗑️ Clear Training</button>
                    </div>
                </div>
                <p style='font-size:13px;color:#94a3b8;margin:0 0 12px 0' data-i18n='ukTrainingHint'>Writes training contest 990 (IC0 + I-frame + S1/S2/S3 + pombal ZPs) to the RAMBOX so it records training arrivals. Uses the pigeons synced in the pombal.</p>
                <div style='font-size:12px;color:#94a3b8;margin-bottom:6px'><span data-i18n='ukTrainingSelect'>Pigeons for the training:</span> <span data-i18n='ukTrainingSelectAll'>No selection = whole loft.</span>
                <a href='#' onclick='return trainingCheckAll(true)' style='color:#60a5fa;margin-left:8px'>Todos</a> |
                <a href='#' onclick='return trainingCheckAll(false)' style='color:#60a5fa'>Nenhum</a></div>
                <div id='trainingPombalSelect' style='max-height:180px;overflow-y:auto;background:#0f172a;border:1px solid #334155;border-radius:6px;padding:6px;font-family:monospace;font-size:12px'></div>
                <div id='training-toast' style='display:none;margin-top:10px;padding:10px 14px;background:#1e3a8a;border-left:4px solid #2563eb;border-radius:6px;font-size:13px;color:#bfdbfe'></div>
            </div>
            </div><!-- /uk-tab-2 -->

            <!-- Tab 4: Connection + Configuration -->
            <div class='uk-tab-panel' id='uk-tab-3'>
            <!-- Section 3.5: Connection Recommendation -->
            <div id='uk-sec-guide' class='section'>
                <h3 data-i18n='ukConnTitle'>&#9432; Connection Recommendation</h3>
                <div style='font-size:12px;color:#cbd5e1;line-height:1.6'>
                    <div style='margin-bottom:8px'><strong data-i18n='ukConnJumper'>Internal jumper (RAMBOX side DB9 hood): Pin 9 → Pin 5 (GND)</strong></div>
                    <div style='margin-bottom:8px'><strong data-i18n='ukConnSpeed'>Speed: 9600 baud, 8N1 (RAMBOX default)</strong></div>
                </div>
            </div>

            <!-- Section 4: Configuration -->
            <div id='uk-sec-config' class='section'>
                <h3>&#9881;&#65039; <span data-i18n='ukConfig'>Configuration</span></h3>
                <div id='rs232Info' style='background:#1e293b;border-radius:6px;padding:8px 14px;margin-bottom:14px;font-size:12px;color:#94a3b8;display:flex;align-items:center;gap:8px;flex-wrap:wrap'><span style='color:#60a5fa' data-i18n='ukRs232Active'>RS232 active:</span> <span id='rs232InfoText' data-i18n='ukRs232Loading'>loading...</span><a href='/rs232' style='margin-left:auto;font-size:11px;color:#60a5fa' data-i18n='ukRs232Configure'>configure</a></div>
                <form id='unikonForm'>
                    <div style='display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:16px'>
                        <label class='checkbox-label' style='background:#1e3a8a'>
                            <input type='checkbox' id='enabled' name='enabled' title='Enable Unikon module' data-i18n-title='unikonTipEnabled'>
                            <span><strong data-i18n='ukModuleActive'>Module active — connects RS-232 to RAMBOX</strong></span>
                        </label>
                    </div>
                    <div style='display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:16px'>
                        <label class='checkbox-label'>
                            <input type='checkbox' id='official_format' name='official_format' checked title='Show ring/chip in official format' data-i18n-title='unikonTipOfficial'>
                            <span><strong data-i18n='ukOfficialFormat'>Columbophilia format — shows ring/chip in official format and sends the same via email</strong></span>
                        </label>
                    </div>
                    <div style='background:#1e3a8a;border-radius:6px;padding:12px;margin-bottom:16px;display:flex;align-items:center;gap:10px;flex-wrap:wrap'>
                        <span style='font-size:13px;color:#93c5fd' data-i18n='ukEmailRegistrations'>Registration email (races):</span>
                        <strong id='emailDestDisplay' style='color:#e0e7ff;font-family:monospace;font-size:13px' data-i18n='ukEmailNotConfigured'>(not configured)</strong>
                        <a href='/email' style='font-size:12px;color:#60a5fa;margin-left:auto' data-i18n='ukEmailConfigure'>Configure</a>
                    </div>
                    <div style='display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:16px'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='ukPollingInterval'>&#9200; Polling Interval:</label>
                        <select id='interval' name='interval' title='Polling interval' data-i18n-title='unikonTipInterval' style='max-width:150px'>
                            <option value='0' data-i18n='ukPollingManual'>Manual</option>
                            <option value='60' data-i18n='ukPolling1min'>1 minute</option>
                            <option value='180' selected data-i18n='ukPolling3min'>3 minutes</option>
                            <option value='300' data-i18n='ukPolling5min'>5 minutes</option>
                            <option value='600' data-i18n='ukPolling10min'>10 minutes</option>
                            <option value='900' data-i18n='ukPolling15min'>15 minutes</option>
                            <option value='1200' data-i18n='ukPolling20min'>20 minutes</option>
                            <option value='1800' data-i18n='ukPolling30min'>30 minutes</option>
                            <option value='2700' data-i18n='ukPolling45min'>45 minutes</option>
                            <option value='3600' data-i18n='ukPolling60min'>60 minutes</option>
                            <option value='7200' data-i18n='ukPolling120min'>120 minutes</option>
                        </select>
                    </div>
                    <div style='text-align:center'>
                    </div>
                </form>
            </div>
            </div><!-- /uk-tab-3 -->

            <!-- Common save bar (all tabs) -->
            <div class='uk-savebar'>
                <button type='button' onclick='saveUnikonConfig()' class='btn' title='Save Unikon config' data-i18n-title='unikonTipSave' style='padding:10px 24px' data-i18n='ukBtnSaveConfig'>&#128190; Save Configuration</button>
            </div>

            <div id='status' style='margin-top:15px;padding:12px;border-radius:6px;display:none'></div>
        </div>
    </main>

    <script>
    var configLoaded = false;
    var emailPombalPending = false;

    function t(key, fb) {
        try {
            if (window._lang && window._lang[key] !== undefined) return window._lang[key];
        } catch (e) {}
        var el = document.querySelector('[data-i18n="' + key + '"]');
        if (el) return el.textContent || key;
        return (fb !== undefined) ? fb : key;
    }

    function escapeHtml(value) {
        return String(value || '')
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    }

    function loadStatus() {
        fetch('/api/unikon-status',{cache:'no-store'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var el = document.getElementById('unikonStatus');
                var badge = d.connected
                    ? '<span style="color:#4ade80;font-weight:700">' + t('ukStatusConnected') + '</span>'
                    : '<span style="color:#f87171;font-weight:700">' + t('ukStatusDisconnected') + '</span>';
                var html = '<div style="margin-bottom:8px">' + badge + '</div>';
                if (d.moduleId) html += '<div><strong>' + t('ukStatusModule') + '</strong> ' + escapeHtml(d.moduleId) + '</div>';
                if (d.clubName)  html += '<div><strong>' + t('ukStatusClub') + '</strong> '  + escapeHtml(d.clubName)  + '</div>';
                if (d.clubCode) html += '<div><strong>' + t('ukStatusClubCode') + '</strong> ' + escapeHtml(d.clubCode) + '</div>';
                if (d.fancierName) html += '<div><strong>' + t('ukStatusFancier') + '</strong> ' + escapeHtml(d.fancierName) + '</div>';
                if (d.soltaName) html += '<div><strong>' + t('ukStatusRelease') + '</strong> '  + escapeHtml(d.soltaName) + '</div>';
                if (d.raceDate && d.raceDate.length > 0) {
                    var raceLabel = d.raceName && d.raceName.length > 0 ? escapeHtml(d.raceName) + ' &mdash; ' + escapeHtml(d.raceDate) : escapeHtml(d.raceDate);
                    html += '<div><strong>' + t('ukStatusRace') + '</strong> <span style="color:#fbbf24">' + raceLabel + '</span></div>';
                }
                if (d.registeredCount !== undefined) html += '<div><strong>' + t('ukStatusZP') + '</strong> ' + escapeHtml(d.registeredCount) + '</div>';
                if (d.contestCount !== undefined) html += '<div><strong>' + t('ukStatusIC') + '</strong> ' + escapeHtml(d.contestCount) + '</div>';
                if (d.polling) {
                    var pollLabel = d.intervalSec < 60
                        ? String(d.intervalSec) + ' s'
                        : String(Math.round(d.intervalSec / 60)) + ' min';
                    html += '<div style="color:#93c5fd;margin-top:6px">' + t('ukStatusAutoPolling') + ' ' + escapeHtml(pollLabel) + '</div>';
                }
                el.innerHTML = html;
                document.getElementById('statTotal').textContent = d.totalKnown  !== undefined ? d.totalKnown  : '--';
                document.getElementById('statRace').textContent  = d.raceKnownCount !== undefined ? d.raceKnownCount : '--';
                document.getElementById('statNew').textContent   = d.lastNewCount !== undefined ? d.lastNewCount : '--';
                document.getElementById('statAgo').textContent   = d.lastPollAgoSec > 0 ? d.lastPollAgoSec + 's' : '--';
                if (!configLoaded) {
                    configLoaded = true;
                    document.getElementById('enabled').checked = d.enabled;
                    document.getElementById('official_format').checked = d.officialFormat !== false;
                    var sel = document.getElementById('interval');
                    var opts = sel.options;
                    var best = 0;
                    for (var o = 0; o < opts.length; o++) {
                        if (parseInt(opts[o].value) <= d.intervalSec) best = o;
                    }
                    sel.selectedIndex = best;
                    var ed = document.getElementById('emailDestDisplay');
                    if (ed) ed.textContent = d.emailDest && d.emailDest.length > 0 ? d.emailDest : t('ukEmailNotConfigured');
                }
            })
            .catch(function() {});
    }

    function buildArrivalsRows(list, emptyText) {
        if (!list || list.length === 0) {
            return '<tr><td colspan="6" style="padding:12px;color:#4b5563;text-align:center">' + emptyText + '</td></tr>';
        }
        var rows = '';
        for (var i = 0; i < list.length; i++) {
            var a = list[i];
            var chip = (!a.chip || a.chip === '00000000000') ? '<em style="color:#4b5563">' + t('ukNoChip') + '</em>' : escapeHtml(a.chip);
            var data = '<em style="color:#4b5563">-</em>';
            var hora = '<em style="color:#4b5563">-</em>';
            if (a.date && a.date.length === 4 && a.date !== '0000') {
                data = a.date.substring(0,2) + '/' + a.date.substring(2,4);
            }
            if (a.time && a.time.length === 6 && a.time !== '000000') {
                hora = a.time.substring(0,2) + ':' + a.time.substring(2,4) + ':' + a.time.substring(4,6);
            }
            rows += '<tr style="border-bottom:1px solid #1e293b">'
                + '<td style="padding:7px 10px;color:#64748b">' + (i + 1) + '</td>'
                + '<td style="padding:7px 10px;font-family:monospace">' + escapeHtml(a.ring) + '</td>'
                + '<td style="padding:7px 10px;font-family:monospace">' + chip + '</td>'
                + '<td style="padding:7px 10px;font-family:monospace;color:#4ade80">' + hora + '</td>'
                + '<td style="padding:7px 10px;font-family:monospace;color:#93c5fd">' + data + '</td>'
                + '<td style="padding:7px 10px;font-family:monospace;color:#fbbf24">' + escapeHtml(a.race || '') + '</td>'
                + '</tr>';
        }
        return rows;
    }

    function splitArrivals(arr) {
        var out = { race: [], training: [] };
        for (var i = 0; i < arr.length; i++) {
            if (arr[i] && arr[i].kind === 'training') out.training.push(arr[i]);
            else out.race.push(arr[i]);
        }
        return out;
    }

    function loadArrivals() {
        fetch('/api/unikon-arrivals',{cache:'no-store'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var arr = (d && d.arrivals && d.arrivals.length) ? d.arrivals : [];
                var parts = splitArrivals(arr);
                var html = buildArrivalsRows(parts.race, t('ukNoArrivals'));
                if (parts.training.length > 0) {
                    html += '<tr><td colspan="6" style="padding:8px 10px;color:#93c5fd;font-weight:bold;background:#1e3a8a">'
                        + escapeHtml(t('ukTrainingTitle', 'Treino')) + '</td></tr>'
                        + buildArrivalsRows(parts.training, '');
                }
                var raceBody = document.getElementById('raceArrivalsBody');
                if (raceBody) raceBody.innerHTML = html;
            })
            .catch(function() {});
    }

    function loadContests() {
        fetch('/api/unikon-contests',{cache:'no-store'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var tbody = document.getElementById('contestsBody');
                if (!d.contests || d.contests.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="4" style="padding:12px;color:#4b5563;text-align:center">' + t('ukNoContests') + '</td></tr>';
                    return;
                }
                var rows = '';
                for (var i = 0; i < d.contests.length; i++) {
                    var c = d.contests[i];
                    var data = '<em style="color:#4b5563">-</em>';
                    if (c.date && c.date.length === 4) {
                        data = c.date.substring(0,2) + '/' + c.date.substring(2,4);
                    }
                    rows += '<tr style="border-bottom:1px solid #1e293b">'
                        + '<td style="padding:7px 10px;color:#64748b">' + (i + 1) + '</td>'
                        + '<td style="padding:7px 10px;font-family:monospace">' + escapeHtml(c.code || '-') + '</td>'
                        + '<td style="padding:7px 10px;font-family:monospace;color:#93c5fd">' + data + '</td>'
                        + '<td style="padding:7px 10px">' + escapeHtml(c.name || '-') + '</td>'
                        + '</tr>';
                }
                tbody.innerHTML = rows;
            })
            .catch(function() {});
    }

    function syncCalendar() {
        var runSync = function() {
            var el = document.getElementById('calendar-toast');
            fetch('/api/unikon-sync-calendar', { method: 'POST' })
                .then(function(r) { return r.json(); })
                .then(function(d) {
                    if (el) {
                        el.style.display = 'block';
                        el.textContent = d && d.message ? d.message : t('ukToastCalendarUpdating');
                        setTimeout(function() {
                            el.style.display = 'none';
                            loadContests();
                            loadStatus();
                        }, 2000);
                    }
                })
                .catch(function(e) { _alert(t('ukAlertError') + ' ' + e.message); });
        };
        customConfirm(t('ukConfirmCalendar'), function(ok) { if (ok) runSync(); });
    }

    function pollNow() {
        fetch('/api/unikon-poll', { method: 'POST' })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                if (d.success) {
                    var toast = document.getElementById('poll-toast');
                    if (toast) {
                        toast.textContent = t('ukToastPollScheduled');
                        toast.style.display = 'block';
                        setTimeout(function() { toast.style.display = 'none'; }, 3000);
                    }
                    setTimeout(function() { loadStatus(); loadArrivals(); }, 3000);
                } else {
                    _alert('&#10060; ' + t('ukAlertError') + ' ' + (d.message || t('ukAlertUnknownError')));
                }
            })
            .catch(function(e) { _alert('&#10060; ' + t('ukAlertError') + ' ' + e.message); });
    }

    function loadPombal() {
        fetch('/api/unikon-pombal',{cache:'no-store'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var tbody = document.getElementById('pombalBody');
                if (!tbody || !d || !d.registrations || d.registrations.length === 0) {
                    if (tbody) tbody.innerHTML = '<tr><td colspan="3" style="padding:12px;color:#4b5563;text-align:center">' + t('ukNoPigeons') + '</td></tr>';
                    return;
                }
                var html = '';
                for (var i = 0; i < d.registrations.length; i++) {
                    var r = d.registrations[i];
                    html += '<tr><td style="padding:8px 10px;border-bottom:1px solid #1e293b;color:#cbd5e1">' + (i+1) + '</td>';
                    html += '<td style="padding:8px 10px;border-bottom:1px solid #1e293b;font-family:monospace;color:#a5f3fc">' + escapeHtml(r.ring) + '</td>';
                    html += '<td style="padding:8px 10px;border-bottom:1px solid #1e293b;font-family:monospace;color:#86efac">' + escapeHtml(r.chip) + '</td></tr>';
                }
                if (tbody) tbody.innerHTML = html;
                var sel = document.getElementById('trainingPombalSelect');
                if (sel) {
                    var kept = {};
                    var prev = sel.querySelectorAll('input[type="checkbox"]:checked');
                    for (var k = 0; k < prev.length; k++) kept[prev[k].value] = true;
                    sel.innerHTML = '';
                    for (var j = 0; j < d.registrations.length; j++) {
                        var lab = document.createElement('label');
                        lab.style.cssText = 'display:flex;align-items:center;padding:3px 4px;cursor:pointer';
                        var cb = document.createElement('input');
                        cb.type = 'checkbox';
                        cb.value = d.registrations[j].ring;
                        cb.style.cssText = 'flex:none;width:16px;height:16px;margin:0 8px 0 0';
                        if (kept[cb.value]) cb.checked = true;
                        lab.appendChild(cb);
                        lab.appendChild(document.createTextNode(d.registrations[j].ring + '  ' + d.registrations[j].chip));
                        sel.appendChild(lab);
                    }
                }
            })
            .catch(function(e) { _alert('❌ ' + t('ukAlertError') + ' ' + e.message); });
    }

    function syncPombal() {
        var runSync = function() {
            var el = document.getElementById('pombal-toast');
            fetch('/api/unikon-sync-pombal', { method: 'POST' })
                .then(function(r) { return r.json(); })
                .then(function(d) {
                    if (el) {
                        el.style.display = 'block';
                        el.textContent = d && d.message ? d.message : t('ukToastPombalUpdating');
                        setTimeout(function() {
                            el.style.display = 'none';
                            loadPombal();
                            loadStatus();
                        }, 2000);
                    }
                })
                .catch(function(e) { _alert('❌ ' + t('ukAlertError') + ' ' + e.message); });
        };
        customConfirm(t('ukConfirmPombal'), function(ok) { if (ok) runSync(); });
    }

    function emailPombal() {
        if (emailPombalPending) return;

        var btn = document.getElementById('btnEmailPombal');
        var runSend = function() {
            emailPombalPending = true;
            if (btn) btn.disabled = true;
            var el = document.getElementById('pombal-toast');
            fetch('/api/unikon-email-pombal', { method: 'POST' })
                .then(function(r) { return r.json(); })
                .then(function(d) {
                    var ok = !!(d && d.success);
                    var message = d && d.message ? d.message : (ok ? t('ukToastEmailQueued') : t('ukToastEmailError'));
                    if (el) {
                        el.style.display = 'block';
                        el.textContent = message;
                        setTimeout(function() { el.style.display = 'none'; }, 2500);
                    }
                    if (!ok) _alert('❌ ' + message);
                })
                .catch(function(e) { _alert('❌ ' + t('ukAlertError') + ' ' + e.message); })
                .finally(function() {
                    emailPombalPending = false;
                    if (btn) btn.disabled = false;
                });
        };

        customConfirm(t('ukConfirmEmailPombal'), function(ok) { if (ok) runSend(); });
    }

    function resetState() {
        customConfirm(t('ukConfirmReset'), function(ok) {
            if (!ok) return;
            fetch('/api/unikon-reset', { method: 'POST' })
                .then(function(r) { return r.json(); })
                .then(function(d) {
                    customAlert(d.success ? t('ukToastCountReset') : '&#10060; ' + d.message);
                    loadStatus(); loadArrivals();
                })
                .catch(function(e) { _alert('&#10060; ' + e.message); });
        });
    }

    // Training send/clear (fictitious race 990 + pombal ZPs, explicit request only).
    var trainingPending = false;

    function selectedTrainingRings(box) {
        var out = [];
        if (!box || !box.querySelectorAll) return out;
        var checked = box.querySelectorAll('input[type="checkbox"]:checked');
        for (var i = 0; i < checked.length; i++) out.push(checked[i].value);
        return out;
    }

    function trainingCheckAll(on) {
        var box = document.getElementById('trainingPombalSelect');
        if (box) {
            var boxes = box.querySelectorAll('input[type="checkbox"]');
            for (var i = 0; i < boxes.length; i++) boxes[i].checked = !!on;
        }
        return false;
    }

    function sendTraining() {
        if (trainingPending) return;
        var btn = document.querySelector('[data-i18n="ukBtnSendTraining"]');
        var runSend = function() {
            trainingPending = true;
            if (btn) btn.disabled = true;
            var el = document.getElementById('training-toast');
            if (el) {
                el.style.display = 'block';
                el.textContent = t('ukTrainingSending', 'Sending training to RAMBOX...');
            }
            var sel = document.getElementById('trainingPombalSelect');
            var body = 'rings=' + encodeURIComponent(selectedTrainingRings(sel).join('\n'));
            fetch('/api/unikon-send-training', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
                .then(function(r) { return r.json(); })
                .then(function(d) {
                    var ok = !!(d && d.success);
                    var msg = d && d.message ? d.message : (ok ? t('ukTrainingSent', 'Training sent.') : t('ukTrainingError', 'Send failed.'));
                    if (el) { el.textContent = msg; }
                    if (!ok) _alert('&#10060; ' + msg);
                    setTimeout(function() {
                        if (el) el.style.display = 'none';
                    }, 3000);
                })
                .catch(function(e) {
                    _alert('&#10060; ' + t('ukAlertError') + ' ' + e.message);
                    if (el) { el.textContent = t('ukTrainingError', 'Send failed.'); }
                })
                .finally(function() {
                    trainingPending = false;
                    if (btn) btn.disabled = false;
                });
        };
        customConfirm(t('ukConfirmTraining', 'Send training contest (I-frame 990) to RAMBOX?'), function(ok) { if (ok) runSend(); });
    }

    var clearTrainingPending = false;

    function clearTraining() {
        if (clearTrainingPending) return;
        var btn = document.querySelector('[data-i18n="ukBtnClearTraining"]');
        var runClear = function() {
            clearTrainingPending = true;
            if (btn) btn.disabled = true;
            var el = document.getElementById('training-toast');
            if (el) {
                el.style.display = 'block';
                el.textContent = t('ukTrainingClearing', 'Clearing training from RAMBOX...');
            }
            fetch('/api/unikon-clear-training', { method: 'POST' })
                .then(function(r) { return r.json(); })
                .then(function(d) {
                    var ok = !!(d && d.success);
                    var msg = d && d.message ? d.message : (ok ? t('ukTrainingCleared', 'Training cleared.') : t('ukTrainingClearError', 'Clear failed.'));
                    if (el) { el.textContent = msg; }
                    if (!ok) _alert('&#10060; ' + msg);
                    setTimeout(function() {
                        if (el) el.style.display = 'none';
                    }, 3000);
                })
                .catch(function(e) {
                    _alert('&#10060; ' + t('ukAlertError') + ' ' + e.message);
                    if (el) { el.textContent = t('ukTrainingClearError', 'Clear failed.'); }
                })
                .finally(function() {
                    clearTrainingPending = false;
                    if (btn) btn.disabled = false;
                });
        };
        customConfirm(t('ukConfirmClearTraining', 'Remove training contest (code 990) from RAMBOX?'), function(ok) { if (ok) runClear(); });
    }

    function _alert(msg) {
        customAlert(msg);
    }

    function ukSwitchTab(idx) {
        var btns = document.querySelectorAll('#ukTabs .uk-tab-btn');
        for (var i = 0; i < btns.length; i++) btns[i].classList.toggle('active', i === idx);
        for (var p = 0; p < 4; p++) {
            var el = document.getElementById('uk-tab-' + p);
            if (el) el.classList.toggle('active', p === idx);
        }
        try { localStorage.setItem('uk_tab', String(idx)); } catch (e) {}
    }

    function saveUnikonConfig() {
        var params = new URLSearchParams();
        params.set('enabled', document.getElementById('enabled').checked ? '1' : '0');
        params.set('official_format', document.getElementById('official_format').checked ? '1' : '0');
        params.set('interval', document.getElementById('interval').value);
        fetch('/api/unikon-config', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: params.toString() })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                if (d.success) {
                    _alert(d.message || t('ukToastConfigSaved'));
                    if (typeof showSavedCard === 'function') showSavedCard();
                    configLoaded = false;
                } else {
                    _alert('\u274c ' + t('ukToastConfigError') + (d.message || t('ukAlertUnknownError')));
                }
            })
            .catch(function(e) { _alert('\u274c ' + t('ukAlertError') + ' ' + e.message); });
    }

    function loadRs232Info() {
        fetch('/api/config?section=rs232', {cache: 'no-cache'})
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var el = document.getElementById('rs232InfoText');
                if (!el) return;
                var sc = (d && d.rs232) ? d.rs232 : {};
                var baudList = Array.isArray(sc.baudrate_list) ? sc.baudrate_list : [1200,2400,4800,9600,19200,38400,57600,115200];
                var baud = baudList[sc.baudrate_index !== undefined ? sc.baudrate_index : 3] || 9600;
                var db = sc.data_bits || 8;
                var par = (sc.parity || 'none').charAt(0).toUpperCase();
                var sb = sc.stop_bits || 1;
                var status = sc.enabled
                    ? '<span style="color:#4ade80">&#9679; ' + escapeHtml(t('statusActive')) + '</span>'
                    : '<span style="color:#f87171">&#9679; ' + escapeHtml(t('statusInactive')) + '</span>';
                el.innerHTML = '<strong style="color:#e0e7ff">' + baud + ' baud &middot; ' + db + par + sb + '</strong> &mdash; ' + status;
            })
            .catch(function() {});
    }

    loadStatus();
    loadArrivals();
    loadContests();
    loadRs232Info();
    setInterval(function() { loadStatus(); loadArrivals(); }, 8000);
    loadPombal();
    setInterval(function() { loadPombal(); }, 8000);
    setInterval(function() { loadRs232Info(); }, 8000);
    try { var _ukTab = parseInt(localStorage.getItem('uk_tab'), 10); if (_ukTab >= 0 && _ukTab < 4) ukSwitchTab(_ukTab); } catch (e) {}
    </script>
)rawliteral";
