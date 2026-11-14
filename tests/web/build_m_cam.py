#!/usr/bin/env python3
"""Build the camera /m jsdom fixture from the assets the renderer prints.

    MOBILE_COMMON_CSS + PAGE_CAM_MOBILE + MOBILE_SYSTEM_HTML +
    MOBILE_COMMON_JS + MOBILE_SYSTEM_JS

Usage:  python build_m_cam.py
"""
import re
from pathlib import Path

import extract

HERE = Path(__file__).resolve().parent
ASSETS = HERE.parents[1] / "chatgpt" / "src" / "webpages" / "assets"
OUT = HERE / "pages" / "camera_mobile.html"

PARTS = [
    ("mobile_common_asset.cpp", "MCCSS"),
    ("page_cam_mobile_asset.cpp", "CAMMOBILE"),
    ("mobile_system_asset.cpp", "MPSYS"),
    ("page_cam_mobile_asset.cpp", "CAMTAIL"),
    ("mobile_common_asset.cpp", "MCJS"),
    ("mobile_system_asset.cpp", "MPSYSJS"),
]


def blob(path: Path, delim: str) -> str:
    text = path.read_text(encoding="utf-8")
    m = re.search(r'R"' + re.escape(delim) + r'\((.*?)\)' + re.escape(delim) + r'"', text, re.S)
    if not m:
        raise SystemExit(f"[fixture] {path.name}: R\"{delim}(...) not found")
    return m.group(1)


def main() -> int:
    body = "".join(blob(ASSETS / name, delim) for name, delim in PARTS)
    page = extract.build_page(extract.inject_mocks_and_hooks(body), "Camera")
    OUT.write_text(page, encoding="utf-8")
    print(f"  Built: {OUT.name} ({len(page)} B) from {len(PARTS)} asset parts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
