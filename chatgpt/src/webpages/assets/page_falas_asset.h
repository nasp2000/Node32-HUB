#pragma once
#include "../../feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

// Falas page: local WAV phrases in LittleFS /falas (list / play / delete).
// Upload goes through /files (existing uploader). JS: no arrow functions
// (ESP32 browser compat), feedback via customAlert, d.success (never d.ok).
static const char FALAS_PAGE[] PROGMEM = R"rawliteral(
<div class="wrap">
<a class="btn" href="/" data-i18n="btnHome">Inicio</a>
<h1 data-i18n="touch7Falas">🔊 Falas locais</h1>
<p>Frases WAV em <code>/falas</code> (16-bit PCM mono, 16 kHz). Para enviar ficheiros novos use a pagina <a href="/files">Files</a>.</p>
<div class="card">
<button class="btn" id="bFalasRef" title="Refresh the list of announcements" data-i18n-title="falasTipRefresh">Refresh list</button>
<span id="falasStatus">A carregar...</span>
<table id="falasTable"><thead><tr><th>File</th><th>Tamanho</th><th></th><th></th></tr></thead><tbody id="falasBody"></tbody></table>
</div>
<div class="card">
<h2 data-i18n="touch7Peers">Peers (viewer 2+2)</h2>
<p>Configure os nodes remotos (ex. pack CAM) em JSON e grave. Teste com o botao.</p>
<textarea id="peersJson" rows="10" cols="60"></textarea><br>
<button class="btn" id="bPeersLoad" title="Load the peer configuration" data-i18n-title="falasTipPeersLoad">Load config</button>
<button class="btn" id="bPeersSave" title="Save the peer configuration" data-i18n-title="falasTipPeersSave">Save</button>
<button class="btn" id="bPeersTest" data-i18n="touch7Test" title="Test the announcement now" data-i18n-title="falasTipPeersTest">Testar agora</button>
</div>
<script>
function falasShow(m){ customAlert(m); }
function falasLoad(){
  fetch('/api/falas').then(function(r){ return r.json(); }).then(function(d){
    var tb = $('falasBody'); tb.innerHTML = '';
    if(!d.success || !d.files || !d.files.length){ $('falasStatus').textContent = 'Sem ficheiros.'; return; }
    $('falasStatus').textContent = d.files.length + ' ficheiro(s).';
    d.files.forEach(function(f){
      var tr = document.createElement('tr');
      tr.innerHTML = '<td></td><td></td><td></td><td></td>';
      tr.children[0].textContent = f.name;
      tr.children[1].textContent = f.size;
      var bP = document.createElement('button'); bP.className = 'btn'; bP.textContent = 'Ouvir';
      bP.onclick = function(){ falasPlay(f.name); };
      var bD = document.createElement('button'); bD.className = 'btn'; bD.textContent = 'Delete';
      bD.onclick = function(){ customConfirm('Delete ' + f.name + '?', function(){ falasDel(f.name); }); };
      tr.children[2].appendChild(bP); tr.children[3].appendChild(bD);
      tb.appendChild(tr);
    });
  });
}
function falasPlay(n){
  fetch('/api/falas-play?file=' + encodeURIComponent(n), {method:'POST'}).then(function(r){ return r.json(); }).then(function(d){
    if(d.success) falasShow('Playing ' + n); else falasShow('Failed: ' + (d.msg || '?'));
  });
}
function falasDel(n){
  fetch('/api/falas-delete?file=' + encodeURIComponent(n), {method:'POST'}).then(function(r){ return r.json(); }).then(function(d){
    if(d.success){ falasShow('Deleted.'); falasLoad(); } else falasShow('Failed: ' + (d.msg || '?'));
  });
}
function peersLoad(){
  fetch('/api/touch7-config').then(function(r){ return r.json(); }).then(function(d){
    if(d.success) $('peersJson').value = JSON.stringify(d, null, 1);
  });
}
function peersSave(){
  fetch('/api/touch7-config', {method:'POST', headers:{'Content-Type':'application/json'}, body:$('peersJson').value}).then(function(r){ return r.json(); }).then(function(d){
    if(d.success) falasShow('Saved.'); else falasShow('Failed to save.');
  });
}
function peersTest(){
  fetch('/api/touch7-peer-refresh', {method:'POST'}).then(function(r){ return r.json(); }).then(function(d){
    if(d.success) falasShow('Updating peers...'); else falasShow('Failed.');
  });
}
if (typeof $ !== 'undefined' && $('bFalasRef')) {
  $('bFalasRef').onclick = falasLoad;
  $('bPeersLoad').onclick = peersLoad;
  $('bPeersSave').onclick = peersSave;
  $('bPeersTest').onclick = peersTest;
  falasLoad();
}
window.__test__ = window.__test__ || {};
window.__test__.falasRow = function(name, size){
  return {name: name, size: size, label: name + ' (' + size + ' B)'};
};
window.__test__.falasSafe = function(name){
  if(!name || name.length > 64) return false;
  if(name.indexOf('..') >= 0 || name.indexOf('/') >= 0) return false;
  return name.endsWith('.wav') || name.endsWith('.WAV');
};
</script>
)rawliteral";

#endif
