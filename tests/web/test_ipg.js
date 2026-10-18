var fs=require('fs'),path=require('path'),jsdom=require('jsdom'),{JSDOM}=jsdom;
var html=fs.readFileSync(path.join(__dirname,'pages','ipg.html'),'utf8').toString();
var dom=new JSDOM(html,{runScripts:'dangerously',url:'http://localhost'});
var win=dom.window,doc=win.document;
var passed=0,failed=0;
function assert(c,m){if(c)passed++;else{console.log('  FAIL:',m);failed++;}}
function assertEq(a,e,m){if(a===e)passed++;else{console.log('  FAIL:',m,'exp:',JSON.stringify(e),'got:',JSON.stringify(a));failed++;}}
console.log('\n=== IPG Laser Page Tests ===\n');
assert(typeof win.escapeHtml==='function','escapeHtml is a function');
assert(typeof win.renderStatus==='function','renderStatus is a function');
assertEq(win.escapeHtml('<>&"'),'&lt;&gt;&amp;&quot;','escapeHtml escapes special chars');
assertEq(win.escapeHtml('hello'),'hello','escapeHtml passes through safe text');
assertEq(win.escapeHtml(''),'','escapeHtml handles empty string');
assertEq(win.escapeHtml(null),'','escapeHtml handles null');
// renderStatus sets innerHTML of ipgStatus element
var statusEl=doc.getElementById('ipgStatus');
assert(!!statusEl,'ipgStatus element exists');
win.renderStatus({
  currentBaudrate:'115200',requiredBaudrate:'115200',
  currentDataBits:8,requiredDataBits:8,
  currentParity:'None',requiredParity:'none',
  currentStopBits:1,requiredStopBits:1,
  currentFlowControl:false,
  currentTemplate:'default',lastCommand:'START',
  captureActive:false,captureEndsInMs:0,
  remoteModeNote:'Remote'
});
assert(statusEl.innerHTML.indexOf('115200')>=0,'renderStatus shows baudrate');
assert(statusEl.innerHTML.indexOf('START')>=0,'renderStatus shows last command');
assert(statusEl.innerHTML.indexOf('Perfil compat')>=0,'renderStatus shows compatible badge');
// All functions exist
var funcs=['useCommand','sendPreset','loadStatus','loadCommands',
  'checkIpgModuleAvailable','showIpgModuleDisabledMessage','hideIpgModuleDisabledMessage',
  'setSequenceStatus','cancelPresetSequence','runPresetSequence','runPresetSequenceStep',
  'updateCategoryFilter','renderFilteredCommands','clearCommandFilters',
  'loadLog','sendIpgCommand','sendIpgCommandWithOptions','clearIpgLog','loadRs232Status'];
funcs.forEach(function(f){
  assert(typeof win[f]==='function',f+' is a function');
});
dom.window.close();
console.log('\nResults: '+passed+'/'+(passed+failed)+' passed, '+failed+' failed\n');
if(failed>0)process.exit(1);
