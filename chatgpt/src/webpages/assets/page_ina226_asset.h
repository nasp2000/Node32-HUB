#ifndef PAGE_INA226_ASSET_H
#define PAGE_INA226_ASSET_H

#include "../../feature_flags.h"

#if ENABLE_INA226==1

#include <Arduino.h>

// Page served at /ina226 — INA226 current/power monitor configuration.
static const char INA226_CONFIG_PAGE[] PROGMEM = R"rawliteral(
    <style>
        .section-box {
            margin: 20px 0;
            padding: 18px;
            background: #0f172a;
            border-radius: 8px;
            border: 2px solid #3b82f6;
        }
        .section-title { margin: 0 0 15px 0; font-size: 20px; color: #60a5fa; font-weight: bold; }
        .checkbox-label {
            display: inline-flex; align-items: center; gap: 8px; cursor: pointer;
            padding: 6px 12px; background: #1e293b; border-radius: 6px; font-size: 13px;
        }
        .checkbox-label input[type="checkbox"] { width: auto; min-height: auto; }
        .cfg-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 14px;
            align-items: start;
        }
        .field-label { font-size: 13px; color: #94a3b8; display: block; margin-bottom: 4px; }
        .field-hint  { font-size: 11px; color: #64748b; margin-top: 3px; }
        .metric-card {
            background: #1e293b; border-radius: 6px; padding: 14px; text-align: center;
        }
        .metric-label { font-size: 11px; color: #94a3b8; text-transform: uppercase; margin-bottom: 4px; letter-spacing: 0.05em; }
        .metric-value { font-size: 28px; font-weight: 700; transition: color 0.3s ease; }
        .metric-unit  { font-size: 12px; color: #64748b; margin-top: 2px; }
        .metrics-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; margin-bottom: 14px; }
        @media (max-width: 480px) {
            .cfg-grid { grid-template-columns: 1fr; }
            .metrics-grid { grid-template-columns: 1fr; }
            .metric-value { font-size: 22px; }
        }
    </style>
    <main class='page'>
        <div class='wrap'>
            <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='otaTipHome'>&#127968; Home</a></div>
            <h1 data-i18n='ina226PageTitle'>&#9889;&#65039; INA226 Current/Power Monitor</h1>

            <!-- Section 1: Live readings -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='ina226LiveTitle'>&#128202; Real-Time Readings</h3>
                <div class='metrics-grid'>
                    <div class='metric-card'>
                        <div class='metric-label' data-i18n='ina226BusVoltage'>Bus Voltage</div>
                        <div class='metric-value' id='busVoltage' style='color:#4ade80'>--</div>
                        <div class='metric-unit'>V</div>
                    </div>
                    <div class='metric-card'>
                        <div class='metric-label' data-i18n='ina226Current'>Current</div>
                        <div class='metric-value' id='currentVal' style='color:#60a5fa'>--</div>
                        <div class='metric-unit' id='currentUnitLbl'>A</div>
                    </div>
                    <div class='metric-card'>
                        <div class='metric-label' data-i18n='ina226Power'>Power</div>
                        <div class='metric-value' id='powerVal' style='color:#f59e0b'>--</div>
                        <div class='metric-unit'>W</div>
                    </div>
                    <div class='metric-card'>
                        <div class='metric-label' data-i18n='ina226ShuntVoltage'>Shunt Voltage</div>
                        <div class='metric-value' id='shuntVoltage' style='color:#a855f7'>--</div>
                        <div class='metric-unit'>mV</div>
                    </div>
                </div>
                <div style='text-align:center;margin-bottom:10px'>
                    <div style='font-size:12px;color:#64748b' id='lastReadTime' data-i18n='hx711NoReading'>No reading</div>
                </div>
                <div style='text-align:center'>
                    <button id='btnMeasure' class='btn' style='background:#10b981;border-color:#10b981;padding:10px 20px' data-i18n='sensorMeasureNow' title='Read power values now' data-i18n-title='ina226TipMeasure'>&#128300; Measure Now</button>
                </div>
            </div>

            <!-- Section 2: Main control -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='hx711MainTitle'>&#9881;&#65039; Main Control</h3>
                <div style='display:flex;gap:15px;align-items:center;flex-wrap:wrap'>
                    <label class='checkbox-label' style='margin:0'>
                        <input type='checkbox' id='sensorEnabled' title='Enable this sensor' data-i18n-title='sensorTipEnable'>
                        <span data-i18n='ina226Enable'>Enable INA226 Sensor</span>
                    </label>
                    <div style='display:flex;align-items:center;gap:8px;flex:1;justify-content:flex-end'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='mq135IntervalLbl'>&#9203;&#65039; Interval:</label>
                        <select id='readInterval' style='max-width:160px' title='Reading interval (seconds)' data-i18n-title='sensorTipInterval'>
                            <option value='2' data-i18n='hx711Int2s'>2 seconds</option>
                            <option value='5' data-i18n='hx711Int5s'>5 seconds</option>
                            <option value='10' data-i18n='mq135Int10s'>10 seconds</option>
                            <option value='30' data-i18n='mq135Int30s'>30 seconds</option>
                            <option value='60' selected data-i18n='sensorInt1m'>1 minute</option>
                            <option value='300' data-i18n='sensorInt5m'>5 minutes</option>
                            <option value='600' data-i18n='sensorInt10m'>10 minutes</option>
                        </select>
                    </div>
                </div>
            </div>

            <!-- Section 3: Hardware configuration -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='ina226HwTitle'>&#128295; Hardware Configuration</h3>
                <div class='cfg-grid'>
                    <div>
                        <label class='field-label' data-i18n='ina226I2cAddr'>I2C Address</label>
                        <select id='i2cAddr' title='I2C device address' data-i18n-title='ina226TipI2c'>
                            <option value='64'>0x40 &mdash; A0=GND, A1=GND</option>
                            <option value='65'>0x41 &mdash; A0=VCC, A1=GND</option>
                            <option value='68'>0x44 &mdash; A0=GND, A1=VCC</option>
                            <option value='69'>0x45 &mdash; A0=VCC, A1=VCC</option>
                        </select>
                    </div>
                    <div>
                        <label class='field-label' data-i18n='ina226ShuntOhm'>Shunt Resistance (&Omega;)</label>
                        <input type='number' id='shuntOhm' step='0.0001' min='0.0001' value='0.1' style='max-width:130px' title='Shunt resistor value (ohms)' data-i18n-title='ina226TipShunt'>
                        <div class='field-hint' data-i18n='ina226ShuntHint'>Shunt resistor value in Ohm. E.g.: 0.1&Omega;</div>
                    </div>
                    <div>
                        <label class='field-label' data-i18n='ina226MaxCurrent'>Expected Maximum Current (A)</label>
                        <input type='number' id='maxCurrentA' step='0.1' min='0.1' value='10.0' style='max-width:130px' title='Max expected current (A)' data-i18n-title='ina226TipMaxCurrent'>
                        <div class='field-hint' data-i18n='ina226MaxCurrentHint'>Sets the internal ADC resolution. E.g.: 10 A</div>
                    </div>
                    <div>
                        <label class='field-label' data-i18n='ina226Averaging'>ADC Sample Averaging</label>
                        <select id='averaging' style='max-width:160px' title='ADC sample averaging' data-i18n-title='ina226TipAvg'>
                            <option value='1' data-i18n='ina226Avg1'>1 (no averaging)</option>
                            <option value='4' data-i18n='ina226Avg4'>4</option>
                            <option value='16' data-i18n='ina226Avg16'>16</option>
                            <option value='64' data-i18n='ina226Avg64'>64</option>
                            <option value='128' data-i18n='ina226Avg128'>128</option>
                            <option value='256' data-i18n='ina226Avg256'>256</option>
                            <option value='512' data-i18n='ina226Avg512'>512</option>
                            <option value='1024' data-i18n='ina226Avg1024'>1024</option>
                        </select>
                    </div>
                    <div>
                        <label class='field-label' data-i18n='ina226CurrentUnit'>Current Unit</label>
                        <select id='currentUnitSel' title='Current display unit' data-i18n-title='ina226TipUnit'>
                            <option value='0' data-i18n='ina226UnitA'>A (Ampere)</option>
                            <option value='1' data-i18n='ina226UnitMa'>mA (mili-Ampere)</option>
                        </select>
                    </div>
                </div>
            </div>

            <!-- Section 4: Active measurements -->
            <div class='section-box'>
                <h3 class='section-title' style='font-size:18px' data-i18n='ina226MeasTitle'>&#128268; Active Measurements</h3>
                <div style='display:flex;gap:12px;flex-wrap:wrap'>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='showBusV' title='Show bus voltage' data-i18n-title='ina226TipShowBusV'>
                        <span data-i18n='ina226ShowBusV'>Bus Voltage</span>
                    </label>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='showCurrent' title='Show current' data-i18n-title='ina226TipShowCurrent'>
                        <span data-i18n='ina226ShowCurrent'>Current</span>
                    </label>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='showPower' title='Show power' data-i18n-title='ina226TipShowPower'>
                        <span data-i18n='ina226ShowPower'>Power</span>
                    </label>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='showShuntV' title='Show shunt voltage' data-i18n-title='ina226TipShowShuntV'>
                        <span data-i18n='ina226ShowShuntV'>Shunt Voltage</span>
                    </label>
                </div>
            </div>

            <!-- Section 6: Output options -->
            <div class='section-box'>
                <h3 class='section-title' style='font-size:18px' data-i18n='sensorOutputTitle'>&#128228; Output Options</h3>
                <div style='display:flex;gap:20px;flex-wrap:wrap;align-items:center'>
                    <label class='checkbox-label' style='background:#1e3a8a;padding:8px 12px;border-radius:6px'>
                        <input type='checkbox' id='injectBuffer' title='Send data to monitor' data-i18n-title='sensorTipMonitor'>
                        <span style='font-weight:bold' data-i18n='sensorInjectBuffer'>&#128202; Data in Monitor</span>
                    </label>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='outputCsv' title='Use CSV output' data-i18n-title='sensorTipCsv'>
                        <span data-i18n='mq135DataImportCsv'>Data for Import (CSV)</span>
                    </label>
                </div>
            </div>

            <!-- Save button -->
            <div style='text-align:center'>
                <button id='btnSave' class='btn' style='padding:10px 24px;margin-top:20px' data-i18n='sensorSaveConfig' title='Save INA226 config' data-i18n-title='ina226TipSave'>&#128190; Save Configuration</button>
            </div>
            <div id='status' style='margin-top:15px;padding:12px;border-radius:6px;display:none'></div>
        </div>
    </main>

    <script>
        function t(key, fallback) {
            if (typeof window._t === 'function') return window._t(key, fallback);
            return fallback;
        }

        function uiLocale() {
            var code = (localStorage.getItem('ui_lang_code') || document.documentElement.lang || 'en').toLowerCase();
            return code.indexOf('en') === 0 ? 'en-GB' : 'pt-PT';
        }

        function updateDisplay(d) {
            // Bus voltage
            if (d.last_bus_v !== undefined) {
                var bvEl = document.getElementById('busVoltage');
                bvEl.textContent = Number(d.last_bus_v).toFixed(3);
                // No bus-voltage alert threshold, just keep green
            }

            // Current (convert to mA if unit=1)
            if (d.last_current_a !== undefined) {
                var isMa = (d.current_unit === 1);
                var cVal = isMa ? Number(d.last_current_a) * 1000 : Number(d.last_current_a);
                var cEl  = document.getElementById('currentVal');
                cEl.textContent = isMa ? cVal.toFixed(1) : cVal.toFixed(4);
                document.getElementById('currentUnitLbl').textContent = isMa ? 'mA' : 'A';
                cEl.style.color = '#60a5fa';
            }

            // Power
            if (d.last_power_w !== undefined) {
                var pEl = document.getElementById('powerVal');
                pEl.textContent = Number(d.last_power_w).toFixed(3);
                pEl.style.color = '#f59e0b';
            }

            // Shunt voltage: stored in V, display in mV
            if (d.last_shunt_v !== undefined) {
                document.getElementById('shuntVoltage').textContent = (Number(d.last_shunt_v) * 1000).toFixed(3);
            }

            // Timestamp
            var timeEl = document.getElementById('lastReadTime');
            if (d.last_reading_time && d.last_reading_time > 0) {
                var dt = new Date(d.last_reading_time * 1000);
                timeEl.textContent = t('sensorLastReadingPrefix', '&#128197; Last reading: ') +
                    dt.toLocaleTimeString(uiLocale()) + ' (' + dt.toLocaleDateString(uiLocale()) + ')';
            } else {
                timeEl.textContent = t('hx711NoReading', 'No reading');
            }
        }

        async function loadConfig() {
            try {
                var res = await fetch('/api/ina226-config', {cache:'no-store'});
                if (!res.ok) throw new Error(t('mq135LoadConfigFailed', 'Failed to load config'));
                var d = await res.json();

                document.getElementById('sensorEnabled').checked    = d.sensor_enabled;
                document.getElementById('readInterval').value       = String(d.read_interval_sec || 60);
                document.getElementById('i2cAddr').value            = String(d.i2c_addr || 64);
                document.getElementById('shuntOhm').value           = d.shunt_ohm || 0.1;
                document.getElementById('maxCurrentA').value        = d.max_current_a || 10.0;
                document.getElementById('averaging').value          = String(d.averaging || 4);
                document.getElementById('currentUnitSel').value     = String(d.current_unit || 0);
                document.getElementById('showBusV').checked         = d.show_bus_voltage;
                document.getElementById('showCurrent').checked      = d.show_current;
                document.getElementById('showPower').checked        = d.show_power;
                document.getElementById('showShuntV').checked       = d.show_shunt_voltage;
                document.getElementById('injectBuffer').checked     = d.inject_to_buffer;
                document.getElementById('outputCsv').checked        = d.output_csv;

                updateDisplay(d);

                if (!d.last_reading_time || d.last_reading_time === 0) {
                    setTimeout(measureNow, 600);
                }
            } catch (e) {
                showStatus(t('sensorLoadErrorPrefix', 'Error loading: ') + e.message, 'error');
            }
        }

        async function saveConfig() {
            disableBtn(btnSave);showLoading();
            try {
                var body = {
                    sensor_enabled:    document.getElementById('sensorEnabled').checked,
                    read_interval_sec: parseInt(document.getElementById('readInterval').value),
                    i2c_addr:          parseInt(document.getElementById('i2cAddr').value),
                    shunt_ohm:         parseFloat(document.getElementById('shuntOhm').value),
                    max_current_a:     parseFloat(document.getElementById('maxCurrentA').value),
                    averaging:         parseInt(document.getElementById('averaging').value),
                    current_unit:      parseInt(document.getElementById('currentUnitSel').value),
                    show_bus_voltage:  document.getElementById('showBusV').checked,
                    show_current:      document.getElementById('showCurrent').checked,
                    show_power:        document.getElementById('showPower').checked,
                    show_shunt_voltage:document.getElementById('showShuntV').checked,
                    inject_to_buffer:  document.getElementById('injectBuffer').checked,
                    output_csv:        document.getElementById('outputCsv').checked
                };
                try {
                    var res = await fetch('/api/ina226-config', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(body)
                    });
                    if (!res.ok) throw new Error(t('mq135SaveFailed', 'Failed to save'));
                    var d = await res.json();
                    if (d.ok) {
                        if (typeof customAlert   === 'function') customAlert(t('sensorAlertSaved', '&#10003;&#65039; Configuration saved!'));
                        if (typeof showSavedCard === 'function') showSavedCard();
                    } else {
                        if (typeof customAlert === 'function') customAlert(t('sensorAlertError', '&#10060; Error: ') + (d.msg || 'unknown'));
                    }
                } catch (e) {
                    if (typeof customAlert === 'function') customAlert(t('sensorAlertError', '&#10060; Error: ') + e.message);
                }
            } finally { hideLoading();enableBtn(btnSave); }
        }

        async function measureNow() {
            showStatus(t('sensorMeasuring', '&#128300; Measuring...'), 'info');
            try {
                var res = await fetch('/api/ina226-measure', { method: 'POST' });
                if (!res.ok) throw new Error(t('mq135MeasureFailed', 'Failed'));
                var d = await res.json();
                updateDisplay(d);
                showStatus(t('sensorMeasureDone', '&#10003;&#65039; Reading complete!'), 'success');
            } catch (e) {
                showStatus(t('sensorAlertError', '&#10060; Error: ') + e.message, 'error');
            }
        }

        function showStatus(msg, type) {
            if (typeof customAlert === 'function') { customAlert(msg); return; }
            var el = document.getElementById('status');
            if (!el) return;
            el.textContent = msg;
            el.style.display = 'block';
            el.style.border     = type === 'error' ? '2px solid #ef4444' : type === 'success' ? '2px solid #10b981' : '2px solid #3b82f6';
            el.style.background = type === 'error' ? '#7f1d1d' : type === 'success' ? '#064e3b' : '#1e3a8a';
            el.style.color = '#e6f1ff';
            setTimeout(function() { el.style.display = 'none'; }, 3000);
        }

        document.getElementById('btnSave').addEventListener('click', saveConfig);
        document.getElementById('btnMeasure').addEventListener('click', measureNow);

        setTimeout(loadConfig, 150);
    </script>
)rawliteral";

#endif // ENABLE_INA226==1
#endif // PAGE_INA226_ASSET_H

