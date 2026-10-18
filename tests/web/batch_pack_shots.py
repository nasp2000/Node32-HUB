#!/usr/bin/env python3
"""Batch mobile screenshots for all Waveshare pack pages (harness copies only)."""
import pathlib
import re
import subprocess

WEB = pathlib.Path(__file__).parent

def core_css():
    txt = (WEB / '../../chatgpt/src/webpages/core_css.h').resolve().read_text(encoding='utf-8')
    m = re.search(r'R"CSS\((.*)\)CSS"', txt, re.S)
    return m.group(1) if m else ''

PAGES = ['access.html', 'alerts.html', 'email.html', 'wifi.html',
         'ota_asset.html', 'sdcard.html', 'sdcard_asset.html', 'dualfile.html',
         'pack_home.html', 'pack_ethernet.html', 'pack_networks.html']

def build(src_name):
    html = (WEB / 'pages' / src_name).read_text(encoding='utf-8')
    css = core_css()
    if css and '</head>' in html and 'core-dedup' not in html:
        html = html.replace('</head>', '<style>\n' + css + '\n</style>\n</head>', 1)
    # TEST-ONLY breakpoint widening (old headless clamps viewport to >=500px)
    html = html.replace('@media(max-width:480px)', '@media(max-width:520px)')
    html = html.replace('@media(max-width:400px)', '@media(max-width:520px)')
    out = 'shots/pack_' + src_name
    (WEB / out).write_text(html, encoding='utf-8')
    return out

def shot(html_path, png_path):
    r = subprocess.run(['chromium', '--headless', '--disable-gpu', '--no-sandbox',
                        '--hide-scrollbars', '--window-size=500,1000',
                        '--virtual-time-budget=6000',
                        f'--screenshot={png_path}', f'file://{html_path}'],
                       capture_output=True, text=True, cwd=str(WEB))
    print(png_path, '<-', r.stdout.strip().splitlines()[-1:] if r.stdout else r.stderr.strip()[-120:])

if __name__ == '__main__':
    (WEB / 'shots').mkdir(exist_ok=True)
    for p in PAGES:
        try:
            out = build(p)
            shot(WEB / out, f'shots/pack_{p.replace(".html", ".png")}')
        except Exception as e:
            print(p, 'SKIP:', e)
