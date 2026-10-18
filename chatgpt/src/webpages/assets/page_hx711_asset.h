#ifndef PAGE_HX711_ASSET_H
#define PAGE_HX711_ASSET_H

#include "../../feature_flags.h"

#if ENABLE_HX711==1

#include <Arduino.h>

// Page served at /hx711 — HX711 load cell / weight sensor configuration.
static const char HX711_CONFIG_PAGE[] PROGMEM = R"rawliteral(
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
        .weight-display {
            font-size: 48px; font-weight: 700; color: #4ade80; text-align: center;
            padding: 12px; transition: color 0.3s ease;
        }
        .weight-unit-lbl { font-size: 18px; color: #94a3b8; text-align: center; margin-top: 2px; }
        @media (max-width: 480px) {
            .cfg-grid { grid-template-columns: 1fr; }
            .weight-display { font-size: 36px; }
        }
    </style>
    <main class='page'>
        <div class='wrap'>
            <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='otaTipHome'>&#127968; Home</a></div>
            <h1 data-i18n='hx711PageTitle'>&#9878;&#65039; HX711 Weight Sensor</h1>

            <!-- Section 1: Current reading + controls -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='hx711ReadingTitle'>&#128202; Current Reading</h3>
                <div style='background:#1e293b;border-radius:8px;border:1px solid #334155;padding:16px;margin-bottom:14px;text-align:center'>
                    <div class='weight-display' id='weightValue'>--</div>
                    <div class='weight-unit-lbl' id='weightUnitLabel'>g</div>
                    <div style='font-size:12px;color:#64748b;margin-top:6px' id='lastReadTime' data-i18n='hx711NoReading'>No reading</div>
                </div>
                <div style='display:flex;gap:10px;justify-content:center;flex-wrap:wrap'>
                    <button id='btnMeasure' class='btn' style='background:#10b981;border-color:#10b981;padding:10px 20px' data-i18n='sensorMeasureNow' title='Take immediate reading' data-i18n-title='sensorTipMeasure'>&#128300; Measure Now</button>
                    <button id='btnTare' class='btn' style='background:#7c3aed;border-color:#7c3aed;padding:10px 20px' data-i18n='hx711TareBtn' title='Reset zero point' data-i18n-title='hx711TipTare'>&#9878;&#65039; Tare (Zero)</button>
                </div>
            </div>

            <!-- Section 2: Main control -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='hx711MainTitle'>&#9881;&#65039; Main Control</h3>
                <div style='display:flex;gap:15px;align-items:center;flex-wrap:wrap'>
                    <label class='checkbox-label' style='margin:0'>
                        <input type='checkbox' id='sensorEnabled' title='Enable this sensor' data-i18n-title='sensorTipEnable'>
                        <span data-i18n='hx711Enable'>Enable HX711 Sensor</span>
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

            <!-- Section 3: Calibration -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='hx711CalibrationTitle'>&#128295; Calibration</h3>
                <div class='cfg-grid'>
                    <div>
                        <label class='field-label' data-i18n='hx711GainLabel'>Amplifier Gain</label>
                        <select id='gain' title='Amplifier gain setting' data-i18n-title='hx711TipGain'>
                            <option value='128' data-i18n='hx711Gain128'>128 &mdash; Channel A (recommended)</option>
                            <option value='64' data-i18n='hx711Gain64'>64 &mdash; Channel A</option>
                            <option value='32' data-i18n='hx711Gain32'>32 &mdash; Channel B</option>
                        </select>
                    </div>
                    <div>
                        <label class='field-label' data-i18n='hx711SamplesLabel'>Number of Samples per Reading</label>
                        <input type='number' id='samples' min='1' max='64' value='8' style='max-width:80px' title='Samples per reading' data-i18n-title='hx711TipSamples'>
                        <div class='field-hint' data-i18n='hx711SamplesHint'>More samples = more stability, but slower.</div>
                    </div>
                    <div>
                        <label class='field-label' data-i18n='hx711ScaleLabel'>Scale Factor (ADC / unit)</label>
                        <input type='number' id='scaleFactor' step='0.001' value='1.0' style='max-width:130px' title='Calibration factor' data-i18n-title='hx711TipCalibration'>
                        <div class='field-hint' data-i18n='hx711ScaleHint'>E.g.: 441.0 for grams. Place a known weight and divide raw by actual value.</div>
                    </div>
                    <div>
                        <label class='field-label' data-i18n='hx711TareOffsetLabel'>Tare Offset (ADC raw)</label>
                        <input type='number' id='tareOffset' step='1' value='0' style='max-width:130px' title='Zero offset (raw counts)' data-i18n-title='hx711TipOffset'>
                        <div class='field-hint' data-i18n='hx711TareOffsetHint'>Auto-updated when pressing "Tare".</div>
                    </div>
                    <div>
                        <label class='field-label' data-i18n='hx711UnitLabel'>Weight Unit</label>
                        <select id='weightUnitSel' title='Weight display unit' data-i18n-title='hx711TipUnit'>
                            <option value='0' data-i18n='hx711UnitG'>g (gram)</option>
                            <option value='1' data-i18n='hx711UnitKg'>kg (kilogram)</option>
                            <option value='2' data-i18n='hx711UnitLbs'>lbs (pound)</option>
                            <option value='3' data-i18n='hx711UnitOz'>oz (ounce)</option>
                            <option value='4' data-i18n='hx711UnitN'>N (Newton)</option>
                        </select>
                    </div>
                </div>
            </div>

            <!-- Section 5: Output options -->
            <div class='section-box'>
                <h3 class='section-title' style='font-size:18px' data-i18n='sensorOutputTitle'>&#128228; Output Options</h3>
                <div style='display:flex;gap:20px;flex-wrap:wrap;align-items:center'>
                    <label class='checkbox-label' style='background:#1e3a8a;padding:8px 12px;border-radius:6px'>
                        <input type='checkbox' id='injectBuffer' title='Send data to monitor' data-i18n-title='sensorTipMonitor'>
                        <span style='font-weight:bold' data-i18n='sensorInjectBuffer'>&#128202; Monitor Data</span>
                    </label>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='outputCsv' title='Use CSV output' data-i18n-title='sensorTipCsv'>
                        <span data-i18n='mq135DataImportCsv'>Data for Import (CSV)</span>
                    </label>
                </div>
            </div>

            <!-- Save button -->
            <div style='text-align:center'>
                <button id='btnSave' class='btn' style='padding:10px 24px;margin-top:20px' data-i18n='sensorSaveConfig' title='Save HX711 config' data-i18n-title='hx711TipSave'>&#128190; Save Configuration</button>
            </div>
            <div id='status' style='margin-top:15px;padding:12px;border-radius:6px;display:none'></div>
        </div>
    </main>

    <script>
        var UNIT_LABELS = ['g', 'kg', 'lbs', 'oz', 'N'];

        function t(key, fallback) {
            if (typeof window._t === 'function') return window._t(key, fallback);
            return fallback;
        }

        function uiLocale() {
            var code = (localStorage.getItem('ui_lang_code') || document.documentElement.lang || 'en').toLowerCase();
            return code.indexOf('en') === 0 ? 'en-GB' : 'pt-PT';
        }

        function updateDisplay(d) {
            var unitIdx = (d.weight_unit !== undefined && d.weight_unit !== null) ? d.weight_unit : 0;
            var unitLabel = UNIT_LABELS[unitIdx] || 'g';
            document.getElementById('weightUnitLabel').textContent = unitLabel;

            var wEl = document.getElementById('weightValue');
            if (d.last_weight !== undefined && d.last_weight !== null) {
                wEl.textContent = Number(d.last_weight).toFixed(3);
                var w = parseFloat(d.last_weight);
                wEl.style.color = '#4ade80';
            }

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
                var res = await fetch('/api/hx711-config', {cache:'no-store'});
                if (!res.ok) throw new Error(t('mq135LoadConfigFailed', 'Failed to load config'));
                var d = await res.json();

                document.getElementById('sensorEnabled').checked  = d.sensor_enabled;
                document.getElementById('readInterval').value     = String(d.read_interval_sec || 60);
                document.getElementById('gain').value             = String(d.gain || 128);
                document.getElementById('samples').value          = d.samples || 8;
                document.getElementById('scaleFactor').value      = d.scale_factor || 1.0;
                document.getElementById('tareOffset').value       = d.tare_offset || 0;
                document.getElementById('weightUnitSel').value    = String(d.weight_unit || 0);
                document.getElementById('injectBuffer').checked   = d.inject_to_buffer;
                document.getElementById('outputCsv').checked      = d.output_csv;

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
                    gain:              parseInt(document.getElementById('gain').value),
                    samples:           parseInt(document.getElementById('samples').value),
                    scale_factor:      parseFloat(document.getElementById('scaleFactor').value),
                    tare_offset:       parseInt(document.getElementById('tareOffset').value),
                    weight_unit:       parseInt(document.getElementById('weightUnitSel').value),
                    inject_to_buffer:  document.getElementById('injectBuffer').checked,
                    output_csv:        document.getElementById('outputCsv').checked
                };
                try {
                    var res = await fetch('/api/hx711-config', {
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
                var res = await fetch('/api/hx711-measure', { method: 'POST' });
                if (!res.ok) throw new Error(t('mq135MeasureFailed', 'Failed'));
                var d = await res.json();
                updateDisplay(d);
                showStatus(t('sensorMeasureDone', '&#10003;&#65039; Reading complete!'), 'success');
            } catch (e) {
                showStatus(t('sensorAlertError', '&#10060; Error: ') + e.message, 'error');
            }
        }

        async function performTare() {
            var runTare = async function() {
                showStatus(t('hx711Taring', '&#9878;&#65039; Taring...'), 'info');
                try {
                    var res = await fetch('/api/hx711-tare', { method: 'POST' });
                    if (!res.ok) throw new Error(t('mq135MeasureFailed', 'Failed'));
                    var d = await res.json();
                    if (d.tare_offset !== undefined) {
                        document.getElementById('tareOffset').value = d.tare_offset;
                        updateDisplay(d);
                        showStatus(t('hx711TareDone', '&#10003;&#65039; Tare applied!'), 'success');
                    } else {
                        showStatus(t('sensorAlertError', '&#10060; Error: ') + (d.msg || 'unknown'), 'error');
                    }
                } catch (e) {
                    showStatus(t('sensorAlertError', '&#10060; Error: ') + e.message, 'error');
                }
            };
            var msg = t('hx711TareConfirm', 'Remove all objects from the scale and confirm tare?');
            if (typeof customConfirm === 'function') {
                customConfirm(msg, function(ok) { if (ok) runTare(); });
                return;
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
        document.getElementById('btnTare').addEventListener('click', performTare);

        setTimeout(loadConfig, 150);
    </script>
)rawliteral";

#endif // ENABLE_HX711==1
#endif // PAGE_HX711_ASSET_H

