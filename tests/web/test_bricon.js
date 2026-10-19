process.on('unhandledRejection', function(){});
var fs=require('fs'),path=require('path'),jsdom=require('jsdom'),{JSDOM}=jsdom;
var html=fs.readFileSync(path.join(__dirname,'pages','bricon_asset.html'),'utf8').toString();
var dom=new JSDOM(html,{runScripts:'dangerously',url:'http://localhost'});
var win=dom.window,doc=win.document;
var passed=0,failed=0;
function assert(c,m){if(c)passed++;else{console.log('  FAIL:',m);failed++;}}
function assertEq(a,e,m){if(a===e)passed++;else{console.log('  FAIL:',m,'exp:',JSON.stringify(e),'got:',JSON.stringify(a));failed++;}}
console.log('\n=== Bricon Page Tests ===\n');
assertEq(win.besc('<>&"'),'&lt;&gt;&amp;&quot;','besc escapes');
assertEq(win.besc('hello'),'hello','besc passes through');
assertEq(win.besc(''),'','besc empty');
assertEq(win.besc(null),'','besc null');
assertEq(win.bt('briconTitle','Fallback'),'Bricon Rambox','bt resolves data-i18n element text');
assertEq(win.bt('missingKey_xyz','Fallback'),'Fallback','bt falls back on unknown key');
var bar=doc.getElementById('briconTabBar');
assert(bar.querySelectorAll('button').length===4,'tab bar has 4 buttons (Status&Config, Arrivals&Contests, Loft, Training)');
assertEq(win.bkTabs.length,4,'bkTabs has 4 groups');
assert(doc.getElementById('bk-tab-0').classList.contains('active'),'tab panel 0 active by default');
assert(!doc.getElementById('bk-tab-3').classList.contains('active'),'tab panel 3 hidden by default');
win.bkShowTab(3);
assert(doc.getElementById('bk-tab-3').classList.contains('active'),'tab panel 3 shows after select');
assert(!doc.getElementById('bk-tab-0').classList.contains('active'),'tab panel 0 hides after select');
win.bkShowTab(0);
assert(doc.getElementById('bk-tab-0').classList.contains('active'),'tab panel 0 shows again');
assert(doc.getElementById('bk-sec-status')!==null,'bk-sec-status exists (tab Status&Config)');
assert(doc.getElementById('bk-sec-config')!==null,'bk-sec-config exists (tab Status&Config)');
assert(doc.getElementById('bk-sec-arrivals')!==null,'bk-sec-arrivals exists (tab Arrivals&Contests)');
assert(doc.getElementById('bk-sec-contests')!==null,'bk-sec-contests exists (tab Arrivals&Contests)');
assert(doc.getElementById('bk-sec-pombal')!==null,'bk-sec-pombal exists (tab Loft)');
assert(doc.getElementById('bk-sec-training')!==null,'bk-sec-training exists (tab Training)');
assert(typeof win.bkSwitchTab==='function','bkSwitchTab is function');
assert(typeof win.loadBriconStatus==='function','loadBriconStatus is function');
assert(typeof win.briconPoll==='function','briconPoll is function');
assert(typeof win.briconReset==='function','briconReset is function');
assert(typeof win.briconSyncCalendar==='function','briconSyncCalendar is function');
assert(typeof win.briconSyncPombal==='function','briconSyncPombal is function');
assert(typeof win.briconSendTraining==='function','briconSendTraining is function');
// briconClearTraining was removed: Bricon has no Unikon-style clear (no REA evidence).
assert(typeof win.briconClearTraining==='undefined','briconClearTraining is gone');
assert(typeof win.saveBriconConfig==='function','saveBriconConfig is function');
assert(typeof win.saveBriconConfig==='function','saveBriconConfig is function');
assert(doc.getElementById('bkStatTotal')!==null,'stat card arrivals exists');
assert(doc.getElementById('bkStatContests')!==null,'stat card contests exists');
assert(doc.getElementById('bkStatPombal')!==null,'stat card loft exists');
assert(doc.getElementById('bkStatAgo')!==null,'stat card last RX exists');
var _br = win.briconRow({ring:'BE-10-1100002',chip:'C54A7B9A',time:'162444',date:'250412',race:'NOYON',posArriveClub:7,evaluation:'1'},1);
assert(_br.indexOf('BE-10-1100002')>=0,'briconRow shows ring');
assert(_br.indexOf('16:24:44')>=0,'briconRow shows time');
assert(_br.indexOf('25/04')>=0,'briconRow shows date');
assert(_br.indexOf('NOYON')>=0,'briconRow shows race');
// The visible columns are now the RS-232 record fields: position + evaluation
// (category/speed are NOT on the wire -- they live in the club database).
assert(_br.indexOf('7')>=0,'briconRow shows position');
assert(_br.indexOf('>1<')>=0,'briconRow shows evaluation');
assert(win.briconCat(3)==='YOU' && win.briconCat(1)==='OLD' && win.briconCat(0)==='','briconCat maps codes');
var _brEmpty = win.briconRow({ring:'',chip:'',time:'000000',date:'0000',race:''},2);
assert(_brEmpty.indexOf('>-<')>=0,'briconRow dashes missing values');
// EN/PT parity on the REAL firmware asset: every data-i18n + bt() key must
// exist in both dictionaries; PT must differ except for the allowlist below
// (technical terms / proper nouns identical in both languages).
(function(){
  var asset=fs.readFileSync(path.join(__dirname,'..','..','chatgpt','src','webpages','assets','page_bricon_asset.h'),'utf8').toString();
  function dict(src){var o={},m,re=/\"([^\"]+)\":\s*\"((?:[^\"\\]|\\.)*)\"/g;while((m=re.exec(src)))o[m[1]]=m[2];return o;}
  var en=dict(fs.readFileSync(path.join(__dirname,'..','..','chatgpt','src','languages','lang_en_ui.h'),'utf8').toString());
  var pt=dict(fs.readFileSync(path.join(__dirname,'..','..','chatgpt','src','languages','lang_pt_ui.h'),'utf8').toString());
  var keys={},m2,re2=/data-i18n='([^']+)'/g;while((m2=re2.exec(asset)))keys[m2[1]]=1;
  re2=/bt\('([^']+)'/g;while((m2=re2.exec(asset)))keys[m2[1]]=1;
  delete keys['" + s + "']; // JS string-concat artifact inside bt(), not a key
  var allow={'ukColChip':1,'ukPollingManual':1,'rs232BaudLbl':1,'briconTitle':1,'bkColCat':1};
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
