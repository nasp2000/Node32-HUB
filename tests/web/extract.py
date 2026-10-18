#!/usr/bin/env python3
"""Extract PROGMEM pages to standalone HTML for jsdom testing.

Usage:
  python extract.py <input.cpp> <output.html> [page_title]

Injects mock API layer + test hooks so internal IIFE functions
are exposed via window.__test__.
"""
import os
import re
import sys
from pathlib import Path

MOCK_LAYER = r'''
<script>
// ===== MOCK API LAYER =====
(function() {
  window.fetch = function(url, opts) {
    var u = typeof url === 'string' ? url : (url && url.url ? url.url : '');
    var MOCKS = typeof window.__MOCK_API__ !== 'undefined' ? window.__MOCK_API__ : {};
    if (opts && opts.method && opts.method !== 'GET') {
      window.__postBodies = window.__postBodies || [];
      window.__postBodies.push({ url: u, method: opts.method, body: opts.body || '' });
    }
    var mock = MOCKS[u];
    if (!mock) {
      for (var k in MOCKS) {
        if (u.indexOf(k) >= 0) { mock = MOCKS[k]; break; }
      }
    }
    if (typeof mock === 'function') mock = mock();
    return Promise.resolve({
      ok: true, status: 200,
      json: function() { return Promise.resolve(mock || {}); },
      text: function() { return Promise.resolve(JSON.stringify(mock || {})); }
    });
  };
  window.customAlert = function(m) { console.log('[mock] customAlert:', m); };
  window.customConfirm = function(m, cb) { if (cb) cb(true); };
  window.showLoading = function() {};
  window.hideLoading = function() {};
  window.disableBtn = function(b) { if (b) { b.disabled = true; } };
  window.enableBtn = function(b) { if (b) { b.disabled = false; } };
  window.showSavedCard = function() {};
  // Feature flags used by some pages (default: off)
  window._rs232Enabled = false;
  window._modbusEnabled = false;
  window.__test__ = {};  // ensure window.__test__ exists even on non-IIFE pages
})();
</script>
'''

TEST_HOOKS = '''
// ===== TEST HOOKS (inside page IIFE scope) =====
// Preserve hooks the page already installed (e.g. page_ecra_asset sets
// window.__test__.* at the end of its script); only fill missing ones below.
window.__test__ = window.__test__ || {};
// Spy mechanism
window.__spy__ = {};
window.__spy__.calls = {};
window.__spy__.reset = function() { window.__spy__.calls = {}; };
window.__spy__.wrap = function(name, fn) {
  return function() {
    window.__spy__.calls[name] = window.__spy__.calls[name] || [];
    window.__spy__.calls[name].push(Array.prototype.slice.call(arguments));
    return fn.apply(this, arguments);
  };
};
// Wrap key functions for spying
if (typeof postSend !== 'undefined') postSend = window.__spy__.wrap('postSend', postSend);
if (typeof showAlert !== 'undefined') showAlert = window.__spy__.wrap('showAlert', showAlert);
// Helper: reset IIFE-local state vars (P, C, SH, RP used by radio page)
window.__test__._reset = function(p, c, sh, rp) {
  try { if (p !== undefined) { var _a=p; P.length=0; for(var _i=0;_i<_a.length;_i++)P.push(_a[_i]); } } catch(e) {}
  try { if (c !== undefined) C = c; } catch(e) {}
  try { if (sh !== undefined) SH = sh; } catch(e) {}
  try { if (rp !== undefined) RP = rp; } catch(e) {}
};
// Expose functions via eval (runs in outer IIFE scope)
var _testNames = [
  // Recipients page
  'rt','rowHTML','addRow','updateNameHint','saveSlot','saveDontCopy','preload',
  'removeRow',
  'switchTab',
  // Proximity page (status + config + shifts)
  'render','renderCfg','fetchData','resetCh','resetAll','_lockOutFields',
  'renderShiftSlots','loadShiftCfg','loadShiftdayEmails','saveAll','loadCfg',
  '_applyVisFilter','onVisChChange','applyTemplate',
  // Commands page
  'esc','readLangCache','t','templateOptions','unitSelect','daysSelect',
  'tplSelect','addRow','attachRow','saveSlot','attachSaveAll','rowHTML','removeRow',
  // DateTime Commands page
  'e','t','to','r','addRow','attachRow','init','lt','ls','la','tt','ss','attachSaveAll','rm',
  // Sidebar / dock
  'renderWidgetSidebar','moveWidgetUp','moveWidgetDown','moveWidgetToSide',
  'toggleWidgetVis','applyTheme','readUiPrefs','saveUiPrefs','saveDockLayout',
  'restoreDockLayout','saveWidgetPrefs','restoreWidgetPrefs','initLeftPanel',
  'byId','qsa','localStorage','state',
  // Utility
  'fmtEta','fmtLagMs','fmtGcodeNum','normalizeZStepValue',
  'setXyStepFromPreset','syncMainStepFromSelectors','updateXyStepButtons',
  'sanitizeFileName','formatFileSize','ensureAllowedUploadPath',
  'recentErrorMeta','clamp',
  // Jog
  'runJog','runJogXY','runJogXYCenter','quickUnlock','quickHome','quickZeroWcs','quickReset','quickStatus',
  // Job
  'startFileStream','pauseStream','resumeStream','cancelStream',
  'sendManualCommand','applyPollInterval','applyUsbLineCoding',
  'postActionOnce','postAction','refreshStatus','saveMacros',
  // Frame
  'runFrameFromPreview','updateFrameInfo',
  'getPreviewFrameBounds','setCommandMode',
  // Override (scoped inside bindEvents)
  // 'sendOverrideFeed','sendOverrideSpin','applyOverrides','stepOverride',
  // Probe
  'runAutoProbeZZero',
  // Macros
  'defaultMacros','getMacroEntry','runMacro','loadMacrosToForm','saveMacrosFromForm',
  // Console
  'ingestConsoleEvents','logLine','clearConsole','toggleConsoleFullscreen',
  // Workspace / DRO
  'extractDroFromStatus','renderDro','fmtDro','hasValidDroValue',
  'parseNumber3','parsePosTriplet','firstNumber3',
  // USB / Connection
  'selectedSlot','normalizeUsbSlots','renderSlots',
  'syncSelectedUsbBaud','ensureUsbBaudOption','usbBaudPrefId',
  'getPreferredUsbBaud','savePreferredUsbBaud',
  // SD Card
  'sdFileNameFromPath','sdParentDir','formatSdSize','normalizeSdEntry',
  'isPreviewSizeBlocked','isPreviewAutoRenderHeavy',
  // PSRAM
  'refreshPsramFileStatus',
  // Preview utilities (pure functions)
  'extractAxisValue','extractAxisInMm','appendLinear',
  'quickWireframe','applyWireframePreview',
  'parseToolpath3dAsync',
  // Critical confirm
  'runWithCriticalConfirm','requireAxesAtZero',
  // Render
  'renderStatus','applyRuntimeControls','applyStatusSnapshot',
  // Job helpers
  'getSegmentExecNo','getActiveExecNo',
  // Light preview (inner functions may not resolve)
  'simplifyPolylineLight',
  // Contour selection (GRBL light preview)
  'filterContoursPreserveCoords',
  'buildLightPreview','buildLightPieceContoursFromGcode',
  'buildPieceContoursFromSegments','_syncContoursFromSegments',
  '_toggleContourSelect','_undoContourToggle','_updateContourSelectInfo',
  '_navContourHighlight','_spaceToggleHighlighted',
  '_handleCanvasContourClick','_screenToMachine','_minDistToContour','_computeTopTransformPx',
  '_distToSegment','_pointInPolygon','_lightPreviewScalePx',
  'renderPreviewFromText','_computeBounds','_drawTopView','_drawIsoView',
  'applyLightPreviewFromText','applyLightPreviewFromBounds','_keepDerivedContours',
  '_drawPreviewNow','_drawContourOverlay','_drawCrosshair',
  'bindPreviewInteractions',
  // MQ135 sensor page
  't','uiLocale','trendArrow','aqiInfo','pickReadableTextColor',
  'applySensorValueTheme','mqTone','showStatus',
  // Sensor config page
  'updateStatistics',
  // HX711 page
  'UNIT_LABELS','t','uiLocale','updateDisplay','showStatus','performTare',
  // INA226 page
  't','uiLocale','updateDisplay','showStatus',
  // Sound page
  't','uiLocale','updateDisplay','showStatus',
  // MAX31865 page
  't','uiLocale','updateDisplay','showStatus',
  // Sensor charts page
  'numericSeries','hasSeriesData','calcTrend','metricColor',
  'calcDew','calcHeat','calcAlt','fmtVal',
  'isRS232Metric','isModbusMetric','isProximityMetric',
  'getMetricMeta','getData','seriesValues','pickData',
  'updateMaxVisibleOptions','setPointStrideAutoValue',
  'mkOpts','fmtTs','trim','mkGradient','registerChartPlugins',
  'mkChart','updChart','saveAllCharts',
  'getY2Key','yPad','yPadNeg','hasNeg',
  'setChartCenterBadge','updStats','setStatRow','calcSt','calcAdv',
  'getMetricAdapter','metricPointCount','showSts','setTheme',
  'fetchChartJson','waitChartRetry','isRetryableChartStatus',
  'TH','currentTheme',
  'prodDayBoundary','prodDayDates','dayBoundaryTs','ensureDayBoundary',
  'dayProdData','_dayBoundary','_dayBoundaryLoading',
  'loadRS232Data','loadModbusData','loadProximityData','loadData','syncPresetRange',
  'T','AP','APJ','BN','sw','onSearch','addPlay','playToggle',
  'radioStop','radioNext','radioPrev','playUrl','updUI','setVol',
  'togSh','togRp','loadFav','addFav','favPlay','favDel',
  'touchActivity','IDLE_T','NAV_DEF','NAV_N','NAV_OFF','NAV_ICON','NAV_LABEL','PNL_ALL',
  'syncQ','rQ','rmQ','savePl','loadPls','loadPl','delPl','loadQ','loadSt','fh',
  'isSup','chkSup','loadResume','renderResume','saveResume','resumeToggle','resumeTargetToggle','RESUME_A','RESUME_F',
];
_testNames.forEach(function(n) {
  try { window.__test__[n] = eval(n); } catch(e) { window.__test__[n] = null; }
});
'''


def _resolve_includes(asset_dir, grbl_dir, content):
    """Replace #include directives with content from included files.
    
    Resolves #include "grbl/..." to files in grbl_dir.
    Resolves #include "page_*_asset.h" to files in asset_dir.
    """
    def resolve_once(text):
        def repl(m):
            path = m.group(1)
            if path.startswith('grbl/'):
                inc_path = os.path.join(grbl_dir, os.path.basename(path))
            elif path.startswith('page_') and path.endswith('.h'):
                inc_path = os.path.join(asset_dir, path)
            else:
                return m.group(0)
            if os.path.exists(inc_path):
                with open(inc_path, 'r', encoding='utf-8') as f:
                    return f.read()
            return m.group(0)
        return re.sub(r'#include\s+"([^"]+)"', repl, text)
    
    prev = None
    result = content
    while result != prev:
        prev = result
        result = resolve_once(result)
    return result

def extract_progmem(filepath):
    asset_dir = os.path.dirname(filepath)
    grbl_dir = os.path.join(asset_dir, 'grbl')
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    # Resolve #include directives (module files)
    content = _resolve_includes(asset_dir, grbl_dir, content)
    # Now extract all R-string literals and concatenate
    matches = re.findall(r'R"(\w+)\((.*?)\)\1"', content, re.DOTALL)
    if not matches:
        raise ValueError('No PROGMEM R-string found in ' + filepath)
    html = ''.join(m[1] for m in matches)
    # Fix orphaned IIFEs: some pages (e.g. recipients) have a bare
    # (function(){...})(); that the ESP32 renderer wraps with <script> at
    # runtime, but FOOTER provides the closing </script>. When concatenated
    # directly, the IIFE falls outside any <script> tag (becomes text node
    # in JSDOM). Detect this pattern and inject the missing <script> open tag.
    html = re.sub(
        r'</script>\s*(\(function\s*\(\)\s*\{)',
        r'</script>\n<script>\1',
        html
    )
    # Also strip duplicate DOCTYPE/HTML wrappers from individual fragments.
    # Some fragments include their own <!DOCTYPE html><html><head>... that
    # collide with build_page()'s wrapper. Strip up to <body> if present,
    # otherwise strip up to </style> (last head element before body content).
    cleaned = re.sub(
        r'^\s*<!DOCTYPE html>.*?<body[^>]*>\s*',
        '',
        html,
        count=1,
        flags=re.DOTALL | re.IGNORECASE
    )
    if cleaned == html:
        cleaned = re.sub(
            r'^\s*<!DOCTYPE html>.*?</style>\s*',
            '',
            html,
            count=1,
            flags=re.DOTALL | re.IGNORECASE
        )
    html = cleaned
    # Remove trailing </body></html> from fragments (build_page adds its own)
    html = re.sub(
        r'\s*</body>\s*</html>\s*$',
        '',
        html,
        flags=re.DOTALL | re.IGNORECASE
    )
    # Wrap bare JS IIFEs: some pages (e.g. commands) have JS fragments that
    # are NOT wrapped in <script> tags (the ESP32 renderer adds them at
    # runtime). Detect bare IIFE and wrap in <script>.
    if re.search(r'\(function\s*\(\s*\)\s*\{', html) and not re.search(r'<script>', html):
        m = re.search(r'(.*?)(\(function\s*\(\s*\)\s*\{[\s\S]*?)(?:</script>|\)\s*</|$)', html, re.DOTALL)
        if m:
            html = m.group(1) + '<script>\n' + m.group(2) + '\n</script>'
    # Also handle pages where a fragment ends with <script> (unclosed) and
    # the next fragment is bare JS — close the script tag after the IIFE.
    if re.search(r'<script>\s*\(function\s*\(\s*\)\s*\{', html) and not re.search(r'</script>', html):
        # Find the unclosed <script>...()... and add </script>
        m = re.search(r'(<script>\s*\(function\s*\(\s*\)\s*\{[\s\S]*)$', html, re.DOTALL)
        if m:
            before = html[:m.start(1)]
            script_content = m.group(1)
            html = before + script_content + '\n</script>'
    # Handle pages where FOOTER fragment provides </script> after the bare IIFE.
    # If we already have <script>(function(){...})();</script> with a stray
    # </script> after (from FOOTER), remove the redundant one.
    html = re.sub(
        r'(<script>\s*\(function\s*\(\s*\)\s*\{.*?\)\s*\(\s*\)\s*;\s*</script>)\s*</script>',
        r'\1',
        html,
        flags=re.DOTALL
    )
    return html


def build_page(raw_html, page_name):
    return f'''<!DOCTYPE html>
<html lang='en'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>{page_name} (Test)</title>
</head>
<body>
{raw_html}
</body>
</html>'''


def inject_mocks_and_hooks(html):
    """Inject mock layer before first <script> and test hooks inside the last IIFE.
    
    Also appends a global fallback script at the end that re-resolves any
    null entries in window.__test__ (catches functions declared as globals
    in a separate script block that runs after the IIFE).
    """
    all_scripts = list(re.finditer(r'(<script>)(.*?)(</script>)', html, re.DOTALL | re.IGNORECASE))
    if not all_scripts:
        return html

    # Inject MOCK_LAYER before the first <script>
    first_script = all_scripts[0]
    prefix = html[:first_script.start()]
    after_first = html[first_script.start():]
    html = prefix + MOCK_LAYER + '\n' + after_first

    # Rescan scripts after insertion
    all_scripts = list(re.finditer(r'(<script>)(.*?)(</script>)', html, re.DOTALL | re.IGNORECASE))

    # Find the last script with })(); or })() for IIFE scope injection.
    # Skip the mock layer script (it has its own IIFE that we must NOT inject into).
    # Also verify the IIFE is at the script's end (top-level wrapper), not an inner IIFE
    # like (function(){...})(); inside addEventListener('load', ...).
    target_idx = None
    for i in range(len(all_scripts) - 1, -1, -1):
        page_js = all_scripts[i].group(2)
        if 'MOCK API LAYER' in page_js:
            continue
        if '})();' in page_js or re.search(r'}\s*\)\s*\(\s*\)', page_js):
            # Verify the last })() is at the end of the script (top-level IIFE)
            m_close = list(re.finditer(r'}\s*\)\s*\(\s*\)\s*;?', page_js))
            if m_close:
                last_c = m_close[-1]
                after_iife = page_js[last_c.start():]
                after_trimmed = re.sub(r'\s*;?\s*$', '', after_iife)
                if after_trimmed in ('})()', '})();'):
                    target_idx = i
                    break
                # else: inner IIFE (code after })() like "});") — fall through to standalone hooks

    result = []
    pos = 0
    for i, m in enumerate(all_scripts):
        result.append(html[pos:m.start()])
        if i == target_idx:
            open_tag = m.group(1)
            page_js = m.group(2)
            close_tag = m.group(3)
            stripped = page_js.rstrip()
            # Find last })() or })(); (some pages omit the semicolon)
            m_close = list(re.finditer(r'}\s*\)\s*\(\s*\)\s*;?', stripped))
            if m_close:
                last_c = m_close[-1]
                idx = last_c.start()
                before_iife = stripped[:idx]
                after_iife = stripped[idx:]
                modified_js = before_iife.rstrip() + '\n' + TEST_HOOKS + '\n' + after_iife
            else:
                # No IIFE closure found — inject at end of script
                modified_js = stripped + '\n' + TEST_HOOKS
            result.append(open_tag + modified_js + close_tag)
        else:
            result.append(m.group(0))
        pos = m.end()

    result.append(html[pos:])

    # If no IIFE was found in page scripts, inject standalone test hooks
    # so that global functions are exposed via window.__test__
    if target_idx is None:
        standalone_hooks = TEST_HOOKS.replace(
            '// ===== TEST HOOKS (inside page IIFE scope) =====',
            '// ===== TEST HOOKS (standalone — no IIFE found) ====='
        )
        standalone_tag = '\n<script>\n' + standalone_hooks + '\n</script>\n'
        body = result[-1]
        body_end = body.rfind('</body>')
        if body_end >= 0:
            result[-1] = body[:body_end] + standalone_tag + body[body_end:]
        else:
            result.append(standalone_tag)

    # Append global fallback hooks (runs after all scripts, re-resolves globals)
    fallback = '''
<script>
(function() {
  for (var k in window.__test__) {
    if (window.__test__[k] === null || typeof window.__test__[k] === 'undefined') {
      try { window.__test__[k] = eval(k); } catch(e) {}
    }
  }
})();
</script>
'''
    body = result[-1]
    body_end = body.rfind('</body>')
    if body_end >= 0:
        result[-1] = body[:body_end] + fallback + body[body_end:]
    else:
        result.append(fallback)

    return ''.join(result)



def main():
    if len(sys.argv) < 3:
        print('Usage: extract.py <input.cpp> <output.html> [page_title]')
        sys.exit(1)

    inp = Path(sys.argv[1])
    out = Path(sys.argv[2])
    title = sys.argv[3] if len(sys.argv) > 3 else inp.stem.replace('page_', '').replace('_asset', '')

    raw = extract_progmem(inp)
    out.parent.mkdir(parents=True, exist_ok=True)
    page = build_page(inject_mocks_and_hooks(raw), title)
    out.write_text(page, encoding='utf-8')
    print(f'  Extracted: {inp.name} -> {out.name}')


if __name__ == '__main__':
    main()
