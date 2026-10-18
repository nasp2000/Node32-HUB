#ifndef PAGE_SENSOR_CONFIG_ASSET_H
#define PAGE_SENSOR_CONFIG_ASSET_H

#include "../../feature_flags.h"

#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1

#include <Arduino.h>

// ====================================================================================
// Sensor Configuration Page HTML
// ====================================================================================

static const char SENSOR_CONFIG_PAGE[] PROGMEM = R"rawliteral(
    <style>
        /* Specific styles for sensor page */
        .sensor-grid {
            display: grid;
            grid-template-columns: auto 80px 120px 120px;
            gap: 3px;
            align-items: center;
            margin: 15px 0;
            font-size: 14px;
        }
        .sensor-grid input[type='checkbox'] {
            transform: scale(0.8);
            cursor: pointer;
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
        .sensor-reading-name {
            color: #e0e7ff;
            font-size: 14px;
            padding: 2px 4px;
        }
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
        
        @media (max-width: 768px) {
            .sensor-grid {
                grid-template-columns: auto 60px 100px 90px;
                gap: 2px;
                font-size: 13px;
            }
            .offset-input {
                padding: 4px;
                font-size: 11px;
            }
            .sensor-reading-name {
                font-size: 12px;
            }
            .sensor-value {
                font-size: 11px;
                padding: 4px 2px;
            }
            .sensor-reference {
                font-size: 9px;
                padding: 2px;
            }
            .section-box {
                padding: 12px;
                margin: 15px 0;
            }
        }
        
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
        .output-row {
            margin: 10px 0;
            display: flex;
            align-items: center;
            gap: 10px;
            flex-wrap: wrap;
        }
        .checkbox-label {
            display: inline-flex;
            align-items: center;
            gap: 8px;
            cursor: pointer;
            padding: 6px 12px;
            background: #1e293b;
            border-radius: 6px;
            font-size: 13px;
        }
        .checkbox-label input[type="checkbox"] {
            width: auto;
            min-height: auto;
        }
        @media (max-width: 600px) {
            .sensor-grid {
                grid-template-columns: auto 60px 90px 90px;
                gap: 6px;
                font-size: 11px;
            }
            .sensor-grid-header, .sensor-reading-name {
                font-size: 10px;
            }
            .sensor-value {
                font-size: 10px;
                padding: 4px 2px;
            }
            .sensor-reference {
                font-size: 8px;
            }
            .offset-input {
                font-size: 11px;
                padding: 4px;
            }
        }
    </style>
    <main class='page'>
        <div class='wrap'>
            <div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='otaTipHome'>🏠 Home</a></div>
            <h1 data-i18n='sensorPageTitle'>🌡️ Ambient Sensor</h1>
        
            <!-- Section 1: Main Controls -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='sensorMainTitle'>⚙️ Main Control</h3>
                <div style='display:flex;gap:15px;align-items:center;flex-wrap:wrap;margin-bottom:15px'>
                    <label class='checkbox-label' style='margin:0'>
                        <input type='checkbox' id='sensorEnabled' checked title='Enable this sensor' data-i18n-title='sensorTipEnable'>
                        <span data-i18n='sensorEnable'>Enable Sensor</span>
                    </label>
                    <div style='display:flex;align-items:center;gap:8px;flex:1;justify-content:flex-end'>
                        <label style='font-size:13px;color:#94a3b8;white-space:nowrap;text-align:right' data-i18n='sensorInterval'>⏱️ Auto Read Interval:</label>
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
                <div style='text-align:center;margin-bottom:15px'>
                    <button id='btnMeasure' class='btn' style='padding:6px 12px;background:#10b981;border-color:#10b981;font-size:13px' data-i18n='sensorMeasureNow' title='Take immediate reading' data-i18n-title='sensorTipMeasure'>🔬 Measure Now</button>
                </div>
                <div id='lastReadingTime' style='padding:10px;font-size:13px;color:#e0e7ff;text-align:center;background:#1e3a8a;border-radius:6px;border:1px solid #3b82f6' data-i18n='sensorNoReading'>No reading available</div>
                <div id='historyBufferInfo' style='padding:8px;font-size:12px;color:#cbd5e1;text-align:center;background:#1e293b;border-radius:6px;border:1px solid #475569;margin-top:8px' data-i18n='sensorBuffer'>📊 Buffer: -- points</div>
            </div>

            <!-- Section 2: Sensor Readings -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='sensorReadingsTitle'>📊 Sensor Readings</h3>
                <div class='sensor-grid'>
                    <div class='sensor-grid-header' data-i18n='sensorGridReading'>Reading</div>
                    <div class='sensor-grid-header' data-i18n='sensorGridActive'>Active</div>
                    <div class='sensor-grid-header' data-i18n='sensorGridLast'>Last</div>
                    <div class='sensor-grid-header' data-i18n='sensorGridOffset'>Offset</div>

                    <div class='sensor-reading-name' data-i18n='sensorTempAht20'>🌡️ Temperature AHT20</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_temp_aht20' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefComfort1826'>Comfort: 18-26 °C</div><div class='sensor-value' id='val_temp_aht20'>--</div></div>
                    <div><input type='number' id='offset_temp_aht20' class='offset-input' step='0.1' min='-5' max='5' value='0' title='Calibration offset' data-i18n-title='sensorTipOffset'></div>

                    <div class='sensor-reading-name' data-i18n='sensorHumidity'>💧 Humidity</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_humidity' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefComfort4060'>Comfort: 40-60 %</div><div class='sensor-value' id='val_humidity'>--</div></div>
                    <div><input type='number' id='offset_humidity' class='offset-input' step='0.1' min='-10' max='10' value='0' title='Calibration offset' data-i18n-title='sensorTipOffset'></div>

                    <div class='sensor-reading-name' data-i18n='sensorTempBmp280'>🌡️ Temperature BMP280</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_temp_bmp280' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefComfort1826'>Comfort: 18-26 °C</div><div class='sensor-value' id='val_temp_bmp280'>--</div></div>
                    <div><input type='number' id='offset_temp_bmp280' class='offset-input' step='0.1' min='-5' max='5' value='0' title='Calibration offset' data-i18n-title='sensorTipOffset'></div>

                    <div class='sensor-reading-name' data-i18n='sensorPressureHpa'>🌐 Pressure (hPa)</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_pressure_hpa' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefNormal9801030'>Normal: 980-1030 hPa</div><div class='sensor-value' id='val_pressure_hpa'>--</div></div>
                    <div><input type='number' id='offset_pressure_hpa' class='offset-input' step='0.1' min='-50' max='50' value='0' title='Calibration offset' data-i18n-title='sensorTipOffset'></div>

                    <div class='sensor-reading-name' data-i18n='sensorPressureMmhg'>🌐 Pressure (mmHg)</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_pressure_mmhg' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefNormal735772'>Normal: 735-772 mmHg</div><div class='sensor-value' id='val_pressure_mmhg'>--</div></div>
                    <div><input type='number' id='offset_pressure_mmhg' class='offset-input' step='0.1' min='-50' max='50' value='0' title='Calibration offset' data-i18n-title='sensorTipOffset'></div>

                    <div class='sensor-reading-name' data-i18n='sensorTempAvg'>📈 Avg Temp</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_temp_avg' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefComfort1826'>Comfort: 18-26 °C</div><div class='sensor-value' id='val_temp_avg'>--</div></div>
                    <div><input type='number' id='offset_temp_avg' class='offset-input' step='0.1' min='-5' max='5' value='0' title='Calibration offset' data-i18n-title='sensorTipOffset'></div>

                    <div class='sensor-reading-name' data-i18n='sensorDewPoint'>💧 Dew Point</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_dew_point' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefComfort8'>Comfort: 8-16 °C</div><div class='sensor-value' id='val_dew_point'>--</div></div>
                    <div style='text-align:center;color:#94a3b8;font-size:11px'>—</div>

                    <div class='sensor-reading-name' data-i18n='sensorHeatIndex'>🔥 Heat Index</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_heat_index' checked title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefComfort1827'>Comfort: 18-27 °C</div><div class='sensor-value' id='val_heat_index'>--</div></div>
                    <div style='text-align:center;color:#94a3b8;font-size:11px'>—</div>

                    <div class='sensor-reading-name' data-i18n='sensorAltitude'>⛰️ Altitude</div>
                    <div style='text-align:center'><input type='checkbox' id='enable_altitude' title='Toggle this reading' data-i18n-title='sensorTipToggle'></div>
                    <div class='sensor-reading-cell'><div class='sensor-reference' data-i18n='sensorRefAltitude'>Reference: depends on location</div><div class='sensor-value' id='val_altitude'>--</div></div>
                    <div><input type='number' id='offset_altitude' class='offset-input' step='1' min='-100' max='100' value='0' title='Calibration offset' data-i18n-title='sensorTipOffset'></div>

                    <div class='sensor-reading-name' data-i18n='sensorSeaLevelRef'>🌊 Sea Level Pressure (altitude ref.)</div>
                    <div style='text-align:center;color:#94a3b8;font-size:11px'>—</div>
                    <div style='display:flex;align-items:center;justify-content:center;gap:5px'>
                        <input type='number' id='seaLevelPressure' class='offset-input' style='max-width:90px;text-align:center' step='0.1' value='1013.25' placeholder='1013.25' title='Sea level pressure (hPa)' data-i18n-title='sensorTipSeaLevel'>
                        <span style='color:#94a3b8;font-size:12px'>hPa</span>
                    </div>
                    <div style='text-align:center;color:#94a3b8;font-size:11px'>—</div>
                </div>
            </div>

            <!-- Section 3: Output Options -->
            <div class='section-box'>
                <h3 class='section-title' style='font-size:18px' data-i18n='sensorOutputTitle'>📤 Output Options</h3>
                
                <!-- Line 1: Destination + Format -->
                <div style='display:flex;gap:20px;margin-bottom:12px;flex-wrap:wrap;align-items:center'>
                    <label class='checkbox-label' style='background:#1e3a8a;padding:8px 12px;border-radius:6px'>
                        <input type='checkbox' id='injectBuffer' title='Send data to monitor' data-i18n-title='sensorTipMonitor'>
                        <span style='font-weight:bold' data-i18n='sensorInjectBuffer'>📊 Monitor Data</span>
                    </label>
                    
                    <div style='color:#94a3b8;font-size:13px;margin:0 5px'>→ Formato:</div>
                    
                    <label class='checkbox-label'>
                        <input type='checkbox' id='outputPretty' checked title='Use formatted output' data-i18n-title='sensorTipFormatted'>
                        <span data-i18n='sensorDataFormatted'>Formatted Data</span>
                    </label>
                    
                    <label class='checkbox-label'>
                        <input type='checkbox' id='outputCSV' title='Use CSV output' data-i18n-title='sensorTipCsv'>
                        <span data-i18n='sensorDataImport'>Data for Import</span>
                    </label>
                </div>

                <!-- Line 2: Inclusion options -->
                <!-- Removed "Include timestamp" and "Include headers" options per request -->
                <!-- Separator (hidden by default, used internally) -->
                <input type='hidden' id='csvSeparator' value=','>
            </div>
                </div>
            </div>

            <!-- Section 4: Statistics -->
            <div class='section-box'>
                <h3 class='section-title' data-i18n='sensorStatsTitle'>📊 24h Statistics</h3>
                <div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-top:15px'>
                    <div style='padding:12px;background:#1e293b;border-radius:6px;border:1px solid #334155;text-align:center'>
                        <div style='font-size:11px;color:#94a3b8;margin-bottom:6px' data-i18n='sensorStatTemp'>🌡️ Temperature</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatMin'>Min:</span> <span id='stats_temp_min'>--</span>°C</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatMax'>Max:</span> <span id='stats_temp_max'>--</span>°C</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatAvg'>Avg:</span> <span id='stats_temp_avg'>--</span>°C</div>
                    </div>
                    <div style='padding:12px;background:#1e293b;border-radius:6px;border:1px solid #334155;text-align:center'>
                        <div style='font-size:11px;color:#94a3b8;margin-bottom:6px' data-i18n='sensorStatHumidity'>💧 Humidity</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatMin'>Min:</span> <span id='stats_hum_min'>--</span>%</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatMax'>Max:</span> <span id='stats_hum_max'>--</span>%</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatAvg'>Avg:</span> <span id='stats_hum_avg'>--</span>%</div>
                    </div>
                    <div style='padding:12px;background:#1e293b;border-radius:6px;border:1px solid #334155;text-align:center'>
                        <div style='font-size:11px;color:#94a3b8;margin-bottom:6px' data-i18n='sensorStatPressure'>🌐 Pressure</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatMin'>Min:</span> <span id='stats_press_min'>--</span>hPa</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatMax'>Max:</span> <span id='stats_press_max'>--</span>hPa</div>
                        <div style='font-size:13px'><span data-i18n='sensorStatAvg'>Avg:</span> <span id='stats_press_avg'>--</span>hPa</div>
                    </div>
                </div>
                <div style='margin-top:12px;text-align:center;color:#94a3b8;font-size:12px'>
                    <span data-i18n='sensorStatSamples'>Samples:</span> <span id='stats_count'>0</span> | <span data-i18n='sensorStatWindow'>Window:</span> <span id='stats_window'>0h 0m</span>
                </div>
                <div style='text-align:center'>
                    <button id='btnResetStats' class='btn' style='margin-top:12px;background:#dc2626;border-color:#dc2626' data-i18n='sensorResetStats' title='Reset statistics' data-i18n-title='sensorTipResetStats'>🔄 Reset Statistics</button>
                </div>
            </div>

            <!-- Save Button -->
            <div style='text-align:center'>
                <button id='btnSave' class='btn' style='padding:10px 24px;margin-top:20px' data-i18n='sensorSaveConfig' title='Save sensor settings' data-i18n-title='sensorTipSave'>💾 Save Configuration</button>
            </div>
            
            <div id='status' style='margin-top:15px;padding:12px;border-radius:6px;display:none'></div>
        </div>
    </main>

    <script>
        // Track previous reading values to render trend arrows
        let prevValues = {};

        function t(k, fb) {
            try {
                if (typeof window._t === 'function') return window._t(k, fb);
            } catch (e) {}
            return fb !== undefined ? fb : k;
        }

        function uiLocale() {
            try {
                const code = (document.documentElement.lang || localStorage.getItem('ui_lang_code') || 'en').toLowerCase();
                return code.startsWith('en') ? 'en-GB' : 'pt-PT';
            } catch (e) {
                return 'en-GB';
            }
        }

        // Returns a colored HTML arrow based on change since last update.
        // delta = minimum change to show up/down (avoids noise arrows).
        function trendArrow(id, curr, delta) {
            const prev = prevValues[id];
            if (prev === undefined || isNaN(curr)) return '';
            const diff = curr - prev;
            if (Math.abs(diff) < delta) return ' <span style="color:#94a3b8;font-size:10px">→</span>';
            return diff > 0
                ? ' <span style="color:#4ade80;font-size:10px">↑</span>'
                : ' <span style="color:#f87171;font-size:10px">↓</span>';
        }

        // Load configuration on page load
        async function loadConfig() {
            try {
                const res = await fetch('/api/sensor-config', {cache:'no-store'});
                if (!res.ok) throw new Error('Failed to load config');
                
                const cfg = await res.json();
                console.log('[SENSOR] Config loaded:', cfg);
                
                // Global settings
                document.getElementById('sensorEnabled').checked = cfg.sensor_enabled;
                document.getElementById('readInterval').value = cfg.read_interval;
                document.getElementById('outputPretty').checked = cfg.output_pretty;
                document.getElementById('outputCSV').checked = cfg.output_csv;
                document.getElementById('injectBuffer').checked = cfg.inject_to_buffer;
                document.getElementById('csvSeparator').value = cfg.csv_separator;
                // Options valid, but UI elements removed:
                // csv_include_timestamp -> ignored/false
                // csv_include_headers -> implied true (vertical format)
                document.getElementById('seaLevelPressure').value = cfg.sea_level_pressure_hpa;
                
                // Individual readings
                const readings = cfg.readings;
                for (const [key, val] of Object.entries(readings)) {
                    const enableEl = document.getElementById('enable_' + key);
                    const offsetEl = document.getElementById('offset_' + key);
                    
                    if (enableEl) enableEl.checked = val.enabled;
                    if (offsetEl) offsetEl.value = val.offset;
                }
                
                // Last reading values
                updateReadingValues(cfg);
                
                // Statistics
                if (cfg.stats) {
                    updateStatistics(cfg.stats);
                }
                
                // History buffer info
                if (cfg.history_buffer) {
                    const bufferEl = document.getElementById('historyBufferInfo');
                    if (bufferEl) {
                        const pct = Math.round((cfg.history_buffer.count / cfg.history_buffer.capacity) * 100);
                        bufferEl.textContent = t('sensorBufferPrefix', 'Buffer: ') + cfg.history_buffer.count + '/' + cfg.history_buffer.capacity + ' ' + t('sensorBufferPoints', 'points') + ' (' + pct + '%)';
                        if (cfg.history_buffer.count === 0) {
                            bufferEl.style.background = '#7f1d1d';
                            bufferEl.style.borderColor = '#991b1b';
                            bufferEl.style.color = '#fecaca';
                        } else if (cfg.history_buffer.count < 12) {
                            bufferEl.style.background = '#92400e';
                            bufferEl.style.borderColor = '#b45309';
                            bufferEl.style.color = '#fed7aa';
                        } else {
                            bufferEl.style.background = '#065f46';
                            bufferEl.style.borderColor = '#059669';
                            bufferEl.style.color = '#d1fae5';
                        }
                    }
                }
                
                showStatus(t('sensorConfigLoaded', 'Configuration loaded'), 'success');
                
                // If no readings yet, trigger automatic measurement
                if (!cfg.last_reading_time || cfg.last_reading_time === 0) {
                    console.log('[SENSOR] No initial data, triggering measurement...');
                    setTimeout(() => measureNow(), 500);
                }
            } catch (e) {
                showStatus(t('sensorLoadErrorPrefix', 'Error loading: ') + e.message, 'error');
            }
        }
        
        function updateReadingValues(cfg) {
            // Check if sensor has been read at least once
            const hasData = cfg.last_reading_time && cfg.last_reading_time > 0;
            
            // Update last reading timestamp display
            const timeEl = document.getElementById('lastReadingTime');
            if (!hasData) {
                timeEl.textContent = t('sensorNoReading', 'No reading available');
                timeEl.style.background = '#1e3a8a';
                timeEl.style.color = '#94a3b8';
                // No data yet - show placeholders
                ['val_temp_aht20', 'val_humidity', 'val_temp_bmp280', 'val_pressure_hpa', 
                 'val_pressure_mmhg', 'val_temp_avg', 'val_dew_point', 'val_heat_index', 'val_altitude'].forEach(id => {
                    document.getElementById(id).textContent = '--';
                });
                console.log('[SENSOR] No readings yet (last_reading_time: ' + cfg.last_reading_time + ')');
                return;
            }
            
            // Format timestamp (Unix timestamp in seconds)
            const timestamp = cfg.last_reading_time * 1000; // Convert to milliseconds
            const date = new Date(timestamp);
            const locale = uiLocale();
            const timeStr = date.toLocaleTimeString(locale);
            const dateStr = date.toLocaleDateString(locale);
            timeEl.textContent = t('sensorLastReadingPrefix', 'Last reading: ') + timeStr + ' (' + dateStr + ')';
            timeEl.style.background = '#065f46';
            timeEl.style.color = '#d1fae5';

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

            function envTone(kind, value) {
                if (isNaN(value)) return { bg: '#064e3b', border: '#065f46' };
                if (kind === 'temp_aht20' || kind === 'temp_bmp280' || kind === 'temp_avg') {
                    if (value >= 18 && value <= 26) return { bg: '#10b981', border: '#059669' };
                    if ((value >= 16 && value < 18) || (value > 26 && value <= 29)) return { bg: '#84cc16', border: '#65a30d' };
                    if ((value >= 12 && value < 16) || (value > 29 && value <= 32)) return { bg: '#f59e0b', border: '#d97706' };
                    if ((value >= 5 && value < 12) || (value > 32 && value <= 38)) return { bg: '#f97316', border: '#ea580c' };
                    return { bg: '#ef4444', border: '#dc2626' };
                }
                if (kind === 'humidity') {
                    if (value >= 40 && value <= 60) return { bg: '#10b981', border: '#059669' };
                    if ((value >= 30 && value < 40) || (value > 60 && value <= 70)) return { bg: '#84cc16', border: '#65a30d' };
                    if ((value >= 20 && value < 30) || (value > 70 && value <= 80)) return { bg: '#f59e0b', border: '#d97706' };
                    return { bg: '#ef4444', border: '#dc2626' };
                }
                if (kind === 'pressure_hpa') {
                    if (value >= 980 && value <= 1030) return { bg: '#10b981', border: '#059669' };
                    if ((value >= 965 && value < 980) || (value > 1030 && value <= 1045)) return { bg: '#84cc16', border: '#65a30d' };
                    if ((value >= 950 && value < 965) || (value > 1045 && value <= 1060)) return { bg: '#f59e0b', border: '#d97706' };
                    return { bg: '#ef4444', border: '#dc2626' };
                }
                if (kind === 'pressure_mmhg') {
                    if (value >= 735 && value <= 772) return { bg: '#10b981', border: '#059669' };
                    if ((value >= 724 && value < 735) || (value > 772 && value <= 783)) return { bg: '#84cc16', border: '#65a30d' };
                    if ((value >= 712 && value < 724) || (value > 783 && value <= 795)) return { bg: '#f59e0b', border: '#d97706' };
                    return { bg: '#ef4444', border: '#dc2626' };
                }
                if (kind === 'dew_point') {
                    if (value >= 8 && value <= 16) return { bg: '#10b981', border: '#059669' };
                    if ((value >= 5 && value < 8) || (value > 16 && value <= 20)) return { bg: '#84cc16', border: '#65a30d' };
                    if ((value >= 0 && value < 5) || (value > 20 && value <= 24)) return { bg: '#f59e0b', border: '#d97706' };
                    return { bg: '#ef4444', border: '#dc2626' };
                }
                if (kind === 'heat_index') {
                    if (value >= 18 && value <= 27) return { bg: '#10b981', border: '#059669' };
                    if ((value >= 16 && value < 18) || (value > 27 && value <= 30)) return { bg: '#84cc16', border: '#65a30d' };
                    if ((value >= 12 && value < 16) || (value > 30 && value <= 34)) return { bg: '#f59e0b', border: '#d97706' };
                    if (value > 34) return { bg: '#ef4444', border: '#dc2626' };
                    return { bg: '#60a5fa', border: '#2563eb' };
                }
                if (kind === 'altitude') {
                    return { bg: '#1e40af', border: '#2563eb' };
                }
                return { bg: '#1e293b', border: '#475569' };
            }
            
            const safe = (val) => (typeof val === 'number' && !isNaN(val)) ? val : 0;
            // Helper: set innerHTML of id to formatted value + trend arrow, then store previous.
            const sv = (id, val, decimals, unit, delta) => {
                const v = safe(val);
                const el = document.getElementById(id);
                if (el) el.innerHTML = v.toFixed(decimals) + ' ' + unit + trendArrow(id, v, delta);
                applySensorValueTheme(id, envTone(id.replace('val_', ''), v));
                prevValues[id] = v;
            };
            sv('val_temp_aht20',    cfg.last_temp_aht20,    2, '°C',   0.1);
            sv('val_humidity',      cfg.last_humidity,       2, '%',    0.5);
            sv('val_temp_bmp280',   cfg.last_temp_bmp280,   2, '°C',   0.1);
            sv('val_pressure_hpa',  cfg.last_pressure_hpa,  2, 'hPa',  0.1);
            sv('val_pressure_mmhg', cfg.last_pressure_mmhg, 2, 'mmHg', 0.1);
            sv('val_temp_avg',      cfg.last_temp_avg,       2, '°C',   0.1);
            sv('val_dew_point',     cfg.last_dew_point,      2, '°C',   0.1);
            sv('val_heat_index',    cfg.last_heat_index,     2, '°C',   0.1);
            sv('val_altitude',      cfg.last_altitude,       1, 'm',    0.5);
            
            console.log('[SENSOR] Updated readings at ' + timeStr);
        }
        
        function updateStatistics(stats) {
            // Update window time
            const hours = stats.window_hours || 0;
            const minutes = stats.window_minutes || 0;
            document.getElementById('stats_window').textContent = hours + 'h ' + minutes + 'm';
            
            // Temperature stats
            if (stats.temperature && stats.temperature.count > 0) {
                document.getElementById('stats_temp_min').textContent = stats.temperature.min.toFixed(2);
                document.getElementById('stats_temp_max').textContent = stats.temperature.max.toFixed(2);
                document.getElementById('stats_temp_avg').textContent = stats.temperature.avg.toFixed(2);
                document.getElementById('stats_count').textContent = stats.temperature.count;
            } else {
                document.getElementById('stats_temp_min').textContent = '--';
                document.getElementById('stats_temp_max').textContent = '--';
                document.getElementById('stats_temp_avg').textContent = '--';
            }
            
            // Humidity stats
            if (stats.humidity && stats.humidity.count > 0) {
                document.getElementById('stats_hum_min').textContent = stats.humidity.min.toFixed(2);
                document.getElementById('stats_hum_max').textContent = stats.humidity.max.toFixed(2);
                document.getElementById('stats_hum_avg').textContent = stats.humidity.avg.toFixed(2);
            } else {
                document.getElementById('stats_hum_min').textContent = '--';
                document.getElementById('stats_hum_max').textContent = '--';
                document.getElementById('stats_hum_avg').textContent = '--';
            }
            
            // Pressure stats
            if (stats.pressure && stats.pressure.count > 0) {
                document.getElementById('stats_press_min').textContent = stats.pressure.min.toFixed(2);
                document.getElementById('stats_press_max').textContent = stats.pressure.max.toFixed(2);
                document.getElementById('stats_press_avg').textContent = stats.pressure.avg.toFixed(2);
            } else {
                document.getElementById('stats_press_min').textContent = '--';
                document.getElementById('stats_press_max').textContent = '--';
                document.getElementById('stats_press_avg').textContent = '--';
            }
        }
        
        // Save configuration
        async function saveConfig() {
            disableBtn(btnSave);showLoading();
            try {
                const cfg = {
                    sensor_enabled: document.getElementById('sensorEnabled').checked,
                    read_interval: parseInt(document.getElementById('readInterval').value),
                    output_pretty: document.getElementById('outputPretty').checked,
                    output_csv: document.getElementById('outputCSV').checked,
                    inject_to_buffer: document.getElementById('injectBuffer').checked,
                    csv_separator: document.getElementById('csvSeparator').value,
                    // UI options removed, sending defaults:
                    csv_include_timestamp: false, 
                    csv_include_headers: true, // Always include for line-by-line keys
                    sea_level_pressure_hpa: parseFloat(document.getElementById('seaLevelPressure').value),
                    readings: {}
                };
                
                // Collect readings config
                const readings = ['temp_aht20', 'humidity', 'temp_bmp280', 'pressure_hpa', 'pressure_mmhg', 
                                'temp_avg', 'dew_point', 'heat_index', 'altitude'];
                
                for (const key of readings) {
                    const enableEl = document.getElementById('enable_' + key);
                    const offsetEl = document.getElementById('offset_' + key);
                    
                    cfg.readings[key] = {
                        enabled: enableEl ? enableEl.checked : false,
                        offset: offsetEl ? parseFloat(offsetEl.value) || 0 : 0
                    };
                }
                
                try {
                    const res = await fetch('/api/sensor-config', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(cfg)
                    });
                    
                    if (!res.ok) throw new Error('Failed to save');
                    
                    // Use standard save notification pattern
                    if (typeof customAlert === 'function') {
                        customAlert(_t('sensorAlertSaved','✅ Configuration saved!'));
                    }
                    if (typeof showSavedCard === 'function') {
                        showSavedCard();
                    }
                } catch (e) {
                    customAlert(_t('sensorAlertError','❌ Error: ')+e.message);
                }
            } finally { hideLoading();enableBtn(btnSave); }
        }
        
        // Force immediate reading
        async function measureNow() {
            showStatus(_t('sensorMeasuring','🔬 Measuring...'), 'info');
            try {
                const res = await fetch('/api/sensor-measure', { method: 'POST' });
                if (!res.ok) throw new Error('Failed to measure');
                
                const cfg = await res.json();
                updateReadingValues(cfg);
                showStatus(_t('sensorMeasureDone','✅ Reading done!'), 'success');
            } catch (e) {
                showStatus(_t('sensorMeasureErr','❌ Reading error: ')+e.message, 'error');
            }
        }
        
        function showStatus(msg, type) {
            // Fallback for legacy status display (now using customAlert + showSavedCard)
            if (typeof customAlert === 'function') {
                customAlert(msg);
            } else {
                const el = document.getElementById('status');
                if (el) {
                    el.textContent = msg;
                    el.style.display = 'block';
                    el.style.border = type === 'error' ? '2px solid #ef4444' : 
                                    type === 'success' ? '2px solid #10b981' : '2px solid #3b82f6';
                    el.style.background = type === 'error' ? '#7f1d1d' : 
                                        type === 'success' ? '#064e3b' : '#1e3a8a';
                    el.style.color = '#e6f1ff';
                    setTimeout(() => el.style.display = 'none', 3000);
                }
            }
        }
        
        // Event listeners
        document.getElementById('btnSave').addEventListener('click', saveConfig);
        document.getElementById('btnMeasure').addEventListener('click', measureNow);
        document.getElementById('btnResetStats').addEventListener('click', async () => {
            const runReset = async () => {
                try {
                    const res = await fetch('/api/sensor-reset-stats', { method: 'POST' });
                    if (!res.ok) throw new Error('Failed to reset');
                    // Clear UI
                    ['stats_temp_min', 'stats_temp_max', 'stats_temp_avg',
                     'stats_hum_min', 'stats_hum_max', 'stats_hum_avg',
                     'stats_press_min', 'stats_press_max', 'stats_press_avg'].forEach(id => {
                        document.getElementById(id).textContent = '--';
                    });
                    document.getElementById('stats_count').textContent = '0';
                    document.getElementById('stats_window').textContent = '0h 0m';
                    showStatus(_t('sensorStatsReset','✅ Statistics reset!'), 'success');
                } catch (e) {
                    showStatus(_t('sensorStatsResetErr','❌ Reset error: ')+e.message, 'error');
                }
            };
            var msg = _t('sensorConfirmReset','Reset 24h statistics? All data will be lost.');
            if (typeof customConfirm === 'function') {
                customConfirm(msg, function(ok) { if (ok) runReset(); });
                return;
            }
        });
        
        // Mutual exclusion: formatted output vs importable output.
        document.getElementById('outputPretty').addEventListener('change', function() {
            if (this.checked) {
                document.getElementById('outputCSV').checked = false;
            }
        });
        
        document.getElementById('outputCSV').addEventListener('change', function() {
            if (this.checked) {
                document.getElementById('outputPretty').checked = false;
            }
        });
        
        // Ensure that at least one format stays active when "Dados no Monitor" is enabled.
        document.getElementById('injectBuffer').addEventListener('change', function() {
            if (this.checked) {
                // If no format is selected, enable the formatted output by default.
                if (!document.getElementById('outputPretty').checked && !document.getElementById('outputCSV').checked) {
                    document.getElementById('outputPretty').checked = true;
                }
            }
        });
        
        // Small delay so /api/lang fetch starts before config fetch to avoid competing on the device.
        setTimeout(loadConfig, 150);
    </script>
)rawliteral";

#endif // ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
#endif // PAGE_SENSOR_CONFIG_ASSET_H

