#include "page_hub_asset.h"

const char HUB_PAGE[] PROGMEM = R"HTML(
<style>
:root{
  --bg-0:#081226;--bg-1:#0d1c38;--bg-2:#14294e;
  --bdr:rgba(147,197,253,.12);--bdr2:rgba(147,197,253,.22);--bdr3:rgba(147,197,253,.38);
  --tx:#eaf2ff;--tx2:#a9c1e8;--tx3:#6b87b3;
  --acc:#2f7bff;--acc2:#5aa0ff;
  --red:#ef4444;--green:#22c55e;
}
*{box-sizing:border-box;margin:0;padding:0}
body{
  background:linear-gradient(180deg, var(--bg-0) 0%, var(--bg-1) 100%);
  color:var(--tx);
  font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
  font-size:11px;min-height:100dvh;
  -webkit-font-smoothing:antialiased;
}
.wrap{max-width:1100px!important;margin:0 auto!important;padding:12px!important;width:100%!important}
.topbar{
  display:flex;align-items:center;gap:8px;flex-wrap:wrap;
  padding:10px 12px;margin-bottom:12px;
  background:linear-gradient(180deg, rgba(20,41,78,.92) 0%, rgba(13,28,56,.88) 100%);
  border:1px solid var(--bdr2);border-radius:10px;
}
.topbar h1{font-size:14px;font-weight:700;margin-right:auto}
.b{
  display:inline-flex;align-items:center;gap:4px;
  background:linear-gradient(180deg, rgba(140,190,255,.30) 0%, rgba(90,160,255,.14) 100%);color:var(--tx);
  border:1px outset var(--bdr3);border-radius:2px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.30), 0 2px 3px rgba(0,0,0,.45);
  padding:5px 12px;font-size:11px;font-weight:600;cursor:pointer;
  text-decoration:none;white-space:nowrap;height:26px;font-family:inherit;
}
.b:hover{background:rgba(90,160,255,.20);border-color:var(--bdr3)}
.b-rec{background:linear-gradient(135deg, var(--red) 0%, #f87171 100%);color:#fff;border-color:var(--red)}
.b-green{background:linear-gradient(135deg, #16a34a 0%, var(--green) 100%);color:#fff;border-color:#16a34a}
.addrow{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
.addrow input{
  flex:1;min-width:140px;background:var(--bg-2);color:var(--tx);
  border:1px solid var(--bdr2);border-radius:8px;padding:6px 10px;
  font-size:11px;font-family:inherit;outline:none;
}
.addrow input:focus{border-color:var(--acc)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:10px}
.card{
  background:linear-gradient(0deg, rgba(10,22,44,.96) 0%, rgba(13,28,56,.94) 100%);
  border:1px solid var(--bdr2);border-radius:10px;overflow:hidden;
}
.card img{width:100%;aspect-ratio:4/3;object-fit:cover;display:block;background:#04070f}
.card .ip{
  display:flex;align-items:center;gap:6px;padding:6px 8px;font-size:11px;
}
.card .ip a{color:var(--tx);text-decoration:none;font-weight:700;flex:1;overflow:hidden;text-overflow:ellipsis}
.card .ip a:hover{color:var(--acc2)}
.card .x{
  background:none;border:none;color:var(--tx3);cursor:pointer;font-size:12px;padding:2px 6px;font-family:inherit;
}
.card .x:hover{color:var(--red)}
.empty{color:var(--tx3);text-align:center;padding:32px 12px;font-size:12px}
.hint{color:var(--tx3);font-size:10px;margin-top:12px;line-height:1.6}

/* ===== core.css guard (same as camera page): neutralize the global
   "button{border-radius:9999px!important;padding:8px 24px!important}" pill. */
.wrap .b{border-radius:2px!important;padding:5px 12px!important;}
.wrap .card .x{border-radius:2px!important;}
</style>

<div class='wrap'>
<div class='topbar'>
  <a href='/' class='b' data-i18n='btnHome' title='Go to home page'>&#127968;</a>
  <h1>&#128246; <span data-i18n='hubTitle'>Camera Hub</span></h1>
  <button class='b' onclick='hubRefresh()' title='Refresh snapshots'>&#8635; <span data-i18n='camRefresh'>Refresh</span></button>
  <button class='b b-rec' onclick='hubClear()' title='Remove all cameras'>&#10005; <span data-i18n='hubClear'>Clear</span></button>
</div>

<div class='addrow'>
  <input id='hubIp' placeholder='192.168.1.18' title='Camera IP address'>
  <button class='b b-green' onclick='hubAdd()' title='Add camera'>&#10133; <span data-i18n='hubAdd'>Add</span></button>
</div>

<div class='grid' id='hubGrid'></div>
<div class='empty' id='hubEmpty' style='display:none' data-i18n='hubEmpty'>No cameras yet — add one above.</div>
<div class='hint' data-i18n='hubHint'>Snapshots refresh every 10 s. IPs stay in this browser only. If a camera asks for login, open it once and sign in.</div>
</div>

<script>
function hubLoad(){
  try{return JSON.parse(localStorage.getItem('camHub')||'[]')}catch(e){return[]}
}
function hubSave(l){try{localStorage.setItem('camHub',JSON.stringify(l))}catch(e){}}
function hubRender(){
  var g=document.getElementById('hubGrid'),e=document.getElementById('hubEmpty');
  var list=hubLoad();
  g.innerHTML='';
  e.style.display=list.length?'none':'block';
  list.forEach(function(ip){
    var d=document.createElement('div');d.className='card';
    var im=document.createElement('img');
    im.alt=ip;im.loading='lazy';
    im.src='http://'+ip+'/cam/snapshot?_='+Date.now();
    im.onerror=function(){im.style.opacity='0.35'};
    im.onload=function(){im.style.opacity='1'};
    var row=document.createElement('div');row.className='ip';
    var a=document.createElement('a');a.href='http://'+ip+'/camera';a.target='_blank';a.textContent=ip;
    var x=document.createElement('button');x.className='x';x.textContent='✕';x.title='Remove';
    x.onclick=function(){hubSave(hubLoad().filter(function(v){return v!==ip}));hubRender()};
    row.appendChild(a);row.appendChild(x);
    d.appendChild(im);d.appendChild(row);
    g.appendChild(d)
  })
}
function hubAdd(){
  var v=document.getElementById('hubIp').value.trim();
  if(!v)return;
  var list=hubLoad();
  if(list.indexOf(v)<0){list.push(v);hubSave(list)}
  document.getElementById('hubIp').value='';
  hubRender()
}
function hubClear(){hubSave([]);hubRender()}
function hubRefresh(){hubRender()}
setInterval(hubRefresh,10000);
hubRender();
</script>
)HTML";
