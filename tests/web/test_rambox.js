// test_rambox.js — unified RAMBOX page (selector Unikon/Bricon, same tabs, checklists).
// Runs the extracted page with the mock API layer (extract.py) and checks every button.
process.on('unhandledRejection', function(){});
var fs = require('fs'), path = require('path'), { JSDOM } = require('jsdom');
var html = fs.readFileSync(path.join(__dirname, 'pages', 'rambox_asset.html'), 'utf8').toString();
var dom = new JSDOM(html, { runScripts: 'dangerously', url: 'http://localhost/rambox?dev=unikon' });
var win = dom.window, doc = win.document;
var passed = 0, failed = 0;

function assert(c, m) { if (c) passed++; else { console.log('  FAIL:', m); failed++; } }
function btn(txt) {
    var bs = doc.querySelectorAll('button');
    for (var i = 0; i < bs.length; i++) if (bs[i].textContent.indexOf(txt) >= 0) return bs[i];
    return null;
}
function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }

win.__MOCK_API__ = {
    '/api/unikon-status': { enabled: true, connected: true, intervalSec: 180, officialFormat: true,
                            emailDest: 'nasp@x.pt', lastNewCount: 2, lastPollAgoSec: 5 },
    '/api/unikon-arrivals': { count: 1, arrivals: [{ ring: '23762610000', chip: 'C9600111101',
                            time: '080000', date: '2604', race: '001', posArriveClub: 1, evaluation: '1' }] },
    '/api/unikon-pombal': { count: 2, registrations: [{ ring: '23762610000', chip: '' },
                            { ring: '23762610137', chip: 'C9600222201' }] },
    '/api/unikon-contests': { count: 1, contests: [{ code: '090', date: '1910', name: 'T1910221' }] },
    '/api/bricon-status': { enabled: true, connected: true, intervalSec: 180, baud: 9600, dataBits: 8,
                            parity: 'none', stopBits: 2, country: 'PT', baActiveAddress: 130,
                            clubId: '1234', flightId: '0001', lastNewCount: 3, lastPollAgoSec: 1 },
    '/api/bricon-arrivals': { count: 1, arrivals: [{ ring: 'PORTUGAL123450', chip: 'C54A7B9A',
                            time: '142500', date: '191026', race: '0001', posArriveClub: 10, evaluation: '1' }] },
    '/api/bricon-pombal': { count: 1, registrations: [{ ring: '00812345', chip: '' }] },
    '/api/bricon-contests': { count: 1, contests: [{ code: '001', date: '191026', name: 'PROVA A' }] }
};

function lastPost(u) {
    var b = win.__postBodies || [];
    for (var i = b.length - 1; i >= 0; i--) if (b[i].url.indexOf(u) >= 0) return b[i];
    return null;
}

(async function () {
    console.log('\n=== Rambox Page Tests ===\n');
    win.rmRefresh();
    await sleep(300);

    // ---- structure ----
    assert(doc.querySelector('.wrap') !== null, 'standard page layout (.wrap)');
    assert(doc.querySelector("a[href='/'][data-i18n='btnHome']") !== null, 'Home button at the top');
    assert(doc.querySelectorAll('.rm-tab').length === 5, '5 tabs');
    assert(doc.querySelectorAll('.rm-pane').length === 5, '5 panes');
    assert(btn('Limpar dados') === null, 'no "clear data" button');
    assert(doc.querySelectorAll('[data-i18n-title]').length >= 30, 'tooltips on the controls');
    assert(doc.querySelector('#rmDev').getAttribute('class').indexOf('rm-sel') >= 0, 'compact device selector');
    assert(doc.querySelectorAll('#tblPom th').length === 3, 'pombal table has 3 columns (fits a phone)');
    setTimeout(function(){}, 0);

    // ---- Unikon actions ----
    // ---- poll interval: fixed minute steps, 3 min default ----
    var ivSel = doc.getElementById('cfg_interval');
    assert(ivSel && ivSel.tagName === 'SELECT', 'poll interval is a selector');
    var ivOpts = [];
    for (var oi = 0; oi < ivSel.options.length; oi++) ivOpts.push(ivSel.options[oi].value);
    assert(ivOpts.join(',') === '1,3,5,10,15,30,60', 'interval options 1/3/5/10/15/30/60 min');
    assert(ivSel.value === '3', 'interval defaults to 3 min (status 180 s)');
    assert(win.rmIntervalOption(0) === '3', '0 s -> 3 min default');
    assert(win.rmIntervalOption(120) === '3', '2 min snaps to 3 min');
    assert(win.rmIntervalOption(900) === '15', '15 min kept');

    doc.getElementById('cfg_interval').value = '5';
    btn('Guardar configura').click(); await sleep(40);
    var c = lastPost('/api/unikon-config');
    assert(c && /interval=300/.test(c.body) && /official_format=1/.test(c.body), 'save unikon body (min -> s)');
    assert(c && /manual=0/.test(c.body), 'own profile is the default (manual=0)');

    // bench mode: module uses the RS232 page settings, own profile untouched
    assert(doc.getElementById('cfg_manual') !== null, 'bench-mode checkbox present');
    assert(doc.getElementById('cfg_manual').checked === false, 'bench mode off by default');
    doc.getElementById('cfg_manual').checked = true;
    btn('Guardar configura').click(); await sleep(40);
    c = lastPost('/api/unikon-config');
    assert(c && /manual=1/.test(c.body), 'bench mode saved (manual=1)');
    doc.getElementById('cfg_manual').checked = false;

    // one click back to the module's own profile
    btn('Perfil do m').click(); await sleep(40);
    c = lastPost('/api/unikon-config');
    assert(c && /manual=0/.test(c.body), 'restore button leaves bench mode');

    btn('Poll agora').click(); await sleep(30);
    assert(lastPost('/api/unikon-poll'), 'poll unikon');
    btn('Sincronizar pombal').click(); await sleep(30);
    assert(lastPost('/api/unikon-sync-pombal'), 'sync pombal unikon');
    btn('Sincronizar calend').click(); await sleep(30);
    assert(lastPost('/api/unikon-sync-calendar'), 'sync calendar unikon');
    btn('Email do pombal').click(); await sleep(30);
    assert(lastPost('/api/unikon-email-pombal'), 'loft email unikon');
    btn('Push ON').click(); await sleep(30);
    assert(/on=1/.test(lastPost('/api/unikon-push').body), 'push ON');
    btn('Push OFF').click(); await sleep(30);
    assert(/on=0/.test(lastPost('/api/unikon-push').body), 'push OFF');

    // ---- training: checkbox list ----
    var cbs = doc.querySelectorAll("#trRings input[type='checkbox']");
    assert(cbs.length === 2, 'one checkbox per pigeon (' + cbs.length + ')');
    var b = btn('Enviar treino');

    doc.getElementById('trMode').value = 'all'; win.rmTrainMode(); b.click(); await sleep(30);
    assert(lastPost('/api/unikon-send-training').body === 'rings=*', 'whole loft -> rings=*');
    doc.getElementById('trMode').value = 'none'; win.rmTrainMode(); b.click(); await sleep(30);
    assert(lastPost('/api/unikon-send-training').body === 'rings=-', 'race only -> rings=-');

    doc.getElementById('trMode').value = 'list'; win.rmTrainMode();
    win.rmSelAll(1);
    cbs = doc.querySelectorAll("#trRings input[type='checkbox']");
    assert(cbs[0].checked && cbs[1].checked, 'select all ticks every pigeon');
    b.click(); await sleep(30);
    assert(/rings=23762610000%0A23762610137/.test(lastPost('/api/unikon-send-training').body),
           'ticked rings are sent');

    win.rmSelAll(0);
    var n0 = (win.__postBodies || []).length;
    b.click(); await sleep(30);
    assert((win.__postBodies || []).length === n0, 'nothing ticked -> nothing sent');

    // ---- Bricon ----
    doc.getElementById('rmDev').value = 'bricon'; win.rmSelect(); await sleep(150);
    assert(doc.getElementById('cfgdev_bricon').style.display !== 'none', 'bricon config visible');
    assert(doc.getElementById('cfgdev_unikon').style.display === 'none', 'unikon config hidden');
    assert(doc.getElementById('pushWrap').style.display === 'none', 'push hidden on bricon');
    assert(btn('Email do pombal').style.display === 'none', 'loft email hidden on bricon');
    assert(doc.getElementById('cfg_ba').value === '130', 'BA address loaded');

    doc.getElementById('cfg_country').value = 'ES';
    doc.getElementById('cfg_club').value = '7507';
    btn('Guardar configura').click(); await sleep(40);
    c = lastPost('/api/bricon-config');
    assert(c && /country=ES/.test(c.body) && /club=7507/.test(c.body), 'save bricon body');

    btn('Poll agora').click(); await sleep(30);
    assert(lastPost('/api/bricon-poll'), 'poll bricon');
    btn('Sincronizar pombal').click(); await sleep(30);
    assert(lastPost('/api/bricon-sync-pombal'), 'sync pombal bricon');
    btn('Ler provas').click(); await sleep(30);
    assert(lastPost('/api/bricon-sync-calendar'), 'sync calendar bricon');
    btn('Enviar treino').click(); await sleep(30);
    c = lastPost('/api/bricon-send-training');
    assert(c && (c.body === '' || c.body === null), 'bricon training without body');

    // ---- tabs ----
    ['config', 'arrivals', 'pombal', 'race', 'status'].forEach(function (t) {
        win.rmTab(t);
        assert(doc.getElementById('rmp-' + t).style.display !== 'none', 'tab ' + t + ' visible');
    });

    // ---- config pane follows the module that is actually enabled ----
    win.rmAdopted = false;
    win.rmStatus({ enabled: true, activeModule: 'rambox', intervalSec: 180 });
    assert(doc.getElementById('rmDev').value === 'unikon', 'adopts the enabled RAMBOX module');
    win.rmAdopted = false;
    win.rmStatus({ enabled: true, activeModule: 'bricon', intervalSec: 180 });
    assert(doc.getElementById('rmDev').value === 'bricon', 'adopts the enabled Bricon module');

    console.log('\nResults: ' + passed + '/' + (passed + failed) + ' passed, ' + failed + ' failed');
    process.exit(failed ? 1 : 0);
})();
