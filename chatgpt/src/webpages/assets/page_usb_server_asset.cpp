#include "page_usb_server_asset.h"

const char PAGE_USB_SERVER[] PROGMEM = R"HTML(
<div class='page usb-server'>
  <div class='wrap'>
    <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='usbTipHome'>🏠 Home</a></div>
    <h1 data-i18n='usbServerPageTitle'>🧩 USB Server (USB/IP)</h1>

    <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:16px'>
      <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='usbServerStatusTitle'>📊 Status</h2>
      <div id='usb_server_status' class='mono' style='background:#1e3a8a;padding:12px;border-radius:4px;border-left:4px solid #3b82f6;color:#e0e7ff;font-size:13px' data-i18n='usbServerLoading'>Getting status...</div>
    </div>

    <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
      <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='usbServerDevicesTitle'>🔌 USB Devices</h2>
      <div id='usb_server_devices' class='mono' style='background:#1e3a8a;padding:12px;border-radius:4px;border-left:4px solid #3b82f6;color:#e0e7ff;font-size:13px;white-space:pre-wrap' data-i18n='usbServerDevicesLoading'>Getting devices...</div>
    </div>

    <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
      <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='usbServerConfigTitle'>⚙️ Configuration</h2>

      <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:0 0 16px 0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px' data-i18n-html='usbServerInfoDesc'>
        <div>🖧 <strong style='color:#60a5fa'>USB/IP:</strong> Exports USB-CDC devices to network clients.</div>
        <div>📡 <strong style='color:#60a5fa'>Port:</strong> TCP port where the server listens (default 3240).</div>
        <div>⏱️ <strong style='color:#60a5fa'>RX Timeout:</strong> Socket receive timeout.</div>
        <div>🔄 <strong style='color:#60a5fa'>IN Timeout:</strong> Maximum wait for RX data on IN requests.</div>
      </div>

      <form id='usb_server_form'>
        <label style='display:flex;gap:8px;align-items:center;margin-bottom:12px;color:#60a5fa;font-weight:bold'>
          <input type='checkbox' id='usb_server_enabled' checked title='Enable USB server' data-i18n-title='usbTipEnable'>
          <span data-i18n='usbServerEnabledLbl'>Enable USB/IP server</span>
        </label>

        <label style='display:block;margin-bottom:4px;color:#60a5fa;font-weight:bold' data-i18n='usbServerPortLbl'>TCP Port:</label>
        <input type='number' id='usb_server_port' min='1' max='65535' value='3240' title='TCP port for USB/IP server' data-i18n-title='usbTipPort' style='width:100%;margin-bottom:12px'>

        <label style='display:block;margin-bottom:4px;color:#60a5fa;font-weight:bold' data-i18n='usbServerRecvTimeoutLbl'>Timeout RX (ms):</label>
        <input type='number' id='usb_server_recv_timeout' min='100' max='60000' value='1200' title='Socket receive timeout (ms)' data-i18n-title='usbTipRxTimeout' style='width:100%;margin-bottom:12px'>

        <label style='display:block;margin-bottom:4px;color:#60a5fa;font-weight:bold' data-i18n='usbServerInWaitLbl'>Timeout IN (ms):</label>
        <input type='number' id='usb_server_in_wait' min='20' max='5000' value='220' title='Max wait for IN request data (ms)' data-i18n-title='usbTipInTimeout' style='width:100%;margin-bottom:12px'>

        <div id='usb_server_msg' class='desc' style='margin-top:12px;text-align:center;color:#10b981;font-weight:bold'></div>
      <div style='text-align:center;margin-top:16px'>
        <button type='button' class='btn' id='usb_server_save' title='Save USB server config' data-i18n-title='usbTipSave' data-i18n='btnSave'>💾 Save</button>
      </div>
      </form>
    </div>
  </div>

  <script>
  (function(){
    var statusEl = document.getElementById('usb_server_status');
    var enabledEl = document.getElementById('usb_server_enabled');
    var portEl = document.getElementById('usb_server_port');
    var recvEl = document.getElementById('usb_server_recv_timeout');
    var inWaitEl = document.getElementById('usb_server_in_wait');
    var msgEl = document.getElementById('usb_server_msg');
    var devicesEl = document.getElementById('usb_server_devices');

    function t(k, fb){
      try{
        if(typeof window._t === 'function') return window._t(k, fb);
      }catch(err){}
      return fb!==undefined ? fb : k;
    }

    function notify(text){
      window.customAlert(text);
    }

    function loadConfig(){
      return fetch('/api/usb-server-config', {cache:'no-store'})
        .then(function(r){ return r.json(); })
        .then(function(j){
          if(!j || j.success !== true) return;
          enabledEl.checked = !!j.enabled;
          portEl.value = String(j.port || 3240);
          recvEl.value = String(j.recv_timeout_ms || 1200);
          inWaitEl.value = String(j.in_wait_timeout_ms || 220);
        })
        .catch(function(){
          msgEl.textContent = t('usbServerRequestFail','Request failed.');
        });
    }

    function loadStatus(){
      return fetch('/api/usb-server-status', {cache:'no-store'})
        .then(function(r){ return r.json(); })
        .then(function(j){
          if(!j || j.success !== true){
            statusEl.textContent = t('usbServerNoData','No USB Server data.');
            return;
          }
          var runningTxt = j.running ? t('usbServerRunning','Active') : t('usbServerStopped','Stopped');
          statusEl.textContent =
            t('usbServerStateLbl','State') + ': ' + runningTxt +
            ' | ' + t('usbServerPortLbl','TCP Port') + ': ' + String(j.port || 3240) +
            ' | ' + t('usbServerClientsLbl','Clients') + ': ' + String(j.active_clients || 0) +
            ' | ' + t('usbServerRxLbl','RX') + ': ' + String(j.rx_bytes || 0) + ' B' +
            ' | ' + t('usbServerTxLbl','TX') + ': ' + String(j.tx_bytes || 0) + ' B' +
            ' | ' + t('usbServerThroughput','Throughput') + ': ' + String(j.rx_bytes_per_sec || 0) + '/' + String(j.tx_bytes_per_sec || 0) + ' B/s';
        })
        .catch(function(){
          statusEl.textContent = t('usbServerStatusError','Failed to get USB Server status.');
        });
    }

    function hex2(value){
      var n = parseInt(value || 0, 10);
      var s = n.toString(16).toUpperCase();
      return (s.length < 2 ? '0' + s : s);
    }

    function hex4(value){
      var n = parseInt(value || 0, 10);
      var s = n.toString(16).toUpperCase();
      while (s.length < 4) s = '0' + s;
      return s;
    }

    function speedText(value){
      var n = parseInt(value || 0, 10);
      if(n === 1) return t('usbSpeedLow','Low');
      if(n === 2) return t('usbSpeedFull','Full');
      if(n === 3) return t('usbSpeedHigh','High');
      if(n === 5) return t('usbSpeedSuper','Super');
      return t('usbSpeedUnknown','Unknown');
    }

    function loadDevices(){
      return fetch('/api/usb-server-devices', {cache:'no-store'})
        .then(function(r){ return r.json(); })
        .then(function(j){
          if(!j || j.success !== true || !j.devices){
            devicesEl.textContent = t('usbServerDevicesError','Failed to get USB devices.');
            return;
          }
          if(!j.devices.length){
            devicesEl.textContent = t('usbServerDevicesNone','No USB-CDC devices connected.');
            return;
          }

          var lines = [];
          var i;
          for(i = 0; i < j.devices.length; i++){
            var d = j.devices[i] || {};
            var desc = d.description ? ' | ' + d.description : '';
            var line =
              t('usbServerDevSlot','Slot') + ' ' + String(d.slot || 0) +
              ' | ' + t('usbServerDevAddr','Addr') + ' ' + String(d.dev_addr || 0) +
              ' | ' + t('usbServerDevSpeed','Speed') + ' ' + speedText(d.speed) +
              ' | VID:PID ' + hex4(d.vid) + ':' + hex4(d.pid) +
              ' | ' + t('usbServerDevBcd','BCD') + ' 0x' + hex4(d.bcd_device) +
              ' | IF ' + String(d.iface_class || 0) + '/' + String(d.iface_subclass || 0) + '/' + String(d.iface_protocol || 0) +
              ' | IN 0x' + hex2(d.in_ep_addr) + ' OUT 0x' + hex2(d.out_ep_addr) +
              ' | RX ' + String(d.rx_bytes || 0) + ' B TX ' + String(d.tx_bytes || 0) + ' B' + desc;
            lines.push(line);
          }
          devicesEl.textContent = lines.join('\n');
        })
        .catch(function(){
          devicesEl.textContent = t('usbServerDevicesError','Failed to get USB devices.');
        });
    }

    document.getElementById('usb_server_save').addEventListener('click', function(){
      var btn=this;disableBtn(btn);showLoading();
      var fd = new FormData();
      fd.append('_html', '1');
      fd.append('enabled', enabledEl.checked ? '1' : '0');
      fd.append('port', String(parseInt(portEl.value || '3240', 10)));
      fd.append('recv_timeout_ms', String(parseInt(recvEl.value || '1200', 10)));
      fd.append('in_wait_timeout_ms', String(parseInt(inWaitEl.value || '220', 10)));

      fetch('/api/usb-server-config', {method:'POST', body:fd})
        .then(function(r){ return r.json(); })
        .then(function(j){
          if(j && j.success){
            msgEl.textContent = t('usbServerSaveOk','Configuration saved.');
            notify(t('alertSaved','✅ Configuration saved!'));
            if(typeof showSavedCard==='function') showSavedCard();
            loadStatus();
            loadDevices();
            return;
          }
          var errMsg = t('usbServerSaveErr','Error saving configuration.');
          if(j && j.message){
            if(j.message === 'porta_invalida') errMsg = t('usbServerSaveErrPort','Invalid TCP port (1-65535).');
            else if(j.message === 'recv_timeout_invalido') errMsg = t('usbServerSaveErrRecv','Invalid RX timeout (100-60000 ms).');
            else if(j.message === 'in_wait_timeout_invalido') errMsg = t('usbServerSaveErrInWait','Invalid IN timeout (20-5000 ms).');
            else if(j.message === 'erro_guardar_littlefs') errMsg = t('usbServerSaveErrLittleFS','Storage error while saving.');
          }
          msgEl.textContent = errMsg;
          notify(t('alertSaveError','❌ Error saving'));
        })
        .catch(function(e){
          msgEl.textContent = t('usbServerRequestFail','Request failed.');
          notify(t('emailErrorPrefix','Error: ') + (e && e.message ? e.message : e));
        })
        .finally(function(){hideLoading();enableBtn(btn);});
    });

    loadConfig().then(function(){
      loadStatus();
      loadDevices();
    }).catch(function(){
      statusEl.textContent = t('usbServerStatusError','Failed to get USB Server status.');
      devicesEl.textContent = t('usbServerDevicesError','Failed to get USB devices.');
    });
    setInterval(function(){
      loadStatus();
      loadDevices();
    }, 6000);
  })();
  </script>
</div>
)HTML";
