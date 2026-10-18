#include "page_ipg_laser_asset.h"

const char PAGE_IPG_LASER[] PROGMEM = R"HTML(
<div class='page ipg-laser'>
  <div class='wrap'>
    <style>
      .section-box {
        margin: 20px 0;
        padding: 18px;
        background: #0f172a;
        border-radius: 8px;
        border: 2px solid #3b82f6;
      }
      .section-title {
        margin: 0 0 15px 0;
        font-size: 20px;
        color: #60a5fa;
        font-weight: bold;
      }
      .ipg-mobile-scroll-hint {
        display: none;
      }
      @media (max-width: 767px) {
        .ipg-mobile-scroll-hint {
          display: block;
        }
      }
      @media (max-width: 480px) {
        .section-box { padding: 14px; }
      }
    </style>

    <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='ipgTipHome'>&#127968; Home</a></div>
    <h1>&#128293; IPG Laser RS232</h1>

    <div class='section-box'>
      <h3 class='section-title' data-i18n='ipgNavQuick'>🧭 Quick navigation</h3>
      <div style='display:flex;gap:8px;flex-wrap:wrap'>
        <a href='#sec-ipg-link' class='btn' style='padding:8px 14px' data-i18n='ipgNavLink'>🔌 Connection</a>
        <a href='#sec-ipg-console' class='btn' style='padding:8px 14px'>&#9000; Console</a>
        <a href='#sec-ipg-log' class='btn' style='padding:8px 14px'>&#128221; Respostas</a>
        <a href='#sec-ipg-catalog' class='btn' style='padding:8px 14px' data-i18n='ipgNavCatalog'>📚 Catalog</a>
      </div>
    </div>

    <div id='sec-ipg-link' class='section-box'>
      <h3 class='section-title' data-i18n='ipgSecLinkRtu'>1) RS232 Connection</h3>
      <div id='ipgStatus' style='font-size:14px;color:#cbd5e1;line-height:1.6' data-i18n='ipgLoading'>Loading module state...</div>
      
      <div style='background:#1e293b;border-radius:6px;padding:8px 14px;margin-top:14px;margin-bottom:14px;font-size:12px;color:#94a3b8;display:flex;align-items:center;gap:8px;flex-wrap:wrap'>
        <span style='color:#60a5fa'>&#128268; RS232 ativo:</span>
        <span id='ipg-rs232-text'>a carregar...</span>
        <a href='/rs232' style='margin-left:auto;font-size:11px;color:#60a5fa'>&#9999;&#65039; configurar</a>
      </div>
      
      <div style='background:#1e3a8a;border:2px solid #0ea5e9;border-radius:8px;padding:14px;margin-bottom:14px'>
        <div style='font-size:13px;font-weight:600;color:#0ea5e9;margin-bottom:8px'> data-i18n='ipgLinkRecomm'>ⓘ Connection Recommendation</div>
        <div style='font-size:12px;color:#cbd5e1;line-height:1.6'>
          <div style='margin-bottom:8px'><strong>Cabo:</strong> RS232 null-modem 3-wire (RXD, TXD, GND)</div>
          <div style='margin-bottom:8px'><strong>Laser Connector:</strong> DB9 female (or proprietary connector per manual)</div>
          <div style='margin-bottom:8px'><strong>Conector Hub:</strong> DB9 macho (entrada RS232)</div>
          <div style='margin-bottom:8px'><strong><strong>Required speed:</strong> 57600 baud, 8N1, no flow control</strong> 57600 baud, 8N1, no flow control</div>
          <div style='margin-bottom:8px'><strong>Laser mode:</strong> Must be in REMOTE with valid interlocks before EMON</div>
          <div style='color:#cbd5e1'><strong>⚠️ Tip:</strong> If no command replies (it gets BCMD), check the laser is in REMOTE mode and the interlocks are satisfied.</div>
        </div>
      </div>
      
      <div style='margin-top:12px;font-size:13px;color:#94a3b8'>
        Perfil exigido pelo manual: 57600 baud, 8N1, sem flow control, null-modem 3-wire. Antes de usar <strong>EMON</strong>, the laser must be in <strong>REMOTE</strong> and with valid interlocks. An invalid command replies <strong>BCMD</strong>.
      </div>
    </div>

    <div id='sec-ipg-console' class='section-box'>
      <h3 class='section-title'>2) Console de Comandos</h3>
      <label for='ipgCommand' style='display:block;margin-bottom:8px;font-size:14px;color:#e2e8f0'>Comando a enviar</label>
      <input id='ipgCommand' type='text' placeholder='Ex.: STA, ROP, EMON, SDC 35.0' title='IPG command to send' data-i18n-title='ipgTipCommand' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
      <div style='display:flex;gap:8px;flex-wrap:wrap;margin-top:12px'>
        <button type='button' class='btn' onclick='sendIpgCommand()' title='Send command to laser' data-i18n-title='ipgTipSend' style='min-width:160px'>&#10148; Send command</button>
        <button type='button' class='btn' onclick='clearIpgLog()' title='Clear response log' data-i18n-title='ipgTipClearLog' style='min-width:160px;background:#334155;border-color:#475569'>&#128465; Limpar respostas</button>
      </div>
      <div style='margin-top:16px;padding:14px;border:1px solid #1e40af;border-radius:8px;background:#020617'>
        <div style='font-size:14px;font-weight:700;color:#93c5fd;margin-bottom:10px'>2.1 &#9881; Presets Operacionais</div>
        <div style='font-size:12px;color:#94a3b8;margin-bottom:10px' data-i18n='ipgPresetDesc'>Quick mode for diagnostics and daily operation without memorizing every command.</div>
        <div style='display:flex;gap:8px;flex-wrap:wrap'>
          <button type='button' class='btn' onclick='sendPreset("STA")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset'>Quick diagnostics (STA)</button>
          <button type='button' class='btn' onclick='sendPreset("ROP")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset'>Average power (ROP)</button>
          <button type='button' class='btn' onclick='sendPreset("RPP")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset'>Peak power (RPP)</button>
          <button type='button' class='btn' onclick='sendPreset("RCS")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset'>Corrente setpoint (RCS)</button>
          <button type='button' class='btn' onclick='sendPreset("RPRR")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset'>Pulse repetition (RPRR)</button>
          <button type='button' class='btn' onclick='sendPreset("RPW")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset'>Pulse width (RPW)</button>
          <button type='button' class='btn' onclick='sendPreset("RFV")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset'>Firmware version (RFV)</button>
          <button type='button' class='btn' onclick='sendPreset("EMON")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset' style='background:#14532d;border-color:#166534'>Start emission (EMON)</button>
          <button type='button' class='btn' onclick='sendPreset("EMOFF")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset' style='background:#7f1d1d;border-color:#991b1b'>Stop emission (EMOFF)</button>
          <button type='button' class='btn' onclick='sendPreset("HELP")' title='Quick laser diagnostics' data-i18n-title='ipgTipPreset'>Help interface (HELP)</button>
        </div>
        <div style='margin-top:12px;padding:12px;border:1px dashed #334155;border-radius:8px;background:#0b1220'>
          <div style='font-size:13px;font-weight:700;color:#cbd5e1;margin-bottom:8px'>2.2 &#9203; Automatic sequences</div>
          <div style='font-size:12px;color:#94a3b8;margin-bottom:10px' data-i18n='ipgSequenceDesc'>Sends several commands in order, with a short delay between steps.</div>
          <div style='display:flex;gap:8px;flex-wrap:wrap'>
            <button type='button' class='btn' onclick='runPresetSequence("Power snapshot", ["STA","ROP","RPP","RCS","RPRR","RPW"], 400)' title='Run preset sequence' data-i18n-title='ipgTipSequence'>Power snapshot</button>
            <button type='button' class='btn' onclick='runPresetSequence("Full state", ["STA","RFV","ROP","RPP","RCS","RPRR","RPW","HELP"], 450)' title='Run preset sequence' data-i18n-title='ipgTipSequence' data-i18n='ipgPresetFull'>Full state</button>
            <button type='button' class='btn' onclick='runPresetSequence("Emission startup", ["STA","EMON","STA"], 500)' title='Run preset sequence' data-i18n-title='ipgTipSequence' style='background:#14532d;border-color:#166534'>Emission startup</button>
            <button type='button' class='btn' onclick='runPresetSequence("Paragem segura", ["EMOFF","STA"], 450)' title='Run preset sequence' data-i18n-title='ipgTipSequence' style='background:#7f1d1d;border-color:#991b1b'>Paragem segura</button>
            <button type='button' class='btn' onclick='cancelPresetSequence()' title='Cancel running sequence' data-i18n-title='ipgTipCancel' style='background:#334155;border-color:#475569'>Cancel sequence</button>
          </div>
          <div id='ipgSequenceStatus' style='font-size:12px;color:#94a3b8;margin-top:10px' data-i18n='ipgNoSequence'>No sequence running.</div>
        </div>
      </div>
    </div>

    <div id='sec-ipg-log' class='section-box'>
      <h3 class='section-title'>3) Respostas do Laser</h3>
      <div style='font-size:13px;color:#94a3b8;margin-bottom:10px' data-i18n='ipgConsoleDesc'>The window below shows TX and RX captured from this page.</div>
      <pre id='ipgLog' style='margin:0;height:200px;overflow-y:auto;background:#020617;color:#e2e8f0;border:1px solid #1d4ed8;border-radius:8px;padding:14px;font-size:13px;line-height:1.5'>Sem respostas ainda.</pre>
    </div>

    <div id='sec-ipg-catalog' class='section-box'>
      <h3 class='section-title' data-i18n='ipgSecAvailable'>4) Available Commands</h3>
      <div style='font-size:13px;color:#94a3b8;margin-bottom:12px'>Catalog consolidated from <strong>xLR-Y14 Interface Specification rev.2</strong> and <strong>YLR-Series Users Guide</strong>.</div>
      <div style='display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end;margin-bottom:10px'>
        <div style='flex:1;min-width:220px'>
          <label for='ipgSearch' style='display:block;margin-bottom:6px;font-size:13px;color:#cbd5e1' data-i18n='ipgQuickSearch'>Quick search</label>
          <input id='ipgSearch' type='text' placeholder='Pesquisar por comando, uso, resposta, notas...' title='Search commands' data-i18n-title='ipgTipSearch' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
        </div>
        <div style='min-width:220px'>
          <label for='ipgCategory' style='display:block;margin-bottom:6px;font-size:13px;color:#cbd5e1'>Categoria</label>
          <select id='ipgCategory' title='Filter by category' data-i18n-title='ipgTipCategory' style='width:100%;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
            <option value=''>A carregar...</option>
          </select>
        </div>
        <div style='display:flex;align-items:center;gap:8px'>
          <button type='button' class='btn' onclick='clearCommandFilters()' title='Clear search and filters' data-i18n-title='ipgTipClearFilters' style='background:#334155;border-color:#475569'>Limpar filtros</button>
        </div>
      </div>
      <div id='ipgFilterCount' style='font-size:12px;color:#94a3b8;margin-bottom:8px'>A carregar comandos...</div>
      <div class='ipg-mobile-scroll-hint' style='font-size:12px;color:#94a3b8;margin-bottom:6px' data-i18n='ipgScrollHint'>&#128073; Scroll horizontally to see all columns on mobile.</div>
      <div class='table-wrapper'>
        <table style='width:100%;border-collapse:collapse;font-size:13px'>
          <thead>
            <tr>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #1e40af;color:#60a5fa'>Cmd</th>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #1e40af;color:#60a5fa'>Categoria</th>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #1e40af;color:#60a5fa'>Uso</th>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ipgColFunction'>Function</th>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ipgColResponse'>Typical response</th>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #1e40af;color:#60a5fa'>Notas</th>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #1e40af;color:#60a5fa' data-i18n='ipgColAction'>Action</th>
            </tr>
          </thead>
          <tbody id='ipgCommandsBody'>
            <tr><td colspan='7' style='padding:12px;color:#94a3b8;text-align:center'>A carregar comandos...</td></tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</div>

<script>
var gIpgCommands = [];
var gSequenceActive = false;
var gSequenceTimer = 0;
var gSequenceName = '';

function escapeHtml(value) {
  return String(value || '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function useCommand(command) {
  document.getElementById('ipgCommand').value = command;
}

function sendPreset(command) {
  useCommand(command);
  sendIpgCommand();
}

function setSequenceStatus(text, color) {
  var el = document.getElementById('ipgSequenceStatus');
  if (!el) {
    return;
  }
  el.textContent = text;
  el.style.color = color || '#94a3b8';
}

function cancelPresetSequence() {
  if (gSequenceTimer) {
    clearTimeout(gSequenceTimer);
    gSequenceTimer = 0;
  }
  if (gSequenceActive) {
    customAlert('ℹ️ Sequence cancelled.');
  }
  gSequenceActive = false;
  gSequenceName = '';
  setSequenceStatus('No sequence running.', '#94a3b8');
}

function runPresetSequence(name, commands, delayMs) {
  var delay = Number(delayMs || 400);
  var list = [];
  var index = 0;
  if (gSequenceActive) {
    customAlert('❌ A sequence is already running. Cancel it first.');
    return;
  }
  if (!commands || !commands.length) {
    customAlert('❌ Invalid sequence.');
    return;
  }
  for (index = 0; index < commands.length; index++) {
    list.push(String(commands[index] || '').replace(/^\s+|\s+$/g, ''));
  }
  gSequenceActive = true;
  gSequenceName = String(name || 'Sequence');
  setSequenceStatus('A iniciar: ' + gSequenceName + ' (' + String(list.length) + ' comandos).', '#93c5fd');
  customAlert('▶️ Sequence started: ' + gSequenceName);
  runPresetSequenceStep(list, 0, delay);
}

function runPresetSequenceStep(commands, position, delay) {
  var current = '';
  if (!gSequenceActive) {
    return;
  }
  if (position >= commands.length) {
    gSequenceActive = false;
    gSequenceName = '';
    setSequenceStatus('Sequence completed successfully.', '#4ade80');
    customAlert('✅ Sequence completed.');
    loadStatus();
    setTimeout(loadLog, 250);
    setTimeout(loadLog, 1200);
    return;
  }
  current = commands[position];
  if (!current) {
    runPresetSequenceStep(commands, position + 1, delay);
    return;
  }
  useCommand(current);
  setSequenceStatus('[' + String(position + 1) + '/' + String(commands.length) + '] ' + gSequenceName + ': a enviar ' + current, '#fbbf24');
  sendIpgCommandWithOptions(current, false, function(success, message) {
    if (!gSequenceActive) {
      return;
    }
    if (!success) {
      gSequenceActive = false;
      setSequenceStatus('Sequence interrupted at ' + current + ': ' + (message || 'unknown error'), '#f87171');
      customAlert('❌ Sequence error (' + current + '): ' + (message || 'send failure'));
      return;
    }
    gSequenceTimer = setTimeout(function() {
      gSequenceTimer = 0;
      runPresetSequenceStep(commands, position + 1, delay);
    }, delay);
  });
}

function renderStatus(data) {
  var rs232Ok = data.currentBaudrate === data.requiredBaudrate &&
    data.currentDataBits === data.requiredDataBits &&
    String(data.currentParity || '').toLowerCase() === data.requiredParity &&
    data.currentStopBits === data.requiredStopBits &&
    !data.currentFlowControl;
  var badge = rs232Ok
    ? '<span style="color:#4ade80;font-weight:700" data-i18n='ipgProfileOk'>&#9679; Compatible profile</span>'
    : '<span style="color:#f59e0b;font-weight:700">&#9679; Ajustar perfil RS232</span>';
  var html = '';
  html += '<div style="margin-bottom:8px">' + badge + '</div>';
  html += '<div><strong>Atual:</strong> ' + escapeHtml(data.currentBaudrate) + ' baud, ' + escapeHtml(data.currentDataBits) + escapeHtml(String(data.currentParity || '').charAt(0).toUpperCase()) + escapeHtml(data.currentStopBits) + ', flow control ' + (data.currentFlowControl ? 'ON' : 'OFF') + '</div>';
  html += '<div><strong>Exigido:</strong> ' + escapeHtml(data.requiredBaudrate) + ' baud, ' + escapeHtml(data.requiredDataBits) + 'N' + escapeHtml(data.requiredStopBits) + ', flow control none</div>';
  html += '<div><strong>Template ativo:</strong> ' + escapeHtml(data.currentTemplate || 'nenhum') + '</div>';
  html += '<div><strong data-i18n='ipgLastCmd'>Last command:</strong> ' + escapeHtml(data.lastCommand || '-') + '</div>';
  html += '<div><strong>Active capture:</strong> ' + (data.captureActive ? 'yes (' + escapeHtml(data.captureEndsInMs) + ' ms)' : 'no') + '</div>';
  html += '<div><strong>Modo requerido:</strong> ' + escapeHtml(data.remoteModeNote) + '</div>';
  document.getElementById('ipgStatus').innerHTML = html;
}

function loadStatus() {
  fetch('/api/ipg-laser-status',{cache:'no-store'})
    .then(function(response) { return response.json(); })
    .then(function(data) { renderStatus(data); })
    .catch(function() {
      document.getElementById('ipgStatus').innerHTML = '<span style="color:#f87171" data-i18n='ipgErrState'>Error loading IPG module state.</span>';
    });
}

function loadCommands() {
  fetch('/api/ipg-laser-commands',{cache:'no-store'})
    .then(function(response) { return response.json(); })
    .then(function(data) {
      if (!data.commands || !data.commands.length) {
        gIpgCommands = [];
        renderFilteredCommands();
        return;
      }
      gIpgCommands = data.commands;
      updateCategoryFilter();
      renderFilteredCommands();
    })
    .catch(function() {
      document.getElementById('ipgFilterCount').textContent = 'Error loading command catalog.';
      document.getElementById('ipgCommandsBody').innerHTML = '<tr><td colspan="7" style="padding:12px;color:#f87171;text-align:center" data-i18n='ipgErrCatalog'>Error loading command catalog.</td></tr>';
    });
}

function updateCategoryFilter() {
  var select = document.getElementById('ipgCategory');
  var current = select.value || '';
  var categories = {};
  var index = 0;
  select.innerHTML = '';
  for (index = 0; index < gIpgCommands.length; index++) {
    var category = String(gIpgCommands[index].category || 'Outros');
    categories[category] = true;
  }
  var keys = Object.keys(categories).sort(function(a, b) {
    return a.localeCompare(b);
  });
  for (index = 0; index < keys.length; index++) {
    var option = document.createElement('option');
    option.value = keys[index];
    option.textContent = keys[index];
    select.appendChild(option);
  }
  if (current && categories[current]) {
    select.value = current;
  } else if (keys.length > 0) {
    select.value = keys[0];
  }
}

function renderFilteredCommands() {
  var body = document.getElementById('ipgCommandsBody');
  var count = document.getElementById('ipgFilterCount');
  var selectedCategory = (document.getElementById('ipgCategory').value || '').toLowerCase();
  var query = (document.getElementById('ipgSearch').value || '').toLowerCase();
  var filtered = [];
  var index = 0;
  for (index = 0; index < gIpgCommands.length; index++) {
    var command = gIpgCommands[index];
    var category = String(command.category || '').toLowerCase();
    if (selectedCategory && category !== selectedCategory) {
      continue;
    }
    if (query) {
      var text = [
        command.code,
        command.category,
        command.usage,
        command.description,
        command.response,
        command.notes
      ].join(' ').toLowerCase();
      if (text.indexOf(query) === -1) {
        continue;
      }
    }
    filtered.push(command);
  }

  if (!filtered.length) {
    if (!gIpgCommands.length) {
      count.textContent = 'No commands available.';
      body.innerHTML = '<tr><td colspan="7" style="padding:12px;color:#94a3b8;text-align:center" data-i18n='ipgNoCommands'>No commands available.</td></tr>';
      return;
    }
    count.textContent = '0 comandos encontrados para os filtros atuais.';
    body.innerHTML = '<tr><td colspan="7" style="padding:12px;color:#94a3b8;text-align:center">Nenhum comando corresponde aos filtros.</td></tr>';
    return;
  }

  count.textContent = String(filtered.length) + ' of ' + String(gIpgCommands.length) + ' commands shown.';
  var html = '';
  for (index = 0; index < filtered.length; index++) {
    var row = filtered[index];
    html += '<tr>';
    html += '<td style="padding:8px;border-bottom:1px solid #1e293b;font-weight:700;color:#e2e8f0">' + escapeHtml(row.code) + '</td>';
    html += '<td style="padding:8px;border-bottom:1px solid #1e293b">' + escapeHtml(row.category) + '</td>';
    html += '<td style="padding:8px;border-bottom:1px solid #1e293b">' + escapeHtml(row.usage) + '</td>';
    html += '<td style="padding:8px;border-bottom:1px solid #1e293b">' + escapeHtml(row.description) + '</td>';
    html += '<td style="padding:8px;border-bottom:1px solid #1e293b">' + escapeHtml(row.response) + '</td>';
    html += '<td style="padding:8px;border-bottom:1px solid #1e293b">' + escapeHtml(row.notes) + '</td>';
    html += '<td style="padding:8px;border-bottom:1px solid #1e293b"><button type="button" class="btn" onclick="useCommand(\'' + escapeHtml(row.code) + '\')">Usar</button></td>';
    html += '</tr>';
  }
  body.innerHTML = html;
}

function clearCommandFilters() {
  document.getElementById('ipgSearch').value = '';
  var category = document.getElementById('ipgCategory');
  if (category && category.options && category.options.length > 0) {
    category.selectedIndex = 0;
  }
  renderFilteredCommands();
}

function loadLog() {
  fetch('/api/ipg-laser-log',{cache:'no-store'})
    .then(function(response) { return response.json(); })
    .then(function(data) {
      var output = [];
      var index = 0;
      if (data.entries && data.entries.length) {
        for (index = 0; index < data.entries.length; index++) {
          output.push('[' + data.entries[index].direction + '] ' + data.entries[index].text);
        }
      } else {
        output.push('Sem respostas ainda.');
      }
      document.getElementById('ipgLog').textContent = output.join('\n');
    })
    .catch(function() {
      document.getElementById('ipgLog').textContent = 'Error loading laser replies.';
    });
}

function sendIpgCommand() {
  var command = document.getElementById('ipgCommand').value || '';
  command = command.replace(/^\s+|\s+$/g, '');
  if (!command) {
    customAlert('❌ Introduz um comando IPG primeiro.');
    return;
  }
  sendIpgCommandWithOptions(command, true);
}

function sendIpgCommandWithOptions(command, showSuccessAlert, doneCallback) {
  fetch('/api/ipg-laser-send', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command: command })
  })
    .then(function(response) { return response.json(); })
    .then(function(data) {
      if (data.success) {
        if (showSuccessAlert) {
          customAlert('✅ ' + data.message);
        }
        loadStatus();
        setTimeout(loadLog, 250);
        setTimeout(loadLog, 1200);
        if (doneCallback) {
          doneCallback(true, data.message || 'ok');
        }
      } else {
        if (showSuccessAlert) {
          customAlert('❌ Error: ' + data.message);
        }
        if (doneCallback) {
          doneCallback(false, data.message || 'erro');
        }
      }
    })
    .catch(function() {
      if (showSuccessAlert) {
        customAlert('❌ Error: failed to send IPG command.');
      }
      if (doneCallback) {
        doneCallback(false, 'communication failure');
      }
    });
}

function clearIpgLog() {
  fetch('/api/ipg-laser-clear', { method: 'POST' })
    .then(function(response) { return response.json(); })
    .then(function(data) {
      if (data.success) {
        customAlert('✅ ' + data.message);
        loadLog();
      } else {
        customAlert('❌ Error: ' + data.message);
      }
    })
    .catch(function() {
      customAlert('❌ Error: failed to clear replies.');
    });
}

function loadRs232Status() {
fetch('/api/config?section=rs232&t=' + Date.now(), { cache: 'no-cache' })
    .then(function(r) { return r.json(); })
    .then(function(d) {
      var textEl = document.getElementById('ipg-rs232-text');
      if (!textEl) {
        return;
      }
      var baud = d.rs232_baudrate || 19200;
      var dataBits = d.rs232_data_bits || 8;
      var parity = String(d.rs232_parity || 'none').charAt(0).toUpperCase();
      var stopBits = d.rs232_stop_bits || 1;
      var enabled = d.rs232_enabled ? '● ativo' : '○ desabilitado';
      textEl.textContent = baud + ' baud · ' + dataBits + parity + stopBits + ' · ' + enabled;
    })
    .catch(function() {
      var textEl = document.getElementById('ipg-rs232-text');
      if (textEl) {
        textEl.textContent = 'erro ao carregar';
      }
    });
}

function checkIpgModuleAvailable() {
  fetch('/api/ipg-laser-available', { cache: 'no-cache' })
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (!d.available) {
        showIpgModuleDisabledMessage();
      } else {
        hideIpgModuleDisabledMessage();
      }
    })
    .catch(function() {
      showIpgModuleDisabledMessage();
    });
}

function showIpgModuleDisabledMessage() {
  var consoleSection = document.getElementById('sec-ipg-console');
  var catalogSection = document.getElementById('sec-ipg-catalog');
  var logSection = document.getElementById('sec-ipg-log');
  
  if (consoleSection) consoleSection.style.display = 'none';
  if (catalogSection) catalogSection.style.display = 'none';
  if (logSection) logSection.style.display = 'none';
  
  var statusEl = document.getElementById('ipgStatus');
  if (statusEl) {
    statusEl.innerHTML = '<div style="background:#7f1d1d;border:2px solid #f87171;border-radius:8px;padding:16px;color:#fca5a5"><strong data-i18n='ipgNotDetected'>⚠️ IPG Laser module not detected</strong><div style="margin-top:8px;font-size:13px" data-i18n='ipgNotEnabled'>The IPG Laser module is not enabled on this device. To enable it:</div><div style="margin-top:8px;font-size:12px;color:#f5d2d2">1. Edite <code>user_config.h</code><br>2. Procure por <code>ENABLE_IPG_LASER</code><br>3. Defina como <code>1</code> (descomente ou altere)<br>4. Recompile e envie para o ESP32<br>5. Reload this page</div></div>';
  }
}

function hideIpgModuleDisabledMessage() {
  var consoleSection = document.getElementById('sec-ipg-console');
  var catalogSection = document.getElementById('sec-ipg-catalog');
  var logSection = document.getElementById('sec-ipg-log');
  
  if (consoleSection) consoleSection.style.display = 'block';
  if (catalogSection) catalogSection.style.display = 'block';
  if (logSection) logSection.style.display = 'block';
}

document.addEventListener('DOMContentLoaded', function() {
  checkIpgModuleAvailable();
  loadStatus();
  loadCommands();
  loadLog();
  loadRs232Status();
  document.getElementById('ipgSearch').addEventListener('input', renderFilteredCommands);
  document.getElementById('ipgCategory').addEventListener('change', renderFilteredCommands);
  setInterval(loadStatus, 1500);
  setInterval(loadLog, 1500);
  setInterval(loadRs232Status, 3000);
  document.getElementById('ipgCommand').addEventListener('keydown', function(event) {
    if (event.key === 'Enter') {
      event.preventDefault();
      sendIpgCommand();
    }
  });
});
</script>
)HTML";