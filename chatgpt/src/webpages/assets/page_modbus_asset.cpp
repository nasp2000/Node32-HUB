#include "page_modbus_asset.h"

// ---- Modbus RTU Config Page ----------------------------------------
// RTU serial settings + RTU devices/registers. TCP devices preserved on save.
// JS filters to RTU devices only (isTcp=false); save merges TCP back from cache.

const char PAGE_MODBUS_RTU[] PROGMEM = R"HTML(
<main class='page'>
 <div class='wrap'>
    <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='modbusTipHome'>&#127968; Home</a></div>
    <div style='display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-bottom:4px'><a href='/modbus-live' style='font-size:13px;color:#60a5fa;text-decoration:none'>&#128200; Live</a> <span style='color:#475569'>|</span> <a href='/modbus-tcp' style='font-size:13px;color:#67e8f9;text-decoration:none'>&#127760; TCP</a></div>
    <h1>&#9881;&#65039; Modbus RTU</h1>

   <!-- Global RTU Settings -->
   <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:16px'>
    <h2 data-sec='rtuConfig' style='margin-top:0;color:#60a5fa' data-i18n='mbGlobalTitle'>🔌 Global Configuration</h2>
    <div style='display:flex;flex-wrap:wrap;gap:20px;margin-bottom:16px'>
     <label style='display:flex;align-items:center;gap:8px;font-weight:bold;cursor:pointer' title='Enable or disable Modbus globally' data-i18n-title='modbusTipSlaveId'>
      <input type='checkbox' id='mb_enabled' title='Enable or disable Modbus globally' data-i18n-title='modbusTipSlaveId'> Modbus Ativo / Active
     </label>
    <label style='display:flex;align-items:center;gap:8px;font-weight:bold;cursor:pointer' title='Enable RTU mode using UART1 on GPIO40 (TX) / GPIO41 (RX). Incompatible with DB25 and Sniffer.'>
     <input type='checkbox' id='mb_rtu_en'> RTU Ativo / Active (UART1 — GPIO40/41)
    </label>
   </div>
   <div style='display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px;align-items:end'>
    <label style='display:flex;flex-direction:column;gap:4px;font-size:13px' title='Serial baud rate. Must match the slave device. Common: 9600 or 19200.'>
     <span>Baud Rate</span>
     <span style='font-size:11px;color:#94a3b8' data-i18n='mbBaudLabel'>Serial speed</span>
      <select id='mb_baud' style='padding:6px;border:1px solid #3b82f6;border-radius:4px;background:#0a1929;color:#e0e7ff;height:36px' title='Serial baud rate' data-i18n-title='modbusTipBaud'>
      <option value='1200'>1200</option><option value='2400'>2400</option><option value='4800'>4800</option>
      <option value='9600'>9600</option><option value='19200'>19200</option><option value='38400'>38400</option>
      <option value='57600'>57600</option><option value='115200'>115200</option>
     </select>
    </label>
    <label style='display:flex;flex-direction:column;gap:4px;font-size:13px' title='Parity bit. Must match slave. N=None (most common), E=Even, O=Odd.'>
     <span>Paridade / Parity</span>
     <span style='font-size:11px;color:#94a3b8' data-i18n='mbParityHint'>N=None, E=Even, O=Odd</span>
      <select id='mb_parity' style='padding:6px;border:1px solid #3b82f6;border-radius:4px;background:#0a1929;color:#e0e7ff;height:36px' title='Parity checking mode' data-i18n-title='modbusTipParity'>
      <option value='N'>N — Nenhuma / None</option><option value='E'>E — Par / Even</option><option value='O' data-i18n='mbParityOdd'>O — Odd</option>
     </select>
    </label>
    <label style='display:flex;flex-direction:column;gap:4px;font-size:13px' title='Stop bits. Usually 1. Use 2 only if explicitly required by the device.'>
     <span>Stop Bits</span>
     <span style='font-size:11px;color:#94a3b8'>Bits de paragem</span>
      <select id='mb_stop' style='padding:6px;border:1px solid #3b82f6;border-radius:4px;background:#0a1929;color:#e0e7ff;height:36px' title='Stop bits per frame' data-i18n-title='modbusTipStopBits'>
      <option value='1'>1</option><option value='2'>2</option>
     </select>
    </label>
    <label style='display:flex;flex-direction:column;gap:4px;font-size:13px' title='GPIO pin connected to DE/RE of the RS-485 transceiver for direction control. Use -1 if your module has automatic direction control.'>
     <span>Pino DE / DE Pin</span>
     <span style='font-size:11px;color:#94a3b8' data-i18n='mbSlaveAutoHint'>-1 = auto-detect</span>
      <input type='number' id='mb_de' value='-1' min='-1' max='48' style='padding:6px;border:1px solid #3b82f6;border-radius:4px;background:#0a1929;color:#e0e7ff;height:36px;box-sizing:border-box' title='RS-485 DE/RE GPIO pin (-1=auto)' data-i18n-title='modbusTipRegAddr'>
    </label>
   </div>
  </div>

  <!-- Devices Section -->
  <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
   <div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;margin-bottom:14px'>
    <h2 style='margin:0;color:#60a5fa' data-sec='rtuDevices'>📊 Devices <span id='mb_dev_count' style='font-size:13px;color:#94a3b8'>(0/4)</span></h2>
     <button type='button' id='mb_add_dev' style='padding:7px 16px;background:#1d4ed8;color:white;border:none;border-radius:4px;cursor:pointer;font-size:13px' title='Add new RTU device' data-i18n-title='modbusTipSaveRtu'>➕ Add Device</button>
   </div>
   <div id='mb_devices'><div style='color:#94a3b8;font-size:13px;padding:10px 0'>Nenhum dispositivo configurado.</div></div>
  </div>

  <!-- Buttons -->
  <div style='margin-top:20px'>
    <button type='button' id='mb_save' style='padding:12px 24px;background:#2563eb;color:white;border:none;border-radius:6px;cursor:pointer;font-size:15px;font-weight:bold;width:100%;max-width:300px' title='Save RTU configuration' data-i18n-title='modbusTipSaveRtu'>💾 Save RTU</button>
  </div>
  <div id='mb_msg' style='margin-top:10px;font-size:13px;color:#94a3b8'></div>

  <!-- Scan RTU Section -->
  <div style='border:2px solid #7c3aed;border-radius:8px;padding:18px;background:#0f172a;margin-top:20px'>
   <h2 style='margin:0 0 14px 0;color:#a78bfa' data-sec='rtuScan'>🔍 RTU Scan — Detect Slaves</h2>
   <div style='display:flex;gap:12px;flex-wrap:wrap;align-items:flex-end;margin-bottom:14px'>
    <label style='flex:1;min-width:100px'>
     <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px' data-i18n='mbStartIdLabel'>Start ID</span>
      <input type='number' id='scan_start' value='1' min='1' max='247' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Scan start slave ID' data-i18n-title='modbusTipSlaveId'>
    </label>
    <label style='flex:1;min-width:100px'>
     <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px'>ID fim</span>
      <input type='number' id='scan_end' value='32' min='1' max='247' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Scan end slave ID' data-i18n-title='modbusTipSlaveId'>
    </label>
    <label style='flex:1;min-width:100px'>
     <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px'>Timeout (ms)</span>
      <input type='number' id='scan_timeout' value='300' min='50' max='2000' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Scan timeout in ms' data-i18n-title='modbusTipPollMs'>
    </label>
   </div>
   <div style='display:flex;gap:10px;flex-wrap:wrap;margin-bottom:14px'>
     <button type='button' id='scan_start_btn' onclick='mbScanStart()' style='padding:10px 22px;background:#7c3aed;color:white;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold' title='Scan RTU bus for devices' data-i18n-title='modbusTipScanRtu'>▶ Start Scan</button>
     <button type='button' id='scan_stop_btn' onclick='mbScanStop()' style='padding:10px 22px;background:#991b1b;color:white;border:none;border-radius:6px;cursor:pointer;font-size:14px;display:none' title='Stop scanning'>⏹ Stop</button>
   </div>
   <div id='scan_progress_wrap' style='display:none;margin-bottom:14px'>
    <div style='display:flex;justify-content:space-between;font-size:12px;color:#94a3b8;margin-bottom:4px'>
     <span>A varrer...</span>
     <span id='scan_pct'>0%</span>
    </div>
    <div style='background:#1e293b;border-radius:4px;overflow:hidden;height:10px'>
     <div id='scan_bar' style='height:10px;background:#7c3aed;width:0%;transition:width 0.3s'></div>
    </div>
   </div>
   <div id='scan_status_msg' style='font-size:13px;color:#94a3b8;margin-bottom:10px'></div>
   <div id='scan_results' style='display:none'>
    <h3 style='color:#a78bfa;font-size:15px;margin-bottom:8px'>Resultados</h3>
     <div class='table-wrapper'><table style='width:100%;border-collapse:collapse;font-size:13px' id='scan_table'>
      <thead><tr>
       <th style='text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #581c87'>ID Escravo</th>
       <th style='text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #581c87'>Reg 0 (FC03)</th>
       <th style='text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #581c87' data-i18n='mbColAction'>Action</th>
      </tr></thead>
      <tbody id='scan_tbody'></tbody>
     </table></div>
   </div>
  </div>
 </div>
</main>

<script>
var _mbFull = null;
var _mbCfg = {devices:[]};
var _fcOpts = [{v:1,l:'01 Read Coils'},{v:2,l:'02 Read Discrete'},{v:3,l:'03 Read Holding'},{v:4,l:'04 Read Input'},{v:5,l:'05 Write Coil'},{v:6,l:'06 Write Reg'}];
var _typeOpts = [{v:0,l:'UINT16'},{v:1,l:'INT16'},{v:2,l:'UINT32'},{v:3,l:'INT32'},{v:4,l:'FLOAT32'},{v:5,l:'BIT'}];
var _orderOpts = [{v:0,l:'ABCD (Big-Endian)'},{v:1,l:'CDAB (Word-swap)'},{v:2,l:'BADC (Byte-swap)'},{v:3,l:'DCBA (Little-Endian)'}];

function _esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
function _sel(opts,cur){return opts.map(function(o){return '<option value="'+o.v+'"'+(o.v==cur?' selected':'')+'>'+o.l+'</option>';}).join('');}

function _renderReg(di,ri,r){
 var is32=(r.type>=2&&r.type<=4),isBit=(r.type===5);
  var orderSel=is32?('<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Ordem<select id="r_'+di+'_'+ri+'_order" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:12px" title="Byte order for 32-bit" data-i18n-title="modbusTipRegAddr">'+_sel(_orderOpts,r.order||0)+'</select></label>'):'';
  var bitIn=isBit?('<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Bit<input type="number" id="r_'+di+'_'+ri+'_bit" value="'+(r.bit||0)+'" min="0" max="15" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;width:60px;font-size:12px" title="Bit position (0-15)" data-i18n-title="modbusTipRegAddr"></label>'):'';
  return '<div id="reg_'+di+'_'+ri+'" style="border:1px solid #1e3a8a;border-radius:4px;padding:10px;margin-bottom:8px;background:#0a1929">'
   +'<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">'
   +'<strong style="color:#93c5fd;font-size:13px">Registo '+(ri+1)+'</strong>'
   +'<button type="button" onclick="mbRemReg('+di+','+ri+')" style="background:#7f1d1d;color:white;border:none;border-radius:3px;padding:2px 8px;cursor:pointer;font-size:12px">✕</button>'
   +'</div>'
   +'<div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:8px">'
   +'<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Address<input type="number" id="r_'+di+'_'+ri+'_addr" value="'+(r.addr||0)+'" min="0" max="65535" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:12px" title="Register start address" data-i18n-title="modbusTipRegAddr"></label>'
   +'<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Function<select id="r_'+di+'_'+ri+'_fc" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:12px" title="Modbus function code" data-i18n-title="modbusTipWrite">'+_sel(_fcOpts,r.fc||3)+'</select></label>'
   +'<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Tipo<select id="r_'+di+'_'+ri+'_type" onchange="mbRefreshReg('+di+','+ri+')" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:12px" title="Register data type" data-i18n-title="modbusTipWrite">'+_sel(_typeOpts,r.type||0)+'</select></label>'
   +orderSel+bitIn
   +'<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Label<input type="text" id="r_'+di+'_'+ri+'_label" value="'+_esc(r.label||'Reg')+'" maxlength="31" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:12px" title="Register display name" data-i18n-title="modbusTipRegAddr"></label>'
   +'<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Unidade<input type="text" id="r_'+di+'_'+ri+'_unit" value="'+_esc(r.unit||'')+'" maxlength="7" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:12px" title="Engineering unit" data-i18n-title="modbusTipRegAddr"></label>'
   +'<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Scale<input type="number" id="r_'+di+'_'+ri+'_scale" value="'+(r.scale!==undefined?r.scale:1)+'" step="any" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:12px" title="Value multiplier" data-i18n-title="modbusTipRegAddr"></label>'
   +'<label style="font-size:11px;display:flex;flex-direction:column;gap:2px">Offset<input type="number" id="r_'+di+'_'+ri+'_offset" value="'+(r.offset||0)+'" step="any" style="padding:3px;border:1px solid #1e40af;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:12px" title="Value offset added after scale" data-i18n-title="modbusTipRegAddr"></label>'
   +'<label style="font-size:11px;display:flex;align-items:center;gap:5px;padding-top:14px"><input type="checkbox" id="r_'+di+'_'+ri+'_log"'+(r.log?' checked':'')+' title="Log to SD card" data-i18n-title="modbusTipResetStats"> Log SD</label>'
   +'<label style="font-size:11px;display:flex;align-items:center;gap:5px;padding-top:14px"><input type="checkbox" id="r_'+di+'_'+ri+'_pretty"'+(r.pretty!==false?' checked':'')+' title="Show formatted value" data-i18n-title="modbusTipResetStats">Readable</label>'
   +'</div></div>';
}

function _renderDev(di,d){
 var regsHtml=(d.regs||[]).map(function(r,ri){return _renderReg(di,ri,r);}).join('');
 return '<div id="dev_'+di+'" style="border:1px solid #2563eb;border-radius:6px;padding:14px;margin-bottom:14px;background:#0c1e35">'
  +'<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">'
  +'<h3 style="margin:0;color:#60a5fa;font-size:15px">'+_esc(d.name||('Device '+(di+1)))+'</h3>'
  +'<button type="button" onclick="mbRemDev('+di+')" style="background:#7f1d1d;color:white;border:none;border-radius:3px;padding:3px 10px;cursor:pointer;font-size:13px">✕</button>'
  +'</div>'
  +'<div style="display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end;margin-bottom:12px">'
   +'<label style="display:flex;flex-direction:column;align-items:flex-start;gap:3px;font-size:12px" title="Enable/disable this device without removing it" data-i18n-title="modbusTipSlaveId">'
   +'<span style="color:#94a3b8">Ativo / Active</span>'
   +'<input type="checkbox" id="d_'+di+'_en"'+(d.enabled?' checked':'')+' style="width:16px;height:16px;accent-color:#2563eb;margin:4px 0 2px 2px">'
   +'</label>'
   +'<label style="display:flex;flex-direction:column;gap:3px;font-size:12px" title="Device slave ID (1-247)" data-i18n-title="modbusTipSlaveId">'
   +'<span style="color:#94a3b8" data-i18n='mbSlaveAddrLabel'>Slave ID / Address</span>'
   +'<input type="number" id="d_'+di+'_sid" value="'+(d.slaveId||1)+'" min="1" max="247" style="width:80px;padding:5px;border:1px solid #2563eb;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:13px">'
   +'</label>'
   +'<label style="display:flex;flex-direction:column;gap:3px;font-size:12px" title="Friendly name for Live page" data-i18n-title="modbusTipRegAddr">'
   +'<span style="color:#94a3b8">Nome / Name</span>'
   +'<input type="text" id="d_'+di+'_name" value="'+_esc(d.name||'')+'" maxlength="31" placeholder="ex: Motor" style="width:130px;padding:5px;border:1px solid #2563eb;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:13px">'
   +'</label>'
   +'<label style="display:flex;flex-direction:column;gap:3px;font-size:12px" title="Poll interval in ms" data-i18n-title="modbusTipPollMs">'
   +'<span style="color:#94a3b8">Intervalo / Poll (ms)</span>'
   +'<input type="number" id="d_'+di+'_poll" value="'+(d.pollMs||5000)+'" min="500" style="width:90px;padding:5px;border:1px solid #2563eb;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:13px">'
   +'</label>'
   +'<label style="display:flex;flex-direction:column;gap:3px;font-size:12px" title="Response timeout in ms" data-i18n-title="modbusTipPollMs">'
   +'<span style="color:#94a3b8">Timeout (ms)</span>'
   +'<input type="number" id="d_'+di+'_tout" value="'+(d.timeoutMs||500)+'" min="100" max="10000" style="width:90px;padding:5px;border:1px solid #2563eb;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:13px">'
   +'</label>'
   +'<label style="display:flex;flex-direction:column;gap:3px;font-size:12px" title="Retry attempts on failure" data-i18n-title="modbusTipPollNow">'
   +'<span style="color:#94a3b8">Tentativas / Retries</span>'
   +'<input type="number" id="d_'+di+'_ret" value="'+(d.retries||3)+'" min="0" max="10" style="width:70px;padding:5px;border:1px solid #2563eb;border-radius:3px;background:#0f172a;color:#e0e7ff;font-size:13px">'
   +'</label>'
  +'</div>'
  +'<div id="regs_'+di+'">'+regsHtml+'</div>'
   +'<button type="button" onclick="mbAddReg('+di+')" style="padding:5px 14px;background:#1e3a8a;color:white;border:none;border-radius:3px;cursor:pointer;font-size:12px" title="Add new register" data-i18n-title="modbusTipWrite">➕ Add Register</button>'
  +'</div>';
}

function mbRefreshAll(){
 var c=document.getElementById('mb_devices');
 var devs=_mbCfg.devices||[];
 c.innerHTML=devs.length?devs.map(function(d,i){return _renderDev(i,d);}).join(''):'<div style="color:#94a3b8;font-size:13px;padding:10px 0">Nenhum dispositivo configurado.</div>';
 document.getElementById('mb_dev_count').textContent='('+devs.length+'/4)';
}

function mbRefreshReg(di,ri){
 var d=_mbCfg.devices[di];if(!d||!d.regs)return;
 var r=d.regs[ri];
 r.type=parseInt(document.getElementById('r_'+di+'_'+ri+'_type').value)||0;
 var el=document.getElementById('reg_'+di+'_'+ri);
 if(el)el.outerHTML=_renderReg(di,ri,r);
}

function mbAddReg(di){
 if(!_mbCfg.devices[di].regs)_mbCfg.devices[di].regs=[];
 if(_mbCfg.devices[di].regs.length>=16){customAlert('Max 16 registers per device');return;}
 _mbCfg.devices[di].regs.push({addr:0,fc:3,type:0,order:0,bit:0,label:'Reg',unit:'',scale:1,offset:0,log:false,pretty:true});
 mbRefreshAll();
}
function mbRemReg(di,ri){
 var r=_mbCfg.devices[di].regs[ri];
 if(!r)return;
 customConfirm('Remove register "'+(r.label||'Reg '+(ri+1))+'"?',function(ok){
  if(!ok)return;
  _mbCfg.devices[di].regs.splice(ri,1);mbRefreshAll();
 });
}

function mbAddDev(){
 if(!_mbCfg.devices)_mbCfg.devices=[];
 var tcpCount=_mbFull?(_mbFull.devices||[]).filter(function(d){return !!d.isTcp;}).length:0;
 if(_mbCfg.devices.length+tcpCount>=4){customAlert('Max 4 devices in total (RTU + TCP)');return;}
 _mbCfg.devices.push({slaveId:1,name:'RTU '+(_mbCfg.devices.length+1),enabled:true,isTcp:false,tcpHost:'',tcpPort:502,pollMs:5000,retries:3,timeoutMs:500,regs:[]});
 mbRefreshAll();
}
function mbRemDev(di){
 var dev=_mbCfg.devices[di];
 if(!dev)return;
 customConfirm('Remove device "'+(dev.name||'Device '+(di+1))+'"?',function(ok){
  if(!ok)return;
  _mbCfg.devices.splice(di,1);mbRefreshAll();
 });
}

function _gv(id){var e=document.getElementById(id);return e?e.value:'';}
function _gc(id){var e=document.getElementById(id);return e?e.checked:false;}

function mbCollectForm(){
 var out={
  enabled:_gc('mb_enabled'),rtu_enabled:_gc('mb_rtu_en'),
  rtu_baud:parseInt(_gv('mb_baud'))||9600,
  rtu_parity:_gv('mb_parity')||'N',
  rtu_stop_bits:parseInt(_gv('mb_stop'))||1,
  rtu_de_pin:parseInt(_gv('mb_de')),
  slave_id:(_mbFull?(_mbFull.slave_id||1):1),
  tcp_enabled:(_mbFull?!!_mbFull.tcp_enabled:false),
  devices:[]
 };
 (_mbCfg.devices||[]).forEach(function(d,di){
  var nd={
   slaveId:parseInt(_gv('d_'+di+'_sid'))||1,
   name:_gv('d_'+di+'_name')||'Dev',
   enabled:_gc('d_'+di+'_en'),isTcp:false,tcpHost:'',tcpPort:502,
   pollMs:parseInt(_gv('d_'+di+'_poll'))||5000,
   retries:parseInt(_gv('d_'+di+'_ret'))||3,
   timeoutMs:parseInt(_gv('d_'+di+'_tout'))||500,
   regs:[]
  };
  (d.regs||[]).forEach(function(r,ri){
   var tp=parseInt(_gv('r_'+di+'_'+ri+'_type'))||0,is32=(tp>=2&&tp<=4),isBit=(tp===5);
   nd.regs.push({
    addr:parseInt(_gv('r_'+di+'_'+ri+'_addr'))||0,
    fc:parseInt(_gv('r_'+di+'_'+ri+'_fc'))||3,
    type:tp,
    order:is32?(parseInt(_gv('r_'+di+'_'+ri+'_order'))||0):0,
    bit:isBit?(parseInt(_gv('r_'+di+'_'+ri+'_bit'))||0):0,
    label:_gv('r_'+di+'_'+ri+'_label')||'Reg',
    unit:_gv('r_'+di+'_'+ri+'_unit')||'',
    scale:parseFloat(_gv('r_'+di+'_'+ri+'_scale'))||1,
    offset:parseFloat(_gv('r_'+di+'_'+ri+'_offset'))||0,
    log:_gc('r_'+di+'_'+ri+'_log'),
    pretty:_gc('r_'+di+'_'+ri+'_pretty')
   });
  });
  out.devices.push(nd);
 });
 // Preserve TCP devices from cached full config
 (_mbFull?(_mbFull.devices||[]):[]).forEach(function(d){
  if(d.isTcp)out.devices.push(d);
 });
 return out;
}

function mbApplyCfg(cfg){
 _mbFull=cfg;
 _mbCfg={devices:(cfg.devices||[]).filter(function(d){return !d.isTcp;})};
 document.getElementById('mb_enabled').checked=!!cfg.enabled;
 document.getElementById('mb_rtu_en').checked=!!cfg.rtu_enabled;
 document.getElementById('mb_baud').value=(cfg.rtu_baud||9600).toString();
 document.getElementById('mb_parity').value=cfg.rtu_parity||'N';
 document.getElementById('mb_stop').value=(cfg.rtu_stop_bits||1).toString();
 document.getElementById('mb_de').value=(cfg.rtu_de_pin!==undefined?cfg.rtu_de_pin:-1).toString();
 mbRefreshAll();
}

function mbSave(){
 var btn=document.getElementById('mb_save');disableBtn(btn);showLoading();
 var data=mbCollectForm();
 fetch('/api/modbus/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
 .then(function(r){return r.json();})
 .then(function(j){
  if(j.success){customAlert('✅ Configuration saved!');if(typeof showSavedCard==='function')showSavedCard();}
  else customAlert('❌ Error: '+(j.error||'failed'));
 }).catch(function(e){customAlert('❌ '+e);}).finally(function(){hideLoading();enableBtn(btn);});
}

// ---- Scan RTU helpers ----
var _scanTimer=null;

function mbScanStart(){
 var s=parseInt(document.getElementById('scan_start').value)||1;
 var e=parseInt(document.getElementById('scan_end').value)||32;
 var t=parseInt(document.getElementById('scan_timeout').value)||300;
 document.getElementById('scan_start_btn').style.display='none';
 document.getElementById('scan_stop_btn').style.display='inline-block';
 document.getElementById('scan_progress_wrap').style.display='block';
 document.getElementById('scan_results').style.display='none';
 document.getElementById('scan_tbody').innerHTML='';
 document.getElementById('scan_status_msg').textContent='A iniciar scan...';
 fetch('/api/modbus/scan-rtu',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({startId:s,endId:e,timeoutMs:t})})
 .then(function(r){return r.json();})
 .then(function(j){
  if(!j.success){mbScanDone();customAlert('❌ '+(j.error||'Falhou'));return;}
  _scanTimer=setInterval(mbScanStatus,800);
 }).catch(function(ex){mbScanDone();customAlert('❌ '+ex);});
}

function mbScanStop(){
 fetch('/api/modbus/scan-stop',{method:'POST'}).catch(function(){});
 clearInterval(_scanTimer);_scanTimer=null;
 mbScanDone();
}

function mbScanDone(){
 document.getElementById('scan_start_btn').style.display='inline-block';
 document.getElementById('scan_stop_btn').style.display='none';
}

function mbScanStatus(){
  fetch('/api/modbus/scan-status',{cache:'no-store'}).then(function(r){return r.json();})
 .then(function(j){
  var pct=j.total>0?Math.round(j.current/j.total*100):0;
  document.getElementById('scan_bar').style.width=pct+'%';
  document.getElementById('scan_pct').textContent=pct+'%';
  document.getElementById('scan_status_msg').textContent=
   (j.running?'Scanning ID '+(j.current)+' of '+(j.total)+' ...':'Scan complete — '+j.found.length+' device(s) found.');
  // Render found rows
  if(j.found&&j.found.length){
   document.getElementById('scan_results').style.display='block';
   var html=j.found.map(function(f){
    return '<tr>'
     +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b">ID '+f.slaveId+'</td>'
     +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b;font-family:monospace;color:#34d399">'+f.reg0+'</td>'
     +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b">'
     +'<button type="button" onclick="mbAddDevFromScan('+f.slaveId+')" '
     +'style="padding:4px 10px;background:#1d4ed8;color:white;border:none;border-radius:4px;cursor:pointer;font-size:12px" title="Add this device">➕ Add</button>'
     +'</td></tr>';
   }).join('');
   document.getElementById('scan_tbody').innerHTML=html;
  }
  if(!j.running){
   clearInterval(_scanTimer);_scanTimer=null;
   document.getElementById('scan_progress_wrap').style.display='none';
   mbScanDone();
  }
 }).catch(function(){});
}

function mbAddDevFromScan(slaveId){
 var tcpCount=_mbFull?(_mbFull.devices||[]).filter(function(d){return !!d.isTcp;}).length:0;
 if((_mbCfg.devices||[]).length+tcpCount>=4){customAlert('Max 4 devices');return;}
 _mbCfg.devices.push({slaveId:slaveId,name:'Escravo '+slaveId,enabled:true,isTcp:false,tcpHost:'',tcpPort:502,pollMs:5000,retries:3,timeoutMs:500,regs:[]});
 mbRefreshAll();
}

function initModbusRtuPage(){
  document.getElementById('mb_add_dev').addEventListener('click',mbAddDev);
  document.getElementById('mb_save').addEventListener('click',mbSave);
  fetch('/api/modbus/config',{cache:'no-store'}).then(function(r){return r.json();}).then(mbApplyCfg)
 .catch(function(e){document.getElementById('mb_msg').textContent='Error loading: '+e;});
}
</script>
)HTML";

// ---- Modbus TCP Config Page -------------------------------------
// TCP device editor. Slave devices connect via IP:port over WiFi or Ethernet.
// Works independently of ENABLE_ETHERNET (uses WiFiClient).
// JS filters to TCP devices only (isTcp=true); save merges RTU back from cache.

const char PAGE_MODBUS_TCP[] PROGMEM = R"HTML(
<main class='page'>
 <div class='wrap'>
   <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='modbusTipHome'>&#127968; Home</a></div>
   <div style='display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-bottom:4px'><a href='/modbus-live' style='font-size:13px;color:#60a5fa;text-decoration:none'>&#128200; Live</a> <span style='color:#475569'>|</span> <a href='/modbus-rtu' style='font-size:13px;color:#94a3b8;text-decoration:none'>&#9881;&#65039; RTU</a></div>
   <h1>&#127760; Modbus TCP</h1>
   <p style='color:#67e8f9;font-size:13px;margin:8px 0 16px 0'>&#128225; Comunica via rede (WiFi ou Ethernet). Independente do barramento RS485.</p>
   <div id='mb_msg' style='font-size:13px;color:#f87171;margin-bottom:8px'></div>

   <!-- Global TCP toggle -->
   <div style='border:2px solid #0891b2;border-radius:8px;padding:18px;background:#0f172a;margin-top:16px'>
    <h2 style='margin:0 0 14px 0;color:#67e8f9' data-sec='tcpConfig'>&#128187; Global Configuration</h2>
    <label style='display:flex;align-items:center;gap:10px;cursor:pointer;margin-bottom:8px'>
     <input type='checkbox' id='mb_enabled' style='width:16px;height:16px;accent-color:#0891b2' title='Enable Modbus globally' data-i18n-title='modbusTipSlaveId'>
     <span style='font-size:14px;color:#e2e8f0'>Modbus ativo (global)</span>
    </label>
    <label style='display:flex;align-items:center;gap:10px;cursor:pointer'>
     <input type='checkbox' id='mb_tcp_en' style='width:16px;height:16px;accent-color:#0891b2' title='Enable Modbus TCP mode' data-i18n-title='modbusTipTcpIp'>
     <span style='font-size:14px;color:#e2e8f0'>Modbus TCP ativo</span>
    </label>
   </div>

  <!-- Register columns legend -->
  <details style='border:1px solid #0e7490;border-radius:8px;background:#0c1e35;margin-top:20px;padding:0'>
   <summary style='cursor:pointer;padding:12px 18px;color:#67e8f9;font-size:13px;font-weight:bold;list-style:none;display:flex;align-items:center;gap:8px'>&#9432; Legenda dos campos de registo <span style='font-size:11px;color:#64748b;font-weight:normal'>(clique para expandir)</span></summary>
   <div style='padding:0 18px 16px 18px'>
    <p style='font-size:12px;color:#94a3b8;margin:8px 0 12px 0'>Cada registo configura um valor a ler (ou escrever) no dispositivo escravo Modbus.</p>
     <div class='table-wrapper'><table style='width:100%;border-collapse:collapse;font-size:12px'>
      <thead><tr style='background:#0f172a'>
       <th style='text-align:left;padding:6px 10px;color:#60a5fa;border-bottom:1px solid #1e3a5f;white-space:nowrap'>Campo</th>
       <th style='text-align:left;padding:6px 10px;color:#60a5fa;border-bottom:1px solid #1e3a5f' data-i18n='mbHelpColDesc'>Description</th>
      </tr></thead>
      <tbody>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Addr</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b' data-i18n='mbHelpAddr'>Register address on the slave device (0–65535), per the equipment manual.</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>FC</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b' data-i18n='mbHelpFc'>Modbus function: <b>FC3</b>=Holding Registers (R/W), <b>FC4</b>=Input Registers (read-only), <b>FC1</b>=Coils (R/W bits), <b>FC2</b>=Discrete Inputs (read-only bits).</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Tipo</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b'>Formato do valor: <b>INT16</b>/UINT16=1 registo (16 bits), <b>INT32</b>/UINT32/<b>FLOAT32</b>=2 registos consecutivos (32 bits), <b>BIT</b>=um bit dentro de um registo.</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Order</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b'>Ordem dos bytes para tipos 32-bit. <b>ABCD</b>=Big-Endian (mais comum), <b>CDAB</b>=Word-swap, <b>BADC</b>=Byte-swap, <b>DCBA</b>=Little-Endian. Ignorado em tipos 16-bit.</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Bit</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b' data-i18n='mbHelpBit'>Only used with Type=BIT. Selects which of the 16 register bits matters (0=least significant bit, 15=most significant).</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Label</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b' data-i18n='mbHelpLabel'>Free name to identify the register on the Live page and in the SD card logs (e.g.: “Temperature”).</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Unit</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b' data-i18n='mbEngUnitHint'>Engineering unit shown in Live next to the value (e.g. &deg;C, %, hPa, V, A). Cosmetic only.</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Scale</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b' data-i18n='mbHelpScale'>Multiplier applied to the raw value. E.g.: 0.1 turns 253 → 25.3. Use 1 if the value is already in the right unit.</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Offset</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b' data-i18n='mbHelpOffset'>Value added to the result after scaling. Useful for zero corrections. Normally 0.</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;border-bottom:1px solid #1e293b;font-weight:bold'>Log</td><td style='padding:5px 10px;color:#cbd5e1;border-bottom:1px solid #1e293b' data-i18n='mbHelpLog'>If enabled, the value is stored in the SD card CSV file on each poll.</td></tr>
       <tr><td style='padding:5px 10px;color:#67e8f9;white-space:nowrap;font-weight:bold'>Pretty</td><td style='padding:5px 10px;color:#cbd5e1' data-i18n='mbHelpPretty'>If enabled, the value is shown on Live with decimals (e.g.: 25.3 °C). Disabled shows the raw integer.</td></tr>
      </tbody>
     </table></div>
   </div>
  </details>

  <!-- TCP Device List -->
  <div style='border:2px solid #0891b2;border-radius:8px;padding:18px;background:#0f172a;margin-top:20px'>
   <h2 style='margin:0 0 14px 0;color:#67e8f9' data-sec='tcpDevices'>&#128225; TCP Devices</h2>
   <p style='font-size:12px;color:#94a3b8;margin:0 0 14px 0' data-i18n='mbMax4Note'>Maximum 4 devices in total (RTU + TCP).</p>
   <div id='mb_devs_wrap'></div>
     <button type='button' id='mb_add_dev' style='margin-top:12px;padding:8px 18px;background:#0891b2;color:white;border:none;border-radius:6px;cursor:pointer;font-size:13px' title='Add new TCP device' data-i18n-title='modbusTipSaveTcp'>&#43; Add TCP Device</button>
  </div>

  <!-- Scan TCP Section -->
  <div style='border:2px solid #0891b2;border-radius:8px;padding:18px;background:#0f172a;margin-top:20px'>
   <h2 style='margin:0 0 14px 0;color:#67e8f9' data-sec='tcpScan'>&#128225; TCP Scan &mdash; Detect Slaves on the Network</h2>
   <p style='font-size:12px;color:#94a3b8;margin:0 0 14px 0'> data-i18n='mbScanSubnet'>Tries port 502 on each subnet IP. If open, tries FC43 to get the device name.</p>
   <div style='display:flex;gap:12px;flex-wrap:wrap;align-items:flex-end;margin-bottom:14px'>
    <label style='flex:2;min-width:120px'>
     <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px'>Subnet (ex: 192.168.1)</span>
      <input type='text' id='tcp_subnet' value='192.168.1' maxlength='15' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Network subnet prefix' data-i18n-title='modbusTipTcpIp'>
     </label>
     <label style='flex:1;min-width:80px'>
      <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px' data-i18n='mbIpStart'>Start IP</span>
       <input type='number' id='tcp_start' value='1' min='1' max='254' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Scan start IP' data-i18n-title='modbusTipTcpIp'>
     </label>
     <label style='flex:1;min-width:80px'>
      <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px'>IP fim</span>
       <input type='number' id='tcp_end' value='254' min='1' max='254' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Scan end IP' data-i18n-title='modbusTipTcpIp'>
     </label>
     <label style='flex:1;min-width:100px'>
      <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px'>Timeout (ms)</span>
       <input type='number' id='tcp_timeout' value='200' min='50' max='5000' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Scan timeout in ms' data-i18n-title='modbusTipPollMs'>
    </label>
   </div>
   <div style='display:flex;gap:10px;flex-wrap:wrap;margin-bottom:14px'>
     <button type='button' id='tcp_scan_btn' onclick='mbTcpScanStart()' style='padding:10px 22px;background:#0891b2;color:white;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold' title='Scan TCP subnet for devices' data-i18n-title='modbusTipScanTcp'>&#9654; Start TCP Scan</button>
     <button type='button' id='tcp_stop_btn' onclick='mbScanStop()' style='padding:10px 22px;background:#991b1b;color:white;border:none;border-radius:6px;cursor:pointer;font-size:14px;display:none' title='Stop scanning'>&#9209; Stop</button>
   </div>
   <div id='tcp_progress_wrap' style='display:none;margin-bottom:14px'>
    <div style='display:flex;justify-content:space-between;font-size:12px;color:#94a3b8;margin-bottom:4px'>
     <span>A varrer...</span><span id='tcp_pct'>0%</span>
    </div>
    <div style='background:#1e293b;border-radius:4px;overflow:hidden;height:10px'>
     <div id='tcp_bar' style='height:10px;background:#0891b2;width:0%;transition:width 0.3s'></div>
    </div>
   </div>
   <div id='tcp_status_msg' style='font-size:13px;color:#94a3b8;margin-bottom:10px'></div>
   <div id='tcp_results' style='display:none'>
    <h3 style='color:#67e8f9;font-size:15px;margin-bottom:8px'>Resultados</h3>
     <div class='table-wrapper'><table style='width:100%;border-collapse:collapse;font-size:13px'>
      <thead><tr>
       <th style='text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #0e7490'>IP</th>
       <th style='text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #0e7490' data-i18n='mbColIdent'>Identification (FC43)</th>
       <th style='text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #0e7490' data-i18n='mbColAction'>Action</th>
      </tr></thead>
      <tbody id='tcp_tbody'></tbody>
     </table></div>
   </div>
  </div>

  <!-- Save -->
  <div style='margin-top:20px'>
    <button type='button' id='mb_save' style='padding:12px 24px;background:#0891b2;color:white;border:none;border-radius:6px;cursor:pointer;font-size:15px;font-weight:bold;width:100%;max-width:300px' title='Save TCP configuration' data-i18n-title='modbusTipSaveTcp'>&#128190; Save TCP</button>
  </div>
 </div>
</main>
<script src='/auto.js'></script>
<script src='/config-manager.js'></script>
<script>
var _mbFull=null;
var _mbCfg={devices:[]};
var _scanTimer=null;

function _esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
function _gv(id){var e=document.getElementById(id);return e?e.value:'';}
function _gc(id){var e=document.getElementById(id);return e?e.checked:false;}

function _renderTcpDev(d,di){
 var typeOpts=['INT16','UINT16','INT32','UINT32','FLOAT32','COIL','DISCRETE','BIT'].map(function(t){
  return '<option value="'+t+'"'+(d.regs&&d.regs[0]&&d.regs[0].type===t?' selected':'')+'>'+t+'</option>';
 }).join('');
 var regsHtml=(d.regs||[]).map(function(r,ri){
  var is32=(r.type==='INT32'||r.type==='UINT32'||r.type==='FLOAT32');
  var isBit=(r.type==='BIT');
  var orderOpts=['ABCD','CDAB','BADC','DCBA'].map(function(o){return '<option value="'+o+'"'+(r.order===o?' selected':'')+'>'+o+'</option>';}).join('');
    return '<tr style="font-size:12px">'
     +'<td style="padding:3px 4px"><input type="number" id="d_'+di+'_r'+ri+'_addr" value="'+(r.addr||0)+'" min="0" max="65535" style="width:70px;padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Register start address" data-i18n-title="modbusTipRegAddr"></td>'
     +'<td style="padding:3px 4px"><select id="d_'+di+'_r'+ri+'_fc" style="padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Modbus function code" data-i18n-title="modbusTipWrite"><option value="1"'+(r.fc===1?' selected':'')+'>FC1</option><option value="2"'+(r.fc===2?' selected':'')+'>FC2</option><option value="3"'+(r.fc===3?' selected':'')+'>FC3</option><option value="4"'+(r.fc===4?' selected':'')+'>FC4</option></select></td>'
     +'<td style="padding:3px 4px"><select id="d_'+di+'_r'+ri+'_type" onchange="mbRefreshAll()" style="padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Register data type" data-i18n-title="modbusTipWrite">'+typeOpts+'</select></td>'
     +'<td style="padding:3px 4px"'+(is32?'':' style="display:none"')+' id="d_'+di+'_r'+ri+'_order_td"><select id="d_'+di+'_r'+ri+'_order" style="padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Byte order for 32-bit" data-i18n-title="modbusTipRegAddr">'+orderOpts+'</select></td>'
     +'<td style="padding:3px 4px"'+(isBit?'':' style="display:none"')+' id="d_'+di+'_r'+ri+'_bit_td"><input type="number" id="d_'+di+'_r'+ri+'_bit" value="'+(r.bit||0)+'" min="0" max="15" style="width:50px;padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Bit position (0-15)" data-i18n-title="modbusTipRegAddr"></td>'
      +'<td style="padding:3px 4px"><input type="text" id="d_'+di+'_r'+ri+'_label" value="'+_esc(r.label||'')+'" placeholder="Label" style="width:90px;padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Register display name" data-i18n-title="modbusTipRegAddr"></td>'
     +'<td style="padding:3px 4px"><input type="text" id="d_'+di+'_r'+ri+'_unit" value="'+(r.unit||'')+'" placeholder="Unit" style="width:50px;padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Engineering unit" data-i18n-title="modbusTipRegAddr"></td>'
     +'<td style="padding:3px 4px"><input type="number" id="d_'+di+'_r'+ri+'_scale" value="'+(r.scale||1)+'" step="0.01" style="width:60px;padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Value multiplier" data-i18n-title="modbusTipRegAddr"></td>'
     +'<td style="padding:3px 4px"><input type="number" id="d_'+di+'_r'+ri+'_offset" value="'+(r.offset||0)+'" step="0.01" style="width:60px;padding:4px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:3px" title="Value offset" data-i18n-title="modbusTipRegAddr"></td>'
     +'<td style="padding:3px 4px;text-align:center"><input type="checkbox" id="d_'+di+'_r'+ri+'_log"'+(r.log?' checked':'')+' style="width:14px;height:14px;accent-color:#0891b2" title="Log to SD card" data-i18n-title="modbusTipResetStats"></td>'
     +'<td style="padding:3px 4px;text-align:center"><input type="checkbox" id="d_'+di+'_r'+ri+'_pretty"'+(r.pretty?' checked':'')+' style="width:14px;height:14px;accent-color:#0891b2" title="Show formatted value" data-i18n-title="modbusTipResetStats"></td>'
     +'<td style="padding:3px 4px"><button type="button" onclick="mbRemReg('+di+','+ri+')" style="padding:3px 8px;background:#7f1d1d;color:white;border:none;border-radius:3px;cursor:pointer;font-size:11px" title="Remove register">X</button></td>'
    +'</tr>';
 }).join('');
 return '<div style="border:1px solid #0891b2;border-radius:6px;padding:14px;margin-bottom:12px;background:#1e293b">'
  +'<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">'
  +'<div style="display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end">'
   +'<label style="display:flex;flex-direction:column;align-items:flex-start;gap:3px;cursor:pointer;justify-content:flex-end;padding-bottom:2px">'
    +'<span style="font-size:11px;color:#94a3b8" title="Enable/disable this device" data-i18n-title="modbusTipSlaveId">Ativo / Active</span>'
    +'<input type="checkbox" id="d_'+di+'_en"'+(d.enabled?' checked':'')+' style="width:16px;height:16px;accent-color:#0891b2;margin:4px 0 2px 2px">'
    +'</label>'
    +'<label style="display:flex;flex-direction:column;gap:3px">'
     +'<span style="font-size:11px;color:#94a3b8" title="Friendly name for Live page" data-i18n-title="modbusTipRegAddr">Nome / Name</span>'
     +'<input type="text" id="d_'+di+'_name" value="'+_esc(d.name||'')+'" placeholder="ex: Unikon" style="width:130px;padding:6px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:4px;font-size:13px">'
    +'</label>'
    +'<label style="display:flex;flex-direction:column;gap:3px">'
    +'<span style="font-size:11px;color:#94a3b8" title="Modbus TCP device IP" data-i18n-title="modbusTipTcpIp">IP / Host IP</span>'
    +'<input type="text" id="d_'+di+'_host" value="'+(d.tcpHost||'')+'" placeholder="192.168.1.10" style="width:150px;padding:6px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:4px;font-size:13px">'
    +'</label>'
    +'<label style="display:flex;flex-direction:column;gap:3px">'
    +'<span style="font-size:11px;color:#94a3b8" title="Modbus TCP port (default 502)" data-i18n-title="modbusTipTcpPort">Porto / Port</span>'
    +'<input type="number" id="d_'+di+'_port" value="'+(d.tcpPort||502)+'" min="1" max="65535" style="width:80px;padding:6px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:4px;font-size:13px">'
    +'</label>'
    +'<label style="display:flex;flex-direction:column;gap:3px">'
    +'<span style="font-size:11px;color:#94a3b8" title="Poll interval in ms" data-i18n-title="modbusTipPollMs">Intervalo / Poll (ms)</span>'
    +'<input type="number" id="d_'+di+'_poll" value="'+(d.pollMs||5000)+'" min="500" max="60000" style="width:90px;padding:6px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:4px;font-size:13px">'
    +'</label>'
    +'<label style="display:flex;flex-direction:column;gap:3px">'
    +'<span style="font-size:11px;color:#94a3b8" title="Retry attempts on failure" data-i18n-title="modbusTipPollNow">Tentativas / Retries</span>'
    +'<input type="number" id="d_'+di+'_ret" value="'+(d.retries||3)+'" min="0" max="10" style="width:70px;padding:6px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:4px;font-size:13px">'
    +'</label>'
    +'<label style="display:flex;flex-direction:column;gap:3px">'
    +'<span style="font-size:11px;color:#94a3b8" title="Response timeout in ms" data-i18n-title="modbusTipPollMs">Timeout (ms)</span>'
    +'<input type="number" id="d_'+di+'_tout" value="'+(d.timeoutMs||500)+'" min="100" max="10000" style="width:90px;padding:6px;background:#0f172a;color:#f1f5f9;border:1px solid #334155;border-radius:4px;font-size:13px">'
    +'</label>'
  +'</div>'
   +'<button type="button" onclick="mbRemDev('+di+')" style="padding:5px 12px;background:#7f1d1d;color:white;border:none;border-radius:4px;cursor:pointer;font-size:12px" title="Remove device">Remove</button>'
  +'</div>'
  +(regsHtml?'<div class="table-wrapper"><table style="width:100%;border-collapse:collapse;margin-bottom:8px"><thead><tr>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Addr</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">FC</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Tipo</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Order</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Bit</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Label</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Unit</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Scale</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Offset</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Log</th>'
   +'<th style="font-size:11px;color:#60a5fa;padding:4px;text-align:left">Pretty</th>'
   +'<th></th>'
   +'</tr></thead><tbody>'+regsHtml+'</tbody></table></div>':'')
    +'<button type="button" onclick="mbAddReg('+di+')" style="padding:5px 12px;background:#065f46;color:white;border:none;border-radius:4px;cursor:pointer;font-size:12px" title="Add new register" data-i18n-title="modbusTipWrite">+ Registo</button>'
  +'</div>';
}

function mbRefreshAll(){
 var wrap=document.getElementById('mb_devs_wrap');
 if(!wrap)return;
 wrap.innerHTML=(_mbCfg.devices||[]).map(_renderTcpDev).join('');
}

function mbRemDev(di){
 var dev=_mbCfg.devices[di];
 if(!dev)return;
 customConfirm('Remove device "'+(dev.name||'TCP '+(di+1))+'"?',function(ok){
  if(!ok)return;
  _mbCfg.devices.splice(di,1);mbRefreshAll();
 });
}
function mbRemReg(di,ri){
 var r=_mbCfg.devices[di].regs[ri];
 if(!r)return;
 customConfirm('Remove register "'+(r.label||'Reg '+(ri+1))+'"?',function(ok){
  if(!ok)return;
  _mbCfg.devices[di].regs.splice(ri,1);mbRefreshAll();
 });
}
function mbAddReg(di){
 if(!_mbCfg.devices[di].regs)_mbCfg.devices[di].regs=[];
 _mbCfg.devices[di].regs.push({addr:0,fc:3,type:'UINT16',order:'ABCD',bit:0,label:'',unit:'',scale:1,offset:0,log:false,pretty:false});
 mbRefreshAll();
}

function mbAddDev(){
 if(!_mbCfg.devices)_mbCfg.devices=[];
 var rtuCount=_mbFull?(_mbFull.devices||[]).filter(function(d){return !d.isTcp;}).length:0;
 if(_mbCfg.devices.length+rtuCount>=4){customAlert('M\u00e1ximo 4 dispositivos no total (RTU + TCP)');return;}
 _mbCfg.devices.push({slaveId:1,name:'TCP'+(_mbCfg.devices.length+1),enabled:true,isTcp:true,tcpHost:'',tcpPort:502,pollMs:5000,retries:3,timeoutMs:500,regs:[]});
 mbRefreshAll();
}

function mbCollectForm(){
 var out={
  enabled:_gc('mb_enabled'),
  tcp_enabled:_gc('mb_tcp_en'),
  rtu_enabled:(_mbFull?!!_mbFull.rtu_enabled:false),
  rtu_baud:(_mbFull?(_mbFull.rtu_baud||9600):9600),
  rtu_parity:(_mbFull?(_mbFull.rtu_parity||'N'):'N'),
  rtu_stop_bits:(_mbFull?(_mbFull.rtu_stop_bits||1):1),
  rtu_de_pin:(_mbFull?(_mbFull.rtu_de_pin!=null?_mbFull.rtu_de_pin:-1):-1),
  slave_id:(_mbFull?(_mbFull.slave_id||1):1),
  devices:[]
 };
 (_mbCfg.devices||[]).forEach(function(d,di){
  var nd={
   slaveId:1, name:_gv('d_'+di+'_name')||'TCP',
   enabled:_gc('d_'+di+'_en'), isTcp:true,
   tcpHost:_gv('d_'+di+'_host'), tcpPort:parseInt(_gv('d_'+di+'_port'))||502,
   pollMs:parseInt(_gv('d_'+di+'_poll'))||5000,
   retries:parseInt(_gv('d_'+di+'_ret'))||3,
   timeoutMs:parseInt(_gv('d_'+di+'_tout'))||500,
   regs:[]
  };
  (d.regs||[]).forEach(function(r,ri){
   var tp=_gv('d_'+di+'_r'+ri+'_type')||'UINT16';
   var is32=(tp==='INT32'||tp==='UINT32'||tp==='FLOAT32');
   var isBit=(tp==='BIT');
   nd.regs.push({
    addr:parseInt(_gv('d_'+di+'_r'+ri+'_addr'))||0,
    fc:parseInt(_gv('d_'+di+'_r'+ri+'_fc'))||3,
    type:tp,
    order:is32?(_gv('d_'+di+'_r'+ri+'_order')||'ABCD'):0,
    bit:isBit?(parseInt(_gv('d_'+di+'_r'+ri+'_bit'))||0):0,
    label:_gv('d_'+di+'_r'+ri+'_label'),
    unit:_gv('d_'+di+'_r'+ri+'_unit'),
    scale:parseFloat(_gv('d_'+di+'_r'+ri+'_scale'))||1,
    offset:parseFloat(_gv('d_'+di+'_r'+ri+'_offset'))||0,
    log:_gc('d_'+di+'_r'+ri+'_log'),
    pretty:_gc('d_'+di+'_r'+ri+'_pretty')
   });
  });
  out.devices.push(nd);
 });
 // Preserve RTU devices from cached full config
 (_mbFull?(_mbFull.devices||[]):[]).forEach(function(d){
  if(!d.isTcp)out.devices.push(d);
 });
 return out;
}

function mbApplyCfg(cfg){
 _mbFull=cfg;
 _mbCfg={devices:(cfg.devices||[]).filter(function(d){return !!d.isTcp;})};
 document.getElementById('mb_enabled').checked=!!cfg.enabled;
 document.getElementById('mb_tcp_en').checked=!!cfg.tcp_enabled;
 mbRefreshAll();
}

function mbSave(){
 var btn=document.getElementById('mb_save');disableBtn(btn);showLoading();
 var data=mbCollectForm();
 fetch('/api/modbus/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
 .then(function(r){return r.json();})
 .then(function(j){
  if(j.success){customAlert('\u2705 TCP configuration saved!');if(typeof showSavedCard==='function')showSavedCard();}
  else customAlert('\u274c Error: '+(j.error||'Falhou'));
 }).catch(function(ex){customAlert('\u274c '+ex);}).finally(function(){hideLoading();enableBtn(btn);});
}

var _scanTimer=null;
function mbTcpScanStart(){
 var subnet=document.getElementById('tcp_subnet').value||'192.168.1';
 var s=parseInt(document.getElementById('tcp_start').value)||1;
 var e=parseInt(document.getElementById('tcp_end').value)||254;
 var t=parseInt(document.getElementById('tcp_timeout').value)||200;
 document.getElementById('tcp_scan_btn').style.display='none';
 document.getElementById('tcp_stop_btn').style.display='inline-block';
 document.getElementById('tcp_progress_wrap').style.display='block';
 document.getElementById('tcp_results').style.display='none';
 document.getElementById('tcp_tbody').innerHTML='';
 document.getElementById('tcp_status_msg').textContent='A iniciar scan TCP...';
 fetch('/api/modbus/scan-tcp',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({subnet:subnet,startIp:s,endIp:e,timeoutMs:t})})
 .then(function(r){return r.json();})
 .then(function(j){
  if(!j.success){mbScanStop();customAlert('\u274c '+(j.error||'Falhou'));return;}
  _scanTimer=setInterval(mbTcpScanStatus,800);
 }).catch(function(ex){mbScanStop();customAlert('\u274c '+ex);});
}

function mbScanStop(){
 document.getElementById('tcp_scan_btn').style.display='inline-block';
 document.getElementById('tcp_stop_btn').style.display='none';
 document.getElementById('tcp_progress_wrap').style.display='none';
 clearInterval(_scanTimer);_scanTimer=null;
}

function mbTcpScanStatus(){
  fetch('/api/modbus/scan-status',{cache:'no-store'}).then(function(r){return r.json();})
 .then(function(j){
  var pct=j.total>0?Math.round(j.current/j.total*100):0;
  document.getElementById('tcp_bar').style.width=pct+'%';
  document.getElementById('tcp_pct').textContent=pct+'%';
  document.getElementById('tcp_status_msg').textContent=
   (j.running?'A varrer IP '+j.current+' de '+j.total+' ...':'Scan conclu\u00eddo \u2014 '+j.found.length+' dispositivo(s) encontrado(s).');
  if(j.found&&j.found.length){
   document.getElementById('tcp_results').style.display='block';
   var html=j.found.map(function(f){
    return '<tr>'
     +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b;font-family:monospace;color:#34d399">'+f.ip+'</td>'
     +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b;color:#94a3b8">'+(f.deviceId||'\u2014')+'</td>'
     +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b">'
     +'<button type="button" onclick="mbAddTcpDev(\''+f.ip+'\',\''+((f.deviceId||'').replace(/\'/g,''))+'\''+')"'
     +' style="padding:4px 10px;background:#0891b2;color:white;border:none;border-radius:4px;cursor:pointer;font-size:12px" title="Add this device">\u2795 Add</button>'
     +'</td></tr>';
   }).join('');
   document.getElementById('tcp_tbody').innerHTML=html;
  }
  if(!j.running){mbScanStop();}
 }).catch(function(){});
}

function mbAddTcpDev(ip,name){
 var rtuCount=_mbFull?(_mbFull.devices||[]).filter(function(d){return !d.isTcp;}).length:0;
 if((_mbCfg.devices||[]).length+rtuCount>=4){customAlert('M\u00e1ximo 4 dispositivos');return;}
 _mbCfg.devices.push({slaveId:1,name:name||('TCP '+ip),enabled:true,isTcp:true,tcpHost:ip,tcpPort:502,pollMs:5000,retries:3,timeoutMs:500,regs:[]});
 mbRefreshAll();
}

function initModbusTcpPage(){
  document.getElementById('mb_add_dev').addEventListener('click',mbAddDev);
  document.getElementById('mb_save').addEventListener('click',mbSave);
  fetch('/api/modbus/config',{cache:'no-store'}).then(function(r){return r.json();}).then(mbApplyCfg)
 .catch(function(e){document.getElementById('mb_msg').textContent='Error loading: '+e;});
}
document.addEventListener('DOMContentLoaded',initModbusTcpPage);
</script>
)HTML";

// ---- Modbus Live Page -------------------------------------------
// Auto-refreshing read-only view of all register current values.

const char PAGE_MODBUS_LIVE[] PROGMEM = R"HTML(
<main class='page'>
 <div class='wrap'>
   <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='modbusTipHome'>&#127968; Home</a></div>
  <div style='display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-bottom:4px'><a href='/modbus-rtu' style='font-size:13px;color:#94a3b8;text-decoration:none'>&#9881;&#65039; RTU</a> <span style='color:#475569'>|</span> <a href='/modbus-tcp' style='font-size:13px;color:#67e8f9;text-decoration:none'>&#127760; TCP</a></div>
  <h1>&#128200; Modbus — Dados Live</h1>

  <div style='border:2px solid #3b82f6;border-radius:8px;padding:14px;background:#0f172a;margin-top:16px'>
   <div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px'>
    <span id='mb_live_status' style='color:#94a3b8;font-size:13px'>A carregar...</span>
    <div style='display:flex;gap:8px'>
     <button type='button' onclick='mbPollNow()' style='padding:6px 14px;background:#1d4ed8;color:white;border:none;border-radius:4px;cursor:pointer;font-size:13px' title='Force immediate poll' data-i18n-title='modbusTipPollNow'>🔄 Poll Agora</button>
     <button type='button' id='mb_auto_btn' onclick='mbToggleAuto()' style='padding:6px 14px;background:#065f46;color:white;border:none;border-radius:4px;cursor:pointer;font-size:13px' title='Toggle auto-refresh' data-i18n-title='modbusTipPollNow'>⏸ Stop Auto</button>
    </div>
   </div>
  </div>

  <div id='mb_live_data' style='margin-top:16px'></div>

  <!-- Manual Write Section -->
  <div style='border:2px solid #d97706;border-radius:8px;padding:18px;background:#0f172a;margin-top:20px'>
   <h2 style='margin:0 0 4px 0;color:#fbbf24'>✏️ Manual Write</h2>
   <p style='font-size:12px;color:#f59e0b;margin:0 0 14px 0'>⚠️ Escreve diretamente no dispositivo. Confirme antes de actuar.</p>
   <div style='display:flex;gap:12px;flex-wrap:wrap;align-items:flex-end;margin-bottom:14px'>
     <label style='flex:2;min-width:140px'>
      <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px'>Device</span>
      <select id='wr_device' onchange='mbWrDevChanged()' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Select target device' data-i18n-title='modbusTipWrite'>
       <option value=''>– selecionar –</option>
      </select>
     </label>
     <label style='flex:2;min-width:140px'>
      <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px'>Registo</span>
      <select id='wr_reg' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Select target register' data-i18n-title='modbusTipWrite'>
       <option value=''>– selecionar –</option>
      </select>
     </label>
     <label style='flex:1;min-width:100px'>
      <span style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px'>Valor</span>
      <input type='number' id='wr_value' step='any' value='0' style='width:100%;padding:8px;background:#1e293b;color:#f1f5f9;border:1px solid #475569;border-radius:4px;font-size:14px' title='Write value to register' data-i18n-title='modbusTipWrite'>
     </label>
   </div>
   <div style='display:flex;gap:10px;flex-wrap:wrap;align-items:center'>
     <button type='button' onclick='mbWrSubmit()' style='padding:10px 24px;background:#d97706;color:white;border:none;border-radius:6px;cursor:pointer;font-size:14px;font-weight:bold' title='Write value to register' data-i18n-title='modbusTipWrite'>⚡ Write</button>
    <span id='wr_result' style='font-size:13px;color:#94a3b8'></span>
   </div>
  </div>
 </div>
</main>

<script>
var _mbAutoRefresh=true,_mbTimer=null;

function mbRenderLive(devs){
 if(!devs||!devs.length){
  return '<div style="color:#94a3b8;text-align:center;padding:20px">Nenhum dispositivo configurado.<br><a href="/modbus-rtu" style="color:#60a5fa">→ Configurar RTU</a> &nbsp; <a href="/modbus-tcp" style="color:#67e8f9">→ Configurar TCP</a></div>';
 }
 return devs.map(function(d){
  var stColor=d.enabled?(d.pollCount>0?'#34d399':'#fbbf24'):'#94a3b8';
  var rows=(d.regs||[]).map(function(r){
   var ok=r.ok,vc=ok?'color:#34d399':'color:#ef4444';
   var val=r.val!==null&&r.val!==undefined?r.val.toFixed(4):'N/A';
   return '<tr>'
    +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b">'+r.label+'</td>'
    +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b;font-family:monospace;'+vc+'">'+val+'</td>'
    +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b;color:#94a3b8">'+(r.unit||'—')+'</td>'
    +'<td style="padding:7px 10px;border-bottom:1px solid #1e293b"><span style="'+vc+'">'+(ok?'✅':'❌')+'</span></td>'
    +'</tr>';
  }).join('');
   var tbl=rows?('<div class="table-wrapper"><table style="width:100%;border-collapse:collapse;font-size:13px">'
    +'<thead><tr>'
    +'<th style="text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #1e40af">Label</th>'
    +'<th style="text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #1e40af">Valor</th>'
    +'<th style="text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #1e40af">Unid.</th>'
    +'<th style="text-align:left;padding:7px 10px;color:#60a5fa;border-bottom:2px solid #1e40af">OK</th>'
    +'</tr></thead><tbody>'+rows+'</tbody></table></div>')
   :'<div style="color:#94a3b8;font-size:13px">Sem registos configurados.</div>';
  return '<div style="border:2px solid #2563eb;border-radius:8px;padding:14px;background:#0f172a;margin-bottom:14px">'
   +'<div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;margin-bottom:10px">'
   +'<h2 style="margin:0;color:#60a5fa;font-size:16px">'+d.name+' <span style="font-size:12px;color:'+stColor+'">('+(d.enabled?'activo':'inactivo')+')</span></h2>'
   +'<div style="font-size:12px;color:#94a3b8">✅ '+d.pollCount+' leituras &nbsp;❌ '+d.errorCount+' erros</div>'
   +'</div>'+tbl+'</div>';
 }).join('');
}

function mbFetchLive(){
  fetch('/api/modbus/live',{cache:'no-store'}).then(function(r){return r.json();}).then(function(devs){
  document.getElementById('mb_live_data').innerHTML=mbRenderLive(devs);
  document.getElementById('mb_live_status').textContent='Actualizado: '+new Date().toLocaleTimeString();
  // Keep write selects populated with latest device list
  if(document.getElementById('wr_device'))mbWrPopulateDevices(devs);
  if(_mbAutoRefresh)_mbTimer=setTimeout(mbFetchLive,3000);
 }).catch(function(e){
  document.getElementById('mb_live_status').textContent='Error: '+e;
  if(_mbAutoRefresh)_mbTimer=setTimeout(mbFetchLive,5000);
 });
}

function mbPollNow(){
 clearTimeout(_mbTimer);
 fetch('/api/modbus/poll-now',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})
 .then(function(){mbFetchLive();}).catch(function(){mbFetchLive();});
}

function mbToggleAuto(){
 _mbAutoRefresh=!_mbAutoRefresh;
 var btn=document.getElementById('mb_auto_btn');
 btn.textContent=_mbAutoRefresh?'⏸ Stop Auto':'▶ Start Auto';
 btn.style.background=_mbAutoRefresh?'#065f46':'#374151';
 if(_mbAutoRefresh){clearTimeout(_mbTimer);mbFetchLive();}
 else clearTimeout(_mbTimer);
}

// ---- Manual Write helpers ----
var _mbLiveDevs=[];

function mbWrPopulateDevices(devs){
 _mbLiveDevs=devs||[];
 var sel=document.getElementById('wr_device');
 sel.innerHTML='<option value="">– selecionar –</option>';
 devs.forEach(function(d,i){
  if(!d.enabled)return;
  var opt=document.createElement('option');
  opt.value=i;
  opt.textContent=d.name+' (ID '+d.slaveId+(d.isTcp?' TCP':'')+')';
  sel.appendChild(opt);
 });
 document.getElementById('wr_reg').innerHTML='<option value="">– selecionar –</option>';
}

function mbWrDevChanged(){
 var idx=parseInt(document.getElementById('wr_device').value);
 var regSel=document.getElementById('wr_reg');
 regSel.innerHTML='<option value="">– selecionar –</option>';
 if(isNaN(idx)||idx<0||!_mbLiveDevs[idx])return;
 var dev=_mbLiveDevs[idx];
 (dev.regs||[]).forEach(function(r,ri){
  // Only FC05 (coil write) and FC06 (register write) are writable
  if(r.fc!==5&&r.fc!==6)return;
  var opt=document.createElement('option');
  opt.value=ri;
  opt.textContent=r.label+(r.unit?' ['+r.unit+']':'');
  regSel.appendChild(opt);
 });
}

function mbWrSubmit(){
 var devIdx=parseInt(document.getElementById('wr_device').value);
 var regIdx=parseInt(document.getElementById('wr_reg').value);
 var val=parseFloat(document.getElementById('wr_value').value)||0;
 if(isNaN(devIdx)||isNaN(regIdx)){
  document.getElementById('wr_result').textContent='⚠️ Selecione dispositivo e registo.';
  return;
 }
 var dev=_mbLiveDevs[devIdx]||{};
 var reg=(dev.regs||[])[regIdx]||{};
 var devName=dev.name||('Device '+devIdx);
 var regLabel=reg.label||('Reg '+regIdx);
 var runWrite=function(){
  document.getElementById('wr_result').textContent='A escrever...';
  fetch('/api/modbus/write',{method:'POST',headers:{'Content-Type':'application/json'},
   body:JSON.stringify({deviceIndex:devIdx,regIndex:regIdx,value:val})})
  .then(function(r){return r.json();})
  .then(function(j){
   document.getElementById('wr_result').textContent=j.success?'✅ Write OK':'❌ '+(j.error||'erro');
  }).catch(function(e){document.getElementById('wr_result').textContent='❌ '+e;});
 };
 var msg='Confirmar escrita:\nDevice: '+devName+'\nRegisto: '+regLabel+'\nValor: '+val;
 if(typeof customConfirm==='function'){
  customConfirm(msg,function(ok){if(ok)runWrite();});
  return;
 }
}

function initModbusLivePage(){mbFetchLive();}
</script>
)HTML";
