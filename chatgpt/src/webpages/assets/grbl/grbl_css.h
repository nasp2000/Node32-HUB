R"GRBL(
    <style>
      .page.grbl-controller{min-height:100vh;font-family:system-ui,-apple-system,'Segoe UI',Roboto,'Noto Sans','Ubuntu','Cantarell','Helvetica Neue',sans-serif;background:var(--g-bg);color:var(--g-text);transition:background .3s,color .3s;overflow-x:hidden;max-width:none!important;width:100%!important;margin:0!important;border:none!important;border-radius:0!important;box-shadow:none!important}
      .page.grbl-controller .wrap{max-width:none!important;width:100%!important;padding:0!important;margin:0!important}
      .page.grbl-controller::-webkit-scrollbar{width:5px;height:5px}
      .page.grbl-controller::-webkit-scrollbar-thumb{background:rgba(0,191,255,.12);border-radius:3px}
      .page.grbl-controller::-webkit-scrollbar-thumb:hover{background:rgba(0,191,255,.25)}
      .page.grbl-controller::-webkit-scrollbar-track{background:transparent}
      .page.grbl-controller{
        --ui-r:8px;
        --ui-r-sm:6px;
        --ui-h:24px;
        --ui-fs:10px;
        --ui-gap:6px;
      }

      .page.grbl-controller{
        --g-bg:#080D18;--g-panel:#0D1524;--g-widget:#111D30;--g-bdr:#20344A;
        --g-text:#EAF2FA;--g-txt2:#95A3B8;--g-txt3:#687590;--g-acc:#00BFFF;
        --g-btn:linear-gradient(135deg,#0E4B7A,#168FD0);--g-banner-bg:linear-gradient(135deg,#080D18 0%,#0D1524 100%);
        --g-lp-bg:#0D1524;--g-sel:#111D30;
        --g-sd-row-bg:#1A2940;--g-sd-row-hover:#223552;--g-sd-row-selected:rgba(0,191,255,.12);
        --g-sd-row-border:rgba(0,191,255,.25);--g-sd-row-hover-border:rgba(0,191,255,.5);--g-sd-row-selected-border:#00BFFF;
        --g-sd-row-text:#EAF2FA;--g-sd-row-subtext:#A0B8D8;
        --g-jog-top:#00BFFF;--g-jog-mid:#0E7DBF;--g-jog-bot:#095A8F;--g-jog-border:#1A3040;
        --g-jog-shadow:rgba(0,191,255,.25);--g-jog-diag-top:#0D1524;--g-jog-diag-bot:#080D18;--g-jog-diag-border:#00BFFF;
        --g-jog-center-top:#FF176F;--g-jog-center-bot:#A40D48;--g-jog-center-border:#FF176F;
      }
      .page.grbl-controller .btn{
        /* 3D Embossed / Relief effect — uses --g-btn from each theme */
        background: var(--g-btn) !important;
        border: 1px solid rgba(255,255,255,.25) !important;
        border-bottom: 2px solid rgba(0,0,0,.3) !important;
        border-right: 2px solid rgba(0,0,0,.2) !important;
        border-top: 1px solid rgba(255,255,255,.35) !important;
        border-left: 1px solid rgba(255,255,255,.2) !important;
        color: #fff !important;
        box-shadow: 0 4px 8px rgba(0,0,0,.35), 0 1px 0 rgba(255,255,255,.15) inset !important;
        text-shadow: 0 1px 2px rgba(0,0,0,.4) !important;
        transition: all 0.15s ease !important;
      }
      .page.grbl-controller .btn:hover{
        opacity:1!important;transform:translateY(-1px)!important;
        box-shadow:0 4px 12px rgba(0,191,255,.25),0 2px 0 rgba(255,255,255,.1) inset!important;
        filter:brightness(1.1)!important;
      }
      .page.grbl-controller .btn:active{
        transform: translateY(1px) !important;
        box-shadow: 0 2px 4px rgba(0,0,0,.4), 0 1px 0 rgba(0,0,0,.1) inset !important;
        border-bottom-width: 1px !important;
        border-right-width: 1px !important;
      }
      /* Smaller radio buttons */
      .page.grbl-controller input[type="radio"]{
        width: 14px !important;
        height: 14px !important;
        accent-color: #3b82f6;
        cursor: pointer;
        vertical-align: middle;
        margin: 0 4px 0 0;
      }
      .page.grbl-controller input[type="radio"]:checked{
        accent-color: #1d4ed8;
      }

      .page.grbl-controller .cncjs-shell-grid .btn{
        height:var(--ui-h)!important;min-height:var(--ui-h)!important;padding:0 10px!important;font-size:var(--ui-fs)!important
      }

      .page.grbl-controller input:not([type='checkbox']):not([type='radio']),
      .page.grbl-controller select,
      .page.grbl-controller textarea{
        border:1px solid var(--g-bdr)!important;background:var(--g-sel)!important;color:var(--g-text)!important;
        border-radius:var(--ui-r-sm)!important;font-size:var(--ui-fs)!important;transition:border-color .2s,box-shadow .2s;box-shadow:none!important;
        font-family:inherit!important
      }
      .page.grbl-controller input:not([type='checkbox']):not([type='radio']):focus,
      .page.grbl-controller select:focus,
      .page.grbl-controller textarea:focus{outline:2px solid var(--g-acc);outline-offset:1px;border-color:var(--g-acc)!important;box-shadow:0 0 0 4px rgba(0,191,255,.1)}
      .page.grbl-controller .note{color:var(--g-txt2)!important}

      .page.grbl-controller .cncjs-shell-grid input:not([type='checkbox']):not([type='radio']),
      .page.grbl-controller .cncjs-shell-grid select{
        height:var(--ui-h)!important;min-height:var(--ui-h)!important;padding:0 8px!important;line-height:1.15
      }
      .page.grbl-controller .cncjs-shell-grid textarea{
        padding:8px 10px!important;line-height:1.35
      }

      .grbl-banner{
        border:1px solid rgba(0,191,255,.25);border-radius:14px;padding:16px 24px 12px;margin:12px 12px 0;
        background:var(--g-banner-bg);box-shadow:0 2px 16px rgba(0,0,0,.4),0 0 32px rgba(0,191,255,.06);
        position:relative;overflow:hidden;text-align:center;
      }
      .grbl-banner:after{
        content:'';position:absolute;left:0;right:0;bottom:0;height:2px;
        background:linear-gradient(90deg,transparent,#00BFFF,transparent);
        background-size:200% 100%;animation:grblBannerFlow 4s linear infinite;
      }
      @keyframes grblBannerFlow{0%{background-position:200% 0}100%{background-position:-200% 0}}
      .grbl-banner h1{margin:0;font-size:clamp(17px,3.5vw,26px);text-align:center;color:var(--g-text);font-weight:700;letter-spacing:1px;text-shadow:0 2px 12px rgba(0,191,255,.2)}
      .grbl-banner p{margin:4px 0 0;text-align:center;color:var(--g-txt2);font-size:clamp(10px,1.4vw,12px);font-weight:500;letter-spacing:.3px;opacity:.8}

      .grbl-ctrlbar{
        border:1px solid var(--g-bdr);border-radius:12px;padding:10px 12px;margin:8px 12px;
        background:var(--g-panel);display:grid;grid-template-columns:minmax(0,1fr) auto;column-gap:8px;row-gap:8px;align-items:center;
        box-shadow:0 2px 12px rgba(0,0,0,.3);overflow-x:hidden;
      }
      .grbl-cg{display:flex;flex-direction:row;align-items:center;gap:8px;min-width:0}
      .grbl-cg label{font-size:12px;color:var(--g-txt2);font-weight:700;white-space:nowrap;text-transform:uppercase;letter-spacing:.4px;margin:0}
      .grbl-cg select,.grbl-cg input{
        width:142px;padding:4px 8px;border-radius:5px;height:34px;min-height:34px;font-size:12px!important;
      }
      .grbl-cb-main{display:block;min-width:0;max-width:100%;overflow-x:auto;overflow-y:hidden}
      .grbl-cb-row{display:flex;flex-wrap:nowrap;gap:6px;justify-content:center;align-items:center;min-width:max-content;padding:4px 8px;border:1px solid rgba(148,163,184,.18);border-radius:999px;background:rgba(15,23,42,.18)}
      .grbl-cb-main .btn{width:auto!important;min-width:86px;max-width:none;height:28px;min-height:28px!important;padding:0 8px!important;display:inline-flex!important;align-items:center;justify-content:center;line-height:1;flex:0 0 auto}
      /* Semantic button colors */
      #btn_quick_hold{background:linear-gradient(135deg,#B45309,#D97706)!important}
      #btn_quick_resume{background:linear-gradient(135deg,#047857,#059669)!important}
      #btn_quick_cancel,#btn_quick_reset{background:linear-gradient(135deg,#991B1B,#DC2626)!important}
      #btn_quick_status{background:var(--g-panel)!important;color:var(--g-txt2)!important}
      .grbl-cb-actions{display:flex;gap:8px;flex-wrap:wrap;align-items:center;flex:0 1 auto}
      .grbl-ctrlbar .grbl-cb-actions{display:flex;justify-content:flex-end;min-width:0;justify-self:end}
      .grbl-ctrlbar .grbl-cb-actions .btn{width:auto;margin:0!important;min-width:106px;height:30px;min-height:30px!important;padding:0 8px!important;display:inline-flex!important;align-items:center;justify-content:center;line-height:1}
      .grbl-chip{
        display:inline-flex;align-items:center;gap:6px;padding:5px 10px;border-radius:999px;white-space:nowrap;
        border:1px solid var(--g-bdr);background:var(--g-panel);font-size:12px;font-weight:700;color:var(--g-text)
      }

      .cncjs-shell-grid{--grbl-side-col:228px;display:grid;grid-template-columns:var(--grbl-side-col) minmax(0,1fr) var(--grbl-side-col);gap:8px;margin:8px 12px 0;align-items:start;width:calc(100% - 24px);transition:grid-template-columns .35s ease}
      .cncjs-shell-grid.dock-left-hidden{grid-template-columns:0px minmax(0,1fr) var(--grbl-side-col)}
      .cncjs-shell-grid.dock-right-hidden{grid-template-columns:var(--grbl-side-col) minmax(0,1fr) 0px}
      .cncjs-shell-grid.dock-left-hidden.dock-right-hidden{grid-template-columns:0px minmax(0,1fr) 0px}
      .cncjs-shell-grid.dock-left-hidden>.grbl-left-column,.cncjs-shell-grid.dock-right-hidden>.grbl-right-column{overflow:hidden;pointer-events:none;visibility:hidden;width:0!important}
      .grbl-side-column{display:grid;gap:8px;min-width:0;width:var(--grbl-side-col)}
      .grbl-center-column{display:grid;gap:8px;min-width:0}
      #wid_workspace{margin-top:0!important;padding:5px 10px 7px!important}
      #wid_workspace .grbl-section-title{font-size:18px!important;line-height:1!important;margin:0 0 4px 0!important}
      #wid_workspace .cncjs-workspace-header{margin:0 0 4px!important}
      #wid_workspace .cncjs-status-pill{min-height:20px;padding:2px 8px!important;font-size:10px!important;line-height:1.1}
      #wid_workspace .grbl-dro-panel{padding:3px!important}
      #wid_workspace .grbl-dro-grid{grid-template-columns:36px 1fr 1fr}
      #wid_workspace .grbl-dro-cell{padding:2px 5px!important;font-size:10px!important;line-height:1!important}
      #wid_workspace .grbl-dro-cell.ax{font-size:13px!important}
      #wid_workspace .grbl-dro-actions{margin-top:3px!important;gap:4px!important}
      #wid_workspace .grbl-dro-actions .btn{height:22px!important;min-height:22px!important}
      #wid_console.grbl-center-console{width:100%;margin:0 auto}
      #wid_console.grbl-center-console #grbl_console{overflow-x:auto!important;overflow-y:scroll!important;white-space:pre;word-break:normal;text-align:left!important;overscroll-behavior:contain;-webkit-overflow-scrolling:touch;scrollbar-gutter:stable both-edges}
      .grbl-left-column,.grbl-right-column{align-self:start;min-width:0}
      .grbl-center-column > *{min-width:0;max-width:100%}
      .grbl-center-column .cncjs-widget,
      .grbl-center-column .grbl-section-block{min-width:0;overflow:hidden}
      .grbl-left-column .cncjs-widget,
      .grbl-right-column .cncjs-widget{width:100%;max-width:100%;min-width:0;box-sizing:border-box;overflow:hidden}
      .grbl-left-column .cncjs-widget h3,
      .grbl-right-column .cncjs-widget h3{word-break:break-word}
      .cncjs-stack{display:grid;gap:8px}
      .cncjs-widget{border:1px solid var(--g-bdr);border-radius:10px;padding:8px;background:linear-gradient(180deg, rgba(255,255,255,.02), rgba(255,255,255,0)) ,var(--g-panel);box-shadow:0 1px 0 rgba(255,255,255,.04) inset,0 2px 10px rgba(0,0,0,.18)}
      .cncjs-widget h3{margin:0 0 6px 0;color:var(--g-acc);font-size:14px;font-weight:700;letter-spacing:.3px;text-align:center;opacity:.9}
      .grbl-widget-dock{display:grid;gap:8px}
      .grbl-dock-widget{border:2px solid var(--g-acc)!important;box-shadow:0 0 0 1px rgba(59,130,246,.22)}
      /* ---- WIDGET SETTINGS SIDE PANEL ---- */
      #grbl_lp_trigger{
        position:fixed;top:0;left:0;width:12px;height:100vh;z-index:400;cursor:e-resize;
      }
      #grbl_left_panel{
        position:fixed;top:0;left:0;height:100vh;width:300px;
        background:var(--g-panel);border-right:2px solid var(--g-acc);
        z-index:401;overflow-y:auto;overflow-x:hidden;
        transform:translateX(-100%);transition:transform .25s ease;
        box-shadow:4px 0 24px rgba(0,0,0,.42);
        display:flex;flex-direction:column;
      }
      #grbl_left_panel.open{transform:translateX(0)}
      .glp-header{
        padding:6px 10px 4px;
        border-bottom:1px solid var(--g-bdr);
        display:flex;justify-content:flex-start;align-items:center;gap:8px;
        min-height:28px;
      }
      .glp-header h2{margin:0;font-size:13px;font-weight:700;color:var(--g-acc)}
      .glp-header .glp-sub{display:block;font-size:9px;color:var(--g-txt3);margin-top:1px}
      .glp-close{
        background:var(--g-bg);border:1px solid var(--g-bdr);color:var(--g-txt2);font-size:10px;cursor:pointer;
        line-height:1;padding:2px 8px;border-radius:4px;display:flex;align-items:center;gap:3px;
        font-weight:600;white-space:nowrap;flex-shrink:0;
      }
      .glp-close:hover{color:var(--g-acc);border-color:var(--g-acc)}
      .glp-body{padding:4px 10px;display:flex;flex-direction:column;gap:6px;flex:1;overflow-y:auto;overflow-x:hidden}
      .glp-section{border:1px solid var(--g-bdr);border-radius:6px;padding:5px 8px;background:var(--g-widget);display:flex;flex-direction:column;gap:2px}
      .glp-section-title{font-size:8px;font-weight:700;color:var(--g-txt2);text-transform:uppercase;letter-spacing:.5px;margin-bottom:0}
      .glp-side-label{font-size:8px;font-weight:700;color:var(--g-acc);text-transform:uppercase;letter-spacing:.5px;padding:1px 0 0;border-bottom:1px solid rgba(0,191,255,.08)}
      .glp-toggle-row{display:flex;justify-content:space-between;align-items:center;gap:6px;min-height:0;padding:1px 0}
      .glp-toggle-row label{font-size:10px;color:var(--g-text);cursor:pointer;flex:1;line-height:1.2}
      .glp-toggle-row input[type=checkbox]{width:12px;height:12px;accent-color:var(--g-acc);cursor:pointer;flex-shrink:0}
      .glp-widget-group{margin-bottom:2px}
      .glp-widget-group:last-child{margin-bottom:0}
      .glp-widget-row{display:flex;align-items:center;gap:4px;padding:2px 0;border-bottom:1px solid rgba(255,255,255,.02);min-height:20px;white-space:nowrap}
      .glp-widget-row:last-child{border-bottom:none}
      .glp-widget-row:hover{background:rgba(0,191,255,.04)}
      .glp-widget-name{flex:1;font-size:10px;color:var(--g-text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;line-height:1.15;min-width:0}
      .glp-widget-name.muted{opacity:.3}
      .glp-widget-row button{width:18px;height:16px;min-width:18px;padding:0;border:1px solid var(--g-bdr);border-radius:3px;background:var(--g-widget);color:var(--g-txt2);cursor:pointer;font-size:9px;line-height:1;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0;box-sizing:border-box}
      .glp-widget-row button:hover{background:rgba(0,191,255,.15);border-color:var(--g-acc);color:var(--g-acc)}
      .glp-widget-row input[type=checkbox]{width:14px;height:14px;min-width:14px;accent-color:var(--g-acc);cursor:pointer;flex-shrink:0}
      .glp-side-label{font-size:7.5px;font-weight:700;color:var(--g-txt2);text-transform:uppercase;letter-spacing:.3px;margin-bottom:0;padding:0}
      .cncjs-metrics{display:grid;grid-template-columns:repeat(5,minmax(80px,1fr));gap:6px;margin-top:6px}
      .cncjs-metric{border:1px solid var(--g-bdr);border-radius:7px;background:var(--g-widget);padding:6px}
      .cncjs-metric strong{display:block;font-size:10px;color:var(--g-txt2);margin-bottom:2px}
      .cncjs-metric div{font-size:11px;color:var(--g-text)}
      .grbl-conn-metrics{grid-template-columns:repeat(2,minmax(76px,1fr))!important;gap:4px!important;margin-top:8px!important}
      #wid_connection .grbl-conn-metrics .cncjs-metric{padding:4px 5px;border-radius:6px}
      #wid_connection .grbl-conn-metrics .cncjs-metric strong{font-size:8.5px;line-height:1.05;margin-bottom:1px;letter-spacing:0;text-transform:none}
      #wid_connection .grbl-conn-metrics .cncjs-metric div{font-size:10px;line-height:1.05;font-weight:700}
      .grbl-lag-chip{display:inline-block;padding:2px 8px;border-radius:12px;font-weight:700;font-size:11px;color:#fff}
      .grbl-recent-errors{display:grid;gap:6px;max-height:220px;overflow:auto;margin-top:6px}
      .grbl-recent-error-row{border:1px solid var(--g-bdr);border-radius:8px;background:var(--g-widget);padding:6px}
      .grbl-recent-error-row .top{display:flex;justify-content:space-between;gap:8px;align-items:center}
      .grbl-recent-error-row .src{font-size:10px;font-weight:700;color:var(--g-txt2);text-transform:uppercase;letter-spacing:.3px}
      .grbl-recent-error-row .meta{font-size:10px;color:var(--g-txt3)}
      .grbl-recent-error-row .msg{font-size:11px;color:#fca5a5;margin-top:2px;word-break:break-word}
      .grbl-recent-error-row .preview{font-family:Consolas,'Courier New',monospace;font-size:10px;color:var(--g-text);margin-top:4px;white-space:pre-wrap;word-break:break-word}
      .cncjs-toolbar{display:grid;grid-template-columns:minmax(0,1fr) repeat(2,150px) auto;gap:8px;align-items:end}
      .cncjs-workspace-header{display:flex;flex-wrap:wrap;gap:8px;align-items:center;justify-content:space-between;margin:4px 0 6px}
      .cncjs-status-pill{display:inline-flex;align-items:center;gap:5px;padding:4px 10px;border-radius:999px;border:1px solid var(--g-bdr);background:var(--g-widget);font-size:10px;color:var(--g-txt2);font-weight:500}
      .cncjs-status-pill strong{color:var(--g-acc);font-weight:700}
      .grbl-mode-pill{border-color:var(--g-acc)!important;background:rgba(0,191,255,.08)!important;color:var(--g-acc)!important}
      .st-idle{color:#27E89A!important}.st-run{color:#00BFFF!important}.st-hold{color:#F59E0B!important}
      .st-alarm{color:#FF3D52!important}.st-home{color:#00BFFF!important}.st-offline{color:var(--g-txt3)!important}
      .grbl-dro-panel{border:1px solid var(--g-bdr);border-radius:8px;background:var(--g-widget);color:var(--g-text);padding:4px}
      .grbl-dro-grid{display:grid;grid-template-columns:42px 1fr 1fr;gap:1px;background:var(--g-bdr);border:1px solid var(--g-bdr);border-radius:6px;overflow:hidden}
      .grbl-dro-cell{background:var(--g-panel);padding:3px 5px;text-align:right;font-weight:700;font-size:11px;line-height:1.05;color:var(--g-text)}
      .grbl-dro-cell.hdr{background:var(--g-bg);color:var(--g-txt2);text-align:center;font-size:9px;font-weight:800}
      .grbl-dro-cell.ax{background:var(--g-widget);text-align:center;font-size:14px;color:var(--g-acc)}
      .grbl-dro-cell.val{font-family:Consolas,'Courier New',monospace;color:var(--g-text)}
      .grbl-dro-actions{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:4px;margin-top:4px}
      .grbl-dro-actions .btn{min-height:26px!important;padding:0 4px!important;font-weight:800;font-size:10px!important}
      .grbl-manual-shell{display:grid;gap:var(--ui-gap)}
      .grbl-manual-input{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:center}
      .grbl-manual-input .btn{min-width:108px}
      .grbl-manual-groups{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:2px}
      .grbl-manual-group{border:1px solid var(--g-bdr);border-radius:8px;background:var(--g-widget);padding:8px}
      .grbl-manual-group .title{display:block;font-size:11px;color:var(--g-txt2);font-weight:700;text-transform:uppercase;letter-spacing:.4px;margin:0 0 6px 0}
      .grbl-manual-grid{display:grid;grid-template-columns:1fr;gap:var(--ui-gap)}
      .grbl-job-widget{display:grid;gap:var(--ui-gap)}
      .grbl-job-row-3{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:var(--ui-gap)}
      .grbl-job-row-2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:var(--ui-gap)}
      .grbl-job-actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:var(--ui-gap)}
      #wid_job .grbl-job-row-3 .btn,
      #wid_job .grbl-job-row-2 .btn,
      #wid_job .grbl-job-actions .btn{min-width:76px}
      #btn_grbl_sd_deselect{white-space:normal!important;overflow:visible!important;text-overflow:clip!important;line-height:1.12!important;padding:7px 10px!important}
      .grbl-manual-grid .btn,
      .grbl-job-actions .btn,
      .cncjs-quick-grid .btn,
      .grbl-frame-actions .btn,
      .grbl-sd-actions .btn{min-height:var(--ui-h)!important}
      .grbl-frame-grid{display:grid;gap:var(--ui-gap)}
      .grbl-frame-params{display:flex;align-items:end;gap:8px;flex-wrap:nowrap}
      .grbl-frame-field{display:grid;grid-template-rows:auto 26px;gap:3px;min-width:0}
      .grbl-frame-field label{display:block;margin:0;font-size:9px;line-height:1.05;color:var(--g-txt2);font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
      #wid_frame .grbl-frame-input{height:26px!important;min-height:26px!important;padding:0 7px!important;font-size:10px!important;line-height:1.05}
      #grbl_frame_power{width:64px!important;max-width:64px!important}
      #grbl_frame_feed{width:92px!important;max-width:92px!important}
      .grbl-frame-actions{display:grid;grid-template-columns:1fr;gap:var(--ui-gap)}
      #wid_frame .grbl-frame-actions .btn{min-width:92px}
      .grbl-frame-bbox{font-size:10px;color:var(--g-txt2)}
      .grbl-sd-filter{width:100%}
      .grbl-sd-browser{border:1px solid var(--g-bdr);border-radius:8px;background:var(--g-widget);min-height:180px;max-height:340px;overflow:auto;padding:6px;display:grid;gap:6px}
      .grbl-sd-row{display:grid;grid-template-columns:minmax(0,1fr);gap:8px;align-items:center;padding:8px;border-radius:6px;border:1px solid var(--g-sd-row-border);background:var(--g-sd-row-bg);cursor:pointer;transition:border-color .18s ease,background .18s ease,box-shadow .18s ease}
      .grbl-sd-row:hover{border-color:var(--g-sd-row-hover-border);background:var(--g-sd-row-hover)}
      .grbl-sd-row.is-selected{border-color:var(--g-sd-row-selected-border);background:var(--g-sd-row-selected);box-shadow:0 0 0 1px rgba(0,191,255,.35),0 4px 12px rgba(2,6,23,.28)}
      .grbl-sd-meta{min-width:0}
      .grbl-sd-name{display:block;font-size:10px;font-weight:700;color:var(--g-sd-row-text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
      .grbl-sd-path{display:block;font-size:9px;color:var(--g-sd-row-subtext);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
      .grbl-sd-upload-progress{display:none;gap:6px;margin-top:6px;padding:6px 8px;border:1px solid rgba(59,130,246,.24);border-radius:8px;background:rgba(15,23,42,.28)}
      .grbl-sd-upload-progress.is-active{display:grid}
      .grbl-sd-upload-text{font-size:10px;color:var(--g-text)}
      .grbl-sd-upload-track{height:8px;border-radius:999px;background:rgba(148,163,184,.2);overflow:hidden}
      .grbl-sd-upload-fill{display:block;height:100%;width:0%;border-radius:999px;background:linear-gradient(90deg,#0ea5e9,#38bdf8);transition:width .12s linear}
      .grbl-sd-empty{padding:6px;border-radius:6px;border:1px dashed var(--g-bdr);font-size:10px;color:var(--g-txt2);text-align:center}
      .grbl-usb-actions{display:grid;grid-template-columns:1fr 1fr;gap:8px}
      .grbl-usb-actions .btn{display:flex!important;align-items:center;justify-content:center;text-align:center;min-height:32px!important;line-height:1.1}
      .grbl-2d-widget{padding:8px}
      .grbl-2d-toolbar{display:flex;align-items:center;gap:6px;margin-bottom:6px;min-width:0}
      .grbl-2d-toolbar h3{margin:0;flex:0 0 auto;white-space:nowrap}
      .grbl-2d-toolbar .grbl-2d-actions{display:flex!important;flex-direction:row!important;flex-wrap:wrap!important;gap:4px;align-items:center;justify-content:flex-start;min-width:0;flex:1 1 auto;padding-bottom:4px;max-width:100%}
      .grbl-2d-toolbar .grbl-2d-actions select,
      .grbl-2d-toolbar .grbl-2d-actions .btn{
        width:auto!important;
        min-width:0!important;
        max-width:none!important;
        flex:0 1 auto!important;
        margin:0!important;
        white-space:nowrap;
      }
      .grbl-2d-toolbar .grbl-2d-actions select{padding:2px 6px;min-height:28px!important;font-size:10px!important;max-width:148px!important}
      .grbl-2d-toolbar .grbl-2d-actions .btn{min-height:28px!important;display:inline-flex!important;align-items:center;justify-content:center;padding:0 10px!important;line-height:1;font-size:11px!important;width:auto!important;min-width:fit-content!important;max-width:none!important;overflow:visible;white-space:nowrap}
      .grbl-2d-toolbar .grbl-2d-actions .btn,
      .grbl-2d-scrub .btn{
        background:var(--g-btn)!important;
        border:1px solid rgba(255,255,255,.28)!important;
        color:#fff!important;
        text-shadow:0 1px 0 rgba(0,0,0,.34);
        box-shadow:
          0 1px 0 rgba(255,255,255,.22) inset,
          0 -2px 0 rgba(0,0,0,.2) inset,
          0 4px 12px rgba(0,0,0,.28),
          0 0 0 1px rgba(255,255,255,.12)!important;
      }
      .grbl-2d-toolbar .grbl-2d-actions .btn:hover,
      .grbl-2d-scrub .btn:hover{
        filter:brightness(1.1);
        transform:translateY(-1px);
        box-shadow:
          0 1px 0 rgba(255,255,255,.26) inset,
          0 -2px 0 rgba(0,0,0,.16) inset,
          0 6px 15px rgba(0,0,0,.34),
          0 0 0 1px rgba(255,255,255,.18)!important;
      }
      .grbl-2d-toolbar .grbl-2d-actions .btn:active,
      .grbl-2d-scrub .btn:active{
        transform:translateY(1px);
        box-shadow:
          0 1px 0 rgba(255,255,255,.14) inset,
          0 -1px 0 rgba(0,0,0,.18) inset,
          0 2px 6px rgba(0,0,0,.22),
          0 0 0 1px rgba(255,255,255,.08)!important;
      }
      .grbl-2d-toolbar .grbl-2d-filter{display:inline-flex;align-items:center;gap:3px;font-size:10px;color:var(--g-txt2);cursor:pointer;white-space:nowrap}
      .grbl-2d-toolbar .grbl-2d-filter input[type=checkbox]{width:11px;height:11px;accent-color:var(--g-acc);cursor:pointer;margin:0}
      .grbl-2d-filter-info{font-size:10px;color:var(--g-txt2);white-space:nowrap;margin-left:2px}
      .grbl-2d-scrub{display:grid;grid-template-columns:auto auto minmax(0,240px) auto auto auto;gap:6px;align-items:center;margin:6px 0 0}
      .grbl-2d-scrub label{display:inline-flex;align-items:center;gap:4px;font-size:10px;color:var(--g-txt2);white-space:nowrap}
      .grbl-2d-scrub input[type='range']{width:100%;height:26px;accent-color:var(--g-acc)}
      .grbl-2d-scrub select{height:28px!important;min-height:28px!important;width:64px!important;padding:2px 6px!important;font-size:10px!important}
      .grbl-2d-scrub .btn{min-height:28px!important;padding:2px 8px!important;font-size:10px!important}
      .grbl-2d-scrub .line-info{font-size:10px;color:var(--g-text);white-space:nowrap;min-width:66px;text-align:right}
      .grbl-2d-wrap{position:relative;border:1px solid var(--g-bdr);border-radius:8px;background:var(--g-bg);overflow:hidden;min-height:320px;box-shadow:inset 0 0 0 1px rgba(255,255,255,.02)}
      .grbl-2d-canvas{display:block;width:100%;height:380px;background:var(--g-bg)}
      .grbl-2d-overlay{position:absolute;left:8px;bottom:8px;font-size:9px;color:var(--g-txt2);background:rgba(8,13,24,.9);border:1px solid var(--g-bdr);padding:3px 6px;border-radius:999px;pointer-events:none}
      .grbl-2d-hud{position:absolute;right:8px;top:8px;font-size:9px;color:var(--g-txt2);background:rgba(8,13,24,.9);border:1px solid var(--g-bdr);padding:3px 6px;border-radius:999px;white-space:nowrap;max-width:calc(100% - 16px);overflow:hidden;text-overflow:ellipsis;pointer-events:none}
      .grbl-2d-legend{position:absolute;right:8px;bottom:8px;display:flex;gap:6px;align-items:center;font-size:9px;color:var(--g-txt2);background:rgba(8,13,24,.9);border:1px solid var(--g-bdr);padding:3px 6px;border-radius:999px;pointer-events:none}
      .grbl-2d-dot{display:inline-block;width:8px;height:8px;border-radius:999px;margin-right:4px}
      .grbl-2d-dot.cut{background:#ef4444}
      .grbl-2d-dot.engrave{background:#22c55e}
      .grbl-2d-dot.rapid{background:#f59e0b}
      .cncjs-jog-fields{display:flex;gap:8px;align-items:end;margin-bottom:8px}
      #wid_jog .cncjs-jog-fields{padding:0 4px;box-sizing:border-box}
      .cncjs-jog-field{display:grid;grid-template-rows:auto 26px;gap:3px;min-width:0;flex:1 1 0}
      .cncjs-jog-field:last-child{flex:1.4 1 0}
      .cncjs-jog-field label{display:block;margin:0;font-size:8px;line-height:1.05;color:var(--g-txt2);font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
      #wid_jog .cncjs-jog-field input{height:26px!important;min-height:26px!important;padding:0 7px!important;font-size:10px!important;line-height:1.05}
      .cncjs-jog-layout{display:grid;grid-template-columns:max-content max-content;gap:6px;align-items:start;justify-content:center}
      #wid_jog .cncjs-jog-layout{padding:0 8px;box-sizing:border-box}
      .cncjs-jog-cross{display:grid;grid-template-columns:repeat(3,50px);grid-template-rows:repeat(3,28px);gap:5px;justify-content:center;align-items:center}
      .cncjs-jog-spacer{display:block;min-height:28px}
      .cncjs-jog-z-stack{display:grid;grid-template-columns:50px;grid-template-rows:repeat(3,28px);gap:5px;justify-content:center;align-items:center}
      .cncjs-jog-z-spacer{min-height:28px}
      #wid_jog .cncjs-jog-cross .btn,
      #wid_jog .cncjs-jog-z-stack .btn,
      #wid_jog .cncjs-quick-grid .btn{min-width:0;width:100%}
      #wid_jog .cncjs-jog-cross .btn,
      #wid_jog .cncjs-jog-z-stack .btn{width:50px!important;min-width:50px!important;max-width:50px!important;height:28px!important;min-height:28px!important;padding:0!important;justify-self:center}
      #wid_jog .cncjs-jog-cross .btn,
      #wid_jog .cncjs-jog-z-stack .btn{
        background:linear-gradient(180deg,var(--g-jog-top) 0%,var(--g-jog-mid) 52%,var(--g-jog-bot) 100%)!important;
        border:1px solid var(--g-jog-border)!important;
        color:#fff!important;
        box-shadow:
          0 1px 0 rgba(255,255,255,.28) inset,
          0 -2px 0 rgba(8,47,73,.22) inset,
          0 3px 10px var(--g-jog-shadow),
          0 2px 0 rgba(8,47,73,.18)!important;
        text-shadow:0 1px 0 rgba(3,7,18,.22);
        transition:transform .14s ease, box-shadow .14s ease, filter .14s ease, border-color .14s ease;
      }
      #wid_jog .cncjs-jog-cross .btn:hover,
      #wid_jog .cncjs-jog-z-stack .btn:hover{
        filter:brightness(1.06) saturate(1.06);
        transform:translateY(-1px);
        box-shadow:
          0 1px 0 rgba(255,255,255,.34) inset,
          0 -2px 0 rgba(8,47,73,.18) inset,
          0 5px 14px rgba(2,132,199,.42),
          0 2px 0 rgba(8,47,73,.14)!important;
      }
      #wid_jog .cncjs-jog-cross .btn:active,
      #wid_jog .cncjs-jog-z-stack .btn:active{
        transform:translateY(1px);
        box-shadow:
          0 1px 0 rgba(255,255,255,.18) inset,
          0 -1px 0 rgba(8,47,73,.20) inset,
          0 1px 4px rgba(2,132,199,.26),
          0 1px 0 rgba(8,47,73,.12)!important;
      }
      #wid_jog .cncjs-jog-cross .cncjs-jog-spacer{width:50px}
      #btn_jog_xy_center{justify-self:center;background:linear-gradient(180deg,#14b8a6 0%,#0f766e 100%)!important;border-color:#9ef0e6!important;color:#fff!important;box-shadow:0 1px 0 rgba(255,255,255,.22) inset,0 -2px 0 rgba(6,78,59,.20) inset,0 3px 10px rgba(13,148,136,.34),0 2px 0 rgba(6,78,59,.18)!important}
      .cncjs-jog-diag{font-size:13px!important;font-weight:900!important}
      #wid_jog .cncjs-jog-cross .btn.cncjs-jog-diag{background:linear-gradient(180deg,#f9fdff 0%,#dff2ff 100%)!important;border:1px solid rgba(30,167,225,.95)!important;color:#fff!important;box-shadow:0 1px 0 rgba(255,255,255,.8) inset,0 -2px 0 rgba(30,167,225,.12) inset,0 3px 9px rgba(30,167,225,.26)!important}
      #wid_jog .cncjs-jog-cross .btn.cncjs-jog-diag:hover{background:linear-gradient(180deg,#ffffff 0%,#e7f6ff 100%)!important}
      .cncjs-jog-home{font-weight:900!important;letter-spacing:.25px}
      .cncjs-jog-step-selectors{display:flex;gap:8px;margin-top:var(--ui-gap);align-items:end}
      .cncjs-jog-step-selectors > div{display:grid;grid-template-rows:auto 26px;row-gap:3px;align-items:start;min-width:0;flex:1 1 0}
      .cncjs-jog-step-selectors label{display:block;font-size:9px!important;line-height:1.05;color:var(--g-txt2);font-weight:700;margin:0;text-transform:none;letter-spacing:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
      #wid_jog .cncjs-jog-step-selectors select{width:100%;height:26px!important;min-height:26px!important;padding:0 7px!important;font-size:10px!important;line-height:1.05}
      .cncjs-step-pills{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:4px;align-items:center}
      .cncjs-step-pills .btn{min-height:34px!important;height:34px!important;padding:0!important;font-size:10px!important;font-weight:800;line-height:1!important;display:flex!important;align-items:center;justify-content:center;text-align:center}
      .cncjs-step-pills .btn.is-active{border-color:var(--g-acc)!important;background:var(--g-acc)!important;color:#080D18!important;box-shadow:0 0 0 1px rgba(0,191,255,.25),0 2px 8px rgba(0,0,0,.25)}
      .cncjs-step-mini{display:grid;grid-template-columns:minmax(0,1fr);gap:var(--ui-gap);align-items:center;margin-top:0}
      .cncjs-step-mini select{width:100%}
      .cncjs-quick-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:var(--ui-gap);margin-top:var(--ui-gap)}
      .cncjs-macro-grid{display:grid;grid-template-columns:1fr;gap:var(--ui-gap)}
      .cncjs-macro-card{border:1px solid var(--g-bdr);border-radius:8px;padding:10px;background:var(--g-panel);display:flex;flex-direction:column;gap:6px}
      .cncjs-macro-card label{font-size:11px;opacity:.82;font-style:italic}
      .cncjs-macro-card select,.cncjs-macro-card input{width:100%;padding:8px;border-radius:6px;border:1px solid var(--g-bdr);background:var(--g-sel);color:var(--g-text);font-size:13px;box-sizing:border-box}
      .cncjs-macro-card .btn{width:100%;margin-top:auto}
      .cncjs-macro-save-row{grid-column:1/-1;display:flex;justify-content:center;padding-top:6px}

  #btn_jog_xy_center{justify-self:center;background:linear-gradient(180deg,var(--g-jog-center-top) 0%,var(--g-jog-center-bot) 100%)!important;border-color:var(--g-jog-center-border)!important;color:#fff!important;box-shadow:0 1px 0 rgba(255,255,255,.22) inset,0 -2px 0 rgba(6,78,59,.20) inset,0 3px 10px var(--g-jog-shadow),0 2px 0 rgba(6,78,59,.18)!important}

  #wid_jog .cncjs-jog-cross .btn.cncjs-jog-diag{background:linear-gradient(180deg,var(--g-jog-diag-top) 0%,var(--g-jog-diag-bot) 100%)!important;border:1px solid var(--g-jog-diag-border)!important;color:#fff!important;box-shadow:0 1px 0 rgba(255,255,255,.8) inset,0 -2px 0 rgba(30,167,225,.12) inset,0 3px 9px rgba(30,167,225,.26)!important}
  #wid_jog .cncjs-jog-cross .btn.cncjs-jog-diag:hover{filter:brightness(1.03) saturate(1.05)}
      .grbl-console-toolbar{display:grid;grid-template-columns:auto minmax(0,1fr) auto;align-items:center;gap:10px;margin-bottom:2px}
      .grbl-console-toolbar .note{min-width:0;font-size:14px;line-height:1.35;text-align:center;color:var(--g-acc);font-weight:600}
      .grbl-console-toolbar .btn{min-width:110px!important;display:inline-flex!important;align-items:center;justify-content:center;padding:2px 10px!important}
      .grbl-console-send-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;margin-top:6px;align-items:center}
      .grbl-console-send-row input{margin:0!important}
      #wid_console .grbl-console-send-row .btn{margin:0!important;align-self:stretch;display:inline-flex!important;align-items:center;justify-content:center;white-space:nowrap}
      .grbl-side-column .btn,
      .grbl-side-column select,
      .grbl-side-column input:not([type='checkbox']):not([type='radio']){font-size:10px!important}
      #wid_connection .btn,
      #wid_console .grbl-console-toolbar .btn,
      #wid_job .btn,
      #wid_frame .btn,
      #wid_jog .btn,
      #wid_poll .btn,
      #wid_macros .btn{min-height:var(--ui-h)!important;line-height:1.05!important;padding:0 7px!important}
      #wid_connection .btn,
      #wid_job .btn,
      #wid_frame .btn,
      #wid_jog .btn,
      #wid_poll .btn,
      #wid_macros .btn{width:100%;min-width:0!important;max-width:100%}
      #wid_console .grbl-console-toolbar .btn{width:auto!important;min-width:0!important}
      #wid_console{display:grid;grid-template-rows:auto auto minmax(0,1fr) auto auto;gap:6px}
      #wid_console #grbl_console{min-height:220px!important;max-height:min(420px,calc(100vh - 320px))!important}
      #usb_slot:disabled{opacity:1!important;color:#e0e7ff!important;background:#1e293b!important}
      .cncjs-widget.console-fullscreen{position:fixed;inset:0;z-index:420;border-radius:0;padding:14px;padding-bottom:calc(14px + env(safe-area-inset-bottom));display:grid;grid-template-rows:auto auto minmax(0,1fr) auto auto;gap:8px}
      .cncjs-widget.console-fullscreen pre{min-height:0!important;max-height:none!important;height:auto!important}
      #grbl_console{font-size:12px!important;line-height:1.4!important}
      .cncjs-widget.console-fullscreen #grbl_console{font-size:13px!important;line-height:1.45!important}

      .grbl-section-block{border:1px solid var(--g-bdr)!important;background:var(--g-panel)!important}
      .grbl-section-title{color:var(--g-acc)!important}
      .grbl-run-focus{border:1px solid var(--g-acc)!important;box-shadow:0 0 0 1px rgba(0,191,255,.1),0 4px 16px rgba(0,0,0,.3)}

      @media (min-width:768px){
        body{display:block!important;padding:0!important}
      }
      @media (max-width: 1280px){
        .grbl-cg{min-width:220px}
        .grbl-cg select,.grbl-cg input{width:150px}
        .cncjs-shell-grid{--grbl-side-col:214px;grid-template-columns:var(--grbl-side-col) minmax(0,1fr) var(--grbl-side-col)}
      }
      @media (max-width: 1040px){
        .grbl-ctrlbar{grid-template-columns:1fr;row-gap:8px}
        .grbl-cg{min-width:220px}
        .grbl-cb-row{justify-content:flex-start}
        .grbl-ctrlbar .grbl-cb-actions{justify-content:flex-start;justify-self:start;min-width:0}
        .grbl-ctrlbar .grbl-cb-actions .btn{min-width:120px}
      }
      @media (max-width: 980px){
        .cncjs-shell-grid{grid-template-columns:1fr;justify-items:center}
        #wid_workspace{margin-top:0!important}
        .cncjs-toolbar{grid-template-columns:1fr 1fr}
        .cncjs-metrics{grid-template-columns:repeat(2,minmax(120px,1fr))}

        .grbl-dro-actions{grid-template-columns:repeat(3,minmax(100px,1fr))}
        .grbl-2d-toolbar{display:flex;align-items:center;gap:5px}
        .grbl-2d-toolbar .grbl-2d-actions{gap:4px;justify-content:flex-start}
        .grbl-2d-toolbar .grbl-2d-actions select,
        .grbl-2d-toolbar .grbl-2d-actions .btn{font-size:10px!important;min-height:28px!important;padding:0 8px!important}
        .cncjs-jog-layout{grid-template-columns:max-content max-content;justify-content:center}
        .grbl-manual-groups{grid-template-columns:1fr}
        .grbl-job-row-3{grid-template-columns:1fr}
        .grbl-job-row-2{grid-template-columns:1fr}
        .grbl-job-actions{grid-template-columns:repeat(2,minmax(120px,1fr))}
        .grbl-frame-params{display:flex;grid-template-columns:none}
        .grbl-sd-row{grid-template-columns:1fr}
        .grbl-right-column{order:1}
        .grbl-left-column{order:2}
        .grbl-center-column{order:3}
      }
      @media (max-width: 640px){
        .grbl-banner{padding:12px 12px 10px;margin:8px 8px 0;border-radius:12px}
        .grbl-banner h1{font-size:18px;letter-spacing:.4px}
        .grbl-banner p{font-size:11px}
        .grbl-ctrlbar{padding:10px;margin:8px}
        .cncjs-shell-grid{width:calc(100% - 16px);margin:8px 8px 0;grid-template-columns:1fr;justify-items:center}
        .grbl-side-column,.grbl-center-column{width:100%;max-width:760px}
        .cncjs-widget{padding:10px;border-radius:10px}
        .cncjs-widget h3{font-size:17px;margin-bottom:8px}
        .grbl-right-column{gap:10px}
        #grbl_right_dock{gap:10px;display:flex;flex-direction:column}
        #grbl_right_dock .cncjs-widget{
          padding:11px 10px 10px;
          margin:0;
          border-radius:11px;
          box-shadow:0 1px 0 rgba(255,255,255,.05) inset,0 3px 11px rgba(0,0,0,.2);
        }
        #grbl_right_dock .cncjs-widget + .cncjs-widget{margin-top:2px}
        #grbl_right_dock .cncjs-widget h3{margin-bottom:10px}
        #grbl_right_dock .grbl-frame-bbox{margin-top:2px;padding-top:6px;border-top:1px dashed rgba(148,163,184,.25)}
        #grbl_right_dock .grbl-job-widget,
        #grbl_right_dock .grbl-frame-grid{gap:9px}
        #wid_jog{order:1}
        #wid_job{order:2}
        #wid_frame{order:3}
        #wid_override{order:4}
        #wid_macros{order:5}
        .grbl-cg{min-width:0;flex:1 1 100%}
        .grbl-cg label{font-size:10px}
        .grbl-cg select,.grbl-cg input{width:100%;height:44px;min-height:44px}
        .grbl-cb-main{display:block!important;overflow:visible;width:100%}
        .grbl-cb-row{display:grid;gap:6px;grid-template-columns:repeat(2,minmax(0,1fr));width:100%;min-width:0;padding:2px 0;border:none;background:transparent;border-radius:0}
        .grbl-cb-main .btn{width:100%!important;min-width:0!important;max-width:none!important;height:44px;min-height:44px!important;flex:0 0 auto}
        .grbl-ctrlbar .grbl-cb-actions{width:100%;justify-content:flex-start}
        .grbl-ctrlbar .grbl-cb-actions .btn{height:44px;min-height:44px!important}
        .grbl-ctrlbar .grbl-cb-actions .btn{width:100%;min-width:0}
        .grbl-chip{min-height:34px;font-size:11px;padding:4px 10px}
        .page.grbl-controller .btn{min-height:48px!important;font-size:15px!important}
        .page.grbl-controller .cncjs-shell-grid input:not([type='checkbox']):not([type='radio']),
        .page.grbl-controller .cncjs-shell-grid select{min-height:48px!important;font-size:15px!important}
        .page.grbl-controller .cncjs-shell-grid textarea{font-size:15px!important;min-height:120px}
        .cncjs-jog-layout{grid-template-columns:max-content max-content;justify-content:center}
        .cncjs-jog-z-stack{grid-template-columns:64px;grid-template-rows:repeat(3,44px)}
        #wid_jog .cncjs-jog-cross{grid-template-columns:repeat(3,64px);grid-template-rows:repeat(3,44px)}
        #wid_jog .cncjs-jog-cross .btn,
        #wid_jog .cncjs-jog-z-stack .btn{width:64px!important;min-width:64px!important;max-width:64px!important;height:44px!important;min-height:44px!important;font-size:16px!important}
        #wid_jog .cncjs-jog-cross .cncjs-jog-spacer{width:auto}
        .cncjs-jog-step-selectors{display:flex;flex-direction:column;gap:8px}
        .cncjs-step-pills{grid-template-columns:repeat(3,minmax(0,1fr));gap:6px}
        #wid_jog .cncjs-jog-step-selectors label{font-size:11px!important;line-height:1.2}
        #wid_jog .cncjs-step-pills .btn,
        #wid_jog .cncjs-step-mini .btn{min-height:40px!important;height:40px!important;font-size:14px!important}
        .cncjs-jog-cross{grid-template-columns:repeat(3,64px);gap:6px}
        .cncjs-jog-cross .btn,.cncjs-jog-z-stack .btn{min-height:44px!important}
        .grbl-2d-toolbar{display:flex;align-items:center;gap:5px}
        .grbl-2d-toolbar h3{white-space:nowrap;font-size:15px;margin-bottom:0}
        .grbl-2d-toolbar .grbl-2d-actions{gap:4px;width:auto;flex-wrap:wrap!important;justify-content:flex-start}
        .grbl-2d-toolbar .grbl-2d-actions select,
        .grbl-2d-toolbar .grbl-2d-actions .btn{font-size:11px!important;min-height:30px!important;padding:0 10px!important}
        .grbl-2d-scrub{grid-template-columns:1fr;gap:8px}
        .grbl-2d-scrub label,.grbl-2d-scrub .line-info{font-size:12px}
        .grbl-2d-scrub .btn{min-height:44px!important;font-size:14px!important}
        .grbl-2d-canvas{height:300px}
        .grbl-console-toolbar{grid-template-columns:auto minmax(0,1fr) auto;gap:8px}
        .grbl-console-toolbar .note{font-size:12px}
        .grbl-console-toolbar .btn{min-width:96px!important}
        .grbl-manual-group .title{font-size:10px}
        .grbl-manual-grid .btn,
        .grbl-job-actions .btn,
        .cncjs-quick-grid .btn,
        .grbl-frame-actions .btn,
        .grbl-sd-actions .btn{width:100%!important;min-width:0}
        .grbl-job-actions{grid-template-columns:1fr 1fr;gap:8px}
        .grbl-sd-actions{display:grid;grid-template-columns:1fr 1fr;width:100%}
        .grbl-sd-actions .btn{min-width:0}
        .grbl-sd-browser{min-height:190px;max-height:320px}
        .grbl-sd-row{padding:8px}
        .grbl-sd-name{font-size:13px}
        .grbl-sd-path{font-size:11px}

        .grbl-dro-actions{grid-template-columns:1fr 1fr 1fr}
        .cncjs-metrics{grid-template-columns:repeat(2,minmax(0,1fr));gap:6px}
        .cncjs-metric strong{font-size:11px}
        .cncjs-metric div{font-size:12px}
        #wid_connection .grbl-conn-metrics .cncjs-metric strong{font-size:9px!important}
        #wid_connection .grbl-conn-metrics .cncjs-metric div{font-size:10px!important}
        #grbl_console{font-size:12px!important;line-height:1.4!important}
        .grbl-manual-input{grid-template-columns:1fr}
        .grbl-manual-input .btn{width:100%!important;min-width:0}
      }
      @media (max-width: 768px){
        .cncjs-shell-grid{grid-template-columns:1fr;justify-items:stretch!important}
        .grbl-side-column,.grbl-center-column,.grbl-widget-dock,.cncjs-widget,.grbl-section-block{width:100%!important;max-width:100%;min-width:0;box-sizing:border-box}
        .grbl-left-column,.grbl-right-column,.grbl-center-column{align-self:stretch}
        .grbl-cb-main{width:100%!important;max-width:100%}
        .grbl-2d-toolbar{flex-wrap:wrap;gap:6px}
        .grbl-frame-params{flex-wrap:wrap}
        .grbl-frame-field{flex:1 1 120px;max-width:100%}
        .cncjs-jog-layout{max-width:100%;justify-content:center}
        #grbl_left_panel{width:min(300px,85vw)}
      }
      @media (max-width: 480px){
        .grbl-banner{padding:11px 10px 9px}
        .grbl-banner h1{font-size:17px}
        .grbl-banner p{font-size:10px}
        .grbl-ctrlbar{padding:8px}
        .cncjs-shell-grid{width:calc(100% - 12px);margin:6px 6px 0}
        .cncjs-widget{padding:9px}
        .cncjs-widget h3{font-size:16px}
        .grbl-right-column{gap:8px}
        #grbl_right_dock{gap:8px}
        #grbl_right_dock .cncjs-widget{padding:10px 9px 9px;border-radius:10px}
        #grbl_right_dock .cncjs-widget h3{margin-bottom:9px}
        #grbl_right_dock .grbl-job-widget,
        #grbl_right_dock .grbl-frame-grid{gap:8px}
        .grbl-chip{font-size:10px}
        .page.grbl-controller .btn{font-size:14px!important;min-height:48px!important}
        .page.grbl-controller .cncjs-shell-grid input:not([type='checkbox']):not([type='radio']),
        .page.grbl-controller .cncjs-shell-grid select,
        .page.grbl-controller .cncjs-shell-grid textarea{font-size:14px!important;min-height:48px!important}
        .grbl-console-toolbar{grid-template-columns:1fr;gap:8px}
        .grbl-console-toolbar .note{order:1;font-size:13px}
        #btn_console_clear{order:2}
        #btn_console_fullscreen{order:3}
        .grbl-console-toolbar .btn{width:100%!important;min-width:0!important}
        .grbl-job-actions{grid-template-columns:1fr}
        .grbl-sd-actions{grid-template-columns:1fr}
        #wid_console #grbl_console{min-height:180px!important;max-height:min(52vh,360px)!important}
        .grbl-2d-overlay,.grbl-2d-legend,.grbl-2d-hud{font-size:10px;padding:3px 7px}
        .grbl-2d-canvas{height:270px}
        #wid_jog .cncjs-jog-cross{grid-template-columns:repeat(3,56px)}
        #wid_jog .cncjs-jog-cross .btn,
        #wid_jog .cncjs-jog-z-stack .btn{width:56px!important;min-width:56px!important;max-width:56px!important}
        .cncjs-jog-z-stack{grid-template-columns:56px}
        .grbl-console-send-row{grid-template-columns:1fr}
        #wid_console .grbl-console-send-row .btn{width:100%!important;min-width:0!important}
        .grbl-2d-toolbar{flex-direction:column;align-items:stretch}
        .grbl-2d-toolbar h3{text-align:left;margin-bottom:0}
        .grbl-2d-toolbar .grbl-2d-actions{width:100%!important;flex-wrap:wrap!important;justify-content:flex-start}
        .grbl-2d-toolbar .grbl-2d-actions select{width:100%!important;max-width:100%!important}
        .grbl-2d-scrub{grid-template-columns:1fr 1fr;gap:6px}
        .grbl-2d-scrub .btn{width:100%!important;min-width:0!important}
        .grbl-2d-scrub input[type='range']{grid-column:1 / -1}
        .grbl-2d-scrub select{width:100%!important;justify-self:stretch}
        .grbl-2d-scrub .line-info{text-align:left;min-width:0}
      }
      @media (orientation: landscape) and (max-height: 520px){
        .grbl-rotate-warning{display:flex!important}
        .grbl-controller-content{display:none!important}
      }
    </style>
)GRBL"

