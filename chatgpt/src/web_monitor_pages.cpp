#include "feature_flags.h"

#include "web_monitor_pages.h"



void streamSharedMonitorPage(Print& out) {

    out.print(F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title id='rx_page_title'>📥 Data Reception</title><link rel='stylesheet' href='/core.css'><script>window._localPagesNeverExpire=true;</script><script src='/auto.js'></script></head><body><div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'><div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div><p id='rx_loading_text' style='color:#e6f1ff;margin-top:15px;font-size:14px'>Processing...</p></div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style><main class='page'><div class='wrap'>"));

    out.print(F("<div class='actions nav-inicio'><a href='/' class='btn' id='rx_btn_home'>🏠 Home</a></div><h1 id='rx_page_heading'>📥 Data Reception</h1>"));

    out.print(F("<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:16px'><h2 id='rx_actions_title' style='margin-top:0;font-size:18px;color:#60a5fa'>⚙️ Actions</h2><div id='rx_actions_info' style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:12px 0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px'><div>🔄 <strong style='color:#60a5fa'>Refresh:</strong> Updates the buffer view with the latest data</div><div>🧹 <strong style='color:#60a5fa'>Clear:</strong> Empties the reception content (irreversible action)</div><div>🪟 <strong style='color:#60a5fa'>Open RX Viewer:</strong> Opens advanced viewer in new window (local processing on the PC)</div></div><div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin-top:14px'><button id='btn_refresh' class='btn' style='background:#3b82f6;color:white;padding:10px 16px;font-size:13px;border:1px solid #3b82f6;white-space:normal;line-height:1.2;min-height:44px'>🔄 Refresh</button><button class='btn' id='btn_clear' type='button' style='background:#dc2626;color:white;padding:10px 16px;font-size:13px;border:1px solid #dc2626;white-space:normal;line-height:1.2;min-height:44px'>🧹 Clear</button><button class='btn' id='btn_open_viewer' type='button' style='background:#059669;color:white;padding:10px 16px;font-size:13px;border:1px solid #059669;white-space:normal;line-height:1.2;min-height:44px'>🪟 Open RX Viewer</button></div></div>"));

    out.print(F("<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'><h2 id='rx_send_title' style='margin-top:0;font-size:18px;color:#60a5fa'>📤 Send Command</h2><div id='rx_send_info' style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:12px 0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px'><div>⌨️ Select the destination and type the command, then press <strong style='color:#60a5fa'>Enter</strong> or click <strong style='color:#60a5fa'>Send</strong></div><div>💡 <strong style='color:#60a5fa'>Tip:</strong> Send commands to different ports and systems</div></div><div style='display:flex;flex-direction:column;gap:8px;margin-top:14px'><div style='display:flex;gap:8px;align-items:center'><select id='cmd_dest' style='width:90px!important;max-width:90px!important;min-width:0!important;flex-shrink:0;padding:5px 6px!important;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px!important;cursor:pointer;min-height:0!important;height:32px!important'><option id='cmd_dest_sys' value='sys' selected>System</option>"));

#if ENABLE_RS232==1

    out.print(F("<option value='rs232'>RS232</option>"));

#endif

#if ENABLE_MODBUS==1

    out.print(F("<option value='modbus'>Modbus</option>"));

#endif

    out.print(F("</select><input id='cmd' type='text' placeholder='Enter command' style='flex:1;min-width:0;padding:5px 10px!important;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:13px!important;min-height:0!important;height:32px!important;box-sizing:border-box'><button class='btn' id='btn_send' type='button' style='width:auto!important;max-width:none!important;min-width:0!important;flex-shrink:0;background:#2563eb;color:white;padding:5px 12px!important;font-size:13px!important;border:1px solid #2563eb;white-space:nowrap;border-radius:6px;min-height:0!important;height:32px!important'>📤 Send</button></div></div></div>"));

    out.print(F("<div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'><h2 id='rx_buffer_title' style='margin:0 0 10px 0;font-size:18px;color:#60a5fa;text-align:center'>📊 RX Buffer Content</h2><div id='rx_buffer_desc' style='font-size:13px;opacity:.85;margin-bottom:10px;color:#e0e7ff'>Real-time view of received data (last 100 lines)</div><pre id='rx_text' style='max-height:1600px;overflow:auto;padding:12px;background:#020617;border:1px solid #334155;border-radius:6px;white-space:pre;line-height:1.3em;font-size:13px;margin:0;font-family:monospace;tab-size:8;color:#e0e7ff'></pre></div></div></div></main>"));

    out.print(F("<script>(function(){try{fetch('/api/monitor-open',{method:'POST'});}catch(e){};window.addEventListener('beforeunload',function(){try{if(navigator.sendBeacon){navigator.sendBeacon('/api/monitor-close');}else{fetch('/api/monitor-close',{method:'POST',keepalive:true});}}catch(_){}});})();</script>"));

    out.print(F("<script>"));

    out.print(R"js(

                function _rxLangCode(){

                    try{

                        return (localStorage.getItem('ui_lang_code') || document.documentElement.lang || 'en').toLowerCase();

                    }catch(_){

                        return 'en';

                    }

                }



                function _rxT(key, pt, en){

                    var code = _rxLangCode();

                    var fallback = code.indexOf('en') === 0 ? en : pt;

                    try{

                        if(typeof window._t === 'function') return window._t(key, fallback);

                        var raw = localStorage.getItem('ui_lang_cache');

                        if(raw){

                            var parsed = JSON.parse(raw);

                            if(parsed && parsed[key] !== undefined) return parsed[key];

                        }

                    }catch(_){ }

                    return fallback;

                }



                function _setHtml(id, html){

                    var el = document.getElementById(id);

                    if(el) el.innerHTML = html;

                }



                function _setText(id, text){

                    var el = document.getElementById(id);

                    if(el) el.textContent = text;

                }



                function applyMonitorTexts(){

                    var isEn = _rxLangCode().indexOf('en') === 0;

                    document.title = isEn ? '📥 Data Reception' : '📥 Data Reception';

                    _setText('rx_loading_text', _rxT('loading','Processing...','Processing...'));

                    _setText('rx_btn_home', _rxT('btnHome','🏠 Home','🏠 Home'));

                    _setText('rx_page_heading', _rxT('homeDataRecv','📥 Data Reception','📥 Data Reception'));

                    _setText('rx_actions_title', isEn ? '⚙️ Actions' : '⚙️ Actions');

                    _setHtml('rx_actions_info', isEn

                        ? "<div>🔄 <strong style='color:#60a5fa'>Refresh:</strong> Updates the buffer view with the latest data</div><div>🧹 <strong style='color:#60a5fa'>Clear:</strong> Empties the reception content (irreversible action)</div><div>🪟 <strong style='color:#60a5fa'>Open RX Viewer:</strong> Opens the advanced viewer in a new window (local processing on the PC)</div>"

                        : "<div>🔄 <strong style='color:#60a5fa'>Refresh:</strong> Updates the buffer view with the latest data</div><div>🧹 <strong style='color:#60a5fa'>Clear:</strong> Empties the reception content (irreversible action)</div><div>🪟 <strong style='color:#60a5fa'>Open RX Viewer:</strong> Opens the advanced viewer in a new window (local processing on the PC)</div>");

                    _setText('btn_refresh', isEn ? '🔄 Refresh' : '🔄 Refresh');

                    _setText('btn_clear', isEn ? '🧹 Clear' : '🧹 Clear');

                    _setText('btn_open_viewer', isEn ? '🪟 Open RX Viewer' : '🪟 Open RX Viewer');

                    _setText('rx_send_title', isEn ? '📤 Send Command' : '📤 Send Command');

                    _setHtml('rx_send_info', isEn

                        ? "<div>⌨️ Select the destination and type the command, then press <strong style='color:#60a5fa'>Enter</strong> or click <strong style='color:#60a5fa'>Send</strong></div><div>💡 <strong style='color:#60a5fa'>Tip:</strong> Send commands for different ports and systems</div>"

                        : "<div>⌨️ Select the destination and type the command, then press <strong style='color:#60a5fa'>Enter</strong> or click <strong style='color:#60a5fa'>Send</strong></div><div>💡 <strong style='color:#60a5fa'>Tip:</strong> Send commands for different ports and systems</div>");

                    _setText('cmd_dest_sys', isEn ? 'System' : 'System');

                    var cmd = document.getElementById('cmd');

                    if(cmd) cmd.placeholder = isEn ? 'Enter command' : 'Enter command';

                    _setText('btn_send', isEn ? '📤 Send' : '📤 Send');

                    _setText('rx_buffer_title', isEn ? '📊 RX Buffer Content' : '📊 RX Buffer Content');

                    _setText('rx_buffer_desc', isEn ? 'Real-time view of received data (last 100 lines)' : 'Real-time view of received data (last 100 lines)');

                }



                var RX_DEFAULT_LIMIT = 100;

                function _setRxHeightForLines(lines){

                    try{

                        var el = document.getElementById('rx_text'); if(!el) return;

                        var cs = window.getComputedStyle(el);

                        var lh = cs.lineHeight;

                        var lhPx = 0;

                        if(lh && lh.endsWith('px')){ lhPx = parseFloat(lh)||0; }

                        if(!lhPx){

                            var fs = cs.fontSize; var fsPx = (fs&&fs.endsWith('px'))?parseFloat(fs):13;

                            lhPx = Math.round(fsPx * 1.2);

                        }

                        var target = Math.max(0, Math.round(lhPx * (lines||100)));

                        el.style.maxHeight = target + 'px';

                    }catch(_){ }

                }



                function loadRxText(limit){

                    limit = limit || RX_DEFAULT_LIMIT;

                    try{

                        fetch('/api/received-text?limit='+limit+'&t='+(Date.now()), {cache:'no-store'})

                            .then(r=>r.ok?r.text():Promise.resolve(''))

                            .then(txt=>{

                                var el=document.getElementById('rx_text');

                                if(el){

                                    el.textContent = (txt||'').replace(/\n{2,}/g, '\n').trim();

                                    _setRxHeightForLines(RX_DEFAULT_LIMIT);

                                }

                            })

                            .catch(function(){});

                    }catch(_){ }

                }



                var _rxIdleTimer = 0;

                var _lastSeqSeen = 0;

                var _lastRefreshTime = 0;

                var REFRESH_THROTTLE_MS = 3000;

                function _scheduleIdleRefresh(){

                    try{ if(_rxIdleTimer) clearTimeout(_rxIdleTimer); }catch(_){ }

                    _rxIdleTimer = setTimeout(function(){

                        try{

                            var now = Date.now();

                            var timeSinceLastRefresh = now - _lastRefreshTime;

                            if(timeSinceLastRefresh >= REFRESH_THROTTLE_MS){

                                loadRxText(RX_DEFAULT_LIMIT);

                                _lastRefreshTime = now;

                            }

                        }catch(_){ }

                    }, 1000);

                }



                function openBufferFullscreen(){

                    try{

                        var el = document.getElementById('rx_text');

                        if(!el) return;

                        if(el.requestFullscreen){

                            el.requestFullscreen();

                        } else if(el.webkitRequestFullscreen){

                            el.webkitRequestFullscreen();

                        } else if(el.mozRequestFullScreen){

                            el.mozRequestFullScreen();

                        } else if(el.msRequestFullscreen){

                            el.msRequestFullscreen();

                        }

                    }catch(_){ }

                }



                function _pollRxActivity(){

                    try{

                        if (document.hidden) return;

                        fetch('/api/monitor-status?t='+(Date.now()), {cache:'no-store'})

                            .then(r=>r.ok?r.json():Promise.resolve(null))

                            .then(function(j){

                                if(!j) return;

                                var seq = (j.buffer_seq||0);

                                if(seq !== _lastSeqSeen){

                                    _lastSeqSeen = seq;

                                    _scheduleIdleRefresh();

                                }

                            })

                            .catch(function(){});

                    }catch(_){ }

                }

                setInterval(_pollRxActivity, 900);

                (function(){ try{ loadRxText(RX_DEFAULT_LIMIT); }catch(e){} })();

                (function(){ var b=document.getElementById('btn_refresh'); if(b) b.addEventListener('click', function(){ try{ loadRxText(RX_DEFAULT_LIMIT); }catch(e){} }); })();

                (function(){ var b=document.getElementById('btn_clear'); if(!b) return; b.addEventListener('click', function(){

                    try{

                        b.disabled = true; b.style.opacity = '.6';

                        fetch('/api/clear-buffer', {method:'POST'}).then(function(){

                            try{ loadRxText(RX_DEFAULT_LIMIT); }catch(_){ }

                        }).catch(function(){}).finally(function(){ b.disabled=false; b.style.opacity='1'; });

                    }catch(_){ b.disabled=false; b.style.opacity='1'; }

                }); })();

                (function(){ var b=document.getElementById('btn_open_viewer'); if(!b) return; b.addEventListener('click', function(){

                    try{

                        window.open('/buffer-viewer', '_blank');

                    }catch(_){ }

                }); })();

                function send(){

                    var el = document.getElementById('cmd');

                    var dest = document.getElementById('cmd_dest');

                    if(!el) return;

                    var c = (el.value||'').trim();

                    if(!c) return;

                    var target = dest ? dest.value : 'sys';

                    var url = target === 'rs232' ? '/api/rs232-command' : target === 'modbus' ? '/api/modbus-command' : '/api/system-command';

                    var origPlaceholder = el.placeholder;

                    el.placeholder = _rxLangCode().indexOf('en') === 0 ? '⏳ Sending...' : '⏳ Sending...';

                    el.disabled = true;

                    fetch(url, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({command:c})})

                        .then(function(r){ return r.ok ? r.json() : Promise.resolve(null); })

                        .then(function(j){

                            if(j && j.success){

                                el.placeholder = '✅ ' + (j.response || (_rxLangCode().indexOf('en') === 0 ? 'Sent' : 'Sent'));

                                setTimeout(function(){ el.placeholder = origPlaceholder; }, 3000);

                            } else {

                                el.placeholder = (_rxLangCode().indexOf('en') === 0 ? '❌ Error: ' : '❌ Error: ') + (j && j.response ? j.response : (_rxLangCode().indexOf('en') === 0 ? 'Failed' : 'Failed'));

                                setTimeout(function(){ el.placeholder = origPlaceholder; }, 4000);

                            }

                        })

                        .catch(function(){

                            el.placeholder = _rxLangCode().indexOf('en') === 0 ? '❌ Network error' : '❌ Network error';

                            setTimeout(function(){ el.placeholder = origPlaceholder; }, 4000);

                        })

                        .finally(function(){

                            el.value = '';

                            el.disabled = false;

                            el.focus();

                        });

                }

                (function(){

                    try{

                        applyMonitorTexts();

                        var inp = document.getElementById('cmd');

                        var btn = document.getElementById('btn_send');

                        if(btn) btn.addEventListener('click', function(){ try{ send(); }catch(_){ } });

                        if(inp) inp.addEventListener('keydown', function(e){ if(e && (e.key==='Enter' || e.keyCode===13)){ e.preventDefault(); try{ send(); }catch(_){ } } });

                    }catch(_){ }

                })();

                // Fullscreen must be triggered by a user gesture — do not call on load.

)js");

    out.print(F("</script></body></html>"));

}



void streamSharedBufferViewerPage(Print& out) {

    out.print(R"HTML(<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>📊 Buffer Viewer - Node32-Hub</title><style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(135deg,#0a192f 0%,#1a365d 100%);color:#e0e7ff;min-height:100vh;padding:16px}.container{max-width:1600px;margin:0 auto;background:#0f172a;border-radius:12px;box-shadow:0 20px 60px rgba(0,0,0,0.5);overflow:hidden}.header{background:linear-gradient(90deg,#1e40af,#3b82f6);padding:20px 24px;display:flex;align-items:center;justify-content:center;border-bottom:3px solid #60a5fa}.header h1{font-size:24px;font-weight:700;color:#fff;display:flex;align-items:center;gap:12px}.header h1 .badge{background:#10b981;font-size:11px;padding:4px 8px;border-radius:4px;font-weight:600}.toolbar{padding:16px 24px;background:#1e293b;border-bottom:2px solid #334155;display:flex;flex-wrap:wrap;gap:12px;align-items:center}.toolbar input,.toolbar select{padding:8px 12px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px;min-width:200px}.toolbar button{padding:8px 16px;border-radius:6px;border:none;cursor:pointer;font-size:14px;font-weight:600;transition:all .2s;display:flex;align-items:center;gap:6px}.btn-primary{background:#2563eb;color:#fff}.btn-primary:hover{background:#1d4ed8}.btn-success{background:#059669;color:#fff}.btn-success:hover{background:#047857}.btn-secondary{background:#475569;color:#fff}.btn-secondary:hover{background:#334155}.stats{padding:12px 24px;background:#1e3a8a;display:flex;gap:24px;font-size:13px;border-bottom:1px solid #3730a3;flex-wrap:wrap}.stats span{display:flex;align-items:center;gap:6px}.stats strong{color:#60a5fa;font-weight:600}.viewer{padding:0;min-height:calc(100vh - 300px)}.options{padding:12px 24px;background:#1e293b;border-bottom:1px solid #334155;display:flex;gap:16px;flex-wrap:wrap}.options label{display:flex;align-items:center;gap:8px;font-size:14px;cursor:pointer}.options input[type=checkbox]{width:16px;height:16px;cursor:pointer}#buffer{padding:16px 24px;font-family:'Consolas','Monaco','Courier New',monospace;font-size:13px;line-height:1.5;white-space:pre;overflow:auto;max-height:calc(100vh - 350px);background:#020617;color:#e0e7ff;tab-size:4}.highlight{background:#fbbf24;color:#000;padding:2px 4px;border-radius:2px}.status{padding:8px 24px;background:#0f172a;color:#94a3b8;font-size:12px;text-align:center;border-top:1px solid #1e293b}.empty-state{padding:60px 24px;text-align:center;color:#64748b}.empty-state .icon{font-size:48px;margin-bottom:16px}@media print{body{background:#fff;color:#000}.container{box-shadow:none;border:none}.header,.toolbar,.stats,.options,.status{display:none}#buffer{color:#000;background:#fff;max-height:none}}.bottom-nav{display:none!important}</style><script>window._localPagesNeverExpire=true;</script><script src='/auto.js'></script></head><body><div class='container'><div class='header'><h1 id='buffer_title'>📊 Buffer Viewer <span class='badge' id='buffer_badge'>LIVE</span></h1></div><div class='toolbar'><input type='text' id='search' placeholder='🔍 Search text... (regex supported)'><button id='btn-search' class='btn-primary'>🔎 Search</button><button id='btn-clear-search' class='btn-secondary'>✕ Clear</button><select id='limit'><option value='50'>50 lines</option><option value='100' selected>100 lines</option><option value='200'>200 lines</option><option value='500'>500 lines</option><option value='1000'>1000 lines</option></select><button id='btn-refresh' class='btn-success'>🔄 Refresh</button><button id='btn-export-txt' class='btn-primary'>💾 .TXT</button><button id='btn-copy' class='btn-secondary'>📋 Copy</button></div><div class='stats'><span><strong id='label-lines'>Lines:</strong> <span id='stat-lines'>0</span></span><span><strong id='label-bytes'>Bytes:</strong> <span id='stat-bytes'>0</span></span><span><strong id='label-updated'>Last update:</strong> <span id='stat-time'>--</span></span><span><strong id='label-status'>Status:</strong> <span id='stat-status' style='color:#10b981'>●</span></span></div><div class='options'><label><input type='checkbox' id='opt-auto' checked><span id='label-auto'>Auto-refresh (5s)</span></label><label><input type='checkbox' id='opt-scroll' checked><span id='label-scroll'>Auto-scroll</span></label><label><input type='checkbox' id='opt-regex'><span id='label-regex'>Regex Search</span></label><label><input type='checkbox' id='opt-case'><span id='label-case'>Case-sensitive</span></label></div><div class='viewer'><pre id='buffer'><div class='empty-state'><div class='icon'>📭</div><div id='buffer_empty'>Loading data...</div></div></pre></div><div class='status' id='buffer_footer'>Advanced viewer 🔄 Local processing 📡 Data via WiFi</div></div><script>let bufferData='',searchTerm='',isRegex=false,isCaseSensitive=false,autoRefresh=true,autoScroll=true,refreshInterval=null;const els={buffer:document.getElementById('buffer'),search:document.getElementById('search'),limit:document.getElementById('limit'),statLines:document.getElementById('stat-lines'),statBytes:document.getElementById('stat-bytes'),statTime:document.getElementById('stat-time'),statStatus:document.getElementById('stat-status'),optAuto:document.getElementById('opt-auto'),optScroll:document.getElementById('opt-scroll'),optRegex:document.getElementById('opt-regex'),optCase:document.getElementById('opt-case')};function langCode(){try{return(localStorage.getItem('ui_lang_code')||document.documentElement.lang||'pt').toLowerCase();}catch(_){return'en';}}function isEn(){return langCode().indexOf('en')===0;}function t(k){const d={pt:{title:'📊 Buffer Viewer',badge:'LIVE',search:'🔍 Search text... (regex supported)',searchBtn:'🔍 Search',clearBtn:'✖ Clear',refreshBtn:'🔄 Refresh',exportBtn:'💾 .TXT',copyBtn:'📋 Copy',lines:'Lines:',bytes:'Bytes:',updated:'Last update:',status:'Status:',lineSuffix:' lines',auto:'Auto-refresh (5s)',scroll:'Auto-scroll',regex:'Regex Search',caseSensitive:'Case-sensitive',loading:'Loading data...',empty:'No data available',loadError:'Error loading data',httpError:'Error ',copyOk:'✅ Copied to clipboard!',copyError:'❌ Copy error: ',footer:'Advanced viewer 🔄 Local processing 📡 Data via WiFi',locale:'en-GB'},en:{title:'📊 Buffer Viewer',badge:'LIVE',search:'🔍 Search text... (regex supported)',searchBtn:'🔎 Search',clearBtn:'✕ Clear',refreshBtn:'🔄 Refresh',exportBtn:'💾 .TXT',copyBtn:'📋 Copy',lines:'Lines:',bytes:'Bytes:',updated:'Last update:',status:'Status:',lineSuffix:' lines',auto:'Auto-refresh (5s)',scroll:'Auto-scroll',regex:'Regex Search',caseSensitive:'Case-sensitive',loading:'Loading data...',empty:'No data available',loadError:'Error loading data',httpError:'Error ',copyOk:'✅ Copied to clipboard!',copyError:'❌ Copy error: ',footer:'Advanced viewer • Local processing • Data via WiFi',locale:'en-GB'}};return (isEn()?d.en:d.pt)[k];}function applyTexts(){document.documentElement.lang='en';document.title='📊 Buffer Viewer - Node32-Hub';document.getElementById('buffer_title').innerHTML=t('title')+" <span class='badge' id='buffer_badge'>"+t('badge')+"</span>";els.search.placeholder=t('search');document.getElementById('btn-search').textContent=t('searchBtn');document.getElementById('btn-clear-search').textContent=t('clearBtn');document.getElementById('btn-refresh').textContent=t('refreshBtn');document.getElementById('btn-export-txt').textContent=t('exportBtn');document.getElementById('btn-copy').textContent=t('copyBtn');document.getElementById('label-lines').textContent=t('lines');document.getElementById('label-bytes').textContent=t('bytes');document.getElementById('label-updated').textContent=t('updated');document.getElementById('label-status').textContent=t('status');document.getElementById('label-auto').textContent=t('auto');document.getElementById('label-scroll').textContent=t('scroll');document.getElementById('label-regex').textContent=t('regex');document.getElementById('label-case').textContent=t('caseSensitive');document.getElementById('buffer_footer').textContent=t('footer');Array.from(els.limit.options).forEach(function(option){option.textContent=option.value+t('lineSuffix');});if(!bufferData){renderEmpty('📭',t('loading'));}}function escapeHtml(s){return(s||'').replace(/[&<>"']/g,function(c){return({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c];});}function updateStats(){const lines=bufferData?((bufferData.match(/\n/g)||[]).length+1):0;els.statLines.textContent=lines;els.statBytes.textContent=new Blob([bufferData||'']).size;els.statTime.textContent=new Date().toLocaleTimeString(t('locale'));}function highlightMatches(text){if(!searchTerm)return escapeHtml(text);try{let re;if(isRegex){re=new RegExp(searchTerm,isCaseSensitive?'g':'gi');}else{const esc=searchTerm.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');re=new RegExp(esc,isCaseSensitive?'g':'gi');}return escapeHtml(text).replace(re,function(m){return "<span class='highlight'>"+m+"</span>";});}catch(_){return escapeHtml(text);}}function renderEmpty(icon,text){els.buffer.innerHTML="<div class='empty-state'><div class='icon'>"+icon+"</div><div>"+escapeHtml(text)+"</div></div>";}function renderBuffer(){if(!bufferData){renderEmpty('📭',t('empty'));updateStats();return;}els.buffer.innerHTML=highlightMatches(bufferData);if(autoScroll)els.buffer.scrollTop=els.buffer.scrollHeight;updateStats();}function loadBuffer(){const limit=parseInt(els.limit.value)||100;els.statStatus.style.color='#f59e0b';els.statStatus.textContent='⟳';fetch('/api/received-text?limit='+limit+'&t='+Date.now(),{cache:'no-store'}).then(function(r){return r.ok?r.text():Promise.reject(t('httpError')+r.status);}).then(function(txt){bufferData=(txt||'').replace(/\n{2,}/g,'\n').trim();renderBuffer();els.statStatus.style.color='#10b981';els.statStatus.textContent='●';}).catch(function(e){console.error('Load error:',e);els.statStatus.style.color='#dc2626';els.statStatus.textContent='✕';renderEmpty('⚠️',t('loadError'));});}function doSearch(){searchTerm=els.search.value.trim();isRegex=els.optRegex.checked;isCaseSensitive=els.optCase.checked;renderBuffer();}function getTimestamp(){const d=new Date(),p=function(n){return(n<10?'0':'')+n;};return d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate())+'_'+p(d.getHours())+p(d.getMinutes())+p(d.getSeconds());}function downloadFile(data,filename,type){const blob=new Blob([data],{type:type}),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=filename;a.click();URL.revokeObjectURL(url);}document.getElementById('btn-search').onclick=doSearch;document.getElementById('btn-clear-search').onclick=function(){els.search.value='';searchTerm='';renderBuffer();};els.search.onkeypress=function(e){if(e.key==='Enter')doSearch();};document.getElementById('btn-refresh').onclick=loadBuffer;document.getElementById('btn-export-txt').onclick=function(){downloadFile(bufferData,'buffer_'+getTimestamp()+'.txt','text/plain');};document.getElementById('btn-copy').onclick=function(){var text=bufferData||'';if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(text).then(function(){customAlert(t('copyOk'));}).catch(function(){_bvCopyFallback(text);});}else{_bvCopyFallback(text);}function _bvCopyFallback(txt){var ta=document.createElement('textarea');ta.value=txt;ta.style.cssText='position:fixed;top:0;left:0;opacity:0';document.body.appendChild(ta);ta.focus();ta.select();try{document.execCommand('copy');customAlert(t('copyOk'));}catch(e){customAlert(t('copyError')+e);}document.body.removeChild(ta);}};els.optAuto.onchange=function(){autoRefresh=els.optAuto.checked;if(autoRefresh){refreshInterval=setInterval(loadBuffer,5000);}else{clearInterval(refreshInterval);}};els.optScroll.onchange=function(){autoScroll=els.optScroll.checked;};els.limit.onchange=loadBuffer;applyTexts();loadBuffer();refreshInterval=setInterval(loadBuffer,5000);</script></body></html>)HTML");

}



void streamSharedSerialMonitorPage(Print& out) {

    // Serial Monitor popup page — full-screen debug terminal with module tag filtering.

    out.print(R"HTML(<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Serial Monitor</title><script>window._localPagesNeverExpire=true;</script><script src='/auto.js'></script><style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:monospace;background:#020617;color:#b5bbc9;display:flex;flex-direction:column;height:100vh;overflow:hidden}#toolbar{display:flex;flex-wrap:wrap;gap:5px;padding:6px 10px;background:#060d28;border-bottom:2px solid #1e66c7;align-items:center;flex-shrink:0}#srch-wrap{position:relative;display:inline-flex;align-items:center}#srch-wrap input{background:#0b3069;border:1px solid #1e66c7;color:#e0e7ff;padding:4px 22px 4px 7px;border-radius:5px;font-size:12px;width:155px}#srch-x{position:absolute;right:4px;background:none;border:none;color:#64748b;font-size:11px;cursor:pointer;padding:0 2px;line-height:1}#srch-x:hover{color:#e0e7ff}#toolbar select{background:#0b3069;border:1px solid #1e66c7;color:#e0e7ff;padding:4px 5px;border-radius:5px;font-size:12px;max-width:160px}.tb-btn{background:#1e3a8a;border:1px solid #1e66c7;color:#e0e7ff;padding:4px 8px;border-radius:5px;font-size:12px;cursor:pointer;white-space:nowrap}.tb-btn:hover{background:#2563eb;border-color:#3b82f6}.tb-pause{background:#7c3aed;border-color:#a78bfa}.tb-pause:hover{background:#6d28d9}.tb-paused{background:#b91c1c;border-color:#f87171;animation:blk .8s step-end infinite}@keyframes blk{50%{opacity:.4}}#optbar{display:flex;gap:12px;padding:4px 10px;background:#060d28;border-bottom:1px solid #1e66c7;font-size:12px;align-items:center;flex-shrink:0;flex-wrap:wrap}#optbar label{display:flex;align-items:center;gap:4px;cursor:pointer;color:#94a3b8}#optbar input[type=checkbox]{accent-color:#3b82f6;width:13px;height:13px}#statbar{display:flex;gap:12px;padding:3px 10px;background:#060d28;border-bottom:1px solid #1e3a5f;font-size:11px;color:#64748b;flex-shrink:0;flex-wrap:wrap;align-items:center}#statbar b{color:#60a5fa}#buf{flex:1;overflow-y:auto;padding:5px 8px;font-size:12px;line-height:1.55;background:#020617}#buf::-webkit-scrollbar{width:7px}#buf::-webkit-scrollbar-track{background:#060d28}#buf::-webkit-scrollbar-thumb{background:#1e66c7;border-radius:4px}#buf::-webkit-scrollbar-thumb:hover{background:#3b82f6}.msg{padding:2px 4px;border-bottom:1px solid #0a1628;border-left:3px solid transparent;word-break:break-all}.msg.nw{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.msg.wr{white-space:pre-wrap}.msg:hover{background:#0a1628;border-left-width:4px}.ts{color:#3b82f6;margin-right:5px;user-select:none;font-weight:bold}.lt{font-weight:bold;margin-right:4px}.mtag{font-weight:bold;margin-right:4px;cursor:pointer;text-decoration:underline dotted;text-decoration-thickness:1px}.mtag:hover{opacity:.75}.msg-txt{color:#c0cce0}mark{background:#fbbf24;color:#000;padding:0 2px;border-radius:2px;font-style:normal}.empty-state{text-align:center;padding:40px;color:#475569;font-size:14px}#cbar{display:flex;gap:6px;padding:6px 10px;background:#060d28;border-top:2px solid #1e66c7;flex-shrink:0;align-items:center}#cbar select{background:#0b3069;border:1px solid #1e66c7;color:#e0e7ff;padding:5px 6px;border-radius:5px;font-size:12px}#cbar input{flex:1;background:#0b3069;border:1px solid #1e66c7;color:#e0e7ff;padding:5px 9px;border-radius:5px;font-size:12px}#cbar input::placeholder{color:#64748b}#cbar button{background:#1e66c7;border:none;color:#fff;padding:5px 13px;border-radius:5px;font-size:12px;cursor:pointer;white-space:nowrap;font-weight:bold}#cbar button:hover{background:#3b82f6}.bottom-nav{display:none!important}</style></head><body><div id='toolbar'><div id='srch-wrap'><input id='srch' type='text' placeholder='Filter...' oninput='renderMsgs()'><button id='srch-x' onclick='clearSearch()' title='Clear search'>&#215;</button></div><select id='ftype' onchange='renderMsgs()'><option value=''>All types</option><option value='INFO'>INFO</option><option value='WARN'>WARN</option><option value='ERROR'>ERROR</option><option value='DEBUG'>DEBUG</option><option value='CRITICAL_EVENT'>CRITICAL_EVENT</option><option value='COMMAND_RESPONSE'>COMMAND_RESPONSE</option><option value='TELNET_EVENT'>TELNET_EVENT</option></select><select id='ftag' onchange='renderMsgs()'><option value=''>All modules</option></select><button id='btn-pause' class='tb-btn tb-pause' onclick='togglePause()'>⏸ Pause</button><button id='btn-load' class='tb-btn' onclick='forceLoad()'>🔄 Refresh</button><button id='btn-copy-all' class='tb-btn' onclick='copyAll()'>📋 Copy</button><button id='btn-export' class='tb-btn' onclick='exportTxt()'>⬇ Export</button><button id='btn-clear' class='tb-btn' onclick='clearFilter()'>✕ Clear</button></div><div id='optbar'><label><input type='checkbox' id='optAuto' checked onchange='toggleAR()'><span>Auto 3s</span></label><label><input type='checkbox' id='optScroll' checked><span>Scroll</span></label><label><input type='checkbox' id='optWrap' checked onchange='renderMsgs()'><span>Wrap</span></label><label><input type='checkbox' id='optRegex' onchange='renderMsgs()'><span>Regex</span></label></div><div id='statbar'><span><b id='sMsgs'>0</b>/<b id='sTotal'>0</b> msgs</span><span>Heap: <b id='sHeap'>-</b> KB</span><span>Updated: <b id='sTime'>-</b></span><span>State: <b id='sStatus' style='color:#f59e0b'>&#9679;</b></span><b id='sPaused' style='display:none;color:#f87171'>&#9646;&#9646; PAUSED</b></div><div id='buf'><div class='empty-state'>Loading messages...</div></div><div id='cbar'><select id='ctype'><option value='sys'>System</option>)HTML");

#if ENABLE_RS232==1

    out.print(R"HTML(<option value='rs232'>RS232</option>)HTML");

#endif

#if ENABLE_MODBUS==1

    out.print(R"HTML(<option value='modbus'>Modbus</option>)HTML");

#endif

    out.print(R"HTML(</select><input id='cmd' type='text' placeholder='Send command...' onkeypress='if(event.key=="Enter")sendCmd()'><button onclick='sendCmd()'>&#x1F4E4; Send</button></div><script>

var allMsgs=[],arInterval=null,_msgCount=0,isLoading=false,isPaused=false;

var typeColors={INFO:'#10b981',WARN:'#f59e0b',ERROR:'#ef4444',DEBUG:'#71717a',CRITICAL_EVENT:'#f97316',COMMAND_RESPONSE:'#60a5fa',TELNET_EVENT:'#a78bfa'};

var modPal=['#60a5fa','#f97316','#a78bfa','#34d399','#fbbf24','#fb7185','#2dd4bf','#e879f9','#38bdf8','#f472b6','#4ade80','#c084fc','#fdba74','#67e8f9'];

var modMap={};var knownTags={};



function modColor(tag){if(!modMap[tag]){var h=0;for(var i=0;i<tag.length;i++)h=((h<<5)-h+tag.charCodeAt(i))|0;modMap[tag]=modPal[Math.abs(h)%modPal.length];}return modMap[tag];}

function gc(t){return typeColors[t]||'#e0e7ff';}

function eh(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}

function extractTag(text){var m=text.match(/^\[([A-Za-z][A-Za-z0-9 _]{0,24})\]/);return m?m[1]:null;}

function fmtTs(n){

    var v=Number(n);

    if(!isFinite(v)||v<0)return '--:--:--';

    var d=new Date(v*1000),p=function(x){return x<10?'0'+x:x;};

    return p(d.getUTCHours())+':'+p(d.getUTCMinutes())+':'+p(d.getUTCSeconds());

}

function msgTs(m){

    if(!m)return 0;

    if(m.ts!==undefined&&m.ts!==null)return m.ts;

    if(m.timestamp!==undefined&&m.timestamp!==null)return m.timestamp;

    return 0;

}

function fmtNow(){return new Date().toLocaleTimeString();}

function buildTagFilter(){

  var sel=document.getElementById('ftag');var cur=sel.value;

  sel.innerHTML='<option value="">All modules</option>';

  var tags=Object.keys(knownTags).sort();

  for(var i=0;i<tags.length;i++){var t=tags[i];var o=document.createElement('option');o.value=t;o.textContent='['+t+'] ('+knownTags[t]+')';o.style.color=modColor(t);sel.appendChild(o);}

  sel.value=cur;

}

function hlText(esc,term,isRx){

  if(!term)return esc;

  try{var re=isRx?new RegExp(term,'gi'):new RegExp(term.replace(/[.*+?^${}()|[\]\\]/g,'\\$&'),'gi');

    return esc.replace(re,function(m){return '<mark>'+m+'</mark>';});}catch(_){return esc;}

}

function renderMsgs(){

  var term=document.getElementById('srch').value;

  var tLow=term.toLowerCase();

  var ft=document.getElementById('ftype').value;

  var ftag=document.getElementById('ftag').value;

  var isRx=document.getElementById('optRegex').checked;

  var wrap=document.getElementById('optWrap').checked;

  var wCls=wrap?'wr':'nw';

  var filtered=allMsgs.filter(function(m){

    if(ft&&m.type!==ft)return false;

    if(ftag&&extractTag(m.text)!==ftag)return false;

    if(term){try{if(isRx){if(!new RegExp(term,'i').test(m.text))return false;}

      else{if(m.text.toLowerCase().indexOf(tLow)===-1)return false;}}

      catch(_){if(m.text.toLowerCase().indexOf(tLow)===-1)return false;}}

    return true;

  });

  document.getElementById('sMsgs').textContent=filtered.length;

  document.getElementById('sTotal').textContent=allMsgs.length;

  if(!filtered.length){document.getElementById('buf').innerHTML="<div class='empty-state'>No messages found</div>";return;}

  var html=filtered.map(function(m){

    var tc=gc(m.type);var txt=m.text;var tagHtml='';

    var tag=extractTag(txt);

    if(tag){var mc=modColor(tag);

      tagHtml="<span class='mtag' style='color:"+mc+"' onclick='filterTag(\""+eh(tag)+"\")' title='Filter by ["+eh(tag)+"]'>["+eh(tag)+"]</span>";

      txt=txt.substring(tag.length+2);}

    return "<div class='msg "+wCls+"' style='border-left-color:"+tc+"'><span class='ts'>"+fmtTs(msgTs(m))+"</span><span class='lt' style='color:"+tc+"'>["+m.type+"]</span>"+tagHtml+"<span class='msg-txt'>"+hlText(eh(txt),term,isRx)+"</span></div>";

  }).join('');

  document.getElementById('buf').innerHTML=html;

  if(document.getElementById('optScroll').checked){var b=document.getElementById('buf');b.scrollTop=b.scrollHeight;}

}

function filterTag(tag){document.getElementById('ftag').value=tag;renderMsgs();}

function load(){

  if(isLoading||isPaused)return;

  isLoading=true;document.getElementById('sStatus').style.color='#f59e0b';

  var fl=_msgCount===0?500:200;

  fetch('/api/serial-monitor?limit_msgs='+fl+'&since='+_msgCount+'&t='+Date.now(),{cache:'no-store'})

    .then(function(r){return r.ok?r.json():Promise.reject('HTTP '+r.status);})

    .then(function(d){

      var nw=d.messages||[];var tot=d.total||0;

      if(tot<_msgCount){allMsgs=nw;knownTags={};}

      else{allMsgs=allMsgs.concat(nw);if(allMsgs.length>500)allMsgs=allMsgs.slice(-500);}

      nw.forEach(function(m){var t=extractTag(m.text);if(t)knownTags[t]=(knownTags[t]||0)+1;});

      buildTagFilter();_msgCount=tot;

      document.getElementById('sHeap').textContent=Math.round((d.heap_free||0)/1024);

      document.getElementById('sTime').textContent=fmtNow();

      document.getElementById('sStatus').style.color='#10b981';

      _retry429=0;isLoading=false;renderMsgs();

    }).catch(function(e){

      isLoading=false;

      if(String(e).indexOf('429')!==-1){document.getElementById('sStatus').style.color='#f59e0b';_retry429++;if(_retry429<=2){scheduleLoad();return;}}

      _retry429=0;document.getElementById('sStatus').style.color='#ef4444';

      document.getElementById('buf').innerHTML="<div class='empty-state'>\u26a0 Error: "+eh(String(e))+"</div>";

    });

}

function forceLoad(){if(!isLoading)load();}

function toggleAR(){clearInterval(arInterval);if(!isPaused&&document.getElementById('optAuto').checked)arInterval=setInterval(load,3000);}

function togglePause(){

  isPaused=!isPaused;

  var b=document.getElementById('btn-pause');

  b.textContent=isPaused?'▶ Resume':'⏸ Pause';

  b.className=isPaused?'tb-btn tb-paused':'tb-btn tb-pause';

  document.getElementById('sPaused').style.display=isPaused?'':'none';

  if(isPaused){clearInterval(arInterval);}

  else{if(document.getElementById('optAuto').checked)arInterval=setInterval(load,3000);load();}

}

function clearSearch(){document.getElementById('srch').value='';renderMsgs();}

function clearFilter(){document.getElementById('srch').value='';document.getElementById('ftype').value='';document.getElementById('ftag').value='';renderMsgs();}

function copyAll(){var lines=allMsgs.map(function(m){return fmtTs(msgTs(m))+' ['+m.type+'] '+m.text;}).join('\n');if(navigator.clipboard&&navigator.clipboard.writeText){navigator.clipboard.writeText(lines).then(function(){customAlert('Copied!');}).catch(function(){_copyFallback(lines);});}else{_copyFallback(lines);}function _copyFallback(text){var ta=document.createElement('textarea');ta.value=text;ta.style.cssText='position:fixed;top:0;left:0;opacity:0';document.body.appendChild(ta);ta.focus();ta.select();try{document.execCommand('copy');customAlert('Copied!');}catch(e){customAlert('Error: '+e);}document.body.removeChild(ta);}}

function exportTxt(){var lines=allMsgs.map(function(m){return fmtTs(msgTs(m))+' ['+m.type+'] '+m.text;}).join('\n');var bl=new Blob([lines],{type:'text/plain'});var url=URL.createObjectURL(bl);var a=document.createElement('a');a.href=url;a.download='serial_monitor_'+Date.now()+'.txt';a.click();URL.revokeObjectURL(url);}

var _loadTimer=0,_retry429=0;function scheduleLoad(){if(_loadTimer)clearTimeout(_loadTimer);_loadTimer=setTimeout(function(){_loadTimer=0;if(!isLoading&&!isPaused)load();},1200);}function sendCmd(){var cmd=document.getElementById('cmd').value.trim();if(!cmd)return;var ctype=document.getElementById('ctype').value;var url=ctype==='rs232'?'/api/rs232-command':ctype==='modbus'?'/api/modbus-command':'/api/system-command';fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command:cmd})}).then(function(r){return r.json().catch(function(){return {success:false,response:'HTTP '+r.status};});}).then(function(j){var resp=(j&&j.response)?j.response:((j&&j.success)?'Sent':'Failed');if(!(j&&j.streamed)){var ts=Math.floor((Date.now()-new Date().getTimezoneOffset()*60000)/1000);allMsgs.push({ts:ts,type:'COMMAND_RESPONSE',text:'\u279c '+cmd+' \u2192 '+resp});if(allMsgs.length>500)allMsgs=allMsgs.slice(-500);}document.getElementById('cmd').value='';_retry429=0;renderMsgs();scheduleLoad();}).catch(function(e){customAlert('Error: '+e);scheduleLoad();});}

load();arInterval=setInterval(load,3000);

</script></body></html>)HTML");

}

