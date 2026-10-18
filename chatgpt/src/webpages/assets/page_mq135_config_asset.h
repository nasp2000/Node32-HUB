#ifndef PAGE_MQ135_CONFIG_ASSET_H
#define PAGE_MQ135_CONFIG_ASSET_H

#include "../../feature_flags.h"

#if ENABLE_MQ135==1

#include <Arduino.h>

// Page served at /mq135-config for air-quality readings.
static const char MQ135_CONFIG_PAGE[] PROGMEM = R"rawliteral(
    <style>
        .sensor-grid {
            display: grid;
            grid-template-columns: auto 80px 130px 110px 80px;
            gap: 3px;
            align-items: center;
            margin: 15px 0;
            font-size: 14px;
        }
        .sensor-grid-header {
            font-weight: bold;
            color: #60a5fa;
            text-align: center;
            padding: 6px 4px;
            background: #1e3a8a;
            border-radius: 4px;
            font-size: 14px;
        }
        .sensor-reading-name { color: #e0e7ff; font-size: 14px; padding: 2px 4px; }
        .sensor-value {
            color: #4ade80;
            font-weight: bold;
            text-align: center;
            padding: 4px;
            background: #064e3b;
            border: 1px solid rgba(255,255,255,0.08);
            border-radius: 4px;
            font-size: 12px;
            transition: background-color .25s ease, color .25s ease, border-color .25s ease, box-shadow .25s ease;
        }
        .sensor-reading-cell {
            display: flex;
            flex-direction: column;
            gap: 3px;
        }
        .sensor-reference {
            color: #cbd5e1;
            text-align: center;
            font-size: 10px;
            line-height: 1.2;
            padding: 2px 4px;
            background: #1e293b;
            border: 1px solid #334155;
            border-radius: 4px;
        }
        .offset-input {
            width: 100%;
            padding: 4px;
            background: #1e293b;
            color: #e0e7ff;
            border: 1px solid #475569;
            border-radius: 4px;
            text-align: center;
            font-size: 12px;
        }
        .section-box {
            margin: 20px 0;
            padding: 18px;
            background: #0f172a;
            border-radius: 8px;
            border: 2px solid #3b82f6;
        }
        .section-title { margin: 0 0 15px 0; font-size: 20px; color: #60a5fa; font-weight: bold; }
        .output-row { margin: 10px 0; display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
        .checkbox-label {
            display: inline-flex; align-items: center; gap: 8px; cursor: pointer;
            padding: 6px 12px; background: #1e293b; border-radius: 6px; font-size: 13px;
        }
        .checkbox-label input[type="checkbox"] { width: auto; min-height: auto; }
        /* AQI colour band */
        .aqi-bar { height: 14px; border-radius: 6px; margin: 8px 0;
            background: linear-gradient(90deg,#10b981 0%,#84cc16 20%,#f59e0b 40%,#f97316 60%,#ef4444 80%,#7c3aed 100%); }
        .aqi-labels { display:flex; justify-content:space-between; font-size:10px; color:#94a3b8; padding:0 2px; }
        /* Warmup notice */
        .warmup-badge {
            display:inline-block; padding:4px 10px; border-radius:20px;
            background:#92400e; color:#fed7aa; font-size:12px; font-weight:600;
        }
        .ready-badge {
            display:inline-block; padding:4px 10px; border-radius:20px;
            background:#065f46; color:#d1fae5; font-size:12px; font-weight:600;
        }
        @media (max-width: 768px) {
            .sensor-grid { grid-template-columns: auto 55px 100px 85px 65px; gap: 2px; font-size: 12px; }
            .offset-input { padding: 3px; font-size: 11px; }
            .sensor-reference { font-size: 9px; padding: 2px; }
        }
        @media (max-width: 600px) {
            .sensor-grid { grid-template-columns: auto 50px 90px 80px 60px; font-size: 11px; }
            .sensor-reference { font-size: 8px; }
        }
    </style>
    <main class='page'>
        <div class='wrap'>
            <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='otaTipHome'>🏠 Home</a></div>
            <h1 data-i18n='mq135PageTitle'>🌫️ Air Quality — MQ-135</h1>

            <!-- Section 1: Status + Controls -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='mq135MainTitle'>⚙️ Main Control</h3>
                <div style='display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:15px'>
                    <label class='checkbox-label' style='margin:0'>
                        <input type='checkbox' id='sensorEnabled' checked title='Enable this sensor' data-i18n-title='sensorTipEnable'>
                        <span data-i18n='mq135Enable'>Enable MQ-135 Sensor</span>
                    </label>
                    <div style='display:flex;align-items:center;gap:8px;flex:1;justify-content:flex-end'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap' data-i18n='mq135IntervalLbl'>⏱️ Interval:</label>
                        <select id='readInterval' style='max-width:150px' title='Reading interval (seconds)' data-i18n-title='sensorTipInterval'>
                            <option value='60' selected data-i18n='sensorInt1m'>1 minute</option>
                            <option value='120' data-i18n='sensorInt2m'>2 minutes</option>
                            <option value='180' data-i18n='sensorInt3m'>3 minutes</option>
                            <option value='300' data-i18n='sensorInt5m'>5 minutes</option>
                            <option value='600' data-i18n='sensorInt10m'>10 minutes</option>
                            <option value='1800' data-i18n='sensorInt30m'>30 minutes</option>
                            <option value='3600' data-i18n='sensorInt1h'>1 hour</option>
                            <option value='10800' data-i18n='sensorInt3h'>3 hours</option>
                            <option value='21600' data-i18n='sensorInt6h'>6 hours</option>
                            <option value='43200' data-i18n='sensorInt12h'>12 hours</option>
                            <option value='86400' data-i18n='sensorInt24h'>24 hours</option>
                        </select>
                    </div>
                </div>
                <div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:12px'>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='useEnvCompensation' checked title='Use AHT20 for compensation' data-i18n-title='mq135TipCompensation'>
                        <span data-i18n='mq135EnvCompensation'>📡 Temp+Humidity Compensation (AHT20)</span>
                    </label>
                </div>
                <div style='text-align:center;margin-bottom:12px'>
                    <button id='btnMeasure' class='btn' style='padding:6px 14px;background:#10b981;border-color:#10b981;font-size:13px' data-i18n='sensorMeasureNow' title='Take immediate reading' data-i18n-title='sensorTipMeasure'>🔬 Measure Now</button>
                </div>
                <div id='lastReadingTime' style='padding:10px;font-size:13px;color:#e0e7ff;text-align:center;background:#1e3a8a;border-radius:6px;border:1px solid #3b82f6' data-i18n='mq135NoReading'>No reading available</div>
                <div id='warmupStatus' style='text-align:center;margin-top:10px'></div>
            </div>

            <!-- Section 2: Calibration -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='mq135CalibrationTitle'>🔧 Calibration</h3>
                <div style='display:grid;grid-template-columns:1fr 1fr;gap:12px;align-items:center'>
                    <div>
                        <label style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px' data-i18n='mq135R0Label'>R0 — Clean Air Resistance (Ohm)</label>
                        <div style='display:flex;gap:6px'>
                            <input type='number' id='r0Value' class='offset-input' style='max-width:120px;text-align:left;padding:6px' step='0.01' min='0.01' value='76.63' title='Clean air calibration reference (Ro)' data-i18n-title='mq135TipRo'>
                            <button id='btnCalibrateR0' class='btn' style='padding:6px 10px;font-size:12px;background:#7c3aed;border-color:#7c3aed;white-space:nowrap' data-i18n='mq135MeasureR0Btn' title='Calibrate Ro in clean air' data-i18n-title='mq135TipCalibrate'>📐 Measure Current R0</button>
                        </div>
                        <div style='font-size:11px;color:#94a3b8;margin-top:4px' data-i18n='mq135R0Hint'>Place the sensor in clean outdoor air for 24h+ before calibrating.</div>
                    </div>
                    <div>
                        <label style='font-size:13px;color:#94a3b8;display:block;margin-bottom:4px' data-i18n='mq135AdcPinLabel'>ADC Pin (GPIO)</label>
                        <input type='number' id='adcPin' class='offset-input' style='max-width:80px;text-align:left;padding:6px' min='0' max='47' value='4' title='GPIO pin number' data-i18n-title='sensorTipGpio'>
                        <div style='font-size:11px;color:#94a3b8;margin-top:4px' data-i18n='mq135AdcPinHint'>GPIO connected to the module's AOUT pin.</div>
                    </div>
                </div>
                <!-- Live calibration values -->
                <div style='display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:14px'>
                    <div style='padding:10px;background:#1e293b;border-radius:6px;border:1px solid #334155;text-align:center'>
                        <div style='font-size:10px;color:#94a3b8;margin-bottom:4px;text-transform:uppercase'>ADC Raw</div>
                        <div style='font-size:16px;font-weight:700;color:#60a5fa' id='valRawAdc'>--</div>
                    </div>
                    <div style='padding:10px;background:#1e293b;border-radius:6px;border:1px solid #334155;text-align:center'>
                        <div style='font-size:10px;color:#94a3b8;margin-bottom:4px;text-transform:uppercase'>Rs (Ohm)</div>
                        <div style='font-size:16px;font-weight:700;color:#60a5fa' id='valRs'>--</div>
                    </div>
                    <div style='padding:10px;background:#1e293b;border-radius:6px;border:1px solid #334155;text-align:center'>
                        <div style='font-size:10px;color:#94a3b8;margin-bottom:4px;text-transform:uppercase'>Rs/R0</div>
                        <div style='font-size:16px;font-weight:700;color:#60a5fa' id='valRatio'>--</div>
                    </div>
                </div>
            </div>

            <!-- Section 3: Gas Readings -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='mq135DetectedGasesTitle'>📊 Detected Gases</h3>
                <div style='overflow-x:auto;-webkit-overflow-scrolling:touch'><div class='sensor-grid'>
                    <div class='sensor-grid-header' data-i18n='mq135ColGas'>Gas / Measurement</div>
                    <div class='sensor-grid-header' data-i18n='sensorGridActive'>Active</div>
                    <div class='sensor-grid-header' data-i18n='mq135ColLastReading'>Last Reading</div>
                    <div class='sensor-grid-header' data-i18n='mq135ColOffsetPpm'>Offset (ppm)</div>
                    <div class='sensor-grid-header' data-i18n='mq135ColHistory'>History</div>

                    <!-- CO2 (corrected) — stored slot 0 in history -->
                    <div class='sensor-reading-name' data-i18n='mq135GasCo2'>🌫️ CO₂ (corrected)</div>
                    <div style='text-align:center'><input type='checkbox' id='en_co2' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='mq135RefCo2'>Ref. good air: 400-800 ppm</div><div class='sensor-value' id='val_co2'>-- ppm</div></div>
                    <div><input type='number' id='off_co2' class='offset-input' step='1' min='-200' max='200' value='0' title='Calibration offset (ppm)' data-i18n-title='mq135TipOffset'></div>
                    <div style='text-align:center;font-size:11px;color:#4ade80' data-i18n='mq135Slot0'>Slot 0</div>

                    <!-- CO -->
                    <div class='sensor-reading-name' data-i18n='mq135GasCo'>💨 CO</div>
                    <div style='text-align:center'><input type='checkbox' id='en_co' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='mq135RefCo'>Ref. good air: &lt; 9 ppm</div><div class='sensor-value' id='val_co'>-- ppm</div></div>
                    <div><input type='number' id='off_co' class='offset-input' step='0.1' min='-50' max='50' value='0' title='Calibration offset (ppm)' data-i18n-title='mq135TipOffset'></div>
                    <div style='text-align:center;font-size:11px;color:#4ade80' data-i18n='mq135Slot1'>Slot 1</div>

                    <!-- NH3 -->
                    <div class='sensor-reading-name' data-i18n='mq135GasNh3'>⚗️ NH₃ (Ammonia)</div>
                    <div style='text-align:center'><input type='checkbox' id='en_nh3' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='mq135RefNh3'>Ref. good air: &lt; 1 ppm</div><div class='sensor-value' id='val_nh3'>-- ppm</div></div>
                    <div><input type='number' id='off_nh3' class='offset-input' step='0.1' min='-50' max='50' value='0' title='Calibration offset (ppm)' data-i18n-title='mq135TipOffset'></div>
                    <div style='text-align:center;font-size:11px;color:#4ade80' data-i18n='mq135Slot2'>Slot 2</div>

                    <!-- NOx -->
                    <div class='sensor-reading-name' data-i18n='mq135GasNox'>🔴 NOx</div>
                    <div style='text-align:center'><input type='checkbox' id='en_nox' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='mq135RefNox'>Ref. good air: &lt; 0.1 ppm</div><div class='sensor-value' id='val_nox'>-- ppm</div></div>
                    <div><input type='number' id='off_nox' class='offset-input' step='0.1' min='-50' max='50' value='0' title='Calibration offset (ppm)' data-i18n-title='mq135TipOffset'></div>
                    <div style='text-align:center;font-size:11px;color:#4ade80' data-i18n='mq135Slot3'>Slot 3</div>

                    <!-- Alcohol -->
                    <div class='sensor-reading-name' data-i18n='mq135GasAlcohol'>🍺 Alcohol / Ethanol</div>
                    <div style='text-align:center'><input type='checkbox' id='en_alcohol' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='mq135RefAlcohol'>Ref. good air: ~0 ppm</div><div class='sensor-value' id='val_alcohol'>-- ppm</div></div>
                    <div><input type='number' id='off_alcohol' class='offset-input' step='0.1' min='-50' max='50' value='0' title='Calibration offset (ppm)' data-i18n-title='mq135TipOffset'></div>
                    <div style='text-align:center;font-size:11px;color:#4ade80' data-i18n='mq135Slot4'>Slot 4</div>

                    <!-- Benzene — display only -->
                    <div class='sensor-reading-name' data-i18n='mq135GasBenzene'>☠️ Benzene</div>
                    <div style='text-align:center'><input type='checkbox' id='en_benzene' title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='mq135RefBenzene'>Ref. good air: &lt; 0.1 ppm</div><div class='sensor-value' id='val_benzene'>-- ppm</div></div>
                    <div><input type='number' id='off_benzene' class='offset-input' step='0.1' min='-20' max='20' value='0' title='Calibration offset (ppm)' data-i18n-title='mq135TipOffset'></div>
                    <div style='text-align:center;font-size:10px;color:#94a3b8' data-i18n='mq135MonitorOnly'>monitor only</div>

                    <!-- Smoke -- display only -->
                    <div class='sensor-reading-name' data-i18n='mq135GasSmoke'>🔥 Smoke</div>
                    <div style='text-align:center'><input type='checkbox' id='en_smoke' title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='mq135RefSmoke'>Ref. good air: ~0 ppm</div><div class='sensor-value' id='val_smoke'>-- ppm</div></div>
                    <div style='text-align:center;color:#94a3b8;font-size:11px'>—</div>
                    <div style='text-align:center;font-size:10px;color:#94a3b8' data-i18n='mq135MonitorOnly'>monitor only</div>

                    <!-- AQI -->
                    <div class='sensor-reading-name' data-i18n='mq135GasAqi'>🌡️ AQI (Air Quality Index)</div>
                    <div style='text-align:center'><input type='checkbox' id='en_aqi' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='mq135RefAqi'>Good: 0-50</div><div class='sensor-value' id='val_aqi'>--</div></div>
                    <div style='text-align:center;color:#94a3b8;font-size:11px'>—</div>
                    <div style='text-align:center;font-size:11px;color:#4ade80' data-i18n='mq135Slot5'>Slot 5</div>
                </div></div>

                <!-- AQI reference bar -->
                <div style='margin-top:16px'>
                    <div style='font-size:12px;color:#94a3b8;margin-bottom:6px' data-i18n='mq135AqiScale'>AQI Scale:</div>
                    <div class='aqi-bar'></div>
                    <div class='aqi-labels'>
                        <span data-i18n='mq135AqiBandGood'>0 Good</span><span>50</span><span data-i18n='mq135AqiBandModerate'>100 Moderate</span><span data-i18n='mq135AqiBandBad'>200 Unhealthy</span><span>300</span><span data-i18n='mq135AqiBandDanger'>500 Hazardous</span>
                    </div>
                    <div id='aqiIndicator' style='margin-top:8px;text-align:center;font-size:14px;font-weight:700'></div>
                </div>
            </div>

            <!-- Section 4: Output Options -->
            <div class='section-box'>
                <h3 class='section-title' style='font-size:18px' data-i18n='sensorOutputTitle'>📤 Output Options</h3>
                <div style='display:flex;gap:20px;margin-bottom:12px;flex-wrap:wrap;align-items:center'>
                    <label class='checkbox-label' style='background:#1e3a8a;padding:8px 12px;border-radius:6px'>
                        <input type='checkbox' id='injectBuffer' checked title='Send data to monitor' data-i18n-title='sensorTipMonitor'>
                        <span style='font-weight:bold' data-i18n='sensorInjectBuffer'>📊 Monitor Data</span>
                    </label>
                    <div style='color:#94a3b8;font-size:13px;margin:0 5px' data-i18n='sensorFormatLabel'>→ Format:</div>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='outputPretty' title='Use formatted output' data-i18n-title='sensorTipFormatted'>
                        <span data-i18n='sensorDataFormatted'>Formatted Data</span>
                    </label>
                    <label class='checkbox-label'>
                        <input type='checkbox' id='outputCSV' checked title='Use CSV output' data-i18n-title='sensorTipCsv'>
                        <span data-i18n='mq135DataImportCsv'>Data for Import (CSV)</span>
                    </label>
                </div>
            </div>

            <!-- Save Button -->
            <div style='text-align:center'>
                <button id='btnSave' class='btn' style='padding:10px 24px;margin-top:20px' data-i18n='sensorSaveConfig' title='Save MQ-135 config' data-i18n-title='mq135TipSave'>💾 Save Configuration</button>
            </div>
            <div id='status' style='margin-top:15px;padding:12px;border-radius:6px;display:none'></div>
        </div>
    </main>

    <script>
        let prevValues = {};

        function t(key, fallback) {
            if (typeof window._t === 'function') return window._t(key, fallback);
            return fallback;
        }

        function uiLocale() {
            const code = (localStorage.getItem('ui_lang_code') || document.documentElement.lang || 'en').toLowerCase();
            return code.startsWith('en') ? 'en-GB' : 'pt-PT';
        }

        function trendArrow(id, curr, delta) {
            const prev = prevValues[id];
            if (prev === undefined || isNaN(curr)) return '';
            const diff = curr - prev;
            if (Math.abs(diff) < delta) return ' <span style="color:#94a3b8;font-size:10px">→</span>';
            return diff > 0
                ? ' <span style="color:#4ade80;font-size:10px">↑</span>'
                : ' <span style="color:#f87171;font-size:10px">↓</span>';
        }

        // AQI colour + label
        function aqiInfo(aqi) {
            if (aqi <= 50)  return { color: '#10b981', label: t('mq135AqiGood', '🟢 Good') };
            if (aqi <= 100) return { color: '#84cc16', label: t('mq135AqiModerate', '🟡 Moderate') };
            if (aqi <= 150) return { color: '#f59e0b', label: t('mq135AqiSensitive', '🟠 Unhealthy for Sensitive Groups') };
            if (aqi <= 200) return { color: '#f97316', label: t('mq135AqiUnhealthy', '🔴 Unhealthy') };
            if (aqi <= 300) return { color: '#ef4444', label: t('mq135AqiVeryUnhealthy', '🔴 Very Unhealthy') };
            return { color: '#7c3aed', label: t('mq135AqiDangerous', '☠️ Hazardous') };
        }

        function pickReadableTextColor(bg) {
            const hex = String(bg || '').replace('#', '');
            if (hex.length !== 6) return '#ffffff';
            const r = parseInt(hex.substring(0, 2), 16);
            const g = parseInt(hex.substring(2, 4), 16);
            const b = parseInt(hex.substring(4, 6), 16);
            const luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255;
            return luminance > 0.62 ? '#0f172a' : '#f8fafc';
        }

        function applySensorValueTheme(id, tone) {
            const el = document.getElementById(id);
            if (!el) return;
            const bg = (tone && tone.bg) ? tone.bg : '#064e3b';
            const fg = (tone && tone.fg) ? tone.fg : pickReadableTextColor(bg);
            const border = (tone && tone.border) ? tone.border : bg;
            el.style.background = bg;
            el.style.color = fg;
            el.style.borderColor = border;
            el.style.boxShadow = 'inset 0 0 0 1px rgba(255,255,255,0.04)';
            el.style.textShadow = fg === '#0f172a' ? '0 1px 0 rgba(255,255,255,0.18)' : '0 1px 1px rgba(0,0,0,0.45)';
        }

        function mqTone(kind, value) {
            if (isNaN(value)) return { bg: '#064e3b', border: '#065f46' };
            if (kind === 'aqi') {
                if (value <= 50)  return { bg: '#10b981', border: '#059669' };
                if (value <= 100) return { bg: '#84cc16', border: '#65a30d' };
                if (value <= 150) return { bg: '#facc15', border: '#ca8a04' };
                if (value <= 200) return { bg: '#fb923c', border: '#ea580c' };
                if (value <= 300) return { bg: '#ef4444', border: '#dc2626' };
                return { bg: '#7c3aed', border: '#6d28d9' };
            }
            if (kind === 'co2') {
                if (value >= 400 && value <= 800) return { bg: '#10b981', border: '#059669' };
                if ((value >= 350 && value < 400) || (value > 800 && value <= 1200)) return { bg: '#84cc16', border: '#65a30d' };
                if (value <= 2000) return { bg: '#f59e0b', border: '#d97706' };
                if (value <= 5000) return { bg: '#ef4444', border: '#dc2626' };
                return { bg: '#7c3aed', border: '#6d28d9' };
            }
            if (kind === 'co') {
                if (value <= 9) return { bg: '#10b981', border: '#059669' };
                if (value <= 20) return { bg: '#84cc16', border: '#65a30d' };
                if (value <= 50) return { bg: '#f59e0b', border: '#d97706' };
                if (value <= 150) return { bg: '#ef4444', border: '#dc2626' };
                return { bg: '#7c3aed', border: '#6d28d9' };
            }
            if (kind === 'nh3') {
                if (value <= 1) return { bg: '#10b981', border: '#059669' };
                if (value <= 3) return { bg: '#84cc16', border: '#65a30d' };
                if (value <= 10) return { bg: '#f59e0b', border: '#d97706' };
                if (value <= 25) return { bg: '#ef4444', border: '#dc2626' };
                return { bg: '#7c3aed', border: '#6d28d9' };
            }
            if (kind === 'nox' || kind === 'benzene') {
                if (value <= 0.1) return { bg: '#10b981', border: '#059669' };
                if (value <= 0.3) return { bg: '#84cc16', border: '#65a30d' };
                if (value <= 1) return { bg: '#f59e0b', border: '#d97706' };
                if (value <= 5) return { bg: '#ef4444', border: '#dc2626' };
                return { bg: '#7c3aed', border: '#6d28d9' };
            }
            if (kind === 'alcohol' || kind === 'smoke') {
                if (value <= 0.5) return { bg: '#10b981', border: '#059669' };
                if (value <= 2) return { bg: '#84cc16', border: '#65a30d' };
                if (value <= 10) return { bg: '#f59e0b', border: '#d97706' };
                if (value <= 100) return { bg: '#ef4444', border: '#dc2626' };
                return { bg: '#7c3aed', border: '#6d28d9' };
            }
            return { bg: '#1e293b', border: '#475569' };
        }

        async function loadConfig() {
            try {
                const res = await fetch('/api/mq135-config',{cache:'no-store'});
                if (!res.ok) throw new Error(t('mq135LoadConfigFailed', 'Failed to load config'));
                const cfg = await res.json();

                document.getElementById('sensorEnabled').checked       = cfg.sensor_enabled;
                document.getElementById('readInterval').value           = cfg.read_interval_sec;
                document.getElementById('useEnvCompensation').checked  = cfg.use_env_compensation;
                document.getElementById('injectBuffer').checked         = cfg.inject_to_buffer;
                document.getElementById('outputPretty').checked         = cfg.output_pretty;
                document.getElementById('outputCSV').checked            = cfg.output_csv;
                document.getElementById('r0Value').value                = cfg.r0;
                document.getElementById('adcPin').value                 = cfg.adc_pin;

                const gasMap = [
                    ['co2', 'co2'], ['co', 'co'], ['nh3', 'nh3'], ['nox', 'nox'],
                    ['alcohol', 'alcohol'], ['benzene', 'benzene'], ['smoke', 'smoke'], ['aqi', 'aqi']
                ];
                gasMap.forEach(([key, id]) => {
                    const enEl  = document.getElementById('en_'  + id);
                    const offEl = document.getElementById('off_' + id);
                    if (enEl  && cfg['co2' === key ? 'gas_co2'
                                     : key === 'co' ? 'gas_co'
                                     : 'gas_' + key] !== undefined) {
                        // flat keys from toJson()
                    }
                    if (enEl  && cfg[key + '_en']  !== undefined) enEl.checked  = cfg[key + '_en'];
                    if (offEl && cfg[key + '_offset'] !== undefined) offEl.value = cfg[key + '_offset'];
                });

                updateReadingValues(cfg);
                showStatus(t('sensorConfigLoaded', 'Configuration loaded'), 'success');

                if (!cfg.last_reading_time || cfg.last_reading_time === 0) {
                    setTimeout(() => measureNow(), 600);
                }
            } catch (e) {
                showStatus(t('sensorLoadErrorPrefix', 'Error loading: ') + e.message, 'error');
            }
        }

        function updateReadingValues(cfg) {
            const hasData = cfg.last_reading_time && cfg.last_reading_time > 0;

            const timeEl = document.getElementById('lastReadingTime');
            if (!hasData) {
                timeEl.textContent = t('mq135NoReading', 'No reading available');
                timeEl.style.background = '#1e3a8a';
                return;
            }
            const d = new Date(cfg.last_reading_time * 1000);
            timeEl.textContent = t('sensorLastReadingPrefix', '📅 Last reading: ') + d.toLocaleTimeString(uiLocale()) + ' (' + d.toLocaleDateString(uiLocale()) + ')';
            timeEl.style.background = '#065f46';

            // Warm-up badge
            const wu = document.getElementById('warmupStatus');
            if (cfg.warmed_up) {
                wu.innerHTML = '<span class="ready-badge">' + t('mq135Stable', '✅ Sensor Stable') + '</span>';
            } else {
                const sec = cfg.warmup_remaining_sec || 0;
                const m = Math.floor(sec / 60), s = sec % 60;
                wu.innerHTML = '<span class="warmup-badge">' + t('mq135WarmupPrefix', '⏳ Warmup: ') + m + ':' + String(s).padStart(2,'0') + ' ' + t('mq135Remaining', 'remaining') + '</span>';
            }

            const sv = (id, val, unit, delta) => {
                const v = parseFloat(val);
                const el = document.getElementById(id);
                if (el) el.innerHTML = isNaN(v) ? '--' : v.toFixed(unit === '' ? 0 : 1) + (unit ? ' ' + unit : '') + trendArrow(id, v, delta);
                applySensorValueTheme(id, mqTone(id.replace('val_', ''), v));
                prevValues[id] = v;
            };
            sv('val_co2',     cfg.last_co2,     'ppm', 5);
            sv('val_co',      cfg.last_co,      'ppm', 1);
            sv('val_nh3',     cfg.last_nh3,     'ppm', 1);
            sv('val_nox',     cfg.last_nox,     'ppm', 1);
            sv('val_alcohol', cfg.last_alcohol, 'ppm', 1);
            sv('val_benzene', cfg.last_benzene, 'ppm', 0.5);
            sv('val_smoke',   cfg.last_smoke,   'ppm', 5);
            sv('val_aqi',     cfg.last_aqi,     '',   2);

            // Calibration raw values
            const rawEl   = document.getElementById('valRawAdc');
            const rsEl    = document.getElementById('valRs');
            const ratioEl = document.getElementById('valRatio');
            if (rawEl)   rawEl.textContent   = cfg.last_raw_adc  != null ? cfg.last_raw_adc  : '--';
            if (rsEl)    rsEl.textContent    = cfg.last_rs       != null ? parseFloat(cfg.last_rs).toFixed(1) + ' Ω' : '--';
            if (ratioEl) ratioEl.textContent = cfg.last_ratio    != null ? parseFloat(cfg.last_ratio).toFixed(3) : '--';

            // AQI indicator
            const aqiEl = document.getElementById('aqiIndicator');
            if (aqiEl && cfg.last_aqi != null) {
                const info = aqiInfo(parseFloat(cfg.last_aqi));
                aqiEl.textContent = info.label + ' (' + Math.round(cfg.last_aqi) + ')';
                aqiEl.style.color = info.color;
            }
        }

        async function saveConfig() {
            disableBtn(btnSave);showLoading();
            try {
                const cfg = {
                    sensor_enabled:       document.getElementById('sensorEnabled').checked,
                    read_interval_sec:    parseInt(document.getElementById('readInterval').value),
                    use_env_compensation: document.getElementById('useEnvCompensation').checked,
                    inject_to_buffer:     document.getElementById('injectBuffer').checked,
                    output_pretty:        document.getElementById('outputPretty').checked,
                    output_csv:           document.getElementById('outputCSV').checked,
                    r0:   parseFloat(document.getElementById('r0Value').value),
                    adc_pin: parseInt(document.getElementById('adcPin').value),

                    co2_en:      document.getElementById('en_co2').checked,
                    co2_offset:  parseFloat(document.getElementById('off_co2').value) || 0,
                    co_en:       document.getElementById('en_co').checked,
                    co_offset:   parseFloat(document.getElementById('off_co').value) || 0,
                    nh3_en:      document.getElementById('en_nh3').checked,
                    nh3_offset:  parseFloat(document.getElementById('off_nh3').value) || 0,
                    nox_en:      document.getElementById('en_nox').checked,
                    nox_offset:  parseFloat(document.getElementById('off_nox').value) || 0,
                    alcohol_en:  document.getElementById('en_alcohol').checked,
                    alcohol_offset: parseFloat(document.getElementById('off_alcohol').value) || 0,
                    benzene_en:  document.getElementById('en_benzene').checked,
                    benzene_offset: parseFloat(document.getElementById('off_benzene').value) || 0,
                    smoke_en:    document.getElementById('en_smoke').checked,
                    aqi_en:      document.getElementById('en_aqi').checked
                };

                try {
                    const res = await fetch('/api/mq135-config', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(cfg)
                    });
                    if (!res.ok) throw new Error(t('mq135SaveFailed', 'Failed to save'));
                    if (typeof customAlert   === 'function') customAlert(t('sensorAlertSaved', '✅ Configuration saved!'));
                    if (typeof showSavedCard === 'function') showSavedCard();
                } catch (e) {
                    if (typeof customAlert === 'function') customAlert(t('sensorAlertError', '❌ Error: ') + e.message);
                }
            } finally { hideLoading();enableBtn(btnSave); }
        }

        async function measureNow() {
            showStatus(t('sensorMeasuring', '🔬 Measuring...'), 'info');
            try {
                const res = await fetch('/api/mq135-measure', { method: 'POST' });
                if (!res.ok) throw new Error(t('mq135MeasureFailed', 'Failed'));
                const cfg = await res.json();
                updateReadingValues(cfg);
                showStatus(t('sensorMeasureDone', '✅ Reading complete!'), 'success');
            } catch (e) {
                showStatus(t('sensorAlertError', '❌ Error: ') + e.message, 'error');
            }
        }

        async function calibrateR0() {
            showStatus(t('mq135CalibratingR0', '📐 Calculating R0...'), 'info');
            try {
                await fetch('/api/mq135-measure', { method: 'POST' });
                const res = await fetch('/api/mq135-calibrate-r0', { method: 'POST' });
                const data = await res.json();
                if (!res.ok || !data.ok) throw new Error(data.msg || t('mq135CalibrationFailed', 'Failed'));
                document.getElementById('r0Value').value = data.r0.toFixed(4);
                showStatus('✅ R0 = ' + data.r0.toFixed(4) + ' Ω ' + t('mq135AssumedCleanAir', '(clean air assumed)'), 'success');
            } catch (e) {
                showStatus(t('sensorAlertError', '❌ Error: ') + e.message, 'error');
            }
        }

        function showStatus(msg, type) {
            if (typeof customAlert === 'function') { customAlert(msg); return; }
            const el = document.getElementById('status');
            if (!el) return;
            el.textContent = msg;
            el.style.display = 'block';
            el.style.border  = type === 'error' ? '2px solid #ef4444' : type === 'success' ? '2px solid #10b981' : '2px solid #3b82f6';
            el.style.background = type === 'error' ? '#7f1d1d' : type === 'success' ? '#064e3b' : '#1e3a8a';
            el.style.color = '#e6f1ff';
            setTimeout(() => el.style.display = 'none', 3000);
        }

        // Mutual exclusion pretty/csv
        document.getElementById('outputPretty').addEventListener('change', function() {
            if (this.checked) document.getElementById('outputCSV').checked = false;
        });
        document.getElementById('outputCSV').addEventListener('change', function() {
            if (this.checked) document.getElementById('outputPretty').checked = false;
        });

        document.getElementById('btnSave').addEventListener('click', saveConfig);
        document.getElementById('btnMeasure').addEventListener('click', measureNow);
        document.getElementById('btnCalibrateR0').addEventListener('click', calibrateR0);

        // Small delay so /api/lang fetch starts before config fetch to avoid competing on the device.
        setTimeout(loadConfig, 150);
    </script>
)rawliteral";

#endif // ENABLE_MQ135==1
#endif // PAGE_MQ135_CONFIG_ASSET_H

