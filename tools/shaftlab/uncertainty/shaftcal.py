"""Shared loaders for the shaft-uncertainty calibration and grading scripts.

A run root holds one dir per swing (<session>__<swing>) written by sweep.py: result.json (or a newer
swing.json — memory: swinglab_run output files), trace.jsonl, and for DTL swings club_dtl.json /
trace_dtl.jsonl. Truth is the corpus truth.json beside each swing (docs/reference/truth_json_schema.md):
shaft marks carry t_us in the SAME window-relative domain as result.json's analysis block.
"""
import json, math, os, glob

CORPUS = "/mnt/swingdata/corpus"
TIERS = ["pred", "ray", "band", "recon", "wedge", "seg"]
# SwingPhase ints (shaft_track_assembly.h): Addr, Backswing, Top, Impact, Downswing, Thru, Finish
PH_ADDR, PH_BS, PH_TOP, PH_IMP, PH_DS, PH_THRU, PH_FIN = range(7)
GROUPS = ["address", "early_bs", "backswing", "top", "downswing", "impact", "through", "finish"]

ShaftMeasured, ShaftImuBridged, ShaftCoasted, ShaftWedge = 0x01, 0x02, 0x04, 0x08
ShaftHeadProjected, ShaftKinematicPredicted, ShaftBallAnchored = 0x10, 0x20, 0x40
ShaftImplausible = 0x200


def wrap(d):
    return (d + 180.0) % 360.0 - 180.0


def result_path(rd):
    """The newer of result.json / swing.json (rename into a reused dir fails silently)."""
    c = [p for p in (os.path.join(rd, "result.json"), os.path.join(rd, "swing.json")) if os.path.exists(p)]
    return max(c, key=os.path.getmtime) if c else None


def load_truth(swing_id):
    s, w = swing_id.split("__")
    p = f"{CORPUS}/swings/{s}/{w}/truth.json"
    return json.load(open(p)) if os.path.exists(p) else None


def load_trace(rd, name="trace.jsonl"):
    p = os.path.join(rd, name)
    if not os.path.exists(p):
        return [], {}
    lines, summ = [], {}
    for ln in open(p, encoding="utf-8"):
        o = json.loads(ln)
        if "summary" in o:
            summ = o
        else:
            lines.append(o)
    return lines, summ


def phase_group(phase, t_us, p2_us):
    if phase == PH_ADDR:
        return "address"
    if phase == PH_BS:
        return "early_bs" if (p2_us is not None and t_us < p2_us) else "backswing"
    return {PH_TOP: "top", PH_DS: "downswing", PH_IMP: "impact", PH_THRU: "through", PH_FIN: "finish"}.get(phase, "finish")


def swing_frames(rd):
    """Per published face-on sample: t (window-relative µs), theta (deg), flags, conf, tier, phase group,
    |θ̇| (deg/frame), and the optional σ fields. Returns (frames, frame_period_us, analysis) or None."""
    rp = result_path(rd)
    if not rp:
        return None
    A = json.load(open(rp, encoding="utf-8")).get("analysis", {})
    club = A.get("club")
    if not club or not club.get("samples"):
        return None
    S = club["samples"]
    tr, summ = load_trace(rd)
    if len(tr) != len(S):
        tr = []          # traced second run disagrees in length: tiers unknown for this swing
    ts = [s["t_us"] for s in S]
    dts = sorted(ts[i] - ts[i - 1] for i in range(1, len(ts)) if ts[i] > ts[i - 1])
    period = dts[len(dts) // 2] if dts else 6635.0
    p2 = next((p["t_us"] for p in club.get("positions", []) if p.get("p") == 2), None)
    out = []
    for i, s in enumerate(S):
        f = s.get("flags", 0)
        tier = tr[i]["tier"] if tr else None
        if tier is not None:
            if f & ShaftImplausible:
                tier = "pred"
            elif (f & ShaftBallAnchored) and not (f & (ShaftMeasured | ShaftWedge)):
                tier = "ball"
        ph = phase_group(tr[i]["phase"], s["t_us"], p2) if tr else None
        thdot = abs(math.degrees(s.get("thetaDot", 0.0))) * period * 1e-6
        out.append({"t": s["t_us"], "theta": math.degrees(s["theta"]) % 360.0, "flags": f,
                    "conf": s.get("conf", 0.0), "tier": tier, "group": ph, "thdot": thdot,
                    "sig": s.get("sigTheta"), "pGross": s.get("pGross"),
                    "fbSig": tr[i].get("fb_sigma") if tr else None,
                    "fbAlt": tr[i].get("fb_palt") if tr else None})
    return out, period, A


def residuals(rd, swing_id, maxdt_frac=0.6):
    """Hand mark ↔ nearest published sample (within maxdt_frac of a frame period)."""
    T = load_truth(swing_id)
    if not T or not T.get("shaft"):
        return []
    r = swing_frames(rd)
    if not r:
        return []
    fr, period, _ = r
    ts = [x["t"] for x in fr]
    res = []
    import bisect
    for m in T["shaft"]:
        t = m["t_us"]
        k = bisect.bisect_left(ts, t)
        cands = [j for j in (k - 1, k) if 0 <= j < len(ts)]
        if not cands:
            continue
        j = min(cands, key=lambda j: abs(ts[j] - t))
        if abs(ts[j] - t) > maxdt_frac * period:
            continue
        x = dict(fr[j])
        x["swing"] = swing_id
        x["r"] = wrap(x["theta"] - math.degrees(m["theta"]))
        res.append(x)
    return res


def swing_ids(root):
    return sorted(os.path.basename(d.rstrip("/")) for d in glob.glob(os.path.join(root, "*__swing_*/")))
