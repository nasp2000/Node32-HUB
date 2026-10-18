// Shared helper JS body served as /auto.js (no <script> wrappers)
#pragma once

#if defined(SELECTED_PACK) && SELECTED_PACK != PACK_NONE
  #define AUTO_JS_PACK_BRANDING_ENABLED "true"
  #if defined(SELECTED_PACK) && SELECTED_PACK == PACK_P4_USB_SERVER
    #undef AUTO_JS_PACK_BRANDING_ENABLED
    #define AUTO_JS_PACK_BRANDING_ENABLED "false"
  #endif
  #define AUTO_JS_PACK_BRAND_NAME PACK_BRAND_NAME
  #define AUTO_JS_PACK_BRAND_EMOJI PACK_EMOJI
#else
  #define AUTO_JS_PACK_BRANDING_ENABLED "false"
  #define AUTO_JS_PACK_BRAND_NAME "Node32-HUB"
  #define AUTO_JS_PACK_BRAND_EMOJI "🤖"
#endif

static const char AUTO_JS_BODY[] PROGMEM = R"rawliteral(

            try{ if(typeof console !== 'undefined' && console && typeof console.log === 'function') console.log('AUTO_JS_BODY: loaded'); }catch(e){}

            (function(){
                var PACK_BRANDING_ENABLED = )rawliteral" AUTO_JS_PACK_BRANDING_ENABLED R"rawliteral(;
                if(!PACK_BRANDING_ENABLED) return;
                var PACK_BRAND_NAME = ')rawliteral" AUTO_JS_PACK_BRAND_NAME R"rawliteral(';
                var PACK_BRAND_EMOJI = ')rawliteral" AUTO_JS_PACK_BRAND_EMOJI R"rawliteral(';
                function replaceBrandingInNode(node){
                    if(!node || typeof node.nodeValue !== 'string') return;
                    // Skip GitHub footer link so the repo name stays as "Node32-HUB"
                    if(node.parentNode && node.parentNode.tagName === 'A' && node.parentNode.href && node.parentNode.href.indexOf('github.com') >= 0) return;
                    var nextValue = node.nodeValue;
                    if(nextValue.indexOf('Node32-HUB') >= 0 || nextValue.indexOf('Node32-Hub') >= 0){
                        nextValue = nextValue.replace(/Node32-HUB/g, PACK_BRAND_NAME).replace(/Node32-Hub/g, PACK_BRAND_NAME);
                    }
                    if(nextValue.indexOf('🤖') >= 0){
                        nextValue = nextValue.replace(/🤖/g, PACK_BRAND_EMOJI);
                    }
                    node.nodeValue = nextValue;
                }

                function hideElementById(id){
                    var el = document.getElementById(id);
                    if(el) el.style.display = 'none';
                }

                function applyRadioPackTheme(){
                    if(PACK_BRAND_NAME !== 'Harmonica Box') return;
                    // Radio-ui player has its own neon theme — never overlay the
                    // blue home-page pack theme on it (its button !important rules
                    // would turn the player's neon buttons blue).
                    if(window._noBottomNav) return;
                    if(document.getElementById('pack-radio-theme')) return;
                    // NOTE: button border-color is per-button (inline style on the
                    // home page module buttons, e.g. white for modules, green for
                    // the dashboard). The pack theme must NEVER force a border color
                    // here — an !important border-color would override those inline
                    // choices and is a recurring source of regressions.
                    var st = document.createElement('style');
                    st.id = 'pack-radio-theme';
                    st.textContent = "body{background:radial-gradient(circle at 10% 10%,#113f67 0%,#071325 45%,#030711 100%)!important;color:#e0f2fe!important;}"+
                        ".home-hero{background:linear-gradient(125deg,#0ea5e9 0%,#2563eb 42%,#0f172a 100%)!important;box-shadow:0 8px 26px rgba(14,165,233,.35)!important;}"+
                        ".home-section h2{color:#7dd3fc!important;}"+
                        ".btn,button,a.btn{background:linear-gradient(125deg,#0284c7,#1d4ed8)!important;color:#ecfeff!important;box-shadow:0 4px 10px rgba(14,165,233,.25)!important;}"+
                        ".btn:hover,button:hover,a.btn:hover{filter:brightness(1.08)!important;}"+
                        ".footer-container .footer>div{background:linear-gradient(125deg,#0f766e 0%,#0369a1 52%,#1d4ed8 100%)!important;}"+
                        "input,select,textarea{border-color:#38bdf8!important;background:#041426!important;color:#e0f2fe!important;}"+
                        ".table,.table th,.table td,#files-table th,#files-table td{border-color:rgba(56,189,248,.3)!important;}";
                    (document.head || document.documentElement).appendChild(st);
                }

                function applyRamboxPackUiRules(){
                    try{
                        if(document.title){
                            document.title = document.title.replace(/Node32-HUB/g, PACK_BRAND_NAME).replace(/Node32-Hub/g, PACK_BRAND_NAME);
                            document.title = document.title.replace(/🤖/g, PACK_BRAND_EMOJI);
                        }

                        var walker = document.createTreeWalker(document.body || document.documentElement, NodeFilter.SHOW_TEXT, null);
                        var n = walker.nextNode();
                        while(n){
                            replaceBrandingInNode(n);
                            n = walker.nextNode();
                        }

                        var nameInput = document.getElementById('system_name');
                        if(nameInput){
                            if(!nameInput.value || nameInput.value === 'Node32-HUB' || nameInput.value === 'Node32-Hub') nameInput.value = PACK_BRAND_NAME;
                            if(!nameInput.placeholder || nameInput.placeholder === 'Node32-HUB' || nameInput.placeholder === 'Node32-Hub') nameInput.placeholder = PACK_BRAND_NAME;
                        }

                        applyRadioPackTheme();
                    }catch(e){}
                }

                document.addEventListener('DOMContentLoaded', applyRamboxPackUiRules);
                window.addEventListener('load', applyRamboxPackUiRules);
            })();

            // Inactivity timeout: 10 minutes (600000ms)
            // Kicks off clients after 10 minutes of no mouse/keyboard/touch activity.
            // Skipped on local processing pages (radio-ui, charts, etc.) when the
            // "Local processing page sessions never expire" option is ON — the
            // server injects window._localPagesNeverExpire=true in that case.
            (function(){
                if(window._localPagesNeverExpire) return;
                var INACTIVITY_TIMEOUT_MS = 600000; // 10 minutes
                var inactivityTimer = null;
                var isActive = true;
                
                function resetInactivityTimer(){
                    if(!isActive) return;
                    if(inactivityTimer) clearTimeout(inactivityTimer);
                    inactivityTimer = setTimeout(function(){
                        try{
                            console.log('[INACTIVITY] 10 minutes elapsed - redirecting to home');
                            // Clear any local storage/session if needed
                            if(typeof sessionStorage !== 'undefined') sessionStorage.clear();
                            // Redirect to home page
                            window.location.href = '/';
                        }catch(e){
                            console.error('[INACTIVITY] Redirect error:', e);
                        }
                    }, INACTIVITY_TIMEOUT_MS);
                }
                
                // Activity events to monitor
                var activityEvents = ['mousedown', 'mousemove', 'keypress', 'keydown', 'scroll', 'touchstart', 'click'];
                
                // Attach listeners
                activityEvents.forEach(function(eventName){
                    document.addEventListener(eventName, resetInactivityTimer, { passive: true });
                });
                
                // Start timer
                resetInactivityTimer();
                
                console.log('[INACTIVITY] Timeout monitor active (10 minutes)');
            })();

            // Removed the legacy 'if(typeof list...' block because it had no closing brace and caused a browser SyntaxError (missing )/}).
            // The 'list' variable is not defined globally here; list management is handled later by specific functions.
// Fill fields when they exist, supporting masked values (ignores '********').
// Shared RS232 template loader (lightweight, self-contained)
// Ensure the formatters exist early (before loading templates), without redefining them if another block already created them.
if (typeof formatTemplateLabel === 'undefined') {
    function formatTemplateLabel(t){
        try{
            if(!t || typeof t !== 'object') return '';
            // Resolve baud preferring direct numeric value, then global list by index, then defaults
            function resolveTemplateBaud(tmp){
                try{
                    if(!tmp) return '';
                    if(tmp.baudrate) return String(tmp.baudrate);
                    if(window && window._rs232_baudlist && tmp.baudrate_index!==undefined){
                        var v = window._rs232_baudlist[tmp.baudrate_index]; if(v) return String(v);
                    }
                    var defaultList = [1200,2400,4800,9600,19200,38400,57600,115200];
                    var idx = Number(tmp.baudrate_index);
                    if(!isNaN(idx) && idx>=0 && idx < defaultList.length) return String(defaultList[idx]);
                    if(!isNaN(idx)) return String(idx);
                    return '';
                }catch(e){ return ''; } // Added semicolon so the statement terminates correctly.
            }
            var baud = resolveTemplateBaud(t) || (t.baudrate||'?');
            var dataBits = t.data_bits || t.dataBits || t.bits || 8;
            var pRaw = (t.parity || t.par || '').toString().toLowerCase();
            var pm = { 'none':'N', 'n':'N', 'even':'E', 'e':'E', 'odd':'O', 'o':'O', 'mark':'M', 'space':'S' };
            var parity = pRaw ? (pm[pRaw] || pRaw.charAt(0).toUpperCase()) : 'N';
            var stopBits = t.stop_bits || t.stopBits || t.stop || 1;
            // Compact label: baud-dataBits-parity-stopBits (e.g. 19200-8-N-1)
            return (baud?baud:'?') + '-' + dataBits + '-' + (parity||'N') + '-' + (stopBits||1);
        }catch(e){ return ''; }
    }
}

if (typeof getTemplateBaudLabel === 'undefined') {
    function getTemplateBaudLabel(t){
        try{
            if(!t) return '';
            if(t.baudrate_index !== undefined){
                var idx = Number(t.baudrate_index);
                if(!isNaN(idx) && window && window._rs232_baudlist && window._rs232_baudlist[idx]) return String(window._rs232_baudlist[idx]);
            }
            if(t.baudrate) return String(t.baudrate);
            var defaultList = [1200,2400,4800,9600,19200,38400,57600,115200];
            var idx2 = Number(t.baudrate_index);
            if(!isNaN(idx2) && idx2>=0 && idx2 < defaultList.length) return String(defaultList[idx2]);
            if(!isNaN(idx2)) return String(idx2);
        }catch(e){return ''}
    }
}
    // Build a compact option label used across selects: prefer "<name> - <baud>" when possible
    function buildTemplateOptionLabel(t){
        try{
            var name = (t && (t.name || t.title)) ? (t.name || t.title) : '';
            var baud = '';
            try{ if(typeof getTemplateBaudLabel === 'function') baud = (getTemplateBaudLabel(t) || ''); }catch(e){}
            if(!baud) baud = (t && (t.baudrate || t.baud)) ? (t.baudrate || t.baud) : '';
            if(name && baud) return name + ' - ' + baud;
            if(name) return name;
            if(baud) return String(baud);
            return '(sem nome)';
        }catch(e){ return (t && t.name) ? t.name : '(sem nome)'; }
    }
function loadRs232Templates(){
    if(window._rs232_templates && Array.isArray(window._rs232_templates) && window._rs232_templates.length) return Promise.resolve(window._rs232_templates);
    if(window._rs232_templates_promise) return window._rs232_templates_promise;
    window._rs232_templates_promise = fetch('/api/rs232-templates',{cache:'no-cache'})
    .then(r=>r.json())
    .then(d=>{
        window._rs232_templates = Array.isArray(d.templates)? d.templates : [];
        window._rs232_default = '';
        if(window._rs232_templates && window._rs232_templates.length){ // Only process when templates exist.
            try{ // Single block for default selection and card rendering.
                if(window._rs232_current_template && window._rs232_current_template.length){ // Try the current template first.
                    for(var k=0;k<window._rs232_templates.length;k++){ // Iterate through all templates.
                        if(window._rs232_templates[k] && window._rs232_templates[k].name === window._rs232_current_template){ // Match current name.
                            window._rs232_default = window._rs232_current_template; // Set default.
                            break; // Stop after the first match.
                        }
                    }
                }
                if(!window._rs232_default){ // If still empty, look for the default template name.
                    for(var i=0;i<window._rs232_templates.length;i++){ // Iterate through all templates.
                        var nm = window._rs232_templates[i] && window._rs232_templates[i].name ? window._rs232_templates[i].name : ''; // Template name.
                        if(nm === 'Padrão' || nm.indexOf('Padrão') >= 0){ window._rs232_default = nm; break; } // Found a suitable default.
                    }
                }
                if(!window._rs232_default) window._rs232_default = window._rs232_templates[0].name || ''; // Fall back to the first entry.

                // Optionally render template cards when #templates-list exists in the DOM.
                var tplContainer = null; try{ tplContainer = document.getElementById('templates-list'); }catch(_e){} // Get the container if present.
                if(tplContainer){ // Render only when the container exists.
                    tplContainer.innerHTML = ''; // Clear previous content.
                    (window._rs232_templates || []).forEach(function(t){ // Render each template.
                        var card = document.createElement('div'); card.className = 'template-card'; // Card node.
                        var header = document.createElement('div'); header.className = 'tpl-card-header'; // Header node.
                        var nameEl = document.createElement('div'); nameEl.className = 'tpl-name'; // Name node.
                        var actions = document.createElement('div'); actions.className = 'tpl-actions'; // Actions node.
                        var chk = document.createElement('input'); chk.type='checkbox'; chk.name='template_delete'; chk.value = t.name || ''; chk.title = 'Seleccionar para eliminar'; // Delete checkbox.
                        actions.appendChild(chk); // Append checkbox.
                        var compact = ''; // Compact label.
                        try{ if(typeof formatTemplateLabel === 'function') compact = (formatTemplateLabel(t) || '').trim(); }catch(e){} // Try the primary formatter first.
                        try{ if((!compact || !compact.length) && typeof getTemplateBaudLabel === 'function') compact = (getTemplateBaudLabel(t) || '').trim(); }catch(e){} // Fall back to baud label.
                        var dispName = t && t.name ? t.name : '(sem nome)'; // Display name.
                        try{ // Build the final label.
                            if(compact && compact.length) nameEl.textContent = (typeof buildTemplateOptionLabel === 'function') ? buildTemplateOptionLabel(t) : (dispName + ' - ' + compact); else nameEl.textContent = dispName; // Set visible text.
                        }catch(e){ nameEl.textContent = dispName; } // Simple fallback.
                        header.appendChild(nameEl); header.appendChild(actions); // Assemble header.
                        var desc = document.createElement('div'); desc.className = 'tpl-desc'; desc.style.fontSize='12px'; desc.style.opacity='.85'; // Description node.
                        try{ desc.textContent = (typeof formatTemplateLabel === 'function') ? formatTemplateLabel(t) : ((t.baudrate||'?') + '-' + (t.data_bits||8) + '-' + (t.parity||'none')); }catch(e){ desc.textContent = (t.baudrate||'?') + '-' + (t.data_bits||8) + '-' + (t.parity||'none'); } // Technical summary.
                        card.appendChild(header); card.appendChild(desc); // Assemble card.
                        tplContainer.appendChild(card); // Inject into the DOM.
                    });
                }
            }catch(e){ /* Failed to compute the default or render template cards. */ }
        }

            try{ updateRs232Selects(); }catch(e){}
            return window._rs232_templates;
        }).catch(e=>{ window._rs232_templates = []; window._rs232_default=''; return window._rs232_templates; }).finally(function(){ window._rs232_templates_promise = null; });
    return window._rs232_templates_promise;
}

async function updateRs232Selects(){
    try{
        if(!window._rs232_templates || !Array.isArray(window._rs232_templates) || window._rs232_templates.length === 0){
            if(window._rs232_templates_promise){
                try{ await window._rs232_templates_promise; }catch(_){}}
            else { try{ await loadRs232Templates(); }catch(_){}}
        }
        var list = window._rs232_templates || [];
        var defaultName = window._rs232_default || '';
    function fill(sel){
        // Fill the template <select> with simpler logic to avoid syntax issues in minified or concatenated output.
        if(!sel) return; // No target element.
        var cur = sel.value || ''; // Keep current value before rebuilding.
        sel.innerHTML = ''; // Clear existing options.
        var ph = document.createElement('option'); // Placeholder option.
        ph.value=''; // Empty value.
        ph.textContent = defaultName || '(Nenhum)'; // Placeholder text.
        sel.appendChild(ph); // Append placeholder.
        list.forEach(function(t){ // Render each template.
            try{
                var o=document.createElement('option'); // New option.
                o.value = t && t.name ? t.name : ''; // Value equals template name.
                var lab = ''; // Compact label.
                try{ if(typeof formatTemplateLabel === 'function') lab = formatTemplateLabel(t) || ''; }catch(e){}
                if(!lab){ // Direct baud fallback.
                    lab = t && (t.baudrate || t.baud || t.baud_rate || t.baudRate) ? (t.baudrate || t.baud || t.baud_rate || t.baudRate) : '';
                }
                if(!lab){ // Fallback through the index list.
                    try{ if(window && window._rs232_baudlist && (t.baudrate_index!==undefined || t.baudrateIndex!==undefined)){
                        var idx = (t.baudrate_index!==undefined)?t.baudrate_index:t.baudrateIndex; if(window._rs232_baudlist[idx]) lab = String(window._rs232_baudlist[idx]);
                    } }catch(e){}
                }
                var name = (t && t.name) ? t.name : (t && t.title ? t.title : ''); // Visible name.
                try{ // Use the central label builder when available.
                    o.textContent = (typeof buildTemplateOptionLabel === 'function') ? buildTemplateOptionLabel(t) : ((name && lab) ? (name + ' - ' + lab) : (lab || name || '(sem nome)'));
                    o.title = lab || name || ''; // Tooltip text.
                }catch(e){ o.textContent = name || '(sem nome)'; }
                sel.appendChild(o); // Append option.
            }catch(e){ /* ignore invalid template */ }
        });
        // Restore the previous value when still present.
        if(cur && sel.querySelector('option[value="'+cur+'"]')) sel.value = cur;
        else if(defaultName && sel.querySelector('option[value="'+defaultName+'"]')) sel.value = defaultName; // Otherwise apply the default value.
    }
        var mon = document.getElementById('monitor-template'); if(mon) fill(mon);
        var rows = document.querySelectorAll('#scheduled-commands-container select[name="command_template[]"]'); rows.forEach(function(s){ fill(s); });
        var pulls = document.querySelectorAll('select.command-template, select.tpl-template'); pulls.forEach(function(s){ fill(s); });
        try{
            function afterApply(name){ try{ window._rs232_current_template = name; window._rs232_default = name; }catch(_){} try{ loadRs232Templates(); }catch(_){} }
            if(mon){ mon.addEventListener('change', function(e){ var name = e.target.value || ''; if(!name) return; fetch('/api/rs232-apply',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({template:name})}).then(r=>r.json()).then(d=>{ afterApply(name); }).catch(()=>{}); }); }
            pulls.forEach(function(s){ s.addEventListener('change', function(e){ var name = e.target.value || ''; if(!name) return; fetch('/api/rs232-apply',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({template:name})}).then(r=>r.json()).then(d=>{ afterApply(name); }).catch(()=>{}); }); });
            rows.forEach(function(s){ s.addEventListener('change', function(e){ var name = e.target.value || ''; if(!name) return; fetch('/api/rs232-apply',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({template:name})}).then(r=>r.json()).then(d=>{ afterApply(name); }).catch(()=>{}); }); });
        }catch(e){ }
    }catch(e){ }
}

async function autoFillConfigs(){
 try{
     async function loadSection(section){
         try{
             var r = await fetch('/api/config?section=' + encodeURIComponent(section), {cache:'no-store'});
             if(!r.ok) return {};
             return await r.json();
         }catch(_e){
             return {};
         }
     }
     const wifiSection = await loadSection('wifi');
     const emailSection = await loadSection('email');
     const rs232Section = await loadSection('rs232');
     const systemSection = await loadSection('system');
     const d = Object.assign({}, systemSection, wifiSection, emailSection, rs232Section);
     // Cache the response so config-manager.js can reuse it instead of making a second request.
     window._lastConfigData = {data: d, ts: Date.now()};
     const c = (d&&d.saved_configs)?d.saved_configs:(d||{});
     const wifi = (c.wifi&&typeof c.wifi==='object')?c.wifi:((d.wifi&&typeof d.wifi==='object')?d.wifi:{});
     const email = (c.email&&typeof c.email==='object')?c.email:((d.email&&typeof d.email==='object')?d.email:{});
     const smtp = (c.smtp&&typeof c.smtp==='object')?c.smtp:((d.smtp&&typeof d.smtp==='object')?d.smtp:{});
     const rs232 = (c.rs232&&typeof c.rs232==='object')?c.rs232:((d.rs232&&typeof d.rs232==='object')?d.rs232:{});
     const baudList = Array.isArray(c.rs232_baudrate_list)
         ? c.rs232_baudrate_list
         : (Array.isArray(rs232.baudrate_list)
             ? rs232.baudrate_list
             : [1200,2400,4800,9600,19200,38400,57600,115200]);
     const setVal=(id,val)=>{const el=document.getElementById(id); if(el && val!==undefined && val!==null && val!=='********'){ if(el.tagName==='SELECT'||el.tagName==='INPUT'||el.tagName==='TEXTAREA'){ el.value=val; } } };
     setVal('ssid', wifi.ssid||c.wifi_ssid);
     if(document.getElementById('password') && (wifi.ssid||c.wifi_ssid)){ document.getElementById('password').placeholder='(saved)'; }
     setVal('smtp_server', smtp.server||c.smtp_server);
     setVal('smtp_port', smtp.port||c.smtp_port);
     setVal('smtp_user', smtp.username||c.smtp_user);
     if(document.getElementById('smtp_pass')) document.getElementById('smtp_pass').placeholder='(saved)';
     setVal('email_dest', email.destination||c.email_destination);
     setVal('emulation_mode', c.printer_emulation_mode||c.printerEmulationMode);
     setVal('charset_mode', c.charset_mode||c.charsetMode);
     if(document.getElementById('rs232_enabled')) document.getElementById('rs232_enabled').checked = !!(rs232.enabled||c.rs232_enabled);
     if(baudList.length){
         var baudIdx = (rs232.baudrate_index!==undefined)?rs232.baudrate_index:c.rs232_baudrate_index;
         if(baudIdx!==undefined && baudList[baudIdx]!==undefined) setVal('baudrate', baudList[baudIdx]);
     }
    setVal('data_bits', rs232.data_bits||c.rs232_data_bits);
    setVal('parity', rs232.parity||c.rs232_parity);
    setVal('stop_bits', rs232.stop_bits||c.rs232_stop_bits);
    if(document.getElementById('invert_pins')) document.getElementById('invert_pins').checked=!!(rs232.invert_pins||c.rs232_invert_pins);
    setVal('read_timeout', rs232.read_timeout_ms||c.rs232_read_timeout);
    // Prefer the canonical nested value (email.auto_send); keep legacy fallbacks (auto_send_email/autoSendEmail).
    if(document.getElementById('auto_send_enabled')){
        var __el=document.getElementById('auto_send_enabled'); __el.checked = !!(email.auto_send || c.auto_send_email || c.autoSendEmail);
    }
        setVal('timeout_active', c.autoEmailEnabledTimeout||email.auto_send_enabled_timeout);
        setVal('timeout_inactive', c.autoEmailDisabledTimeout||email.auto_send_disabled_timeout);
        setVal('max_lines', c.current_max_email_lines||email.max_lines);
        setVal('min_email_chars', c.min_email_content_length||email.min_content_length);
        // Do NOT auto-populate recipients here; recipients can be large and should be lazy-loaded
        // by the recipients page using /api/recipients. This avoids pulling the full list on unrelated pages
        // and reduces heap pressure on the firmware when serializing configs.
        // Removed support for legacy scheduled_commands; only slot-based sched_cmd / sched_dt handlers remain.
    return true;
 }catch(e){ return false; }
}
function hasAutoFillConfigTargets(){
    var ids=['ssid','password','smtp_server','smtp_port','smtp_user','smtp_pass','email_dest','emulation_mode','charset_mode','rs232_enabled','baudrate','data_bits','parity','stop_bits','invert_pins','read_timeout','auto_send_enabled','timeout_active','timeout_inactive','max_lines','min_email_chars'];
    for(var i=0;i<ids.length;i++){
        if(document.getElementById(ids[i])) return true;
    }
    return false;
}
document.addEventListener('DOMContentLoaded', function(){ try{ var path=window.location.pathname||''; if(path.indexOf('recipients')===-1 && hasAutoFillConfigTargets()) autoFillConfigs().catch(function(){}); }catch(e){} });

// Lightweight fallback implementations
var optimizedFetch = (function(){
    function noop(){}
    return {
        cache: { cleanup: noop },
        request: function(url, opts, ttl){
            if(!opts) opts = {};
            if(!opts.cache && !opts.method) opts.cache = 'no-store';
            return fetch(url, opts).then(function(resp){
                var ct = resp.headers && resp.headers.get ? resp.headers.get('content-type') : '';
                if(ct && ct.indexOf('application/json')>=0) return resp.json();
                return resp.text();
            });
        }
    };
})();
// --- Commands page specific helpers (moved) ---
// (moved into AUTO_JS_BODY so /auto.js serves them together)
// loadCommandsConfig() removed — the legacy commands page was replaced by slot endpoint management.

// addScheduledCommandWithData() removed — the old interval-command UI is deprecated.

// addScheduledCommand() removed — management now happens through the newer forms/slots.

// removeSelectedCommand() removed — obsolete.

// --- Recipients page helpers (moved here from RECIPIENTS_PAGE) ---
function addRecipientWithData(name, email){
    try{
        var container = document.getElementById('recipients-container'); if (!container) return;
        var row = document.createElement('tr'); row.className = 'recipient-row';
        row.innerHTML = `
            <td style="text-align:center;"><input type="checkbox" class="recipient-select"></td>
            <td><input type="text" name="recipient_name[]" value="${name ? name.replace(/"/g, '&quot;') : ''}" placeholder="Nome" class="form-input"></td>
            <td><input type="email" name="recipient_email[]" value="${email ? email.replace(/"/g, '&quot;') : ''}" placeholder="Email" class="form-input"></td>
        `;
        container.appendChild(row);
    }catch(e){ console.warn('addRecipientWithData failed', e); }
}

function clearAllRecipientsRows(){ try{ var container = document.getElementById('recipients-container'); if(!container) return; container.innerHTML = ''; }catch(e){} }
function addNumericRuleWithData(name, keyword, op, emails){
    try{
        var container = document.getElementById('numeric-rules-container'); if(!container) return;
        var row = document.createElement('tr'); row.className = 'numeric-rule-row';
        row.innerHTML = `
            <td class="numeric-check" style="text-align:center;width:8%;"><input type="checkbox" class="numeric-select"></td>
            <td class="numeric-ref" style="text-align:center;width:35%;"><input type="text" name="numeric_keyword[]" value="${keyword?keyword.replace(/\"/g,'&quot;'):''}" placeholder="' + (window._t ? window._t('t7RefPlaceholder', 'Reference (e.g. Castelo 40)') : 'Reference (e.g. Castelo 40)') + '" class="form-input" style="width:100%;box-sizing:border-box;text-align:center;padding:8px 10px;"></td>
            <td class="numeric-crit" style="text-align:center;width:22%;"><select name="numeric_op[]" class="form-select" style="width:100%;box-sizing:border-box;text-align:center;padding:8px 10px;">
                <option value="less" ${op==='less'?'selected':''}>Menor</option>
                <option value="different" ${op==='different'?'selected':''}>Diferente</option>
                <option value="greater" ${op==='greater'?'selected':''}>Maior</option>
            </select></td>
            <td class="numeric-emails" style="text-align:center;width:35%;"><input type="text" name="numeric_emails[]" value="${emails?emails.replace(/\"/g,'&quot;'):''}" placeholder="' + (window._t ? window._t('t7EmailsHint', 'emails separated by commas') : 'emails separated by commas') + '" class="form-input" style="width:100%;box-sizing:border-box;text-align:center;padding:8px 10px;"></td>
        `;
        container.appendChild(row);
    }catch(e){ console.warn('addNumericRuleWithData failed', e); }
}

function clearAllNumericRulesRows(){ try{ var container = document.getElementById('numeric-rules-container'); if(!container) return; container.innerHTML=''; }catch(e){} }

function prepareRecipientsPayload(){
    try{
        var recipients = [];
        var rows = document.querySelectorAll('#recipients-container tr.recipient-row');
        rows.forEach(row => {
            var name = row.querySelector('input[name="recipient_name[]"]').value.trim();
            var email = row.querySelector('input[name="recipient_email[]"]').value.trim();
            if(name || email) recipients.push({name, email});
        });
        var el = document.getElementById('recipients_json'); if(el) el.value = JSON.stringify(recipients);

        // Numeric rules
        var nrContainer = document.getElementById('numeric-rules-container');
        if(nrContainer){
            var numeric_rules = [];
            var rows = nrContainer.querySelectorAll('.numeric-rule-row');
            rows.forEach(function(row){
                var keyword = row.querySelector('input[name="numeric_keyword[]"]').value.trim();
                var op = row.querySelector('select[name="numeric_op[]"]').value;
                var emails = row.querySelector('input[name="numeric_emails[]"]').value.trim();
                if(keyword || emails){ var opNum = (op==='less'?0:(op==='greater'?1:2)); numeric_rules.push({keyword, op:opNum, emails}); }
            });
            var nrEl = document.getElementById('numeric_rules_json'); if(!nrEl){ nrEl = document.createElement('input'); nrEl.type='hidden'; nrEl.id='numeric_rules_json'; nrEl.name='numeric_rules_json'; var f = document.getElementById('recipients-form'); if(f) f.appendChild(nrEl); }
            nrEl.value = JSON.stringify(numeric_rules);
        }

        // Don't copy sender flag
        var chk = document.getElementById('dont_copy_sender_checkbox');
        var hid = document.getElementById('email_dont_copy_sender');
        if(!hid){ hid = document.createElement('input'); hid.type='hidden'; hid.id='email_dont_copy_sender'; hid.name='email_dont_copy_sender'; var f2 = document.getElementById('recipients-form'); if(f2) f2.appendChild(hid); }
        hid.value = (chk && chk.checked) ? '1' : '0';
    }catch(e){ console.warn('prepareRecipientsPayload failed', e); }
}

function setupDontCopySenderAutoSave(){
    try{
        var chk = document.getElementById('dont_copy_sender_checkbox'); if(!chk) return;
        chk.addEventListener('change', function(){
            var body = 'section=recipients&email_dont_copy_sender=' + (chk.checked ? '1' : '0');
            try{ if(typeof optimizedFetch !== 'undefined' && optimizedFetch && typeof optimizedFetch.request === 'function'){
                optimizedFetch.request('/api/config', { method: 'POST', body: body, headers: {'Content-Type':'application/x-www-form-urlencoded'} }, 0)
                .then(function(resp){ console.log('✔️ Config updated (dont_copy_sender):', resp); }).catch(function(err){ console.error('❌ Failed to save preference dont_copy_sender', err); });
            } else {
                fetch('/api/config', { method: 'POST', headers: {'Content-Type':'application/x-www-form-urlencoded'}, body: body }).then(r=>r.json()).then(function(resp){ console.log('✔️ Config updated (dont_copy_sender):', resp); }).catch(function(err){ console.error('❌ Failed to save preference dont_copy_sender', err); });
            }}catch(e){ console.error('setupDontCopySenderAutoSave post failed', e); }
        });
    }catch(e){ /* ignore */ }
}

function loadRecipientsConfig(){
    try{
        if(typeof optimizedFetch !== 'undefined' && optimizedFetch && typeof optimizedFetch.request === 'function'){
            optimizedFetch.request('/api/recipients?limit=100', {}, 0).then(function(list){
                try{ if(typeof clearAllRecipientsRows === 'function') clearAllRecipientsRows(); }catch(e){}
                try{ if(Array.isArray(list) && list.length){ list.forEach(function(rec){ if(typeof addRecipientWithData === 'function') addRecipientWithData(rec.name||'', rec.email||''); }); }
                    if(typeof addRecipientWithData === 'function') addRecipientWithData('', '');
                }catch(e){ console.error('Error rendering recipients:', e); }
            }).catch(function(err){
                console.error('Failed to fetch /api/recipients:', err);
                // fallback minimal: fetch configs to populate numeric rules / dont_copy_sender
                optimizedFetch.request('/api/configs?t=' + Date.now(), {}, 0).then(function(d){
                    try{
                        if(d.saved_configs && Array.isArray(d.saved_configs.numeric_rules)){
                            if(typeof clearAllNumericRulesRows === 'function') clearAllNumericRulesRows();
                            d.saved_configs.numeric_rules.forEach(function(r){ if(typeof addNumericRuleWithData==='function') addNumericRuleWithData(r.name||'', r.keyword||'', (r.op===0?'less':(r.op===1?'greater':'different')), r.emails||''); });
                        }
                        if(typeof addNumericRuleWithData === 'function') addNumericRuleWithData('','','less','');
                        var dontCopy = false;
                        if(d.saved_configs){ if(d.saved_configs.email && typeof d.saved_configs.email.dont_copy_sender!=='undefined') dontCopy = !!d.saved_configs.email.dont_copy_sender; else if(typeof d.saved_configs.email_dont_copy_sender!=='undefined') dontCopy = !!d.saved_configs.email_dont_copy_sender; else if(typeof d.saved_configs['email.dont_copy_sender']!=='undefined') dontCopy = !!d.saved_configs['email.dont_copy_sender']; }
                        var cb = document.getElementById('dont_copy_sender_checkbox'); if(cb) cb.checked = dontCopy;
                        try{ if(typeof setupDontCopySenderAutoSave === 'function') setTimeout(setupDontCopySenderAutoSave, 50); }catch(e){}
                    }catch(e){}
                }).catch(function(){ setTimeout(loadRecipientsConfig, 2000); });
            });
        } else {
            // fallback: use fetch directly
            fetch('/api/recipients?limit=100',{cache:'no-store'}).then(r=>r.json()).then(function(list){ try{ if(typeof clearAllRecipientsRows === 'function') clearAllRecipientsRows(); }catch(e){} if(Array.isArray(list)) list.forEach(function(rec){ try{ if(typeof addRecipientWithData === 'function') addRecipientWithData(rec.name||'', rec.email||''); }catch(e){} }); if(typeof addRecipientWithData === 'function') addRecipientWithData('', ''); }).catch(function(){ setTimeout(loadRecipientsConfig,2000); });
        }
    }catch(e){ console.error('loadRecipientsConfig error', e); }
}


// prepareScheduledCommandsPayload() removed — the legacy payload is no longer sent.

var pageLoaded = false;
function initCommandsWhenReady(){
    if(pageLoaded) return;
    var helpersReady = (typeof loadRs232Templates === 'function');
    if(!helpersReady){ setTimeout(initCommandsWhenReady, 120); return; }
    pageLoaded = true;
    console.log('📄 Initializing commands page (helpers detected) ...');
    try{ if(typeof optimizedFetch !== 'undefined' && optimizedFetch && optimizedFetch.cache) optimizedFetch.cache.cleanup(); }catch(e){}
    var templatesPromise = Promise.resolve();
    try{ if(typeof loadRs232Templates === 'function') templatesPromise = loadRs232Templates(); }catch(e){ templatesPromise = Promise.resolve(); }
    var commanded = false;
    // Legacy command loading was removed — only RS232 templates are still loaded.

    // Drain any queued UI actions that were pushed by the page stubs before /auto.js loaded.
    try{
        // ♻️ _cmd_queue legacy ignorado – filas de UI antigas descartadas
    }catch(e){ }
        // If recipients or numeric-rules were queued by lightweight page stubs (e.g. user clicked "Add" before /auto.js loaded),
        // replay them now so UI reflects the click immediately.
        try{
            // replay recipients queue
            var _rq = window._recipients_queue || [];
            if(_rq && _rq.length){
                try{ console.log('Replaying', _rq.length, 'queued recipients action(s) from page stubs...'); }catch(e){}
                _rq.forEach(function(it){ try{ if(!it) return; if(it.type==='clear' && typeof clearAllRecipientsRows==='function') clearAllRecipientsRows(); else if(it.type==='add' && typeof addRecipientWithData==='function') addRecipientWithData(it.name||'', it.email||''); }catch(e){} });
                window._recipients_queue = [];
            }
        }catch(e){}
    // Drain recipient/numeric-rule queues that pages may have created before /auto.js loaded
    try{
        var rq = window._recipients_queue || [];
        if(rq && rq.length){
            console.log('Replaying', rq.length, 'queued recipients actions...');
            rq.forEach(function(it){ try{ if(!it) return; if(it.type==='clear' && typeof clearAllRecipientsRows==='function') clearAllRecipientsRows(); else if(it.type==='add' && typeof addRecipientWithData==='function') addRecipientWithData(it.name||'', it.email||''); }catch(e){} });
            window._recipients_queue = [];
        }
    }catch(e){}
    try{
        var nrq = window._numeric_rules_queue || [];
        if(nrq && nrq.length){
            console.log('Replaying', nrq.length, 'queued numeric-rule actions...');
            nrq.forEach(function(it){ try{ if(!it) return; if(it.action==='clear' && typeof clearAllNumericRulesRows==='function') clearAllNumericRulesRows(); else if(typeof addNumericRuleWithData==='function') addNumericRuleWithData(it.name||'', it.keyword||'', it.op||'less', it.emails||''); }catch(e){} });
            window._numeric_rules_queue = [];
        }
    }catch(e){}
}

// ==========================
// Custom dialogs (styled alert/prompt/confirm)
// ==========================
(function(){
    if(window._customDialogsLoaded) return;
    window._customDialogsLoaded=true;
    
    function createOverlay(){
        var o=document.createElement('div');
        o.style.cssText='position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(2,6,23,0.78);z-index:99998;display:flex;align-items:center;justify-content:center;backdrop-filter:blur(4px)';
        return o;
    }
    
    function createBox(){
        var b=document.createElement('div');
        b.style.cssText='background:#0f172a;border:2px solid #3b82f6;border-radius:12px;padding:24px;min-width:320px;max-width:500px;box-shadow:0 12px 40px rgba(0,0,0,0.5);color:#e6f1ff;font-family:system-ui,-apple-system,sans-serif';
        return b;
    }
    
    window.customAlert=function(msg){
        var o=createOverlay(),b=createBox();
        b.innerHTML='<div style="font-size:15px;margin-bottom:20px;line-height:1.5">'+msg+'</div>';
        var btn=document.createElement('button');
        btn.textContent='OK';
        btn.style.cssText='background:#1d4ed8;color:#fff;border:1px solid #3b82f6;padding:10px 24px;border-radius:8px;cursor:pointer;font-size:14px;font-weight:600;width:100%';
        btn.onmouseenter=function(){btn.style.background='#2563eb';};
        btn.onmouseleave=function(){btn.style.background='#1d4ed8';};
        btn.onclick=function(){document.body.removeChild(o);};
        b.appendChild(btn);o.appendChild(b);document.body.appendChild(o);
        btn.focus();
    };
    
    window.customConfirm=function(msg,cb){
        var o=createOverlay(),b=createBox();
        b.innerHTML='<div style="font-size:15px;margin-bottom:20px;line-height:1.5">'+msg+'</div>';
        var btns=document.createElement('div');
        btns.style.cssText='display:flex;gap:10px';
        var no=document.createElement('button'),yes=document.createElement('button');
        no.textContent=(window._t?window._t('btnCancel','Cancel'):'Cancel');yes.textContent=(window._t?window._t('btnConfirm','Confirm'):'Confirm');
        no.style.cssText='flex:1;background:#1e293b;color:#e2e8f0;border:1px solid #334155;padding:10px;border-radius:8px;cursor:pointer;font-size:14px';
        yes.style.cssText='flex:1;background:#1d4ed8;color:#fff;border:1px solid #3b82f6;padding:10px;border-radius:8px;cursor:pointer;font-size:14px;font-weight:600';
        no.onmouseenter=function(){no.style.background='#334155';};
        no.onmouseleave=function(){no.style.background='#1e293b';};
        yes.onmouseenter=function(){yes.style.background='#2563eb';};
        yes.onmouseleave=function(){yes.style.background='#1d4ed8';};
        no.onclick=function(){document.body.removeChild(o);if(cb)cb(false);};
        yes.onclick=function(){document.body.removeChild(o);if(cb)cb(true);};
        btns.appendChild(no);btns.appendChild(yes);b.appendChild(btns);o.appendChild(b);document.body.appendChild(o);
        yes.focus();
    };
    
    window.customPrompt=function(msg,def,cb){
        var o=createOverlay(),b=createBox();
        b.innerHTML='<div style="font-size:15px;margin-bottom:12px;line-height:1.5">'+msg+'</div>';
        var inp=document.createElement('input');
        inp.type='text';inp.value=def||'';
        inp.style.cssText='width:100%;padding:10px;border:1px solid #3b82f6;border-radius:8px;background:#020617;color:#e6f1ff;font-size:14px;margin-bottom:16px;box-sizing:border-box';
        b.appendChild(inp);
        var btns=document.createElement('div');
        btns.style.cssText='display:flex;gap:10px';
        var no=document.createElement('button'),yes=document.createElement('button');
        no.textContent=(window._t?window._t('btnCancel','Cancel'):'Cancel');yes.textContent='OK';
        no.style.cssText='flex:1;background:#1e293b;color:#e2e8f0;border:1px solid #334155;padding:10px;border-radius:8px;cursor:pointer;font-size:14px';
        yes.style.cssText='flex:1;background:#1d4ed8;color:#fff;border:1px solid #3b82f6;padding:10px;border-radius:8px;cursor:pointer;font-size:14px;font-weight:600';
        no.onmouseenter=function(){no.style.background='#334155';};
        no.onmouseleave=function(){no.style.background='#1e293b';};
        yes.onmouseenter=function(){yes.style.background='#2563eb';};
        yes.onmouseleave=function(){yes.style.background='#1d4ed8';};
        no.onclick=function(){document.body.removeChild(o);if(cb)cb(null);};
        yes.onclick=function(){var v=inp.value;document.body.removeChild(o);if(cb)cb(v);};
        inp.onkeydown=function(e){if(e.key==='Enter'){yes.click();}else if(e.key==='Escape'){no.click();}};
        btns.appendChild(no);btns.appendChild(yes);b.appendChild(btns);o.appendChild(b);document.body.appendChild(o);
        inp.focus();inp.select();
    };
    
    // Loading overlay helpers
    window.showLoading=function(){var o=document.getElementById('loadingOverlay');if(o)o.style.display='flex'};
    window.hideLoading=function(){var o=document.getElementById('loadingOverlay');if(o)o.style.display='none'};
    window.disableBtn=function(b){if(!b)return;b.disabled=true;b.dataset.ot=b.textContent;b.textContent='⏳ ...';b.style.opacity='0.65'};
    window.enableBtn=function(b){if(!b)return;b.disabled=false;b.textContent=b.dataset.ot||'';b.style.opacity='';delete b.dataset.ot};
    window.submitWithLoading=function(btnEl,fetchPromise){disableBtn(btnEl);showLoading();return fetchPromise.finally(function(){hideLoading();enableBtn(btnEl)});};

    // Form submission helper (converts form POST to fetch with JSON response + alert)
    window.handleFormSubmitWithJson=function(formId,onSuccess){
        var form=document.getElementById(formId);
        if(!form)return;
        form.addEventListener('submit',function(e){
            e.preventDefault();
            var btn=form.querySelector('button[type="submit"]');
            disableBtn(btn);
            showLoading();
            var fd=new FormData(form);
            fetch(form.action,{method:'POST',body:fd})
            .then(function(r){return r.json();})
            .then(function(d){
                if(d.success){
                    customAlert(d.message||(window._t?window._t('alertSaved','✅ Configuration saved!'):'✅ Configuration saved!'));
                    if(onSuccess)onSuccess(d);
                }else{
                    customAlert(d.message||(window._t?window._t('alertSaveError','❌ Save error'):'❌ Save error'));
                }
            })
            .catch(function(e){
                customAlert((window._t?window._t('alertConnError','❌ Server connection error'):'❌ Server connection error')+': '+e);
            })
            .finally(function(){
                hideLoading();
                enableBtn(btn);
            });
        });
    };
})();

// Bottom navigation bar (mobile) — defer until body exists
document.addEventListener('DOMContentLoaded',function(){
    if(window._noBottomNav)return;
    if(document.querySelector('.bottom-nav'))return;
    var ni=document.querySelector('.nav-inicio');
    if(ni&&window.innerWidth<768)ni.style.display='none';
    var nav=document.createElement('div');
    nav.className='bottom-nav';
    var html='<a href="/">🏠 Home</a><a href="#" onclick="history.back();return false">↩️ Back</a>';
    var sb=document.querySelector('button[type="submit"],button[id$="_save"],button[data-save]');
    if(!sb)sb=document.querySelector('.btn-save');
    if(sb)html+='<button>💾 Save</button>';
    nav.innerHTML=html;
    if(sb)nav.lastChild.onclick=function(){sb.click()};
    document.body.appendChild(nav);
});

// microSD page helpers
function initSdcardPage(){
    // Protection against multiple initializations
    if(window.sdcardPageInitialized) return;
    window.sdcardPageInitialized = true;
    
    var info=document.getElementById('sd_info');
    var msg=document.getElementById('sd_msg');
    var configMsg=document.getElementById('sd_config_msg');
    var writePauseSelect=document.getElementById('sd_write_pause_ms');
    var activeSelect=document.getElementById('sd_active');
    var formatSelect=document.getElementById('sd_format');
    var emailSelect=document.getElementById('sd_email');
    var saveBtn=document.getElementById('sd_save');
    var isPsramMode=false; // Track storage mode
    // Fields the page UI does not expose. Loaded from GET /api/sdcard-config and
    // re-sent on save so they are never reset to defaults (see BACKLOG SD-1).
    var sdExtra = { structured:true, email_from_sd:true, max_size_mb:4, emailday_extra_emails:'' };
    
    // CRITICAL: Validate essential elements exist before proceeding
    if(!info){
        console.error('initSdcardPage: sd_info element missing');
        return;
    }
    
    // Save button validation - but continue even if missing (page might be read-only)
    if(!saveBtn){
        console.warn('initSdcardPage: sd_save button missing - page will be read-only');
    }
    function fmtMB(b){return !b?'0 MB':(b/1024/1024).toFixed(1)+' MB';}
    function render(j){
        if(!info)return; // Safety check
        if(!j||j.status!=='ok'){info.textContent=(window._t?window._t('sdStatusNoData','No SD data'):'No SD data');return;}
        if(j.disabled){info.textContent=(window._t?window._t('sdStatusDisabled','SD disabled'):'SD disabled');return;}
        isPsramMode=(j.is_psram===true); // Store PSRAM mode state
        var line=(j.storage_mode||'SD Card')+' | '+(window._t?window._t('sdStatusMounted','Mounted'):'Mounted')+'='+(j.mounted?(window._t?window._t('opYes','Yes'):'Yes'):(window._t?window._t('opNo','No'):'No'));
        if(j.mounted)line+=' | Total='+fmtMB(j.total_bytes)+' | '+(window._t?window._t('sdStatusUsed','Used'):'Used')+'='+fmtMB(j.used_bytes);
        if(j.flash_total_bytes)line+=' | '+(window._t?window._t('sdStatusFlash','Flash'):'Flash')+'='+fmtMB(j.flash_used_bytes)+'/'+fmtMB(j.flash_total_bytes);
        info.textContent=line;
    }
    function load(){fetch('/api/sd-status',{cache:'no-store'}).then(r=>r.json()).then(render).catch(()=>{if(info)info.textContent=(window._t?window._t('sdStatusFailure','SD status failed'):'SD status failed');});}
    function loadConfig(){
        fetch('/api/sdcard-config',{cache:'no-store'}).then(r=>r.json()).then(j=>{
            if(j&&j.status==='ok'){
                // Safe value assignment - only if elements exist
                if(writePauseSelect){
                    // Map flush_interval string to milliseconds for dropdown value
                    var intervalMap = {'2s':'2000','10s':'10000','15s':'15000','30s':'30000',
                                      '1m':'60000','5m':'300000','10m':'600000','30m':'1800000','60m':'3600000'};
                    var msValue = intervalMap[j.flush_interval] || '1800000'; // Default 30m
                    writePauseSelect.value = msValue;
                }
                if(activeSelect) activeSelect.value=j.enabled?'1':'0';
                if(formatSelect) formatSelect.value=j.format||'csv';
                if(emailSelect) emailSelect.value=j.email_send?'1':'0';
                // Preserve the UI-invisible fields so the next save does not wipe them.
                if(typeof j.structured==='boolean') sdExtra.structured=j.structured;
                if(typeof j.email_from_sd==='boolean') sdExtra.email_from_sd=j.email_from_sd;
                if(typeof j.max_size_mb==='number') sdExtra.max_size_mb=j.max_size_mb;
                if(typeof j.emailday_extra_emails==='string') sdExtra.emailday_extra_emails=j.emailday_extra_emails;
                var cleanupThreshSel=document.getElementById('sd_cleanup_threshold');
                var keepDaysSel=document.getElementById('sd_keep_days');
                if(cleanupThreshSel) cleanupThreshSel.value=String(j.cleanup_threshold||70);
                if(keepDaysSel) keepDaysSel.value=(j.keep_days!=null)?String(j.keep_days):'0';
            }
        }).catch(e=>{console.warn('loadConfig failed:',e);});
    }
    function act(a){var fd=new FormData();fd.append('action',a);fd.append('_html','1');fetch('/api/sd-action',{method:'POST',body:fd}).then(r=>r.json()).then(j=>{if(msg)msg.textContent=(j&&j.status==='ok')?('Action '+a+' OK'):('Failed: '+a);load();}).catch(()=>{if(msg)msg.textContent='Action error '+a;});}
    function saveConfig(){
        // CRITICAL: Validate ALL required elements exist before accessing their properties
        var missingElements = [];
        if(!activeSelect) missingElements.push('sd_active');
        if(!formatSelect) missingElements.push('sd_format');
        if(!emailSelect) missingElements.push('sd_email');
        
        if(missingElements.length > 0){
            var errMsg = '❌ Error: missing elements: ' + missingElements.join(', ');
            console.error('saveConfig: ' + errMsg);
            if(configMsg) configMsg.textContent = errMsg;
            return;
        }
        
        // All elements validated, safe to access .value properties
        var fd=new FormData();
        
        // Map flush_interval: writePauseSelect is optional (may not exist in some page versions)
        if(writePauseSelect && writePauseSelect.value){ 
            var msToInterval = {'600000':'10m','1800000':'30m','3600000':'60m'};
            var interval = msToInterval[writePauseSelect.value] || '30m'; // Default 30m
            fd.append('flush_interval', interval);
        } else {
            // If element doesn't exist, use default value
            fd.append('flush_interval', '30m');
        }
        
        if (activeSelect) fd.append('enabled',activeSelect.value);
        if (formatSelect) fd.append('format',formatSelect.value);
        if (emailSelect)  fd.append('email_send',emailSelect.value);

        var ctSel=document.getElementById('sd_cleanup_threshold');
        var kdSel=document.getElementById('sd_keep_days');
        fd.append('auto_cleanup','1');
        fd.append('cleanup_threshold',ctSel?ctSel.value:'70');
        fd.append('keep_days',kdSel?kdSel.value:'0');
        // Re-send the UI-invisible fields (BACKLOG SD-1) — preserves them across saves.
        fd.append('structured', sdExtra.structured ? '1' : '0');
        fd.append('email_from_sd', sdExtra.email_from_sd ? '1' : '0');
        fd.append('max_size_mb', String(sdExtra.max_size_mb || 4));
        fd.append('emailday_extra_emails', sdExtra.emailday_extra_emails || '');

        fd.append('_html','1');
        
        if(saveBtn) saveBtn.disabled=true;
        console.log('Saving microSD config...'); // Debug
        
        fetch('/api/sdcard-config-save',{method:'POST',body:fd}).then(r=>r.json()).then(j=>{
            console.log('microSD config API response:', j); // Debug
            if(j&&j.status==='ok'){
                // Use standard save notification pattern
                if(typeof customAlert === 'function'){
                    customAlert(window._t?window._t('alertSaved','✅ Configuration saved!'):'✅ Configuration saved!');
                }
                if(typeof showSavedCard === 'function'){
                    showSavedCard();
                }
                if(configMsg)configMsg.textContent=(window._t?window._t('alertSaved','✅ Configuration saved!'):'✅ Configuration saved!');
            }else{
                console.error('Failed to save microSD config'); // Debug
                if(typeof customAlert === 'function'){
                    customAlert(window._t?window._t('alertSaveError','❌ Error saving'):'❌ Error saving');
                }
                if(configMsg)configMsg.textContent=(window._t?window._t('alertSaveError','❌ Error saving'):'❌ Error saving');
            }
            setTimeout(()=>{if(configMsg)configMsg.textContent='';if(saveBtn)saveBtn.disabled=false;},2000);
        }).catch(()=>{
            if(typeof customAlert === 'function'){
                customAlert(window._t?window._t('alertConnError','❌ Server connection error'):'❌ Server connection error');
            }
            if(configMsg)configMsg.textContent=(window._t?window._t('alertSaveError','❌ Error saving'):'❌ Error saving');
            if(saveBtn)saveBtn.disabled=false;
        });
    }
    // Event listeners - attach only if elements exist
    var mountBtn = document.getElementById('sd_mount');
    var unmountBtn = document.getElementById('sd_unmount');
    var refreshBtn = document.getElementById('sd_refresh');
    
    if(mountBtn) mountBtn.addEventListener('click',function(){act('mount');});
    if(unmountBtn) unmountBtn.addEventListener('click',function(){act('unmount');});
    if(refreshBtn) refreshBtn.addEventListener('click',function(){act('refresh');});
    if(saveBtn) saveBtn.addEventListener('click',function(){saveConfig();});
    
    load();
    loadConfig();
    var _sdPollTimer=null;
    function _startSdPoll(){if(!_sdPollTimer)_sdPollTimer=setInterval(load,10000);}
    function _stopSdPoll(){if(_sdPollTimer){clearInterval(_sdPollTimer);_sdPollTimer=null;}}
    document.addEventListener('visibilitychange',function(){document.hidden?_stopSdPoll():_startSdPoll();});
    _startSdPoll();
}

// Apply active language translations to all [data-i18n] and [data-i18n-html] elements.
// data-i18n uses textContent (safe); data-i18n-html uses innerHTML (only for our own PROGMEM strings).
// Skips the fetch entirely when no translatable elements are present on the page.
// Exposed as window.applyI18n so pages can call it again after a language config save.
(function(){
    function hasI18nTargets(){
        return !!document.querySelector('[data-i18n],[data-i18n-html]');
    }
    function ensureI18nStyle(){
        if(document.getElementById('i18n-preload-style')) return;
        var st=document.createElement('style');
        st.id='i18n-preload-style';
        st.textContent="html:not([data-i18n-ready='1']) [data-i18n],html:not([data-i18n-ready='1']) [data-i18n-html]{visibility:hidden;}";
        (document.head||document.documentElement).appendChild(st);
    }
    function setI18nReady(ready){
        document.documentElement.setAttribute('data-i18n-ready',ready?'1':'0');
    }
    function persistLangChoice(code){
        if(!code) return;
        try{ localStorage.setItem('ui_lang_code',code); }catch(e){}
        document.documentElement.lang=code;
    }
    function persistLangPayload(code,strings){
        try{
            if(code) localStorage.setItem('ui_lang_code',code);
            if(strings) localStorage.setItem('ui_lang_cache',JSON.stringify(strings));
        }catch(e){}
    }
    function readCachedLang(){
        try{
            var raw=localStorage.getItem('ui_lang_cache');
            if(!raw) return null;
            var parsed=JSON.parse(raw);
            return parsed&&typeof parsed==='object'?parsed:null;
        }catch(e){ return null; }
    }
    function applyI18nMap(strings,langCode){
        if(!strings) return false;
        window._lang = strings;
        if(langCode) persistLangChoice(langCode);
document.querySelectorAll('[data-i18n],[data-i18n-html]').forEach(function(el){
            var k=el.getAttribute('data-i18n')||el.getAttribute('data-i18n-html');
            if(strings[k]===undefined) return;
            if(el.hasAttribute('data-i18n-html')) el.innerHTML=strings[k];
            else el.textContent=strings[k];
        });
        document.querySelectorAll('[data-i18n-title]').forEach(function(el){
            var k=el.getAttribute('data-i18n-title');
            if(k && strings[k]!==undefined) el.setAttribute('title',strings[k]);
        });
        setI18nReady(true);
        return true;
    }
    function applyI18n(opts){
        if(!hasI18nTargets()) return;
        opts=opts||{};
        ensureI18nStyle();
        var cachedCode='';
        try{ cachedCode=localStorage.getItem('ui_lang_code')||''; }catch(e){}
        var cachedMap=opts.forceRefresh?null:readCachedLang();
        if(cachedMap){
            applyI18nMap(cachedMap,opts.langHint||cachedCode||document.documentElement.lang||'pt');
        } else {
            setI18nReady(false);
            if(opts.langHint) persistLangChoice(opts.langHint);
        }
        fetch('/api/lang?t='+Date.now(),{cache:'no-store'}).then(function(r){
            if(!r.ok) return null;
            var langCode=r.headers.get('X-UI-Lang')||r.headers.get('Content-Language')||opts.langHint||cachedCode||document.documentElement.lang||'pt';
            return r.json().then(function(s){ return {strings:s,langCode:langCode}; });
        }).then(function(payload){
            if(!payload||!payload.strings) {
                if(!window._lang) setI18nReady(true);
                return;
            }
            persistLangPayload(payload.langCode,payload.strings);
            applyI18nMap(payload.strings,payload.langCode);
        }).catch(function(){
            if(!window._lang) setI18nReady(true);
        });
    }
    window.applyI18n=applyI18n;
    window.setUiLanguagePref=function(code){ persistLangChoice(code||'pt'); };
    window._t=function(k,fb){return (window._lang&&window._lang[k]!==undefined)?window._lang[k]:(fb!==undefined?fb:k);};
    if(document.readyState==='loading'){
        document.addEventListener('DOMContentLoaded',applyI18n);
    } else {
        applyI18n();
    }
    // Safety net: if i18n fetch hangs for any reason (e.g. device under load), reveal elements after 5s.
    setTimeout(function(){
        if(document.documentElement.getAttribute('data-i18n-ready')!=='1'){
            document.documentElement.setAttribute('data-i18n-ready','1');
        }
    },5000);
})();

// Firmware cache buster.
// On every page load, fetch the current firmware_version from /api/status and
// compare it with the last known value stored in localStorage._fw_ver.
// If they differ (new firmware was flashed), save the new version and force a
// hard reload — this bypasses all browser caches and ensures the client always
// runs the latest HTML, CSS and JS after an OTA or serial flash.
// No reload is triggered on the very first visit (no stored value yet).
(function(){
    try{
        var path = (window.location && window.location.pathname) ? window.location.pathname : '';
        if(path === '/status' || path === '/api-reference') return;

        var nowMs = Date.now();
        var lastCheck = 0;
        try{ lastCheck = Number(localStorage.getItem('_fw_ver_last_check') || '0'); }catch(_){ lastCheck = 0; }
        if(lastCheck > 0 && (nowMs - lastCheck) < 180000){
            return;
        }
        try{ localStorage.setItem('_fw_ver_last_check', String(nowMs)); }catch(_){}

        fetch('/api/status?t=' + Date.now(), {cache: 'no-store'})
            .then(function(r){ return r.ok ? r.json() : null; })
            .then(function(d){
                if(!d) return;
                var v = d.firmware_version || '';
                if(!v) return;
                var stored = '';
                try{ stored = localStorage.getItem('_fw_ver') || ''; }catch(_){}
                try{ localStorage.setItem('_fw_ver', v); }catch(_){}
                if(stored && stored !== v){
                    // New firmware detected — hard reload to flush all stale browser caches
                    location.reload(true);
                }
            })
            .catch(function(){});
    }catch(_){}
})();

)rawliteral";
