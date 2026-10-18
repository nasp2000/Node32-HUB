#!/usr/bin/env python3
"""Audit i18n coverage on pack page assets: hardcoded visible strings w/o data-i18n,
title/placeholder/alt without data-i18n-*, JS literals in customAlert/Confirm without t()."""
import re
import pathlib

ROOT = pathlib.Path(__file__).parent.parent.parent
ASSETS = {
    'home': 'chatgpt/src/webpages/assets/page_home_asset.cpp',
    'proximity': 'chatgpt/src/webpages/assets/page_proximity_asset.h',
    'dashboard': 'chatgpt/src/webpages/assets/page_proximity_dashboard_asset.h',
    'ethernet': 'chatgpt/src/webpages/assets/page_ethernet_asset.cpp',
    'networks': 'chatgpt/src/webpages/assets/page_networks_asset.cpp',
    'sdcard': 'chatgpt/src/webpages/assets/page_sdcard_asset.cpp',
    'dualfile': 'chatgpt/src/webpages/assets/page_dualfile_asset.h',
    'access': 'chatgpt/src/webpages/assets/page_access_asset.h',
    'alerts': 'chatgpt/src/webpages/assets/page_alerts_asset.cpp',
    'email': 'chatgpt/src/webpages/assets/page_email_asset.cpp',
    'ftp': 'chatgpt/src/webpages/assets/page_ftp_asset.cpp',
    'ota': 'chatgpt/src/webpages/assets/page_ota_asset.cpp',
    'wifi': 'chatgpt/src/webpages/assets/page_wifi_asset.cpp',
}

SKIP_TEXT = re.compile(r'^[\s\d\W_]*$|%%[A-Z_]+%%|^[<>]$')


def strip_scripts(txt):
    # remove <script>...</script> bodies for HTML-text scan (JS handled separately)
    return re.sub(r'<script.*?</script>', '', txt, flags=re.S | re.I)


from html.parser import HTMLParser

VOID = {'input', 'br', 'hr', 'img', 'meta', 'link', 'option'}


class CoverParser(HTMLParser):
    """Collects visible text nodes NOT covered by any ancestor data-i18n[-html]."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []   # [(tag, covered)]
        self.issues = []
        self.in_script_style = False

    def handle_starttag(self, tag, attrs):
        tag = tag.lower()
        if tag in ('script', 'style'):
            self.in_script_style = True
            self.stack.append((tag, True))
            return
        d = dict(attrs)
        covered = ('data-i18n' in d or 'data-i18n-html' in d or
                   any(c for _, c in self.stack))
        self.stack.append((tag, covered))
        # lonely <td>/<span> text handled at endtag via buffer
        self._buf = ''

    def handle_startendtag(self, tag, attrs):
        pass

    def handle_data(self, data):
        if self.in_script_style:
            return
        if not self.stack:
            return
        tag, covered = self.stack[-1]
        if tag in ('script', 'style', 'option'):
            return
        text = data.strip()
        if not text or SKIP_TEXT.match(text):
            return
        if re.search(r'\+\s*[a-zA-Z_]|getElementById|querySelector', text):
            return
        if not covered:
            self.issues.append(f'TEXT <{tag}>: {text[:90]!r}')

    def handle_endtag(self, tag):
        tag = tag.lower()
        if tag in ('script', 'style'):
            self.in_script_style = False
        while self.stack:
            t, _ = self.stack.pop()
            if t == tag:
                break


def audit_text_nodes(name, txt):
    p = CoverParser()
    try:
        p.feed(strip_scripts(txt))
    except Exception:
        pass
    return p.issues


def audit_attrs(name, txt):
    issues = []
    for m in re.finditer(r'<[a-zA-Z][^<>]*>', strip_scripts(txt)):
        tag = m.group(0)
        for attr in re.findall(r'''\b(title|placeholder|alt|aria-label)\s*=\s*(['"])(.*?)\2''', tag):
            aname, _, aval = attr
            if not aval.strip() or SKIP_TEXT.match(aval):
                continue
            if f'data-i18n-{aname}' in tag or 'data-i18n-title' in tag and aname == 'title':
                continue
            issues.append(f'ATTR {aname}=: {aval[:80]!r}')
    return issues


def audit_js(name, txt):
    issues = []
    for m in re.finditer(r'''custom(Alert|Confirm)\(\s*(['"])(.*?)\2''', txt):
        lit = m.group(3)
        if lit.startswith('_t(') or lit.startswith('t(') or lit.startswith('rt('):
            continue
        if not lit.strip() or SKIP_TEXT.match(lit):
            continue
        issues.append(f'JS custom*: {lit[:80]!r}')
    return issues


if __name__ == '__main__':
    total = 0
    for name, rel in ASSETS.items():
        txt = (ROOT / rel).read_text(encoding='utf-8', errors='replace')
        issues = audit_text_nodes(name, txt) + audit_attrs(name, txt) + audit_js(name, txt)
        # dedupe
        seen, uniq = set(), []
        for i in issues:
            if i not in seen:
                seen.add(i)
                uniq.append(i)
        total += len(uniq)
        print(f'===== {name}: {len(uniq)} =====')
        for u in uniq[:40]:
            print('  ' + u)
    print('TOTAL:', total)
