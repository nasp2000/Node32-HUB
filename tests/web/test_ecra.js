// TouchMeteo-7 exact framebuffer mirror tests.
var { JSDOM } = require('jsdom');
var fs = require('fs');
var path = require('path');

var PAGE = path.join(__dirname, 'pages', 'ecra_asset.html');
var passed = 0, failed = 0;

function test(name, fn) {
  try { fn(); passed++; console.log('  \u2713 ' + name); }
  catch (e) { failed++; console.log('  \u2717 ' + name + ': ' + e.message); }
}
function is(a, b, m) { if (a !== b) throw new Error(m || ('expected ' + JSON.stringify(b) + ' got ' + JSON.stringify(a))); }
function ok(v, m) { if (!v) throw new Error(m || 'assertion failed'); }

function run() {
  var html = fs.readFileSync(PAGE, 'utf8');
  var dom = new JSDOM(html, {
    runScripts: 'dangerously',
    url: 'http://localhost',
    beforeParse: function(w) {
      // Prevent network/timers while still exercising the page's production JS.
      w.fetch = function() {
        return Promise.resolve({ok:true, json:function(){return Promise.resolve({success:true});}});
      };
      w.setInterval = function(){ return 1; };
      w.clearInterval = function() {};
      w.__imageDataAllocations = 0;
      w.__lastImageData = null;
      w.HTMLCanvasElement.prototype.getContext = function() {
        return {
          createImageData:function(width, height) {
            w.__imageDataAllocations++;
            return {width:width, height:height, data:new w.Uint8ClampedArray(width * height * 4)};
          },
          putImageData:function(imageData) { w.__lastImageData = imageData; }
        };
      };
    }
  });
  var w = dom.window;
  var T = w.__test__;
  ok(T, '__test__ hooks must be installed');

  console.log('  --- one-source-of-truth mirror ---');
  test('canvas uses the physical LCD resolution', function() {
    var c = w.document.getElementById('ecraCanvas');
    ok(c, 'ecraCanvas'); is(c.width, 800); is(c.height, 480);
  });
  test('no separately-rendered HTML screen can drift from the LCD', function() {
    ok(!w.document.getElementById('ecraScreen'), 'HTML mock screen must be absent');
    is(w.document.querySelectorAll('canvas').length, 1);
  });
  test('canvas is configured for pixel-preserving scaling and touch', function() {
    var css = w.document.querySelector('style').textContent;
    ok(css.indexOf('image-rendering:pixelated') >= 0, 'nearest-neighbour scaling');
    ok(css.indexOf('touch-action:none') >= 0, 'touch input stays on the mirror');
  });

  console.log('  --- framebuffer bytes / RGB565 conversion ---');
  test('frame endpoint and no-cache URL', function() {
    ok(T.ecraFrameUrl().indexOf('/api/touch7-frame?t=') === 0, T.ecraFrameUrl());
  });
  test('unchanged-frame hash is sent back to avoid a repeated 768 KB transfer', function() {
    ok(T.ecraFrameUrlForHash('8f13ab42ab12cd34').indexOf('&hash=8f13ab42ab12cd34') > 0);
  });
  test('conditional frame response still requests the original LCD endpoint', function() {
    ok(T.ecraFrameUrlForHash('8f13ab42ab12cd34').indexOf('/api/touch7-frame?') === 0);
  });
  test('RGB565 primaries and black expand to exact RGBA', function() {
    var rgba = new w.Uint8ClampedArray(16);
    [0xF800, 0x07E0, 0x001F, 0x0000].forEach(function(v, i) { T.ecraWriteRgb565(rgba, i * 4, v); });
    is(Array.prototype.join.call(rgba.slice(0, 4), ','), '255,0,0,255');
    is(Array.prototype.join.call(rgba.slice(4, 8), ','), '0,255,0,255');
    is(Array.prototype.join.call(rgba.slice(8, 12), ','), '0,0,255,255');
    is(Array.prototype.join.call(rgba.slice(12, 16), ','), '0,0,0,255');
  });
  test('RGB565 non-primary colour preserves each expanded channel', function() {
    var rgba = new w.Uint8ClampedArray(4);
    T.ecraWriteRgb565(rgba, 0, 0x8410);
    is(Array.prototype.join.call(rgba, ','), '132,130,132,255');
  });
  test('full RGB565 frame paints the first/last pixels and reuses its canvas buffer', function() {
    var frame = new w.ArrayBuffer(800 * 480 * 2);
    var words = new w.Uint16Array(frame);
    words[0] = 0xF800;
    words[words.length - 1] = 0x07E0;
    ok(T.ecraDrawFrame(frame), 'valid frame accepted');
    var first = w.__lastImageData;
    is(Array.prototype.join.call(first.data.slice(0, 4), ','), '255,0,0,255');
    is(Array.prototype.join.call(first.data.slice(first.data.length - 4), ','), '0,255,0,255');
    words[0] = 0x001F;
    ok(T.ecraDrawFrame(frame), 'second valid frame accepted');
    is(w.__lastImageData, first, 'ImageData reused between polls');
    is(Array.prototype.join.call(first.data.slice(0, 4), ','), '0,0,255,255');
    is(w.__imageDataAllocations, 1);
  });
  test('malformed frame length is rejected without painting', function() {
    var previous = w.__lastImageData;
    ok(!T.ecraDrawFrame(new w.ArrayBuffer(16)));
    is(w.__lastImageData, previous);
  });

  console.log('  --- LCD touch coordinate mapping ---');
  test('1:1 canvas coordinates pass through', function() {
    var p = T.ecraCanvasPoint({left:12, top:24, width:800, height:480}, 112, 74);
    is(p.x, 100); is(p.y, 50);
  });
  test('scaled canvas maps its centre to LCD centre', function() {
    var p = T.ecraCanvasPoint({left:0, top:0, width:400, height:240}, 200, 120);
    is(p.x, 400); is(p.y, 240);
  });
  test('canvas edges clamp to valid LCD pixels', function() {
    var p = T.ecraCanvasPoint({left:10, top:20, width:1600, height:960}, 1610, 980);
    is(p.x, 799); is(p.y, 479);
    var q = T.ecraCanvasPoint({left:10, top:20, width:1600, height:960}, -100, -50);
    is(q.x, 0); is(q.y, 0);
  });
  test('zero-sized canvas rejects a touch safely', function() {
    is(T.ecraCanvasPoint({left:0, top:0, width:0, height:480}, 1, 1), null);
  });
  test('remote pointer sends down, move and up for taps and drags', function() {
    var p = {x:321, y:245};
    is(T.ecraPointerUrl(p, 'down'), '/api/touch7-tap?state=down&x=321&y=245');
    is(T.ecraPointerUrl({x:400, y:300}, 'move'), '/api/touch7-tap?state=move&x=400&y=300');
    is(T.ecraPointerUrl(p, 'up'), '/api/touch7-tap?state=up&x=321&y=245');
  });
  test('mirror binds pointer lifecycle so sliders can be dragged', function() {
    var scripts = w.document.querySelectorAll('script'), js = '';
    for(var i = 0; i < scripts.length; i++) js += scripts[i].textContent;
    ok(js.indexOf("addEventListener('pointerdown'") >= 0);
    ok(js.indexOf("addEventListener('pointermove'") >= 0);
    ok(js.indexOf("addEventListener('pointerup'") >= 0);
    ok(js.indexOf("addEventListener('pointercancel'") >= 0);
  });

  console.log('  --- separate weather-location tools ---');
  test('location controls exist outside the mirrored image', function() {
    ok(w.document.getElementById('ecraGeoTools'), 'ecraGeoTools');
    ok(w.document.getElementById('ecGeoQ'), 'ecGeoQ');
    ok(w.document.getElementById('ecGeoGo'), 'ecGeoGo');
    ok(w.document.getElementById('ecGeoAuto'), 'ecGeoAuto');
  });
  test('location display accepts the config API shape (without a success wrapper)', function() {
    T.ecraGeoShow('Lisboa', 38.72, -9.14);
    is(w.document.getElementById('ecPlace').textContent, 'Lisboa (38.72, -9.14)');
  });
  test('weather status displays a fresh forecast and its age', function() {
    T.ecraWeatherRender({has:true, refreshing:false, stale:false, age_sec:42,
      today:'Hoje 12/25C Nublado', days:['TER 13/26C Sol','QUA 14/24C Chuva']});
    ok(w.document.getElementById('ecWeatherStatus').textContent.indexOf('Updated · 42 s ago') >= 0);
    ok(w.document.getElementById('ecWeatherSummary').textContent.indexOf('QUA 14/24C Chuva') >= 0);
  });
  test('weather status identifies stale data and a failed refresh', function() {
    T.ecraWeatherRender({has:true, refreshing:false, stale:true, age_sec:900, last_error:'WiFi disconnected', today:'Cached'});
    ok(w.document.getElementById('ecWeatherStatus').textContent.indexOf('Stale') >= 0);
    ok(w.document.getElementById('ecWeatherStatus').textContent.indexOf('WiFi disconnected') >= 0);
  });
  test('manual weather refresh uses the Touch7 refresh endpoint', function() {
    is(T.ecraWeatherRefreshUrl(), '/api/touch7-weather-refresh');
    ok(w.document.getElementById('ecWeatherRefresh'), 'weather refresh control');
  });
  test('page keeps the owner-requested home-button exception', function() {
    ok(!w.document.querySelector('#ecraWrap a[href="/"]'));
  });

  var total = passed + failed;
  console.log('\n  Ecra: ' + passed + '/' + total + ' passed, ' + failed + ' failed');
  dom.window.close();
}

try { run(); } catch (e) { console.log('  \u2717 Fatal: ' + e.message); failed++; }
process.exit(failed > 0 ? 1 : 0);
