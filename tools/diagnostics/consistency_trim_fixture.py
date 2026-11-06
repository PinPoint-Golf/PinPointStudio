#!/usr/bin/env python3
"""Consistency: cut a committed test fixture from an extract file (consistency_extract.py) so that consistency_dump's
`result` over the cut EQUALS its result over the whole file. The 5 Oct fixture is made by

    python3 tools/diagnostics/consistency_trim_fixture.py <fixtures>/Mark-Liversedge/2026-10-05_Mark-Liversedge_Wrist_01.json \\
        docs/research/data/consistency/consistency_readings.csv src/Gui/tests/data/charts/session_1005.json

Every shot and every metric stay, with label, unit, sigma and phase samples: the unjudged readings count towards the
stand-outs and the shuffle. Dropped: sigmaKind (nothing downstream of the loader in Consistency reads it); the curves of
impact-area metrics (read at P7 only); and curve samples no reading can reach. Curve values go to DP decimal places,
the coarsest that keeps every spread, z and dv within 1e-6 of the whole file's on 5 Oct (at 5 a z moves 7e-6, and a z
is judged at a hard 3.5). Phase samples and sigmas stay exact, nothing is resampled. After any change, run
consistency_dump on both files and diff their `result`; the two margins below take the same check.

What a reading reaches. Spans P1-P2 .. P6-P7 run from P1 to impact; P7-P8 runs L after impact on the shot's own clock,
L being kConsistencyAfterImpactMs. So the end of every curve is cut at impact + L. The start is cut
at the shot's own P1 only where the warp must keep P1 as an anchor: P1..P7 all there and passing its gates, with some
margin; other shots keep their start. A curve is read through windowed means (series_reduce.h), so each end keeps the
samples a mean at the last read sample takes in: at least MARGIN_US of curve and MIN_VALID valid samples, the most that
window can widen to. On 5 Oct, whose non-landmark curves run at 120 Hz, one valid sample moves a spread by 2.8 and two
are exact; four is the margin. Valid samples at the end also mean the masked-end rule sees the same thing. The warp's
landmark curves stay whole.
"""
import json, csv, os, sys, statistics
SRC, CSV, DEST = sys.argv[1], sys.argv[2], sys.argv[3]
MARGIN_US = int(sys.argv[4]) if len(sys.argv) > 4 else 25000   # > the mean's half-width, ReduceConfig: 20 ms and a sample
MIN_VALID = int(sys.argv[5]) if len(sys.argv) > 5 else 4       # > ReduceConfig::minExtremumSamples, 3
DP = int(sys.argv[6]) if len(sys.argv) > 6 else 6
P = [0, 12, 8, 2, 13, 9, 5]                                    # P1..P7 as Phase ids
LAND = {"handSpeed", "clubheadSpeed", "clubAngularSpeed"}      # WarpOptions::landmarkKeys
MIN_CONF, MAX_RATIO = 0.35, 1.8                                # WarpOptions::minConf; maxSegmentRatio is 2.0
L = 100.0                                                      # kConsistencyAfterImpactMs
d = json.load(open(SRC, encoding="utf-8"))
impact_area = {r["metric"] for r in csv.DictReader(open(CSV, encoding="utf-8"))
               if r["session"] == d["session"] and r["area"] == "impact"}
def rnd(x):
    if x is None: return None
    v = round(x, DP)
    return int(v) if v == int(v) else v
def marks(s):
    ph = {}
    for p in s["phases"]: ph.setdefault(p[0], p)
    return [ph.get(x) for x in P]
def segments(s):
    m = marks(s)
    return [b[1]-a[1] if a and b else None for a, b in zip(m, m[1:])]
med = [statistics.median(x for x in c if x and x > 0) for c in zip(*[segments(s) for s in d["shots"]])]
def anchored(s):
    m = marks(s); seg = segments(s)
    return all(m) and all(p[2] is not None and p[2] >= MIN_CONF for p in m) and \
           all(x and 1/MAX_RATIO <= x/md <= MAX_RATIO for x, md in zip(seg, med))
def edge(t, valid, at, step):
    """The index to keep to, walking by step from at (the sample bracketing the last time read)."""
    i, good = at, 0
    while 0 <= i + step < len(t) and (good < MIN_VALID or abs(t[i] - t[at]) < MARGIN_US):
        i += step
        good += 1 if not valid or valid[i] else 0
    return i
kept = total = front = 0
for s in d["shots"]:
    ok = anchored(s); front += not ok
    lo, hi = marks(s)[0][1] if ok else None, s["impactUs"] + L * 1000.0
    for k, m in s["metrics"].items():
        m.pop("sigmaKind", None)
        if k in impact_area:
            for f in ("t_us", "value", "valid"): m.pop(f, None)
        if "t_us" not in m: continue
        t, valid = m["t_us"], m.get("valid") or []
        i, j = 0, len(t) - 1
        if k not in LAND and s["impactUs"] >= 0:
            if lo is not None:
                at = max((n for n, x in enumerate(t) if x <= lo), default=0)
                i = edge(t, valid, at, -1)
            at = min((n for n, x in enumerate(t) if x >= hi), default=len(t) - 1)
            j = max(i, edge(t, valid, at, +1))
        kept += j + 1 - i; total += len(t)
        m["t_us"] = t[i:j+1]; m["value"] = [rnd(v) for v in m["value"][i:j+1]]
        if "valid" in m:
            m["valid"] = m["valid"][i:j+1]
            if len(m["valid"]) == len(m["t_us"]) and all(m["valid"]): del m["valid"]
with open(DEST, "w", encoding="utf-8") as f:
    json.dump(d, f, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
print(DEST, os.path.getsize(DEST), "bytes; L", L, "ms;", kept, "of", total, "curve samples;", front, "shots keep their start")
