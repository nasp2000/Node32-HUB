var fs=require('fs'),path=require('path'),jsdom=require('jsdom'),{JSDOM}=jsdom;
var html=fs.readFileSync(path.join(__dirname,'pages','access.html'),'utf8').toString();
var dom=new JSDOM(html,{runScripts:'dangerously',url:'http://localhost'});
var win=dom.window,doc=win.document;
var accSwitchTab=win.accSwitchTab;

var passed=0,failed=0;
function assert(c,m){if(c)passed++;else{console.log('  FAIL:',m);failed++;}}
function assertEq(a,e,m){if(a===e)passed++;else{console.log('  FAIL:',m,'exp:',JSON.stringify(e),'got:',JSON.stringify(a));failed++;}}
console.log('\n=== Access Page Tests ===\n');

assert(typeof accSwitchTab==='function','accSwitchTab is a function');

// Test pure function: accSwitchTab toggles active class
var btn1=doc.createElement('button'),btn2=doc.createElement('button');
var pan1=doc.createElement('div'),pan2=doc.createElement('div');
btn1.classList.add('acc-tab-btn');
btn2.classList.add('acc-tab-btn');
pan1.classList.add('acc-panel');
pan2.classList.add('acc-panel');
var btns=[btn1,btn2],panels=[pan1,pan2];

assert(!btn1.classList.contains('active'),'initially not active');
accSwitchTab(btns,panels,0);
assert(btn1.classList.contains('active'),'switching to 0 activates btn1');
assert(!btn2.classList.contains('active'),'switching to 0 deactivates btn2');
assert(pan1.classList.contains('active'),'switching to 0 activates panel1');
assert(!pan2.classList.contains('active'),'switching to 0 deactivates panel2');

accSwitchTab(btns,panels,1);
assert(!btn1.classList.contains('active'),'switching to 1 deactivates btn1');
assert(btn2.classList.contains('active'),'switching to 1 activates btn2');
assert(!pan1.classList.contains('active'),'switching to 1 deactivates panel1');
assert(pan2.classList.contains('active'),'switching to 1 activates panel2');

// Switching to same index keeps active
accSwitchTab(btns,panels,1);
assert(btn2.classList.contains('active'),'switching to same index keeps active');

// Empty arrays are handled
accSwitchTab([],[],0); // should not throw

// Test loadAccessConfig exists
assert(typeof win.loadAccessConfig==='function','loadAccessConfig is a function');
assert(typeof win.accInitTabs==='function','accInitTabs is a function');

// ---- auth_enabled toggle wiring ----
var aeEl=doc.getElementById('auth_enabled');
assert(!!aeEl,'auth_enabled checkbox exists in admin section');
assert(!!doc.querySelector('input[name="auth_enabled_present"]'),'auth_enabled_present hidden field exists');
assert(!!doc.querySelector('input[name="web_username"]'),'admin username field exists');

win._t=function(k,fb){return fb;};
var alertCalls=[],origAlert=win.customAlert;
win.customAlert=function(m){alertCalls.push(m);};

// loadAccessConfig populates the toggle from /api/access-config
if(!win.__MOCK_API__)win.__MOCK_API__={};
win.__MOCK_API__['/api/access-config']={web:{auth_enabled:true,username:'admin'}};
win.loadAccessConfig();
setTimeout(function(){
  assert(aeEl.checked===true,'auth_enabled checked when config true');

  win.__MOCK_API__['/api/access-config']={web:{auth_enabled:false,username:''}};
  win.loadAccessConfig();
  setTimeout(function(){
    assert(aeEl.checked===false,'auth_enabled unchecked when config false');
    assert(doc.getElementById('web_username').value==='','username defaults to empty (no root fallback)');

    // Mismatched passwords: no POST sent, no success alert.
    var npInput=doc.getElementById('web_password_new');
    var cpInput=doc.getElementById('web_password_confirm');
    var capturedBody=null, realFetch=win.fetch;
    win.fetch=function(url,opts){
      if(opts&&opts.method==='POST'){capturedBody=String(opts.body||'');}
      return realFetch(url,opts);
    };
    npInput.value='pass123';
    cpInput.value='different';
    var form=doc.getElementById('access-form');
    form.dispatchEvent(new win.Event('submit'));
    assert(capturedBody===null,'password mismatch prevents POST');

    // Matching passwords + auth enabled: POST includes the toggle.
    win.__MOCK_API__['/api/access-config']={success:true,message:'Saved!'};
    cpInput.value='pass123';
    aeEl.checked=true;
    form.dispatchEvent(new win.Event('submit'));

    setTimeout(function(){
      assert(capturedBody!==null,'POST captured for matching passwords');
      assert(capturedBody.indexOf('auth_enabled_present=1')>=0,'submit includes auth_enabled_present');
      assert(capturedBody.indexOf('auth_enabled=on')>=0,'submit includes auth_enabled=on when checked');
      var found=alertCalls.some(function(m){return m.indexOf('Saved')>=0||m.indexOf('✓')>=0;});
      assert(found,'form submit with matching passwords shows success');
      win.customAlert=origAlert;

      dom.window.close();
      console.log('\nResults: '+passed+'/'+(passed+failed)+' passed, '+failed+' failed\n');
      if(failed>0)process.exit(1);
    },50);
  },50);
},50);
