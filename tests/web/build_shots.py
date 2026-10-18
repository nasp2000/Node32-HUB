#!/usr/bin/env python3
"""Build screenshot harness HTML files (mock API seed) for headless Chromium."""
import pathlib
import re

WEB = pathlib.Path(__file__).parent

def core_css():
    txt = (WEB / '../../chatgpt/src/webpages/core_css.h').resolve().read_text(encoding='utf-8')
    m = re.search(r'R"CSS\((.*)\)CSS"', txt, re.S)
    return m.group(1) if m else ''
MOCK_JS = """
<script>
window.__shotTick = 0;
window.__MOCK_API__ = {
  '/api/proximity-chart-data': function() {
    var now = new Date(); now.setMinutes(0, 0, 0);
    var stamps = [], ins = [], outs = [];
    var base = 200000, sbase = 1700;
    var incIn = [35000, 37000, 35000, 800];
    var incOut = [5, 8, 6, 2];
    stamps.push(Math.floor((now.getTime() - 4*3600*1000) / 1000));
    ins.push(base); outs.push(sbase);
    for (var k = 0; k < 4; k++) {
      var t = new Date(now.getTime() - (3-k)*3600*1000);
      stamps.push(Math.floor(t.getTime() / 1000));
      base += incIn[k]; sbase += incOut[k];
      ins.push(base); outs.push(sbase);
    }
    return { timestamps: stamps, count: 5, stride: 1,
      prox_ch0_in: ins, prox_ch0_out: [0,0,0,0,0,0], prox_ch1_in: outs };
  },
  '/api/proximity': function() {
    window.__shotTick++;
    var t = window.__shotTick;
    return { channels: [
      { name: 'Linha 1', mode: 0, count_in: 361000 + t*120, count_out: 0, cycle_ms: 2054, ppm: 7198.6, state: true, setup_count: 0, elapsed_sec: 3727 },
      { name: 'Scrap', mode: 0, count_in: 1819 + t*2, count_out: 0, cycle_ms: 2059, ppm: 36.0, state: true, setup_count: 0, elapsed_sec: 3725 },
      { name: 'Porta', mode: 1, count_in: 0, count_out: 0, cycle_ms: 0, ppm: 0, state: true, setup_count: 0, elapsed_sec: 3821 }
    ]};
  },
  '/api/proximity/config': { channels: [
    { n: 'Linha 1', m: 0, gi: 10, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 200, tp: 600, om: 0, gc: 2, sc: 1 },
    { n: 'Scrap', m: 0, gi: 11, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 1, tp: 0, om: 0, gc: 255, sc: 255 },
    { n: 'Porta', m: 1, gi: 12, gi2: 255, go: 255, db: 50, gl: 0, pd: 0, pu: 0, mo: false, ml: 1, tp: 0, om: 0, gc: 255, sc: 255 }
  ]},
  '/api/proximity/oee': [
    { ch: 0, name: 'Linha 1', availability: 37.5, performance: 100, quality: 99.4, oee: 37.3, scrap_count: 1819, running_sec: 10800, downtime_sec: 18000 },
    { ch: 1, name: 'Scrap', availability: 12.6, performance: 100, quality: 100, oee: 12.6, scrap_count: 0, running_sec: 3640, downtime_sec: 25160 },
    { ch: 2, name: 'Porta', availability: 100, performance: 0, quality: 100, oee: 0, scrap_count: 0, running_sec: 28800, downtime_sec: 0 }
  ],
  '/api/proximity/shift-config': { en: true, ar: true, nSlots: 3,
    slots: [{ h: 6, m: 0 }, { h: 14, m: 0 }, { h: 22, m: 0 }] }
};
try { localStorage.setItem('prox_visCh', '3'); } catch (e) {}
</script>
"""

def build(src_name, out_name):
    html = (WEB / 'pages' / src_name).read_text(encoding='utf-8')
    marker = '})();\n</script>'
    assert marker in html, 'mock layer marker missing in ' + src_name
    html = html.replace(marker, marker + MOCK_JS, 1)
    css = core_css()
    if css and '</head>' in html:
        html = html.replace('</head>', '<style>\n' + css + '\n</style>\n', 1)
    # TEST-ONLY: old headless Chromium clamps layout viewport to >=500px, so the
    # 480/400px mobile breakpoints would never fire in screenshots. Widen them
    # in the harness copy only (production keeps 480/400); the 730px+ tables
    # render identically at 500px and 390px (horizontal scroll in both).
    html = html.replace('@media(max-width:480px)', '@media(max-width:520px)')
    html = html.replace('@media(max-width:400px)', '@media(max-width:520px)')
    (WEB / 'shots' / out_name).write_text(html, encoding='utf-8')
    print('built shots/' + out_name)

if __name__ == '__main__':
    (WEB / 'shots').mkdir(exist_ok=True)
    build('proximity.html', 'shot_proximity.html')
    build('proximity_dashboard.html', 'shot_dashboard.html')
