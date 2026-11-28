#!/usr/bin/env python3
"""TDK-Lambda CSV report — what the supply did, from the file it wrote.

The module logs one row per poll (or per N polls with `csv_every`), table or
sensor style. This tool turns that into the numbers that matter for electrical
behaviour: energy, apparent load resistance, time in current limit, CV/CC
transitions, standby and the drift hour by hour. Stdlib only (csv, datetime,
math) — no pandas, no plotting library: the chart is a hand-written SVG.

Usage
  python3 tests/packs/rs232/tdk_csv_report.py TDK_27-11-2026.csv
  python3 tests/packs/rs232/tdk_csv_report.py day.csv --out report.md --svg report.svg
  python3 tests/packs/rs232/tdk_csv_report.py --selftest

Reads both CSV schemas:
  v1  timestamp,v_meas,v_set,i_meas,i_set,p_w,ovp,uvl,mode,out
  v2  ... ,samples,v_min,v_max,i_min,i_max,wh        (csv_every > 1, or new firmware)
and the sensor style ("2026-11-27 14:03:00, vout, 600.22, vmin, ..."),
with or without the "#" header line the firmware writes on a new day file.

Exit codes: 0 ok, 1 unreadable/no rows, 2 selftest failure.
"""

import argparse
import csv
import datetime as dt
import math
import os
import sys
import tempfile


# ── parsing ────────────────────────────────────────────────────────────────
def _num(s):
    try:
        return float(str(s).strip())
    except (TypeError, ValueError):
        return None


def _ts(s):
    s = str(s).strip()
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M"):
        try:
            return dt.datetime.strptime(s, fmt)
        except ValueError:
            pass
    return None


def _sensor_record(line):
    """'ts, key, val, key, val...' -> dict (the module's sensor-style row)."""
    parts = [p.strip() for p in line.split(",")]
    if len(parts) < 5:
        return None
    ts = _ts(parts[0])
    if ts is None:
        return None
    rec = {"ts": ts}
    for k in range(1, len(parts) - 1, 2):
        key = parts[k].strip().lower()
        val = parts[k + 1].strip()
        if key in ("mode",):
            rec["mode"] = val
        elif key in ("out",):
            rec["out"] = val.upper() in ("ON", "1", "TRUE")
        else:
            n = _num(val)
            if n is not None:
                rec[key] = n
    # normalise to the table field names
    out = {"ts": ts, "mode": rec.get("mode", ""), "out": rec.get("out", True),
           "v": rec.get("vout"), "i": rec.get("iout"),
           "vset": rec.get("vset"), "iset": rec.get("iset"),
           "ovp": rec.get("ovp"), "uvl": rec.get("uvl")}
    for k in ("vmin", "vmax", "imin", "imax", "wh", "n"):
        if k in rec:
            out[k] = rec[k]
    return out


def load(path):
    """Returns (records, schema) where schema is 'v1', 'v2' or 'sensor'."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        # A new day file starts with a header: "# timestamp, vout, ..." for the
        # sensor style, "timestamp,..." for the table style. Comments are skipped.
        lines = [ln.rstrip("\n") for ln in fh
                 if ln.strip() and not ln.lstrip().startswith("#")]
    if not lines:
        return [], "empty"

    schema = "sensor" if "vout" in lines[0].lower() else None
    header = []
    rows = []
    if schema != "sensor":
        header = [h.strip().lower() for h in next(csv.reader([lines[0]]))]
        if header and header[0].startswith("timestamp"):
            body = lines[1:]
        else:
            header = ["timestamp", "v_meas", "v_set", "i_meas", "i_set", "p_w",
                      "ovp", "uvl", "mode", "out"]
            body = lines
        schema = "v2" if "v_min" in header else "v1"
        for ln in body:
            cols = [c.strip() for c in next(csv.reader([ln]))]
            if len(cols) < len(header):
                continue
            cell = dict(zip(header, cols))
            ts = _ts(cell.get("timestamp", ""))
            if ts is None:
                continue
            rec = {"ts": ts,
                   "v": _num(cell.get("v_meas")), "i": _num(cell.get("i_meas")),
                   "vset": _num(cell.get("v_set")), "iset": _num(cell.get("i_set")),
                   "ovp": _num(cell.get("ovp")), "uvl": _num(cell.get("uvl")),
                   "mode": cell.get("mode", ""),
                   "out": (cell.get("out", "1") in ("1", "ON", "on", "true", "True"))}
            for k_src, k_dst in (("v_min", "vmin"), ("v_max", "vmax"),
                                 ("i_min", "imin"), ("i_max", "imax")):
                if k_src in cell:
                    rec[k_dst] = _num(cell[k_src])
            if "wh" in cell:
                rec["wh"] = _num(cell["wh"])
            if "samples" in cell:
                rec["n"] = _num(cell["samples"])
            rows.append(rec)
        return rows, schema

    for ln in lines:
        rec = _sensor_record(ln)
        if rec and rec.get("v") is not None:
            rows.append(rec)
    return rows, schema


# ── analysis ───────────────────────────────────────────────────────────────
def _median(values):
    if not values:
        return 0.0
    v = sorted(values)
    mid = len(v) // 2
    return v[mid] if len(v) % 2 else 0.5 * (v[mid - 1] + v[mid])


def analyse(rows):
    rows = [r for r in rows if r.get("v") is not None and r.get("i") is not None]
    rows.sort(key=lambda r: r["ts"])
    if not rows:
        return None

    gaps = [(rows[k + 1]["ts"] - rows[k]["ts"]).total_seconds() for k in range(len(rows) - 1)]
    step = _median([g for g in gaps if g > 0]) or 60.0
    span_s = (rows[-1]["ts"] - rows[0]["ts"]).total_seconds() + step

    energy_wh = 0.0
    cc_s = lim_s = off_s = stab_s = 0.0
    transitions = 0
    prev_mode = ""
    r_values = []
    peaks = []
    for k, r in enumerate(rows):
        d = gaps[k] if k < len(gaps) and gaps[k] > 0 else step
        v, i = r["v"], r["i"]
        if r.get("wh") is not None:
            energy_wh += r["wh"]
        else:
            energy_wh += v * i * d / 3600.0
        if not r["out"]:
            off_s += d
        cc = (r["mode"] or "").upper() == "CC"
        if cc:
            cc_s += d
        iset = r.get("iset") or 0.0
        if iset > 0 and i >= 0.98 * iset:
            lim_s += d
        if prev_mode and r["mode"] and (cc and prev_mode.upper() == "CV" or
                                        r["mode"].upper() == "CV" and prev_mode.upper() == "CC"):
            transitions += 1
        if r["mode"]:
            prev_mode = r["mode"]
        if i > 0.02:
            r_values.append(v / i)
        if r.get("vmin") is not None and r.get("vmax") is not None:
            peaks.append((r["vmax"] - r["vmin"], r["ts"], r["v"], r["vmin"], r["vmax"]))
        if r["out"] and i < 0.02:
            stab_s += d

    vs = [r["v"] for r in rows]
    i_s = [r["i"] for r in rows]
    ps = [r["v"] * r["i"] for r in rows]

    # per-hour buckets
    hours = {}
    for k, r in enumerate(rows):
        d = gaps[k] if k < len(gaps) and gaps[k] > 0 else step
        key = r["ts"].strftime("%Y-%m-%d %H")
        b = hours.setdefault(key, {"n": 0, "v": 0.0, "i": 0.0, "wh": 0.0, "cc": 0.0,
                                   "r": [], "prod": 0})
        b["n"] += 1
        b["v"] += r["v"]
        b["i"] += r["i"]
        if r.get("wh") is not None:
            b["wh"] += r["wh"]
        else:
            b["wh"] += r["v"] * r["i"] * d / 3600.0
        if (r["mode"] or "").upper() == "CC":
            b["cc"] += d
        if r["i"] > 0.02:
            b["r"].append(r["v"] / r["i"])
            b["prod"] += 1
    hourly = []
    for key in sorted(hours):
        b = hours[key]
        hourly.append({"hour": key, "n": b["n"],
                       "v": b["v"] / b["n"], "i": b["i"] / b["n"],
                       "wh": b["wh"], "cc": b["cc"], "prod": b["prod"],
                       "r": (sum(b["r"]) / len(b["r"])) if b["r"] else None})

    # Drift only means something while the load is on: compare the first and the
    # last hour that actually delivered current (a standby hour would hide it).
    worked = [h for h in hourly if h["prod"]]
    drift_v = drift_i = None
    if len(worked) >= 2:
        drift_v = worked[-1]["v"] - worked[0]["v"]
        drift_i = worked[-1]["i"] - worked[0]["i"]

    peaks.sort(reverse=True)
    return {
        "rows": len(rows), "start": rows[0]["ts"], "end": rows[-1]["ts"],
        "span_s": span_s, "step_s": step,
        "v_avg": sum(vs) / len(vs), "v_min": min(vs), "v_max": max(vs),
        "i_avg": sum(i_s) / len(i_s), "i_min": min(i_s), "i_max": max(i_s),
        "p_avg": sum(ps) / len(ps), "p_peak": max(ps),
        "energy_wh": energy_wh,
        "kwh": energy_wh / 1000.0,
        "cc_s": cc_s, "lim_s": lim_s, "off_s": off_s, "standby_s": stab_s,
        "transitions": transitions,
        "r_min": min(r_values) if r_values else None,
        "r_avg": (sum(r_values) / len(r_values)) if r_values else None,
        "r_max": max(r_values) if r_values else None,
        "hourly": hourly, "drift_v": drift_v, "drift_i": drift_i,
        "peaks": peaks[:5],
        "has_windows": any(r.get("vmin") is not None for r in rows),
        "efficiency_note": "energy from the wh column" if any(r.get("wh") is not None for r in rows)
                           else "energy by the rectangle rule on p=u*i",
    }


def _dur(seconds):
    s = int(round(seconds))
    h, m = s // 3600, (s % 3600) // 60
    if h:
        return "%dh %02dm" % (h, m)
    if m:
        return "%dm %02ds" % (m, s % 60)
    return "%ds" % s


def report_md(stats, path, schema):
    L = []
    add = L.append
    add("# TDK-Lambda CSV report")
    add("")
    add("`%s` · schema **%s** · %d rows · %s → %s (%s, one row per %s)" % (
        os.path.basename(path), schema, stats["rows"],
        stats["start"].strftime("%Y-%m-%d %H:%M:%S"), stats["end"].strftime("%H:%M:%S"),
        _dur(stats["span_s"]), _dur(stats["step_s"])))
    add("")
    add("## Output")
    add("")
    add("| Metric | Value |")
    add("|---|---|")
    add("| Voltage | %.2f V avg · %.2f min · %.2f max |" % (stats["v_avg"], stats["v_min"], stats["v_max"]))
    add("| Current | %.3f A avg · %.3f min · %.3f max |" % (stats["i_avg"], stats["i_min"], stats["i_max"]))
    add("| Power | %.1f W avg · %.1f W peak |" % (stats["p_avg"], stats["p_peak"]))
    add("| Energy | **%.3f kWh** (%s) |" % (stats["kwh"], stats["efficiency_note"]))
    if stats["r_avg"]:
        add("| Apparent load | %.1f Ω avg · %.1f min · %.1f max |" % (
            stats["r_avg"], stats["r_min"], stats["r_max"]))
    add("")
    add("## Behaviour")
    add("")
    add("| Metric | Value |")
    add("|---|---|")
    add("| Time in CC (current limit) | %s (%.0f %% of the period) |" % (
        _dur(stats["cc_s"]), 100.0 * stats["cc_s"] / stats["span_s"] if stats["span_s"] else 0))
    add("| Time with I at the setpoint | %s |" % _dur(stats["lim_s"]))
    add("| Time with the output off | %s |" % _dur(stats["off_s"]))
    add("| Standby (output on, no load) | %s |" % _dur(stats["standby_s"]))
    add("| CV↔CC transitions | %d |" % stats["transitions"])
    if stats["drift_v"] is not None:
        add("| Drift, first → last hour with load | V %+.2f V · I %+.3f A |" % (
            stats["drift_v"], stats["drift_i"]))
    add("")
    if stats["hourly"]:
        add("## Hour by hour")
        add("")
        add("| Hour | Rows | V avg | I avg | Power | Energy | In CC | R avg |")
        add("|---|---|---|---|---|---|---|---|")
        for h in stats["hourly"]:
            add("| %s | %d | %.2f V | %.3f A | %.1f W | %.1f Wh | %s | %s |" % (
                h["hour"], h["n"], h["v"], h["i"], h["v"] * h["i"], h["wh"],
                _dur(h["cc"]), ("%.0f Ω" % h["r"]) if h["r"] else "—"))
        add("")
    if stats["peaks"]:
        add("## Largest excursions inside a row")
        add("")
        add("The row's own min/max (needs `samples`/`v_min`…, i.e. `csv_every` > 1):")
        add("")
        add("| Time | V min | V max | Spread |")
        add("|---|---|---|---|")
        for spread, ts, v, vmin, vmax in stats["peaks"]:
            add("| %s | %.2f V | %.2f V | %.2f V |" % (ts.strftime("%H:%M:%S"), vmin, vmax, spread))
        add("")
    add("## Reading it")
    add("")
    add("- **R falling** with V steady means the load is drawing more (element ageing, short); "
        "**R rising** slowly is thermal drift of a resistive load.")
    add("- **Time in CC** is the honest measure of an undersized supply: the load wants more than "
        "the setpoint allows.")
    add("- **CV↔CC transitions** cluster at start-up (inrush) or at load steps.")
    add("- One sample per row cannot see ripple or transients; raise the sampling rate "
        "(`csv_every`) for the min/max of a window.")
    add("")
    return "\n".join(L)


# ── chart (hand-written SVG, no dependency) ────────────────────────────────
def svg_chart(rows, stats, width=900, height=520):
    if not rows:
        return ""
    t0 = rows[0]["ts"]
    span = max((rows[-1]["ts"] - t0).total_seconds(), 1.0)
    vmin, vmax = stats["v_min"], stats["v_max"]
    imin, imax = stats["i_min"], stats["i_max"]
    pad_v = max((vmax - vmin) * 0.1, 0.5)
    pad_i = max((imax - imin) * 0.1, 0.01)
    vmin, vmax = vmin - pad_v, vmax + pad_v
    imin, imax = imin - pad_i, imax + pad_i
    if stats["i_min"] >= 0.0 and imin < 0.0:
        imin = 0.0            # a current axis that goes negative is just noise

    def x(ts):
        return 60.0 + (width - 90.0) * ((ts - t0).total_seconds() / span)

    def yv(v):
        return 20.0 + 180.0 * (1.0 - (v - vmin) / (vmax - vmin))

    def yi(i):
        return 280.0 + 180.0 * (1.0 - (i - imin) / (imax - imin))

    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
           'viewBox="0 0 %d %d" font-family="sans-serif">' % (width, height, width, height),
           '<rect width="100%" height="100%" fill="#0b1220"/>']
    for label, top, colour, vmin_, vmax_, f, unit, key in (
            ("Voltage (V)", 20.0, "#60a5fa", vmin, vmax, yv, "V", "v"),
            ("Current (A)", 280.0, "#fbbf24", imin, imax, yi, "A", "i")):
        out.append('<text x="60" y="%.0f" fill="#93c5fd" font-size="13">%s</text>' % (top - 6, label))
        for g in range(5):
            y = top + g * 45.0
            out.append('<line x1="60" y1="%.1f" x2="%d" y2="%.1f" stroke="#1e293b"/>' %
                       (y, width - 30, y))
        span_ = vmax_ - vmin_
        for g in range(5):
            val = vmax_ - span_ * g / 4.0
            out.append('<text x="4" y="%.1f" fill="#94a3b8" font-size="10">%.2f</text>' %
                       (top + g * 45.0 + 3, val))
        pts = " ".join("%.1f,%.1f" % (x(r["ts"]), f(r[key])) for r in rows)
        out.append('<polyline fill="none" stroke="%s" stroke-width="1.8" points="%s"/>' %
                   (colour, pts))
        out.append('<text x="%d" y="%.0f" fill="#94a3b8" font-size="10">%s</text>' %
                   (width - 28, top + 8, unit))
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        ts = t0 + dt.timedelta(seconds=span * frac)
        out.append('<text x="%.0f" y="%d" fill="#94a3b8" font-size="10" text-anchor="middle">%s</text>' %
                   (x(ts), height - 8, ts.strftime("%H:%M")))
    out.append('<text x="60" y="%d" fill="#e2e8f0" font-size="12">%s · %.3f kWh · CC %s · %d CV/CC</text>' %
               (height - 28, factor_caption(stats), stats["kwh"], _dur(stats["cc_s"]),
                stats["transitions"]))
    out.append("</svg>")
    return "\n".join(out)


def factor_caption(stats):
    return "%d rows · %.1f W avg · %.1f W peak" % (stats["rows"], stats["p_avg"], stats["p_peak"])


# ── selftest ───────────────────────────────────────────────────────────────
def selftest():
    tmp = tempfile.mkdtemp(prefix="tdk_csv_")
    path = os.path.join(tmp, "TDK_test.csv")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("timestamp,v_meas,v_set,i_meas,i_set,p_w,ovp,uvl,mode,out,"
                 "samples,v_min,v_max,i_min,i_max,wh\n")
        t = dt.datetime(2026, 11, 27, 10, 0, 0)
        # 60 rows, 1/min: 30 min CV at 600 V / 1.0 A, then 30 min CC at 1.3 A.
        for k in range(60):
            ts = (t + dt.timedelta(minutes=k)).strftime("%Y-%m-%d %H:%M:%S")
            if k < 30:
                v, i, mode = 600.0, 1.0, "CV"
            else:
                v, i, mode = 600.0, 1.3, "CC"
            wh = v * i / 60.0
            fh.write("%s,%.2f,600.00,%.3f,1.300,%.2f,660.0,0.0,%s,1,1,%.2f,%.2f,%.3f,%.3f,%.4f\n" %
                     (ts, v, i, v * i, mode, v, v, i, i, wh))
    rows, schema = load(path)
    assert schema == "v2", schema
    st = analyse(rows)
    assert st["rows"] == 60, st["rows"]
    # 30 min at 600 W + 30 min at 780 W = 300 + 390 = 690 Wh
    assert abs(st["kwh"] - 0.690) < 0.01, st["kwh"]
    assert abs(st["cc_s"] - 1800) < 1, st["cc_s"]
    assert abs(st["lim_s"] - 1800) < 1, st["lim_s"]
    assert st["transitions"] == 1, st["transitions"]
    assert abs(st["r_avg"] - ((600.0 / 1.0 + 600.0 / 1.3) / 2)) < 30, st["r_avg"]
    md = report_md(st, path, schema)
    assert "0.690 kWh" in md, md[:200]
    assert "Hour by hour" in md
    chart = svg_chart(rows, st)
    assert chart.startswith("<svg") and chart.rstrip().endswith("</svg>")

    # v1 (no wh column): the rectangle rule must give the same energy
    path1 = os.path.join(tmp, "TDK_v1.csv")
    with open(path1, "w", encoding="utf-8") as fh:
        fh.write("timestamp,v_meas,v_set,i_meas,i_set,p_w,ovp,uvl,mode,out\n")
        for k in range(10):
            ts = (t + dt.timedelta(minutes=k)).strftime("%Y-%m-%d %H:%M:%S")
            fh.write("%s,600.00,600.00,1.000,1.300,600.00,660.0,0.0,CV,1\n" % ts)
    rows1, schema1 = load(path1)
    assert schema1 == "v1", schema1
    st1 = analyse(rows1)
    assert abs(st1["kwh"] - (600.0 / 60.0 * 10) / 1000.0) < 0.02, st1["kwh"]

    # sensor style
    path2 = os.path.join(tmp, "TDK_sensor.csv")
    with open(path2, "w", encoding="utf-8") as fh:
        for k in range(5):
            ts = (t + dt.timedelta(minutes=k)).strftime("%Y-%m-%d %H:%M:%S")
            fh.write("%s, vout, 600.00, vmin, 599.90, vmax, 600.10, iout, 1.000, "
                     "imin, 0.990, imax, 1.010, power, 600.00, wh, 10.000, n, 1, "
                     "vset, 600.00, iset, 1.300, ovp, 660.0, uvl, 0.0, mode, CV, out, ON\n" % ts)
    rows2, schema2 = load(path2)
    assert schema2 == "sensor", schema2
    assert len(rows2) == 5, len(rows2)
    st2 = analyse(rows2)
    assert abs(st2["kwh"] - 0.050) < 0.005, st2["kwh"]

    # sensor style with the "#" header the firmware now writes on a new file
    path3 = os.path.join(tmp, "TDK_sensor_hdr.csv")
    with open(path3, "w", encoding="utf-8") as fh:
        fh.write("# timestamp, vout, vmin, vmax, iout, imin, imax, power, wh, n, "
                 "vset, iset, ovp, uvl, mode, out\n")
        for k in range(5):
            ts = (t + dt.timedelta(minutes=k)).strftime("%Y-%m-%d %H:%M:%S")
            fh.write("%s, vout, 600.00, vmin, 599.90, vmax, 600.10, iout, 1.000, "
                     "imin, 0.990, imax, 1.010, power, 600.00, wh, 10.000, n, 1, "
                     "vset, 600.00, iset, 1.300, ovp, 660.0, uvl, 0.0, mode, CV, out, ON\n" % ts)
    rows3, schema3 = load(path3)
    assert schema3 == "sensor", schema3
    assert len(rows3) == 5, len(rows3)
    assert abs(analyse(rows3)["kwh"] - 0.050) < 0.005
    print("selftest OK — v2/v1/sensor parsed, energy/CC/limit/transition/SVG checks pass")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv", nargs="?", help="TDK CSV file (table or sensor style)")
    ap.add_argument("--out", help="write the markdown report here (default: stdout)")
    ap.add_argument("--svg", help="write the chart here")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.csv:
        ap.print_help()
        return 1

    rows, schema = load(args.csv)
    stats = analyse(rows)
    if not stats:
        print("no usable rows in %s (need timestamp + v + i)" % args.csv, file=sys.stderr)
        return 1
    text = report_md(stats, args.csv, schema)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
        print("report: %s" % args.out)
    else:
        print(text)
    if args.svg:
        with open(args.svg, "w", encoding="utf-8") as fh:
            fh.write(svg_chart(rows, stats) + "\n")
        print("chart:  %s" % args.svg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
