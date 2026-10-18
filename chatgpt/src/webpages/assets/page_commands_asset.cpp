#include "page_commands_asset.h"

const char PAGE_COMMANDS_HTML[] PROGMEM = R"CMDS(<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Scheduled Commands</title><link rel='stylesheet' href='/core.css'><style>/* 📊 Wrapper without scrollbar on PC */ .table-wrapper{width:100%;overflow-x:auto;-webkit-overflow-scrolling:touch;border:1px solid rgba(255,255,255,.1);border-radius:6px}table{border-collapse:collapse;font-size:13px;table-layout:fixed}table.cmd{width:100%;background:rgba(255,255,255,.03)}th,td{border:1px solid rgba(255,255,255,.15);padding:6px;text-align:left;word-break:break-word;overflow-wrap:anywhere}th{background:rgba(255,255,255,.07)}table.cmd th:last-child,table.cmd td:last-child{padding:0;text-align:center}table.cmd td:last-child button{width:32px!important;max-width:32px!important;min-width:32px!important;height:32px!important;min-height:32px!important;padding:0!important;margin:0 auto!important;display:flex!important;align-items:center!important;justify-content:center!important;border-radius:16px!important}input[type=text],input[type=number]{padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:rgba(10,20,45,.8);color:#fff;font-size:13px;box-sizing:border-box;width:100%;min-height:32px}select{padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:rgba(10,20,45,.8);color:#fff;font-size:13px;min-height:32px;width:100%;box-sizing:border-box}@media(max-width:768px){.table-wrapper{overflow-x:auto;margin:0 -10px;padding:0 10px}table.cmd{min-width:950px}}@media(max-width:640px){.table-wrapper{overflow-x:auto;margin:0 -8px;padding:0 8px;border:none}table.cmd{min-width:1050px}th,td{padding:5px}input[type=text],input[type=number]{font-size:12px;padding:5px}select{font-size:12px;min-height:30px;padding:4px 6px}}@media(max-width:480px){.table-wrapper{overflow-x:auto;margin:0 -6px;padding:0 6px}table.cmd{min-width:1100px}input[type=text],input[type=number]{font-size:12px;padding:4px 6px;min-height:30px}select{font-size:12px;min-height:28px;padding:3px 5px}th,td{padding:4px}}</style></head><body><div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'><div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div><p style='color:#e6f1ff;margin-top:15px;font-size:14px' data-i18n='loading'>A processar...</p></div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style><main class='page'><div class='wrap'><div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome'>🏠 Home</a></div><h1 data-i18n='cmdsPageTitle'>📡 Commands by Time Interval</h1><div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:16px'><h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='infoTitle'>ℹ️ Information</h2><div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px' data-i18n-html='cmdsInfoDesc'><div>🎯 <strong style='color:#60a5fa'>Type:</strong> Send commands to different ports and systems, or internal commands</div><div>🔁 <strong style='color:#60a5fa'>Interval:</strong> How often to run (e.g.: 5 = every 5 seconds/minutes/hours)</div><div>⚙️ <strong style='color:#60a5fa'>Unit:</strong> Period (Second, Minute, Hour)</div><div>📅 <strong style='color:#60a5fa'>Days:</strong> Weekdays to run (All, Weekdays, Weekend, specific days)</div><div>🧬 <strong style='color:#60a5fa'>Template:</strong> Pre-saved RS232 configuration (baudrate, parity, etc.) - RS232 Type only</div></div><div style='font-size:11px;color:#94a3b8;margin:0 0 6px 2px'>👉 Deslize horizontalmente para ver todas as colunas</div><div class='table-wrapper'><table class='cmd'><thead><tr><th style='width:30px'>#</th><th style='width:65px' data-i18n='thType'>Type</th><th style='width:95px' data-i18n='thCommand'>Command</th><th style='width:70px' data-i18n='thInterval'>Interval</th><th style='width:70px' data-i18n='thUnit'>Unit</th><th style='width:120px' data-i18n='thDays'>Days</th><th style='width:105px' data-i18n='thTemplate'>Template</th><th style='width:65px' data-i18n='thActive'>Active</th><th style='width:42px'></th></tr></thead><tbody id='sched_cmd_body'></tbody></table></div><div style='text-align:center;margin-top:14px'><button id='cmd_save_all' class='btn' type='button' style='padding:10px 24px' data-i18n='btnSaveAll'>💾 Save All</button> <button type='button' id='add_cmd' class='btn' style='padding:10px 24px;background:#065f46;border:1px solid #10b981' data-i18n='cmdsAddRow'>➕ Add Command</button></div></div><script>)CMDS";

const char PAGE_COMMANDS_JS[] PROGMEM = R"JS((function(){
    var tb=document.getElementById('sched_cmd_body');
    var saveAllBtn=document.getElementById('cmd_save_all');
    var templatesCache=[];
    var MAX=30;
    var currentCount=0;
    function esc(s){ return (s||'').replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;','\'':'&#39;'}[c];}); }
    function readLangCache(){
        try{
            if(window._lang) return window._lang;
            var raw=localStorage.getItem('ui_lang_cache');
            if(!raw) return null;
            return JSON.parse(raw);
        }catch(e){ return null; }
    }
    function t(k,fb){
        if(typeof window._t==='function') return window._t(k,fb);
        var cache=readLangCache();
        return cache&&cache[k]!==undefined?cache[k]:(fb!==undefined?fb:k);
    }
    function templateOptions(cur){ var h="<option value=''>--</option>"; templatesCache.forEach(function(t){ var n=t&&t.name?t.name:''; if(!n)return; h+="<option value='"+esc(n)+"'"+(cur===n?" selected":"")+">"+esc(n)+"</option>"; }); return h; }
    function unitSelect(i){ return "<select name='unit_"+i+"'><option value='s'>"+esc(t('unitSecondShort','Sec'))+"</option><option value='m' selected>"+esc(t('unitMinuteShort','Min'))+"</option><option value='h'>"+esc(t('unitHourShort','Hour'))+"</option></select>"; }
    function daysSelect(i){ return "<select name='days_"+i+"'><option value='everyday'>"+esc(t('dayEveryday','Every day'))+"</option><option value='weekdays'>"+esc(t('dayWeekdays','Weekdays'))+"</option><option value='weekends'>"+esc(t('dayWeekends','Weekends'))+"</option><option value='monday'>"+esc(t('dayMonday','Mon'))+"</option><option value='tuesday'>"+esc(t('dayTuesday','Tue'))+"</option><option value='wednesday'>"+esc(t('dayWednesday','Wed'))+"</option><option value='thursday'>"+esc(t('dayThursday','Thu'))+"</option><option value='friday'>"+esc(t('dayFriday','Fri'))+"</option><option value='saturday'>"+esc(t('daySaturday','Sat'))+"</option><option value='sunday'>"+esc(t('daySunday','Sun'))+"</option></select>"; }
    function tplSelect(i){ return "<select name='tpl_"+i+"'></select>"; }
    function rowHTML(i){ return "<tr data-i='"+i+"'>"+
        "<td style='text-align:center'>"+i+"</td>"+
        "<td><select name='type_"+i+"' title='Command type' data-i18n-title='cmdTipType'><option value='system' selected>"+esc(t('typeSystem','System'))+"</option>"+(_rs232Enabled?"<option value='rs232'>RS232</option>":"")+(_modbusEnabled?"<option value='modbus'>Modbus</option>":"")+"</select></td>"+
        "<td><input name='cmd_"+i+"' placeholder='"+esc(t('cmdsPlaceholder','Command'))+"' title='Command to send' data-i18n-title='cmdTipCommand'></td>"+
        "<td><input name='int_"+i+"' type='number' min='0'></td>"+
        "<td>"+unitSelect(i)+"</td>"+
        "<td>"+daysSelect(i)+"</td>"+
        "<td>"+tplSelect(i)+"</td>"+
        "<td><select name='en_"+i+"' title='Enable command' data-i18n-title='cmdTipEnabled'><option value='1'>"+esc(t('opYes','Yes'))+"</option><option value='0' selected>"+esc(t('opNo','No'))+"</option></select></td>"+
        "<td><button type='button' data-save='"+i+"' style='cursor:pointer;background:#1e66c7;border:1px solid #20a3b9;padding:0;border-radius:16px;font-size:14px;color:#fff;display:flex;align-items:center;justify-content:center;width:32px;height:32px;line-height:1;margin:0 auto' title='Save this command' data-i18n-title='cmdTipSave'>💾</button><button type='button' data-del='"+i+"' style='cursor:pointer;background:#7f1d1d;border:1px solid #dc2626;padding:0;border-radius:16px;font-size:13px;color:#fff;display:flex;align-items:center;justify-content:center;width:32px;height:32px;line-height:1;margin:0 auto' title='Remove this command' data-i18n-title='cmdTipDel'>🗑️</button></td>"+
        "</tr>"; }
    function addRow(){
        if(currentCount >= MAX) return false;
        currentCount++;
        var d=document.createElement('tbody');
        d.innerHTML=rowHTML(currentCount);
        var row=d.removeChild(d.firstChild);
        tb.appendChild(row);
        attachRow(row);
        return true;
    }
    function attachRow(tr){
        var bt=tr.querySelector('[data-save]');
        if(bt){
            bt.addEventListener('click',function(){
                var i=parseInt(bt.getAttribute('data-save'));
                if(!i) return;
                saveSlot(i,bt);
            });
        }
        var dl=tr.querySelector('[data-del]');
        if(dl){
            dl.addEventListener('click',function(){
                var i=parseInt(dl.getAttribute('data-del'));
                if(!i) return;
                removeRow(i);
            });
        }
        var typeSel=tr.querySelector("select[name^='type_']");
        if(typeSel){
            typeSel.addEventListener('change',function(){
                var i=parseInt(typeSel.name.match(/\d+/)[0]);
                toggleTemplate(i);
            });
        }
        var iMatch=tr.getAttribute('data-i');
        if(iMatch) toggleTemplate(parseInt(iMatch));
    }
    function removeRow(i){
        var tr=tb.querySelector("tr[data-i='"+i+"']");
        if(!tr) return;
        window.customConfirm(t('cmdsConfirmRemove','Remove command '+i+'?'),function(ok){
            if(!ok) return;
            var data=[];
            var rows=tb.querySelectorAll('tr[data-i]');
            for(var r=0;r<rows.length;r++){
                var row=rows[r];
                var old=parseInt(row.getAttribute('data-i'));
                if(old===i) continue;
                data.push({
                    type:(row.querySelector("select[name='type_"+old+"']")||{}).value||'system',
                    command:(row.querySelector("input[name='cmd_"+old+"']")||{}).value||'',
                    interval:(row.querySelector("input[name='int_"+old+"']")||{}).value||'0',
                    unit:(row.querySelector("select[name='unit_"+old+"']")||{}).value||'m',
                    days:(row.querySelector("select[name='days_"+old+"']")||{}).value||'everyday',
                    template:(row.querySelector("select[name='tpl_"+old+"']")||{}).value||'',
                    enabled:(row.querySelector("select[name='en_"+old+"']")||{}).value||'0'
                });
            }
            tb.innerHTML='';
            currentCount=0;
            for(var d=0;d<data.length;d++){
                addRow();
                var idx=d+1;
                var nrow=tb.querySelector("tr[data-i='"+idx+"']");
                if(!nrow) continue;
                var f;
                f=nrow.querySelector("select[name='type_"+idx+"']"); if(f) f.value=data[d].type;
                f=nrow.querySelector("input[name='cmd_"+idx+"']"); if(f) f.value=data[d].command;
                f=nrow.querySelector("input[name='int_"+idx+"']"); if(f) f.value=data[d].interval;
                f=nrow.querySelector("select[name='unit_"+idx+"']"); if(f) f.value=data[d].unit;
                f=nrow.querySelector("select[name='days_"+idx+"']"); if(f) f.value=data[d].days;
                var tplSel=nrow.querySelector("select[name='tpl_"+idx+"']"); if(tplSel){ tplSel.setAttribute('data-cur',data[d].template); tplSel.innerHTML=templateOptions(data[d].template); if(data[d].template) tplSel.value=data[d].template; }
                f=nrow.querySelector("select[name='en_"+idx+"']"); if(f) f.value=data[d].enabled;
                toggleTemplate(idx);
            }
            loadAll();
        });
    }
    function init(){
        if(tb.querySelectorAll('tr').length===0){
            for(var i=0;i<10;i++) addRow();
        }
        attachSaveAll();
        loadAll();
    }
    function loadTemplates(){ fetch('/api/rs232-templates',{cache:'no-store'}).then(r=>r.json()).then(j=>{ templatesCache=Array.isArray(j.templates)?j.templates:[]; tb.querySelectorAll("select[name^='tpl_']").forEach(function(sel){ var cur=sel.getAttribute('data-cur')||''; sel.innerHTML=templateOptions(cur); if(cur) sel.value=cur; }); }).catch(()=>{}); }
    function mapUnit(u){ if(u==='seconds')return 's'; if(u==='minutes')return 'm'; if(u==='hours')return 'h'; return 'm'; }
    function normaliseUnit(u){ if(u==='s')return 'seconds'; if(u==='m')return 'minutes'; if(u==='h')return 'hours'; return 'minutes'; }
    function loadSlot(i){
        fetch('/api/scheduled-command-slot?i='+i,{cache:'no-store'}).then(r=>r.ok?r.text():null).then(txt=>{
            if(!txt) return;
            var j=null;
            try{ j=JSON.parse(txt);}catch(e){return;}
            if(!j||j.status!=='ok') return;
            var tr=tb.querySelector("tr[data-i='"+i+"']");
            if(!tr) return;
            var f;
            f=tr.querySelector("select[name='type_"+i+"']"); if(f) f.value=j.type||'rs232';
            f=tr.querySelector("input[name='cmd_"+i+"']"); if(f) f.value=j.command||'';
            f=tr.querySelector("input[name='int_"+i+"']"); if(f) f.value=j.interval||'';
            var uSel=tr.querySelector("select[name='unit_"+i+"']"); if(uSel) uSel.value=mapUnit(j.unit||'minutes');
            var dSel=tr.querySelector("select[name='days_"+i+"']"); if(dSel) dSel.value=j.days||'everyday';
            var tplSel=tr.querySelector("select[name='tpl_"+i+"']"); if(tplSel){ tplSel.setAttribute('data-cur',j.template||''); tplSel.innerHTML=templateOptions(j.template||''); if(j.template) tplSel.value=j.template; }
            var enSel=tr.querySelector("select[name='en_"+i+"']"); if(enSel) enSel.value=(j.enabled?'1':'0');
            toggleTemplate(i);
        }).catch(()=>{});
    }
    function loadAll(){ for(var i=1;i<=currentCount;i++) loadSlot(i); loadTemplates(); }
    function toggleTemplate(i){ var tr=tb.querySelector("tr[data-i='"+i+"']"); if(!tr)return; var ty=tr.querySelector("select[name='type_"+i+"']"),tpl=tr.querySelector("select[name='tpl_"+i+"']"); if(!ty||!tpl)return; if(ty.value==='system'||ty.value==='modbus'){tpl.style.opacity='0.3';tpl.disabled=true;}else{tpl.style.opacity='1';tpl.disabled=false;} }
    function saveSlot(i,btn){
        var tr=tb.querySelector("tr[data-i='"+i+"']");
        if(!tr) return;
        var fd=new FormData();
        fd.append('i',i);
        fd.append('type',tr.querySelector("select[name='type_"+i+"']").value);
        fd.append('command',tr.querySelector("input[name='cmd_"+i+"']").value.trim());
        fd.append('interval',tr.querySelector("input[name='int_"+i+"']").value||'0');
        fd.append('unit',normaliseUnit(tr.querySelector("select[name='unit_"+i+"']").value));
        fd.append('days',tr.querySelector("select[name='days_"+i+"']").value);
        fd.append('template',tr.querySelector("select[name='tpl_"+i+"']").value);
        fd.append('enabled',((tr.querySelector("select[name='en_"+i+"']")||{}).value||'0'));
        if(btn){disableBtn(btn);showLoading();}
        fetch('/api/scheduled-command-slot',{method:'POST',body:fd}).then(r=>r.json()).then(j=>{
            if(j&&j.status==='ok'){ customAlert('\u2705 '+t('cmdsSaved','Command saved!')); showSavedCard(); }
            else{ customAlert('\u274C '+t('cmdsError','Error saving command')); }
        }).catch(function(){
            customAlert('\u274C '+t('cmdsNetError','Network error saving command'));
        }).finally(function(){if(btn){hideLoading();enableBtn(btn);}});
    }
    function attachSaveAll(){ if(saveAllBtn) saveAllBtn.addEventListener('click', function(){ var i=1; function seq(){ if(i>currentCount)return; var tr=tb.querySelector("tr[data-i='"+i+"']"); var btn=tr?tr.querySelector('[data-save]'):null; if(btn) saveSlot(i,btn); i++; setTimeout(seq,160);} seq(); }); var addBtn=document.getElementById('add_cmd'); if(addBtn) addBtn.addEventListener('click',function(){ if(currentCount >= MAX){ customAlert(t('cmdsMaxRows','Maximum of '+MAX+' commands reached')); return; } addRow(); }); }
    init();
})();)JS";
