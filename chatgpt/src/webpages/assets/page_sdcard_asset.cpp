#include "page_sdcard_asset.h"

const char SDCARD_COMMANDS_SECTION[] PROGMEM = R"HTML(
        <!-- 📦 Section 3: EMAILDAY Commands -->
        <div style='margin-top:20px;border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a'>
          <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='sdCommandsTitle'>⌨️ Commands:</h2>
          <div style='margin-bottom:12px;padding:8px 12px;background:#064e3b;border-left:4px solid #10b981;border-radius:4px;font-size:12px;color:#d1fae5' data-i18n='sdCommandsHint'>
            💡 These commands are for use on the command sending pages, sending the command to the system.
          </div>
          <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:12px 0;font-size:13px;color:#e0e7ff;border-radius:4px'>
            <div style='display:flex;align-items:flex-start;gap:16px;flex-wrap:wrap'>
              <div style='flex:1;min-width:180px'>
                <strong style='color:#60a5fa'>📧 EMAILDAY</strong> — <span data-i18n='sdCmdEmaildayDesc'>Sends current day files by email</span>
                <div style='margin-top:4px;color:#cbd5e1;font-size:12px'>Example: <code style='background:#0f172a;padding:2px 6px;border-radius:3px;color:#38bdf8'>emailday</code></div>
              </div>
              <div style='flex:1;min-width:180px'>
                <strong style='color:#60a5fa'>📅 EMAILDAY [date]</strong> — <span data-i18n='sdCmdEmaildayDateDesc'>Sends files from specific date</span>
                <div style='margin-top:4px;color:#cbd5e1;font-size:12px'>Example: <code style='background:#0f172a;padding:2px 6px;border-radius:3px;color:#38bdf8'>emailday 29-12-2025</code></div>
              </div>
            </div>
            <div style='display:flex;align-items:flex-start;gap:16px;flex-wrap:wrap;margin-top:12px;padding-top:10px;border-top:1px solid #2d4a8a'>
              <div style='flex:1;min-width:180px'>
                <strong style='color:#a78bfa'>📋 LOGDAY</strong> — <span data-i18n='sdCmdLogdayDesc'>Sends current day log by email</span>
                <div style='margin-top:4px;color:#cbd5e1;font-size:12px'>Example: <code style='background:#0f172a;padding:2px 6px;border-radius:3px;color:#38bdf8'>logday</code></div>
              </div>
              <div style='flex:1;min-width:180px'>
                <strong style='color:#a78bfa'>📋 LOGDAY [date]</strong> — <span data-i18n='sdCmdLogdayDateDesc'>Sends log from specific date</span>
                <div style='margin-top:4px;color:#cbd5e1;font-size:12px'>Example: <code style='background:#0f172a;padding:2px 6px;border-radius:3px;color:#38bdf8'>logday 28-02-2026</code></div>
              </div>
            </div>
            <div style='display:flex;align-items:flex-start;gap:16px;flex-wrap:wrap;margin-top:12px;padding-top:10px;border-top:1px solid #2d4a8a'>
              <div style='flex:1;min-width:180px'>
                <strong style='color:#34d399'>🌡️ SENSORDAY</strong> — <span data-i18n='sdCmdSensordayDesc'>Sends today's sensor history by email</span>
                <div style='margin-top:4px;color:#cbd5e1;font-size:12px'>Example: <code style='background:#0f172a;padding:2px 6px;border-radius:3px;color:#38bdf8'>sensorday</code></div>
              </div>
              <div style='flex:1;min-width:180px'>
                <strong style='color:#34d399'>🌡️ SENSORDAY [date]</strong> — <span data-i18n='sdCmdSensordayDateDesc'>Sends sensor history from specific date</span>
                <div style='margin-top:4px;color:#cbd5e1;font-size:12px'>Example: <code style='background:#0f172a;padding:2px 6px;border-radius:3px;color:#38bdf8'>sensorday 01-03-2026</code></div>
              </div>
            </div>

          </div>
        </div>
)HTML";

const char PAGE_SDCARD[] PROGMEM = R"HTML(
<div class='page sdcard'>
 <div class='wrap'>
        <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='sdTipHome'>&#127968; Home</a></div>
        <h1 data-i18n='sdPageTitle'>&#128190; Configurar Armazenamento</h1>
        
        <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:16px'>
          <div id='sd_info' class='info-item mono' data-i18n='sdGettingStatus'>Getting status...</div>
          <div style='margin-top:14px;display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;max-width:400px;margin-left:auto;margin-right:auto'>
                <button type='button' class='btn' id='sd_mount' style='background:#2563eb;color:white;padding:10px 16px;font-size:13px;border:1px solid #2563eb;white-space:nowrap' data-i18n='sdMountBtn' title='Mount SD card' data-i18n-title='sdTipMount'>&#128194; Mount</button>
                <button type='button' class='btn' id='sd_unmount' style='background:#1e40af;color:white;padding:10px 16px;font-size:13px;border:1px solid #1e40af;white-space:nowrap' data-i18n='sdUnmountBtn' title='Unmount SD card' data-i18n-title='sdTipUnmount'>&#128228; Unmount</button>
                <button type='button' class='btn' id='sd_refresh' style='background:#3b82f6;color:white;padding:10px 16px;font-size:13px;border:1px solid #3b82f6;white-space:nowrap' data-i18n='sdRefreshBtn' title='Refresh SD status' data-i18n-title='sdTipRefresh'>&#128260; Refresh</button>
          </div>
          </div>
        
        <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
          <h2 style='margin-top:0;font-size:18px;color:#60a5fa;text-align:left' data-i18n='sdSavingTitle'>&#9881;&#65039; Save received data (microSD / Flash / PSRAM):</h2>
          
          <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:12px 0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px;text-align:left'>
            <div data-i18n-html='sdInfoBuf'>&#9203; <strong style='color:#60a5fa'>Buffer write:</strong> Waits for an idle period and writes in bulk (saves SD card writes). Writes earlier if the buffer reaches the limit.</div>
            <div data-i18n-html='sdInfoActive'>&#10004;&#65039; <strong style='color:#60a5fa'>Active:</strong> <strong style='color:#10b981'>Yes</strong> = Saves data to storage (SD or PSRAM, as available). <strong style='color:#ef4444'>No</strong> = Discards data after use (emails/alerts/recipients).</div>
            <div data-i18n-html='sdInfoFormat'>&#128196; <strong style='color:#60a5fa'>Format:</strong> File type (.txt with timestamps, .csv comma-separated, .bin pure binary).</div>
            <div data-i18n-html='sdInfoEmail'>&#128231; <strong style='color:#60a5fa'>Email:</strong> If active, sends data line by email according to recipients/alerts (in addition to saving if Active=Yes).</div>
            <div style='margin-top:8px;padding-top:8px;border-top:1px solid #3b82f6;text-align:left' data-i18n-html='sdInfoModes'><strong style='color:#60a5fa'>&#128190; Where it is stored:</strong> <strong>microSD</strong> = persistent (when the card is mounted) · <strong>Flash (LittleFS)</strong> = persistent without the card, with quota and automatic cleanup, never deletes the configuration in <strong>/config</strong> · <strong>PSRAM</strong> = <strong style='color:#fbbf24' data-i18n='storageVolatile'>volatile</strong>, only temporary (G-code, buffers).</div>
            <div style='margin-top:8px;padding-top:8px;border-top:1px solid #3b82f6;text-align:left' data-i18n-html='sdInfoPsram'><strong style='color:#fbbf24'>&#9888;&#65039; PSRAM Mode:</strong> Data stored in PSRAM is <strong>volatile</strong> (lost on reboot/power-off). Use only as temporary fallback when SD is not available.</div>
            <div style='margin-top:8px;padding-top:8px;border-top:1px solid #3b82f6;text-align:left' data-i18n-html='sdInfoFolder'><strong style='color:#10b981'>&#2139;&#65039; Folder name:</strong> The folder name where data is saved is based on the <strong>System Name</strong> configured in the System Settings page (format: "SystemName_DD-MM-YYYY").</div>
          </div>
          
          <div class='table-wrapper'>
          <table class='table' style='width:100%;margin-top:12px;border-collapse:collapse'>
           <thead>
            <tr style='background:#f0f0f0;border-bottom:2px solid #666'>
             <th style='padding:8px;text-align:center;font-weight:bold' data-i18n='sdThBuffer'>Buffer write:</th>
             <th style='padding:8px;text-align:center;font-weight:bold' data-i18n='sdThActive'>Active:</th>
             <th style='padding:8px;text-align:center;font-weight:bold' data-i18n='sdThFormat'>Format:</th>
             <th style='padding:8px;text-align:center;font-weight:bold' data-i18n='sdThEmail'>Email:</th>
            </tr>
           </thead>
           <tbody>
            <tr style='border-bottom:1px solid #ddd;background:#fafafa;height:20px'></tr>
            <tr style='border-bottom:1px solid #ddd'>
              <td style='padding:8px;text-align:center'><select id='sd_write_pause_ms' style='width:80px;padding:6px;border:1px solid #ccc;border-radius:4px;font-size:13px' title='Buffer write interval' data-i18n-title='sdTipWritePause'><option value='600000'>10m</option><option value='1800000' selected>30m</option><option value='3600000'>60m</option></select></td>
             <td style='padding:8px;text-align:center'><select id='sd_active' style='width:80px;padding:6px;border:1px solid #ccc;border-radius:4px;font-size:13px' title='Enable data saving' data-i18n-title='sdTipActive'><option value='1' selected>Yes</option><option value='0'>No</option></select></td>
             <td style='padding:8px;text-align:center'><select id='sd_format' style='width:80px;padding:6px;border:1px solid #ccc;border-radius:4px;font-size:13px' title='File output format' data-i18n-title='sdTipFormat'><option value='txt'>.txt</option><option value='csv' selected>.csv</option><option value='bin'>.bin</option></select></td>
             <td style='padding:8px;text-align:center'><select id='sd_email' style='width:80px;padding:6px;border:1px solid #ccc;border-radius:4px;font-size:13px' title='Send data by email' data-i18n-title='sdTipEmailReport'><option value='1' selected>Yes</option><option value='0'>No</option></select></td>
            </tr>
           </tbody>
          </table>
          </div>
        </div>

        <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
          <h2 style='margin-top:0;font-size:18px;color:#60a5fa;text-align:left' data-i18n='sdAutoCleanupTitle'>&#128465;&#65039; Auto Cleanup:</h2>
           <div class='table-wrapper'>
           <table class='table' style='width:100%;margin-top:12px;border-collapse:collapse'>
            <thead>
             <tr style='background:#f0f0f0;border-bottom:2px solid #666'>
               <th style='padding:8px;text-align:center;font-weight:bold' data-i18n='sdThCleanupThreshold'>Clean when &gt;:</th>
               <th style='padding:8px;text-align:center;font-weight:bold' data-i18n='sdThKeepDays'>Keep (days):</th>
             </tr>
            </thead>
            <tbody>
             <tr style='border-bottom:1px solid #ddd;background:#fafafa;height:20px'></tr>
             <tr style='border-bottom:1px solid #ddd'>
              <td style='padding:8px;text-align:center'><select id='sd_cleanup_threshold' style='width:80px;padding:6px;border:1px solid #ccc;border-radius:4px;font-size:13px' title='Cleanup trigger level' data-i18n-title='sdTipCleanupThreshold'><option value='50'>50%</option><option value='60'>60%</option><option value='70' selected>70%</option><option value='80'>80%</option><option value='85'>85%</option><option value='90'>90%</option><option value='95'>95%</option></select></td>
              <td style='padding:8px;text-align:center'><select id='sd_keep_days' style='width:80px;padding:6px;border:1px solid #ccc;border-radius:4px;font-size:13px' title='Days to keep data' data-i18n-title='sdTipKeepDays'><option value='0' selected>&#8734;</option><option value='30'>30</option><option value='90'>90</option><option value='180'>180</option><option value='360'>360</option></select></td>
             </tr>
            </tbody>
           </table>
           </div>
           <div style='margin-top:10px;padding:8px 12px;background:#1e3a8a;border-left:4px solid #60a5fa;border-radius:4px;font-size:12px;color:#cbd5e1;text-align:left' data-i18n-html='sdAutoCleanupNote'>
             &#128161; Automatically deletes the oldest data folders when storage exceeds the limit. <strong>Keep (days)</strong>: does not delete data newer than N days (&#8734; = no limit).
           </div>
        </div>

     )HTML";

const char PAGE_SDCARD_PART2[] PROGMEM = R"HTML(
        <!--&#128190; Save Button -->
        <div class='actions' style='margin-top:16px;display:flex;gap:8px;justify-content:center'>
         <button type='button' class='btn' id='sd_save' style='background:#20a3b9;padding:10px 24px;font-size:14px;border:1px solid #20a3b9' data-i18n='btnSave' title='Save SD configuration' data-i18n-title='sdTipSave'>&#128190; Save</button>
        </div>
        <div id='sd_config_msg' class='desc' style='margin-top:12px;text-align:center'></div>
 </div>
</div>
<script>
function t(k,fb){try{if(typeof window._t==='function')return window._t(k,fb);var raw=localStorage.getItem('ui_lang_cache');if(raw){var p=JSON.parse(raw);if(p&&p[k]!==undefined)return p[k];}}catch(e){}return fb!==undefined?fb:k;}
function initSdcardPage() {
  function byId(id) { return document.getElementById(id); }
  function refreshStatus() {
    return apiGet('/api/sdcard-config')
      .then(function(data) {
        if (!data || data.success !== true) return;
        var info = byId('sd_info');
        if (info) {
          var parts = [];
          function fmtMB(b){return (b/1024/1024).toFixed(1)+' MB';}
          if (data.storage_mode !== undefined) parts.push(data.storage_mode);
          if (data.mounted !== undefined) parts.push(t('sdMounted','Mounted: ') + (data.mounted ? t('sdYes','Yes') : t('sdNo','No')));
          if (data.total_mb !== undefined) parts.push(t('sdTotal','Total: ') + data.total_mb + ' MB');
          if (data.free_mb !== undefined) parts.push(t('sdFree','Free: ') + data.free_mb + ' MB');
          if (data.flash_total_bytes !== undefined) parts.push('Flash: ' + fmtMB(data.flash_used_bytes) + '/' + fmtMB(data.flash_total_bytes));
          info.textContent = parts.join(' | ');
        }
        function setVal(id, val) {
          var el = byId(id);
          if (el && val !== undefined) el.value = String(val);
        }
        setVal('sd_write_pause_ms', data.write_pause_ms);
        setVal('sd_active', data.active);
        setVal('sd_format', data.format);
        setVal('sd_email', data.email);
        setVal('sd_cleanup_threshold', data.cleanup_threshold);
        setVal('sd_keep_days', data.keep_days);
      })
      .catch(function() {});
  }
  bindClick('sd_refresh', function() { refreshStatus(); });
  bindClick('sd_mount', function() {
    apiPost('/api/sdcard-config-save', { mount: 1 })
      .then(function(data) {
        if (data && data.success) customAlert('✅ ' + (data.message || t('sdMountedStatus','Mounted')));
        else customAlert('❌ ' + (data && data.message ? data.message : t('sdErrGeneric','Error')));
        refreshStatus();
      })
      .catch(function() { customAlert('❌ ' + t('sdErrMount','Error mounting')); });
  });
  bindClick('sd_unmount', function() {
    customConfirm(t('sdUnmountConfirm', 'Unmount the SD card?')).then(function(ok) {
      if (!ok) return;
      apiPost('/api/sdcard-config-save', { unmount: 1 })
        .then(function(data) {
          if (data && data.success) customAlert('✅ ' + (data.message || t('sdUnmounted','Unmounted')));
          else customAlert('❌ ' + (data && data.message ? data.message : t('sdErrGeneric','Error')));
          refreshStatus();
        })
        .catch(function() { customAlert('❌ ' + t('sdErrUnmount','Error unmounting')); });
    });
  });
  bindClick('sd_save', function() {
    var btn=this;disableBtn(btn);showLoading();
    var payload = {
      write_pause_ms: byId('sd_write_pause_ms') ? byId('sd_write_pause_ms').value : '1800000',
      active: byId('sd_active') ? byId('sd_active').value : '1',
      format: byId('sd_format') ? byId('sd_format').value : 'csv',
      email: byId('sd_email') ? byId('sd_email').value : '1',
      cleanup_threshold: byId('sd_cleanup_threshold') ? byId('sd_cleanup_threshold').value : '70',
      keep_days: byId('sd_keep_days') ? byId('sd_keep_days').value : '0'
    };
    apiPost('/api/sdcard-config-save', payload)
      .then(function(data) {
        if (data && data.success) customAlert('✅ ' + t('sdConfigSaved','Configuration saved!'));
        else customAlert('❌ ' + (data && data.message ? data.message : t('sdErrGeneric','Error')));
        if (typeof showSavedCard === 'function') showSavedCard();
      })
      .catch(function() { customAlert('❌ ' + t('sdErrSaving','Error saving')); })
      .finally(function(){hideLoading();enableBtn(btn);});
  });
  refreshStatus();
}
</script>
)HTML";