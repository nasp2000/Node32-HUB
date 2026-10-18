var fs=require('fs'),path=require('path'),jsdom=require('jsdom'),{JSDOM}=jsdom;
var html=fs.readFileSync(path.join(__dirname,'pages','google_script.html'),'utf8').toString();
var dom=new JSDOM(html,{runScripts:'dangerously',url:'http://localhost'});
var win=dom.window,doc=win.document;
var gasText=win.gasText,testEmailFromGas=win.testEmailFromGas;
var passed=0,failed=0;
function assert(c,m){if(c)passed++;else{console.log('  FAIL:',m);failed++;}}
function assertEq(a,e,m){if(a===e)passed++;else{console.log('  FAIL:',m,'exp:',JSON.stringify(e),'got:',JSON.stringify(a));failed++;}}
console.log('\n=== Google Script Page Tests ===\n');
assert(typeof gasText==='function','gasText is a function');
assert(typeof testEmailFromGas==='function','testEmailFromGas is a function');
win.localStorage.removeItem('ui_lang_cache');
assertEq(gasText('nonexistent','fb'),'fb','gasText returns fallback');
assertEq(gasText('nonexistent'),'nonexistent','gasText returns key when no fallback');
win.localStorage.setItem('ui_lang_cache',JSON.stringify({test:'Teste'}));
assertEq(gasText('test','fb'),'Teste','gasText reads from cache');
win.localStorage.removeItem('ui_lang_cache');
if(!win.__MOCK_API__)win.__MOCK_API__={};
win.__MOCK_API__['/api/email-test']={success:true,message:'OK'};
var alertCalls=[],orig=win.customAlert;
win.customAlert=function(m){alertCalls.push(m);};
testEmailFromGas();
setTimeout(function(){
  assert(alertCalls.some(function(m){return m.indexOf('OK')>=0;}),'testEmailFromGas shows success');
  win.__MOCK_API__['/api/email-test']={success:false,message:'Fail'};
  testEmailFromGas();
  setTimeout(function(){
    assert(alertCalls.some(function(m){return m.indexOf('Fail')>=0||m.indexOf('Erro')>=0;}),'testEmailFromGas shows error on failure');
    win.customAlert=orig;
    dom.window.close();
    console.log('\nResults: '+passed+'/'+(passed+failed)+' passed, '+failed+' failed\n');
    if(failed>0)process.exit(1);
  },50);
},50);
