#pragma once

#include <Arduino.h>

// Page body fragment served at /api-reference.
// Wrapped with standard shell by streamApiReferencePage() in the renderer.
static const char API_REFERENCE_PAGE[] PROGMEM = R"rawliteral(
<style>
details{background:#0f172a;border:2px solid #1e3a8a;border-radius:8px;margin:8px 0;overflow:hidden}
summary{cursor:pointer;padding:12px 16px;font-weight:700;color:#93c5fd;font-size:15px;list-style:none;display:flex;align-items:center;gap:8px;user-select:none}
summary::-webkit-details-marker{display:none}
summary::before{content:'▶';font-size:11px;transition:transform 0.2s;color:#60a5fa;flex-shrink:0}
details[open] summary::before{transform:rotate(90deg)}
table{width:100%;border-collapse:collapse}
td,th{padding:8px 10px;border-bottom:1px solid #1e293b;vertical-align:top;font-size:13px}
th{background:#020617;color:#94a3b8;font-weight:600;text-align:left}
tr:last-child td{border-bottom:none}
.m{display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:700;min-width:50px;text-align:center}
.g{background:#166534;color:#bbf7d0}
.p{background:#1e3a8a;color:#bfdbfe}
.d{background:#7f1d1d;color:#fecaca}
.r{font-family:monospace;color:#67e8f9;font-size:12px}
a.r{color:#67e8f9;text-decoration:none;border-bottom:1px dashed #67e8f9}
a.r:hover{color:#a5f3fc;border-bottom-color:#a5f3fc}
.ds{color:#94a3b8}
.ip{color:#60a5fa;font-weight:600}
.cnt{background:#1e293b;color:#60a5fa;font-size:11px;padding:2px 7px;border-radius:10px;margin-left:8px}
.try-btn{background:transparent;color:#60a5fa;border:1px solid #1e40af;padding:1px 6px;border-radius:4px;font-size:11px;cursor:pointer;margin-left:8px;vertical-align:middle;line-height:1.4}
.try-btn:hover{background:#1e3a8a;color:#bfdbfe}
.api-pre{background:#020617;color:#67e8f9;font-size:11px;white-space:pre;overflow-x:auto;margin:0;padding:8px 10px;border-radius:4px;border:1px solid #1e293b;max-height:300px;overflow-y:auto;text-align:left}
</style>
<div class='page api-reference'><div class='wrap'>
<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome'>&#127968; Home</a></div>
<h1 data-i18n='apiRefTitle'>&#128268; API Reference</h1>

<details>
<summary data-i18n='apiRefSecStatus'>&#128421;&#65039; Status &amp; Monitor <span class="cnt">11</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/status</td><td class="ds" data-i18n='apiRefDescAStatus'>System state (IP, heap, version, uptime)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/nvs-stats</td><td class="ds" data-i18n='apiRefDescANvsStats'>NVS storage usage</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/monitor-status</td><td class="ds" data-i18n='apiRefDescAMonitorStatus'>Monitor session state</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/received-text</td><td class="ds" data-i18n='apiRefDescAReceivedText'>Received text buffer (RS232/LPT)</td></tr>
<tr data-feat="sniffer"><td><span class="m g">GET</span></td><td class="r">/api/serial-monitor</td><td class="ds" data-i18n='apiRefDescASerialMonitor'>Serial sniffer buffer as JSON</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/monitor-open</td><td class="ds" data-i18n='apiRefDescAMonitorOpen'>Opens a monitor session</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/monitor-close</td><td class="ds" data-i18n='apiRefDescAMonitorClose'>Closes the monitor session</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/clear-buffer</td><td class="ds" data-i18n='apiRefDescAClearBuffer'>Clears the reception buffer</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sensor-flags</td><td class="ds" data-i18n='apiRefDescASensorFlags'>Sensor enable flags</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/self-test</td><td class="ds" data-i18n='apiRefDescASelfTest'>Full device audit (features, pages, config health, modules)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/test-manifest</td><td class="ds" data-i18n='apiRefDescATestManifest'>Test contract for the AI agent (device, capabilities, pages, api)</td></tr>
</table></div>
</details>

<details>
<summary data-i18n='apiRefSecConfig'>&#9881;&#65039; Config Global <span class="cnt">4</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/config</td><td class="ds" data-i18n='apiRefDescAConfig'>Reads config per section (required: ?section=wifi|email|rs232|system|recipients|scheduler)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/config</td><td class="ds" data-i18n='apiRefDescAConfig02'>Saves config per section (section is required in the payload)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/configs</td><td class="ds" data-i18n='apiRefDescAConfigs'>List of config snapshots (backup/restore)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/lang</td><td class="ds" data-i18n='apiRefDescALang'>Active language JSON for i18n</td></tr>
</table></div>
</details>

<details data-feat="rs232">
<summary data-i18n='apiRefSecRs232'>&#128225; RS232 <span class="cnt">11</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/rs232-templates</td><td class="ds" data-i18n='apiRefDescARs232Templates'>Lists RS232 templates</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/rs232-templates</td><td class="ds" data-i18n='apiRefDescARs232Templates02'>Creates/updates an RS232 template</td></tr>
<tr><td><span class="m d">DEL</span></td><td class="r">/api/rs232-templates</td><td class="ds" data-i18n='apiRefDescARs232Templates03'>Removes a template by name</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/rs232-apply</td><td class="ds" data-i18n='apiRefDescARs232Apply'>Applies a template as active config</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/rs232-status</td><td class="ds" data-i18n='apiRefDescARs232Status'>RS232 port state (baud, parity, etc.)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/test-rs232</td><td class="ds" data-i18n='apiRefDescATestRs232'>Sends a test string via RS232</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/rs232/persist-current-template</td><td class="ds" data-i18n='apiRefDescARs232PersistCurrentTemplate'>Persists the current RS232 config as a template</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/rs232-command</td><td class="ds" data-i18n='apiRefDescARs232Command'>Sends an RS232 command</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/rs232-cols</td><td class="ds" data-i18n='apiRefDescARs232Cols'>RS232 chart column settings</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/rs232-history-files</td><td class="ds" data-i18n='apiRefDescARs232HistoryFiles'>Lists RS232 history files on SD</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/rs232-history</td><td class="ds" data-i18n='apiRefDescARs232History'>RS232 history data (paginated)</td></tr>
</table></div>
</details>

<details>
<summary data-i18n='apiRefSecNetwork'>&#127760; Rede / WiFi / Ethernet <span class="cnt">20</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/wifi-scan</td><td class="ds" data-i18n='apiRefDescAWifiScan'>Scans for available WiFi networks</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/wifi-status</td><td class="ds" data-i18n='apiRefDescAWifiStatus'>Current WiFi link state</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/wifi-intelligent-status</td><td class="ds" data-i18n='apiRefDescAWifiIntelligentStatus'>Hybrid manager state (STA+AP)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/wifi-runtime-status</td><td class="ds" data-i18n='apiRefDescAWifiRuntimeStatus'>WiFi runtime state (disabled, connected, IP, SSID)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/config/saved_networks</td><td class="ds" data-i18n='apiRefDescAConfigSavedNetworks'>Lists saved WiFi profiles</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/config/saved_networks</td><td class="ds" data-i18n='apiRefDescAConfigSavedNetworks02'>Adds/updates a saved WiFi profile</td></tr>
<tr><td><span class="m d">DEL</span></td><td class="r">/api/config/saved_networks</td><td class="ds" data-i18n='apiRefDescAConfigSavedNetworks03'>Removes a WiFi profile</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/config/access-point</td><td class="ds" data-i18n='apiRefDescAConfigAccessPoint'>AP mode configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/config/access-point</td><td class="ds" data-i18n='apiRefDescAConfigAccessPoint02'>Saves the AP mode configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/wifi-reconnect</td><td class="ds" data-i18n='apiRefDescAWifiReconnect'>Forces a WiFi reconnect</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/wifi-switch-network</td><td class="ds" data-i18n='apiRefDescAWifiSwitchNetwork'>Switches to a specific saved network</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/wifi-disable</td><td class="ds" data-i18n='apiRefDescAWifiDisable'>Disables WiFi at runtime and persists the disable flag</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/wifi-enable</td><td class="ds" data-i18n='apiRefDescAWifiEnable'>Re-enables the WiFi flag (reboot needed to apply the driver)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/wifi-force-ap</td><td class="ds" data-i18n='apiRefDescAWifiForceAp'>Forces AP mode</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/wifi-disable-ap</td><td class="ds" data-i18n='apiRefDescAWifiDisableAp'>Disables AP mode</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/clear-ap-lock</td><td class="ds" data-i18n='apiRefDescAClearApLock'>Clears the AP lock (re-enables STA attempts)</td></tr>
<tr data-feat="telnet"><td><span class="m g">GET</span></td><td class="r">/api/telnet-status</td><td class="ds" data-i18n='apiRefDescATelnetStatus'>Telnet server state</td></tr>
<tr data-feat="telnet"><td><span class="m p">POST</span></td><td class="r">/api/telnet-toggle</td><td class="ds" data-i18n='apiRefDescATelnetToggle'>Enables/disables Telnet</td></tr>
<tr data-feat="ethernet"><td><span class="m g">GET</span></td><td class="r">/api/eth-status</td><td class="ds" data-i18n='apiRefDescAEthStatus'>Ethernet link state (W5500)</td></tr>
<tr data-feat="ethernet"><td><span class="m p">POST</span></td><td class="r">/api/eth-config</td><td class="ds" data-i18n='apiRefDescAEthConfig'>Saves Ethernet config (static IP, gateway)</td></tr>
</table></div>
</details>

<details>
<summary data-i18n='apiRefSecEmail'>&#128231; Email <span class="cnt">12</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/email-test</td><td class="ds" data-i18n='apiRefDescAEmailTest'>Sends a test email</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/recipients</td><td class="ds" data-i18n='apiRefDescARecipients'>Lists all email recipients</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/recipient-slots</td><td class="ds" data-i18n='apiRefDescARecipientSlots'>Recipient slot capacity</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/recipient-slot</td><td class="ds" data-i18n='apiRefDescARecipientSlot'>Gets a recipient slot by index</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/recipient-slot</td><td class="ds" data-i18n='apiRefDescARecipientSlot02'>Saves a recipient slot</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/inactivity-alert-config</td><td class="ds" data-i18n='apiRefDescAInactivityAlertConfig'>Inactivity alert configuration</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/emailday-extra-emails</td><td class="ds" data-i18n='apiRefDescAEmaildayExtraEmails'>Extra recipients for the SD daily report</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/emailday-extra-emails</td><td class="ds" data-i18n='apiRefDescAEmaildayExtraEmails02'>Saves extra recipients for the SD daily report</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sensorday-extra-emails</td><td class="ds" data-i18n='apiRefDescASensordayExtraEmails'>Extra recipients for the sensors daily report</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sensorday-extra-emails</td><td class="ds" data-i18n='apiRefDescASensordayExtraEmails02'>Saves extra recipients for the sensors daily report</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/logday-extra-emails</td><td class="ds" data-i18n='apiRefDescALogdayExtraEmails'>Extra recipients for the logs daily report</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/logday-extra-emails</td><td class="ds" data-i18n='apiRefDescALogdayExtraEmails02'>Saves extra recipients for the logs daily report</td></tr>
</table></div>
</details>

<details>
<summary data-i18n='apiRefSecScheduled'> Comandos Agendados <span class="cnt">10</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/scheduled-command-slots</td><td class="ds" data-i18n='apiRefDescAScheduledCommandSlots'>Lists interval-scheduled commands</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/scheduled-command-slot</td><td class="ds" data-i18n='apiRefDescAScheduledCommandSlot'>Gets a command slot by index</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/scheduled-command-slot</td><td class="ds" data-i18n='apiRefDescAScheduledCommandSlot02'>Saves a scheduled command slot</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/scheduled-datetime-slots</td><td class="ds" data-i18n='apiRefDescAScheduledDatetimeSlots'>Lists date/time-scheduled commands</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/scheduled-datetime-slot</td><td class="ds" data-i18n='apiRefDescAScheduledDatetimeSlot'>Gets a date/time slot by index</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/scheduled-datetime-slot</td><td class="ds" data-i18n='apiRefDescAScheduledDatetimeSlot02'>Saves a date/time slot</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/daily-reboot</td><td class="ds" data-i18n='apiRefDescADailyReboot'>Daily reboot configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/daily-reboot</td><td class="ds" data-i18n='apiRefDescADailyReboot02'>Saves the daily reboot time</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/system-command</td><td class="ds" data-i18n='apiRefDescASystemCommand'>Runs a system command</td></tr>
<tr data-feat="modbus"><td><span class="m p">POST</span></td><td class="r">/api/modbus-command</td><td class="ds" data-i18n='apiRefDescAModbusCommand'>Runs a Modbus command</td></tr>
</table></div>
</details>

<details data-feat="sensors">
<summary data-i18n='apiRefSecSensors'>&#128202; Sensores <span class="cnt">18</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sensor-config</td><td class="ds" data-i18n='apiRefDescASensorConfig'>Full sensor configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sensor-config</td><td class="ds" data-i18n='apiRefDescASensorConfig02'>Saves the sensor configuration</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sensor-slots</td><td class="ds" data-i18n='apiRefDescASensorSlots'>Sensor slots and last values</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sensor-measure</td><td class="ds" data-i18n='apiRefDescASensorMeasure'>Triggers an immediate sensor reading</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sensor-reset-stats</td><td class="ds" data-i18n='apiRefDescASensorResetStats'>Resets the min/max/avg accumulators</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sensor-flush</td><td class="ds" data-i18n='apiRefDescASensorFlush'>Forces the pending SD write</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sensor-history</td><td class="ds" data-i18n='apiRefDescASensorHistory'>SD sensor history (paginated)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sensor-history-files</td><td class="ds" data-i18n='apiRefDescASensorHistoryFiles'>Lists sensor history files</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sensor-history-file</td><td class="ds" data-i18n='apiRefDescASensorHistoryFile'>Downloads a history file</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/chart-caps</td><td class="ds" data-i18n='apiRefDescAChartCaps'>Chart capabilities (PSRAM, max slots)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sensor-charts-session</td><td class="ds" data-i18n='apiRefDescASensorChartsSession'>Opens a live charts session</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sensor-charts-heartbeat</td><td class="ds" data-i18n='apiRefDescASensorChartsHeartbeat'>Charts session heartbeat</td></tr>
<tr><td><span class="m d">DEL</span></td><td class="r">/api/sensor-charts-session</td><td class="ds" data-i18n='apiRefDescASensorChartsSession02'>Closes the charts session</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sensor-charts-session-end</td><td class="ds" data-i18n='apiRefDescASensorChartsSessionEnd'>Ends the charts session (alternative to DELETE)</td></tr>
<tr data-feat="mq135"><td><span class="m g">GET</span></td><td class="r">/api/mq135-config</td><td class="ds" data-i18n='apiRefDescAMq135Config'>MQ-135 sensor configuration</td></tr>
<tr data-feat="mq135"><td><span class="m p">POST</span></td><td class="r">/api/mq135-config</td><td class="ds" data-i18n='apiRefDescAMq135Config02'>Saves the MQ-135 configuration</td></tr>
<tr data-feat="mq135"><td><span class="m p">POST</span></td><td class="r">/api/mq135-measure</td><td class="ds" data-i18n='apiRefDescAMq135Measure'>Immediate MQ-135 reading</td></tr>
<tr data-feat="mq135"><td><span class="m p">POST</span></td><td class="r">/api/mq135-calibrate-r0</td><td class="ds" data-i18n='apiRefDescAMq135CalibrateR0'>Ro calibration in clean air</td></tr>
</table></div>
</details>

<details data-feat="proximity">
<summary data-i18n='apiRefSecProximity'>&#128207; Proximidade <span class="cnt">14</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/proximity</td><td class="ds" data-i18n='apiRefDescAProximity'>Counter state (all channels)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/proximity/config</td><td class="ds" data-i18n='apiRefDescAProximityConfig'>Channel configuration (GPIO, mode, goal…)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/proximity/config</td><td class="ds" data-i18n='apiRefDescAProximityConfig02'>Saves the channel configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/proximity/reset</td><td class="ds" data-i18n='apiRefDescAProximityReset'>Resets one channel counter</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/proximity/reset-all</td><td class="ds" data-i18n='apiRefDescAProximityResetAll'>Resets all counters</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/proximity/shift-config</td><td class="ds" data-i18n='apiRefDescAProximityShiftConfig'>Shift configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/proximity/shift-config</td><td class="ds" data-i18n='apiRefDescAProximityShiftConfig02'>Saves the shift configuration</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/proximity/shiftday-emails</td><td class="ds" data-i18n='apiRefDescAProximityShiftdayEmails'>Email list for SHIFTDAY</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/proximity/shiftday-emails</td><td class="ds" data-i18n='apiRefDescAProximityShiftdayEmails02'>Saves SHIFTDAY emails</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/proximity-chart-files</td><td class="ds" data-i18n='apiRefDescAProximityChartFiles'>Proximity chart files on SD</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/proximity-chart-cols</td><td class="ds" data-i18n='apiRefDescAProximityChartCols'>Proximity chart columns</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/proximity-chart-data</td><td class="ds" data-i18n='apiRefDescAProximityChartData'>Proximity chart data</td></tr>
<tr data-feat="do_out"><td><span class="m g">GET</span></td><td class="r">/api/proximity/tca9554</td><td class="ds" data-i18n='apiRefDescAProximityTca9554'>TCA9554 output relay configuration (DO)</td></tr>
<tr data-feat="do_out"><td><span class="m p">POST</span></td><td class="r">/api/proximity/tca9554</td><td class="ds" data-i18n='apiRefDescAProximityTca955402'>Saves the TCA9554 relay configuration</td></tr>
</table></div>
</details>

<details data-feat="modbus">
<summary data-i18n='apiRefSecModbus'>&#128295; Modbus <span class="cnt">15</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/modbus/config</td><td class="ds" data-i18n='apiRefDescAModbusConfig'>Modbus configuration (devices + registers)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/modbus/config</td><td class="ds" data-i18n='apiRefDescAModbusConfig02'>Saves the Modbus RTU configuration</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/modbus/live</td><td class="ds" data-i18n='apiRefDescAModbusLive'>Live values of the Modbus registers</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/modbus/poll-now</td><td class="ds" data-i18n='apiRefDescAModbusPollNow'>Forces an immediate Modbus read</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/modbus/rtu-write</td><td class="ds" data-i18n='apiRefDescAModbusRtuWrite'>Writes an RTU/TCP register by slaveId+addr</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/modbus/write</td><td class="ds" data-i18n='apiRefDescAModbusWrite'>Writes a register by deviceIndex+regIndex</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/modbus/scan-rtu</td><td class="ds" data-i18n='apiRefDescAModbusScanRtu'>Starts an RTU bus scan (IDs 1–247)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/modbus/scan-status</td><td class="ds" data-i18n='apiRefDescAModbusScanStatus'>Scan progress and devices found</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/modbus/scan-stop</td><td class="ds" data-i18n='apiRefDescAModbusScanStop'>Cancels the running scan</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/modbus/scan-tcp</td><td class="ds" data-i18n='apiRefDescAModbusScanTcp'>TCP subnet scan (port 502)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/modbus/stats</td><td class="ds" data-i18n='apiRefDescAModbusStats'>Poll/error statistics per device</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/modbus/reset-stats</td><td class="ds" data-i18n='apiRefDescAModbusResetStats'>Resets the poll/error counters</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/modbus-chart-files</td><td class="ds" data-i18n='apiRefDescAModbusChartFiles'>Modbus chart files on SD</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/modbus-chart-cols</td><td class="ds" data-i18n='apiRefDescAModbusChartCols'>Modbus chart columns</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/modbus-chart-data</td><td class="ds" data-i18n='apiRefDescAModbusChartData'>Modbus chart data</td></tr>
</table></div>
</details>

<details data-feat="sdcard">
<summary data-i18n='apiRefSecSdcard'>&#128190; SD Card / Files <span class="cnt">14</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sd-status</td><td class="ds" data-i18n='apiRefDescASdStatus'>Presence, free space and mount state</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sd-buffers</td><td class="ds" data-i18n='apiRefDescASdBuffers'>SD write buffer state</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sdcard-config</td><td class="ds" data-i18n='apiRefDescASdcardConfig'>SD pipeline configuration (folders, sizes)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sdcard-config-save</td><td class="ds" data-i18n='apiRefDescASdcardConfigSave'>Saves the SD pipeline configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sd-action</td><td class="ds" data-i18n='apiRefDescASdAction'>SD administrative action (flush, rotate, etc.)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/storage-retry-sd</td><td class="ds" data-i18n='apiRefDescAStorageRetrySd'>Retries the SD initialization</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sd/mount</td><td class="ds" data-i18n='apiRefDescASdMount'>Mounts the SD card</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sd/unmount</td><td class="ds" data-i18n='apiRefDescASdUnmount'>Unmounts the SD card</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/sd/download</td><td class="ds" data-i18n='apiRefDescASdDownload'>SD file download (streaming)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sd/create-file</td><td class="ds" data-i18n='apiRefDescASdCreateFile'>Creates a new file on SD</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sd/create-dir</td><td class="ds" data-i18n='apiRefDescASdCreateDir'>Creates a new directory on SD</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sd/rename</td><td class="ds" data-i18n='apiRefDescASdRename'>Renames a file or directory</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sd/delete</td><td class="ds" data-i18n='apiRefDescASdDelete'>Removes a file or directory</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/sd/delete-multiple</td><td class="ds" data-i18n='apiRefDescASdDeleteMultiple'>Removes multiple files at once</td></tr>
</table></div>
</details>

<details data-feat="ota">
<summary data-i18n='apiRefSecOta'>&#128260; OTA &amp; Sistema <span class="cnt">6</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/enable-ota</td><td class="ds" data-i18n='apiRefDescAEnableOta'>Opens the OTA update window (temporary, auth required)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/firmware-upload</td><td class="ds" data-i18n='apiRefDescAFirmwareUpload'>Uploads a .bin file (multipart)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/restart</td><td class="ds" data-i18n='apiRefDescARestart'>Restarts the ESP32</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/factory-reset</td><td class="ds" data-i18n='apiRefDescAFactoryReset'>Erases NVS and restarts (factory reset)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/test</td><td class="ds" data-i18n='apiRefDescATest'>Connectivity ping</td></tr>
<tr data-feat="debug_api"><td><span class="m g">GET</span></td><td class="r">/api/db25-inject</td><td class="ds" data-i18n='apiRefDescADb25Inject'>Injects test data into the DB25/LPT emulator</td></tr>
</table></div>
</details>
<details data-feat="grbl">
<summary data-i18n='apiRefSecGrbl'>&#128736;&#65039; GRBL G-Code Sender <span class="cnt">28</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/status</td><td class="ds" data-i18n='apiRefDescAGrblStatus'>GRBL state (stream, positions, USB, errors)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/debug</td><td class="ds" data-i18n='apiRefDescAGrblDebug'>Dumps the PSRAM debug ring buffer</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/control-token</td><td class="ds" data-i18n='apiRefDescAGrblControlToken'>Write token for concurrent control</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/events</td><td class="ds" data-i18n='apiRefDescAGrblEvents'>SSE: console, production and state events</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/list-gcode</td><td class="ds" data-i18n='apiRefDescAGrblListGcode'>Lists G-code files in /gcode/ on SD</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/read-gcode-text</td><td class="ds" data-i18n='apiRefDescAGrblReadGcodeText'>Streams the text content of a G-code file</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/frame-bbox</td><td class="ds" data-i18n='apiRefDescAGrblFrameBbox'>2D bounding box of a G-code file</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/psram-file</td><td class="ds" data-i18n='apiRefDescAGrblPsramFile'>Info of the file loaded in PSRAM</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/grbl/psram-read-text</td><td class="ds" data-i18n='apiRefDescAGrblPsramReadText'>PSRAM buffer content as text</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/send</td><td class="ds" data-i18n='apiRefDescAGrblSend'>Sends a raw command to GRBL</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/stream</td><td class="ds" data-i18n='apiRefDescAGrblStream'>Starts streaming an SD file to GRBL</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/queue-start</td><td class="ds" data-i18n='apiRefDescAGrblQueueStart'>Runs an inline queue of G-code commands</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/queue-stop</td><td class="ds" data-i18n='apiRefDescAGrblQueueStop'>Stops the local queue execution</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/pause</td><td class="ds" data-i18n='apiRefDescAGrblPause'>Pauses the active GRBL stream</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/resume</td><td class="ds" data-i18n='apiRefDescAGrblResume'>Resumes the paused GRBL stream</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/cancel</td><td class="ds" data-i18n='apiRefDescAGrblCancel'>Cancels the active GRBL job/stream</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/poll-interval</td><td class="ds" data-i18n='apiRefDescAGrblPollInterval'>Sets the state poll interval (ms)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/usb-restart</td><td class="ds" data-i18n='apiRefDescAGrblUsbRestart'>Restarts the USB Host stack</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/usb-line-coding</td><td class="ds" data-i18n='apiRefDescAGrblUsbLineCoding'>Configures CDC (baud, data, stop, parity)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/auto-detect-baud</td><td class="ds" data-i18n='apiRefDescAGrblAutoDetectBaud'>Detects the baud rate automatically</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/create-dir</td><td class="ds" data-i18n='apiRefDescAGrblCreateDir'>Creates a directory in /gcode/</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/delete-path</td><td class="ds" data-i18n='apiRefDescAGrblDeletePath'>Deletes a file or directory in /gcode/</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/upload-gcode-stream</td><td class="ds" data-i18n='apiRefDescAGrblUploadGcodeStream'>Uploads a G-code file to SD</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/psram-upload</td><td class="ds" data-i18n='apiRefDescAGrblPsramUpload'>Uploads G-code to the PSRAM buffer</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/psram-stream</td><td class="ds" data-i18n='apiRefDescAGrblPsramStream'>Starts streaming the PSRAM buffer to GRBL</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/grbl/psram-clear</td><td class="ds" data-i18n='apiRefDescAGrblPsramClear'>Clears the PSRAM buffer</td></tr>
</table></div>
</details>

<details data-feat="camera">
<summary data-i18n='apiRefSecCamera'>&#128247; Camera <span class="cnt">3</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/camera/status</td><td class="ds" data-i18n='apiRefDescACameraStatus'>Camera state (ready, model, FPS, heap)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/cam/snapshot</td><td class="ds" data-i18n='apiRefDescCSnapshot'>Captures a single JPEG frame (image/jpeg)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/camera/start</td><td class="ds" data-i18n='apiRefDescACameraStart'>Starts the camera asynchronously</td></tr>
</table></div>
</details>

<details data-feat="uda1334a">
<summary data-i18n='apiRefSecAudio'>&#127925; Audio / Radio <span class="cnt">16</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/radio/status</td><td class="ds" data-i18n='apiRefDescARadioStatus'>Playback state (track, volume, state)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/radio/queue</td><td class="ds" data-i18n='apiRefDescARadioQueue'>Current playback queue</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/radio/favorites</td><td class="ds" data-i18n='apiRefDescARadioFavorites'>Favorites list</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/stop</td><td class="ds" data-i18n='apiRefDescARadioStop'>Stop playback</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/next</td><td class="ds" data-i18n='apiRefDescARadioNext'>Next track in queue</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/random</td><td class="ds" data-i18n='apiRefDescARadioRandom'>Random track from queue</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/volume</td><td class="ds" data-i18n='apiRefDescARadioVolume'>Set volume (0-100)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/play-url</td><td class="ds" data-i18n='apiRefDescARadioPlayUrl'>Play stream URL</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/play-sd</td><td class="ds" data-i18n='apiRefDescARadioPlaySd'>Play MP3 file from SD</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/queue</td><td class="ds" data-i18n='apiRefDescARadioQueue02'>Set playback queue</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/favorites/add</td><td class="ds" data-i18n='apiRefDescARadioFavoritesAdd'>Add favorite</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/favorites/remove</td><td class="ds" data-i18n='apiRefDescARadioFavoritesRemove'>Remove favorite by index</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/radio/favorites/play</td><td class="ds" data-i18n='apiRefDescARadioFavoritesPlay'>Play favorite by index</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/uda1334a-config</td><td class="ds" data-i18n='apiRefDescAUda1334aConfig'>UDA1334A DAC configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/uda1334a-config</td><td class="ds" data-i18n='apiRefDescAUda1334aConfig02'>Saves the DAC configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/uda1334a-test</td><td class="ds" data-i18n='apiRefDescAUda1334aTest'>Test tone on the DAC</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/uda1334a-timing</td><td class="ds" data-i18n='apiRefDescAUda1334aTiming'>60s test: measures the real I2S clock vs expected</td></tr>
</table></div>
</details>

<details>
<summary data-i18n='apiRefSecAccess'>&#128272; Access Control <span class="cnt">4</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/access-config</td><td class="ds" data-i18n='apiRefDescAAccessConfig'>Authentication and active sessions configuration</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/access-config</td><td class="ds" data-i18n='apiRefDescAAccessConfig02'>Saves the access configuration (password, etc.)</td></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/session-history</td><td class="ds" data-i18n='apiRefDescASessionHistory'>Web session history (IP, user-agent, timestamp)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/access-kick-all</td><td class="ds" data-i18n='apiRefDescAAccessKickAll'>Ends all active sessions (forces re-authentication)</td></tr>
</table></div>
</details>

<details data-feat="do_out">
<summary data-i18n='apiRefSecDigitalOut'>&#9889; Digital Outputs <span class="cnt">2</span></summary>
<div class='table-wrapper'><table>
<tr><th data-i18n='apiRefMethod'>Method</th><th data-i18n='apiRefRoute'>Rota</th><th data-i18n='apiRefDescription'>Description</th></tr>
<tr><td><span class="m g">GET</span></td><td class="r">/api/do/status</td><td class="ds" data-i18n='apiRefDescADoStatus'>Current state of the digital outputs (TCA9554 / DO)</td></tr>
<tr><td><span class="m p">POST</span></td><td class="r">/api/do/set</td><td class="ds" data-i18n='apiRefDescADoSet'>Enables or disables a digital output by index</td></tr>
</table></div>
</details>
</div>
<script>
fetch('/api/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
  var port=location.port&&location.port!='80'?':'+location.port:'';
  var base='http://'+(d.ip_address||location.hostname)+port;
  document.querySelectorAll('span.g').forEach(function(badge){
    var row=badge.closest('tr');
    if(!row) return;
    var cells=row.querySelectorAll('td');
    if(cells.length<2) return;
    var routeCell=cells[1];
    var route=routeCell.textContent.trim();
    var url=base+route;
    var a=document.createElement('a');
    a.className='r'; a.href=url; a.target='_blank'; a.rel='noopener'; a.textContent=route;
    a.addEventListener('click',function(e){
      e.preventDefault();
      fetch(url,{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){
        var pretty;try{pretty=JSON.stringify(JSON.parse(t),null,2);}catch(ex){pretty=t;}
        var blob=new Blob([pretty],{type:'text/plain;charset=utf-8'});
        var bu=URL.createObjectURL(blob);
        window.open(bu,'_blank','noopener');
        setTimeout(function(){URL.revokeObjectURL(bu);},15000);
      }).catch(function(){window.open(url,'_blank','noopener');});
    });
    routeCell.textContent='';
    routeCell.appendChild(a);
    var btn=document.createElement('button');
    btn.className='try-btn';
    btn.textContent='▶';
    btn.title='Test endpoint';
    btn.setAttribute('data-i18n-title','apiRefTestBtn');
    var respRow=document.createElement('tr');
    respRow.style.display='none';
    var td=document.createElement('td');
    td.colSpan=3;
    td.style.padding='0 10px 8px';
    var pre=document.createElement('pre');
    pre.className='api-pre';
    td.appendChild(pre);
    respRow.appendChild(td);
    row.parentNode.insertBefore(respRow,row.nextSibling);
    btn.onclick=function(){
      if(respRow.style.display!='none'){respRow.style.display='none';btn.textContent='▶';return;}
      btn.textContent='…';btn.disabled=true;
      fetch(url,{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){
        try{pre.textContent=JSON.stringify(JSON.parse(t),null,2);}catch(e){pre.textContent=t;}
        respRow.style.display='';btn.textContent='▲';btn.disabled=false;
      }).catch(function(e){pre.textContent='Error: '+e;respRow.style.display='';btn.textContent='▲';btn.disabled=false;});
    };
    routeCell.appendChild(btn);
  });
}).catch(function(){});
// Feature-flag filtering: hide sections and rows not compiled into the firmware
if(typeof _F!=='undefined'){Object.keys(_F).forEach(function(k){if(!_F[k]){document.querySelectorAll('[data-feat="'+k+'"]').forEach(function(el){el.style.display='none';});}});}
</script>
</div></div>
)rawliteral";
