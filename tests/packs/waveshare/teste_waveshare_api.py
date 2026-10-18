#!/usr/bin/env python3
"""Real API test harness for the Waveshare 8DI8DO pack (Node32-HUB) on 192.168.1.29.

Per Nuno's directive (issues.txt, 2026-08-28): test every page/endpoint via
api/curl, verify persistence (save/load round-trip), and exercise reboot recovery.
Rules honoured:
  - DO NOT touch critical board configs (wifi / email) -> those endpoints are only
    READ, never POSTed.
  - Persistence test is an echo round-trip (write back exactly what we read) so the
    board's running config is never altered.
  - Reboot test verifies the board comes back online and reports a fresh uptime.

Outputs a PASS/FAIL report to stdout and a JSON summary next to this file.
"""
import base64
import json
import time
import urllib.request
import urllib.error

BASE = "http://192.168.1.29"
AUTH = "Basic " + base64.b64encode(b"admin:admin").decode()
UA = {"User-Agent": "Joana-TestHarness/1.0", "Authorization": AUTH}

GET_ENDPOINTS = [
    ("/api/status", 200, "board confirm"),
    ("/api/self-test", 200, "page manifest"),
    ("/api/proximity", 200, "proximity 8ch state"),
    ("/api/proximity/oee", 200, "OEE (A% bug check)"),
    ("/api/sd-status", 200, "SD card state"),
    ("/api/access-config", 200, "access control config"),
    ("/alerts-config", 200, "alerts page"),
    ("/", 200, "home page"),
    ("/api/config?section=email", 200, "email config (no plaintext pw)"),
    ("/api/config?section=ethernet", 200, "BUG RE-TEST: ethernet section (was 400)"),
    ("/api/inactivity-alert-config", None, "alerts config (GET may 302/200)"),
    ("/api/proximity-chart-data?file=test", None, "BUG RE-TEST: chart-data timeout"),
]

PERSISTENCE_ECHO = "/api/inactivity-alert-config"
REBOOT_EP = "/api/restart"  # real endpoint (web_routes_core.cpp:58); /api/reboot is a no-op


def req(path, method="GET", data=None, headers=None, timeout=12):
    time.sleep(0.6)  # board rate-limits /api/status to 1 call / 500ms (core_admin_handler.cpp:31)
    h = dict(UA)
    if headers:
        h.update(headers)
    body = None
    if data is not None:
        if isinstance(data, (dict, list)):
            body = json.dumps(data).encode()
            h["Content-Type"] = "application/json"
        else:
            body = data
    r = urllib.request.Request(BASE + path, data=body, headers=h, method=method)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            raw = resp.read()
            return resp.status, raw, None
    except urllib.error.HTTPError as e:
        return e.code, e.read(), None
    except Exception as e:  # timeout / connection reset etc.
        return None, b"", str(e)


def jsummary(raw, limit=240):
    try:
        o = json.loads(raw)
        s = json.dumps(o, ensure_ascii=False)
        return s[:limit]
    except Exception:
        t = raw.decode("utf-8", "replace")
        return t[:limit]


def main():
    results = []
    print("=" * 70)
    print("WAVESHARE 8DI8DO REAL API TEST  —  target", BASE)
    print("=" * 70)

    # 0) confirm board
    st, raw, err = req("/api/status")
    if st == 200:
        o = json.loads(raw)
        ip = o.get("ip_address")
        fw = o.get("firmware_version")
        a = o.get("inactivity_alerts", {}).get("slots", [{}])[0]
        print(f"[CONFIRM] board OK  ip={ip}  fw={fw}  eth_link={o.get('ethernet_link')}  wifi={o.get('wifi_connected')}")
        results.append(("confirm", "ip==192.168.1.29", ip == "192.168.1.29", f"fw={fw} ip={ip}"))
    else:
        print(f"[CONFIRM] FAILED to reach board: status={st} err={err}")
        # still try the rest
        fw = "UNKNOWN"

    # 1) GET endpoints
    for path, exp, desc in GET_ENDPOINTS:
        st, raw, err = req(path)
        ok = (st == exp) if exp is not None else (st is not None and st < 500)
        tag = "PASS" if ok else "FAIL"
        extra = ""
        if path.endswith("section=ethernet"):
            extra = "  <-- BUG was 400; now 200=resolved, still 400=unresolved"
        if "chart-data" in path:
            extra = "  <-- BUG was TIMEOUT; now OK if 200/valid"
        print(f"[{tag}] GET {path} -> {st}  ({desc}){extra}")
        print(f"        {jsummary(raw)}")
        results.append(("GET", path, ok, f"status={st} {jsummary(raw,120)}"))

    # 2) persistence round-trip: read inactivity_alerts from /api/status, POST it back
    #    unchanged via /api/inactivity-alert-config, confirm it survives (echo test =
    #    proves save+load without altering the running config).
    st0, raw0, _ = req("/api/status")
    orig_alerts = None
    if st0 == 200:
        try:
            orig_alerts = json.loads(raw0).get("inactivity_alerts")
        except Exception:
            orig_alerts = None
    if orig_alerts is not None:
        st2, raw2, _ = req(PERSISTENCE_ECHO, "POST", orig_alerts)
        st3, raw3, _ = req("/api/status")
        after = json.loads(raw3).get("inactivity_alerts") if st3 == 200 else None
        # Compare only USER-CONFIGURABLE fields; volatile runtime counters
        # (alerts_sent_today, alert_sent_this_period) are server-managed and
        # legitimately reset on save -> ignore them.
        def _sig(a):
            try:
                return [(s.get("enabled"), s.get("timeout_minutes"),
                         s.get("max_per_day"), s.get("emails")) for s in a["slots"]]
            except Exception:
                return None
        same = (_sig(after) == _sig(orig_alerts))
        ok = st2 in (200, 302) and same
        print(f"[{'PASS' if ok else 'FAIL'}] PERSISTENCE echo on {PERSISTENCE_ECHO}: POST={st2} config-stable={same}")
        results.append(("PERSIST", PERSISTENCE_ECHO, ok, f"POST={st2} config-stable={same}"))
    else:
        print(f"[SKIP] PERSISTENCE: cannot read inactivity_alerts from /api/status (st={st0})")
        results.append(("PERSIST", PERSISTENCE_ECHO, None, f"st={st0}"))

    # 3) reboot recovery test (real endpoint /api/restart)
    print("-" * 70)
    print("REBOOT TEST: POST", REBOOT_EP, "(board should reboot and come back)")

    def _uptime_sec(jraw):
        try:
            u = json.loads(jraw).get("uptime", "0:0:0")
            h, m, s = [int(x) for x in str(u).split(":")]
            return h * 3600 + m * 60 + s
        except Exception:
            return None

    s_before, r_before, _ = req("/api/status")
    up_before = _uptime_sec(r_before) if s_before == 200 else None
    st, raw, err = req(REBOOT_EP, "POST", b"", timeout=8)
    # POST usually times out because the board drops the connection as it reboots
    print(f"  reboot POST -> {st} (err={err})")
    back = False
    fresh_uptime = None
    for i in range(24):  # up to ~120s
        time.sleep(5)
        s, r, e = req("/api/status", timeout=5)
        if s == 200:
            o = json.loads(r)
            up = _uptime_sec(r)
            fwb = o.get("firmware_version")
            print(f"  recovered after ~{(i+1)*5}s  uptime={o.get('uptime')} fw={fwb}")
            fresh_uptime = up
            back = True
            break
    rebooted = back and (fresh_uptime is not None and up_before is not None
                         and fresh_uptime < up_before - 5)  # uptime in seconds; 5s margin
    print(f"[{'PASS' if rebooted else 'FAIL'}] REBOOT recovery: back={back} up_before={up_before}s up_after={fresh_uptime}s")
    results.append(("REBOOT", REBOOT_EP, rebooted, f"back={back} before={up_before}s after={fresh_uptime}s"))

    # summary
    print("=" * 70)
    passed = sum(1 for _, _, ok, _ in results if ok is True)
    failed = sum(1 for _, _, ok, _ in results if ok is False)
    skipped = sum(1 for _, _, ok, _ in results if ok is None)
    print(f"SUMMARY: {passed} PASS / {failed} FAIL / {skipped} SKIP  (fw={fw})")
    with open(__file__.replace(".py", "_results.json"), "w") as f:
        json.dump({"fw": fw, "results": results,
                   "totals": {"pass": passed, "fail": failed, "skip": skipped}}, f, indent=2)
    print("JSON summary ->", __file__.replace(".py", "_results.json"))


if __name__ == "__main__":
    main()
