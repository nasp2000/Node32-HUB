#!/usr/bin/env python3
"""build_site.py — turn site/ into the published tree.

Reads the release manifest and the GitHub releases, then writes a copy of the
site with:

  * every "Download vX.YYY" / "firmware vX.YYY" / tag link refreshed to the
    latest release of that pack (no page can claim an old version);
  * releases.html generated from the same data;
  * search-index.json built from the reference tables;
  * sitemap.xml and robots.txt;
  * WebP copies of the heavy screenshots, wrapped in <picture>.

Usage:
  python site/scripts/build_site.py --out _site            # CI / deploy
  python site/scripts/build_site.py --out /tmp/site-preview --offline

--offline skips the GitHub API and keeps the versions already in the HTML
(useful to preview layout without a token).
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SITE = os.path.abspath(os.path.join(HERE, ".."))
ROOT = os.path.abspath(os.path.join(SITE, ".."))
MANIFEST = os.path.join(ROOT, ".github", "release_targets.json")
REPO = os.environ.get("GITHUB_REPOSITORY", "nasp2000/Node32-HUB")
BASE_URL = "https://nasp2000.github.io/Node32-HUB"

WEBP_MIN_BYTES = 150 * 1024


def log(msg):
    print(msg, file=sys.stderr)


def api_releases():
    """All releases of this repo, newest first (tag, date, assets)."""
    res = subprocess.run(
        ["gh", "api", f"repos/{REPO}/releases?per_page=100"],
        capture_output=True, text=True, check=False,
    )
    if res.returncode != 0:
        log(f"WARNING: gh api failed, versions left as-is: {res.stderr.strip()[:120]}")
        return []
    try:
        return json.loads(res.stdout or "[]")
    except json.JSONDecodeError:
        return []


def version_key(tag):
    m = re.match(r"^v(\d+)\.(\d+)", tag or "")
    return (int(m.group(1)), int(m.group(2))) if m else (0, 0)


def latest_by_slug(variants, releases):
    """slug -> {tag, version, date, size} for the newest release of that pack."""
    out = {}
    for slug in variants:
        mine = [
            r for r in releases
            if (r.get("tag_name") or "").endswith(f"-{slug}") or (r.get("tag_name") or "") == f"v-{slug}"
        ]
        mine = [r for r in mine if not r.get("draft")]
        if not mine:
            continue
        mine.sort(key=lambda r: version_key(r.get("tag_name", "")), reverse=True)
        rel = mine[0]
        tag = rel.get("tag_name", "")
        suffix = f"-{slug}"
        version = tag[: -len(suffix)] if tag.endswith(suffix) else tag
        assets = rel.get("assets") or []
        fw = next((a for a in assets if not a["name"].endswith("_third-party.zip")), None)
        size_mb = (fw["size"] / 1048576) if fw else 0
        out[slug] = {
            "page": variants[slug].get("page", slug),
            "tag": tag,
            "version": version,
            "date": (rel.get("published_at") or "")[:10],
            "size": f"{size_mb:.1f} MB" if size_mb else "—",
            "assets": len(assets),
        }
    return out


def rewrite_versions(tree, data):
    """Point every pack page at its newest release."""
    changed = 0
    for slug, info in data.items():
        page = info.get("page") or slug
        targets = [os.path.join(tree, "packs", f"{page}.html"),
                   os.path.join(tree, "packs", page, "reference.html")]
        for path in targets:
            if not os.path.isfile(path):
                continue
            s = open(path, encoding="utf-8").read()
            before = s
            s = re.sub(r"Download v\d+\.\d+", f"Download {info['version']}", s)
            s = re.sub(r"firmware v\d+\.\d+", f"firmware {info['version']}", s)
            s = re.sub(rf"releases/tag/v\d+\.\d+-{re.escape(slug)}", f"releases/tag/{info['tag']}", s)
            if s != before:
                open(path, "w", encoding="utf-8").write(s)
                changed += 1
    return changed


RELEASES_PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Releases — Node32-HUB</title>
<meta name="description" content="Every Node32-HUB pack and its current firmware release: version, date, size and download.">
<link rel="icon" href="assets/favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="assets/site.css">
<link rel="canonical" href="{base}/releases.html">
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<div class="sheet">
  <header class="masthead">
    <a class="wordmark" href="./">Node32<span>-HUB</span></a>
    <nav>
      <a href="./#packs">Packs</a>
      <a href="install.html">Flash</a>
      <a href="releases.html">Releases</a>
      <a href="https://github.com/nasp2000/Node32-HUB">Source</a>
    </nav>
  </header>

  <section class="product-head">
    <h1>Releases</h1>
    <p>One release per pack. Each one carries the flashable build folder and a separate compliance bundle with the LGPL sources, relinkable objects and licence texts.</p>
  </section>

  <div class="product-body" id="main">
    <div>
      <table class="opts">
        <tr><th>Pack</th><th>Version</th><th>Published</th><th>Firmware</th><th>Download</th></tr>
{rows}
      </table>
      <p style="margin-top:22px">Older versions stay on the tag itself: every release keeps the build it shipped, and the commit it was built from is written in its notes.</p>
    </div>

    <aside class="aside">
      <div class="dl">
        <h2>Which one do I need?</h2>
        <p class="ver">One pack per board</p>
        <a class="btn" href="./#packs">See the packs</a>
        <a class="btn btn--ghost" href="install.html" style="margin-top:8px">How to flash</a>
        <p class="note">The pack page lists the board, the wiring and the screenshots; its reference lists every setting.</p>
      </div>
    </aside>
  </div>

  <footer class="foot">
    <span>MIT first-party · LGPL compliance bundle with every release</span>
    <span><a href="https://github.com/nasp2000/Node32-HUB/blob/main/RELEASES.md">Source per release</a></span>
  </footer>
</div>
</body>
</html>
"""


def write_releases_page(tree, variants, data):
    rows = []
    seen_pages = set()
    for slug, meta in variants.items():
        page = meta.get("page", slug)
        if page in seen_pages and slug not in data:
            continue
        if slug in data:
            seen_pages.add(page)
        info = data.get(slug)
        product = meta.get("product", slug)
        if info:
            rows.append(
                f'        <tr><td data-label="Pack"><a href="packs/{page}.html">{product}</a></td>'
                f'<td data-label="Version">{info["version"]}</td>'
                f'<td data-label="Published">{info["date"]}</td>'
                f'<td data-label="Firmware">{info["size"]}</td>'
                f'<td data-label="Download"><a href="https://github.com/{REPO}/releases/tag/{info["tag"]}">Download</a></td></tr>'
            )
        else:
            rows.append(
                f'        <tr><td data-label="Pack"><a href="packs/{page}.html">{product}</a></td>'
                f'<td data-label="Version">—</td><td data-label="Published">—</td>'
                f'<td data-label="Firmware">—</td><td data-label="Download">not released yet</td></tr>'
            )
    html = RELEASES_PAGE.format(base=BASE_URL, rows="\n".join(rows))
    open(os.path.join(tree, "releases.html"), "w", encoding="utf-8").write(html)
    return len(rows)


def build_search_index(tree, variants):
    """(pack, section, field) -> the reference tables, for the Ctrl+K box."""
    entries = []
    for slug, meta in variants.items():
        page = meta.get("page", slug)
        path = os.path.join(tree, "packs", page, "reference.html")
        if not os.path.isfile(path):
            continue
        s = open(path, encoding="utf-8").read()
        product = variants[slug].get("product", slug)
        for match in re.finditer(r'<h2 id="([^"]+)"[^>]*>(.*?)</h2>(.*?)(?=<h2 |\Z)', s, re.S):
            anchor, title, body = match.group(1), re.sub(r"<[^>]+>", "", match.group(2)), match.group(3)
            for row in re.findall(r"<tr>(.*?)</tr>", body, re.S):
                raw = [(attrs, inner) for attrs, inner in
                       re.findall(r"<td([^>]*)>(.*?)</td>", row, re.S)
                       if 'class="ico"' not in attrs]
                cells = [re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", inner)).strip()
                         for _attrs, inner in raw]
                if len(cells) < 2 or not cells[0]:
                    continue
                entries.append({
                    "pack": product,
                    "slug": slug,
                    "section": title,
                    "field": cells[0],
                    "values": cells[1],
                    "url": f"packs/{page}/reference.html#{anchor}",
                })
    out = os.path.join(tree, "search-index.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(entries, f, ensure_ascii=False, separators=(",", ":"))
    return len(entries)


def add_canonical(tree):
    """Every page declares its own URL; index.html is the root."""
    n = 0
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in files:
            if not f.endswith(".html"):
                continue
            path = os.path.join(root, f)
            rel = os.path.relpath(path, tree).replace(os.sep, "/")
            if 'rel="canonical"' in open(path, encoding="utf-8").read():
                continue
            url = BASE_URL + "/" + ("" if rel == "index.html" else rel)
            s = open(path, encoding="utf-8").read()
            s = s.replace("</title>", f'</title>\n<link rel="canonical" href="{url}">', 1)
            open(path, "w", encoding="utf-8").write(s)
            n += 1
    return n


def write_sitemap(tree):
    pages = []
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in files:
            if f.endswith(".html"):
                rel = os.path.relpath(os.path.join(root, f), tree).replace(os.sep, "/")
                pages.append(rel)
    urls = "\n".join(
        f"  <url><loc>{BASE_URL}/{'' if p == 'index.html' else p}</loc></url>" for p in sorted(pages)
    )
    open(os.path.join(tree, "sitemap.xml"), "w", encoding="utf-8").write(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n' + urls + "\n</urlset>\n"
    )
    open(os.path.join(tree, "robots.txt"), "w", encoding="utf-8").write(
        "User-agent: *\nAllow: /\nSitemap: " + BASE_URL + "/sitemap.xml\n"
    )
    return len(pages)


def make_webp(tree):
    """Heavy PNG/JPG get a WebP twin; the HTML is wrapped in <picture>."""
    done = 0
    for root, _dirs, files in os.walk(os.path.join(tree, "assets", "img")):
        for f in files:
            if not f.lower().endswith((".png", ".jpg", ".jpeg")):
                continue
            src = os.path.join(root, f)
            if os.path.getsize(src) < WEBP_MIN_BYTES:
                continue
            webp = os.path.splitext(src)[0] + ".webp"
            if not os.path.isfile(webp):
                try:
                    r = subprocess.run(
                        ["ffmpeg", "-v", "error", "-y", "-i", src, "-quality", "82", webp],
                        capture_output=True, text=True, check=False,
                    )
                except OSError:
                    log("WARNING: ffmpeg not found — WebP twins skipped")
                    return 0
                if r.returncode != 0:
                    continue
            done += 1
    return done


def wrap_pictures(tree):
    """<img src="x.png"> -> <picture><source webp><img></picture> where a twin exists."""
    changed = 0
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in files:
            if not f.endswith(".html"):
                continue
            path = os.path.join(root, f)
            s = open(path, encoding="utf-8").read()
            def repl(m):
                tag = m.group(0)
                src = re.search(r'src="([^"]+)"', tag)
                if not src or "<picture>" in tag:
                    return tag
                rel = src.group(1)
                base = os.path.dirname(rel)
                webp_rel = os.path.splitext(rel)[0] + ".webp"
                if not os.path.isfile(os.path.join(os.path.dirname(path), webp_rel)):
                    return tag
                return (f'<picture><source srcset="{webp_rel}" type="image/webp">{tag}</picture>')
            new = re.sub(r"<img\b[^>]*>", repl, s)
            if new != s:
                open(path, "w", encoding="utf-8").write(new)
                changed += 1
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="destination tree (e.g. _site)")
    ap.add_argument("--offline", action="store_true", help="do not call the GitHub API")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    if os.path.exists(out):
        shutil.rmtree(out)
    shutil.copytree(SITE, out, ignore=shutil.ignore_patterns("scripts", "__pycache__"))

    manifest = json.load(open(MANIFEST, encoding="utf-8"))["variants"]
    # only the variants that actually have a page on the site, keyed by slug
    variants = {}
    for key, meta in manifest.items():
        slug = meta.get("slug", key.lower())
        page = meta.get("page", slug)
        if os.path.isfile(os.path.join(SITE, "packs", page + ".html")):
            meta = dict(meta)
            meta["page"] = page
            variants[slug] = meta
    releases = [] if args.offline else api_releases()
    data = latest_by_slug(variants, releases) if releases else {}

    n_ver = rewrite_versions(out, data)
    n_rel = write_releases_page(out, variants, data)
    n_idx = build_search_index(out, variants)
    n_canon = add_canonical(out)
    n_pages = write_sitemap(out)
    n_webp = make_webp(out)
    n_wrap = wrap_pictures(out)

    log(f">>> versions refreshed in {n_ver} pages")
    log(f">>> releases.html: {n_rel} packs ({len(data)} with a release)")
    log(f">>> search index: {n_idx} entries")
    log(f">>> canonical: {n_canon} pages; sitemap: {n_pages} pages, robots.txt")
    log(f">>> webp: {n_webp} images, {n_wrap} pages wrapped in <picture>")
    log(f">>> site built in {out}")


if __name__ == "__main__":
    main()
