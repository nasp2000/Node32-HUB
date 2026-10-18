#include "page_structures_asset.h"

const char PAGE_STRUCTURES_BODY[] PROGMEM = R"STRUCTURES(
<main class='page'>
  <div class='wrap'>
    <style>
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
      @media (max-width: 480px) {
        .section-box { padding: 14px; }
      }
    </style>

    <div class='actions nav-inicio'>
      <a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='structTipHome'>&#127968; Home</a>
    </div>

    <h1 data-i18n='structPageTitle'>&#127981; Amada Welding</h1>

    <div id='sec-nav' class='section-box'>
      <h3 class='section-title'>&#129517; Quick Navigation</h3>
      <div style='display:flex;gap:8px;flex-wrap:wrap'>
        <a href='#sec-overview' class='btn' style='padding:8px 14px'>&#8505;&#65039; Overview</a>
        <a href='#sec-config' class='btn' style='padding:8px 14px'>&#9881;&#65039; Configuration</a>
        <a href='#sec-diagnostic' class='btn' style='padding:8px 14px'>&#128200; Diagnostics</a>
        <a href='#sec-errors' class='btn' style='padding:8px 14px'>&#9888;&#65039; Errors</a>
      </div>
    </div>

    <div id='sec-overview' class='section-box'>
      <h3 class='section-title'>1) &#8505;&#65039; Amada Overview</h3>
      
      <div style='display:flex;flex-direction:column;gap:10px'>
        <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:10px 12px;border-radius:4px'>
          <div style='font-size:13px;font-weight:600;color:#c7d2fe;margin-bottom:4px'>&#128269; Auto Detection</div>
          <div style='font-size:12px;color:#94a3b8;line-height:1.5'>
            Automatically detects the data structure as received &mdash; no manual selection required.
            Recommended when using a single Amada machine.
          </div>
        </div>
        <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:10px 12px;border-radius:4px'>
          <div style='font-size:13px;font-weight:600;color:#c7d2fe;margin-bottom:4px'>&#128203; ISQ20 Structures &mdash; MTF1 / AWSE</div>
          <div style='font-size:12px;color:#94a3b8;line-height:1.5'>
            Manual structure selection when auto-detection is disabled.
            <b style='color:#c7d2fe'>ISQ20 MTF1</b> &mdash; RS232 classic mode &nbsp;&middot;&nbsp;
            <b style='color:#c7d2fe'>ISQ20 AWSE</b> &mdash; RS232 AWS3 mode.
          </div>
        </div>
        <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:10px 12px;border-radius:4px'>
          <div style='font-size:13px;font-weight:600;color:#c7d2fe;margin-bottom:4px'>&#9986;&#65039; Filter received headers</div>
          <div style='font-size:12px;color:#94a3b8;line-height:1.5'>
            Discards header lines sent by the Amada machine before processing data.
            Recommended to keep records clean.
          </div>
        </div>
      </div>
    </div>

    <div id='sec-config' class='section-box'>
      <h3 class='section-title'>2) &#9881;&#65039; Amada Settings</h3>

      <div id='amada-rs232-info' style='background:#1e293b;border-radius:6px;padding:8px 14px;margin-bottom:14px;font-size:12px;color:#94a3b8;display:flex;align-items:center;gap:8px;flex-wrap:wrap'>
        <span style='color:#60a5fa'>&#128268; RS232 active:</span>
        <span id='amada-rs232-text'>loading...</span>
        <a href='/rs232' style='margin-left:auto;font-size:11px;color:#60a5fa'>&#9999;&#65039; configurar</a>
      </div>

      <form id='structures-form' action='/api/config' method='POST'>
        <input type='hidden' name='_html' value='1'>
        <input type='hidden' name='structures_present' value='1'>

        <div style='display:flex;flex-direction:column;gap:10px;padding:10px 12px;border:1px solid #3b82f6;border-radius:6px;background:rgba(59,130,246,0.05);align-items:center'>
          <h3 style='margin:0;color:#93c5fd'>&#129529; Parsing Rules</h3>
          <label style='display:flex;align-items:center;gap:6px;cursor:pointer;margin:0'>
            <input type='checkbox' id='structures_auto_detect' name='structures_auto_detect' title='Auto-detect data structure' data-i18n-title='structTipAutoDetect' style='width:14px;height:14px;cursor:pointer'>
            <span style='font-size:13px;color:#e0e7ff' data-i18n='structAutoDetectLbl'>&#128269; Auto Detection</span>
          </label>

          <div style='display:flex;align-items:center;gap:10px;justify-content:center;flex-wrap:wrap'>
            <span style='font-size:12px;color:#60a5fa;white-space:nowrap'>Structures:</span>
            <label style='display:flex;align-items:center;gap:6px;cursor:pointer;margin:0'>
              <input type='checkbox' id='amada_isq20_mtf1_enabled' name='amada_isq20_mtf1_enabled' title='Enable ISQ20 MTF1 structure' data-i18n-title='structTipMtf1' style='width:14px;height:14px;cursor:pointer'>
              <span style='font-size:13px;color:#e0e7ff'>ISQ20 MTF1</span>
            </label>
            <label style='display:flex;align-items:center;gap:6px;cursor:pointer;margin:0'>
              <input type='checkbox' id='amada_isq20_awse_enabled' name='amada_isq20_awse_enabled' title='Enable ISQ20 AWSE structure' data-i18n-title='structTipAwse' style='width:14px;height:14px;cursor:pointer'>
              <span style='font-size:13px;color:#e0e7ff'>ISQ20 AWSE</span>
            </label>
          </div>

          <label style='display:flex;align-items:center;gap:6px;cursor:pointer;margin:0'>
            <input type='checkbox' id='amada_filter_headers' name='amada_filter_headers' title='Discard header lines from machine' data-i18n-title='structTipFilter' style='width:14px;height:14px;cursor:pointer'>
            <span style='font-size:13px;color:#e0e7ff' data-i18n='structFilterLbl'>Filter headers</span>
          </label>

          <div style='display:flex;align-items:center;gap:8px;justify-content:center;flex-wrap:wrap'>
            <span style='font-size:12px;color:#60a5fa;white-space:nowrap'>&#128203; Header every:</span>
            <select id='amada_header_every' name='amada_header_every' title='Repeat header every N lines' data-i18n-title='structTipHeaderEvery' style='width:160px;padding:4px 6px;border-radius:5px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:12px'>
              <option value='0'>Only at start</option>
              <option value='10'>10 linhas</option>
              <option value='20' selected>20 linhas</option>
              <option value='30'>30 linhas</option>
              <option value='50'>50 linhas</option>
              <option value='100'>100 linhas</option>
            </select>
          </div>
        </div>
      </form>
    </div>

    <div id='sec-diagnostic' class='section-box'>
      <h3 class='section-title'>3) &#128200; Quick Parsing Diagnostics</h3>
      <div id='amada-diagnostic' style='display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:8px'>
        <div style='background:#1e293b;padding:10px;border-radius:6px'>
          <div style='font-size:11px;color:#94a3b8'>Detected Structure</div>
          <div id='amada-diag-structure' style='font-size:13px;color:#e2e8f0'>loading...</div>
        </div>
        <div style='background:#1e293b;padding:10px;border-radius:6px'>
          <div style='font-size:11px;color:#94a3b8'>Column Count</div>
          <div id='amada-diag-cols' style='font-size:13px;color:#e2e8f0'>loading...</div>
        </div>
        <div style='background:#1e293b;padding:10px;border-radius:6px'>
          <div style='font-size:11px;color:#94a3b8'>Structure Mode</div>
          <div id='amada-diag-mode' style='font-size:13px;color:#e2e8f0'>loading...</div>
        </div>
      </div>
      <div style='margin-top:10px;font-size:12px;color:#94a3b8'>
        Reading based on <b>/api/rs232-cols</b> and saved configuration.
      </div>
    </div>

    <div id='sec-errors' class='section-box'>
      <h3 class='section-title'>4) &#9888;&#65039; Quick Amada Error Dictionary</h3>
      <div class='table-wrapper'>
        <table style='width:100%;border-collapse:collapse;font-size:12px'>
          <thead>
            <tr>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #334155;color:#93c5fd'>Code</th>
              <th style='text-align:left;padding:8px;border-bottom:1px solid #334155;color:#93c5fd'>Meaning</th>
            </tr>
          </thead>
          <tbody>
            <tr><td style='padding:8px;border-bottom:1px solid #1e293b'>61</td><td style='padding:8px;border-bottom:1px solid #1e293b'>Error pressure switch 1</td></tr>
            <tr><td style='padding:8px;border-bottom:1px solid #1e293b'>70</td><td style='padding:8px;border-bottom:1px solid #1e293b'>No external program selected</td></tr>
            <tr><td style='padding:8px;border-bottom:1px solid #1e293b'>73</td><td style='padding:8px;border-bottom:1px solid #1e293b'>Error thermal switch</td></tr>
            <tr><td style='padding:8px;border-bottom:1px solid #1e293b'>74</td><td style='padding:8px;border-bottom:1px solid #1e293b'>Mains compressed air error</td></tr>
            <tr><td style='padding:8px;border-bottom:1px solid #1e293b'>75</td><td style='padding:8px;border-bottom:1px solid #1e293b'>Quick stop error</td></tr>
            <tr><td style='padding:8px;border-bottom:1px solid #1e293b'>76</td><td style='padding:8px;border-bottom:1px solid #1e293b'>Error pressure switch 2</td></tr>
            <tr><td style='padding:8px;border-bottom:1px solid #1e293b'>83</td><td style='padding:8px;border-bottom:1px solid #1e293b'>No program section AC chosen (action counter)</td></tr>
          </tbody>
        </table>
      </div>
      <div style='margin-top:10px;font-size:12px;color:#94a3b8'>
        Quick reference for operation; full validation should follow the machine manual.
      </div>
    </div>

    <div style='text-align:center;margin-top:20px'>
      <button type='submit' form='structures-form' class='btn' title='Save structure settings' data-i18n-title='structTipSave' style='padding:10px 24px' data-i18n='btnSave'>&#128190; Save</button>
    </div>

    <script>
      function sst(k, fb) {
        try {
          if (typeof window._t === 'function') return window._t(k, fb);
          var raw = localStorage.getItem('ui_lang_cache');
          if (raw) {
            var parsed = JSON.parse(raw);
            if (parsed && parsed[k] !== undefined) return parsed[k];
          }
        } catch (err) {}
        return fb !== undefined ? fb : k;
      }

      function el(id) {
        return document.getElementById(id);
      }

      function syncManualState() {
        var ad = el('structures_auto_detect');
        var c1 = el('amada_isq20_mtf1_enabled');
        var c2 = el('amada_isq20_awse_enabled');
        var on = ad && ad.checked;
        if (c1) c1.disabled = on;
        if (c2) c2.disabled = on;
      }

      function setDiag(id, text, color) {
        var node = el(id);
        if (!node) return;
        node.textContent = text;
        if (color) node.style.color = color;
      }

      function refreshDiagnostic(savedConfigs) {
        fetch('/api/rs232-cols?t=' + Date.now(), { cache: 'no-cache' })
          .then(function(r) { return r.json(); })
          .then(function(d) {
            var cols = d && typeof d.cols !== 'undefined' ? Number(d.cols) : 0;
            var structure = d && d.structure ? String(d.structure) : 'unknown';

            setDiag('amada-diag-cols', String(cols), '#e2e8f0');
            setDiag('amada-diag-structure', structure, '#e2e8f0');

            var mode = 'Manual';
            if (savedConfigs && savedConfigs.structures && savedConfigs.structures.auto_detect) {
              mode = 'Auto detect';
            }
            setDiag('amada-diag-mode', mode, '#e2e8f0');

            if (structure === 'amada_isq20_awse' || structure === 'amada_isq20_mtf1') {
              setDiag('amada-diag-structure', structure + ' (ok)', '#4ade80');
            } else if (cols > 0) {
              setDiag('amada-diag-structure', structure + ' (unrecognized)', '#facc15');
            }
          })
          .catch(function() {
            setDiag('amada-diag-structure', 'read error', '#f87171');
            setDiag('amada-diag-cols', 'read error', '#f87171');
            setDiag('amada-diag-mode', 'read error', '#f87171');

          });
      }

      function bindEvents() {
        var ad = el('structures_auto_detect');
        var c1 = el('amada_isq20_mtf1_enabled');
        var c2 = el('amada_isq20_awse_enabled');

        if (ad) {
          ad.addEventListener('change', function() {
            syncManualState();
          });
        }

        if (c1 && c2) {
          c1.addEventListener('change', function() {
            var auto = el('structures_auto_detect');
            if (auto && auto.checked) return;
            if (c1.checked) c2.checked = false;
          });
          c2.addEventListener('change', function() {
            var auto = el('structures_auto_detect');
            if (auto && auto.checked) return;
            if (c2.checked) c1.checked = false;
          });
        }
      }

      function loadPageData() {
        fetch('/api/config?section=system&t=' + Date.now(), { cache: 'no-cache' })
          .then(function(r) { return r.json(); })
          .then(function(d) {
            var s = d && d.structures ? d.structures : null;
            var ad = s && typeof s.auto_detect !== 'undefined' ? !!s.auto_detect : true;
            var e1 = s && s.amada_isq20_mtf1 ? !!s.amada_isq20_mtf1.enabled : false;
            var e2 = s && s.amada_isq20_awse ? !!s.amada_isq20_awse.enabled : false;
            var afh = s && typeof s.amada_filter_headers !== 'undefined' ? !!s.amada_filter_headers : true;
            var ahe = s && typeof s.amada_header_every !== 'undefined' ? s.amada_header_every : 20;

            var cad = el('structures_auto_detect');
            if (cad) cad.checked = ad;
            var c1 = el('amada_isq20_mtf1_enabled');
            if (c1) c1.checked = e1;
            var c2 = el('amada_isq20_awse_enabled');
            if (c2) c2.checked = e2;
            var c4 = el('amada_filter_headers');
            if (c4) c4.checked = afh;
            var c5 = el('amada_header_every');
            if (c5) c5.value = String(ahe);

            syncManualState();

            var saved = d && d.saved_configs ? d.saved_configs : {};
            var baudList = Array.isArray(saved.rs232_baudrate_list) ? saved.rs232_baudrate_list : [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];
            var idx = saved.rs232_baudrate_index !== undefined ? saved.rs232_baudrate_index : 3;
            var baud = baudList[idx] || 9600;
            var db = saved.rs232_data_bits || 8;
            var par = (saved.rs232_parity || 'none').charAt(0).toUpperCase();
            var sb = saved.rs232_stop_bits || 1;
            var ena = !!saved.rs232_enabled;
            var ri = el('amada-rs232-text');
            if (ri) {
              ri.innerHTML =
                '<strong style="color:#e0e7ff">' + baud + ' baud &middot; ' + db + par + sb + '</strong> &mdash; ' +
                (ena ? '<span style="color:#4ade80">&#9679; active</span>' : '<span style="color:#f87171">&#9679; inactive</span>');
            }

            refreshDiagnostic(saved);
          })
          .catch(function() {
            refreshDiagnostic(null);
          });
      }

      el('structures-form').addEventListener('submit', function(e) {
        e.preventDefault();
        var btn=this.querySelector('button[type="submit"]');disableBtn(btn);showLoading();
        var fd = new FormData(this);
        fd.set('section','system');
        fetch(this.action, {
          method: 'POST',
          body: new URLSearchParams(fd),
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
        })
          .then(function(r) { return r.json(); })
          .then(function(d) {
            if (d.success) {
              customAlert(d.message || sst('alertSaved', '✅ Configuration saved!'));
              if (typeof showSavedCard === 'function') showSavedCard();
              refreshDiagnostic(null);
            } else {
              customAlert(d.message || sst('alertSaveError', '❌ Error saving'));
            }
          })
          .catch(function(e2) {
            customAlert(sst('emailErrorPrefix', 'Error: ') + (e2 && e2.message ? e2.message : e2));
          })
          .finally(function(){hideLoading();enableBtn(btn);});
      });

      bindEvents();
      syncManualState();
      loadPageData();
    </script>
  </div>
</main>
)STRUCTURES";