#!/usr/bin/env python3
"""Consistency: extract v2, a read-only copy of an athlete's sessions, one JSON per session, in exactly the shape
charts_test::loadFixture reads (src/Gui/tests/charts_maths_fixture.h). This is the first of three steps: extract here,
consistency_dump over the files, consistency_tally.py over the dumps (docs/design/consistency_design.md, Appendix A).
The tally never reads these files. Run this on the cabin PC, where the share is local:

    python tools\\diagnostics\\consistency_extract.py C:\\PinPointStudio\\Mark-Liversedge C:\\PinPointStudio\\scratch\\consistency\\fixtures\\Mark-Liversedge

Arguments: <athlete dir> <out dir> [one session folder].

Each shot keeps its swing folder name and every metric of the swing, lm.* included: label, unit, sigma, phase samples
and the whole curve. Shots are read as SessionSeriesModel::extractShot reads them: the same club, order and ordinals
(a swing with no analysis metrics is not a shot and takes no ordinal), the same window-relative times, the same impact.
Values are NEVER rounded: a reading at z 3.5003 stands out only by its last digits. The files are kept small by what
the loader reads as missing anyway: a null sigma, sigmaKind or phase sigma is left out, and an all-ones valid. A missing
value stays null. conditions, readAt and fired stay empty, and kinematicSequence is left out: the loader tolerates all
four and the consistency layers read none of them. consistency_trim_fixture.py cuts a committed fixture from a file.
"""
import sys, os, json, glob, math, time, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import load_swing, has_swing
ROOT = sys.argv[1]
OUT  = sys.argv[2]
ONLY = sys.argv[3] if len(sys.argv) > 3 else ""    # one session folder name, for a quick check
os.makedirs(OUT, exist_ok=True)
IMPACT = 5                        # Phase::Impact
def isnum(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)
def num(x):                       # a number, or null: a missing reading never becomes 0
    if not isnum(x): return None
    return int(x) if x == int(x) else x
def whole(x):                     # QJsonValue::toInt(-1)
    return int(x) if isnum(x) and x == int(x) else -1
def rel_us(v, t0):                # SessionSeriesModel::relUs: absolute times come back to the window, relative ones pass
    t = int(v) if isnum(v) else 0
    return t - t0 if t >= t0 else t
def text(x):
    return x.strip() if isinstance(x, str) else ""
def shot_id(wd):                  # SessionSeriesModel::folderShotId
    tail = os.path.basename(wd).rsplit("_", 1)[-1]
    return int(tail) if tail.isdigit() else -1
def strike_order(wd):             # buildSnapshot: the folder number, then the folder name for anything unnumbered
    i = shot_id(wd)
    return (0, i, wd) if i >= 0 else (1, 0, wd)
def sample(p, t0):                # a missing sigma is a short row: the loader reads it as NaN
    row = [whole(p.get("phase")), rel_us(p.get("t_us"), t0), num(p.get("value"))]
    return row + [num(p["sigma"])] if isnum(p.get("sigma")) else row
def metric(m, t0):
    ts, vs = m.get("t_us") or [], m.get("value") or []
    n = min(len(ts), len(vs))
    sk = m.get("sigmaKind")
    e = {"label": m.get("label") or "", "unit": m.get("unit") or "", "at": [sample(p, t0) for p in m.get("phaseSamples") or []]}
    if isnum(m.get("sigma")): e["sigma"] = num(m["sigma"])
    if isnum(sk): e["sigmaKind"] = whole(sk)
    if n:
        e["t_us"] = [rel_us(t, t0) for t in ts[:n]]
        e["value"] = [num(v) for v in vs[:n]]
    vd = [1 if isnum(v) and v != 0 else 0 for v in (m.get("valid") or [])[:n]]
    if vd and (len(vd) < n or not all(vd)):    # all ones is the same as absent
        e["valid"] = vd
    return e
def shot(d, swing, ordinal):
    a = d.get("analysis")
    ms = a.get("metrics") if isinstance(a, dict) else None
    if not ms: return None
    review = d.get("review") or {}; cap = d.get("capture") or {}
    t0 = int((d.get("clock") or {}).get("t0_us") or 0)
    metrics, order = {}, []
    for m in ms:
        k = m.get("key")
        if not k or k in metrics: continue
        order.append(k); metrics[k] = metric(m, t0)
    impact, phases = -1, []
    for p in a.get("phases") or []:
        ph, t = whole(p.get("phase")), rel_us(p.get("t_us"), t0)
        if ph == IMPACT and impact < 0: impact = t
        phases.append([ph, t, num(p.get("conf"))])
    ks = a.get("kinematicSequence") or {}
    if impact < 0 and "impactUs" in ks: impact = rel_us(ks["impactUs"], t0)
    return {"ordinal": ordinal, "swing": swing, "club": text(review.get("club")) or text((cap.get("club") or {}).get("name")),
            "impactUs": impact, "phases": phases, "metricOrder": order, "metrics": metrics, "fired": []}
t00 = time.time()
for sd in sorted(glob.glob(os.path.join(ROOT, "2026-*"))):
    name = os.path.basename(sd); shots = []; skipped = 0
    if not os.path.isdir(sd) or (ONLY and name != ONLY): continue
    for wd in sorted(glob.glob(os.path.join(sd, "swing_*")), key=strike_order):
        if not os.path.isdir(wd) or not has_swing(wd): continue
        try: s = shot(load_swing(wd), os.path.basename(wd), len(shots) + 1)
        except Exception as e:
            print("skip", wd, e, flush=True); s = None
        if s: shots.append(s)
        else: skipped += 1
    clubs = collections.Counter(s["club"] for s in shots)
    out = {"session": name, "date": name[:10], "club": clubs.most_common(1)[0][0] if shots else "",
           "pickedOrdinal": 1 if shots else 0, "shotCount": len(shots), "conditions": {}, "readAt": {}, "shots": shots}
    with open(os.path.join(OUT, name + ".json"), "w", encoding="utf-8") as f:
        json.dump(out, f, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    print(name, len(shots), "skipped", skipped, "blank-club", clubs[""], "no-phases",
          sum(not s["phases"] for s in shots), round(time.time()-t00, 1), flush=True)
print("DONE", round(time.time()-t00, 1), flush=True)
