var fs=require('fs'),path=require('path'),jsdom=require('jsdom'),{JSDOM}=jsdom;
var html=fs.readFileSync(path.join(__dirname,'pages','wifi.html'),'utf8').toString();
var dom=new JSDOM(html,{runScripts:'dangerously',url:'http://localhost'});
var win=dom.window,doc=win.document;
var passed=0,failed=0;
function assert(c,m){if(c)passed++;else{console.log('  FAIL:',m);failed++;}}
function assertEq(a,e,m){if(a===e)passed++;else{console.log('  FAIL:',m,'exp:',JSON.stringify(e),'got:',JSON.stringify(a));failed++;}}
console.log('\n=== WiFi Page Tests ===\n');
assert(typeof win.wt==='function','wt is a function');
assert(typeof win.sel==='function','sel is a function');
assert(typeof win.scan==='function','scan is a function');
assert(typeof win.loadWifiConfig==='function','loadWifiConfig is a function');
assert(typeof win.applyWifiTexts==='function','applyWifiTexts is a function');
win.localStorage.removeItem('ui_lang_cache');
assertEq(win.wt('nonexistent','fb'),'fb','wt returns fallback');
assertEq(win.wt('nonexistent'),'nonexistent','wt returns key when no fallback');
win.localStorage.setItem('ui_lang_cache',JSON.stringify({wifiSearching:'Scanning...'}));
assertEq(win.wt('wifiSearching','Search'),'Scanning...','wt reads from cache');
win.localStorage.removeItem('ui_lang_cache');
// sel: selects network SSID
var ssidInput=doc.getElementById('ssid');
var pwdInput=doc.getElementById('password');
if(ssidInput&&pwdInput){
  pwdInput.style.display='block';
  win.sel('MyNetwork','WPA2',null);
  assertEq(ssidInput.value,'MyNetwork','sel sets SSID');
  assert(pwdInput.style.display!=='none','sel shows password for encrypted network');
  win.sel('OpenNet','Open',null);
  assertEq(ssidInput.value,'OpenNet','sel sets SSID for open network');
  assertEq(pwdInput.value,'','sel clears password for open network');
  assert(pwdInput.style.display==='none','sel hides password for open network');
}
var alertCalls=[],orig=win.customAlert;
win.customAlert=function(m){alertCalls.push(m);};
if(!win.__MOCK_API__)win.__MOCK_API__={};
win.__MOCK_API__['/api/config']={success:true};
var form=doc.getElementById('send-config-form');
if(form)form.dispatchEvent(new win.Event('submit'));
setTimeout(function(){
  assert(alertCalls.some(function(m){return m.indexOf('guardada')>=0||m.indexOf('✅')>=0;}),'form submit saves config');
  win.customAlert=orig;
  dom.window.close();
  console.log('\nResults: '+passed+'/'+(passed+failed)+' passed, '+failed+' failed\n');
  if(failed>0)process.exit(1);
},50);
