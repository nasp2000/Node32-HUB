#include "page_home_asset.h"

const char PAGE_HOME[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>
<title>Node32-Hub</title><link rel='stylesheet' href='/core.css'></head><body><div id='loadingOverlay' style='display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(10,25,41,0.95);z-index:99999;align-items:center;justify-content:center'><div style='text-align:center'><div style='border:4px solid #1e40af;border-top:4px solid #3b82f6;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:0 auto'></div><p style='color:#e6f1ff;margin-top:15px;font-size:14px' data-i18n='loading'>Processing...</p></div></div><style>@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style><main class='page home'><div class='wrap'>
<div class='home-hero'>
<h1 style='font-size:1.7em!important;line-height:1.15;margin:0;color:#fff;text-align:center;width:100%'>🤖 Node32-Hub</h1>
<div style='font-size:16px;opacity:.95;margin:8px 0 0 0;color:#e0f2fe;text-align:center;width:100%'>%%HERO_SUBTITLE%%</div>
</div>

%%MODULES_SECTION%%

<div class='home-section'>
<h2 style='margin:0 0 12px 0;font-size:18px;color:#60a5fa' data-i18n='homeMonTitle'>📊 Monitoring</h2>
%%MONITOR_BUTTON%%
%%EXPLORADOR_FICHEIROS%%
%%SERIAL_MONITOR_BTN%%
<a href='/status' class='btn' data-i18n='homeStatus'>📊 Status</a>
</div>

%%RECIPIENTS_SECTION%%

%%COMMANDS_SECTION%%


<div class='home-section'>
<h2 style='margin:0 0 12px 0;font-size:18px;color:#60a5fa' data-i18n='homeNetTitle'>🌐 Networks</h2>
%%WIFI_BUTTON%%
%%ETHERNET_BUTTON%%
%%NETWORKS_BUTTON%%
%%EMAIL_BUTTON%%
%%GOOGLE_SCRIPT_BUTTON%%
%%FTP_BUTTON%%
</div>

%%SYSTEM_SECTION%%

<div class='footer-container' style='margin-top:28px;clear:both;display:flex;justify-content:center;'>
    <footer class='footer' style='padding:12px 0;border-radius:12px;color:#fff;max-width:640px;width:100%;margin:0 auto;'>
            <div style='max-width:640px;width:100%;margin:0 auto;display:flex;flex-wrap:wrap;align-items:center;justify-content:center;gap:12px;padding:24px 20px;background:linear-gradient(135deg,#1e3a8a 0%,#3b82f6 100%);border-radius:12px;box-shadow:0 4px 12px rgba(59,130,246,0.3)'>
            <div style='min-width:260px;text-align:center'>
                <a href='https://github.com/nasp2000/Node32-HUB' target='_blank' style='color:#e0f2fe;text-decoration:underline;font-weight:600'>Node32-HUB</a>
                <div style='font-size:12px;opacity:.85;margin-top:4px' data-i18n='homeSince'>since July 2025</div>
            </div>
            <div style='flex:1;min-width:180px;text-align:center'>
                <div style='font-weight:700;color:#ffffff;font-size:14px;line-height:1.1'>Node32-Hub %%VERSION%%</div>
                <div style='font-size:12px;opacity:.9;margin-top:2px' data-i18n='footerDev'>Developed with Artificial Intelligence</div>
            </div>
            <div style='flex:1;min-width:160px;text-align:center;opacity:.9;font-size:15px;color:#e0f2fe;text-decoration:none;margin:0 8px' data-i18n='footerQuote'>
                "There's a storm coming in"
            </div>
        </div>
    </footer>
</div>

</div></main><script src='/auto.js?v=%%VERSION%%'></script></body></html>)rawliteral";