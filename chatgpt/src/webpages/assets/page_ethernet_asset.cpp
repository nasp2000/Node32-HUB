#include "page_ethernet_asset.h"

const char PAGE_ETHERNET[] PROGMEM = R"HTML(
<div class='page ethernet'>
 <div class='wrap'>
    <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to main page' data-i18n-title='btnHomeTitle'>🏠 Home</a></div>
    <h1 data-i18n='ethernetPageTitle'>🌐 Ethernet</h1>
    
    <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:16px'>
      <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='ethernetStatusTitle'>📊 Current Status</h2>
      <div id='eth_info' class='info-item mono' style='background:#1e3a8a;padding:12px;border-radius:4px;border-left:4px solid #3b82f6;color:#e0e7ff;font-size:13px' data-i18n='ethernetStatusLoading'>Getting status...</div>
    </div>
    
    <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
      <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='ethernetConfigTitle'>⚙️ Configuration</h2>
      
      <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:0 0 16px 0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px' data-i18n-html='ethernetInfoDesc'>
        <div>🔌 <strong style='color:#60a5fa'>Mode:</strong> DHCP (automatic) or Static (manual)</div>
        <div>🌐 <strong style='color:#60a5fa'>IP:</strong> Fixed IP address (static mode only)</div>
        <div>🚪 <strong style='color:#60a5fa'>Gateway:</strong> Network router/gateway (static mode only)</div>
        <div>📡 <strong style='color:#60a5fa'>DNS:</strong> DNS server (static mode only)</div>
        <div>🏷️ <strong style='color:#60a5fa'>MAC:</strong> Custom MAC address (optional)</div>
      </div>
      
      <form id='eth_form'>
        <label style='display:block;margin-bottom:4px;color:#60a5fa;font-weight:bold' data-i18n='ethernetModeLbl'>Mode:</label>
        <select name='mode' id='eth_mode' style='width:100%;margin-bottom:12px' title='Use fixed IP address' data-i18n-title='ethTipStaticIp'>
            <option value='dhcp'>DHCP</option>
            <option value='static' data-i18n='ethernetStatic'>Static</option>
        </select>
        
        <label style='display:block;margin-bottom:4px;color:#60a5fa;font-weight:bold' data-i18n='ethernetIpLbl'>IP:</label>
        <input type='text' name='ip' id='eth_ip' placeholder='192.168.1.50' maxlength='15' style='width:100%;margin-bottom:12px' title='Device IP address' data-i18n-title='ethTipIp'>
        
        <label style='display:block;margin-bottom:4px;color:#60a5fa;font-weight:bold' data-i18n='ethernetGatewayLbl'>Gateway:</label>
        <input type='text' name='gw' id='eth_gw' placeholder='192.168.1.1' maxlength='15' style='width:100%;margin-bottom:12px' title='Network gateway' data-i18n-title='ethTipGateway'>
        
        <label style='display:block;margin-bottom:4px;color:#60a5fa;font-weight:bold' data-i18n='ethernetDnsLbl'>DNS:</label>
        <input type='text' name='dns' id='eth_dns' placeholder='8.8.8.8' maxlength='15' style='width:100%;margin-bottom:12px' title='DNS server' data-i18n-title='ethTipDns'>
        
        <label style='display:block;margin-bottom:4px;color:#60a5fa;font-weight:bold' data-i18n='ethernetMacLbl'>MAC (override):</label>
        <input type='text' name='mac' id='eth_mac' placeholder='DE:AD:BE:EF:FE:ED' maxlength='17' style='width:100%;margin-bottom:12px'>
        
        <div id='eth_msg' class='desc' style='margin-top:12px;text-align:center;color:#10b981;font-weight:bold'></div>
      </form>
      
      <div style='text-align:center;margin-top:16px'>
        <button type='button' class='btn' id='eth_save' style='padding:10px 24px' data-i18n='btnSave' title='Save Ethernet config' data-i18n-title='ethTipSave'>💾 Save</button>
      </div>
    </div>

    <div style='border:2px solid #f59e0b;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
      <h2 style='margin-top:0;font-size:18px;color:#fbbf24' data-i18n='ethernetWifiRuntimeTitle'>📶 WiFi Runtime</h2>
      <div style='background:#1e3a8a;border-left:4px solid #f59e0b;padding:12px;margin:0 0 16px 0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px'>
        <div data-i18n-html='ethernetWifiRuntimeInfoOff'>⚡ <strong style='color:#fbbf24'>Disable WiFi</strong> frees ~25-35 KB of heap immediately (driver + lwIP netif)</div>
        <div data-i18n-html='ethernetWifiRuntimeInfoOn'>🔄 <strong style='color:#fbbf24'>Re-enable WiFi</strong> requires a device restart to reinitialize the driver</div>
        <div data-i18n-html='ethernetWifiRuntimeInfoPersist'>💾 The choice <strong style='color:#fbbf24'>persists across reboot</strong> - WiFi will not start if disabled</div>
      </div>
      <div id='wifi_rt_status' style='background:#0c1425;padding:10px 14px;border-radius:4px;font-size:13px;color:#94a3b8;margin-bottom:14px' data-i18n='ethernetWifiRuntimeLoading'>Getting WiFi status...</div>
      <div style='text-align:center;display:flex;gap:12px;justify-content:center;flex-wrap:wrap'>
        <button type='button' class='btn' id='btn_wifi_off' style='padding:10px 24px;background:#dc2626' data-i18n='ethernetWifiDisableBtn' title='Disable WiFi, free heap' data-i18n-title='ethTipWifiDisable'>⛔ Disable WiFi</button>
        <button type='button' class='btn' id='btn_wifi_on' style='padding:10px 24px;background:#16a34a' data-i18n='ethernetWifiEnableBtn' title='Re-enable WiFi (reboot needed)' data-i18n-title='ethTipWifiEnable'>✅ Enable WiFi</button>
      </div>
      <div id='wifi_rt_msg' style='margin-top:12px;text-align:center;font-size:13px;color:#fbbf24;font-weight:bold'></div>
    </div>

 </div>
 <script>
 (function(){
     var info=document.getElementById('eth_info');
     var modeSel=document.getElementById('eth_mode');
     var ip=document.getElementById('eth_ip');
     var gw=document.getElementById('eth_gw');
     var dns=document.getElementById('eth_dns');
     var mac=document.getElementById('eth_mac');
     var msg=document.getElementById('eth_msg');
      function t(k,fb){
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
     function notify(text){
       window.customAlert(text);
     }
     function toggleStatic(){
         var st = modeSel.value==='static';
         [ip,gw,dns].forEach(function(el){ el.disabled=!st; });
         // Do not clear values here: DHCP-assigned values are kept as read-only info
     }
     modeSel.addEventListener('change',function(){
         // When user manually changes mode, clear static fields to avoid confusion
         if(modeSel.value!=='static'){ ip.value=''; gw.value=''; dns.value=''; }
         toggleStatic();
     });
     function poll(){
         fetch('/api/eth-status',{cache:'no-store'})
             .then(r=>r.json())
             .then(j=>{
               if(!j||!j.status){ info.textContent=t('ethernetStatusNoData','No Ethernet data'); return; }
               var modeValue = j.mode==='static' ? t('ethernetStatic','Static') : 'DHCP';
               info.textContent=t('ethernetStatusIpLabel','IP')+': '+(j.ip||'-')+' | '+t('ethernetStatusModeLabel','Mode')+': '+modeValue+' | '+t('ethernetStatusMacLabel','MAC')+': '+(j.mac||'-');
               if(j.mode){ modeSel.value=j.mode; }
               if(j.mode==='static'){
                 if(j.ip) ip.value=j.ip;
                 if(j.gw) gw.value=j.gw;
                 if(j.dns) dns.value=j.dns;
               } else {
                 // Show DHCP-assigned values in disabled fields for reference
                 if(j.ip) ip.value=j.ip;
                 if(j.dhcp_gw && j.dhcp_gw!=='0.0.0.0') gw.value=j.dhcp_gw;
                 if(j.dhcp_dns && j.dhcp_dns!=='0.0.0.0') dns.value=j.dhcp_dns;
               }
               if(j.mac_override){ mac.value=j.mac_override; }
               toggleStatic();
             })
             .catch(()=>{ info.textContent=t('ethernetStatusFetchError','Failed to fetch Ethernet status'); });
     }
     poll();
     // Retry every 6s in case the first poll fires while the server is still busy
     // (Ethernet bootstrap is single-threaded; first fetch often fails on load)
     setInterval(poll, 6000);
     document.getElementById('eth_save').addEventListener('click',function(){
          var btn=this;disableBtn(btn);showLoading();
          var fd=new FormData(); fd.append('_html','1'); fd.append('mode',modeSel.value);
          if(modeSel.value==='static'){ fd.append('ip',ip.value.trim()); fd.append('gw',gw.value.trim()); fd.append('dns',dns.value.trim()); }
          if(mac.value.trim()) fd.append('mac',mac.value.trim());
          fetch('/api/eth-config',{method:'POST',body:fd})
           .then(r=>r.json()).then(j=>{
             if(j&&j.status==='ok'){
               msg.textContent=t('ethernetSaveSuccess','Saved. Restart to apply.');
               notify(t('alertSaved','✅ Configuration saved!'));
               if(typeof showSavedCard==='function') showSavedCard();
               return;
             }
             msg.textContent=t('ethernetSaveError','Error saving.');
             notify(t('alertSaveError','❌ Error saving'));
           })
           .catch((e2)=>{
             msg.textContent=t('ethernetRequestFail','Request failed.');
             notify(t('emailErrorPrefix','Error: ')+(e2&&e2.message?e2.message:e2));
           })
           .finally(function(){hideLoading();enableBtn(btn);});
      });

     // WiFi runtime toggle section
     var wifiStatus=document.getElementById('wifi_rt_status');
     var wifiMsg=document.getElementById('wifi_rt_msg');
     function pollWifi(){
       fetch('/api/wifi-runtime-status',{cache:'no-store'})
         .then(function(r){return r.json();})
         .then(function(j){
            if(j.disabled){
              wifiStatus.textContent=t('ethernetWifiRuntimeDisabled','⛔ WiFi DISABLED (persisted) - restart to reactivate after enabling');
              wifiStatus.style.color='#f87171';
            } else if(j.connected){
              wifiStatus.textContent=t('ethernetWifiRuntimeOn','📶 WiFi ON')+' - '+j.ssid+' ('+j.ip+')';
              wifiStatus.style.color='#4ade80';
            } else {
              wifiStatus.textContent=t('ethernetWifiRuntimeConnecting','📶 WiFi ON - connecting...');
              wifiStatus.style.color='#fbbf24';
            }
         })
          .catch(function(){wifiStatus.textContent=t('ethernetWifiRuntimeFetchError','Error getting WiFi status');});
     }
     pollWifi();
     setInterval(pollWifi, 8000);
     document.getElementById('btn_wifi_off').addEventListener('click',function(){
        customConfirm(t('ethernetWifiDisableConfirm','Disable WiFi now? Heap is freed immediately. To reactivate, click Enable WiFi and restart.'),function(ok){
       if(!ok) return;
       var btn=document.getElementById('btn_wifi_off');disableBtn(btn);showLoading();
       fetch('/api/wifi-disable',{method:'POST'})
         .then(function(r){return r.json();})
         .then(function(j){
            wifiMsg.textContent=j.message||t('ethernetWifiDisabledMsg','WiFi disabled.');
            notify(t('ethernetWifiDisabledNotify','⛔ WiFi disabled!'));
           setTimeout(pollWifi,1000);
         })
          .catch(function(e){notify(t('emailErrorPrefix','Error: ')+e.message);})
          .finally(function(){hideLoading();enableBtn(btn);});
        });
      });
      document.getElementById('btn_wifi_on').addEventListener('click',function(){
        var btn=document.getElementById('btn_wifi_on');disableBtn(btn);showLoading();
        fetch('/api/wifi-enable',{method:'POST'})
          .then(function(r){return r.json();})
          .then(function(j){
            wifiMsg.textContent=(j.message||t('ethernetWifiEnabledMsg','WiFi re-enabled.'))+' - '+t('ethernetWifiRestartHint','restart the device.');
            notify(t('ethernetWifiEnabledNotify','✅ WiFi re-enabled - restart to apply!'));
           setTimeout(pollWifi,1000);
         })
          .catch(function(e){notify(t('emailErrorPrefix','Error: ')+e.message);})
          .finally(function(){hideLoading();enableBtn(btn);});
     });

 })();
 </script>
</div>
)HTML";