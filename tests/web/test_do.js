var fs=require('fs'),path=require('path'),jsdom=require('jsdom'),{JSDOM}=jsdom;
var dom=new JSDOM(fs.readFileSync(path.join(__dirname,'pages','do_asset.html'),'utf8'),{runScripts:'dangerously',url:'http://localhost'});
var win=dom.window,doc=win.document;
var passed=0,failed=0;
function assert(c,m){if(c)passed++;else{console.log('  FAIL:',m);failed++;}}
function assertEq(a,e,m){if(a===e)passed++;else{console.log('  FAIL:',m,'exp:',JSON.stringify(e),'got:',JSON.stringify(a));failed++;}}
console.log('\n=== Digital Outputs Page Tests ===\n');
assert(typeof win._updBtn==='function','_updBtn is function');
assert(typeof win.toggleDO==='function','toggleDO is function');
assert(typeof win.setAllDO==='function','setAllDO is function');
assert(typeof win.loadStatus==='function','loadStatus is function');
assert(typeof win.loadDoConfig==='function','loadDoConfig is function');
assert(typeof win.saveDoConfig==='function','saveDoConfig is function');
assert(typeof win.applyDoNames==='function','applyDoNames is function');
var card0=doc.querySelector('.do-card .do-label strong');
assertEq(card0&&card0.textContent,'DO 1','first card header is DO 1');
var lastCard=doc.querySelectorAll('.do-card .do-label strong');
assertEq(lastCard[lastCard.length-1].textContent,'DO 8','last card header is DO 8');
var btn=doc.createElement('button');
btn.className='btn-on';
btn.setAttribute('data-state','true');
win._updBtn(btn,false);
assert(btn.getAttribute('data-state')==='false','_updBtn sets state=false');
assert(btn.className.indexOf('btn-off')>=0||btn.className.indexOf('btn-on')<0,'_updBtn toggles class off');
win._updBtn(btn,true);
assert(btn.getAttribute('data-state')==='true','_updBtn sets state=true');

console.log('-- config labels --');
win.applyDoNames(['Motor','','Valve 3']);
assertEq(doc.getElementById('doName0').textContent,'Motor','doName0 uses custom name');
assertEq(doc.getElementById('doName1').textContent,'Relay 2','doName1 falls back to Relay 2');
assertEq(doc.getElementById('doName2').textContent,'Valve 3','doName2 uses custom name');
assertEq(doc.getElementById('doName7').textContent,'Relay 8','doName7 untouched keeps fallback');

console.log('-- loadDoConfig fills form + labels --');
function waitFor(fn,ms){
  return new Promise(function(res){var t0=Date.now();(function tick(){if(fn())return res();if(Date.now()-t0>ms)return res();setTimeout(tick,5);})();});
}
win.__MOCK_API__={'/api/do/config':{ch_count:8,invert:0x85,names:['Bomba','','']}};
win.loadDoConfig();
waitFor(function(){return doc.getElementById('doNameInput0').value==='Bomba';},500).then(function(){
  assertEq(doc.getElementById('doNameInput0').value,'Bomba','input0 gets name');
  assertEq(doc.getElementById('doNameInput1').value,'','input1 empty name');
  assertEq(doc.getElementById('doName0').textContent,'Bomba','label0 custom');
  assertEq(doc.getElementById('doName1').textContent,'Relay 2','label1 fallback');
  assert(doc.getElementById('doInv0').checked,'invert bit0 checked');
  assert(!doc.getElementById('doInv1').checked,'invert bit1 unchecked');
  assert(doc.getElementById('doInv2').checked,'invert bit2 checked');
  assert(doc.getElementById('doInv7').checked,'invert bit7 checked');
  assert(!doc.getElementById('doInv3').checked,'invert bit3 unchecked');

  console.log('-- saveDoConfig posts trimmed names + invert mask --');
  doc.getElementById('doNameInput0').value='  Motor A  ';
  doc.getElementById('doNameInput1').value='';
  doc.getElementById('doInv1').checked=true;
  doc.getElementById('doInv2').checked=false;
  win.saveDoConfig();
  var bodies=win.__postBodies||[];
  assert(bodies.length>=1,'saveDoConfig performed POST');
  var body=JSON.parse(bodies[bodies.length-1].body);
  assertEq(body.names[0],'Motor A','posted name trimmed');
  assertEq(body.names[1],'','posted empty name preserved');
  assertEq(body.names[7],'','posted channel 7 empty');
  var invExp=(1<<0)|(1<<1)|(1<<7); // bit0 was checked in load, bit1 newly set, bit2 cleared
  assertEq(body.invert,invExp,'posted invert mask');
  assertEq(body.names[0].length<=16,true,'name capped at 16');
  dom.window.close();
  console.log('\nResults: '+passed+'/'+(passed+failed)+' passed, '+failed+' failed\n');
  if(failed>0)process.exit(1);
}).catch(function(e){console.log('  FAIL: promise rejected',e);failed++;dom.window.close();console.log('\nResults: '+passed+'/'+(passed+failed)+' passed, '+failed+' failed\n');if(failed>0)process.exit(1);});
