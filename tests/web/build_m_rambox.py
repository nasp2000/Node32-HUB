#!/usr/bin/env python3
"""Build the Rambox32 /m jsdom fixture (same order as the renderer)."""
import re
from pathlib import Path

import extract

HERE = Path(__file__).resolve().parent
ASSETS = HERE.parents[1] / "chatgpt" / "src" / "webpages" / "assets"
OUT = HERE / "pages" / "rambox_mobile.html"

PARTS = [
    ("mobile_common_asset.cpp", "MCCSS"),
    ("page_rambox_mobile_asset.cpp", "RBMOBILE"),
    ("mobile_system_asset.cpp", "MPSYS"),
    ("page_rambox_mobile_asset.cpp", "RBTAIL"),
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
    page = extract.build_page(extract.inject_mocks_and_hooks(body), "Rambox32")
    OUT.write_text(page, encoding="utf-8")
    print(f"  Built: {OUT.name} ({len(page)} B) from {len(PARTS)} asset parts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
