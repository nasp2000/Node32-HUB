#include "page_camera_asset.h"

// Layout concept inspired by s60sc/ESP32-CAM_MJPEG2SD (icon rail + titled
// panels + status footer). All CSS/HTML/JS below is original code written for
// Node32-HUB — nothing is copied from that project (AGPL-3.0, see
// docs/camera.md §12.5). Our blue palette, our endpoints, our IDs.

const char CAMERA_PAGE[] PROGMEM = R"HTML(
<style>
:root{
  --bg-0:#1a3a6e;
  --bg-1:#22488a;
  --bg-2:#2f5da8;
  --p:#0a0c11;
  --bdr:rgba(180,215,255,.20);
  --bdr2:rgba(180,215,255,.34);
  --bdr3:rgba(200,228,255,.55);
  --tx:#f2f7ff;
  --tx2:#c3d6f5;
  --tx3:#8ba7d4;
  --acc:#2f7bff;
  --acc2:#5aa0ff;
  --acc-dim:rgba(47,123,255,.16);
  --acc-glow:rgba(47,123,255,.45);
  --red:#ef4444;
  --red2:#f87171;
  --red-dim:rgba(239,68,68,.12);
  --red-glow:rgba(239,68,68,.45);
  --green:#22c55e;
  --green-glow:rgba(34,197,94,.45);
  --orange:#f59e0b;
  --r:8px;
  --fs:10px;
  --shadow:0 8px 32px rgba(0,0,0,.5);
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;width:100%}
body{
  background:linear-gradient(180deg, var(--bg-0) 0%, var(--bg-1) 100%);
  color:var(--tx);
  font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
  overflow:hidden;
  height:100dvh;
  font-size:var(--fs);
  -webkit-font-smoothing:antialiased;
  -moz-osx-font-smoothing:grayscale;
}

/* ===== Main wrapper (column: toolbar + main + statusbar) ===== */
.wrap{
  display:flex;
  flex-direction:column;
  height:100dvh;
  max-width:none!important;
  width:100%!important;
  padding:0!important;
  margin:0!important;
  position:relative;
}

/* ===== Slim toolbar (single row of equal buttons, centered) ===== */
.top{
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:6px;
  padding:2px 6px;
  min-height:26px;
  background:linear-gradient(180deg, rgba(47,93,168,.95) 0%, rgba(34,72,138,.93) 100%);
  border-bottom:1px solid rgba(4,10,24,.8);
  box-shadow:inset 0 1px 0 rgba(255,255,255,.28), 0 3px 6px rgba(0,0,0,.5);
  flex-shrink:0;
  z-index:5;
}
.tgroup{display:flex;align-items:center;gap:6px;}
.dd-w{position:relative;}
.dd-m{display:none;position:absolute;top:100%;right:0;margin-top:4px;background:rgba(4,10,24,.92);border:1px solid var(--bdr2);border-radius:6px;min-width:150px;z-index:20;box-shadow:0 8px 24px rgba(0,0,0,.6);overflow:hidden;}
.dd-m.show{display:block;}
.dd-i{padding:6px 12px;font-size:11px;color:var(--tx);cursor:pointer;white-space:nowrap;display:flex;align-items:center;gap:6px;}
.dd-i:hover{background:rgba(59,130,246,.2);}
.st.mini{font-size:10px;padding:2px 8px;min-height:20px;}
.top .b{flex:0 0 auto}
.b{
  display:inline-flex;
  align-items:center;
  justify-content:center;
  gap:3px;
  background:linear-gradient(180deg, rgba(140,190,255,.30) 0%, rgba(90,160,255,.14) 100%);
  color:var(--tx);
  border:1px outset var(--bdr3);
  border-radius:3px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.30), 0 2px 3px rgba(0,0,0,.45);
  padding:2px 8px;
  font-size:11px;
  font-weight:600;
  cursor:pointer;
  text-decoration:none;
  white-space:nowrap;
  line-height:1;
  transition:all .15s;
  height:22px;
  width:84px;
  max-width:84px;
  overflow:hidden;
  font-family:inherit;
  margin:0;
}
.b:hover{background:linear-gradient(180deg, rgba(140,190,255,.42) 0%, rgba(90,160,255,.22) 100%);border-color:var(--bdr3)}
.b:active{transform:translateY(1px);box-shadow:inset 0 2px 3px rgba(0,0,0,.4)}
.b-rec{background:linear-gradient(180deg, #f87171 0%, var(--red) 100%);color:#fff;border:1px outset #fca5a5;box-shadow:inset 0 1px 0 rgba(255,255,255,.35), 0 2px 3px rgba(0,0,0,.45)}
.b-green{background:linear-gradient(180deg, #4ade80 0%, #16a34a 100%);color:#fff;border:1px outset #86efac;box-shadow:inset 0 1px 0 rgba(255,255,255,.35), 0 2px 3px rgba(0,0,0,.45)}
.b-icon{padding:1px;width:14px;border-radius:2px;font-size:8px}
.st{
  font-size:11px;
  color:var(--tx2);
  display:flex;
  align-items:center;
  gap:5px;
  padding:3px 10px;
  min-height:22px;
  background:rgba(8,18,38,.35);
  border:1px solid var(--bdr2);
  border-radius:3px;
  font-variant-numeric:tabular-nums;
  font-weight:500;
  white-space:nowrap;
}
.st .dot{width:8px;height:8px;border-radius:50%;display:inline-block;flex-shrink:0}
.dot-g{background:var(--green);box-shadow:0 0 8px var(--green-glow);animation:pulse 2s ease-in-out infinite}
.dot-r{background:var(--red);box-shadow:0 0 6px rgba(239,68,68,.4)}
@keyframes pulse{0%,100%{opacity:1;transform:scale(1)}50%{opacity:.6;transform:scale(.85)}}

/* ===== Main split: sidebar LEFT + stream CENTER ===== */
.main{
  flex:1;
  display:flex;
  flex-direction:row;
  min-height:0;
  overflow:hidden;
}
.side{
  width:320px;
  flex-shrink:0;
  order:-1;
  display:flex;
  flex-direction:row;
  min-height:0;
  background:linear-gradient(0deg, rgba(26,58,110,.98) 0%, rgba(34,72,138,.96) 100%);
  border-right:1px solid rgba(4,10,24,.8);
  box-shadow:inset -1px 0 0 rgba(200,228,255,.22), inset 0 1px 0 rgba(255,255,255,.14);
  overflow:hidden;
}

/* ===== Icon rail (panel switcher, centered, no horizontal scroll) ===== */
.rail{
  width:52px;
  flex-shrink:0;
  display:flex;
  flex-direction:column;
  align-items:stretch;
  justify-content:flex-start;
  gap:6px;
  padding:4px 0;
  background:rgba(8,18,38,.35);
  border-right:1px solid rgba(4,10,24,.7);
  box-shadow:inset -1px 0 0 rgba(200,228,255,.18);
  overflow-x:hidden;
  overflow-y:auto;
}
.rail button{
  flex:1 1 0;
  display:flex;
  flex-direction:column;
  align-items:center;
  justify-content:center;
  gap:10px;
  padding:3px 0;
  margin:0;
  width:100%;
  border-radius:10px;
  border:1px solid rgba(200,228,255,.55);
  box-shadow:
    inset 0 2px 0 rgba(255,255,255,.35),
    inset 0 -2px 0 rgba(0,0,0,.40),
    inset 2px 0 0 rgba(255,255,255,.12),
    inset -2px 0 0 rgba(0,0,0,.28),
    0 3px 4px rgba(0,0,0,.50);
  background:linear-gradient(180deg, rgba(140,190,255,.22) 0%, rgba(90,160,255,.08) 100%);
  color:var(--tx2);
  font-size:11px;
  cursor:pointer;
  font-family:inherit;
  transition:all .15s;
  min-height:42px;
}
.rail button .ra-ic{font-size:26px;line-height:1}
.rail button .ra-tx{writing-mode:vertical-rl;text-orientation:upright;font-size:9px;font-weight:700;letter-spacing:1px;text-transform:none;white-space:nowrap}
.rail button:hover{color:var(--tx)}
.rail button:active{transform:translateY(1px)}
.rail button.on{
  background:linear-gradient(180deg, var(--acc2) 0%, var(--acc) 100%);
  color:#fff;
  border:1px solid #93c5fd;
  box-shadow:
    inset 0 2px 0 rgba(255,255,255,.45),
    inset 0 -2px 0 rgba(0,0,0,.35),
    inset 2px 0 0 rgba(255,255,255,.18),
    inset -2px 0 0 rgba(0,0,0,.25),
    0 2px 6px var(--acc-glow);
}
.panelwrap{
  flex:1;
  min-width:0;
  overflow-y:auto;
  padding:8px 10px 12px;
  scrollbar-width:thin;
  scrollbar-color:var(--bdr2) transparent;
}
.panelwrap::-webkit-scrollbar{width:4px}
.panelwrap::-webkit-scrollbar-thumb{background:var(--bdr2);border-radius:2px}

/* ===== Panel header (full section name, always visible) ===== */
.phead{
  position:sticky;
  top:-8px;
  z-index:2;
  display:flex;
  align-items:center;
  gap:5px;
  padding:5px 2px 4px;
  margin-bottom:4px;
  background:linear-gradient(0deg, rgba(26,58,110,0) 0%, rgba(26,58,110,.96) 30%);
  background-color:rgba(26,58,110,.92);
  border-bottom:1px solid var(--bdr);
  font-size:11px;
  font-weight:700;
  letter-spacing:.3px;
  color:var(--tx);
  white-space:nowrap;
  overflow:hidden;
}
.psec{display:none;animation:fadeIn .2s ease}
.psec.on{
  display:block;
  background:rgba(8,18,38,.28);
  border:1px solid var(--bdr);
  border-radius:4px;
  box-shadow:inset 0 2px 6px rgba(0,0,0,.45), inset 0 -1px 0 rgba(200,228,255,.12);
  padding:4px 5px 6px;
}
@keyframes fadeIn{from{opacity:0;transform:translateY(4px)}to{opacity:1;transform:translateY(0)}}

/* ===== Stream area (hero, center) ===== */
.str{
  flex:1;
  min-width:0;
  background:
    radial-gradient(ellipse at 50% 30%, rgba(47,123,255,.10) 0%, transparent 60%),
    linear-gradient(180deg, #060d1d 0%, #04070f 100%);
  display:flex;
  align-items:center;
  justify-content:center;
  position:relative;
  overflow:hidden;
  border-left:1px solid rgba(4,10,24,.8);
  box-shadow:inset 2px 0 6px rgba(0,0,0,.55), inset 0 0 40px rgba(0,0,0,.5);
}
.str::before{
  content:'';
  position:absolute;
  inset:0;
  background-image:
    linear-gradient(rgba(147,197,253,.03) 1px, transparent 1px),
    linear-gradient(90deg, rgba(147,197,253,.03) 1px, transparent 1px);
  background-size:24px 24px;
  pointer-events:none;
  opacity:.5;
}
.str img{
  max-width:100%;
  max-height:100%;
  object-fit:contain;
  display:block;
  position:relative;
  z-index:1;
  border-radius:4px;
  box-shadow:0 12px 48px rgba(0,0,0,.6);
  border:1px solid var(--bdr);
}
.str .ph{
  color:var(--tx3);
  font-size:14px;
  text-align:center;
  padding:24px;
  position:relative;
  z-index:1;
  display:flex;
  flex-direction:column;
  align-items:center;
  gap:14px;
}
.str .ph .ph-icon{font-size:36px;opacity:.4;animation:pulse 2.5s ease-in-out infinite}
.str .ph .ph-sub{font-size:11px;color:var(--tx3);opacity:.9;max-width:420px}

/* ===== Floating REC / LIVE badges ===== */
.str .rec-badge{
  position:absolute;
  top:12px;left:12px;
  display:none;
  align-items:center;
  gap:6px;
  padding:4px 10px;
  background:rgba(239,68,68,.95);
  border-radius:12px;
  color:#fff;
  font-size:9px;
  font-weight:700;
  letter-spacing:.5px;
  text-transform:uppercase;
  z-index:2;
  box-shadow:0 4px 16px var(--red-glow);
}
.str .rec-badge.on{display:inline-flex}
.str .rec-badge .rec-dot{width:6px;height:6px;background:#fff;border-radius:50%;animation:pulse 1s ease-in-out infinite}

/* ===== Preset buttons (3+2 grid, full text, 3D relief) ===== */
.btns{display:grid;grid-template-columns:repeat(3,1fr);gap:3px;margin-bottom:6px}
.bp{
  padding:3px 2px;
  border-radius:3px;
  border:1px outset var(--bdr3);
  box-shadow:inset 0 1px 0 rgba(255,255,255,.25), 0 2px 3px rgba(0,0,0,.45);
  background:linear-gradient(180deg, rgba(140,190,255,.22) 0%, rgba(90,160,255,.08) 100%);
  color:var(--tx2);
  font-size:8px;
  font-weight:700;
  cursor:pointer;
  font-family:inherit;
  letter-spacing:.2px;
  transition:all .15s;
  min-width:0;
  overflow:visible;
  white-space:nowrap;
}
.bp:hover{background:linear-gradient(180deg, rgba(140,190,255,.34) 0%, rgba(90,160,255,.16) 100%);color:var(--tx)}
.bp.on{background:linear-gradient(180deg, var(--acc2) 0%, var(--acc) 100%);color:#fff;border:1px outset #93c5fd;box-shadow:inset 0 1px 0 rgba(255,255,255,.35), 0 2px 6px var(--acc-glow)}
.bp:active{transform:translateY(1px)}

/* ===== Control rows: label left (40%), control right ===== */
.rw{
  display:flex;
  align-items:center;
  gap:4px;
  margin-bottom:3px;
  padding:2px 4px;
  background:rgba(8,18,38,.22);
  border:1px outset var(--bdr2);
  border-radius:4px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.12), 0 1px 2px rgba(0,0,0,.4);
  transition:border-color .15s;
}
.rw:hover{border-color:var(--bdr2)}
.rw label{font-size:8px;color:var(--tx2);min-width:40%;flex-shrink:0;letter-spacing:.2px;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.rw select{
  flex:1;
  background:var(--bg-2);
  color:var(--tx);
  border:1px solid var(--bdr2);
  border-radius:4px;
  padding:1px 3px;
  font-size:9px;
  height:18px;
  cursor:pointer;
  font-family:inherit;
  outline:none;
  min-width:0;
}
.rw select:focus{border-color:var(--acc);box-shadow:0 0 0 2px var(--acc-dim)}
.rw input[type=range]{
  flex:1;
  min-width:24px;
  height:4px;
  -webkit-appearance:none;
  appearance:none;
  outline:none;
  background:linear-gradient(90deg, var(--acc) 0%, var(--acc) var(--p,50%), var(--bdr2) var(--p,50%), var(--bdr2) 100%);
  border-radius:3px;
  cursor:pointer;
  margin:0;
}
.rw input[type=range]::-webkit-slider-thumb{
  -webkit-appearance:none;
  width:10px;height:10px;
  border-radius:50%;
  background:linear-gradient(135deg, #fff 0%, #cfe2ff 100%);
  cursor:pointer;
  border:2px solid var(--acc);
  box-shadow:0 2px 8px rgba(0,0,0,.3), 0 0 8px var(--acc-glow);
}
.rw input[type=range]::-moz-range-thumb{width:10px;height:10px;border-radius:50%;background:#fff;cursor:pointer;border:2px solid var(--acc)}
.rw .v{
  font-size:9px;
  color:var(--tx);
  min-width:24px;
  text-align:right;
  font-variant-numeric:tabular-nums;
  font-weight:600;
  background:var(--bg-2);
  padding:1px 5px;
  border-radius:4px;
  border:1px solid var(--bdr);
  flex-shrink:0;
}

/* ===== Checkboxes ===== */
.ck{
  display:flex;
  align-items:center;
  gap:4px;
  margin-bottom:3px;
  padding:2px 4px;
  background:rgba(8,18,38,.22);
  border:1px outset var(--bdr2);
  border-radius:4px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.12), 0 1px 2px rgba(0,0,0,.4);
  cursor:pointer;
  transition:all .15s;
}
.ck:hover{background:rgba(90,160,255,.10);border-color:var(--bdr2)}
.ck input{
  appearance:none;
  -webkit-appearance:none;
  width:11px;height:11px;
  border:1px outset var(--bdr3);
  border-radius:2px;
  background:linear-gradient(180deg, rgba(140,190,255,.25) 0%, rgba(90,160,255,.08) 100%);
  box-shadow:inset 0 1px 0 rgba(255,255,255,.25), 0 1px 2px rgba(0,0,0,.4);
  cursor:pointer;
  position:relative;
  margin:0;
  flex-shrink:0;
  transition:all .15s;
}
.ck input:checked{background:linear-gradient(135deg, var(--acc) 0%, var(--acc2) 100%);border-color:var(--acc)}
.ck input:checked::after{
  content:'';
  position:absolute;
  left:50%;top:50%;
  width:3px;height:6px;
  margin-left:-2px;margin-top:-4px;
  border:solid #fff;
  border-width:0 2px 2px 0;
  transform:rotate(45deg) translate(0,-1px);
}
.ck label{font-size:9px;color:var(--tx);cursor:pointer;flex:1;user-select:none;font-weight:500;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}

/* ===== Section labels ===== */
.sec-lbl{
  font-size:7px;
  font-weight:700;
  color:var(--tx3);
  margin:5px 0 3px;
  padding:3px 0 2px;
  border-top:1px solid var(--bdr);
  text-transform:uppercase;
  letter-spacing:1px;
  display:flex;
  align-items:center;
  gap:6px;
}
.sec-lbl::before{content:'';width:3px;height:10px;background:linear-gradient(180deg, var(--acc) 0%, var(--acc2) 100%);border-radius:1px}
.ri{
  font-size:9px;
  color:var(--tx);
  font-family:'SF Mono',Menlo,Consolas,monospace;
  margin:3px 0;
  padding:3px 6px;
  background:rgba(8,18,38,.35);
  border:1px inset var(--bdr2);
  border-radius:4px;
  box-shadow:inset 0 2px 4px rgba(0,0,0,.45);
  text-align:center;
  font-weight:600;
  letter-spacing:.3px;
  font-variant-numeric:tabular-nums;
  overflow-wrap:anywhere;
}

/* ===== 2-column grid / action row ===== */
.g2{display:grid;grid-template-columns:1fr 1fr;gap:4px}
.act-row{display:flex;gap:4px;margin-bottom:5px}
.act-row .b{flex:1;justify-content:center;max-width:none}
.rw .score-pill{
  font-size:9px;
  color:var(--orange);
  font-weight:700;
  background:var(--bg-2);
  border:1px solid var(--bdr2);
  padding:3px 10px;
  border-radius:10px;
  font-variant-numeric:tabular-nums;
  min-width:48px;
  text-align:center;
}

/* ===== Bottom status bar (model · SD · REC · motion) ===== */
.statusbar{
  display:flex;
  align-items:center;
  gap:6px;
  padding:2px 8px;
  min-height:20px;
  background:linear-gradient(0deg, rgba(47,93,168,.95) 0%, rgba(34,72,138,.93) 100%);
  border-top:1px solid rgba(4,10,24,.8);
  box-shadow:inset 0 1px 0 rgba(255,255,255,.22), 0 -3px 6px rgba(0,0,0,.45);
  font-size:8px;
  color:var(--tx2);
  font-variant-numeric:tabular-nums;
  flex-shrink:0;
  overflow:hidden;
  white-space:nowrap;
}
.statusbar span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.statusbar .fmodel{color:var(--tx);font-weight:700}
.statusbar .frec{color:var(--red2);font-weight:700}
.statusbar .fsep{color:var(--tx3)}

/* ===== Mobile: stack stream on top, controls below, rail horizontal ===== */
@media(max-width:1023px){
  .main{flex-direction:column}
  .str{min-height:32vh;flex-shrink:0;border-left:none;border-top:1px solid var(--bdr)}
  .side{
    width:100%;
    order:0;
    border-right:none;
    border-top:1px solid var(--bdr2);
    flex:1;
    min-height:0;
    flex-direction:column;
  }
  .rail{
    width:100%;
    flex-direction:row;
    border-right:none;
    border-bottom:1px solid var(--bdr);
    padding:5px 6px;
  }
  .rail button{flex:1;min-height:38px;gap:4px}
  .rail button .ra-ic{font-size:30px}
  .rail button .ra-tx{display:none}
  .g2{grid-template-columns:1fr}
  .statusbar{font-size:8px}
}
@media(max-width:480px){
  .top{padding:1px 3px;gap:4px}
  .b{padding:1px 5px;font-size:10px;height:20px;width:72px;max-width:72px}
  .b-icon{width:12px;font-size:7px}
  .st{font-size:10px;padding:2px 6px}
  .statusbar{gap:4px;padding:2px 5px;font-size:8px}
}

/* ===== core.css guard: the shell stylesheet ships
   ".btn,button,a.btn{...border-radius:9999px!important;padding:8px 24px!important;
   font-size:16px;min-height:40px;width:100%;max-width:420px;display:block;margin:6px auto...}"
   plus "input,select{...font-size:13px!important;min-height:28px}" — without
   these scoped overrides every camera button renders giant (the huge Stop in
   the screenshot) and every control 28px tall. Scoped !important wins here. */
.top .b{border-radius:3px!important;padding:2px 8px!important;font-size:11px!important;min-height:0!important;height:22px!important;width:84px!important;max-width:84px!important;display:inline-flex!important;margin:0!important;line-height:1!important;text-align:center!important;}
.act-row .b{border-radius:3px!important;padding:2px 8px!important;font-size:11px!important;min-height:0!important;height:22px!important;display:inline-flex!important;margin:0!important;line-height:1!important;}
.statusbar .b{border-radius:3px!important;padding:1px 4px!important;min-height:0!important;margin:0!important;}
.rail button{border-radius:10px!important;padding:3px 0!important;font-size:11px!important;min-height:0!important;margin:0!important;width:100%!important;flex:1 1 0!important;line-height:1.2!important;}
.btns .bp{border-radius:3px!important;padding:4px 2px!important;font-size:9px!important;min-height:0!important;height:22px!important;margin:0!important;line-height:1!important;}
.wrap select{font-size:9px!important;min-height:0!important;}
.wrap input[type=range]{min-height:0!important;}
.wrap .ck input{min-width:0!important;min-height:0!important;width:11px!important;}

/* ===== Instant tooltips: styled, no delay, fed by the native title attr.
   Covers toolbar / action-row / preset buttons (rail excluded: tooltip on a
   full-height tab would sit far off-centre; the rail buttons already carry
   emoji + rotated full name). */
.top .b,.act-row .b,.btns .bp{position:relative;}
.top .b:hover::after,.act-row .b:hover::after,.btns .bp:hover::after{
  content:attr(title);
  position:absolute;top:calc(100% + 6px);left:50%;transform:translateX(-50%);
  max-width:190px;white-space:normal;text-align:center;
  background:rgba(4,10,24,.97);color:var(--tx);
  border:1px solid var(--bdr3);border-radius:4px;
  padding:4px 8px;font-size:9px;font-weight:500;line-height:1.4;
  z-index:60;pointer-events:none;box-shadow:0 4px 12px rgba(0,0,0,.5);
}
.hint{
  font-size:9px;color:var(--tx2);line-height:1.5;
  padding:4px 6px;margin:2px 0 6px;
  background:rgba(90,160,255,.06);border:1px solid var(--bdr);border-radius:4px;
}
</style>

<div class='wrap'>

<div class='top'>
  <div class='tgroup'>
    <span class='st mini' id='heapPill' title='Free heap' data-i18n-title='camTipHeap'>&#129504; --</span>
    <span class='st mini' id='tempPill' title='Board temperature' data-i18n-title='camTipTemp'>&#127777;&#65039; --</span>
  </div>
  <div class='tgroup'>
  <button class='b b-green' id='bToggle' onclick='camToggle()' title='Start/stop camera' data-i18n-title='camTipStart'>&#9654; <span data-i18n='camStart'>Start</span></button>
  <button class='b' id='bSnap' onclick='snapPhoto()' title='Capture photo' data-i18n-title='camTipCapture'>&#128247; <span data-i18n='camPhoto'>Photo</span></button>
  <button class='b b-rec' id='bRecS' style='display:none' onclick='recStart()' title='Start recording' data-i18n-title='camTipRecord'>&#9679; <span data-i18n='camRec'>Rec</span></button>
  <a href='/hub' class='b' title='Camera hub' data-i18n-title='camTipHub'>&#128246; <span data-i18n='camHub'>Hub</span></a>
  <a href='/cam/raw' target='_blank' class='b' title='Raw fullscreen stream (no UI)' data-i18n-title='camTipRaw'>&#128250; <span data-i18n='camRaw'>Raw</span></a>
  <div class='dd-w' id='linkDd'>
    <button class='b' onclick='toggleLinkDd()' title='Copy stream URL for external players' data-i18n-title='camTipLink'>&#128279; <span data-i18n='camLink'>Link</span></button>
    <div class='dd-m' id='linkMenu'>
      <div class='dd-i' onclick="copyLink('stream')">&#127909; <span data-i18n='camLinkStream'>MJPEG Stream</span></div>
      <div class='dd-i' onclick="copyLink('snapshot')">&#128247; <span data-i18n='camLinkSnap'>Snapshot</span></div>
      <div class='dd-i' onclick="copyLink('raw')">&#128250; <span data-i18n='camLinkRaw'>Raw (no UI)</span></div>
    </div>
  </div>
  </div>
  <div class='st'>
    <span class='dot dot-r' id='stDot'></span>
    <span id='stInfo'></span>
  </div>
</div>

<div class='main'>
  <div class='side'>
    <div class='rail'>
      <button class='on' onclick='showPsec(this,"cam")' title='Camera control' data-i18n-title='camTipTabCam'><span class='ra-ic'>&#127916;</span><span class='ra-tx'>Camera</span></button>
      <button onclick='showPsec(this,"pic")' title='Picture settings' data-i18n-title='camTipTabPic'><span class='ra-ic'>&#127912;</span><span class='ra-tx'>Pictures</span></button>
      <button onclick='showPsec(this,"rec")' title='Record and files' data-i18n-title='camTipTabRec'><span class='ra-ic'>&#128249;</span><span class='ra-tx'>Recording</span></button>
      <button id='railMot' onclick='showPsec(this,"mot")' title='Motion detect' data-i18n-title='camTipTabMot'><span class='ra-ic'>&#128200;</span><span class='ra-tx'>Motion</span></button>
      <button id='railVis' style='display:none' onclick='showPsec(this,"vis")' title='Vision (P4 only)' data-i18n-title='camTipTabVis'><span class='ra-ic'>&#129504;</span><span class='ra-tx'>Vision</span></button>
    </div>
    <div class='panelwrap'>

    <div class='psec on' id='psecCam'>
      <div class='phead'><span>&#127916;</span><span data-i18n='camPanelCam'>Camera Control</span></div>
      <div class='btns'>
        <button class='bp' id='bPrHd' onclick='preset("hd",this)' title='HD preset (1280x720)' data-i18n-title='camTipPresetHd'>HD</button>
        <button class='bp' id='bPrVga' onclick='preset("vga",this)' title='VGA preset (640x480)' data-i18n-title='camTipPresetVga'>VGA</button>
        <button class='bp' id='bPrFast' onclick='preset("fast",this)' title='Fast preset (low latency)' data-i18n-title='camTipPresetFast'>Fast</button>
        <button class='bp' id='bPrNight' onclick='preset("night",this)' title='Night preset (enhanced)' data-i18n-title='camTipPresetNight'>Night</button>
        <button class='bp' id='bPrSafe' onclick='preset("safe",this)' title='Safe preset (tested stable on this hardware)' data-i18n-title='camTipPresetSafe'>Safe</button>
        <button class='bp' id='bPrEco' onclick='preset("eco",this)' title='Economy preset (QVGA, 8 fps, high compression — lowest CPU/bandwidth)' data-i18n-title='camTipPresetEco'>&#127811; Eco</button>
      </div>
      <div class='rw'><label>Resolution</label><select id='selFr' onchange='cfg()' title='Camera resolution' data-i18n-title='camTipResolution'><option value='6'>320x240</option><option value='10'>640x480</option><option value='11'>800x600</option><option value='12'>1024x768</option><option value='13'>1280x720</option><option value='14'>1280x1024</option><option value='15'>1600x1200</option><option value='16'>1920x1080</option><option value='19'>2048x1536</option><option value='24'>2592x1944</option></select></div>
      <div class='rw'><label>Quality</label><input type='range' id='slQ' min='4' max='63' value='20' oninput='cfg()' title='Image quality (lower=better)' data-i18n-title='camTipQuality'><span class='v' id='vQ'>20</span></div>
      <div class='rw'><label>FPS</label><input type='range' id='selFps' min='1' max='30' value='12' oninput='cfg()' title='Frames per second' data-i18n-title='camTipFps'><span class='v' id='vFps'>12</span></div>
      <div class='rw'><label>Gain ceiling</label><select id='slGc' onchange='cfg()' title='AGC gain ceiling' data-i18n-title='camTipGainCeil'><option value='0'>2x</option><option value='1'>4x</option><option value='2'>8x</option><option value='3'>16x</option><option value='4'>32x</option><option value='5'>64x</option><option value='6'>128x</option></select></div>
      <div class='rw'><label>Special FX</label><select id='selSfx' onchange='cfg()' title='Special effect filter' data-i18n-title='camTipFx'><option value='0'>None</option><option value='1'>Neg</option><option value='2'>Gray</option><option value='3'>Red</option><option value='4'>Green</option><option value='5'>Blue</option><option value='6'>Sepia</option></select></div>
      <div class='rw'><label>White balance</label><select id='selWb' onchange='cfg()' title='White balance mode' data-i18n-title='camTipWb'><option value='0'>Auto</option><option value='1'>Sun</option><option value='2'>Cloud</option><option value='3'>Off</option><option value='4'>Home</option></select></div>
      <div class='rw' id='rwSrc' style='display:none'><label>Source</label><select id='selSrc' onchange='srcCfg()' title='Capture source (P4: CSI or USB camera)' data-i18n-title='camTipSource'><option value='0'>Native</option><option value='1'>USB camera</option></select></div>
      <div class='rw' id='rwLamp'><label>Lamp</label><input type='range' id='slLamp' min='0' max='15' value='0' oninput='lampCfg()' title='Onboard lamp brightness' data-i18n-title='camTipLamp'><span class='v' id='vLamp'>0</span></div>
    </div>

    <div class='psec' id='psecPic'>
      <div class='phead'><span>&#127912;</span><span data-i18n='camPanelPic'>Picture Settings</span></div>
      <div class='ri' id='pic5mpNote' style='display:none' data-i18n='camPic5mpNote'>5MP stills-only — video/stream/record stay at video sizes.</div>
      <div class='rw'><label>Brightness</label><input type='range' id='slBr' min='-2' max='2' value='0' step='1' oninput='cfg()' title='Brightness adjustment' data-i18n-title='camTipBright'><span class='v' id='vBr'>0</span></div>
      <div class='rw'><label>Contrast</label><input type='range' id='slCt' min='-2' max='2' value='0' step='1' oninput='cfg()' title='Contrast adjustment' data-i18n-title='camTipContrast'><span class='v' id='vCt'>0</span></div>
      <div class='rw'><label>Saturation</label><input type='range' id='slSa' min='-2' max='2' value='0' step='1' oninput='cfg()' title='Saturation adjustment' data-i18n-title='camTipSaturation'><span class='v' id='vSa'>0</span></div>
      <div class='rw'><label>Exposure level</label><input type='range' id='slAeL' min='-2' max='2' value='0' step='1' oninput='cfg()' title='AE level adjustment' data-i18n-title='camTipAeLevel'><span class='v' id='vAeL'>0</span></div>
      <div class='rw'><label>AGC gain</label><input type='range' id='slAgcG' min='0' max='30' value='5' oninput='cfg()' title='AGC gain value (manual mode)' data-i18n-title='camTipAgcGain'><span class='v' id='vAgcG'>6x</span></div>
      <div class='rw'><label>Manual exposure</label><input type='range' id='slAecV' min='0' max='1200' value='204' oninput='cfg()' title='AEC value, manual exposure only' data-i18n-title='camTipAecValue'><span class='v' id='vAecV'>204</span></div>
      <div class='g2'>
        <div>
          <div class='ck'><input type='checkbox' id='chkHM' onchange='cfg()' title='Horizontal mirror' data-i18n-title='camTipMirror'><label>H-Mirror</label></div>
          <div class='ck'><input type='checkbox' id='chkVF' onchange='cfg()' title='Vertical flip' data-i18n-title='camTipFlip'><label>V-Flip</label></div>
          <div class='ck'><input type='checkbox' id='chkAWB' onchange='cfg()' title='Auto white balance' data-i18n-title='camTipAwb'><label>AWB</label></div>
          <div class='ck'><input type='checkbox' id='chkLenc' onchange='cfg()' title='Lens correction' data-i18n-title='camTipLenc'><label>LENC</label></div>
        </div>
        <div>
          <div class='ck'><input type='checkbox' id='chkAEC' onchange='cfg()' title='Auto exposure control' data-i18n-title='camTipAec'><label>AEC</label></div>
          <div class='ck'><input type='checkbox' id='chkAec2' onchange='cfg()' title='AEC DSP control' data-i18n-title='camTipAec2'><label>AEC DSP</label></div>
          <div class='ck'><input type='checkbox' id='chkAGC' onchange='cfg()' title='Auto gain control' data-i18n-title='camTipAgc'><label>AGC</label></div>
        </div>
      </div>
    </div>

    <div class='psec' id='psecRec'>
      <div class='phead'><span>&#128249;</span><span data-i18n='camPanelRec'>Record and Files</span></div>
      <div class='act-row'>
        <button class='b b-rec' onclick='recStart()' title='Start video recording' data-i18n-title='camTipRecStart'>&#9679; Rec</button>
        <button class='b' onclick='recStop()' title='Stop video recording' data-i18n-title='camTipRecStop'>&#9632; Stop</button>
        <button class='b b-green' onclick='snapPhoto()' title='Capture photo' data-i18n-title='camTipCapture'>&#128247;</button>
      </div>
      <div class='ri' id='recInfo' title='Recording timer and frame count' data-i18n-title='camTipRecInfo'>&#9201; 00:00 | 0 fr</div>
      <div class='ri' id='photoInfo' style='display:none' title='Last photo path' data-i18n-title='camTipPhotoInfo'></div>
      <div class='ri' id='sdInfo' title='SD card free space' data-i18n-title='camTipSdInfo'>SD n/a</div>
      <div id='codecBlock' style='display:none'>
        <div class='sec-lbl'><span data-i18n='camCodec'>Codec</span> (P4)</div>
        <div class='rw'><label data-i18n='camCodec'>Codec</label><select id='selCodec' onchange='codecCfg()' title='Recording codec' data-i18n-title='camTipCodec'><option value='0'>MJPEG (.avi)</option><option value='1'>H.264 (.264)</option></select></div>
        <div class='rw' id='rwH264Cont' style='display:none'><label data-i18n='camContainer'>Container</label><select id='selH264Cont' onchange='codecCfg()' title='H.264 container' data-i18n-title='camTipContainer'><option value='0'>Raw (.264)</option><option value='1'>AVI (.avi)</option></select></div>
        <div class='rw' id='rwH264Br' style='display:none'><label data-i18n='camBitrate'>Bitrate (Mbps)</label><input type='range' id='slH264Br' min='1' max='8' value='4' oninput='codecCfg()' title='H.264 bitrate' data-i18n-title='camTipBitrate'><span class='v' id='vH264Br'>4</span></div>
        <div class='ri' id='h264Est' style='display:none' title='Estimated SD cost' data-i18n-title='camTipEst'></div>
      </div>
      <div class='sec-lbl'>DashCam</div>
      <div class='ck'><input type='checkbox' id='chkDash' onchange='autoCfg()' title='Enable dashcam mode' data-i18n-title='camTipDashcam'><label>Enable</label></div>
      <div class='rw'><label>Rotation (min)</label><input type='range' id='slDash' min='1' max='30' value='5' oninput='autoCfg()' title='Dashcam rotation interval (min)' data-i18n-title='camTipDashRot'><span class='v' id='vDash'>5</span></div>
      <div class='sec-lbl'>Timelapse</div>
      <div class='rw'><label>Every (sec)</label><input type='range' id='slTl' min='0' max='600' value='0' oninput='tlCfg()' title='Timelapse interval seconds (0=off)' data-i18n-title='camTipTimelapse'><span class='v' id='vTl'>0</span></div>
      <div class='act-row'><button class='b' onclick='tlStop()' title='Stop timelapse capture' data-i18n-title='camTipTlStop'>&#9632; Stop</button></div>
      <div class='ri' id='tlInfo' style='display:none' title='Timelapse info' data-i18n-title='camTipTlInfo'></div>
      <div class='sec-lbl'>Auto Mode</div>
      <div class='rw'><label>Boot mode</label><select id='selAuto' onchange='autoCfg2()' title='Camera mode to start automatically at boot' data-i18n-title='camTipAutoMode'><option value='0'>Off</option><option value='1'>Photo</option><option value='2'>Video</option><option value='3'>Motion</option><option value='4'>Timelapse</option></select></div>
      <div class='sec-lbl'><span data-i18n='camFiles'>Files</span></div>
      <div class='rw'><label>AVI file</label><select id='selFile' title='Recorded AVI file' data-i18n-title='camTipFiles'><option value=''>--</option></select></div>
      <div class='act-row'>
        <button class='b' onclick='loadFiles()' title='Refresh file list' data-i18n-title='camTipRefresh'>&#8635; <span data-i18n='camRefresh'>Refresh</span></button>
        <button class='b b-green' onclick='playFile()' title='Play selected AVI' data-i18n-title='camTipPlay'>&#9654; <span data-i18n='camPlay'>Play</span></button>
      </div>
      <div class='act-row'>
        <button class='b' onclick='stopPlay()' title='Stop playback' data-i18n-title='camTipStopPlay'>&#9632; <span data-i18n='camStopPlay'>Stop</span></button>
        <button class='b b-rec' onclick='delFile()' title='Delete selected AVI' data-i18n-title='camTipDelete'>&#10005; <span data-i18n='camDelete'>Delete</span></button>
      </div>
      <div class='ri' id='playInfo' style='display:none' title='Playback status' data-i18n-title='camTipPlayInfo'></div>
    </div>

    <div class='psec' id='psecMot'>
      <div class='phead'><span>&#128200;</span><span data-i18n='camPanelMot'>Motion Detect</span></div>
      <div class='ck'><input type='checkbox' id='chkMot' onchange='mot()' title='Enable motion detection' data-i18n-title='camTipMotion'><label>Enabled</label></div>
      <div class='hint' data-i18n='camMotHint'>Motion needs the camera ON (Start). Detection pauses while streaming.</div>
      <div class='rw'><label>Sensitivity</label><input type='range' id='slMot' min='1' max='100' value='20' oninput='mot()' title='Motion sensitivity' data-i18n-title='camTipMotSens'><span class='v' id='vMot'>20</span></div>
      <div class='rw'><label>Confirm frames</label><input type='range' id='slMotCf' min='1' max='20' value='3' oninput='mot()' title='Min frames to confirm motion' data-i18n-title='camTipMotConfirm'><span class='v' id='vMotCf'>3</span></div>
      <div class='rw'><label>Stop frames</label><input type='range' id='slMotSf' min='2' max='30' value='10' oninput='mot()' title='Frames to stop recording' data-i18n-title='camTipMotStop'><span class='v' id='vMotSf'>10</span></div>
      <div class='rw'><label>Post seconds</label><input type='range' id='slMotPs' min='0' max='30' value='5' oninput='mot()' title='Post-record seconds' data-i18n-title='camTipMotPost'><span class='v' id='vMotPs'>5</span></div>
      <div class='rw'><label>Action</label><select id='selMotAct' onchange='mot()' title='Action on motion detection' data-i18n-title='camTipMotAction'><option value='0'>Record video</option><option value='1'>Snap photo</option><option value='2'>Email alert</option></select></div>
      <div class='rw'><label>Score</label><span class='score-pill' id='motSc' title='Motion score percentage' data-i18n-title='camTipMotScore'>0%</span></div>
      <div class='rw'><label>Events</label><span class='score-pill' id='motEv' style='color:var(--tx)' title='Motion event count' data-i18n-title='camTipMotEvents'>0</span></div>
      <div class='rw'><label>Cooldown</label><input type='range' id='slMotCd' min='0' max='120' value='5' oninput='mot()' title='Seconds between counted events' data-i18n-title='camTipMotCooldown'><span class='v' id='vMotCd'>5</span></div>
      <div class='ck'><input type='checkbox' id='chkMotLamp' onchange='mot()' title='Full lamp during motion event' data-i18n-title='camTipMotLamp'><label>Lamp on event</label></div>
      <div class='sec-lbl'>ROI (0-39 / 0-29, 0 = full)</div>
      <div class='g2'>
        <div class='rw'><label>X</label><input type='range' id='slRoiX' min='0' max='39' value='0' oninput='mot()' title='ROI left' data-i18n-title='camTipMotRoi'><span class='v' id='vRoiX'>0</span></div>
        <div class='rw'><label>Y</label><input type='range' id='slRoiY' min='0' max='29' value='0' oninput='mot()' title='ROI top' data-i18n-title='camTipMotRoi'><span class='v' id='vRoiY'>0</span></div>
        <div class='rw'><label>W</label><input type='range' id='slRoiW' min='0' max='40' value='0' oninput='mot()' title='ROI width (0=full)' data-i18n-title='camTipMotRoi'><span class='v' id='vRoiW'>0</span></div>
        <div class='rw'><label>H</label><input type='range' id='slRoiH' min='0' max='30' value='0' oninput='mot()' title='ROI height (0=full)' data-i18n-title='camTipMotRoi'><span class='v' id='vRoiH'>0</span></div>
      </div>
      <div class='act-row'>
        <button class='b' onclick='motReset()' title='Reset event counter' data-i18n-title='camTipMotReset'>&#8635; <span data-i18n='camMotReset'>Reset count</span></button>
        <a class='b' href='/cam/motion/diff' target='_blank' title='Motion diff visualisation' data-i18n-title='camTipMotDiff'>&#128200; Diff view</a>
      </div>
    </div>
    <div class='psec' id='psecVis'>
      <div class='phead'><span>&#129504;</span><span data-i18n='camPanelVis'>Vision</span> (P4)</div>
      <div class='rw'><label data-i18n='camVisMode'>Mode</label><select id='selVisMode' onchange='visCfg()' title='Vision mode' data-i18n-title='camTipVisMode'><option value='0'>Off</option><option value='1'>Count</option><option value='2'>Defect</option></select></div>
      <div class='rw'><label data-i18n='camVisThresh'>Threshold</label><input type='range' id='slVisTh' min='0' max='100' value='50' oninput='visCfg()' title='OK/NOK score threshold' data-i18n-title='camTipVisThresh'><span class='v' id='vVisTh'>50</span></div>
      <div class='ri' id='visModel' title='Model file state' data-i18n-title='camTipVisModel'>Model: none</div>
      <div class='rw'><label>OK</label><span class='score-pill' id='visOk' title='OK count' data-i18n-title='camTipVisOk'>0</span></div>
      <div class='rw'><label>NOK</label><span class='score-pill' id='visNok' title='NOK count' data-i18n-title='camTipVisNok'>0</span></div>
      <div class='ri' id='visLast' style='display:none' title='Last annotated frame' data-i18n-title='camTipVisLast'></div>
      <div class='ri' id='visFps' title='Inference rate' data-i18n-title='camTipVisFps'>infer 0.0 fps</div>
      <div class='sec-lbl'><span data-i18n='camVisBurst'>Dataset burst</span></div>
      <div class='rw'><label data-i18n='camVisN'>Photos</label><input type='range' id='slVisN' min='1' max='50' value='10' oninput='visN()' title='Burst photo count' data-i18n-title='camTipVisN'><span class='v' id='vVisN'>10</span></div>
      <div class='rw'><label data-i18n='camVisLabel'>Label</label><input type='text' id='inVisLabel' maxlength='16' value='gear' oninput='visN()' title='Burst label' data-i18n-title='camTipVisLabel'></div>
      <div class='act-row'>
        <button class='b b-green' onclick='visBurst()' title='Start dataset burst' data-i18n-title='camTipVisBurst'>&#128247; <span data-i18n='camVisGo'>Collect</span></button>
        <button class='b' onclick='visReset()' title='Reset OK/NOK counters' data-i18n-title='camTipVisReset'>&#8635; <span data-i18n='camVisResetC'>Reset</span></button>
      </div>
      <div class='ri' id='visBurstInfo' style='display:none' title='Burst progress' data-i18n-title='camTipVisBurstInfo'></div>
    </div>
    </div>
  </div>

  <div class='str'>
    <div class='rec-badge' id='recBadge' title='Recording in progress' data-i18n-title='camTipRecBadge'><span class='rec-dot'></span><span>REC</span></div>
    <div class='ph' id='camPh' title='Waiting for camera...' data-i18n-title='camTipWaiting'>
      <div class='ph-icon'>&#128247;</div>
      <div data-i18n='camWaiting'>Waiting for camera...</div>
      <div class='ph-sub' id='phSub'>Press Start to begin streaming</div>
    </div>
    <img id='camImg' style='display:none' alt='Live' title='Live camera stream' data-i18n-title='camTipStream'>
  </div>
</div>

<div class='statusbar'>
  <a href='/' class='b b-icon' data-i18n='btnHome' title='Go to home page' data-i18n-title='camTipHome' style='height:16px;width:16px;font-size:8px'>&#127968;</a>
  <span class='fmodel' id='ftModel' title='Camera model' data-i18n-title='camTipModel'>Camera off</span>
  <span class='fsep'>|</span>
  <span id='ftHeap' title='Free heap / max alloc block' data-i18n-title='camTipHeap'>--</span>
  <span class='fsep'>|</span>
  <span id='ftSd' title='SD card free space' data-i18n-title='camTipSdInfo'>SD n/a</span>
  <span class='fsep'>|</span>
  <span class='frec' id='ftRec' title='Recording status' data-i18n-title='camTipRecStatus'></span>
  <span id='ftMot' title='Motion status' data-i18n-title='camTipMotStatus'></span>
</div>
</div>

<script>
var R=false,T=null,PollMs=5000,NotReadyStrikes=0;
function $(i){return document.getElementById(i)}

function showPsec(btn,id){
  document.querySelectorAll('.psec').forEach(function(p){p.classList.remove('on')});
  document.querySelectorAll('.rail button').forEach(function(b){b.classList.remove('on')});
  var el=$('psec'+id.charAt(0).toUpperCase()+id.slice(1));if(el)el.classList.add('on');
  btn.classList.add('on')
}

function toggleLinkDd(){
  var m=$('linkMenu');
  m.classList.toggle('show');
}
function copyLink(type){
  var h=location.hostname||location.host;
  var p=location.port?':'+location.port:'';
  var base='http://'+h+p;
  var url=type==='snapshot'?base+'/cam/snapshot':type==='raw'?base+'/cam/raw':base+'/cam/stream';
  if(navigator.clipboard){navigator.clipboard.writeText(url).then(function(){linkToast(url)}).catch(function(){linkToast(url)})}
  else{var ta=document.createElement('textarea');ta.value=url;document.body.appendChild(ta);ta.select();document.execCommand('copy');document.body.removeChild(ta);linkToast(url)}
  $('linkMenu').classList.remove('show');
}
function linkToast(url){
  var t=document.createElement('div');
  t.textContent='Copied: '+url;
  t.style.cssText='position:fixed;bottom:40px;left:50%;transform:translateX(-50%);background:rgba(4,10,24,.9);color:#4ade80;padding:6px 14px;border-radius:6px;font-size:11px;z-index:99;border:1px solid rgba(74,222,128,.3);pointer-events:none;box-shadow:0 4px 12px rgba(0,0,0,.5)';
  document.body.appendChild(t);
  setTimeout(function(){t.remove()},2000);
}
document.addEventListener('click',function(e){var dd=$('linkDd');if(dd&&!dd.contains(e.target))$('linkMenu').classList.remove('show')});

function camToggle(){
  if(!R){
    fetch('/api/camera/start',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
      if(d.success){R=true;upd({ready:true});strStart()}
      else customAlert(d.message||'Failed')
    }).catch(function(){customAlert('Failed')})
  } else {
    fetch('/api/camera/stop',{method:'POST'}).then(function(){R=false;upd({});strStop()}).catch(function(){R=false;strStop()})
  }
}

function strStart(){
  if(!R)return;
  var i=$('camImg'),p=$('camPh'),sub=$('phSub');
  i.style.display='none';p.style.display='block';
  if(sub)sub.textContent='Connecting to /cam/stream...';
  i.onerror=function(){p.style.display='none';i.style.display='none';p.style.display='block';if(sub)sub.textContent='Stream unreachable or busy (1 viewer max) — retrying...';if(R)setTimeout(strStart,2000)};
  i.onload=function(){p.style.display='none';i.style.display='block';if(sub)sub.textContent='Press Start to begin streaming'};
  i.src='/cam/stream?_='+Date.now()
}
function strStop(){
  var i=$('camImg'),p=$('camPh');
  if(i){i.onerror=null;try{i.removeAttribute('src')}catch(e){}i.src='';i.style.display='none'}
  if(p)p.style.display='block'
}

var LastMaxFs=-1;
var LastResList='';
var LastSrcList='';
var PreferredP4Res='';
// P4 adaptive state (driven by /api/camera/status in upd()): detected sensor
// model ('OV5647' = RPi v1.3 MIPI, 'USB-UVC' = webcam), P4 build flag and the
// fps ceiling. The legacy S3 path leaves CurFpsMax at 0 (no ceiling).
var CurModel='';
var CurIsP4=false;
var CurFpsMax=0;
// P4 OV5647 (RPi v1.3) presets: explicit sensor modes from kP4CsiModes —
// never 1280x720 (experimental RAW10 profile stalls the pipeline, CAM-62).
// fps values stay at/below the MIPI sustained ceiling (6). Safe = the only
// HW-verified mode (800x800). USB presets carry no resolution: the geometry
// is negotiated with the webcam, so only quality/fps/picture are pushed.
var P4_OV5647_PRESETS={
  hd:{res:'1920x1080',q:10,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  vga:{res:'800x640',q:20,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  fast:{res:'800x640',q:35,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  night:{res:'800x800',q:15,fs:4,br:1,ct:-1,sa:-1,awb:0,aec:1,agc:1,gc:0},
  safe:{res:'800x800',q:12,fs:5,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  eco:{res:'800x640',q:24,fs:4,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0}
};
var P4_USB_PRESETS={
  hd:{q:10,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  vga:{q:20,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  fast:{q:35,fs:6,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  night:{q:15,fs:4,br:1,ct:-1,sa:-1,awb:0,aec:1,agc:1,gc:0},
  safe:{q:12,fs:5,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},
  eco:{q:24,fs:4,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0}
};
function resolutionPostField(value){
  return value.indexOf('x')>=0?'resolution='+encodeURIComponent(value)+'&':'framesize='+value+'&';
}
function selectResOption(sel,value){
  if(!sel||!value)return false;
  for(var i=0;i<sel.options.length;i++){
    if(sel.options[i].value===String(value)){sel.value=String(value);return true}
  }
  return false;
}
// P4 source selector: list ATTACHED cameras, not just generic entries, so the
// user sees what is plugged in before pressing Start. Labels come from the
// status presence fields (detMipi/detUsb/usbId); the persisted choice is kept
// whenever it is still a valid option.
function applySrcList(d){
  if(!d||d.detMipi===undefined)return;
  var key=(d.detMipi?1:0)+'/'+(d.detUsb?1:0)+'/'+(d.usbId||'');
  if(key===LastSrcList)return;
  LastSrcList=key;
  var s=$('selSrc');if(!s)return;
  var cur=s.value;
  s.options.length=0;
  s.options[s.options.length]=new Option(d.detMipi?'Native \u2014 OV5647 (CSI)':'Native \u2014 no CSI camera','0');
  s.options[s.options.length]=new Option(d.detUsb?('USB \u2014 '+(d.usbId||'camera')):'USB \u2014 none detected','1');
  if(cur==='0'||cur==='1')s.value=cur;
  else s.value=d.detUsb?'1':(d.detMipi?'0':'0');
  if(s.selectedIndex<0)s.value='0';
}
function applyResList(resList){
  // P4 adaptive sizes: the backend reports the probed sensor modes
  // ("1920x1080,..." MIPI CSI, negotiated "WxH" on USB) instead of one
  // camera's enum list. Rebuild the select to those entries only; the
  // values are "WxH" strings (cfg() posts them as resolution=WxH).
  if(resList===LastResList)return;
  LastResList=resList;
  var fr=$('selFr');if(!fr||!resList)return;
  var parts=resList.split(',');
  fr.options.length=0;
  for(var i=0;i<parts.length;i++){
    var s=(parts[i]||'').replace(/^\s+|\s+$/g,'');
    if(/^[0-9]+x[0-9]+$/.test(s))fr.options[fr.options.length]=new Option(s,s);
  }
  if(PreferredP4Res)selectResOption(fr,PreferredP4Res);
  LastMaxFs=-1;  // resList owns the list now; legacy clamp stays out
}
function applyMaxFrameSize(maxFs){
  if(LastResList)return;  // adaptive list active: never re-disable entries
  if(maxFs===LastMaxFs)return;   // only touch the list once per detected sensor
  LastMaxFs=maxFs;
  var fr=$('selFr');if(!fr)return;
  var opts=fr.options,i,maxOpt=-1;
  for(i=0;i<opts.length;i++){
    var v=parseInt(opts[i].value,10);
    opts[i].disabled=(v>maxFs);
    if(v<=maxFs&&v>maxOpt)maxOpt=v;
  }
  // If the current selection is now unavailable, clamp it to the highest allowed.
  var cur=parseInt(fr.value,10);
  if(cur>maxFs&&maxOpt>=0){fr.value=maxOpt;cfg()}
}
function upd(d){
  var on=d&&d.ready;
  if(d&&d.resList!==undefined&&d.resList)applyResList(d.resList);
  else if(d&&d.maxFrameSize!==undefined)applyMaxFrameSize(d.maxFrameSize);
  // Highlight the live geometry in the resolution selector (P4 width/height).
  if(d&&d.width&&d.height){var frx=$('selFr');if(frx)selectResOption(frx,d.width+'x'+d.height)}
  if(d&&d.fpsMax!==undefined){
    var sf=$('selFps');
    // Fase 4 (Tarefa 5): per-path ceiling — H.264 runs to the sensor rate
    // (fpsMaxH264, 30 MIPI), MJPEG keeps the measured fpsMax (6 MIPI).
    var fmax=(d.fpsMaxH264!==undefined&&$('selCodec')&&$('selCodec').value==='1')?d.fpsMaxH264:d.fpsMax;
    if(sf){sf.max=fmax;if(parseInt(sf.value,10)>fmax){sf.value=fmax;cfg()}}
  }
  var starting=d&&d.initInProgress&&!on;
  $('bToggle').className='b '+(on?'b-rec':(starting?'':'b-green'));
  $('bToggle').innerHTML=on?'&#9632; <span data-i18n="camStop">Stop</span>':(starting?'&#9632; <span data-i18n="camInit">Starting…</span>':'&#9654; <span data-i18n="camStart">Start</span>');
  var rs=$('bRecS');if(rs)rs.style.display=on?'inline-flex':'none';
  $('stDot').className='dot '+(on?'dot-g':'dot-r');
  var pill=on?((d.model||'Cam')+' '+(d.streamFps||0)+'fps'):'Off';
  if(d&&d.initInProgress&&!on){$('stDot').className='dot dot-g';pill='Starting...'}
  if(d&&d.recording)pill+=' [REC]';
  // Capture-pipeline stall (CAM-62): the P4 status carries capStall=1 while no
  // frame arrived for 4 s. Surface it in the pill so a wedged sensor is
  // diagnosed from the page instead of looking like a dead HTTP path.
  if(on&&d&&d.capStall){$('stDot').className='dot dot-r';pill+=' [NO FRAMES]'}
  $('stInfo').textContent=pill;
  var p4model=(d&&(d.model==='OV5647'||d.model==='USB-UVC'));
  var rws=$('rwSrc');if(rws)rws.style.display=(p4model||(d&&d.p4src))?'flex':'none';
  if(d&&d.detMipi!==undefined)applySrcList(d);
  updVision(d);
  // Fase 1 (Tarefa 5): Codec block only where the HW encoder exists
  // (h264Avail from /api/camera/config+status, P4 MIPI + esp_h264 core).
  var cb=$('codecBlock');if(cb)cb.style.display=(d&&d.h264Avail)?'block':'none';
  // P4 preset state: remember the detected sensor + fps ceiling so preset()
  // pushes per-sensor values (OV5647 WxH modes vs USB quality/fps only).
  CurModel=(d&&d.model)||'';
  CurIsP4=!!(p4model||(d&&d.p4src)||LastResList);
  if(d&&d.fpsMax!==undefined)CurFpsMax=d.fpsMax;
  applyPresetTitles();
  // Fase 3 (Tarefa 5): 5MP stills-only note — live geometry wider than the
  // 1920 video ceiling means Photo saves full-res while stream/record drop
  // oversize frames (P4 only; existing width/height fields, no new API).
  var p5n=$('pic5mpNote');if(p5n)p5n.style.display=(d&&d.width&&d.width>1920)?'block':'none';
  var rb=$('recBadge');if(rb)rb.classList.toggle('on',!!(d&&d.recording));
  var hp=$('heapPill');if(hp)hp.textContent='🧠 '+((d&&d.freeHeap!==undefined)?Math.round(d.freeHeap/1024)+'K':'--');
  var tp=$('tempPill');if(tp)tp.textContent='🌡️ '+((d&&d.boardTemp!==undefined)?d.boardTemp.toFixed(1)+'°C':'--');
  if(d&&d.motion!==undefined){var sc=d.motionScore||0;var ms=$('motSc');if(ms)ms.textContent=sc+'%'}
  if(d&&d.motionEvents!==undefined){var me=$('motEv');if(me)me.textContent=d.motionEvents}
  var tl=$('tlInfo');
  if(tl){
    if(d&&d.timelapseActive){
      tl.style.display='block';
      var s='Next capture in '+d.timelapseNextInSecs+'s';
      if(d.timelapseLast&&d.timelapseLast.length)s+=' · '+d.timelapseLast;
      tl.textContent=s
    } else tl.style.display='none'
  }
  var sdTxt='SD n/a';
  var sd=$('sdInfo');
  if(d&&(d.sdTotal!==undefined&&d.sdTotal>0)){sdTxt='SD '+d.sdFree+'/'+d.sdTotal+' MB free';if(sd)sd.textContent=sdTxt}
  else if(sd)sd.textContent=sdTxt;
  var ftd=$('ftSd');if(ftd)ftd.textContent=sdTxt;
  var ftm=$('ftModel');if(ftm)ftm.textContent=on?((d.model||'Cam')+' '+(d.streamFps||0)+' fps'):'Camera off';
  var fth=$('ftHeap');if(fth&&d)fth.textContent=(d.freeHeap/1024).toFixed(0)+'K/'+(d.maxAlloc/1024).toFixed(0)+'K';
  var recOn=!!(d&&d.recording);
  var ftr=$('ftRec');
  if(!recOn){
    if(rs&&rs.innerHTML.indexOf(':')>=0)rs.innerHTML='&#9679; Rec';  // restore REC button when a stale timer was shown
    if(ftr)ftr.textContent=(d&&d.timelapseActive)?('TL next '+d.timelapseNextInSecs+'s'):'';
    var ftmo=$('ftMot');
    if(ftmo)ftmo.textContent=(d&&d.motion)?('Motion '+(d.motionScore||0)+'%'):'';
    return
  }
  var m=Math.floor(d.recElapsed/60),s=d.recElapsed%60;
  var tstr=(m<10?'0':'')+m+':'+(s<10?'0':'')+s;
  if(rs)rs.innerHTML='&#9679; Rec';
  var ri=$('recInfo');
  if(ri)ri.innerHTML='&#9201; '+tstr+' | '+d.recFrames+' fr';
  if(ftr)ftr.textContent='REC '+tstr+' | '+d.recFrames+' fr';
  var ftmo2=$('ftMot');
  if(ftmo2)ftmo2.textContent=(d&&d.motion)?('Motion '+(d.motionScore||0)+'%'):'';
}

function snapPhoto(){
  if(!R)return;
  fetch('/api/camera/photo',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
    if(d.success){
      var p=$('photoInfo');
      if(p){p.style.display='block';p.textContent='Saved: '+(d.filename||'photo.jpg')}
      customAlert('Photo saved')
    } else customAlert(d.message||'Failed')
  })
}
function recStart(){
  if(!R)return;
  fetch('/api/camera/recording/start',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
    if(!d.success)customAlert(d.message||'Failed')
  })
}
function recStop(){
  if(!R)return;
  fetch('/api/camera/recording/stop',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
    if(d.success){
      var ri=$('recInfo');
      if(ri)ri.textContent='Saved '+(d.frames||0)+' fr → '+(d.path||'');
    } else customAlert(d.message||'Failed')
  })
}

function pCfg(b){
  if(T)clearTimeout(T);
  T=setTimeout(function(){fetch('/api/camera/config',{method:'POST',body:b,headers:{'Content-Type':'application/x-www-form-urlencoded'}}).then(function(r){return r.json()}).then(function(d){if(!d.success)customAlert('Config failed')})},400)
}
function cfg(){
  var q=$('slQ').value,br=$('slBr').value,ct=$('slCt').value,sa=$('slSa').value;
  if($('vQ'))$('vQ').textContent=q;if($('vBr'))$('vBr').textContent=br;if($('vCt'))$('vCt').textContent=ct;if($('vSa'))$('vSa').textContent=sa;
  if($('vFps'))$('vFps').textContent=$('selFps').value;if($('vAeL'))$('vAeL').textContent=$('slAeL').value;
  var ag=$('slAgcG').value;if($('vAgcG'))$('vAgcG').textContent=(parseInt(ag)+1)+'x';
  if($('vAecV'))$('vAecV').textContent=$('slAecV').value;
  var aec2El=$('chkAec2');
  // Adaptive "WxH" options (resList) carry no framesize enum: skip the field
  // so the backend keeps its current enum (used only for fps clamp + tags;
  // capture/recording follow the live negotiated size).
  var frV=$('selFr').value;
  // Adaptive "WxH" options (resList) post an explicit resolution the backend
  // validates against the sensor mode table; enum values keep legacy path.
  var frPost=resolutionPostField(frV);
  pCfg(frPost+'quality='+q+'&fps='+$('selFps').value+'&brightness='+br+'&contrast='+ct+'&saturation='+sa+'&hmirror='+($('chkHM').checked?1:0)+'&vflip='+($('chkVF').checked?1:0)+'&awb='+($('chkAWB').checked?1:0)+'&aec='+($('chkAEC').checked?1:0)+'&agc='+($('chkAGC').checked?1:0)+'&lenc='+($('chkLenc').checked?1:0)+'&gainceiling='+$('slGc').value+'&special_effect='+$('selSfx').value+'&wb_mode='+$('selWb').value+'&aec_value='+$('slAecV').value+'&aec2='+((aec2El&&aec2El.checked)?1:0)+'&agc_gain='+$('slAgcG').value+'&ae_level='+$('slAeL').value)
}
function mot(){
  var s=$('slMot').value,cf=$('slMotCf').value,sf=$('slMotSf').value,ps=$('slMotPs').value,ma=$('selMotAct').value;
  if($('vMot'))$('vMot').textContent=s;if($('vMotCf'))$('vMotCf').textContent=cf;if($('vMotSf'))$('vMotSf').textContent=sf;if($('vMotPs'))$('vMotPs').textContent=ps;
  var cd=$('slMotCd').value;if($('vMotCd'))$('vMotCd').textContent=cd;
  var rx=$('slRoiX').value,ry=$('slRoiY').value,rw=$('slRoiW').value,rh=$('slRoiH').value;
  if($('vRoiX'))$('vRoiX').textContent=rx;if($('vRoiY'))$('vRoiY').textContent=ry;if($('vRoiW'))$('vRoiW').textContent=rw;if($('vRoiH'))$('vRoiH').textContent=rh;
  pCfg('motion_enabled='+($('chkMot').checked?1:0)+'&motion_sensitivity='+s+'&motion_confirm_frames='+cf+'&motion_stop_frames='+sf+'&motion_post_record_secs='+ps+'&motion_action='+ma+'&motion_cooldown_secs='+cd+'&motion_lamp_auto='+($('chkMotLamp').checked?1:0)+'&motion_roi_x='+rx+'&motion_roi_y='+ry+'&motion_roi_w='+rw+'&motion_roi_h='+rh)
}
function motReset(){
  fetch('/api/camera/motion/reset',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
    if(d.success){var e=$('motEv');if(e)e.textContent='0'}else customAlert('Failed')
  })
}
// Fase 5 (Tarefa 5): Vision tab (P4-only). Mode/threshold persist via pCfg;
// burst is a one-shot POST; counters reset via POST.
function visCfg(){
  var t=$('slVisTh').value;
  if($('vVisTh'))$('vVisTh').textContent=t;
  pCfg('vision_mode='+$('selVisMode').value+'&vision_thresh='+t)
}
function visN(){
  var n=$('slVisN').value;
  if($('vVisN'))$('vVisN').textContent=n;
}
function visBurst(){
  visN();
  var n=$('slVisN').value,l=($('inVisLabel').value||'burst');
  var b='n='+encodeURIComponent(n)+'&label='+encodeURIComponent(l);
  fetch('/api/camera/vision/burst',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(function(r){return r.json()}).then(function(d){
    var e=$('visBurstInfo');
    if(d.success){if(e){e.style.display='block';e.textContent='burst '+d.n+'x '+d.label+' …'}}
    else customAlert('Burst failed: '+(d.message||'error'))
  })
}
function visReset(){
  fetch('/api/camera/vision/reset',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
    if(d.success){if($('visOk'))$('visOk').textContent='0';if($('visNok'))$('visNok').textContent='0'}
    else customAlert('Failed')
  })
}
function updVision(d){
  var rv=$('railVis');if(rv)rv.style.display=(d&&(d.p4src||d.visMode!==undefined))?'':'none';
  if(!d||d.visMode===undefined)return;
  var pi=$('photoInfo');
  if(pi&&(d.photoPend||d.photoRam)){
    pi.style.display='block';
    var t='RAM photo';
    if(d.photoRam)t+=' (<a href=\'/api/camera/last-photo\' target=\'_blank\'>view</a>)';
    if(d.photoPend)t+=' — '+d.photoPend+' saving…';
    if(pi.innerHTML!==t)pi.innerHTML=t;
  }
  if($('selVisMode'))$('selVisMode').value=d.visMode;
  var m=$('visModel');
  if(m)m.textContent=d.visModel?('Model: '+Math.round((d.visModelSize||0)/1024)+'KB'):'Model: none (FTP to /CAM/VISION/model.espdl)';
  if($('visOk'))$('visOk').textContent=d.visOk||0;
  if($('visNok'))$('visNok').textContent=d.visNok||0;
  var vl=$('visLast');
  if(vl){if(d.visLast&&d.visLast.length){vl.style.display='block';vl.textContent=d.visLast}else vl.style.display='none'}
  var vf=$('visFps');
  // Label follows reality: with a model loaded the rate is inference; without
  // one it is the PPA preprocess pass (still proof the path works).
  if(vf){var vlab=d.visModel?'infer':'preprocess';vf.textContent=vlab+' '+((d.visFpsX10||0)/10).toFixed(1)+' fps'+(d.visPpa?'':' (no PPA)')}
  var bi=$('visBurstInfo');
  if(bi){if(d.visBurstLeft>0){bi.style.display='block';bi.textContent='burst left: '+d.visBurstLeft}else if(bi.textContent.indexOf('…')<0)bi.style.display='none'}
}
function autoCfg2(){
  pCfg('auto_mode='+$('selAuto').value)
}
function autoCfg(){
  var dm=$('chkDash').checked?$('slDash').value:0;
  if($('vDash'))$('vDash').textContent=$('slDash').value;
  pCfg('dashcam_mins='+dm)
}
function tlCfg(){
  var t=$('slTl').value;
  if($('vTl'))$('vTl').textContent=t;
  pCfg('timelapse_secs='+t)
}
function tlStop(){
  if($('slTl'))$('slTl').value=0;
  if($('vTl'))$('vTl').textContent='0';
  pCfg('timelapse_secs=0')
}
function srcCfg(){
  pCfg('source='+$('selSrc').value)
}
// Fase 1 (Tarefa 5): Codec block (P4-only). MJPEG stays the default; H.264
// writes raw Annex B .264 at slH264Br Mbps. Estimate matches
// cameraH264MbPerMin() (Mbps*60/8/1024).
function codecUI(){
  var c=$('selCodec')?$('selCodec').value:'0';
  var br=$('slH264Br')?parseInt($('slH264Br').value,10):4;
  if($('vH264Br'))$('vH264Br').textContent=br;
  var e=$('h264Est');
  if(e){e.textContent='~'+(br*60/8/1024).toFixed(1)+' MB/min';e.style.display=(c==='1')?'block':'none'}
  var rw=$('rwH264Br');if(rw)rw.style.display=(c==='1')?'flex':'none';
  var rc=$('rwH264Cont');if(rc)rc.style.display=(c==='1')?'flex':'none';
}
function codecCfg(){
  codecUI();
  pCfg('rec_codec='+$('selCodec').value+'&h264_bitrate='+($('slH264Br').value*1000)+'&h264_container='+$('selH264Cont').value)
}
function lampCfg(){
  var t=$('slLamp').value;
  if($('vLamp'))$('vLamp').textContent=t;
  pCfg('lamp='+t)
}
var PM='';
function loadFiles(){
  fetch('/api/camera/files',{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){
    var s=$('selFile');if(!s)return;
    s.options.length=0;
    var files=(d&&d.files)||[];
    if(!files.length){var o=document.createElement('option');o.value='';o.text='--';s.appendChild(o);return}
    files.forEach(function(f){
      var o=document.createElement('option');
      o.value=f.n;o.text=f.n.split('/').pop()+' ('+Math.round(f.s/1024)+'KB)';
      s.appendChild(o)
    })
  }).catch(function(){})
}
function playFile(){
  var s=$('selFile');
  if(!s||!s.value){customAlert('Select a file');return}
  PM=s.value;
  var i=$('camImg'),p=$('camPh');
  i.style.display='none';if(p)p.style.display='block';
  i.onerror=function(){if(p)p.style.display='block';i.style.display='none';var pi=$('playInfo');if(pi){pi.style.display='block';pi.textContent='Playback failed'};PM=''};
  i.onload=function(){if(p)p.style.display='none';i.style.display='block'};
  i.src='/cam/playback?file='+encodeURIComponent(PM)+'&_='+Date.now();
  var pi2=$('playInfo');if(pi2){pi2.style.display='block';pi2.textContent='Playing: '+PM}
}
function stopPlay(){
  if(!PM)return;
  PM='';
  var i=$('camImg');
  if(i){i.onerror=null;try{i.removeAttribute('src')}catch(e){}i.src='';i.style.display='none'}
  var p=$('camPh');if(p)p.style.display='block';
  var pi=$('playInfo');if(pi)pi.style.display='none';
  if(R)strStart()
}
function delFile(){
  var s=$('selFile');
  if(!s||!s.value){customAlert('Select a file');return}
  if(!customConfirm('Delete '+s.value+'?'))return;
  fetch('/api/camera/delete',{method:'POST',body:'file='+encodeURIComponent(s.value),headers:{'Content-Type':'application/x-www-form-urlencoded'}}).then(function(r){return r.json()}).then(function(d){
    if(d.success){stopPlay();loadFiles()}else customAlert(d.message||'Failed')
  })
}
// P4 preset tooltips follow the detected sensor (CAM-68): the static HTML
// titles describe the S3/legacy presets, so override them per poll while a
// P4 sensor is known. S3 boards keep the static titles untouched.
function applyPresetTitles(){
  if(!CurIsP4)return;
  var usb=(CurModel==='USB-UVC');
  var t={
    bPrHd:usb?'HD preset (max quality — resolution fixed by the USB camera)':'HD preset (1920x1080)',
    bPrVga:usb?'VGA preset (balanced — resolution fixed by the USB camera)':'VGA preset (800x640)',
    bPrFast:usb?'Fast preset (low latency — resolution fixed by the USB camera)':'Fast preset (800x640, low latency)',
    bPrNight:usb?'Night preset (enhanced — resolution fixed by the USB camera)':'Night preset (800x800, enhanced)',
    bPrSafe:usb?'Safe preset (quality/fps only — resolution fixed by the USB camera)':'Safe preset (800x800 verified stable)',
    bPrEco:usb?'Economy preset (low CPU/bandwidth — resolution fixed by the USB camera)':'Economy preset (800x640, lowest CPU/bandwidth)'
  };
  for(var id in t){var b=$(id);if(b)b.title=t[id]}
}
function preset(n,el){
  // P4 path (CAM-68): push per-sensor values. OV5647 (RPi v1.3) posts an
  // explicit resolution=WxH from the sensor mode table; USB posts quality +
  // fps only (geometry is negotiated with the webcam). fps is clamped to the
  // status fpsMax. Unknown/pre-start models fall through to legacy.
  if(CurIsP4&&(CurModel==='OV5647'||CurModel==='USB-UVC')){
    var usb=(CurModel==='USB-UVC');
    var tab=usb?P4_USB_PRESETS:P4_OV5647_PRESETS;
    var pc=tab[n]||tab.vga;
    var ceiling=CurFpsMax||30;
    var fs=pc.fs>ceiling?ceiling:pc.fs;
    if(!usb&&$('selFr'))selectResOption($('selFr'),pc.res);
    if($('slQ'))$('slQ').value=pc.q;if($('selFps'))$('selFps').value=fs;
    if($('slBr'))$('slBr').value=pc.br;if($('slCt'))$('slCt').value=pc.ct;if($('slSa'))$('slSa').value=pc.sa;
    if($('vQ'))$('vQ').textContent=pc.q;if($('vBr'))$('vBr').textContent=pc.br;if($('vCt'))$('vCt').textContent=pc.ct;if($('vSa'))$('vSa').textContent=pc.sa;if($('vFps'))$('vFps').textContent=fs;
    if($('slGc'))$('slGc').value=pc.gc;
    if($('chkAWB'))$('chkAWB').checked=pc.awb;if($('chkAEC'))$('chkAEC').checked=pc.aec;if($('chkAGC'))$('chkAGC').checked=pc.agc;
    document.querySelectorAll('.btns .bp').forEach(function(e){e.classList.remove('on')});
    if(el)el.classList.add('on');
    pCfg((usb?'':'resolution='+encodeURIComponent(pc.res)+'&')+'quality='+pc.q+'&fps='+fs+'&brightness='+pc.br+'&contrast='+pc.ct+'&saturation='+pc.sa+'&awb='+pc.awb+'&aec='+pc.aec+'&agc='+pc.agc+'&gainceiling='+pc.gc);
    return;
  }
  var p={hd:{fr:13,q:10,fs:10,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},vga:{fr:10,q:20,fs:12,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},fast:{fr:6,q:35,fs:25,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},night:{fr:6,q:15,fs:5,br:1,ct:-1,sa:-1,awb:0,aec:1,agc:1,gc:0},safe:{fr:13,q:12,fs:10,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0},eco:{fr:6,q:24,fs:8,br:0,ct:0,sa:0,awb:1,aec:1,agc:1,gc:0}};
  var c=p[n]||p.vga;
  if($('selFr'))$('selFr').value=c.fr;if($('slQ'))$('slQ').value=c.q;if($('selFps'))$('selFps').value=c.fs;
  if($('slBr'))$('slBr').value=c.br;if($('slCt'))$('slCt').value=c.ct;if($('slSa'))$('slSa').value=c.sa;
  if($('vQ'))$('vQ').textContent=c.q;if($('vBr'))$('vBr').textContent=c.br;if($('vCt'))$('vCt').textContent=c.ct;if($('vSa'))$('vSa').textContent=c.sa;if($('vFps'))$('vFps').textContent=c.fs;
  if($('slGc'))$('slGc').value=c.gc;
  if($('chkAWB'))$('chkAWB').checked=c.awb;if($('chkAEC'))$('chkAEC').checked=c.aec;if($('chkAGC'))$('chkAGC').checked=c.agc;
  document.querySelectorAll('.btns .bp').forEach(function(e){e.classList.remove('on')});
  if(el)el.classList.add('on');
  pCfg('framesize='+c.fr+'&quality='+c.q+'&fps='+c.fs+'&brightness='+c.br+'&contrast='+c.ct+'&saturation='+c.sa+'&awb='+c.awb+'&aec='+c.aec+'&agc='+c.agc+'&gainceiling='+c.gc)
}
function loadCfg(){
  fetch('/api/camera/config',{cache:'no-store'}).then(function(r){return r.json()}).then(function(c){
    if(!c)return;
    if($('selFr')){
      PreferredP4Res=c.p4_resolution||'';
      if(PreferredP4Res){
        var matched=selectResOption($('selFr'),PreferredP4Res);
        if(!matched&&LastResList)PreferredP4Res='';
      }else $('selFr').value=c.framesize;
    }
    if($('slQ'))$('slQ').value=c.quality;if($('vQ'))$('vQ').textContent=c.quality;
    if($('selFps'))$('selFps').value=c.fps;if($('vFps'))$('vFps').textContent=c.fps;
    if($('slBr'))$('slBr').value=c.brightness;if($('vBr'))$('vBr').textContent=c.brightness;
    if($('slCt'))$('slCt').value=c.contrast;if($('vCt'))$('vCt').textContent=c.contrast;
    if($('slSa'))$('slSa').value=c.saturation;if($('vSa'))$('vSa').textContent=c.saturation;
    if($('slGc'))$('slGc').value=c.gainceiling;
    if($('chkHM'))$('chkHM').checked=c.hmirror;if($('chkVF'))$('chkVF').checked=c.vflip;
    if($('chkAWB'))$('chkAWB').checked=c.awb;if($('chkAEC'))$('chkAEC').checked=c.aec;if($('chkAec2'))$('chkAec2').checked=c.aec2;if($('chkAGC'))$('chkAGC').checked=c.agc;if($('chkLenc'))$('chkLenc').checked=c.lenc;
    if($('slAgcG'))$('slAgcG').value=c.agc_gain;if($('vAgcG'))$('vAgcG').textContent=(parseInt(c.agc_gain)+1)+'x';
    if($('slAecV'))$('slAecV').value=c.aec_value;if($('vAecV'))$('vAecV').textContent=c.aec_value;
    if($('slAeL'))$('slAeL').value=c.ae_level;if($('vAeL'))$('vAeL').textContent=c.ae_level;
    if($('selSfx'))$('selSfx').value=c.special_effect;if($('selWb'))$('selWb').value=c.wb_mode;
    if(c.motion){
      if($('chkMot'))$('chkMot').checked=c.motion.enabled;
      if($('slMot'))$('slMot').value=c.motion.sensitivity;if($('vMot'))$('vMot').textContent=c.motion.sensitivity;
      if($('slMotCf'))$('slMotCf').value=c.motion.confirm_frames;if($('vMotCf'))$('vMotCf').textContent=c.motion.confirm_frames;
      if($('slMotSf'))$('slMotSf').value=c.motion.stop_frames;if($('vMotSf'))$('vMotSf').textContent=c.motion.stop_frames;
      if($('slMotPs'))$('slMotPs').value=c.motion.post_record_secs;if($('vMotPs'))$('vMotPs').textContent=c.motion.post_record_secs;
      if($('selMotAct')&&c.motion.action!==undefined)$('selMotAct').value=c.motion.action;
      // No email in this build (motionEmailAvail false): drop the "Email alert"
      // option entirely and fold a legacy stored 2 into Snap photo (1).
      if(c.motionEmailAvail===false){
        var ma=$('selMotAct');
        if(ma){
          for(var oi=ma.options.length-1;oi>=0;oi--){if(ma.options[oi].value==='2')ma.remove(oi)}
          if(ma.value==='2'||ma.value===''||ma.selectedIndex<0)ma.value='1';
        }
      }
      if($('slMotCd')){$('slMotCd').value=c.motion.cooldown_secs||0;if($('vMotCd'))$('vMotCd').textContent=c.motion.cooldown_secs||0}
      if($('chkMotLamp')&&c.motion.lamp_auto!==undefined)$('chkMotLamp').checked=c.motion.lamp_auto;
      if($('slRoiX')){$('slRoiX').value=c.motion.roi_x||0;if($('vRoiX'))$('vRoiX').textContent=c.motion.roi_x||0}
      if($('slRoiY')){$('slRoiY').value=c.motion.roi_y||0;if($('vRoiY'))$('vRoiY').textContent=c.motion.roi_y||0}
      if($('slRoiW')){$('slRoiW').value=c.motion.roi_w||0;if($('vRoiW'))$('vRoiW').textContent=c.motion.roi_w||0}
      if($('slRoiH')){$('slRoiH').value=c.motion.roi_h||0;if($('vRoiH'))$('vRoiH').textContent=c.motion.roi_h||0}
    }
    if($('selAuto')&&c.auto_mode!==undefined)$('selAuto').value=c.auto_mode;
    if($('selSrc')&&c.source!==undefined)$('selSrc').value=c.source;
    if($('selCodec')&&c.rec_codec!==undefined)$('selCodec').value=c.rec_codec;
    if($('slH264Br')&&c.h264_bitrate!==undefined)$('slH264Br').value=Math.round(c.h264_bitrate/1000);
    if($('selH264Cont')&&c.h264_container!==undefined)$('selH264Cont').value=c.h264_container;
    if($('selVisMode')&&c.vision_mode!==undefined)$('selVisMode').value=c.vision_mode;
    if($('slVisTh')&&c.vision_thresh!==undefined){$('slVisTh').value=c.vision_thresh;if($('vVisTh'))$('vVisTh').textContent=c.vision_thresh}
    codecUI();
    if($('chkDash'))$('chkDash').checked=c.dashcam_mins>0;if(c.dashcam_mins>0&&$('slDash')){$('slDash').value=c.dashcam_mins;if($('vDash'))$('vDash').textContent=c.dashcam_mins}
    if($('slTl')){$('slTl').value=c.timelapse_secs||0;if($('vTl'))$('vTl').textContent=c.timelapse_secs||0}
    if($('slLamp')){$('slLamp').value=c.lamp||0;if($('vLamp'))$('vLamp').textContent=c.lamp||0}
    var rl=$('rwLamp');if(rl)rl.style.display=(c.lampAvail===false)?'none':'flex';
  })
}

function poll(){
  fetch('/api/camera/status',{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){
    if(d.ready){
      // A single not-ready poll must never kill the view (transient blips:
      // reboot window, session re-sync, init race). Only three consecutive
      // not-ready polls stop the img; a later ready self-heals via strStart.
      NotReadyStrikes=0;
      if(!R){R=true;upd(d);strStart()}else{upd(d);streamWatchdog(d)}
    }else{
      if(R){
        NotReadyStrikes++;
        upd(d);
        if(NotReadyStrikes>=3){R=false;upd({});strStop();NotReadyStrikes=0}
      }else{upd(d)}
    }
  }).catch(function(){});
  setTimeout(poll,PollMs)
}
// Frozen-stream watchdog (CAM-63): the MJPEG img fires no event when the
// connection goes quiet without closing, so watch the monotonic streamFrames
// counter — unchanged across 3 polls (15 s) with the stream wanted means the
// pump stalled, and only an in-page reconnect (no full refresh) revives it.
var LastStreamFrames=-1,FrozenStrikes=0;
function streamWatchdog(d){
  if(!R||!d||!d.ready)return;
  var fr=(d.streamFrames===undefined)?-1:d.streamFrames;
  if(LastStreamFrames>=0&&fr>=0){
    if(fr===LastStreamFrames){FrozenStrikes++;if(FrozenStrikes>=3){FrozenStrikes=0;strStart()}}
    else FrozenStrikes=0;
  }
  LastStreamFrames=fr;
}
if(window.addEventListener)window.addEventListener('pagehide',function(){strStop()});
loadCfg();poll();
</script>
)HTML";
