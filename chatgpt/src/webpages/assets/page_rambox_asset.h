// page_rambox_asset.h
// Unified RAMBOX page: ONE device selector (Unikon / Bricon / future clocks) and the
// SAME tab structure for every device (Config, Arrivals, Pigeons, Race, Status).
// Only one clock is ever connected to an ESP32, so the selector just picks which
// device's configuration/data the tabs show -- the hub still enforces the exclusion.
//
// Adding a new clock family = adding one entry to the DEV registry below (endpoints +
// config fields); the page renders itself from that description.
//
// Does NOT include DOCTYPE/html/head/body -- the shell renderer adds those.

#pragma once
#include <pgmspace.h>

static const char PAGE_RAMBOX[] PROGMEM = R"rawliteral(
<main class='page'><div class='wrap'>
<style>
    .rm-tabs{display:flex;gap:6px;flex-wrap:wrap;margin:10px 0 0}
    .rm-tab{padding:8px 14px;border-radius:8px 8px 0 0;background:#1e293b;color:#cbd5e1;
            border:1px solid #334155;cursor:pointer;font-size:13px}
    .rm-tab.active{background:#1d4ed8;color:#fff;border-color:#3b82f6}
    .rm-card{background:#0f172a;border:1px solid #334155;border-radius:0 10px 10px 10px;padding:12px;margin:0 0 10px}
    .rm-row{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin:6px 0}
    .rm-row label{color:#cbd5e1;font-size:13px}
    .rm-card input,.rm-card select,.rm-card textarea{background:#1e293b;color:#e2e8f0;
            border:1px solid #475569;border-radius:6px;padding:6px}
    .rm-card table{width:100%;border-collapse:collapse;font-size:13px}
    .rm-card th,.rm-card td{border:1px solid #334155;padding:5px 7px;text-align:left}
    .rm-card th{background:#1e293b;color:#93c5fd;position:sticky;top:0}
    .rm-badge{padding:2px 9px;border-radius:10px;font-size:12px;background:#334155;color:#e2e8f0}
    .rm-badge.on{background:#166534}
    .rm-badge.off{background:#7f1d1d}
    .rm-muted{color:#94a3b8;font-size:12px}
    .rm-ok{color:#4ade80}.rm-err{color:#f87171}
    .rm-scroll{max-height:52vh;overflow:auto}
</style>

<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='btnHome'>&#127968; Home</a></div>
<h1>&#128038; RAMBOX</h1>

<div class='rm-row'>
    <label><b>Aparelho:</b></label>
    <select id='rmDev' onchange='rmSelect()'>
        <option value='unikon'>&#128038; Unikon RAMBOX</option>
        <option value='bricon'>&#128225; Bricon</option>
    </select>
    <span id='rmBadge' class='rm-badge'>—</span>
    <span class='rm-muted'>Só um relógio pode estar ligado/activo em cada ESP32.</span>
</div>

<div class='rm-tabs'>
    <button class='rm-tab active' data-tab='config'   onclick='rmTab("config")'>&#9881; Configuração</button>
    <button class='rm-tab'        data-tab='arrivals' onclick='rmTab("arrivals")'>&#128229; Chegadas</button>
    <button class='rm-tab'        data-tab='pombal'   onclick='rmTab("pombal")'>&#128038; Pombos</button>
    <button class='rm-tab'        data-tab='race'     onclick='rmTab("race")'>&#127937; Prova</button>
    <button class='rm-tab'        data-tab='status'   onclick='rmTab("status")'>&#128202; Estado</button>
</div>

<!-- ================= CONFIG ================= -->
<section id='rmp-config' class='rm-pane'>
  <div class='rm-card'>
    <div class='rm-row'>
        <label><input type='checkbox' id='cfg_enabled'> Relógio activo</label>
        <label>Intervalo de poll (s) <input type='number' id='cfg_interval' min='0' max='3600' style='width:90px'></label>
    </div>

    <div id='cfgdev_unikon' class='rm-devcfg'>
        <div class='rm-row'>
            <label><input type='checkbox' id='cfg_official'> Formato oficial (columbofilia)</label>
        </div>
        <p class='rm-muted'>Ligação fixa do Unikon: 9600 8N1. &nbsp; Email: <span id='rmEmailDest'>—</span></p>
    </div>

    <div id='cfgdev_bricon' class='rm-devcfg'>
        <div class='rm-row'>
            <label>Baud <select id='cfg_baud'>
                <option>9600</option><option>19200</option><option>38400</option>
                <option>57600</option><option>115200</option></select></label>
            <label>Dados <select id='cfg_data'><option>8</option><option>7</option></select></label>
            <label>Paridade <select id='cfg_parity'>
                <option value='none'>N</option><option value='even'>E</option><option value='odd'>O</option></select></label>
            <label>Stop <select id='cfg_stop'><option>2</option><option>1</option></select></label>
            <label>País <select id='cfg_country'>
                <option>PT</option><option>UK</option><option>BE</option><option>ES</option>
                <option>FR</option><option>NL</option></select></label>
        </div>
        <div class='rm-row'>
            <label>Endereço BA <input type='number' id='cfg_ba' min='0' max='255' style='width:80px' placeholder='auto'></label>
            <label>Clube <input id='cfg_club' maxlength='4' style='width:70px'></label>
            <label>Voo <input id='cfg_flight' maxlength='4' style='width:70px'></label>
        </div>
        <p class='rm-muted'>Bricon usa 9600 8N2 (a página aplica o perfil automaticamente).</p>
    </div>
  </div>
</section>

<!-- ================= ARRIVALS ================= -->
<section id='rmp-arrivals' class='rm-pane' style='display:none'>
  <div class='rm-card'>
    <div class='rm-row'>
        <button onclick='rmRefresh()'>&#128260; Actualizar</button>
        <button onclick='rmAct("poll")'>&#128225; Poll agora</button>
        <button onclick='rmAct("syncPombal")'>&#128038; Sincronizar pombal</button>
        <button onclick='rmAct("syncCalendar")'>&#127937; Sincronizar calendário</button>
        <span id='pushWrap' style='display:none'>
            <button onclick='rmPush(1)'>&#128225; Push ON</button>
            <button onclick='rmPush(0)'>&#128225; Push OFF</button>
        </span>
        <span id='arrmsg' class='rm-muted'></span>
    </div>
    <div class='rm-row' id='rmStats' style='gap:16px'></div>
    <div class='rm-scroll'><table id='tblArr'><thead></thead><tbody></tbody></table></div>
  </div>
</section>

<!-- ================= PIGEONS ================= -->
<section id='rmp-pombal' class='rm-pane' style='display:none'>
  <div class='rm-card'>
    <div class='rm-row'>
        <button onclick='rmRefresh()'>&#128260; Actualizar</button>
        <button onclick='rmAct("syncPombal")'>&#128229; Ler do relógio</button>
        <button id='btnEmailPombal' onclick='rmAct("emailPombal")'>&#9993; Email do pombal</button>
        <span id='pommsg' class='rm-muted'></span>
    </div>
    <div class='rm-scroll'><table id='tblPom'><thead></thead><tbody></tbody></table></div>
  </div>
</section>

<!-- ================= RACE / TRAINING ================= -->
<section id='rmp-race' class='rm-pane' style='display:none'>
  <div class='rm-card'>
    <div class='rm-row'>
        <button onclick='rmRefresh()'>&#128260; Actualizar</button>
        <button onclick='rmAct("syncCalendar")'>&#128229; Ler provas do relógio</button>
        <span id='racemmsg' class='rm-muted'></span>
    </div>
    <div class='rm-scroll'><table id='tblRace'><thead></thead><tbody></tbody></table></div>
  </div>

  <div class='rm-card' id='rmTraining'>
    <h3 style='margin:0 0 8px;color:#93c5fd;font-size:15px'>&#127937; Enviar treino</h3>
    <div id='train_unikon'>
        <div class='rm-row'>
            <label>Pombos:
                <select id='trMode' onchange='rmTrainMode()'>
                    <option value='all'>Pombal todo</option>
                    <option value='list'>Só os indicados</option>
                    <option value='none'>Só a prova (sem pombos)</option>
                </select>
            </label>
        </div>
        <div class='rm-row' id='trRingsRow' style='display:none'>
            <select id='trRings' multiple size='9' style='min-width:230px'></select>
            <span class='rm-muted'>Ctrl/Shift para escolher vários.<br>As anilhas vêm do pombal lido.</span>
        </div>
        <p class='rm-muted'>O clube envia a prova com os pombos dela; o relógio acumula provas (não existe "apagar pombo").</p>
    </div>
    <div id='train_bricon' class='rm-muted' style='display:none'>
        Escreve a prova de treino/Bricon no relógio.
    </div>
    <div class='rm-row'>
        <button onclick='rmSendTraining()'>&#9654; Enviar treino</button>
        <span id='trainmsg' class='rm-muted'></span>
    </div>
  </div>
</section>

<!-- ================= STATUS ================= -->
<section id='rmp-status' class='rm-pane' style='display:none'>
  <div class='rm-card'>
    <div class='rm-row'>
        <button onclick='rmRefresh()'>&#128260; Actualizar</button>
        <button onclick='rmAct("reset")' style='background:#7f1d1d'>&#128465; Limpar dados do relógio</button>
        <span id='stmsg' class='rm-muted'></span>
    </div>
    <div class='rm-scroll'><table id='tblStatus'><thead></thead><tbody></tbody></table></div>
  </div>
</section>

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
        reset:        '/api/unikon-reset',
        training:     '/api/unikon-send-training',
        trainingMode: true,
        arrCols: ['No','Anilha','Chip','Hora','Data','Prova','Pos','Eval'],
        pomCols: ['No','Anilha','Chip','Hora','Data','Prova']
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
        reset:        '/api/bricon-reset',
        training:     '/api/bricon-send-training',
        trainingMode: false,
        arrCols: ['No','Anilha','Chip','Hora','Data','Prova','Pos','Eval'],
        pomCols: ['No','Anilha','Chip']
    }
};

var rmDev = 'unikon', rmCurTab = 'config';
var rmLastStatus = null;

function rmD() { return RM_DEV[rmDev]; }

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

function rmPost(url, body, msgEl) {
    if (!url) return;
    var opt = {method:'POST'};
    if (body !== null && body !== undefined) {
        opt.headers = {'Content-Type':'application/x-www-form-urlencoded'};
        opt.body = body;
    }
    fetch(url, opt).then(function(r){ return r.json().catch(function(){ return {}; }); })
        .then(function(d){
            if (msgEl) {
                msgEl.textContent = (d && (d.message || d.msg)) || ('HTTP ok');
                msgEl.className = (d && d.success === false) ? 'rm-err' : 'rm-ok';
            }
            setTimeout(rmRefresh, 1200);
        })
        .catch(function(e){ if (msgEl) { msgEl.textContent = 'Erro: ' + e; msgEl.className='rm-err'; } });
}

/* ---- selector / tabs ---- */
function rmSelect() {
    var s = document.getElementById('rmDev');
    rmDev = s.value;
    try { localStorage.setItem('rm_dev', rmDev); } catch (e) {}
    document.getElementById('cfgdev_unikon').style.display = (rmDev === 'unikon') ? '' : 'none';
    document.getElementById('cfgdev_bricon').style.display = (rmDev === 'bricon') ? '' : 'none';
    document.getElementById('train_unikon').style.display = rmD().trainingMode ? '' : 'none';
    document.getElementById('train_bricon').style.display = rmD().trainingMode ? 'none' : '';
    document.getElementById('btnEmailPombal').style.display = rmD().emailPombal ? '' : 'none';
    document.getElementById('pushWrap').style.display = (rmDev === 'unikon') ? '' : 'none';
    rmRefresh();
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
    var th = '<tr>';
    for (var i = 0; i < cols.length; i++) th += '<th>' + rmEsc(cols[i]) + '</th>';
    th += '</tr>';
    t.tHead.innerHTML = th;
    var body = '';
    for (var r = 0; r < rows.length; r++) {
        body += '<tr>';
        for (var c = 0; c < rows[r].length; c++) body += '<td>' + rmEsc(rows[r][c]) + '</td>';
        body += '</tr>';
    }
    if (!rows.length) body = '<tr><td colspan="' + cols.length + '" class="rm-muted">sem dados</td></tr>';
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
    var en = !!d.enabled;
    var b = document.getElementById('rmBadge');
    b.textContent = rmD().label + ' — ' + (en ? 'activo' : 'inactivo') +
                    (d.connected ? ' · ligado' : '') +
                    (d.activeModule && d.activeModule !== rmDev ? ' · activo: ' + d.activeModule : '');
    b.className = 'rm-badge ' + (en ? 'on' : 'off');

    document.getElementById('cfg_enabled').checked = en;
    var iv = rmPick(d, ['intervalSec','interval']);
    if (iv !== '') document.getElementById('cfg_interval').value = iv;
    if (rmDev === 'unikon') {
        document.getElementById('cfg_official').checked = d.officialFormat !== false;
        var ed = document.getElementById('rmEmailDest');
        if (ed) ed.textContent = d.emailDest || '(nao configurado)';
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
    rmTable('tblStatus', ['Campo','Valor'], rows);
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
    rmTable('tblArr', rmD().arrCols, rows);
    rmStats(rows.length);
}

function rmStats(nArr) {
    var d = rmLastStatus || {};
    var ago = rmPick(d, ['lastPollAgoSec','pollAgoSec']);
    var el = document.getElementById('rmStats');
    if (!el) return;
    el.innerHTML =
        '<span class="rm-badge">Chegadas: ' + nArr + '</span>' +
        '<span class="rm-badge">Novas: ' + rmPick(d, ['lastNewCount','newCount']) + '</span>' +
        '<span class="rm-badge">' + (ago === '' ? 'sem poll' : ('poll há ' + ago + ' s')) + '</span>' +
        '<span class="rm-badge">' + (d.connected ? 'ligado' : 'desligado') + '</span>';
}

function rmPombal(d) {
    var arr = d.registrations || d.pombal || [];
    var rows = [];
    for (var i = 0; i < arr.length; i++) {
        var a = arr[i];
        rows.push([
            (i + 1) + '.',
            rmPick(a, ['ring','anilha']),
            rmPick(a, ['chip']),
            rmTime(rmPick(a, ['time','arrivalTime','hhmmss'])),
            rmDate(rmPick(a, ['date','arrivalDate','ddmm'])),
            rmPick(a, ['race','raceName'])
        ]);
    }
    rmTable('tblPom', rmD().pomCols, rows);

    // Checklist do treino: uma opcao por anilha lida do relogio.
    var sel = document.getElementById('trRings');
    var keep = [];
    for (var k = 0; k < sel.options.length; k++) if (sel.options[k].selected) keep.push(sel.options[k].value);
    sel.innerHTML = '';
    for (var m = 0; m < arr.length; m++) {
        var ring = String(rmPick(arr[m], ['ring','anilha']) || '').trim();
        if (ring.length !== 11) continue;
        var o = document.createElement('option');
        o.value = ring;
        o.textContent = ring;
        if (keep.indexOf(ring) >= 0) o.selected = true;
        sel.appendChild(o);
    }
}

function rmContests(d) {
    var arr = d.contests || [];
    var rows = [];
    for (var i = 0; i < arr.length; i++) {
        var a = arr[i];
        rows.push([ (i + 1) + '.', rmPick(a, ['code']), rmPick(a, ['date']), rmPick(a, ['name']),
                    rmPick(a, ['pigeonCount','count']) ]);
    }
    rmTable('tblRace', ['No','Código','Data','Nome','Pombos'], rows);
}

/* ---- refresh ---- */
function rmRefresh() {
    var D = rmD();
    rmGet(D.status).then(rmStatus).catch(function(){});
    rmGet(D.arrivals).then(rmArrivals).catch(function(){});
    rmGet(D.pombal).then(rmPombal).catch(function(){});
    rmGet(D.contests).then(rmContests).catch(function(){});
}

function rmPush(on) {
    var el = document.getElementById('arrmsg');
    rmPost('/api/unikon-push', 'on=' + (on ? '1' : '0') + '&auto=1', el);
}

function rmAct(kind) {
    var D = rmD();
    var msg = {poll:'arrmsg', syncPombal:'pommsg', syncCalendar:'racemmsg', reset:'stmsg'}[kind] || null;
    var el = msg ? document.getElementById(msg) : null;
    if (kind === 'emailPombal') { rmPost(D.emailPombal, 'pombal=current', el); return; }
    rmPost(D[kind], null, el);
}

/* ---- config ---- */
function rmSaveCfg() {
    var D = rmD(), el = document.getElementById('cfgmsg');
    var b = 'enabled=' + (document.getElementById('cfg_enabled').checked ? '1' : '0') +
            '&interval=' + encodeURIComponent(document.getElementById('cfg_interval').value);
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

/* ---- training ---- */
function rmTrainMode() {
    var m = document.getElementById('trMode').value;
    document.getElementById('trRingsRow').style.display = (m === 'list') ? '' : 'none';
}

function rmSendTraining() {
    var D = rmD(), el = document.getElementById('trainmsg');
    if (!D.trainingMode) { rmPost(D.training, null, el); return; }
    var m = document.getElementById('trMode').value;
    if (m === 'none') { rmPost(D.training, 'rings=-', el); return; }   /* "-" = so a prova */
    if (m === 'all')  { rmPost(D.training, 'rings=*', el); return; }   /* "*" = pombal todo */
    var sel = document.getElementById('trRings');
    var lines = [];
    for (var i = 0; i < sel.options.length; i++) if (sel.options[i].selected) lines.push(sel.options[i].value);
    rmPost(D.training, 'rings=' + encodeURIComponent(lines.join('\n')), el);
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
    setInterval(function(){ if (rmCurTab === 'arrivals' || rmCurTab === 'status') rmRefresh(); }, 5000);
})();
</script>

<div class='actions' style='margin:14px 0 24px'>
    <button onclick='rmSaveCfg()'>&#128190; Guardar configuração</button>
    <span id='cfgmsg' class='rm-muted'></span>
</div>
</div></main>
)rawliteral";
