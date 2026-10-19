#pragma once
#include "../../feature_flags.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

// /ecra displays the actual LCD framebuffer; there is no separately-rendered
// HTML approximation that can drift from the device UI.
static const char ECRA_PAGE[] PROGMEM = R"rawliteral(
<div class="wrap" id="ecraWrap">
<h1>TouchMeteo-7 screen mirror</h1>
<p>Live copy of the LCD framebuffer (800 × 480). Click or touch the image to interact with the real display.</p>
<section class="card" id="ecraMirrorCard" aria-label="Live LCD mirror">
  <div class="ecra-mirror-wrap">
    <canvas id="ecraCanvas" width="800" height="480" aria-label="TouchMeteo-7 display"></canvas>
  </div>
  <div class="ecra-mirror-bar">
    <button class="btn" id="ecMirrorPlay" type="button" title="Pause or resume the screen mirror" data-i18n-title="ecraTipMirrorPlay">Pause</button>
    <label for="ecMirrorRate">Refresh</label>
    <select id="ecMirrorRate">
      <option value="500">2 fps</option>
      <option value="1000" selected>1 fps</option>
      <option value="2000">0.5 fps</option>
      <option value="4000">0.25 fps</option>
    </select>
    <span id="ecMirrorInfo" role="status" aria-live="polite">Connecting…</span>
  </div>
  <p class="ecra-hint">The image is taken from the LCD framebuffer. Touch coordinates are mapped back to the 800 × 480 screen.</p>
</section>

<details class="card ecra-tools" id="ecraGeoTools">
  <summary>Weather location settings</summary>
  <p>These controls configure the forecast location; they are not part of the mirrored LCD image. Saving requests fresh weather for the chosen coordinates. Open the LCD's AGORA or PREVISÃO tab in the mirror to see current conditions and the five-day forecast.</p>
  <div class="ecra-geo-current">Current location: <span id="ecPlace">—</span></div>
  <div class="ecra-geo-form">
    <input id="ecGeoQ" maxlength="60" placeholder="Town or city" autocomplete="off">
    <button class="btn" id="ecGeoGo" type="button" title="Search the address" data-i18n-title="ecraTipGeoGo">Search</button>
    <button class="btn" id="ecGeoAuto" type="button" title="Detect the location automatically" data-i18n-title="ecraTipGeoAuto">Detect location</button>
  </div>
  <div id="ecGeoRes" aria-live="polite"></div>
  <div class="ecra-weather-tools">
    <div id="ecWeatherStatus" role="status" aria-live="polite">Checking forecast…</div>
    <pre id="ecWeatherSummary"></pre>
    <button class="btn" id="ecWeatherRefresh" type="button" title="Refresh the weather forecast" data-i18n-title="ecraTipWeather">Refresh weather</button>
  </div>
</details>
</div>
<style>
#ecraWrap{max-width:1120px;margin:0 auto}
.ecra-mirror-wrap{width:min(100%,960px);aspect-ratio:5/3;margin:0 auto;background:#000;outline:2px solid #3b82f6;border-radius:8px;overflow:hidden}
#ecraCanvas{width:100%;height:100%;display:block;background:#000;image-rendering:pixelated;touch-action:none;cursor:crosshair}
.ecra-mirror-bar{display:flex;gap:10px;align-items:center;justify-content:center;flex-wrap:wrap;margin-top:14px}
.ecra-mirror-bar select{min-width:100px}
#ecMirrorInfo{min-width:110px;text-align:center}
.ecra-hint,.ecra-tools p{font-size:13px;opacity:.82;margin:10px 0 0}
.ecra-tools{margin-top:14px}
.ecra-tools summary{font-weight:600;cursor:pointer}
.ecra-geo-current{margin:12px 0}
.ecra-geo-form{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.ecra-geo-form input{min-width:180px;flex:1}
#ecGeoRes{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}
.ecra-weather-tools{margin-top:14px;padding-top:12px;border-top:1px solid #64748b}
#ecWeatherStatus{font-weight:600}
#ecWeatherSummary{white-space:pre-wrap;font:inherit;margin:8px 0;opacity:.88}
@media(max-width:600px){.ecra-mirror-bar{justify-content:flex-start}.ecra-mirror-wrap{outline-width:1px}}
</style>
<script>
var ecMirrorTimer = null, ecMirrorOn = true, ecMirrorRate = 1000, ecMirrorBusy = false;
var ecMirrorHash = '';
var ecraImageData = null;
var ecWeatherTimer = null, ecWeatherPollCount = 0;
var ecPointerActive = false, ecPointerId = null, ecPointerLastMs = 0;
var ecPointerLast = null, ecPointerHeartbeat = null;

function ecraFrameUrlForHash(hash){
  var url = '/api/touch7-frame?t=' + Date.now();
  if(hash) url += '&hash=' + encodeURIComponent(hash);
  return url;
}
function ecraFrameUrl(){ return ecraFrameUrlForHash(ecMirrorHash); }
function ecraCanvasPoint(rect, clientX, clientY){
  if(!rect || rect.width <= 0 || rect.height <= 0) return null;
  var x = Math.floor((clientX - rect.left) * 800 / rect.width);
  var y = Math.floor((clientY - rect.top) * 480 / rect.height);
  if(x < 0) x = 0; if(x > 799) x = 799;
  if(y < 0) y = 0; if(y > 479) y = 479;
  return {x:x, y:y};
}
function ecraWriteRgb565(rgba, offset, value){
  var r = (value >>> 11) & 0x1F;
  var g = (value >>> 5) & 0x3F;
  var b = value & 0x1F;
  rgba[offset] = (r << 3) | (r >>> 2);
  rgba[offset + 1] = (g << 2) | (g >>> 4);
  rgba[offset + 2] = (b << 3) | (b >>> 2);
  rgba[offset + 3] = 255;
}
function ecraDrawFrame(buf){
  var cv = document.getElementById('ecraCanvas');
  if(!cv || !buf || buf.byteLength !== 800 * 480 * 2) return false;
  var ctx = cv.getContext('2d'); if(!ctx) return false;
  if(!ecraImageData) ecraImageData = ctx.createImageData(800, 480);
  var img = ecraImageData;
  var d = img.data;
  // ESP32 and supported browsers use little-endian RGB565 framebuffer words.
  var u16 = new Uint16Array(buf);
  for(var i = 0, j = 0; i < u16.length; i++, j += 4){
    ecraWriteRgb565(d, j, u16[i]);
  }
  ctx.putImageData(img, 0, 0);
  return true;
}
function ecraTap(pt){
  if(!pt) return;
  fetch('/api/touch7-tap?x=' + pt.x + '&y=' + pt.y, {method:'POST'}).catch(function(){});
}
function ecraPointerUrl(pt, state){
  return '/api/touch7-tap?state=' + state + '&x=' + pt.x + '&y=' + pt.y;
}
function ecraSendPointer(pt, state){
  if(!pt) return;
  fetch(ecraPointerUrl(pt, state), {method:'POST'}).catch(function(){});
}
function ecraPointerDown(ev, cv){
  if(ev.pointerType === 'mouse' && ev.button !== 0) return;
  ev.preventDefault();
  ecPointerActive = true;
  ecPointerId = ev.pointerId;
  ecPointerLast = ecraCanvasPoint(cv.getBoundingClientRect(), ev.clientX, ev.clientY);
  ecPointerLastMs = Date.now();
  ecraSendPointer(ecPointerLast, 'down');
  if(cv.setPointerCapture){ try { cv.setPointerCapture(ev.pointerId); } catch(e) {} }
  if(ecPointerHeartbeat) clearInterval(ecPointerHeartbeat);
  ecPointerHeartbeat = setInterval(function(){
    if(ecPointerActive && ecPointerLast && Date.now() - ecPointerLastMs >= 180){
      ecPointerLastMs = Date.now();
      ecraSendPointer(ecPointerLast, 'move');
    }
  }, 200);
}
function ecraPointerMove(ev, cv){
  if(!ecPointerActive || ev.pointerId !== ecPointerId) return;
  ev.preventDefault();
  var now = Date.now();
  if(now - ecPointerLastMs < 40) return;
  ecPointerLastMs = now;
  ecPointerLast = ecraCanvasPoint(cv.getBoundingClientRect(), ev.clientX, ev.clientY);
  ecraSendPointer(ecPointerLast, 'move');
}
function ecraPointerUp(ev, cv){
  if(!ecPointerActive || ev.pointerId !== ecPointerId) return;
  ev.preventDefault();
  var pt = ecraCanvasPoint(cv.getBoundingClientRect(), ev.clientX, ev.clientY) || ecPointerLast;
  ecPointerActive = false;
  ecPointerId = null;
  ecPointerLast = null;
  if(ecPointerHeartbeat){ clearInterval(ecPointerHeartbeat); ecPointerHeartbeat = null; }
  ecraSendPointer(pt, 'up');
}
function ecraPoll(){
  if(!ecMirrorOn || ecMirrorBusy) return;
  ecMirrorBusy = true;
  fetch(ecraFrameUrl(), {cache:'no-store'}).then(function(r){
    if(r.status === 304) return null;
    if(!r.ok) throw new Error('HTTP ' + r.status);
    var hash = r.headers && r.headers.get ? r.headers.get('X-Frame-Hash') : '';
    return r.arrayBuffer().then(function(buf){ return {buffer:buf, hash:hash}; });
  }).then(function(frame){
    if(frame === null) {
      var unchanged = document.getElementById('ecMirrorInfo');
      if(unchanged) unchanged.textContent = 'LIVE · unchanged';
      return;
    }
    if(!ecraDrawFrame(frame.buffer)) throw new Error('Invalid LCD frame');
    if(frame.hash) ecMirrorHash = frame.hash;
    var inf = document.getElementById('ecMirrorInfo');
    if(inf) inf.textContent = 'LIVE · ' + (1000 / ecMirrorRate) + ' fps';
  }).catch(function(){
    var inf = document.getElementById('ecMirrorInfo');
    if(inf) inf.textContent = 'Mirror unavailable';
  }).then(function(){ ecMirrorBusy = false; });
}
function ecraMirrorStart(){
  if(ecMirrorTimer) clearInterval(ecMirrorTimer);
  ecraPoll();
  ecMirrorTimer = setInterval(ecraPoll, ecMirrorRate);
}
function ecraMirrorToggle(){
  ecMirrorOn = !ecMirrorOn;
  var b = document.getElementById('ecMirrorPlay');
  if(b) b.textContent = ecMirrorOn ? 'Pause' : 'Resume';
  if(ecMirrorOn) ecraMirrorStart();
  else if(ecMirrorTimer){ clearInterval(ecMirrorTimer); ecMirrorTimer = null; }
}

function ecraGeoShow(place, lat, lon){
  var el = document.getElementById('ecPlace');
  if(el) el.textContent = place + ' (' + Number(lat).toFixed(2) + ', ' + Number(lon).toFixed(2) + ')';
}
function ecraWeatherRender(d){
  var status = document.getElementById('ecWeatherStatus');
  var summary = document.getElementById('ecWeatherSummary');
  var age = Math.max(0, Number(d.age_sec) || 0);
  var ageText = age < 60 ? age + ' s' : Math.floor(age / 60) + ' min';
  if(status){
    if(d.refreshing) status.textContent = 'Fetching forecast…';
    else if(d.has) status.textContent = (d.stale ? 'Stale' : 'Updated') + ' · ' + ageText + ' ago' + (d.last_error ? ' · ' + d.last_error : '');
    else status.textContent = d.last_error || 'No forecast data yet.';
  }
  if(summary){
    var lines = [];
    if(d.today) lines.push(d.today);
    if(d.days && d.days.length) lines = lines.concat(d.days);
    summary.textContent = lines.join('\n');
  }
}
function ecraWeatherStopWatch(){
  if(ecWeatherTimer){ clearInterval(ecWeatherTimer); ecWeatherTimer = null; }
  ecWeatherPollCount = 0;
}
function ecraWeatherPollTick(){
  ecWeatherPollCount++;
  if(ecWeatherPollCount > 20){
    ecraWeatherStopWatch();
    var status = document.getElementById('ecWeatherStatus');
    if(status) status.textContent = 'Weather update is taking longer than expected.';
    return;
  }
  ecraWeatherLoad();
}
function ecraWeatherStartWatch(){
  ecraWeatherStopWatch();
  ecWeatherTimer = setInterval(ecraWeatherPollTick, 1000);
  ecraWeatherLoad();
}
function ecraWeatherRefreshUrl(){ return '/api/touch7-weather-refresh'; }
function ecraWeatherLoad(){
  return fetch('/api/touch7-forecast', {cache:'no-store'}).then(function(r){
    if(!r.ok) throw new Error('Weather status unavailable');
    return r.json();
  }).then(function(d){
    ecraWeatherRender(d);
    var done = !d.refreshing && ((d.has && !d.stale) || d.last_error);
    if(done) ecraWeatherStopWatch();
    else if(!ecWeatherTimer && (d.refreshing || d.stale || !d.has)) ecraWeatherStartWatch();
    return d;
  }).catch(function(){
    var status = document.getElementById('ecWeatherStatus');
    if(status) status.textContent = 'Weather status unavailable.';
    return null;
  });
}
function ecraWeatherRefresh(){
  var status = document.getElementById('ecWeatherStatus');
  if(status) status.textContent = 'Requesting weather update…';
  fetch(ecraWeatherRefreshUrl(), {method:'POST'}).then(function(r){ return r.json(); }).then(function(d){
    if(!d.success) throw new Error('Refresh request failed');
    ecraWeatherStartWatch();
  }).catch(function(){
    if(status) status.textContent = 'Could not request weather update.';
  });
}
function ecraGeoSave(lat, lon, place){
  fetch('/api/touch7-config').then(function(r){
    if(!r.ok) throw new Error('Configuration unavailable');
    return r.json();
  }).then(function(d){
    if(d.wx_lat === undefined || d.wx_lon === undefined) throw new Error('Configuration unavailable');
    d.wx_lat = lat; d.wx_lon = lon; d.wx_place = place;
    return fetch('/api/touch7-config', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(d)});
  }).then(function(r){
    if(!r.ok) throw new Error('Save failed');
    return r.json();
  }).then(function(d){
    if(!d.success) throw new Error('Save failed');
    ecraGeoShow(place, lat, lon);
    ecraWeatherStartWatch();
    customAlert('Forecast location saved. The device is fetching the new forecast.');
  }).catch(function(){ customAlert('Could not save the forecast location.'); });
}
function ecraGeoLabel(p){
  var s = p.name || '';
  var rest = p.admin1 ? (p.admin1 + ', ' + (p.country || '')) : (p.country || '');
  if(rest) s += ' — ' + rest;
  return s;
}
function ecraGeoRenderResults(list){
  var box = document.getElementById('ecGeoRes'); if(!box) return;
  box.innerHTML = '';
  if(!list || !list.length){ box.textContent = 'No results.'; return; }
  list.forEach(function(p){
    var b = document.createElement('button'); b.className = 'btn';
    b.textContent = ecraGeoLabel(p);
    b.onclick = function(){ ecraGeoSave(p.lat, p.lon, p.name); };
    box.appendChild(b);
  });
}
function ecraGeoSearch(){
  var q = document.getElementById('ecGeoQ'); if(!q || !q.value.trim()) return;
  var box = document.getElementById('ecGeoRes'); if(box) box.textContent = 'Searching…';
  fetch('/api/touch7-geo?name=' + encodeURIComponent(q.value.trim())).then(function(r){ return r.json(); }).then(function(d){
    if(!d.success) throw new Error('Search failed');
    ecraGeoRenderResults(d.results);
  }).catch(function(){ if(box) box.textContent = 'Search unavailable. Try again.'; });
}
function ecraGeoAuto(){
  var box = document.getElementById('ecGeoRes'); if(box) box.textContent = 'Detecting…';
  fetch('/api/touch7-autoloc').then(function(r){ return r.json(); }).then(function(d){
    if(!d.success) throw new Error('Detection failed');
    if(box) box.innerHTML = '';
    ecraGeoSave(d.lat, d.lon, d.city || d.country || 'Auto');
  }).catch(function(){ if(box) box.textContent = 'Location detection unavailable.'; });
}
function ecraLoadGeo(){
  fetch('/api/touch7-config').then(function(r){ return r.json(); }).then(function(d){
    if(d.wx_place !== undefined) ecraGeoShow(d.wx_place, d.wx_lat, d.wx_lon);
  }).catch(function(){});
}

(function(){
  var cv = document.getElementById('ecraCanvas');
  if(cv && window.PointerEvent){
    cv.addEventListener('pointerdown', function(ev){ ecraPointerDown(ev, cv); });
    cv.addEventListener('pointermove', function(ev){ ecraPointerMove(ev, cv); });
    cv.addEventListener('pointerup', function(ev){ ecraPointerUp(ev, cv); });
    cv.addEventListener('pointercancel', function(ev){ ecraPointerUp(ev, cv); });
    cv.addEventListener('lostpointercapture', function(ev){ ecraPointerUp(ev, cv); });
  } else if(cv){
    cv.addEventListener('click', function(ev){
      ecraTap(ecraCanvasPoint(cv.getBoundingClientRect(), ev.clientX, ev.clientY));
    });
  }
  var play = document.getElementById('ecMirrorPlay');
  if(play) play.onclick = ecraMirrorToggle;
  var rate = document.getElementById('ecMirrorRate');
  if(rate) rate.onchange = function(){
    ecMirrorRate = parseInt(rate.value, 10) || 1000;
    if(ecMirrorOn) ecraMirrorStart();
  };
  var search = document.getElementById('ecGeoGo');
  if(search) search.onclick = ecraGeoSearch;
  var detect = document.getElementById('ecGeoAuto');
  if(detect) detect.onclick = ecraGeoAuto;
  var refreshWeather = document.getElementById('ecWeatherRefresh');
  if(refreshWeather) refreshWeather.onclick = ecraWeatherRefresh;
  var query = document.getElementById('ecGeoQ');
  if(query) query.onkeydown = function(ev){ if(ev && ev.key === 'Enter') ecraGeoSearch(); };
  ecraLoadGeo();
  ecraWeatherLoad();
  ecraMirrorStart();
})();

window.__test__ = window.__test__ || {};
window.__test__.ecraFrameUrl = ecraFrameUrl;
window.__test__.ecraFrameUrlForHash = ecraFrameUrlForHash;
window.__test__.ecraCanvasPoint = ecraCanvasPoint;
window.__test__.ecraWriteRgb565 = ecraWriteRgb565;
window.__test__.ecraDrawFrame = ecraDrawFrame;
window.__test__.ecraGeoShow = ecraGeoShow;
window.__test__.ecraPointerUrl = ecraPointerUrl;
window.__test__.ecraWeatherRender = ecraWeatherRender;
window.__test__.ecraWeatherRefreshUrl = ecraWeatherRefreshUrl;
</script>
)rawliteral";

#endif
