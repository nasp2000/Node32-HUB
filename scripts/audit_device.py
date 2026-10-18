#!/usr/bin/env python3
"""Device functional audit for Node32-HUB (Waveshare 8DI8DO pack).

Reads the device's self-description endpoints and smoke-tests the enabled
pages and read-only APIs. This is the "degrau A+B" prototype of the external
AI test agent (self.md Phase 7): it consumes /api/self-test and
/api/test-manifest and produces a PASS/FAIL/WARNING/SKIPPED report.
Everything runs on THIS computer — the ESP32 only serves its existing HTTP
routes, so no extra heap/CPU is used on the device.

Usage:
    python scripts/audit_device.py --host 192.168.1.100 [--auth user:pass]
"""

import argparse
import json
import sys
import urllib.parse

import requests

PASS = "PASS"
FAIL = "FAIL"
WARNING = "WARNING"
SKIPPED = "SKIPPED"
NOT_VERIFIED = "NOT_VERIFIED"


def make_session(auth):
    s = requests.Session()
    if auth and ":" in auth:
        user, pwd = auth.split(":", 1)
        s.auth = (user, pwd)
    return s


def get_json(session, url, timeout):
    try:
        r = session.get(url, timeout=timeout)
        return r, r.json() if r.status_code == 200 else None
    except requests.exceptions.RequestException as e:
        return None, {"_error": str(e)}
    except ValueError:
        return r, {"_error": "invalid JSON"}


class Report:
    def __init__(self):
        self.items = []

    def add(self, layer, name, result, detail=""):
        self.items.append({"layer": layer, "name": name, "result": result, "detail": detail})

    def summary(self):
        counts = {k: 0 for k in (PASS, FAIL, WARNING, SKIPPED, NOT_VERIFIED)}
        for it in self.items:
            counts[it["result"]] = counts.get(it["result"], 0) + 1
        return counts


def main():
    ap = argparse.ArgumentParser(description="Node32-HUB device audit")
    ap.add_argument("--host", required=True, help="device IP/hostname")
    ap.add_argument("--auth", default="", help="HTTP Basic Auth as user:pass")
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--json", action="store_true", help="output raw JSON of endpoints")
    args = ap.parse_args()

    base = f"http://{args.host}"
    session = make_session(args.auth)
    report = Report()

    print("=" * 70)
    print(f"Node32-HUB AUDIT  ->  {base}")
    print("=" * 70)

    # ── 1. Device health (self-test) ────────────────────────────────────
    print("\n[1] DEVICE HEALTH (/api/self-test)")
    st, st_json = get_json(session, f"{base}/api/self-test", args.timeout)
    if st is None or st.status_code != 200:
        print(f"  {FAIL}  /api/self-test unreachable ({st.status_code if st else 'conn error'})")
        report.add("DISCOVERY", "self-test", FAIL, "endpoint unreachable")
        print("\nAborting: device must serve /api/self-test.")
        sys.exit(2)

    device = st_json.get("device", {})
    print(f"  firmware : {device.get('firmware')}")
    print(f"  pack     : {device.get('pack')} (id={device.get('pack_id')})")
    print(f"  brand    : {device.get('brand')}")
    print(f"  chip     : {device.get('chip')}")
    report.add("DISCOVERY", "self-test", PASS, f"fw {device.get('firmware')}")

    # checks from self-test
    for c in st_json.get("checks", []):
        res = PASS if c.get("pass") else FAIL
        report.add("CHECK", f"selftest.{c.get('name')}", res, c.get("detail", ""))
        print(f"  check [{c.get('name')}] -> {res} {c.get('detail','')}")

    # test_safety (informative)
    tsafe = st_json.get("test_safety", {})
    print(f"  test_safety: read_only={tsafe.get('read_only')} "
          f"config_write={tsafe.get('config_write')} reboot={tsafe.get('reboot')}")

    if args.json:
        print(json.dumps(st_json, indent=2))

    # ── 2. Test contract (test-manifest) ────────────────────────────────
    print("\n[2] TEST MANIFEST (/api/test-manifest)")
    mf, mf_json = get_json(session, f"{base}/api/test-manifest", args.timeout)
    if mf is None or mf.status_code != 200:
        report.add("DISCOVERY", "test-manifest", FAIL, "endpoint unreachable")
        print(f"  {FAIL}  /api/test-manifest unreachable")
        mf_json = {}
    else:
        report.add("DISCOVERY", "test-manifest", PASS, f"manifest v{mf_json.get('manifest_version')}")
        print(f"  manifest_version : {mf_json.get('manifest_version')}")
        print(f"  capabilities     : {mf_json.get('capabilities')}")
        n_pages = len(mf_json.get("pages", []))
        n_api = len(mf_json.get("api", []))
        n_fields = len(mf_json.get("fields", []))
        n_actions = len(mf_json.get("actions", []))
        print(f"  pages={n_pages}  api={n_api}  fields={n_fields}  actions={n_actions}")

    # ── 3. Pages smoke test ─────────────────────────────────────────────
    print("\n[3] PAGES (smoke test, enabled only)")
    pages = mf_json.get("pages", [])
    pages_tested = 0
    for p in pages:
        path = p.get("path")
        if not p.get("testable"):
            report.add("DISCOVERY", f"page {path}", SKIPPED, "not testable")
            continue
        try:
            r = session.get(f"{base}{path}", timeout=args.timeout)
        except requests.exceptions.RequestException as e:
            report.add("UI", f"page {path}", FAIL, f"network error: {type(e).__name__}")
            print(f"  {FAIL}  {path}  (network error: {type(e).__name__})")
            continue
        pages_tested += 1
        if r.status_code == 200:
            ct = r.headers.get("content-type", "").lower()
            if "html" in ct or "text/html" in ct:
                report.add("UI", f"page {path}", PASS, f"{len(r.content)}B")
                print(f"  {PASS}  {path}  ({len(r.content)}B, {ct.split(';')[0]})")
            else:
                report.add("UI", f"page {path}", WARNING, f"not HTML: {ct}")
                print(f"  {WARNING}  {path}  content-type={ct}")
        else:
            report.add("UI", f"page {path}", FAIL, f"HTTP {r.status_code}")
            print(f"  {FAIL}  {path}  HTTP {r.status_code}")
    if not pages:
        print("  (no pages in manifest)")

    # ── 4. Read-only API smoke test ─────────────────────────────────────
    print("\n[4] READ-ONLY APIs (GET, risk=read_only)")
    api_list = mf_json.get("api", [])
    apis_tested = 0
    for a in api_list:
        path = a.get("path")
        method = a.get("method")
        risk = a.get("risk")
        # Only smoke-test GET + read_only; skip write/safe_write.
        if method != "GET" or risk != "read_only":
            continue
        # Skip endpoints that need query params or are obviously streaming.
        if "?" in path or "/api/monitor" in path:
            report.add("API", f"{method} {path}", SKIPPED, "needs params/stream")
            continue
        # /api/config requires ?section=...; test it with a known valid section.
        test_path = path
        if path == "/api/config":
            test_path = path + "?section=system"
        try:
            r = session.get(f"{base}{test_path}", timeout=args.timeout)
        except requests.exceptions.RequestException as e:
            report.add("API", f"{method} {path}", FAIL, f"network error: {type(e).__name__}")
            print(f"  {FAIL}  {method} {path}  (network error: {type(e).__name__})")
            continue
        apis_tested += 1
        body_ok = True
        detail = f"HTTP {r.status_code}"
        if r.status_code == 200:
            try:
                r.json()
                detail += ", valid JSON"
            except ValueError:
                body_ok = False
                detail += ", INVALID JSON"
        if r.status_code == 200 and body_ok:
            report.add("API", f"{method} {path}", PASS, detail)
            print(f"  {PASS}  {method} {path}  ({detail})")
        elif r.status_code in (501, 404):
            # A "disabled" or unknown feature endpoint is not a failure of the
            # device — it indicates the feature is not compiled in.
            report.add("API", f"{method} {path}", SKIPPED, f"HTTP {r.status_code} (feature disabled/absent)")
            print(f"  {SKIPPED}  {method} {path}  ({detail})")
        else:
            report.add("API", f"{method} {path}", FAIL, detail)
            print(f"  {FAIL}  {method} {path}  ({detail})")

    # ── 5. Invariants (manifest-declared) ───────────────────────────────
    print("\n[5] INVARIANTS")
    invariants = mf_json.get("invariants", [])
    for iv in invariants:
        name = iv.get("id")
        itype = iv.get("type")
        if itype == "feature_page":
            # Feature flag vs page presence is already covered by the manifest
            # generation; we cross-check the page actually serves.
            page = iv.get("page")
            try:
                r = session.get(f"{base}{page}", timeout=args.timeout)
                res = PASS if r.status_code == 200 else FAIL
                report.add("INVARIANT", name, res, f"{page} HTTP {r.status_code}")
                print(f"  {res}  {name}  ({page})")
            except requests.exceptions.RequestException as e:
                report.add("INVARIANT", name, FAIL, f"network error: {type(e).__name__}")
                print(f"  {FAIL}  {name}  (network error: {type(e).__name__})")
        elif itype == "value_consistency":
            api = iv.get("api")
            expect = iv.get("expect")
            report.add("INVARIANT", name, NOT_VERIFIED, f"needs authored eval: {expect}")
            print(f"  {NOT_VERIFIED}  {name}  ({api})")
        else:
            report.add("INVARIANT", name, SKIPPED, f"unknown type {itype}")
            print(f"  {SKIPPED}  {name}  type={itype}")

    # ── Report ──────────────────────────────────────────────────────────
    summary = report.summary()
    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"  Pages tested       : {pages_tested}")
    print(f"  Read-only APIs     : {apis_tested}")
    for k in (PASS, FAIL, WARNING, SKIPPED, NOT_VERIFIED):
        if summary[k]:
            print(f"  {k:<12}: {summary[k]}")
    overall = FAIL if summary[FAIL] else (WARNING if summary[WARNING] else PASS)
    print(f"\n  RESULT: {overall}")
    print("=" * 70)

    return 1 if summary[FAIL] else 0


if __name__ == "__main__":
    sys.exit(main())
