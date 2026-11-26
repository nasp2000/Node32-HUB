"""validate_pages.py — every web page asset must use the Node32-HUB page shell.

WHY (2026-11-26, the /tdk-lambda bug): core.css sets `body{display:flex}` from
768 px up and `.page{max-width:1000px}`. A page asset that opens neither
`.page` nor `.wrap` becomes a set of direct flex children of <body>: the boxes
end up side by side, shrunk, with every heading at the same height. The
/tdk-lambda page shipped like that once; this check exists so it cannot happen
again.

CANONICAL SKELETON (see page_ipg_laser_asset.cpp):

    <div class='page <name>'>
      <div class='wrap'>
        <style> .section-box{...} .section-title{...} </style>
        <div class='actions nav-inicio'><a href='/' class='btn'>Home</a></div>
        <h1>Title</h1>
        <div class='section-box'><h3 class='section-title'>1) ...</h3> ... </div>
        ...
      </div>
    </div>

Acceptable alternatives (declared here, not silently tolerated):
  - fully self-contained documents that open <body> themselves;
  - kiosk/dashboard pages that use <main class='page'> (no .wrap);
  - full-screen app pages (radio-ui uses <div id='app'>);
  - fragments (fragments_*) and mobile assets (*_mobile*, mobile_*) — the
    mobile model has its own CSS and is documented in AGENTS.md.

Exit codes: 0 always unless --strict (or STRICT=1), which fails on a finding.
Runs as a PlatformIO pre-script (warnings only) and from release_check.sh.
"""

import os
import re
import sys

try:
    HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:
    # PlatformIO runs pre-scripts through SCons exec(): no __file__, CWD is
    # chatgpt/. Same fallback as validate_third_party.py.
    HERE = os.path.abspath(os.path.join(os.getcwd(), "scripts"))
ASSETS = os.path.join(HERE, "..", "src", "webpages", "assets")

# Documented exceptions (path fragment -> reason).
EXEMPT = {
    "fragments_": "fragment included inside another page",
    "mobile_system_asset": "mobile shared System block (own model)",
    "_mobile_asset": "mobile page (/m model, own CSS)",
    "page_radio_ui_asset": "full-screen player (<div id='app'>)",
}

PAGE_CONTAINERS = re.compile(r"class=['\"]page\b")
WRAP_CONTAINERS = re.compile(r"class=['\"]wrap['\"]")
BODY_DOC = re.compile(r"<\s*body\b", re.IGNORECASE)
MAIN_PAGE = re.compile(r"<\s*main[^>]*class=['\"]page", re.IGNORECASE)
APP_ROOT = re.compile(r"<div\s+id=['\"](app|appRoot)['\"]")


def assets():
    out = []
    for name in sorted(os.listdir(ASSETS)):
        if not name.endswith((".cpp", ".h")):
            continue
        path = os.path.join(ASSETS, name)
        try:
            text = open(path, encoding="utf-8", errors="ignore").read()
        except OSError:
            continue
        # Only files that actually carry markup in a raw string.
        if 'R"' not in text or "<div" not in text:
            continue
        out.append((name, text))
    return out


def main():
    strict = ("--strict" in sys.argv
              or os.environ.get("STRICT", "0").strip().lower() in ("1", "true", "yes", "on"))
    findings = []
    checked = 0

    for name, text in assets():
        reason = next((why for frag, why in EXEMPT.items() if frag in name), None)
        if reason:
            continue
        checked += 1
        if WRAP_CONTAINERS.search(text):
            continue                      # canonical (with or without .page)
        if BODY_DOC.search(text):
            continue                      # self-contained document
        if MAIN_PAGE.search(text):
            continue                      # kiosk dashboard
        if APP_ROOT.search(text):
            continue                      # full-screen app
        has_page = bool(PAGE_CONTAINERS.search(text))
        findings.append(
            f"{name}: no .wrap container"
            + (" (has .page but no .wrap -> still a flex child of <body>)" if has_page else "")
        )

    if findings:
        print("[validate_pages] page assets outside the standard shell:")
        for f in findings:
            print(f"  - {f}")
        print("[validate_pages] add <div class='page X'><div class='wrap'> ... </div></div> "
              "(skeleton in chatgpt/scripts/validate_pages.py)")
        if strict:
            return 1
    print(f"[validate_pages] OK ({checked} page asset(s) checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
