const { JSDOM } = require('jsdom');
const fs = require('fs');

const asset = fs.readFileSync('C:/Users/nasp2/Desktop/Code/Node32-HUB-S3-N16R8/chatgpt/src/webpages/assets/page_proximity_asset.h', 'utf8');
const tabsJs = fs.readFileSync('C:/Users/nasp2/Desktop/Code/Node32-HUB-S3-N16R8/chatgpt/src/webpages/renderers/proximity_renderer.cpp', 'utf8');
const pageHtml = asset.match(/R"rawliteral\(([\s\S]*?)\)rawliteral"/)[1];
const tabsBlock = tabsJs.match(/R"JSTABS\(([\s\S]*?)\)JSTABS"/)[1];
const fullHtml = `<!DOCTYPE html><html><head><meta charset=utf-8></head><body>${tabsBlock}${pageHtml}</body></html>`;

// Simula o que o backend devolveria após o load do ficheiro unificado do utilizador
// loadProximityConfig -> proximityShiftFromJson -> _shiftCfg
const loaded = { en: true, ar: true, nSlots: 3, slots: [{h:6,m:0},{h:14,m:0},{h:22,m:0}], emails: 'owner@example.com' };
const live = { channels: [{mode:0,name:'Maq1',count_in:0,count_out:0,cycle_ms:0,ppm:0,state:false,goal:0,direction:0}] };
const cfg = { channels: [{fixedPins:true,n:'Maq1',m:0,gi:4,gi2:255,go:255,db:50,gl:0,pd:0,pu:0,mo:true,ml:2,tp:0,om:0}] };

const dom = new JSDOM(fullHtml, { runScripts:'dangerously', resources:'usable', url:'http://x/proximity', beforeParse(window) {
  window.fetch = function(url) {
    const p = String(url).split('?')[0];
    const data = p === '/api/proximity/shift-config' ? loaded
               : p === '/api/proximity/shiftday-emails' ? { emails: loaded.emails }
               : p === '/api/proximity' ? live
               : p === '/api/proximity/config' ? cfg
               : p === '/api/do/status' ? {ok:false}
               : {channels:[]};
    return Promise.resolve({ ok: p==='/api/do/status'?false:true, status: p==='/api/do/status'?404:200, json:function(){ return Promise.resolve(JSON.parse(JSON.stringify(data))); } });
  };
  window.customAlert=function(m){console.log('[alert]',m);};
  window.customConfirm=function(){}; window.customPrompt=function(){};
  window.showLoading=function(){}; window.hideLoading=function(){};
  window.disableBtn=function(){}; window.enableBtn=function(){};
  window.showSavedCard=function(){};
  window.__err=[];
  window.addEventListener('error',function(e){window.__err.push(e.message);});
}});
const { window } = dom;
setTimeout(() => {
  const doc = window.document;
  console.log('=== TABS SHIFT/SHIFTDAY ===');
  console.log('shen.checked:', doc.getElementById('shen') ? doc.getElementById('shen').checked : 'MISSING');
  console.log('shar.checked:', doc.getElementById('shar') ? doc.getElementById('shar').checked : 'MISSING');
  console.log('shnsl.value:', doc.getElementById('shnsl') ? doc.getElementById('shnsl').value : 'MISSING');
  const h0=doc.getElementById('sh_h0'),m0=doc.getElementById('sh_m0');
  const h1=doc.getElementById('sh_h1'),m1=doc.getElementById('sh_m1');
  const h2=doc.getElementById('sh_h2'),m2=doc.getElementById('sh_m2');
  console.log('sh0:', h0?h0.value:'X', m0?m0.value:'X', '| sh1:', h1?h1.value:'X', m1?m1.value:'X', '| sh2:', h2?h2.value:'X', m2?m2.value:'X');
  console.log('emails:', doc.getElementById('shiftday-emails') ? doc.getElementById('shiftday-emails').value : 'MISSING');
  console.log('Erros JS:', window.__err.length ? window.__err.join(' | ') : 'nenhum');
  window.close();
}, 2000);
