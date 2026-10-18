#pragma once

static const char CORE_CSS[] PROGMEM = R"CSS(
*{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,-apple-system,'Segoe UI',Roboto,'Noto Sans','Ubuntu','Cantarell','Helvetica Neue',sans-serif}
html,body{background:#0b3069;color:#b5bbc9;font-size:16px;padding:0;min-height:100vh;overflow-x:hidden}
html:not([data-i18n-ready='1']) [data-i18n],html:not([data-i18n-ready='1']) [data-i18n-html]{visibility:hidden}
.page{width:100%;background:#060d28;min-height:100vh}
.wrap{padding:16px;padding-top:80px}
/* Override: any .actions inside .wrap that is not the nav-inicio button must be static. */
.wrap .actions:not(.nav-inicio){position:static !important;transform:none !important}
.wrap > .actions:first-child + h1{margin-top:12px}
@media (min-width:768px){body{display:flex;justify-content:center;align-items:flex-start;padding:20px 0}.page{max-width:1000px;width:100%;margin:0 auto;border-radius:12px;box-shadow:0 8px 32px rgba(0,0,0,0.4);border:1px solid #1e66c7;position:relative;overflow:hidden}.wrap{padding:32px;padding-top:32px}}
/* Typography standard - GLOBAL (applied to ALL pages) */
h1{font-size:22px!important;text-align:center;margin:0 0 12px 0;color:#b5bbc9}
h2{font-size:17px!important;text-align:center;margin:0 0 10px 0;color:#b5bbc9}
h3{font-size:15px!important;text-align:center;margin:0 0 8px 0;color:#b5bbc9}
.page,.wrap,body,button,td,th{font-size:13px!important}
label,label strong,strong{font-size:14px!important}
.note,.desc,.section-desc,small{font-size:11px!important}
.page.home h1{font-size:19px !important}
@media (min-width:768px){h1{font-size:24px!important}h2{font-size:18px!important}h3{font-size:16px!important}.page,.wrap,body,button,td,th{font-size:14px!important}label,label strong,strong{font-size:15px!important}.note,.desc,.section-desc,small{font-size:12px!important}.page.home h1{font-size:21px!important}}
.btn,button,a.btn{background:#1e66c7;color:#b5bbc9;border:2px solid #20a3b9;border-radius:9999px !important;padding:8px 24px !important;cursor:pointer;text-decoration:none;display:block;margin:6px auto;font-size:16px;text-align:center;line-height:16px;min-height:40px;width:100%;max-width:420px;overflow:hidden;transition:all 0.2s ease}
.btn[href='/'],.menu .btn{background:#0d4a8a;border-color:#0d4a8a}
.btn:hover,button:hover{background:#20a3b9;border-color:#b5bbc9;color:#060d28;transform:translateY(-1px);box-shadow:0 4px 12px rgba(32,163,185,0.3)}
@media (min-width:768px){.btn,button,a.btn{padding:12px 32px!important;font-size:15px;min-height:48px}}
.btn-danger{background:#cd5c5c;border-color:#b22222}
input,select,textarea{background:#0b3069;color:#b5bbc9;border:1px solid #1e66c7;border-radius:6px;padding:4px 8px;width:100%;font-size:13px!important;min-height:28px;transition:all 0.2s ease}
input:focus,select:focus{border-color:#20a3b9;outline:0;background:#060d28}
input:hover,select:hover,textarea:hover{border-color:#20a3b9;background:#0a2547}
@media (min-width:768px){input,select,textarea{min-height:36px;padding:6px 10px;font-size:14px!important}}
@media (min-width:768px){#rec_body input,#rec_body select,#sched_dt_body input,#sched_dt_body select,#sched_cmd_body input,#sched_cmd_body select{min-height:28px!important;height:auto!important;padding:3px 6px!important;font-size:13px!important}}
input[type=checkbox]{width:16px;height:16px;cursor:pointer;accent-color:#3b82f6}
label{display:block;margin:8px 0 4px;color:#b5bbc9}
.desc,.note,.section-desc{opacity:.9;margin:4px 0;color:#b5bbc9}
.section{background:rgba(30,102,199,.1);border:1px solid #1e66c7;border-radius:8px;padding:12px;margin:8px 0}
@media (min-width:768px){.section{border-radius:10px;padding:24px;margin:12px 0}}
/* Normalize all inline section boxes across pages to match System Settings visual rhythm. */
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a']{
  padding:20px !important;
  margin-top:20px !important;
  text-align:center;
}
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] h2,
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] h3,
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] p,
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] .section-desc,
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] .desc,
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] .note{
  text-align:center;
}
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] table,
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] th,
div[style*='border:2px solid'][style*='border-radius:8px'][style*='background:#0f172a'] td{
  text-align:left;
}
.menu{display:flex;flex-direction:column;gap:8px;margin:12px 0;align-items:center}
.actions{display:flex;flex-direction:column;gap:6px;align-items:center;position:fixed;top:10px;left:50%;transform:translateX(-50%);z-index:9999;background:transparent;padding:0;border-radius:0;box-shadow:none}
.actions.nav-inicio{position:static;top:auto;left:auto;transform:none;width:100%;flex-direction:row;justify-content:center}
.page-actions{display:flex;flex-direction:column;gap:6px;margin:12px 0;align-items:center}
.nav-inicio{padding:8px 0 16px 0;background:transparent;border:none;z-index:100}
.nav-inicio .btn{margin:0 !important;width:85% !important;max-width:360px !important;padding:16px 40px !important;border-radius:9999px !important;font-size:20px !important;font-weight:600;min-height:52px !important;text-align:center}
.actions-fixed{position:static;width:100%;background:transparent;padding:16px 0;display:flex;gap:10px;justify-content:center;align-items:center;flex-wrap:wrap;z-index:50}
@media(min-width:768px){.actions-fixed{padding:20px;gap:12px}}
.page.home .wrap{padding-top:16px}
.menu .btn{border-radius:9999px;padding:10px 28px;width:100%;max-width:420px;overflow:hidden;margin:3px 0}
.actions .btn, form .btn, .page-actions .btn { padding:10px 24px; border-radius:9999px; width:100%; max-width:420px; overflow:hidden }
table{width:100%;border-collapse:collapse;background:rgba(30,102,199,.1);border-radius:6px;border:1px solid #1e66c7;table-layout:fixed}
th,td{padding:8px;border-bottom:1px solid #1e66c7;color:#b5bbc9;font-size:13px;word-wrap:break-word;word-break:break-word}
th{background:#1e66c7;font-weight:600}
@media (min-width:768px){th,td{padding:10px;font-size:14px}}
.text-center{text-align:center}
.info-item{padding:8px;background:rgba(30,102,199,.15);border-radius:4px;margin:4px 0;font-size:14px;text-align:center;border:1px solid #1e66c7}
.status-success{color:#20a3b9}
.status-warning{color:#ffa500}
.status-error{color:#ff6b6b}
.text-muted{opacity:.7}
.fw-bold{font-weight:bold}
.desc{font-weight:normal !important}
.desc strong,.desc b{font-weight:normal !important}
.mono{font-family:monospace}
.net{padding:12px;margin:4px 0;border:1px solid #333;border-radius:6px;cursor:pointer;background:rgba(255,255,255,.05);min-height:48px}
.net:hover,.net.sel{border-color:#0af;background:rgba(0,170,255,.1)}
.monitor-controls{display:flex;flex-direction:column;gap:8px;align-items:center}
.monitor-controls .cmd-input{width:100%;box-sizing:border-box}
.monitor-controls .ctrl-buttons{display:flex;flex-direction:column;gap:8px;align-items:center;width:100%}
.monitor-controls .ctrl-buttons .btn{width:100%;max-width:420px;margin:6px 0}
[class*="btn"], button[class*="btn"], a[class*="btn"], .btn, a.btn {
    border-radius: 9999px !important;
    padding: 10px 24px !important;
    min-height: 40px !important;
    overflow: hidden !important;
}
.menu .btn, .actions .btn, form .btn, .wrap > .actions .btn, .page-actions .btn {
    border-radius: 9999px !important;
    width: 100% !important;
    max-width: 420px !important;
    display: block !important;
}
.btn-row{display:flex;flex-direction:column;gap:10px;align-items:center;width:100%}
.btn-row .btn{margin:0!important}
.home-hero{background:linear-gradient(135deg,#1e3a8a 0%,#3b82f6 100%);border-radius:12px;padding:24px 20px;text-align:center;margin-bottom:28px;box-shadow:0 4px 12px rgba(59,130,246,0.3);display:flex;flex-direction:column;align-items:center;justify-content:center}
.home-hero h1{width:100%;display:flex;align-items:center;justify-content:center;gap:10px;text-align:center}
.home-hero > div{width:100%;text-align:center}
.home-grid{display:grid;gap:16px;margin-top:24px}
.home-section{border:none;background:transparent;padding:0;min-width:0;display:grid;gap:5px;margin-top:28px}
.home-section h2{margin:0 0 12px 0!important}
.home-links{display:grid;gap:8px}
.home-section .btn{width:100%!important;max-width:420px!important;margin:6px auto!important;display:block!important}
.status-message{margin-top:12px;min-height:20px;text-align:center;font-size:13px;color:#e0e7ff}
@media (min-width:768px){.btn-row{flex-direction:row;justify-content:center;flex-wrap:wrap}.btn-row .btn{width:auto!important;min-width:240px;max-width:320px!important;display:inline-flex!important;justify-content:center;align-items:center}.home-grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.home-section{padding:0}.home-wide{grid-column:1 / -1}.home-section .btn{max-width:420px!important;margin:6px auto!important}}
::-webkit-scrollbar{width:10px;height:10px}
::-webkit-scrollbar-track{background:#060d28}
::-webkit-scrollbar-thumb{background:#1e66c7;border-radius:5px}
::-webkit-scrollbar-thumb:hover{background:#20a3b9}
@media (max-width:480px){
  .wrap{padding:12px;padding-top:75px;padding-bottom:80px;padding-left:max(12px, env(safe-area-inset-left));padding-right:max(12px, env(safe-area-inset-right))}
  .page h1{font-size:18px !important}
  .page h2{font-size:14px !important}
  .page h3{font-size:13px !important}
  body,.page,.wrap{font-size:15px!important}
  .note,.desc,.section-desc,small{font-size:12px!important}
  .btn,button,a.btn{font-size:15px;min-height:44px!important;padding:12px 20px !important;max-width:360px}
  input,select,textarea{font-size:15px;min-height:44px!important;padding:10px}
  input[type=checkbox]{width:14px;height:14px}
  table{table-layout:fixed}
  th,td{font-size:13px;padding:8px}
  .section{padding:16px;margin:12px 0}
  .nav-inicio{position:fixed!important;bottom:0!important;left:0!important;right:0!important;top:auto!important;transform:none!important;background:#060d28!important;border-top:2px solid #1e66c7!important;padding:12px!important;z-index:9999!important;box-shadow:0 -4px 12px rgba(0,0,0,0.5)!important;margin:0!important}
  .nav-inicio .btn{width:100%!important;max-width:none!important;margin:0!important}
  .btn:active,button:active{transform:scale(0.97);opacity:0.8}
  input:focus,select:focus{box-shadow:0 0 0 3px rgba(32,163,185,0.3)}
  #loadingOverlay>div>div{width:60px!important;height:60px!important}
  #loadingOverlay p{font-size:16px!important}
    .page.home{-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale;text-rendering:optimizeLegibility}
    .home-hero{padding:20px 16px!important}
    .home-hero h1{justify-content:center!important;text-align:center!important;line-height:1.2!important;font-size:20px!important;font-weight:700!important;letter-spacing:.2px;color:#eaf2ff!important;text-shadow:0 1px 1px rgba(0,0,0,.35)!important}
    .home-hero > div{margin-left:auto!important;margin-right:auto!important;text-align:center!important}
    .page.home h2{font-size:16px!important;font-weight:700!important;letter-spacing:.15px;color:#dbe9ff!important}
    .page.home .btn{font-weight:600!important;letter-spacing:.2px!important;text-rendering:optimizeLegibility}
    .page.home .status-message{font-weight:600!important;color:#eaf2ff!important}
    #rec_body input,#rec_body select{font-size:13px !important;min-height:26px !important;height:auto !important;padding:2px 5px !important}
    #sched_dt_body input,#sched_dt_body select{font-size:13px !important;min-height:26px !important;height:auto !important;padding:2px 5px !important}
    #sched_cmd_body input,#sched_cmd_body select{font-size:13px !important;min-height:26px !important;height:auto !important;padding:2px 5px !important}
}
@media (max-width:768px){
  .table-wrapper{position:relative}
  .table-wrapper::after{content:'👉 Deslize';position:absolute;right:10px;top:10px;background:#1e66c7;color:#fff;padding:4px 10px;border-radius:12px;font-size:11px;pointer-events:none;opacity:0.8;z-index:10}
}
@media (min-width:769px){
  .table-wrapper::after{display:none}
}
.bottom-nav{position:fixed;bottom:0;left:0;right:0;display:flex;justify-content:space-around;background:#060d28;border-top:2px solid #1e66c7;padding:8px 0;padding-bottom:calc(8px + env(safe-area-inset-bottom));z-index:10000}
.bottom-nav a,.bottom-nav button{flex:1;padding:10px 4px;font-size:13px;text-align:center;background:transparent;border:none;color:#b5bbc9;cursor:pointer;min-height:44px;display:flex;align-items:center;justify-content:center;gap:4px;text-decoration:none}
.bottom-nav .active{color:#20a3b9}
@media (min-width:768px){.bottom-nav{display:none}}
@media (max-width:767px){body{padding-bottom:64px}}
)CSS";