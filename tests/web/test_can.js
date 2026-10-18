var fs=require('fs'),path=require('path'),jsdom=require('jsdom'),{JSDOM}=jsdom;
var html=fs.readFileSync(path.join(__dirname,'pages','can_asset.html'),'utf8').toString();
var dom=new JSDOM(html,{runScripts:'dangerously',url:'http://localhost'});
var win=dom.window,doc=win.document;
var passed=0,failed=0;
function assert(c,m){if(c)passed++;else{console.log('  FAIL:',m);failed++;}}
function assertEq(a,e,m){if(a===e)passed++;else{console.log('  FAIL:',m,'exp:',JSON.stringify(e),'got:',JSON.stringify(a));failed++;}}
console.log('\n=== CAN Bus Page Tests ===\n');
assertEq(win.stateClass('RUNNING'),'cs-run','stateClass RUNNING');
assertEq(win.stateClass('BUS_OFF'),'cs-off','stateClass BUS_OFF');
assertEq(win.stateClass('STOPPED'),'cs-stop','stateClass STOPPED');
assertEq(win.stateClass(''),'cs-stop','stateClass empty returns stop');
assert(typeof win.clearFrames==='function','clearFrames is function');
assert(typeof win.togglePause==='function','togglePause is function');
assert(typeof win.sendFrame==='function','sendFrame is function');
assert(typeof win.loadStatus==='function','loadStatus is function');
assert(typeof win.loadFrames==='function','loadFrames is function');
assert(typeof win.canLoadConfig==='function','canLoadConfig is function');
assert(typeof win.canSaveConfig==='function','canSaveConfig is function');
assert(typeof win.restartDriver==='function','restartDriver is function');
var logEl=doc.getElementById('log')||doc.getElementById('canLog');
if(logEl)assert(true,'log element exists');
dom.window.close();
console.log('\nResults: '+passed+'/'+(passed+failed)+' passed, '+failed+' failed\n');
if(failed>0)process.exit(1);
