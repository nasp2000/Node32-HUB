process.on('unhandledRejection', function(){});
var fs=require('fs'),path=require('path'),jsdom=require('jsdom'),{JSDOM}=jsdom;
var html=fs.readFileSync(path.join(__dirname,'pages','unikon_asset.html'),'utf8').toString();
var dom=new JSDOM(html,{runScripts:'dangerously',url:'http://localhost'});
var win=dom.window,doc=win.document;
var passed=0,failed=0;
function assert(c,m){if(c)passed++;else{console.log('  FAIL:',m);failed++;}}
function assertEq(a,e,m){if(a===e)passed++;else{console.log('  FAIL:',m,'exp:',JSON.stringify(e),'got:',JSON.stringify(a));failed++;}}
console.log('\n=== Unikon Page Tests ===\n');
assertEq(win.escapeHtml('<>&"'),'&lt;&gt;&amp;&quot;','escapeHtml escapes');
assertEq(win.escapeHtml('hello'),'hello','escapeHtml passes through');
assertEq(win.escapeHtml(''),'','escapeHtml empty');
assertEq(win.escapeHtml(null),'','escapeHtml null');
var html=win.buildArrivalsRows([{ring:'PT123',time:'103000',date:'2212',chip:'12345',race:'T26071430'}],'Sem dados');
assert(html.indexOf('PT123')>=0,'buildArrivalsRows shows ring');
assert(html.indexOf('T26071430')>=0,'buildArrivalsRows shows race');
assert(html.indexOf('10:30:00')>=0,'buildArrivalsRows shows time');
assert(html.indexOf('22/12')>=0,'buildArrivalsRows shows date');
var emptyHtml=win.buildArrivalsRows([],'Nenhuma chegada');
assert(emptyHtml.indexOf('Nenhuma chegada')>=0,'buildArrivalsRows empty shows message');
assert(typeof win.loadStatus==='function','loadStatus is function');
assert(typeof win.loadArrivals==='function','loadArrivals is function');
assert(typeof win.loadContests==='function','loadContests is function');
assert(typeof win.syncCalendar==='function','syncCalendar is function');
assert(typeof win.pollNow==='function','pollNow is function');
assert(typeof win.saveUnikonConfig==='function','saveUnikonConfig is function');
assert(typeof win.sendTraining==='function','sendTraining is function');
assert(typeof win.clearTraining==='function','clearTraining is function');
assert(win.trainingPending===false,'trainingPending starts false');
assert(win.clearTrainingPending===false,'clearTrainingPending starts false');
assert(typeof win.splitArrivals==='function','splitArrivals is function');
assert(win.document.getElementById('trainingPombalSelect')!==null,'training pombal box exists');
assert(typeof win.selectedTrainingRings==='function','selectedTrainingRings is function');
assert(typeof win.trainingCheckAll==='function','trainingCheckAll is function');
(function(){
  var box = win.document.getElementById('trainingPombalSelect');
  function addBox(value, checked) {
    var lab = win.document.createElement('label');
    var cb = win.document.createElement('input');
    cb.type = 'checkbox'; cb.value = value; cb.checked = !!checked;
    lab.appendChild(cb); box.appendChild(lab);
  }
  assert(win.selectedTrainingRings(box).length===0,'no selection by default');
  assert(win.selectedTrainingRings(null).length===0,'null box is safe');
  addBox('24004342525', false);
  addBox('02002830633', true);
  var got = win.selectedTrainingRings(box);
  assert(got.length===1 && got[0]==='02002830633','only checked boxes returned');
  win.trainingCheckAll(true);
  assert(win.selectedTrainingRings(box).length===2,'check all selects all');
  win.trainingCheckAll(false);
  assert(win.selectedTrainingRings(box).length===0,'uncheck all clears');
})();
// sendTraining payload sentinels: none -> "-", all -> "*", some -> the ring list
// (the server maps them to UNIKON_TRAIN_MODE_NONE / ALL / LIST).
(function(){
  var box = win.document.getElementById('trainingPombalSelect');
  box.innerHTML = '';
  function addBox(value, checked) {
    var lab = win.document.createElement('label');
    var cb = win.document.createElement('input');
    cb.type = 'checkbox'; cb.value = value; cb.checked = !!checked;
    lab.appendChild(cb); box.appendChild(lab);
  }
  var sent = [];
  win.fetch = function(url, opts) {
    sent.push((opts && opts.body) || '');
    return Promise.resolve({ json: function(){ return Promise.resolve({success:true,message:'ok'}); } });
  };
  addBox('24004342525', false);
  addBox('02002830633', false);
  win.sendTraining();
  assertEq(sent.length, 1, 'sendTraining posts once');
  assertEq(sent[0], 'rings=-', 'none ticked sends the "-" sentinel');
  win.trainingCheckAll(true);
  win.sendTraining();
  assertEq(sent[1], 'rings=*', 'all ticked sends the "*" sentinel');
  win.trainingCheckAll(false);
  box.querySelectorAll('input[type="checkbox"]')[0].checked = true;
  win.sendTraining();
  assertEq(sent[2], 'rings=24004342525', 'some ticked sends the ring list');
})();
var _sp = win.splitArrivals([{ring:'A',kind:'race'},{ring:'B',kind:'training'},{ring:'C'}]);
assert(_sp.race.length===2 && _sp.training.length===1,'splitArrivals separates race/training, missing kind defaults race');
var _spEmpty = win.splitArrivals([]);
assert(_spEmpty.race.length===0 && _spEmpty.training.length===0,'splitArrivals handles empty');
// EN/PT parity on the REAL firmware asset: every data-i18n + t() key must
// exist in both dictionaries; PT must differ except for the allowlist below
// (technical terms / proper nouns identical in both languages).
(function(){
  var asset=fs.readFileSync(path.join(__dirname,'..','..','chatgpt','src','webpages','assets','page_unikon_asset.h'),'utf8').toString();
  function dict(src){var o={},m,re=/\"([^\"]+)\":\s*\"((?:[^\"\\]|\\.)*)\"/g;while((m=re.exec(src)))o[m[1]]=m[2];return o;}
  var en=dict(fs.readFileSync(path.join(__dirname,'..','..','chatgpt','src','languages','lang_en_ui.h'),'utf8').toString());
  var pt=dict(fs.readFileSync(path.join(__dirname,'..','..','chatgpt','src','languages','lang_pt_ui.h'),'utf8').toString());
  var keys={},m2,re2=/data-i18n='([^']+)'/g;while((m2=re2.exec(asset)))keys[m2[1]]=1;
  re2=/(?<![\w])t\('([^']+)'/g;while((m2=re2.exec(asset)))keys[m2[1]]=1;
  var allow={'ukColChip':1,'ukPollingManual':1};
  var names=Object.keys(keys).sort(),missing=0,same=0;
  for(var i=0;i<names.length;i++){var k=names[i];
    if(!(k in en)||!(k in pt)){console.log('  FAIL: i18n key missing: '+k+' en='+((k in en))+' pt='+((k in pt)));missing++;}
    else if(en[k]===pt[k]&&!allow[k]){console.log('  FAIL: i18n key identical EN==PT: '+k);same++;}
  }
  assert(missing===0,'all '+names.length+' real-asset keys exist in EN+PT');
  assert(same===0,'no untranslated EN==PT values outside allowlist');
})();
dom.window.close();
console.log('\nResults: '+passed+'/'+(passed+failed)+' passed, '+failed+' failed\n');
if(failed>0)process.exit(1);
