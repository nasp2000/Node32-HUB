#include "config_manager_js_asset.h"

const char CONFIG_MANAGER_JS_BODY[] PROGMEM = R"CFG(// ========================================
// ConfigManager - JavaScript Universal
// Robust system for loading and saving configuration.
// ========================================

console.log('🔄 ConfigManager JS carregado');

function _applyLoadedConfig(config) {
    if (typeof loadWifiConfig === 'function') loadWifiConfig(config);
    if (typeof loadEmailConfig === 'function') loadEmailConfig(config);
    if (typeof loadRecipientsConfig === 'function') loadRecipientsConfig(config);
    if (typeof loadCommandsConfig === 'function') loadCommandsConfig(config);
    if (typeof loadRs232Config === 'function') loadRs232Config(config);
    if (typeof loadPrinterConfig === 'function') loadPrinterConfig(config);
}

function _detectConfigSection() {
    if (document.getElementById('rs232-form') || document.getElementById('rs232_enabled')) return 'rs232';
    if (document.getElementById('smtp_server') || document.getElementById('email_method')) return 'email';
    if (document.getElementById('recipients-table')) return 'recipients';
    if (document.getElementById('ssid') || document.getElementById('wifi_ssid')) return 'wifi';
    if (document.getElementById('printer-form') || document.getElementById('printer_emulation_mode') || document.getElementById('structures-form')) return 'system';
    return 'system';
}

function loadConfig(sectionOverride) {
    var section = sectionOverride || _detectConfigSection();
    // Reuse config recently fetched by auto.js (autoFillConfigs runs at DOMContentLoaded,
    // this runs at window.load+150ms — the 5s window is enough to avoid a double request).
    if (window._lastConfigData && window._lastConfigData.section === section && (Date.now() - window._lastConfigData.ts) < 5000) {
        console.log('Config: using cached data from auto.js fetch');
        _applyLoadedConfig(window._lastConfigData.data);
        return;
    }
    console.log('Config: fetching /api/config?section=' + section);
    fetch('/api/config?section=' + encodeURIComponent(section), {cache:'no-store'})
    .then(function(response) {
        if (!response.ok) { throw new Error('HTTP ' + response.status); }
        return response.json();
    })
    .then(function(config) {
        window._lastConfigData = {data: config, section: section, ts: Date.now()};
        _applyLoadedConfig(config);
        console.log('Config: loaded OK');
    })
    .catch(function(error) {
        // Non-critical: pages use dedicated narrow endpoints to load their own data.
        // A failure here means the sectioned config is unavailable but page-specific loaders may still run.
        console.warn('Config: load skipped (non-critical):', error.message);
    });
}

function saveConfig(configSection, data, successCallback) {
    console.log(`💾 Salvando (${configSection}) via /api/config:`, data);
    data.section = configSection;

    fetch(`/api/config`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: new URLSearchParams(data)
    })
    .then(response => {
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        return response.json();
    })
    .then(result => {
        if (result.success) {
            console.log(`✅ ${configSection} salvo com sucesso (unificado)`);
            showSuccessMessage(`${configSection} atualizado com sucesso!`);
            showSavedCard();
            if (successCallback) successCallback();
        } else {
            throw new Error(result.message || 'Unknown error');
        }
    })
    .catch(error => {
        console.error(`❌ Error saving ${configSection}:`, error);
        showErrorMessage((window._t?window._t('cfgSaveErr','Error saving'):'Error saving')+' '+configSection+': '+error.message);
    });
}

function loadWifiConfig(config) {
    console.log('🔄 Loading WiFi config');
    if (config.wifi) {
        if (document.getElementById('wifi_ssid')) {
            document.getElementById('wifi_ssid').value = config.wifi.ssid || '';
        }
        if (document.getElementById('wifi_password')) {
            document.getElementById('wifi_password').value = '';
            document.getElementById('wifi_password').placeholder = '(saved)';
        }
    }
}

function loadEmailConfig(config) {
    console.log('🔄 Loading Email config');
    if (config.smtp) {
        if (document.getElementById('smtp_server')) {
            document.getElementById('smtp_server').value = config.smtp.server || '';
        }
        if (document.getElementById('smtp_port')) {
            document.getElementById('smtp_port').value = config.smtp.port || '';
        }
        if (document.getElementById('smtp_username')) {
            document.getElementById('smtp_username').value = config.smtp.username || '';
        }
        if (document.getElementById('smtp_password')) {
            document.getElementById('smtp_password').value = '';
            document.getElementById('smtp_password').placeholder = '(saved)';
        }
    }
    if (config.email) {
        if (document.getElementById('email_destination')) {
            document.getElementById('email_destination').value = config.email.destination || '';
        }
        const autoSendEl = document.getElementById('email_auto_send') || document.getElementById('auto_send_email');
        if (autoSendEl) {
            autoSendEl.checked = !!(config.email && config.email.auto_send);
        }
        if (document.getElementById('email_max_lines')) {
            document.getElementById('email_max_lines').value = config.email.max_lines || 66;
        }
        if (document.getElementById('email_min_content_length')) {
            document.getElementById('email_min_content_length').value = config.email.min_content_length || 3;
        }
    }
}

function loadRs232Config(config) {
    console.log('🔄 Loading RS232 config');
    if (config.rs232) {
        if (document.getElementById('rs232_enabled')) {
            document.getElementById('rs232_enabled').checked = config.rs232.enabled || false;
        }
        if (document.getElementById('rs232_baudrate_index')) {
            document.getElementById('rs232_baudrate_index').value = config.rs232.baudrate_index || 3;
        }
        if (document.getElementById('rs232_invert_pins')) {
            document.getElementById('rs232_invert_pins').checked = config.rs232.invert_pins || false;
        }
    }
}

function loadPrinterConfig(config) {
    console.log('🔄 Loading Printer config');
    if (config.printer) {
        if (document.getElementById('printer_emulation_mode')) {
            document.getElementById('printer_emulation_mode').value = config.printer.emulation_mode || 0;
        }
        if (document.getElementById('printer_charset_mode')) {
            document.getElementById('printer_charset_mode').value = config.printer.charset_mode || 0;
        }
    }
}

function loadRecipientsConfig(config) {
    console.log('🔄 Loading Recipients config');
    const tbody = document.querySelector('#recipients-table tbody');
    if (!tbody) return;

    if (config && config.recipients) {
        tbody.innerHTML = '';
        Object.keys(config.recipients).forEach(name => {
            const email = config.recipients[name];
            addRecipientRow(name, email);
        });
        addRecipientRow('', '');
        return;
    }

    fetch('/api/recipients?limit=100', { cache: 'no-store' })
        .then(r => {
            if (!r.ok) throw new Error('HTTP ' + r.status);
            return r.json();
        })
        .then(list => {
            tbody.innerHTML = '';
            if (Array.isArray(list)) {
                list.forEach(function(rec){ addRecipientRow(rec.name || '', rec.email || ''); });
            }
            addRecipientRow('', '');
        })
        .catch(err => {
            console.warn('Failed to load /api/recipients, leaving recipient table empty', err);
            tbody.innerHTML = '';
            addRecipientRow('', '');
        });
}

function addRecipientRow(name, email) {
    const tbody = document.querySelector('#recipients-table tbody');
    if (!tbody) return;
    const row = tbody.insertRow();
    let displayName = name;
    if (displayName.length > 0) {
        displayName = displayName.charAt(0).toUpperCase() + displayName.slice(1);
    }
    row.innerHTML = `
        <td><input type="text" name="recipient_name[]" autocomplete="name" value="${displayName}" placeholder="Nome" class="form-input" /></td>
        <td><input type="email" name="recipient_email[]" autocomplete="email" value="${email}" placeholder="email@exemplo.com" class="form-input" /></td>
        <td>
            <button onclick="saveRecipient(this)" class="btn btn-small">Salvar</button>
            <button onclick="removeRecipient(this, '${name}')" class="btn btn-small btn-danger">Remove</button>
        </td>
    `;
}

function saveRecipient(button) {
    const row = button.closest('tr');
    const nameInput = row.cells[0].querySelector('input');
    const emailInput = row.cells[1].querySelector('input');
    const name = nameInput.value.trim();
    const email = emailInput.value.trim();
    if (name && email) {
        const recipients = collectRecipientsFromTable();
        const payload = { recipients_json: JSON.stringify(recipients) };
        saveConfig('recipients', payload, () => setTimeout(loadConfig, 500));
    } else {
        showErrorMessage(window._t?window._t('cfgRecipientsRequired','Name and email are required'):'Name and email are required');
    }
}

function removeRecipient(button, name) {
    const doRemove = () => {
        const row = button.closest('tr');
        if (row && row.parentElement) row.parentElement.removeChild(row);
        const recipients = collectRecipientsFromTable();
        const payload = { recipients_json: JSON.stringify(recipients) };
        saveConfig('recipients', payload, () => setTimeout(loadConfig, 500));
    };
    const msg = `Remove recipient ${name || ''}?`;
    if (typeof customConfirm === 'function') {
        customConfirm(msg, function(ok) { if (ok) doRemove(); });
        return;
    }
}

function collectRecipientsFromTable() {
    const tbody = document.querySelector('#recipients-table tbody');
    const recipients = [];
    if (!tbody) return recipients;
    const rows = Array.from(tbody.rows);
    for (const r of rows) {
        const name = (r.cells[0]?.querySelector('input')?.value || '').trim();
        const email = (r.cells[1]?.querySelector('input')?.value || '').trim();
        if (name && email) recipients.push({ name, email });
    }
    return recipients;
}

// Success notification disabled — customAlert provides save feedback
function showSuccessMessage(message) {
    console.log('✅ ' + message);
}

// showSavedCard: disabled — customAlert already provides save feedback
function showSavedCard() {}

function showErrorMessage(message) {
    console.error('❌ ' + message);
    const notification = document.createElement('div');
    notification.className = 'notification notification-error';
    notification.innerHTML = `
        <span>❌ ${message}</span>
        <button onclick="this.parentElement.remove()" style="float:right; background:none; border:none; color:white; cursor:pointer;">×</button>
    `;
    document.body.appendChild(notification);
    setTimeout(() => {
        if (notification.parentElement) {
            notification.remove();
        }
    }, 8000);
}

function setupForm(formId, configSection, fieldsMap) {
    const form = document.getElementById(formId);
    if (!form) return;
    form.addEventListener('submit', function(e) {
        e.preventDefault();
        const formData = new FormData(form);
        const data = {};
        for (const [formField, apiField] of Object.entries(fieldsMap)) {
            if (formData.has(formField)) {
                data[apiField] = formData.get(formField);
            }
        }
        saveConfig(configSection, data);
    });
}

document.addEventListener('DOMContentLoaded', function() {
    if (document.getElementById('printer-form')) {
        setupForm('printer-form', 'system', {
            'emulation_mode': 'printer_emulation_mode',
            'charset_mode': 'printer_charset_mode',
            'timezone': 'timezone',
            'ntp_server': 'ntp_server'
        });
    }
});

window.addEventListener('load', function() {
    function hasConfigTargets(){
        var ids = [
            'wifi_ssid','wifi_password','smtp_server','smtp_port','smtp_username','smtp_password',
            'email_destination','email_auto_send','auto_send_email','email_max_lines','email_min_content_length',
            'rs232_enabled','rs232_baudrate_index','rs232_invert_pins',
            'printer_emulation_mode','printer_charset_mode','recipients-table'
        ];
        for (var i = 0; i < ids.length; i++) {
            if (document.getElementById(ids[i])) return true;
        }
        return !!document.getElementById('printer-form');
    }

    if (hasConfigTargets()) {
        console.log('Config fields detected, scheduling load...');
        setTimeout(loadConfig, 150);
        _startCfgPoll();
    }
});

var _cfgPollTimer=null;
// Poll every 5 minutes (reduced from 2 min to ease ESP32 load).
function _startCfgPoll(){if(!_cfgPollTimer)_cfgPollTimer=setInterval(loadConfig,300000);}
function _stopCfgPoll(){if(_cfgPollTimer){clearInterval(_cfgPollTimer);_cfgPollTimer=null;}}
document.addEventListener('visibilitychange',function(){document.hidden?_stopCfgPoll():_startCfgPoll();});

console.log('✅ ConfigManager JS inicializado');
)CFG";