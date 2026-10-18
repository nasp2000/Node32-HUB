// page_audio_dac_uda1334a_asset.cpp
// PROGMEM body fragment for the UDA1334A configuration page.

#include "page_audio_dac_uda1334a_asset.h"
#include "../../feature_flags.h"

#if ENABLE_UDA1334A == 1

const char PAGE_AUDIO_DAC_UDA1334A[] PROGMEM = R"HTML(
<div class='page audio-dac-uda1334a'>
  <div class='wrap'>
    <div class='actions nav-inicio'>
      <a href='/' class='btn' data-i18n='btnHome' title='Go to home page' data-i18n-title='udaTipHome'>&#127968; Home</a>
    </div>

    <h1 data-i18n='uda1334aPageTitle'>&#128266; Placa de Som UDA1334A</h1>

    <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
      <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='rs232StatusTitle'>&#128202; Status</h2>
      <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px'>
        <div><strong data-i18n='uda1334aInitialized'>Inicializado:</strong> <span id='initialized-status'>-</span></div>
        <div><strong data-i18n='uda1334aSampleRate'>Sampling Rate:</strong> <span id='current-sr'>-</span> Hz</div>
        <div><strong data-i18n='uda1334aBitDepth'>Profundidade de Bits:</strong> <span id='current-bd'>-</span> bits</div>
        <div><strong data-i18n='uda1334aEnabled'>Ativado:</strong> <span id='enabled-status'>-</span></div>
      </div>
    </div>

    <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
      <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='ethernetConfigTitle'>&#9881;&#65039; Configuration</h2>
      <div style='background:#1e3a8a;border-left:4px solid #3b82f6;padding:12px;margin:0 0 16px 0;font-size:13px;line-height:1.8;color:#e0e7ff;border-radius:4px;text-align:left' data-i18n-html='uda1334aInfo'>
        <div data-i18n-html='udaIntroUda'>🔊 <strong style='color:#60a5fa'>UDA1334A:</strong> Digital-to-analog converter for I2S audio playback.</div>
        <div data-i18n-html='udaIntroRate'>🎧 <strong style='color:#60a5fa'>Rate/bit depth:</strong> Adjust the stream quality to the audio source.</div>
        <div data-i18n-html='udaIntroRoute'>🔁 <strong style='color:#60a5fa'>Automatic routing:</strong> Starts playback when eligible data arrives.</div>
      </div>
      <form id='uda-config-form'>
        <div style='margin-top:12px'>

        <div style='margin-top:16px'>
          <label for='uda-sr' style='display:block;margin-bottom:8px;color:#60a5fa;font-weight:bold' data-i18n='uda1334aSampleRateHz'>Sampling Rate (Hz)</label>
          <select id='uda-sr' name='sampleRate' required title='Select audio sample rate' data-i18n-title='udaTipSampleRate' style='width:100%;max-width:420px;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
            <option value='44100' data-i18n='uda1334aSr44100'>44,100 Hz (CD Quality)</option>
            <option value='48000' data-i18n='uda1334aSr48000'>48,000 Hz (Audio profissional)</option>
          </select>
        </div>

        <div style='margin-top:16px'>
          <label for='uda-bd' style='display:block;margin-bottom:8px;color:#60a5fa;font-weight:bold' data-i18n='uda1334aBitDepth'>Profundidade de Bits</label>
          <select id='uda-bd' name='bitDepth' required title='Select audio bit depth' data-i18n-title='udaTipBitDepth' style='width:100%;max-width:420px;padding:10px;border-radius:6px;border:1px solid #3b82f6;background:#020617;color:#e0e7ff;font-size:14px'>
            <option value='16' data-i18n='uda1334aBd16'>16 bits (Standard)</option>
            <option value='24' data-i18n='uda1334aBd24'>24 bits (Studio quality)</option>
          </select>
        </div>

        <div style='margin-top:16px'>
          <label for='uda-auto-routing' style='display:block;margin-bottom:8px;color:#60a5fa;font-weight:bold' data-i18n='uda1334aAutoRouting'>Automatic Routing</label>
          <input type='checkbox' id='uda-auto-routing' name='autoRoutingEnabled' title='Auto-play when audio data received' data-i18n-title='udaTipAutoRouting' />
          <div style='margin-top:6px;font-size:12px;color:#94a3b8' data-i18n='uda1334aAutoRoutingDesc'>Play audio automatically when data is received</div>
        </div>

        <div class='actions' style='margin-top:20px;display:flex;gap:12px;justify-content:center;flex-wrap:wrap'>
          <button type='button' class='btn' id='uda-test-btn' style='padding:10px 24px' data-i18n='uda1334aTestBtn' title='Play a test sound' data-i18n-title='udaTipTest'>&#128265; Teste de Som</button>
        </div>
      </form>
    </div>

    <div style='border:2px solid #3b82f6;border-radius:8px;padding:20px;background:#0f172a;margin-top:20px'>
      <h2 style='margin-top:0;font-size:18px;color:#60a5fa' data-i18n='infoTitle'>&#8505;&#65039; Information</h2>
      <div style='font-size:13px;line-height:1.8;color:#e0e7ff;text-align:left' data-i18n='uda1334aInfo'>
        The UDA1334A is a passive digital-to-analog converter (DAC) for high-quality audio playback.
        On the ESP32, 44.1/48 kHz rates with 16 or 24 bits work reliably.
      </div>
    </div>

    <div class='actions' style='margin-top:20px;display:flex;gap:12px;justify-content:center;flex-wrap:wrap;margin-bottom:80px'>
      <button type='button' class='btn' id='uda-save-btn' style='padding:10px 24px' data-i18n='btnSave' title='Save DAC configuration' data-i18n-title='udaTipSave'>&#128190; Save</button>
    </div>

  </div>
</div>

<script>
function wt(k, fb) {
  try {
    if (typeof window._t === 'function') return window._t(k, fb);
    var raw = localStorage.getItem('ui_lang_cache');
    if (raw) {
      var parsed = JSON.parse(raw);
      if (parsed && parsed[k] !== undefined) return parsed[k];
    }
  } catch (e) {}
  return fb !== undefined ? fb : k;
}

function loadUDAConfig() {
  return fetch('/api/uda1334a-config', { cache:'no-store' })
    .then(function(resp) { return resp.json(); })
    .then(function(data) {
      document.getElementById('uda-sr').value = String(data.sampleRate || 44100);
      document.getElementById('uda-bd').value = String(data.bitDepth || 16);
      document.getElementById('uda-auto-routing').checked = !!data.autoRoutingEnabled;

      document.getElementById('initialized-status').textContent = data.initialized ? wt('opYes', 'Sim') : wt('opNo', 'Nao');
      document.getElementById('current-sr').textContent = data.sampleRate || 44100;
      document.getElementById('current-bd').textContent = data.bitDepth || 16;
      document.getElementById('enabled-status').textContent = data.enabled ? wt('opYes', 'Sim') : wt('opNo', 'Nao');
    })
    .catch(function() {
      customAlert('❌ ' + wt('uda1334aLoadError', 'Error loading configuration'));
    });
}

document.getElementById('uda-save-btn').addEventListener('click', function() {
  var btn=this;disableBtn(btn);showLoading();
  const form = document.getElementById('uda-config-form');
  const formData = new FormData(form);

  const payload = {
    sampleRate: parseInt(formData.get('sampleRate')),
    bitDepth: parseInt(formData.get('bitDepth')),
    autoRoutingEnabled: formData.get('autoRoutingEnabled') === 'on'
  };

  fetch('/api/uda1334a-config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
    .then(function(resp) { return resp.json(); })
    .then(function(result) {
      if (result.success) {
        customAlert(wt('alertSaved', '✅ Configuration saved!'));
        showSavedCard();
        loadUDAConfig();
      } else {
        customAlert('❌ ' + wt('emailErrorPrefix', 'Error: ') + (result.message || wt('uda1334aSaveFailDefault', 'Save failed')));
      }
    })
    .catch(function() {
      customAlert('❌ ' + wt('uda1334aSaveError', 'Error saving configuration'));
    })
    .finally(function(){hideLoading();enableBtn(btn);});
});

document.getElementById('uda-test-btn').addEventListener('click', function() {
  var btn=this;disableBtn(btn);showLoading();
  fetch('/api/uda1334a-test', { method: 'POST' })
    .then(function(resp) { return resp.json(); })
    .then(function(result) {
      if (result.success) {
        customAlert(wt('uda1334aTestRunning', '✅ Teste de som em execucao...'));
      } else {
        customAlert('❌ ' + wt('emailErrorPrefix', 'Error: ') + (result.message || wt('uda1334aTestFailDefault', 'Test failed')));
      }
    })
    .catch(function() {
      customAlert('❌ ' + wt('uda1334aTestError', 'Error running test'));
    })
    .finally(function(){hideLoading();enableBtn(btn);});
});

window.addEventListener('load', loadUDAConfig);
</script>
)HTML";

#endif // ENABLE_UDA1334A == 1

