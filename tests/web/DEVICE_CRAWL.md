# device_crawl.js — crawl every page of a pack and keep evidence

Headless-Chromium crawler for a **real board**. It asks the device which pages
it has (`/api/test-manifest`), visits every one, clicks the page's tabs/menus,
and writes a bundle an agent (or a human) can review afterwards.

No npm dependencies: it talks raw CDP to the system Chromium
(`/usr/bin/chromium`, or `$CHROME_BIN`).

## Usage — on demand, one pack + one IP per run

No IP file is stored and nothing runs on a schedule: you say the pack and the
IP, it runs once and writes the evidence bundle.

```bash
node tests/web/device_crawl.js --pack rambox32 --host 192.168.1.99
node tests/web/device_crawl.js --pack s3_cnc  --host 192.168.1.38 --auth admin:admin
node tests/web/device_crawl.js --pack waveshare --host 192.168.1.50 --mobile
```

`--host` takes the **full IPv4 address** (no shorthand).

| Option | Meaning |
|---|---|
| `--pack <name>` | label used in the report/evidence folder |
| `--host <ip>` | board IPv4 address |
| `--auth user:pass` | HTTP basic auth |
| `--mobile` | 390x844 (phone) instead of the default 1280x800 desktop |
| `--no-tabs` | skip the menu/tab pass (not recommended: coverage drops) |
| `--timeout <ms>` | per-page timeout (default 25000) |
| `--out <dir>` | evidence root (default `artifacts/device_crawl`) |

Exit code `0` = all pages PASS, `1` = at least one FAIL (CI friendly), `2` = bad usage/launch.

## What is checked per page

**Coverage is 100%**: every page the device declares, every tab/section on it,
every one screenshotted and validated — nothing is sampled. Pages the build
declares as disabled/not-testable are reported as `SKIP` with the reason, so the
report accounts for the whole manifest.

| Check | Severity |
|---|---|
| HTTP status of the document is 200 | FAIL |
| `console.error` / uncaught JS exception | FAIL |
| Rendered text contains `&#123;`, `\u{XXXX}`, `â€`, `Ã…` (escaped/mojibake) | FAIL |
| Failed requests / HTTP >= 400 (assets, XHR) | WARN |
| `data-i18n-ready` never becomes `1` | WARN (noted) |
| Each tab/section: screenshot + the same text check + new console errors | FAIL / WARN |

Pages are fetched with `?v=<firmware_build>` so the PROGMEM/ETag cache never
serves the previous build's page (a real cause of "fix is not showing").

## Evidence bundle

```
artifacts/device_crawl/<timestamp>/<pack>_<ip>/
  report.json      full results (per page + per tab, console, HTTP, findings)
  summary.txt      one line per page: PASS/FAIL/WARN + reason
  index.html       gallery — open in a browser, images lazy-loaded
  console.log      raw run log
  manifest.json    the device's own page manifest (audit trail)
  screenshots/
    home.png
    access.png
    access__tab0.png   (one per tab/menu)
```

`artifacts/` is git-ignored; keep bundles for the build under test and diff
them between runs.

## Recommended loop

```bash
python3 chatgpt/scripts/run_native_tests.py                 # 90/90 gate
./build_firmware.sh -e n16r8 -p S3_CNC --bg                 # ~11 min with ccache
curl -X POST http://192.168.1.38/api/enable-ota             # 5-minute window
curl -F file=@<bin> -F filesize=<bytes> http://192.168.1.38/api/firmware-upload
node tests/web/device_crawl.js --pack s3_cnc --host 192.168.1.38
python3 scripts/audit_device.py --host 192.168.1.38          # API/self-test audit
```

Rules: one sacrificial board per pack, never a board that is streaming, keep
the previous `.bin` for rollback, and run a single Chromium at a time (the Pi
has 4 GB).
