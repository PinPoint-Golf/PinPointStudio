#!/usr/bin/env python3
"""How well-conditioned is attackAngle, and can a different estimator do better?

swing_storage_impl.md, Phase 2 stage 5. The raw-vs-mp4 study found attackAngle moving a median 11 deg
between two encodings of the SAME frames with the pose pinned: a few pixels of head track turn into
double-digit degrees. This measures why, on the swings where a launch monitor gives the truth.

For every swing carrying both our `attackAngle` and the GC Quad's `lm.attackAngle`:

  prod    the production estimator, re-derived exactly as club_delivery.cpp does it: the measured
          head subset (headConf >= 0.30, not projected), a +/-2-sample centred difference,
          atan2(-dy, |dx|), linearly interpolated at impact. Checked against the stored value.
  synth   the same centred difference on the dense synthesised arc (club.synth) around impact.
  lsq     a least-squares straight line through the measured heads within +/-W ms of impact
          (x(t), y(t) fitted separately): the velocity direction over a window, not two samples.
  circle  a circle through the measured heads within +/-A ms of impact; the tangent at the
          head's impact position, taken in the direction of travel.

and reports, per estimator: the error against the launch monitor, and the SENSITIVITY — the spread
of the estimate when the measured heads are jittered by sigma px (Monte Carlo) and when impact moves
one frame either way. Analytic conditioning too: a centred difference over +/-k samples of a head
moving s px per sample turns sigma px of noise into about sqrt(2)*sigma/(2*k*s) radians.

    club_arc_sensitivity.py ROOT [ROOT ...] --out DIR

Writes DIR/club_arc_attack.csv (per swing) and prints the summary. Pure stdlib + pp_swingdoc.

STABILITY mode: the same estimators on two swinglab run trees of the same swings (the raw-vs-mp4
rig's pinned arms: pose held fixed, only the frames' encoding differs), reporting how far each
estimate moves between them:

    club_arc_sensitivity.py --runs build/rawcrf --pairs PR-PM1,PR-PM23,PR-PM18,PR-PM12 --out DIR
"""
import argparse
import csv
import math
import os
import random
import statistics
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import find_swing_dirs, load_swing  # noqa: E402

IMPACT = 5                 # the Phase code analysis.phases uses for impact (P7)
HEAD_CONF_MIN = 0.30       # tuned::clubDelivery::kHeadConfMin
VEL_HALF_SPAN = 2          # tuned::clubDelivery::kVelHalfSpan
PROJECTED = 0x10           # ShaftHeadProjected
LSQ_WIN_US = 15000
CIRCLE_WIN_US = 60000
FRAME_US = 6667            # 150 fps

ap = argparse.ArgumentParser()
ap.add_argument("roots", nargs="*")
ap.add_argument("--runs", help="stability mode: a raw-vs-mp4 rig root holding out_<arm>/ trees")
ap.add_argument("--pairs", default="PR-PM1")
ap.add_argument("--out", required=True)
ap.add_argument("--sigma", type=float, nargs="*", default=[1.0, 2.0, 5.0])
ap.add_argument("--reps", type=int, default=200)
a = ap.parse_args()


def at_phase(metric, phase):
    for s in (metric or {}).get("phaseSamples", []) or []:
        if s.get("phase") == phase and isinstance(s.get("value"), (int, float)):
            return float(s["value"]), s.get("t_us")
    return None, None


def angle(dx, dy):
    """The production convention: positive = rising (image y grows down), mirror-free."""
    return math.degrees(math.atan2(-dy, abs(dx)))


def interp(ts, vs, t):
    if not ts:
        return None
    if t <= ts[0]:
        return vs[0]
    if t >= ts[-1]:
        return vs[-1]
    for i in range(1, len(ts)):
        if ts[i] >= t:
            f = (t - ts[i - 1]) / (ts[i] - ts[i - 1]) if ts[i] != ts[i - 1] else 0.0
            return vs[i - 1] + f * (vs[i] - vs[i - 1])
    return vs[-1]


def centred(heads, k, t_imp):
    ts, vs = [], []
    for i in range(k, len(heads) - k):
        (ta, xa, ya), (tb, xb, yb) = heads[i - k], heads[i + k]
        dx, dy = xb - xa, yb - ya
        if abs(dx) <= 1e-9 and abs(dy) <= 1e-9:
            continue
        ts.append(heads[i][0])
        vs.append(angle(dx, dy))
    return interp(ts, vs, t_imp)


def lsq(heads, t_imp, win):
    pts = [(t, x, y) for t, x, y in heads if abs(t - t_imp) <= win]
    if len(pts) < 3:
        return None
    tm = statistics.fmean(p[0] for p in pts)
    sxx = sum((p[0] - tm) ** 2 for p in pts)
    if sxx <= 0:
        return None
    vx = sum((p[0] - tm) * p[1] for p in pts) / sxx
    vy = sum((p[0] - tm) * p[2] for p in pts) / sxx
    return angle(vx, vy)


def circle(heads, t_imp, win):
    pts = [(t, x, y) for t, x, y in heads if abs(t - t_imp) <= win]
    if len(pts) < 5:
        return None
    # Algebraic (Kasa) fit: x^2 + y^2 + D x + E y + F = 0, by normal equations.
    m = [[0.0] * 4 for _ in range(3)]
    for _, x, y in pts:
        row = (x, y, 1.0)
        r = -(x * x + y * y)
        for i in range(3):
            for j in range(3):
                m[i][j] += row[i] * row[j]
            m[i][3] += row[i] * r
    for c in range(3):                                   # Gauss-Jordan
        p = max(range(c, 3), key=lambda i: abs(m[i][c]))
        if abs(m[p][c]) < 1e-12:
            return None
        m[c], m[p] = m[p], m[c]
        for i in range(3):
            if i != c:
                f = m[i][c] / m[c][c]
                for j in range(c, 4):
                    m[i][j] -= f * m[c][j]
    D, E = m[0][3] / m[0][0], m[1][3] / m[1][1]
    cx, cy = -D / 2, -E / 2
    # The head at impact: the nearest measured sample; the tangent there, pointed the way it travels.
    t0, x0, y0 = min(pts, key=lambda p: abs(p[0] - t_imp))
    rx, ry = x0 - cx, y0 - cy
    tx, ty = -ry, rx
    after = [p for p in pts if p[0] > t0]
    before = [p for p in pts if p[0] < t0]
    ref = (after[0][1] - x0, after[0][2] - y0) if after else (x0 - before[-1][1], y0 - before[-1][2])
    if tx * ref[0] + ty * ref[1] < 0:
        tx, ty = -tx, -ty
    return angle(tx, ty)


def heads_of(samples, w, h):
    out = []
    for s in samples:
        if s.get("flags", 0) & PROJECTED or s.get("headConf", -1) < HEAD_CONF_MIN:
            continue
        hd = s.get("head")
        if hd:
            out.append((s["t_us"], hd[0] * w, hd[1] * h))
    out.sort()
    return out


def synth_heads(synth, w, h):
    return sorted((s["t_us"], s["head"][0] * w, s["head"][1] * h) for s in synth if s.get("head"))


def estimates(an):
    """prod and synth attackAngle of one analysis block at its own impact."""
    club = an.get("club", {})
    t_imp = next((p["t_us"] for p in an.get("phases", []) if p.get("phase") == IMPACT), None)
    if t_imp is None or not club.get("samples"):
        return None, None
    w, h = club.get("frameWidth", 1280), club.get("frameHeight", 1024)
    syn = synth_heads(club.get("synth", []), w, h)
    near = sum(1 for p in syn if abs(p[0] - t_imp) <= 20000)
    heads = heads_of(club["samples"], w, h)
    prod = centred(heads, VEL_HALF_SPAN, t_imp)
    hybrid = centred(syn, VEL_HALF_SPAN, t_imp) if near >= 5 else prod
    return prod, hybrid


if a.runs:
    import json
    out = []
    for pair in a.pairs.split(","):
        x, y = pair.split("-", 1)
        dp, dh = [], []
        base = os.path.join(a.runs, "out_" + x)
        for sess in sorted(os.listdir(base)):
            for sw in sorted(os.listdir(os.path.join(base, sess))):
                fa = os.path.join(a.runs, "out_" + x, sess, sw, "result.json")
                fb = os.path.join(a.runs, "out_" + y, sess, sw, "result.json")
                if not (os.path.exists(fa) and os.path.exists(fb)):
                    continue
                pa, ha = estimates(json.load(open(fa))["analysis"])
                pb, hb = estimates(json.load(open(fb))["analysis"])
                if pa is not None and pb is not None:
                    dp.append(abs(pa - pb))
                if ha is not None and hb is not None:
                    dh.append(abs(ha - hb))
                out.append({"pair": pair, "swing": f"{sess}/{sw}", "prod_a": pa, "prod_b": pb,
                            "hybrid_a": ha, "hybrid_b": hb})
        q = lambda v, f: sorted(v)[int(f * (len(v) - 1))] if v else float("nan")
        print(f"{pair:10s} prod   n={len(dp):2d} median |d| {statistics.median(dp) if dp else float('nan'):6.2f}  p90 {q(dp, .9):6.2f}"
              f"   hybrid n={len(dh):2d} median |d| {statistics.median(dh) if dh else float('nan'):6.2f}  p90 {q(dh, .9):6.2f}")
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "club_arc_stability.csv"), "w", newline="") as f:
        wtr = csv.DictWriter(f, fieldnames=list(out[0].keys()) if out else ["pair"])
        wtr.writeheader()
        for r in out:
            wtr.writerow({k: (round(v, 4) if isinstance(v, float) else v) for k, v in r.items()})
    sys.exit(0)

rows = []
for root in a.roots:
    for d in find_swing_dirs(root):
        doc = load_swing(d)
        an = doc.get("analysis", {})
        mets = {m.get("key"): m for m in an.get("metrics", [])}
        truth, _ = at_phase(mets.get("lm.attackAngle"), IMPACT)
        stored, t_imp = at_phase(mets.get("attackAngle"), IMPACT)
        club = an.get("club", {})
        if truth is None or stored is None or not club.get("samples"):
            continue
        w, h = club.get("frameWidth", 1280), club.get("frameHeight", 1024)
        heads = heads_of(club["samples"], w, h)
        syn = synth_heads(club.get("synth", []), w, h)
        est = {
            "prod":   centred(heads, VEL_HALF_SPAN, t_imp),
            "synth":  centred(syn, VEL_HALF_SPAN, t_imp) if syn else None,
            "lsq":    lsq(heads, t_imp, LSQ_WIN_US),
            "circle": circle(heads, t_imp, CIRCLE_WIN_US),
        }
        # Head speed near impact, px per measured sample — the conditioning's denominator.
        near = [p for p in heads if abs(p[0] - t_imp) <= 3 * FRAME_US]
        step = statistics.fmean(math.hypot(near[i][1] - near[i - 1][1], near[i][2] - near[i - 1][2])
                                for i in range(1, len(near))) if len(near) > 1 else float("nan")
        row = {"swing": os.path.relpath(d, root), "truth": truth, "stored": stored,
               "prod_reproduces": abs((est["prod"] or 1e9) - stored) < 0.01,
               "head_px_per_sample": step, "measured_heads": len(heads),
               "measured_within_2_frames": sum(1 for p in heads if abs(p[0] - t_imp) <= 2 * FRAME_US)}
        rng = random.Random(7)
        for name, fn in (("prod", lambda hs, t: centred(hs, VEL_HALF_SPAN, t)),
                         ("lsq", lambda hs, t: lsq(hs, t, LSQ_WIN_US)),
                         ("circle", lambda hs, t: circle(hs, t, CIRCLE_WIN_US))):
            row[name] = est[name]
            row[name + "_err"] = None if est[name] is None else est[name] - truth
            for sg in a.sigma:
                vals = []
                for _ in range(a.reps):
                    jit = [(t, x + rng.gauss(0, sg), y + rng.gauss(0, sg)) for t, x, y in heads]
                    v = fn(jit, t_imp)
                    if v is not None:
                        vals.append(v)
                row[f"{name}_sd_at_{sg:g}px"] = statistics.pstdev(vals) if len(vals) > 1 else None
            shifted = [fn(heads, t_imp + s) for s in (-FRAME_US, FRAME_US)]
            row[name + "_impact_pm1frame"] = (max(abs(v - est[name]) for v in shifted if v is not None)
                                              if est[name] is not None and any(v is not None for v in shifted)
                                              else None)
        row["synth"] = est["synth"]
        row["synth_err"] = None if est["synth"] is None else est["synth"] - truth
        rows.append(row)

os.makedirs(a.out, exist_ok=True)
if rows:
    keys = list(dict.fromkeys(k for r in rows for k in r))
    with open(os.path.join(a.out, "club_arc_attack.csv"), "w", newline="") as f:
        wtr = csv.DictWriter(f, fieldnames=keys)
        wtr.writeheader()
        for r in rows:
            wtr.writerow({k: (round(v, 4) if isinstance(v, float) else v) for k, v in r.items()})


def med(xs):
    xs = [x for x in xs if x is not None and not (isinstance(x, float) and math.isnan(x))]
    return statistics.median(xs) if xs else None


print(f"{len(rows)} launch-monitor-paired swings; production estimator reproduced on "
      f"{sum(r['prod_reproduces'] for r in rows)}")
print(f"truth (lm.attackAngle) median {med([r['truth'] for r in rows]):.2f} deg; "
      f"head moves a median {med([r['head_px_per_sample'] for r in rows]):.1f} px per measured sample near impact; "
      f"median measured heads within +/-2 frames of impact: {med([r['measured_within_2_frames'] for r in rows])}")
for name in ("prod", "synth", "lsq", "circle"):
    errs = [abs(r[name + "_err"]) for r in rows if r.get(name + "_err") is not None]
    bias = med([r[name + "_err"] for r in rows if r.get(name + "_err") is not None])
    line = f"{name:7s} n={len(errs):2d}  median |err| {med(errs) if errs else float('nan'):6.2f} deg  (median signed {bias if bias is not None else float('nan'):+.2f})"
    if name != "synth":
        for sg in a.sigma:
            line += f"  sd@{sg:g}px {med([r.get(f'{name}_sd_at_{sg:g}px') for r in rows]) or float('nan'):5.2f}"
        line += f"  impact+/-1f {med([r.get(name + '_impact_pm1frame') for r in rows]) or float('nan'):5.2f}"
    print(line)
