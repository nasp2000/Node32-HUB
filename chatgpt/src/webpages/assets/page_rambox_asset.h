// page_rambox_asset.h
// Unified RAMBOX page: ONE device selector (Unikon / Bricon / future clocks) and the
// SAME tab structure for every device (Config, Arrivals, Pigeons, Race, Status).
// Only one clock is ever connected to an ESP32, so the selector just picks which
// device's configuration/data the tabs show -- the hub still enforces the exclusion.
//
// Adding a new clock family = adding one entry to the DEV registry below (endpoints +
// config fields); the page renders itself from that description.
//
// All visible text and tooltips are localised via data-i18n / data-i18n-title and the
// rmb* keys (see lang_pt_ui.h / lang_en_ui.h); dynamic strings use T().
//
// Does NOT include DOCTYPE/html/head/body -- the shell renderer adds those.

#pragma once
#include <pgmspace.h>

static const char PAGE_RAMBOX[] PROGMEM = R"rawliteral(
<main class='page'><div class='wrap'>
<style>
    .rm-bar{display:flex;align-items:center;gap:8px;flex-wrap:nowrap;overflow-x:auto;margin:8px 0 2px}
    .rm-lbl{color:#93c5fd;font-weight:bold;font-size:14px;white-space:nowrap}
    .rm-sel{width:auto;padding:3px 6px;font-size:13px;background:#0f172a;color:#e2e8f0;
            border:1px solid #60a5fa;border-radius:6px}
    .rm-badge{padding:3px 10px;border-radius:999px;font-size:12px;font-weight:bold;
              background:#334155;color:#e2e8f0;white-space:nowrap}
    .rm-badge.on{background:#15803d;color:#fff}
    .rm-badge.off{background:#b91c1c;color:#fff}
    .rm-tabs{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;margin:10px 0}
    .rm-tab{padding:8px 6px;border-radius:8px;background:#1e293b;color:#e2e8f0;
            border:1px solid #475569;cursor:pointer;font-size:13px;font-weight:600;
            white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
    .rm-tab:hover{background:#334155}
    .rm-tab.active{background:#2563eb;border-color:#93c5fd;color:#fff}
    .rm-card{background:#0b1220;border:1px solid #475569;border-radius:10px;padding:12px;margin:0 0 10px}
    .rm-acts{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0}
    .rm-acts>button{flex:1 1 150px;padding:9px 10px;border-radius:8px;background:#1d4ed8;
            color:#fff;border:1px solid #3b82f6;cursor:pointer;font-size:13px;font-weight:600}
    .rm-acts>button:hover{background:#2563eb}
    .rm-row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin:6px 0}
    .rm-fld{display:flex;align-items:center;gap:6px;color:#cbd5e1;font-size:13px}
    .rm-chk{display:flex;align-items:center;gap:6px;color:#e2e8f0;font-size:13px;cursor:pointer}
    .rm-card input[type=number],.rm-card input[type=text],.rm-card select{
            background:#0f172a;color:#e2e8f0;border:1px solid #64748b;border-radius:6px;padding:5px 7px}
    .rm-checks{display:grid;grid-template-columns:repeat(auto-fill,minmax(155px,1fr));
               gap:4px 10px;max-height:34vh;overflow:auto;width:100%}
    .rm-checks label{display:flex;align-items:center;gap:6px;font-family:monospace;font-size:12px;
               color:#e2e8f0;background:#111827;border:1px solid #374151;border-radius:6px;padding:3px 6px}
    .rm-card table{width:100%;border-collapse:collapse;font-size:13px}
    .rm-card th,.rm-card td{border:1px solid #475569;padding:5px 7px;text-align:left;white-space:nowrap}
    .rm-card th{background:#1e293b;color:#93c5fd;position:sticky;top:0}
    .rm-muted{color:#94a3b8;font-size:12px}
    .rm-ok{color:#4ade80}.rm-err{color:#f87171}
    .rm-scroll{max-height:48vh;overflow:auto;-webkit-overflow-scrolling:touch}
    .rm-scroll table{min-width:100%}
    @media (max-width:640px){
        .rm-card{padding:8px}
        .rm-tab{font-size:12px;padding:7px 4px}
        .rm-card th,.rm-card td{padding:4px 5px;font-size:12px}
        .rm-acts>button{flex:1 1 120px;padding:8px 6px;font-size:12px}
        .rm-checks{grid-template-columns:repeat(auto-fill,minmax(130px,1fr));max-height:30vh}
        .rm-fld,.rm-chk{font-size:12px}
    }
</style>

<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' data-i18n-title='btnHome' title='Home'>&#127968; Home</a></div>

<h1 data-i18n='rmbTitle' data-i18n-title='rmbTitleTip' title='RAMBOX'>&#128038; RAMBOX</h1>

<div class='rm-bar'>
    <label class='rm-lbl' data-i18n='rmbDevice' data-i18n-title='rmbDeviceTip' title='Aparelho'>Aparelho:</label>
    <select id='rmDev' class='rm-sel' onchange='rmSelect()' title='Aparelho'>
        <option value='unikon'>&#128038; Unikon</option>
        <option value='bricon'>&#128225; Bricon</option>
    </select>
    <span id='rmBadge' class='rm-badge'>&mdash;</span>
</div>
<p class='rm-muted' style='margin:2px 0 0' data-i18n='rmbOneOnly' data-i18n-title='rmbDeviceTip'>S&oacute; um rel&oacute;gio pode estar ligado/activo em cada ESP32.</p>

<div class='rm-tabs'>
    <button class='rm-tab active' data-tab='config'   onclick='rmTab("config")'
            data-i18n='rmbTabConfig'   data-i18n-title='rmbTabConfigTip'>&#9881; Config</button>
    <button class='rm-tab'        data-tab='arrivals' onclick='rmTab("arrivals")'
            data-i18n='rmbTabArrivals' data-i18n-title='rmbTabArrivalsTip'>&#128229; Chegadas</button>
    <button class='rm-tab'        data-tab='pombal'   onclick='rmTab("pombal")'
            data-i18n='rmbTabPombal'   data-i18n-title='rmbTabPombalTip'>&#128038; Pombos</button>
    <button class='rm-tab'        data-tab='race'     onclick='rmTab("race")'
            data-i18n='rmbTabRace'     data-i18n-title='rmbTabRaceTip'>&#127937; Prova</button>
    <button class='rm-tab'        data-tab='status'   onclick='rmTab("status")'
            data-i18n='rmbTabStatus'   data-i18n-title='rmbTabStatusTip'>&#128202; Estado</button>
</div>

<!-- ================= CONFIG ================= -->
<section id='rmp-config' class='rm-pane'>
  <div class='rm-card'>
    <div class='rm-row'>
        <label class='rm-chk'><input type='checkbox' id='cfg_enabled' data-i18n-title='rmbActiveTip'
               title='Rel&oacute;gio activo'><span data-i18n='rmbActive'>Rel&oacute;gio activo</span></label>
        <label class='rm-fld'><span data-i18n='rmbInterval' data-i18n-title='rmbIntervalTip'
               title='Intervalo de poll (minutos)'>Intervalo de poll (min)</span>
            <select id='cfg_interval' class='rm-sel' data-i18n-title='rmbIntervalTip'
                    title='Intervalo de poll (minutos)'>
                <option value='1'>&#9201; 1 min</option>
                <option value='3' selected>&#9201; 3 min</option>
                <option value='5'>&#9201; 5 min</option>
                <option value='10'>&#9201; 10 min</option>
                <option value='15'>&#9201; 15 min</option>
                <option value='30'>&#9201; 30 min</option>
                <option value='60'>&#9201; 60 min</option>
            </select></label>
    </div>

    <div class='rm-row'>
        <label class='rm-chk'><input type='checkbox' id='cfg_manual' data-i18n-title='rmbManualTip'
               title='Usar a configura&ccedil;&atilde;o da p&aacute;gina RS232'><span data-i18n='rmbManual'>Usar a configura&ccedil;&atilde;o da RS232</span></label>
    </div>

    <div id='cfgdev_unikon' class='rm-devcfg'>
        <div class='rm-row'>
            <label class='rm-chk'><input type='checkbox' id='cfg_official' data-i18n-title='rmbOfficialTip'
                   title='Formato oficial'><span data-i18n='rmbOfficial'>Formato oficial (columbofilia)</span></label>
            <span class='rm-muted'><span data-i18n='rmbEmailLabel' data-i18n-title='rmbEmailLabelTip' title='Email'>Email:</span>
                <span id='rmEmailDest'>&mdash;</span></span>
        </div>
        <p class='rm-muted' data-i18n='rmbSerialFixed'>Liga&ccedil;&atilde;o fixa do Unikon: 9600 8N1.</p>
    </div>

    <div id='cfgdev_bricon' class='rm-devcfg'>
        <div class='rm-row'>
            <label class='rm-fld'><span data-i18n='rmbBaud' data-i18n-title='rmbBaudTip' title='Baud'>Baud</span>
                <select id='cfg_baud' data-i18n-title='rmbBaudTip' title='Baud'>
                    <option>9600</option><option>19200</option><option>38400</option>
                    <option>57600</option><option>115200</option></select></label>
            <label class='rm-fld'><span data-i18n='rmbData' data-i18n-title='rmbDataTip' title='Dados'>Dados</span>
                <select id='cfg_data' data-i18n-title='rmbDataTip' title='Dados'><option>8</option><option>7</option></select></label>
            <label class='rm-fld'><span data-i18n='rmbParity' data-i18n-title='rmbParityTip' title='Paridade'>Paridade</span>
                <select id='cfg_parity' data-i18n-title='rmbParityTip' title='Paridade'>
                    <option value='none'>N</option><option value='even'>E</option><option value='odd'>O</option></select></label>
            <label class='rm-fld'><span data-i18n='rmbStop' data-i18n-title='rmbStopTip' title='Stop'>Stop</span>
                <select id='cfg_stop' data-i18n-title='rmbStopTip' title='Stop'><option>2</option><option>1</option></select></label>
            <label class='rm-fld'><span data-i18n='rmbCountry' data-i18n-title='rmbCountryTip' title='Pa&iacute;s'>Pa&iacute;s</span>
                <select id='cfg_country' data-i18n-title='rmbCountryTip' title='Pa&iacute;s'>
                    <option>PT</option><option>UK</option><option>BE</option><option>ES</option>
                    <option>FR</option><option>NL</option></select></label>
        </div>
        <div class='rm-row'>
            <label class='rm-fld'><span data-i18n='rmbBa' data-i18n-title='rmbBaTip' title='Endere&ccedil;o BA'>Endere&ccedil;o BA</span>
                <input type='number' id='cfg_ba' min='0' max='255' style='width:80px' placeholder='auto'
                       data-i18n-title='rmbBaTip' title='Endere&ccedil;o BA'></label>
            <label class='rm-fld'><span data-i18n='rmbClub' data-i18n-title='rmbClubTip' title='Clube'>Clube</span>
                <input id='cfg_club' maxlength='4' style='width:70px' data-i18n-title='rmbClubTip' title='Clube'></label>
            <label class='rm-fld'><span data-i18n='rmbFlight' data-i18n-title='rmbFlightTip' title='Voo'>Voo</span>
                <input id='cfg_flight' maxlength='4' style='width:70px' data-i18n-title='rmbFlightTip' title='Voo'></label>
        </div>
        <p class='rm-muted' data-i18n='rmbSerialHint'>O Bricon usa 9600 8N2 (o perfil &eacute; aplicado automaticamente).</p>
    </div>
  </div>
</section>

<!-- ================= ARRIVALS ================= -->
<section id='rmp-arrivals' class='rm-pane' style='display:none'>
  <div class='rm-card'>
    <div class='rm-acts'>
        <button onclick='rmRefresh()' data-i18n='rmbRefresh' data-i18n-title='rmbRefreshTip' title='Actualizar'>&#128260; Actualizar</button>
        <button onclick='rmAct("poll")' data-i18n='rmbPoll' data-i18n-title='rmbPollTip' title='Poll agora'>&#128225; Poll agora</button>
        <button onclick='rmAct("syncPombal")' data-i18n='rmbSyncPombal' data-i18n-title='rmbSyncPombalTip' title='Sincronizar pombal'>&#128038; Sincronizar pombal</button>
        <button onclick='rmAct("syncCalendar")' data-i18n='rmbSyncCalendar' data-i18n-title='rmbSyncCalendarTip' title='Sincronizar calend&aacute;rio'>&#127937; Sincronizar calend&aacute;rio</button>
        <span id='pushWrap' style='display:contents'>
            <button onclick='rmPush(1)' data-i18n='rmbPushOn' data-i18n-title='rmbPushOnTip' title='Push ON'>&#128225; Push ON</button>
            <button onclick='rmPush(0)' data-i18n='rmbPushOff' data-i18n-title='rmbPushOffTip' title='Push OFF'>&#128225; Push OFF</button>
        </span>
    </div>
    <div class='rm-row' id='rmStats' style='gap:8px'></div>
    <div class='rm-scroll'><table id='tblArr'><thead></thead><tbody></tbody></table></div>
    <p class='rm-muted' id='arrmsg'></p>
  </div>
</section>

<!-- ================= PIGEONS ================= -->
<section id='rmp-pombal' class='rm-pane' style='display:none'>
  <div class='rm-card'>
    <div class='rm-acts'>
        <button onclick='rmRefresh()' data-i18n='rmbRefresh' data-i18n-title='rmbRefreshTip' title='Actualizar'>&#128260; Actualizar</button>
        <button onclick='rmAct("syncPombal")' data-i18n='rmbReadDevice' data-i18n-title='rmbReadDeviceTip' title='Ler do rel&oacute;gio'>&#128229; Ler do rel&oacute;gio</button>
        <button id='btnEmailPombal' onclick='rmAct("emailPombal")' data-i18n='rmbEmailPombal' data-i18n-title='rmbEmailPombalTip' title='Email do pombal'>&#9993; Email do pombal</button>
    </div>
    <div class='rm-scroll'><table id='tblPom'><thead></thead><tbody></tbody></table></div>
    <p class='rm-muted' id='pommsg'></p>
  </div>
</section>

<!-- ================= RACE / TRAINING ================= -->
<section id='rmp-race' class='rm-pane' style='display:none'>
  <div class='rm-card'>
    <div class='rm-acts'>
        <button onclick='rmRefresh()' data-i18n='rmbRefresh' data-i18n-title='rmbRefreshTip' title='Actualizar'>&#128260; Actualizar</button>
        <button onclick='rmAct("syncCalendar")' data-i18n='rmbSyncCalendar' data-i18n-title='rmbSyncCalendarTip' title='Ler provas'>&#128229; Ler provas do rel&oacute;gio</button>
    </div>
    <div class='rm-scroll'><table id='tblRace'><thead></thead><tbody></tbody></table></div>
    <p class='rm-muted' id='racemmsg'></p>
  </div>

  <div class='rm-card' id='rmTraining'>
    <h3 style='margin:0 0 8px;color:#93c5fd;font-size:15px'
        data-i18n='rmbTrainTitle' data-i18n-title='rmbTrainTitleTip' title='Enviar treino'>&#127937; Enviar treino</h3>

    <div id='train_unikon'>
        <div class='rm-row'>
            <label class='rm-fld'><span data-i18n='rmbTrainMode' data-i18n-title='rmbTrainModeTip' title='Pombos'>Pombos:</span>
                <select id='trMode' onchange='rmTrainMode()' data-i18n-title='rmbTrainModeTip' title='Pombos'>
                    <option value='all'  data-i18n='rmbModeAll'>Pombal todo</option>
                    <option value='list' data-i18n='rmbModeList'>S&oacute; os marcados</option>
                    <option value='none' data-i18n='rmbModeNone'>S&oacute; a prova (sem pombos)</option>
                </select></label>
            <button onclick='rmSelAll(1)' data-i18n='rmbSelectAll' data-i18n-title='rmbSelectAllTip' title='Marcar todos'>Marcar todos</button>
            <button onclick='rmSelAll(0)' data-i18n='rmbClearSel' data-i18n-title='rmbClearSelTip' title='Limpar selec&ccedil;&atilde;o'>Limpar selec&ccedil;&atilde;o</button>
        </div>
        <div class='rm-row' id='trRingsRow' style='display:none'>
            <div id='trRings' class='rm-checks' data-i18n-title='rmbChecklistTip' title='Marque os pombos'></div>
        </div>
        <p class='rm-muted' data-i18n='rmbChecklistTip'>Marque um ou v&aacute;rios pombos para enviar no treino.</p>
        <p class='rm-muted' data-i18n='rmbTrainHint'>No clube leva-se o m&oacute;dulo e os pombos dessa prova.</p>
    </div>

    <div id='train_bricon' class='rm-muted' style='display:none'
         data-i18n='rmbTrainBricon'>Escreve a prova de treino no rel&oacute;gio Bricon.</div>

    <div class='rm-acts'>
        <button onclick='rmSendTraining()' data-i18n='rmbSendTraining' data-i18n-title='rmbSendTrainingTip' title='Enviar treino'>&#9654; Enviar treino</button>
        <span id='trainmsg' class='rm-muted'></span>
    </div>
  </div>
</section>

<!-- ================= STATUS ================= -->
<section id='rmp-status' class='rm-pane' style='display:none'>
  <div class='rm-card'>
    <div class='rm-acts'>
        <button onclick='rmRefresh()' data-i18n='rmbRefresh' data-i18n-title='rmbRefreshTip' title='Actualizar'>&#128260; Actualizar</button>
    </div>
    <div class='rm-scroll'><table id='tblStatus'><tbody></tbody></table></div>
  </div>
</section>

<div class='actions' style='margin:6px 0 26px'>
    <button onclick='rmSaveCfg()' data-i18n='rmbSave' data-i18n-title='rmbSaveTip' title='Guardar configura&ccedil;&atilde;o'>&#128190; Guardar configura&ccedil;&atilde;o</button>
    <button onclick='rmRestoreProfile()' data-i18n='rmbRestoreProfile' data-i18n-title='rmbRestoreProfileTip'
            title='Voltar ao perfil pr&oacute;prio do m&oacute;dulo'>&#8617; Perfil do m&oacute;dulo</button>
    <span id='cfgmsg' class='rm-muted'></span>
</div>

<script>
/* ---------------------------------------------------------------------------
 * Device registry. ONE entry per clock family; the page renders from it.
 * Adding a new clock = new entry here (+ its routes on the hub).
 * ------------------------------------------------------------------------- */
var RM_DEV = {
    unikon: {
        label: 'Unikon RAMBOX',
        status:   '/api/unikon-status',
        arrivals: '/api/unikon-arrivals',
        pombal:   '/api/unikon-pombal',
        contests: '/api/unikon-contests',
        config:   '/api/unikon-config',
        poll:     '/api/unikon-poll',
        syncPombal:   '/api/unikon-sync-pombal',
        syncCalendar: '/api/unikon-sync-calendar',
        emailPombal:  '/api/unikon-email-pombal',
        training:     '/api/unikon-send-training',
        push:         '/api/unikon-push',
        trainingMode: true,
        colsArr: 8,
        colsPom: 6
    },
    bricon: {
        label: 'Bricon',
        status:   '/api/bricon-status',
        arrivals: '/api/bricon-arrivals',
        pombal:   '/api/bricon-pombal',
        contests: '/api/bricon-contests',
        config:   '/api/bricon-config',
        poll:     '/api/bricon-poll',
        syncPombal:   '/api/bricon-sync-pombal',
        syncCalendar: '/api/bricon-sync-calendar',
        emailPombal:  null,
        training:     '/api/bricon-send-training',
        push:         null,
        trainingMode: false,
        colsArr: 8,
        colsPom: 3
    }
};

var rmDev = 'unikon', rmCurTab = 'config';
var rmLastStatus = null;
var rmAdopted = false;   // first status load already adopted the enabled module

function rmd() { return RM_DEV[rmDev]; }
function T(k, fb) { return (window._t ? window._t(k, fb) : fb); }

function rmEsc(s) {
    return String(s === undefined || s === null ? '' : s)
        .replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function rmGet(url) {
    return fetch(url, {cache:'no-store'}).then(function(r){
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
    });
}

function rmMsg(el, txt, ok) {
    if (!el) return;
    el.textContent = txt || '';
    el.className = ok === false ? 'rm-err' : 'rm-ok';
}

function rmPost(url, body, msgEl) {
    if (!url) return;
    var opt = {method:'POST'};
    if (body !== null && body !== undefined) {
        opt.headers = {'Content-Type':'application/x-www-form-urlencoded'};
        opt.body = body;
    }
    fetch(url, opt).then(function(r){ return r.json().catch(function(){ return {}; }); })
        .then(function(d){
            rmMsg(msgEl, (d && (d.message || d.msg)) || 'ok', d && d.success === false);
            setTimeout(rmRefresh, 1200);
        })
        .catch(function(e){ rmMsg(msgEl, 'Erro: ' + e, false); });
}

/* ---- poll interval ----
 * Fixed minute steps shared by both clocks (Unikon RAMBOX and Bricon): the
 * hub stores/uses seconds, the page only offers 1/3/5/10/15/30/60 min, 3 min
 * by default. A stored value outside the list snaps to the next larger step. */
var RM_INTERVAL_MIN = [1, 3, 5, 10, 15, 30, 60];
var RM_INTERVAL_DEFAULT_MIN = 3;

function rmIntervalOption(sec) {
    var s = Number(sec);
    if (!isFinite(s) || s <= 0) return String(RM_INTERVAL_DEFAULT_MIN);   // 0/absent = 3 min
    var i;
    for (i = 0; i < RM_INTERVAL_MIN.length; i++) {
        if (RM_INTERVAL_MIN[i] * 60 === s) return String(RM_INTERVAL_MIN[i]);
    }
    for (i = 0; i < RM_INTERVAL_MIN.length; i++) {
        if (RM_INTERVAL_MIN[i] * 60 > s) return String(RM_INTERVAL_MIN[i]);
    }
    return String(RM_INTERVAL_MIN[RM_INTERVAL_MIN.length - 1]);
}

function rmIntervalSec() {
    var m = parseInt(document.getElementById('cfg_interval').value, 10);
    if (!isFinite(m) || m <= 0) m = RM_INTERVAL_DEFAULT_MIN;
    return m * 60;
}

/* ---- selector / tabs ---- */
function rmSelect() {
    rmDev = document.getElementById('rmDev').value;
    try { localStorage.setItem('rm_dev', rmDev); } catch (e) {}
    document.getElementById('cfgdev_unikon').style.display = (rmDev === 'unikon') ? '' : 'none';
    document.getElementById('cfgdev_bricon').style.display = (rmDev === 'bricon') ? '' : 'none';
    document.getElementById('train_unikon').style.display = rmd().trainingMode ? '' : 'none';
    document.getElementById('train_bricon').style.display = rmd().trainingMode ? 'none' : '';
    document.getElementById('btnEmailPombal').style.display = rmd().emailPombal ? '' : 'none';
    document.getElementById('pushWrap').style.display = (rmDev === 'unikon') ? 'contents' : 'none';
    rmRefresh();
}

/* The Config pane must show the clock that is actually enabled on the hub.
 * The first status load adopts activeModule ("rambox" | "bricon") over the
 * remembered/URL choice, so the serial settings on screen are the ones the
 * device is running. A later manual choice in the selector is left alone. */
function rmAdoptActive(d) {
    if (rmAdopted) return;
    rmAdopted = true;
    var am = d && d.activeModule;
    if (am !== 'rambox' && am !== 'bricon') return;
    var want = (am === 'bricon') ? 'bricon' : 'unikon';
    if (want === rmDev) return;
    rmDev = want;
    document.getElementById('rmDev').value = want;
    try { localStorage.setItem('rm_dev', want); } catch (e) {}
    rmSelect();
}

function rmTab(t) {
    rmCurTab = t;
    var ps = document.querySelectorAll('.rm-pane');
    for (var i = 0; i < ps.length; i++) ps[i].style.display = (ps[i].id === 'rmp-' + t) ? '' : 'none';
    var bs = document.querySelectorAll('.rm-tab');
    for (var j = 0; j < bs.length; j++) bs[j].classList.toggle('active', bs[j].getAttribute('data-tab') === t);
    try { localStorage.setItem('rm_tab', t); } catch (e) {}
    rmRefresh();
}

/* ---- tables ---- */
function rmTable(id, cols, rows) {
    var t = document.getElementById(id);
    if (cols) {
        var th = '<tr>';
        for (var i = 0; i < cols.length; i++) th += '<th>' + rmEsc(cols[i]) + '</th>';
        th += '</tr>';
        if (!t.tHead) {
            var thead = document.createElement('thead');
            t.insertBefore(thead, t.firstChild);
        }
        t.tHead.innerHTML = th;
    } else if (t.tHead) {
        t.tHead.innerHTML = '';
    }
    var body = '';
    for (var r = 0; r < rows.length; r++) {
        body += '<tr>';
        for (var c = 0; c < rows[r].length; c++) body += '<td>' + rmEsc(rows[r][c]) + '</td>';
        body += '</tr>';
    }
    var n = cols ? cols.length : (rows[0] ? rows[0].length : 1);
    if (!rows.length) body = '<tr><td colspan="' + n + '" class="rm-muted">' + rmEsc(T('rmbEmpty','sem dados')) + '</td></tr>';
    t.tBodies[0].innerHTML = body;
}

function rmPick(o, keys) {
    for (var i = 0; i < keys.length; i++) {
        if (o[keys[i]] !== undefined && o[keys[i]] !== null && o[keys[i]] !== '') return o[keys[i]];
    }
    return '';
}

function rmTime(s) {
    s = String(s || '');
    return (s.length >= 6) ? (s.substr(0,2) + ':' + s.substr(2,2) + ':' + s.substr(4,2)) : s;
}

function rmDate(s) {
    s = String(s || '');
    if (s.length >= 6) return s.substr(0,2) + '/' + s.substr(2,2) + '/' + s.substr(4,2);
    if (s.length >= 4) return s.substr(0,2) + '/' + s.substr(2,2);
    return s;
}

/* ---- status / badge / config ---- */
function rmStatus(d) {
    rmLastStatus = d;
    rmAdoptActive(d);
    var en = !!d.enabled;
    var b = document.getElementById('rmBadge');
    b.textContent = rmd().label + ' \u2014 ' + (en ? (T('rmbOnline','ligado')) : (T('rmbOffline','desligado'))) +
                    (d.connected ? ' \u00b7 ' + T('rmbOnline','ligado') : '') +
                    (d.activeModule && d.activeModule !== rmDev ? ' \u00b7 ' + d.activeModule : '');
    b.className = 'rm-badge ' + (en ? 'on' : 'off');

    document.getElementById('cfg_enabled').checked = en;
    document.getElementById('cfg_manual').checked = !!d.manualSerial;
    var iv = rmPick(d, ['intervalSec','interval']);
    if (iv !== '') document.getElementById('cfg_interval').value = rmIntervalOption(iv);
    if (rmDev === 'unikon') {
        document.getElementById('cfg_official').checked = d.officialFormat !== false;
        var ed = document.getElementById('rmEmailDest');
        if (ed) ed.textContent = d.emailDest || '\u2014';
    } else {
        document.getElementById('cfg_baud').value = String(rmPick(d, ['baud','baudrate']) || 9600);
        document.getElementById('cfg_data').value = String(rmPick(d, ['dataBits','data']) || 8);
        document.getElementById('cfg_parity').value = String(rmPick(d, ['parity']) || 'none');
        document.getElementById('cfg_stop').value = String(rmPick(d, ['stopBits','stop']) || 2);
        document.getElementById('cfg_country').value = String(rmPick(d, ['country']) || 'PT');
        document.getElementById('cfg_ba').value = String(rmPick(d, ['baActiveAddress','baAddress','ba']) || '');
        document.getElementById('cfg_club').value = String(rmPick(d, ['clubId','club']) || '');
        document.getElementById('cfg_flight').value = String(rmPick(d, ['flightId','flight']) || '');
    }

    var rows = [];
    for (var k in d) {
        if (!Object.prototype.hasOwnProperty.call(d, k)) continue;
        var v = d[k];
        if (typeof v === 'object') v = JSON.stringify(v);
        rows.push([k, v]);
    }
    rmTable('tblStatus', null, rows);
}

/* ---- arrivals / pombal / contests ---- */
function rmArrivals(d) {
    var arr = d.arrivals || d.registrations || [];
    var rows = [];
    for (var i = 0; i < arr.length; i++) {
        var a = arr[i];
        rows.push([
            (i + 1) + '.',
            rmPick(a, ['ring','anilha']),
            rmPick(a, ['chip']),
            rmTime(rmPick(a, ['time','arrivalTime','hhmmss'])),
            rmDate(rmPick(a, ['date','arrivalDate','ddmm'])),
            rmPick(a, ['race','raceName']),
            rmPick(a, ['posArriveClub','pos']),
            rmPick(a, ['evaluation'])
        ]);
    }
    rmTable('tblArr', [T('rmbNo','No'), T('rmbRing','Anilha'), T('rmbChip','Chip'),
                       T('rmbTime','Hora'), T('rmbDate','Data'), T('rmbRace','Prova'),
                       T('rmbPos','Pos'), T('rmbEval','Eval')], rows);
    rmStats(rows.length);
}

function rmStats(nArr) {
    var d = rmLastStatus || {};
    var ago = rmPick(d, ['lastPollAgoSec','pollAgoSec']);
    var el = document.getElementById('rmStats');
    if (!el) return;
    el.innerHTML =
        '<span class="rm-badge">' + rmEsc(T('rmbArrivals','Chegadas:')) + ' ' + nArr + '</span>' +
        '<span class="rm-badge">' + rmEsc(T('rmbNew','Novas:')) + ' ' + rmEsc(rmPick(d, ['lastNewCount','newCount']) || 0) + '</span>' +
        '<span class="rm-badge">' + rmEsc(ago === '' ? T('rmbNoPoll','sem poll') : (T('rmbPollAgo','poll há') + ' ' + ago + ' s')) + '</span>' +
        '<span class="rm-badge">' + rmEsc(d.connected ? T('rmbOnline','ligado') : T('rmbOffline','desligado')) + '</span>';
}

function rmPombal(d) {
    var arr = d.registrations || d.pombal || [];
    var rows = [];
    for (var i = 0; i < arr.length; i++) {
        var a = arr[i];
        rows.push([
            (i + 1) + '.',
            rmPick(a, ['ring','anilha']),
            rmPick(a, ['chip'])
        ]);
    }
    // No telemovel uma tabela de 6 colunas nao cabe: o pombal mostra o essencial
    // (No / Anilha / Chip), que e o que o relogio devolve no X (ring + chip).
    rmTable('tblPom', [T('rmbNo','No'), T('rmbRing','Anilha'), T('rmbChip','Chip')], rows);

    // Checklist do treino: uma CAIXA DE MARCACAO por anilha lida do relogio.
    var box = document.getElementById('trRings');
    var keep = {};
    var old = box.querySelectorAll("input[type='checkbox']");
    for (var k = 0; k < old.length; k++) if (old[k].checked) keep[old[k].value] = 1;
    box.innerHTML = '';
    var seen = {};
    for (var m = 0; m < arr.length; m++) {
        var ring = String(rmPick(arr[m], ['ring','anilha']) || '').trim();
        if (ring.length !== 11 || seen[ring]) continue;
        seen[ring] = 1;
        var lab = document.createElement('label');
        var cb  = document.createElement('input');
        cb.type = 'checkbox'; cb.value = ring; cb.checked = !!keep[ring];
        var sp  = document.createElement('span');
        sp.textContent = ring;
        lab.appendChild(cb); lab.appendChild(sp);
        box.appendChild(lab);
    }
    if (!box.children.length) {
        var em = document.createElement('span');
        em.className = 'rm-muted';
        em.textContent = T('rmbEmpty','sem dados');
        box.appendChild(em);
    }
}

function rmSelAll(on) {
    var cbs = document.getElementById('trRings').querySelectorAll("input[type='checkbox']");
    for (var i = 0; i < cbs.length; i++) cbs[i].checked = !!on;
}

function rmContests(d) {
    var arr = d.contests || [];
    var rows = [];
    for (var i = 0; i < arr.length; i++) {
        var a = arr[i];
        rows.push([ (i + 1) + '.', rmPick(a, ['code']), rmPick(a, ['date']), rmPick(a, ['name']),
                    rmPick(a, ['pigeonCount','count']) ]);
    }
    rmTable('tblRace', [T('rmbNo','No'), T('rmbRace','Prova'), T('rmbDate','Data'),
                        'Nome', T('rmbPigeons','Pombos:')], rows);
}

/* ---- refresh ---- */
function rmRefresh() {
    var D = rmd();
    rmGet(D.status).then(rmStatus).catch(function(){});
    rmGet(D.arrivals).then(rmArrivals).catch(function(){});
    rmGet(D.pombal).then(rmPombal).catch(function(){});
    rmGet(D.contests).then(rmContests).catch(function(){});
}

function rmPush(on) {
    rmPost(rmd().push, 'on=' + (on ? '1' : '0') + '&auto=1', document.getElementById('arrmsg'));
}

function rmAct(kind) {
    var el = {poll:'arrmsg', syncPombal:'pommsg', syncCalendar:'racemmsg'}[kind] || null;
    var box = el ? document.getElementById(el) : null;
    if (kind === 'emailPombal') { rmPost(rmd().emailPombal, 'pombal=current', box); return; }
    rmPost(rmd()[kind], null, box);
}

/* ---- config ---- */
function rmSaveCfg() {
    var D = rmd(), el = document.getElementById('cfgmsg');
    var b = 'enabled=' + (document.getElementById('cfg_enabled').checked ? '1' : '0') +
            '&interval=' + rmIntervalSec() +
            '&manual=' + (document.getElementById('cfg_manual').checked ? '1' : '0');
    if (rmDev === 'unikon') {
        b += '&official_format=' + (document.getElementById('cfg_official').checked ? '1' : '0');
    } else {
        b += '&baud=' + document.getElementById('cfg_baud').value;
        b += '&data=' + document.getElementById('cfg_data').value;
        b += '&parity=' + document.getElementById('cfg_parity').value;
        b += '&stop=' + document.getElementById('cfg_stop').value;
        b += '&country=' + document.getElementById('cfg_country').value;
        b += '&ba=' + encodeURIComponent(document.getElementById('cfg_ba').value || '0');
        b += '&club=' + encodeURIComponent(document.getElementById('cfg_club').value);
        b += '&flight=' + encodeURIComponent(document.getElementById('cfg_flight').value);
    }
    rmPost(D.config, b, el);
}

/* Back to the module's own serial profile (leaves bench mode). The profile stored
 * in unikon.json / bricon.json was never touched by the RS232 page, so this simply
 * re-applies it. */
function rmRestoreProfile() {
    var el = document.getElementById('cfgmsg');
    document.getElementById('cfg_manual').checked = false;
    rmPost(rmd().config, 'manual=0', el);
}

/* ---- training ---- */
function rmTrainMode() {
    var m = document.getElementById('trMode').value;
    document.getElementById('trRingsRow').style.display = (m === 'list') ? '' : 'none';
}

function rmCheckedRings() {
    var out = [];
    var cbs = document.getElementById('trRings').querySelectorAll("input[type='checkbox']");
    for (var i = 0; i < cbs.length; i++) if (cbs[i].checked) out.push(cbs[i].value);
    return out;
}

function rmSendTraining() {
    var D = rmd(), el = document.getElementById('trainmsg');
    if (!D.trainingMode) { rmPost(D.training, null, el); return; }
    var m = document.getElementById('trMode').value;
    if (m === 'none') { rmPost(D.training, 'rings=-', el); return; }   /* "-" = so a prova */
    if (m === 'all')  { rmPost(D.training, 'rings=*', el); return; }   /* "*" = pombal todo */
    var rings = rmCheckedRings();
    if (!rings.length) { rmMsg(el, T('rmbChecklistTip','Marque um ou vários pombos.'), false); return; }
    rmPost(D.training, 'rings=' + encodeURIComponent(rings.join('\n')), el);
}

/* ---- boot ---- */
(function(){
    try {
        var sd = localStorage.getItem('rm_dev');
        var st = localStorage.getItem('rm_tab');
        var qd = (typeof URLSearchParams !== 'undefined')
               ? new URLSearchParams(location.search).get('dev') : null;
        if (qd && RM_DEV[qd]) rmDev = qd;
        else if (sd && RM_DEV[sd]) rmDev = sd;
        if (st) rmCurTab = st;
    } catch (e) {}
    document.getElementById('rmDev').value = rmDev;
    rmSelect();
    rmTab(rmCurTab);
    // re-render depois do i18n carregar (cabecalhos/titulos usam _t)
    if (window.applyI18n) { try { window.applyI18n(); } catch (e) {} }
    setTimeout(rmRefresh, 900);
    setInterval(function(){ if (rmCurTab === 'arrivals' || rmCurTab === 'status') rmRefresh(); }, 5000);
})();
</script>
</div></main>
)rawliteral";
