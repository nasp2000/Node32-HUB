#include "page_recipients_asset.h"

const char PAGE_RECIPIENTS_HEAD[] PROGMEM = R"RECIP(<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'><title>Recipients</title><link rel='stylesheet' href='/core.css'><style>/* 📊 Page-specific styles (typography now in CORE_CSS) */ .table-wrapper{width:100%;overflow-x:auto;-webkit-overflow-scrolling:touch;border:1px solid rgba(255,255,255,.1);border-radius:6px}table{width:100%;border-collapse:collapse;font-size:13px;table-layout:fixed;min-width:700px}th,td{border:1px solid rgba(255,255,255,.15);padding:4px 6px;text-align:left;word-break:break-word;overflow-wrap:anywhere}th{background:rgba(255,255,255,.07)}input[type=text]{width:100%;box-sizing:border-box;padding:6px;border-radius:6px;border:1px solid rgba(255,255,255,.18);background:rgba(10,20,45,.8);color:#fff;font-size:13px;min-width:0}.op{width:46px;text-transform:uppercase;text-align:center}.note{opacity:.75;margin-top:8px;line-height:1.3}#rec_body td{vertical-align:middle}#rec_body input,#rec_body select{min-height:26px!important;height:auto!important;padding:2px 4px!important;font-size:13px!important}#rec_body button[data-save]{width:auto!important;max-width:none!important;min-width:36px!important;min-height:28px!important;height:28px!important;padding:0 8px!important;border-radius:16px!important;display:flex!important;align-items:center!important;justify-content:center!important;margin:0 auto!important;font-size:13px!important}#rec_body select.op{margin:0 auto}.sender-copy-wrap{display:flex;align-items:flex-start;justify-content:flex-start;gap:12px;cursor:pointer;text-align:left}.sender-copy-wrap input[type=checkbox]{width:20px;height:20px;flex-shrink:0;margin-top:2px}.sender-copy-title{color:#e0e7ff;font-size:14px;line-height:1.35;text-align:left}.sender-copy-desc{font-size:11px;color:#94a3b8;line-height:1.6;display:block;margin-top:6px;text-align:left}@media(max-width:768px){.table-wrapper{overflow-x:auto;margin:0 -10px;padding:0 10px}table{min-width:850px}}@media(max-width:640px){html,body,.wrap{overflow-x:hidden}.table-wrapper{overflow-x:auto;margin:0 -8px;padding:0 8px;border:none}table{min-width:900px;margin:0 auto}table thead th{width:auto!important}th,td{padding:4px}input[type=text]{font-size:13px;padding:5px}.op{width:42px}}@media(max-width:480px){.table-wrapper{overflow-x:auto;margin:0 -6px;padding:0 6px}table{min-width:950px}#rec_body input,#rec_body select{font-size:13px!important;min-height:26px!important;height:auto!important;padding:2px 4px!important}#rec_body button[data-save]{font-size:13px!important;height:26px!important;min-height:26px!important;min-width:34px!important}}.acc-tab-bar{display:flex;flex-wrap:nowrap;gap:6px;margin:10px 0 0 0}.acc-tab-btn{flex:1 1 0;min-width:0;border:2px solid #1e40af;border-radius:8px;background:#0f172a;color:#93c5fd;padding:8px 6px;font-size:12px;cursor:pointer;transition:all .2s ease;text-align:center;white-space:nowrap}.acc-tab-btn.active{background:#1d4ed8;color:#fff;border-color:#3b82f6;box-shadow:0 0 0 1px rgba(96,165,250,.35) inset}.acc-panel{display:none}.acc-panel.active{display:block}</style></head><body><div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'><div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div><p style='color:#e6f1ff;margin-top:15px;font-size:13px' data-i18n='loading'>Processing...</p></div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style><main class='page'><div class='wrap'><div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='recipTipHome'>🏠 Home</a></div><h1 data-i18n='recipPageTitle'>📧 Recipients</h1><div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:16px'><h2 style='margin-top:0;color:#60a5fa' data-i18n='recipSenderTitle'>✉️ Email Sender</h2><label class='sender-copy-wrap'><span><strong class='sender-copy-title' data-i18n='recipDontCopyLbl'>🚫 Don't send automatic copy to the sender</strong><span class='sender-copy-desc' data-i18n-html='recipDontCopyDesc'>✅ <strong>Active:</strong> the sender does not receive a copy of <strong>any email</strong> sent by the system (except email test).<br>☑️ <strong>Disabled:</strong> the sender receives a copy of <strong>each send.</strong><br>📌 <em>Note: when the SD card is active, emails are sent only to rule recipients — the sender never receives an automatic copy, regardless of this option.</em></span></span><input type='checkbox' id='email_dont_copy_sender' value='1' title='Sender copy toggle' data-i18n-title='recipTipDontCopy' )RECIP";

const char PAGE_RECIPIENTS_AFTER_DONT_COPY[] PROGMEM = R"RECIP(></label></div><div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:20px'><h2 style='margin-top:0;color:#60a5fa' data-i18n='recipHowToTitle'>📋 How to Configure a Rule</h2><h3 style='margin:0 0 6px 0;color:#60a5fa' data-i18n='recipKeywordTitle'>Name / Keyword field format</h3><div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:10px 12px;border-radius:4px;font-size:13px;line-height:1.85;color:#e0e7ff;margin-bottom:14px' data-i18n-html='recipKeywordDesc'><div><b style='color:#93c5fd'>1. Simple text</b> — exact word, word-bounded. <code>FAULT</code> + op <b>I</b> triggers on 'FAULT' (not on 'NOFAULT')</div><div><b style='color:#93c5fd'>2. key, threshold</b> — looks for 'key,' in text and compares the following number. Ex: <code>temp_aht20, 25.5</code> → reads value after 'temp_aht20,'</div><div><b style='color:#93c5fd'>3. colN, threshold</b> — compares the Nth column number by position. Ex: <code>col3, 10600</code> → evaluates 3rd column, ignores rest</div><div><b style='color:#93c5fd'>4. any, threshold</b> — scans all numbers in the line. Ex: <code>any, 999</code> + op <b>+</b> → triggers if any number &gt; 999</div><div><b style='color:#93c5fd'>5. Range min..max</b> — bare: <code>20.00..45.00</code> | CSV: <code>rpm, 800..3500</code>. Use with op <b>R</b> (inside) or <b>DR</b> (outside)</div><div><b style='color:#93c5fd'>6. Exact format</b> (no comma) — threshold defines the exact digit pattern. Ex: <code>019.00</code> only accepts ddd.dd; <code>t 030.00</code> looks for 't' then ddd.dd</div></div><h3 style='margin:0 0 6px 0;color:#60a5fa' data-i18n='recipOpsTitle'>Operators (OP field)</h3><div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:10px 12px;border-radius:4px;font-size:13px;line-height:1.85;color:#e0e7ff' data-i18n-html='recipOpsDesc'><div><b style='color:#93c5fd'>I = Equal</b> — text: exact word | numeric: value = threshold. Ex: <code>FAULT</code> or <code>temp_aht20, 25.0</code></div><div><b style='color:#93c5fd'>D = Different</b> — value ≠ threshold. Ex: <code>humidity, 60.0</code> → triggers when ≠ 60.0</div><div><b style='color:#93c5fd'>+ = Greater</b> — value &gt; threshold. Ex: <code>temp_aht20, 30.0</code> → triggers when &gt; 30.0</div><div><b style='color:#93c5fd'>- = Less</b> — value &lt; threshold. Ex: <code>pressure_hpa, 980.0</code> → triggers when &lt; 980.0</div><div><b style='color:#93c5fd'>G = Greater or equal</b> — value ≥ threshold. Ex: <code>corrente, 10.5</code> → triggers when ≥ 10.5A</div><div><b style='color:#93c5fd'>L = Less or equal</b> — value ≤ threshold. Ex: <code>bateria, 20.0</code> → triggers when ≤ 20%</div><div><b style='color:#93c5fd'>R = Range</b> — value inside [min..max]. Ex: <code>rpm, 800..3500</code> → triggers when rpm is between 800 and 3500</div><div><b style='color:#93c5fd'>DR = Out of range</b> — value outside [min..max]. Ex: <code>tensao, 210..250</code> → triggers when &lt;210 or &gt;250</div><div><b style='color:#93c5fd'>V = Variation (delta)</b> — |current − previous| ≥ threshold. Ex: <code>nivel, 5.0</code> → triggers if varies more than 5 between readings</div><div><b style='color:#93c5fd'>C = Count</b> — triggers on Nth occurrence (resets after sending). Ex: <code>OVERLOAD, 3</code> → alerts on 3rd time</div><div><b style='color:#93c5fd'>Cw = Window count</b> — N occurrences in W seconds. Ex: <code>FAULT, 4/60</code> → alerts if appears 4x in 60s</div><div><b style='color:#93c5fd'>Hy = Hysteresis</b> — triggers at high, resets at low. Ex: <code>temp_aht20, 35..28</code> → alerts ≥35°C, resets &lt;28°C</div><div><b style='color:#93c5fd'>P = Presence (watchdog)</b> — triggers if text does not arrive in time. Ex: <code>HEARTBEAT, 120</code> → alerts if missing 120s</div><div style='margin-top:8px;color:#94a3b8;font-size:11px;border-top:1px solid rgba(96,165,250,0.2);padding-top:6px'>📧 <b>Emails:</b> comma separated. Ex: supervisor@company.com, log@company.com &nbsp;&nbsp; ⏱ Minimum frequency between alerts of the same rule: 3 minutes.</div></div></div><div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:20px'><h2 style='margin-top:0;color:#60a5fa' data-i18n='recipListTitle'>📋 Recipients List</h2><div class='table-wrapper'><table><thead><tr><th style='width:32px'>#</th><th style='width:150px' data-i18n='recipTableNameOp'>Name / Keyword</th><th style='width:85px' data-i18n='recipTableOp'>OP</th><th style='width:210px' data-i18n='recipTableEmails'>Emails (comma separated)</th><th style='width:110px' data-i18n='recipTableFreq'>Frequency</th><th style='width:72px' data-i18n='recipTableActive'>Active</th><th style='width:56px'></th></tr></thead><tbody id='rec_body'></tbody></table></div><div style='text-align:center;margin-top:14px'><button type='button' id='save_all' class='btn' style='padding:10px 24px;font-size:13px' data-i18n='btnSaveAll' title='Save all rules' data-i18n-title='recipTipSaveAll'>💾 Save All</button> <button type='button' id='add_rule' class='btn' style='padding:10px 24px;font-size:13px;background:#065f46;border:1px solid #10b981' data-i18n='recipAddRule' title='Add a new rule row' data-i18n-title='recipTipAddRule'>➕ Add Rule</button></div></div>)RECIP";

// Day-commands section: 4 fragments with EMAILDAY/LOGDAY/SENSORDAY values injected by renderer
const char PAGE_RECIPIENTS_DAY_CMDS_A[] PROGMEM = R"DAYCMD(<div style='border:2px solid #3b82f6;border-radius:8px;padding:16px;background:#0f172a;margin-top:20px'><h2 style='margin-top:0;color:#60a5fa'>📨 Day Command Sends</h2><p style='font-size:12px;color:#94a3b8;margin:0 0 10px 0'>Additional emails for EMAILDAY, LOGDAY and SENSORDAY. Separate multiple emails with commas.</p><div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:8px 12px;border-radius:4px;font-size:12px;color:#e0e7ff;margin-bottom:14px'><div><strong style='color:#60a5fa'>📧 EMAILDAY</strong> — sends day files by email. Ex: <code style='background:#0f172a;padding:1px 5px;border-radius:3px;color:#38bdf8'>emailday</code> &nbsp;|&nbsp; <code style='background:#0f172a;padding:1px 5px;border-radius:3px;color:#38bdf8'>emailday 29-12-2025</code></div><div style='margin-top:4px'><strong style='color:#a78bfa'>📋 LOGDAY</strong> — sends day log by email. Ex: <code style='background:#0f172a;padding:1px 5px;border-radius:3px;color:#38bdf8'>logday</code> &nbsp;|&nbsp; <code style='background:#0f172a;padding:1px 5px;border-radius:3px;color:#38bdf8'>logday 28-02-2026</code></div><div style='margin-top:4px'><strong style='color:#34d399'>🌡️ SENSORDAY</strong> — sends sensor history by email. Ex: <code style='background:#0f172a;padding:1px 5px;border-radius:3px;color:#38bdf8'>sensorday</code> &nbsp;|&nbsp; <code style='background:#0f172a;padding:1px 5px;border-radius:3px;color:#38bdf8'>sensorday 01-03-2026</code></div><div style='margin-top:6px;font-size:11px;color:#94a3b8'>💡 Send these commands from the command page or Telnet. The emails below are added to the main destination.</div></div><div style='margin-top:6px'><label style='display:block;font-weight:bold;color:#e0e7ff;font-size:13px;margin-bottom:4px'>📧 EMAILDAY <span style='font-weight:normal;color:#94a3b8;font-size:12px'>(sends day files by email)</span></label><input type='text' id='day_emailday_emails' title='EMAILDAY recipients' data-i18n-title='recipTipDayEmailday' placeholder='email1@example.com, email2@example.com' style='width:100%;padding:8px;border:1px solid #3b82f6;border-radius:4px;font-size:13px;background:#0a1929;color:#e0e7ff;box-sizing:border-box' value=')DAYCMD";

const char PAGE_RECIPIENTS_DAY_CMDS_B[] PROGMEM = R"DAYCMD('></div><div style='margin-top:10px'><label style='display:block;font-weight:bold;color:#e0e7ff;font-size:13px;margin-bottom:4px'>📋 LOGDAY <span style='font-weight:normal;color:#94a3b8;font-size:12px'>(sends day log by email)</span></label><input type='text' id='day_logday_emails' title='LOGDAY recipients' data-i18n-title='recipTipDayLogday' placeholder='email1@example.com, email2@example.com' style='width:100%;padding:8px;border:1px solid #3b82f6;border-radius:4px;font-size:13px;background:#0a1929;color:#e0e7ff;box-sizing:border-box' value=')DAYCMD";

const char PAGE_RECIPIENTS_DAY_CMDS_C[] PROGMEM = R"DAYCMD('></div><div style='margin-top:10px'><label style='display:block;font-weight:bold;color:#e0e7ff;font-size:13px;margin-bottom:4px'>🌡️ SENSORDAY <span style='font-weight:normal;color:#94a3b8;font-size:12px'>(sends sensor history of the day by email)</span></label><input type='text' id='day_sensorday_emails' title='SENSORDAY recipients' data-i18n-title='recipTipDaySensorday' placeholder='email1@example.com, email2@example.com' style='width:100%;padding:8px;border:1px solid #3b82f6;border-radius:4px;font-size:13px;background:#0a1929;color:#e0e7ff;box-sizing:border-box' value=')DAYCMD";

const char PAGE_RECIPIENTS_DAY_CMDS_D[] PROGMEM = R"DAYCMD('></div><div style='text-align:center;margin-top:16px'><button type='button' id='save_day_emails' class='btn' style='padding:10px 24px' title='Save day command destinations' data-i18n-title='recipTipDaySave'>💾 Save Sends</button><div id='day_emails_msg' style='margin-top:6px;font-size:12px;color:#94a3b8;min-height:16px'></div></div></div><script>(function(){var saveBtn=document.getElementById('save_day_emails');var msgDiv=document.getElementById('day_emails_msg');function postEmail(url,val,cb){var fd=new FormData();fd.append('emails',val);fd.append('_html','1');fetch(url,{method:'POST',body:fd}).then(function(r){return r.json();}).then(cb).catch(function(){cb(null);});}if(saveBtn)saveBtn.addEventListener('click',function(){var e1=document.getElementById('day_emailday_emails');var e2=document.getElementById('day_logday_emails');var e3=document.getElementById('day_sensorday_emails');var v1=e1?e1.value.trim():'';var v2=e2?e2.value.trim():'';var v3=e3?e3.value.trim():'';disableBtn(saveBtn);showLoading();var done=0;var ok=true;function finish(j){done++;if(!j||j.status!=='ok')ok=false;if(done===3){hideLoading();enableBtn(saveBtn);if(ok){if(typeof customAlert==='function')customAlert('✅ Day destinations saved!');if(typeof showSavedCard==='function')showSavedCard();if(msgDiv)msgDiv.textContent='✅ Saved';}else{if(typeof customAlert==='function')customAlert('❌ Error saving destinations');if(msgDiv)msgDiv.textContent='❌ Error';}setTimeout(function(){if(msgDiv)msgDiv.textContent='';},3000);}}postEmail('/api/emailday-extra-emails',v1,finish);postEmail('/api/logday-extra-emails',v2,finish);postEmail('/api/sensorday-extra-emails',v3,finish);});})();</script>)DAYCMD";

const char PAGE_RECIPIENTS_JS[] PROGMEM = R"RECJS((function(){
    function rt(k,fb){
        try{
            if(typeof window._t==='function') return window._t(k,fb);
            var raw=localStorage.getItem('ui_lang_cache');
            if(raw){
                var parsed=JSON.parse(raw);
                if(parsed&&parsed[k]!==undefined) return parsed[k];
            }
        }catch(err){}
        return fb!==undefined?fb:k;
    }
    var MAX=50;
    var currentCount=0;
    var b=document.getElementById('rec_body');
    var saveAllBtn=document.getElementById('save_all');
    var cb=document.getElementById('email_dont_copy_sender');
    function rowHTML(i){
        return "<tr data-i='"+i+"'>"
            +"<td style='width:32px;text-align:center;font-weight:600'>"+i+"</td>"
            +"<td><input type='text' name='r_name_"+i+"' maxlength='50' title='Rule keyword or threshold' data-i18n-title='recipTipName'><div class='nm-hint' style='font-size:10px;margin-top:2px;line-height:1.4'></div></td>"
            +"<td><div style='display:flex;align-items:center;gap:6px'><select class='op' name='r_op_"+i+"' title='Comparison operator' data-i18n-title='recipTipRuleOp' style='min-width:85px;font-size:10px'><option value='I'>"+rt('opEqual','I (equal)')+"</option><option value='D'>"+rt('opDiff','D (different)')+"</option><option value='+'>"+rt('opGt','+ (greater)')+"</option><option value='-'>"+rt('opLt','- (less)')+"</option><option value='G'>"+rt('opGte','G (greater or equal)')+"</option><option value='L'>"+rt('opLte','L (less or equal)')+"</option><option value='R'>"+rt('opRange','R (range)')+"</option><option value='DR'>"+rt('opOutRange','DR (out-of-range)')+"</option><option value='V'>"+rt('opVariation','V (variation)')+"</option><option value='C'>"+rt('opCount','C (count)')+"</option><option value='P'>"+rt('opPresence','P (presence)')+"</option><option value='Cw'>"+rt('opWindow','Cw (window)')+"</option><option value='Hy'>"+rt('opHysteresis','Hy (hysteresis)')+"</option></select></div></td>"
            +"<td><input type='text' name='r_emails_"+i+"' maxlength='256' title='Recipient email addresses' data-i18n-title='recipTipEmails' style='width:100%'></td>"
            +"<td><select name='r_freq_"+i+"' title='Alert frequency limit' data-i18n-title='recipTipFreq' style='min-width:110px;font-size:10px'><option value='180'>"+rt('freq3m','1 alert/3m')+"</option><option value='300'>"+rt('freq5m','1 alert/5m')+"</option><option value='600' selected>"+rt('freq10m','1 alert/10m')+"</option><option value='1200'>"+rt('freq20m','1 alert/20m')+"</option><option value='1800'>"+rt('freq30m','1 alert/30m')+"</option><option value='2700'>"+rt('freq45m','1 alert/45m')+"</option><option value='3600'>"+rt('freq1h','1 alert/1h')+"</option><option value='7200'>"+rt('freq2h','1 alert/2h')+"</option><option value='14400'>"+rt('freq4h','1 alert/4h')+"</option><option value='21600'>"+rt('freq6h','1 alert/6h')+"</option><option value='43200'>"+rt('freq12h','1 alert/12h')+"</option><option value='86400'>"+rt('freq24h','1 alert/24h')+"</option></select></td>"
            +"<td style='width:72px;text-align:center'><select name='r_active_"+i+"' title='Enable or disable rule' data-i18n-title='recipTipActive' style='font-size:11px;width:65px;padding:2px'><option value='1'>"+rt('opYes','Yes')+"</option><option value='0'>"+rt('opNo','No')+"</option></select></td>"
            +"<td style='width:56px;text-align:center'><button type='button' data-save='"+i+"' title='Save this rule' data-i18n-title='recipTipSaveRow' style='display:flex;align-items:center;justify-content:center;height:32px;min-width:36px;font-size:14px;padding:0 10px;line-height:1;border-radius:16px;background:#1e66c7;border:1px solid #20a3b9;color:#fff;cursor:pointer'>💾</button><button type='button' data-del='"+i+"' title='Remove this rule' data-i18n-title='recipTipDelRow' style='display:flex;align-items:center;justify-content:center;height:32px;min-width:36px;font-size:14px;padding:0 10px;line-height:1;border-radius:16px;background:#7f1d1d;border:1px solid #dc2626;color:#fff;cursor:pointer;margin-left:4px'>🗑️</button></td>"
        +"</tr>";
    }
    function addRow(){
        if(currentCount >= MAX) return false;
        currentCount++;
        var d=document.createElement('tbody');
        d.innerHTML=rowHTML(currentCount);
        var row=d.removeChild(d.firstChild);
        b.appendChild(row);
        attachRow(row);
        return true;
    }
    function updateNameHint(tr,i){
        var ni=tr.querySelector("input[name='r_name_"+i+"']");
        var os=tr.querySelector("select[name='r_op_"+i+"']");
        var hd=tr.querySelector('.nm-hint');
        if(!ni||!os||!hd) return;
        var val=ni.value.trim(),op=os.value;
        if(!val){hd.innerHTML='';return;}
        var ci=val.indexOf(','),kwt='',thr='',key='',n='';
        if(ci>0){
            key=val.substring(0,ci).trim().toLowerCase();
            thr=val.substring(ci+1).trim();
            if(/^col\d+$/.test(key)){kwt='col';n=key.replace('col','');}
            else if(key==='any'){kwt='any';}
            else{kwt='key';}
        } else if(/^\d+\.?\d*\.\.\d+\.?\d*$/.test(val)){kwt='range';thr=val;}
        else if(/^\d[\d.]+$/.test(val)){kwt='exact';}
        else{kwt='text';}
        var nr=op==='R'||op==='DR',isHy=op==='Hy';
        var G='color:#4ade80',O='color:#f59e0b',R='color:#f87171',BL='color:#93c5fd';
        var h='';
        if(kwt==='col'){
            if((nr||isHy)&&thr.indexOf('..')<0) h='<span style="'+O+'">⚠️ Op '+op+' requires range <b>min..max</b> in threshold</span>';
            else h='<span style="'+G+'">📍 Col <b>'+n+'</b> · Nth numeric column · Threshold: <b>'+(thr||'?')+'</b></span>';
        } else if(kwt==='any'){
            if(nr&&thr.indexOf('..')<0) h='<span style="'+O+'">⚠️ Op R/DR requires range in threshold (e.g. 10..50)</span>';
            else h='<span style="'+G+'">🔢 <b>any</b> · Scans all numbers · Threshold: <b>'+(thr||'?')+'</b></span>';
        } else if(kwt==='key'){
            if(op==='P') h='<span style="'+G+'">⏱ Watchdog · Alerts if <b>'+key+'</b> does not arrive in <b>'+(thr||'?')+'</b>s</span>';
            else if((nr||isHy)&&thr.indexOf('..')<0) h='<span style="'+O+'">⚠️ Op '+op+' requires range in threshold (e.g. '+key+', 10..50)</span>';
            else h='<span style="'+G+'">🔑 Key · Looks for <b>'+key+',</b> in text · Threshold: <b>'+(thr||'?')+'</b></span>';
        } else if(kwt==='range'){
            if(!nr) h='<span style="'+O+'">⚠️ Range · Use op <b>R</b> (inside) or <b>DR</b> (out of range)</span>';
            else h='<span style="'+G+'">📏 Range <b>'+val+'</b> · Any number in the line</span>';
        } else if(kwt==='exact'){
            h='<span style="'+BL+'">🎯 Exact format · Pattern: <b>'+val+'</b></span>';
        } else {
            if(op==='I'||op==='D') h='<span style="'+G+'">🔤 Exact text · Word: <b>'+val+'</b></span>';
            else if(op==='P') h='<span style="'+G+'">⏱ Watchdog · Alerts if <b>'+val+'</b> does not arrive</span>';
            else if(op==='C'||op==='Cw') h='<span style="'+G+'">🔢 Count · Occurrences of <b>'+val+'</b></span>';
            else h='<span style="'+R+'">⚠️ Numeric op (<b>'+op+'</b>) with simple text — no numeric value</span>';
        }
        hd.innerHTML=h;
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
        var i=parseInt(tr.getAttribute('data-i'));
        if(i){
            var ni=tr.querySelector("input[name='r_name_"+i+"']");
            var os=tr.querySelector("select[name='r_op_"+i+"']");
            if(ni) ni.addEventListener('input',function(){updateNameHint(tr,i);});
            if(os) os.addEventListener('change',function(){updateNameHint(tr,i);});
        }
    }
    function removeRow(i){
        var tr=b.querySelector("tr[data-i='"+i+"']");
        if(!tr) return;
        window.customConfirm(rt('recipConfirmRemove','Remove rule '+i+'?'),function(ok){
            if(!ok) return;
            // Collect all row values except the removed one, then rebuild the
            // table with fresh indices so slot numbers stay contiguous 1..N.
            var data=[];
            var rows=b.querySelectorAll('tr[data-i]');
            for(var r=0;r<rows.length;r++){
                var row=rows[r];
                var old=parseInt(row.getAttribute('data-i'));
                if(old===i) continue;
                data.push({
                    name:(row.querySelector("input[name='r_name_"+old+"']")||{}).value||'',
                    op:(row.querySelector("select[name='r_op_"+old+"']")||{}).value||'I',
                    emails:(row.querySelector("input[name='r_emails_"+old+"']")||{}).value||'',
                    freq:(row.querySelector("select[name='r_freq_"+old+"']")||{}).value||'600',
                    active:(row.querySelector("select[name='r_active_"+old+"']")||{}).value||'1'
                });
            }
            b.innerHTML='';
            currentCount=0;
            for(var d=0;d<data.length;d++){
                addRow();
                var idx=d+1;
                var nrow=b.querySelector("tr[data-i='"+idx+"']");
                if(!nrow) continue;
                var f;
                f=nrow.querySelector("input[name='r_name_"+idx+"']"); if(f) f.value=data[d].name;
                f=nrow.querySelector("select[name='r_op_"+idx+"']"); if(f) f.value=data[d].op;
                f=nrow.querySelector("input[name='r_emails_"+idx+"']"); if(f) f.value=data[d].emails;
                f=nrow.querySelector("select[name='r_freq_"+idx+"']"); if(f) f.value=data[d].freq;
                f=nrow.querySelector("select[name='r_active_"+idx+"']"); if(f) f.value=data[d].active;
                updateNameHint(nrow,idx);
            }
            preload();
        });
    }
    function saveSlot(i,btn){
        var tr=b.querySelector("tr[data-i='"+i+"']");
        if(!tr) return;
        var fd=new FormData();
        fd.append('i',i);
        fd.append('name',(tr.querySelector("input[name='r_name_"+i+"']")||{}).value||'');
        fd.append('op',(tr.querySelector("select[name='r_op_"+i+"']")||{}).value||'I');
        fd.append('emails',(tr.querySelector("input[name='r_emails_"+i+"']")||{}).value||'');
        fd.append('freq',(tr.querySelector("select[name='r_freq_"+i+"']")||{}).value||'0');
        fd.append('active',(tr.querySelector("select[name='r_active_"+i+"']")||{}).value||'1');
        if(btn){disableBtn(btn);showLoading();}
        fetch('/api/recipient-slot',{method:'POST',body:new URLSearchParams(fd),headers:{'Content-Type':'application/x-www-form-urlencoded'}}).then(function(r){return r.text().then(function(t){return {ok:r.ok,txt:t};});}).then(function(o){
            var j=null; try{j=JSON.parse(o.txt);}catch(e){}
            if(j&&j.status==='ok'){ customAlert('\u2705 '+rt('recipSaved','Recipient saved!')); showSavedCard(); }
            else{ customAlert('\u274C '+rt('recipError','Error saving recipient')); }
        }).catch(function(){
            customAlert('\u274C '+rt('recipNetError','Network error saving recipient'));
        }).finally(function(){if(btn){hideLoading();enableBtn(btn);}});
    }
    function attachSaveAll(){
        if(saveAllBtn) saveAllBtn.addEventListener('click',function(){
            var i=1;
            (function seq(){ if(i>currentCount) return; var tr=b.querySelector("tr[data-i='"+i+"']"); var btn=tr?tr.querySelector('[data-save]'):null; if(btn){ saveSlot(i,btn); setTimeout(function(){ i++; seq(); },120);} else { i++; seq(); } })();
        });
        var addBtn=document.getElementById('add_rule');
        if(addBtn) addBtn.addEventListener('click',function(){
            if(currentCount >= MAX){ customAlert(rt('recipMaxRules','Maximum of '+MAX+' rules reached')); return; }
            addRow();
        });
        if(cb) cb.addEventListener('change',saveDontCopy);
    }
    function saveDontCopy(){
        if(!cb) return;
        var fd=new FormData();
        fd.append('_html','1');
        fd.append('section','recipients');
        fd.append('email_dont_copy_sender', cb.checked?'1':'0');
        fetch('/api/config',{method:'POST',body:new URLSearchParams(fd),headers:{'Content-Type':'application/x-www-form-urlencoded'}}).catch(function(){});
    }
    function preload(){
        fetch('/api/recipient-slots',{cache:'no-store'}).then(function(r){return r.ok?r.json():null;}).then(function(j){
            if(!j||!j.slots) return;
            for(var i=1;i<=currentCount && i<=j.slots.length;i++){
                var s=j.slots[i-1]||{};
                var tr=b.querySelector("tr[data-i='"+i+"']");
                if(!tr) continue;
                var f;
                f=tr.querySelector("input[name='r_name_"+i+"']"); if(f&&s.name) f.value=s.name;
                f=tr.querySelector("select[name='r_op_"+i+"']"); if(f&&s.op) f.value=s.op;
                f=tr.querySelector("input[name='r_emails_"+i+"']"); if(f&&s.emails) f.value=s.emails;
                f=tr.querySelector("select[name='r_freq_"+i+"']"); if(f&&s.freq) f.value=s.freq;
                f=tr.querySelector("select[name='r_active_"+i+"']"); if(f) f.value=(s.active===false||s.active===0)?'0':'1';
                updateNameHint(tr,i);
            }
        }).catch(function(){});
    }
    function init(){ for(var i=0;i<10;i++) addRow(); attachSaveAll(); preload(); }
    init();

    // ── Tab system ──────────────────────────────────────────────────────────
    (function(){
      var wrap = document.querySelector('.wrap');
      if (!wrap) return;
      if (wrap.querySelector('.acc-tab-bar')) return;

      // Find bordered sections in document order
      var els = [];
      for (var i = 0; i < wrap.children.length; i++) {
        var el = wrap.children[i];
        if (el.nodeType !== 1) continue;
        var s = el.getAttribute('style') || '';
        if (s.indexOf('border:2px solid #3b82f6') >= 0) {
          els.push(el);
        }
      }
      // els[0]=Sender, els[1]=HowTo, els[2]=Recipients, els[3]=DayCmds
      if (els.length < 4) return;

      // Group into 3 tabs: Remetente | Regras (HowTo+Recipients) | Comandos
      var groups = [
        { key:'sender', label:rt('recipTabSender','\u2709\ufe0f Remetente'), nodes:[els[0]] },
        { key:'rules',  label:rt('recipTabRules','\uD83D\uDCCB Regras'),      nodes:[els[1], els[2]] },
        { key:'cmds',   label:rt('recipTabCmds','\uD83D\uDCE8 Comandos'),     nodes:[els[3]] }
      ];
      var activeKey = 'sender';

      function switchTab(key) {
        activeKey = key;
        var btns = tabBar.querySelectorAll('.acc-tab-btn');
        for (var i = 0; i < btns.length; i++) {
          btns[i].classList.toggle('active', btns[i].getAttribute('data-tab') === key);
        }
        var panels = wrap.querySelectorAll('.acc-panel');
        for (var i = 0; i < panels.length; i++) {
          panels[i].classList.toggle('active', panels[i].getAttribute('data-tab') === key);
        }
      }

      // Create tab bar
      var tabBar = document.createElement('div');
      tabBar.className = 'acc-tab-bar';
      for (var g = 0; g < groups.length; g++) {
        var btn = document.createElement('button');
        btn.className = 'acc-tab-btn' + (groups[g].key === activeKey ? ' active' : '');
        btn.setAttribute('data-tab', groups[g].key);
        btn.textContent = groups[g].label;
        btn.addEventListener('click', (function(k){ return function(){ switchTab(k); }; })(groups[g].key));
        tabBar.appendChild(btn);
      }

      // Insert tab bar below home button (BEFORE h1)
      var homeDiv = wrap.querySelector('.actions.nav-inicio');
      if (homeDiv && homeDiv.parentNode) {
        homeDiv.parentNode.insertBefore(tabBar, homeDiv.nextSibling);
      } else {
        wrap.insertBefore(tabBar, wrap.firstChild);
      }

      // Wrap each group's nodes in acc-panel divs
      for (var g = 0; g < groups.length; g++) {
        var panel = document.createElement('div');
        panel.className = 'acc-panel' + (groups[g].key === activeKey ? ' active' : '');
        panel.setAttribute('data-tab', groups[g].key);
        var parent = groups[g].nodes[0].parentNode;
        parent.insertBefore(panel, groups[g].nodes[0]);
        for (var n = 0; n < groups[g].nodes.length; n++) {
          panel.appendChild(groups[g].nodes[n]);
        }
      }

      // Initial visibility — handled by .acc-panel.active CSS class on panels
      // already set during panel creation above
    })();
})();)RECJS";

const char PAGE_RECIPIENTS_FOOTER[] PROGMEM = R"RECIP(</script></div></main></body></html>)RECIP";
