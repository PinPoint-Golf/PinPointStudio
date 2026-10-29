#!/opt/homebrew/bin/python3
"""Club delivery chart, the evidence (club_delivery_chart_design.md §2, Appendix A).

Reads every swing under a library root (one folder per golfer) and writes one row per swing: the swing
plane curve and the hand plane at P1..P7 (from swing_phasegrid.json, the app's own sampler), the
transition readings, the impact readings, the launch monitor's numbers, and the shoulder line at
address (from the swing document: the down-the-line ball and the trail shoulder). Then:

    club_delivery_extract.py extract <root> <out.csv>     one row per swing
    club_delivery_extract.py fits <csv>                   §2.2: plane -> launch monitor, per golfer
    club_delivery_extract.py medians <csv>                §2.1: the curve per golfer and club

The committed CSV was extracted from the library on 29 Oct 2026, after the ball-finder and hand-plane
sweep (4827699c, a412aed3). Needs numpy and tools/pp_swingdoc.py.
"""
import csv, glob, json, math, os, statistics, sys, collections

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import load_swing   # noqa: E402

GOLFERS = ("Mark-Liversedge", "Nick-Henn", "Jess-Ratcliffe", "Chris-Marriott", "Paddy-Marriott")
POSITIONS = [("P1", 0), ("P2", 12), ("P3", 8), ("P4", 2), ("P5", 13), ("P6", 9), ("P7", 5)]
CURVES = ["swingPlaneCurve", "handPlaneCurve"]
SCALARS = ["handPlaneSteepening", "handPathLoop", "transitionPlaneDelta"]
AT_IMPACT = ["impactShaftLean", "lagAngle", "shaftLie"]
LM = ["lm.clubPath", "lm.faceAngle", "lm.faceToPath", "lm.attackAngle", "lm.dynamicLoft", "lm.lieAngle",
      "lm.launchDirection", "lm.spinAxis", "lm.clubheadSpeed", "lm.offline"]


def _view_plane_incl(theta, yaw=0.0, pitch=0.0, roll=0.0):
    """The plane through the camera's view ray and the image line at theta, to the ground (shaft_plane_curve.h)."""
    p, a, r = (math.radians(x) for x in (pitch, yaw, roll))
    d0, r0, dn0 = (1.0, 0.0, 0.0), (0.0, -1.0, 0.0), (0.0, 0.0, -1.0)
    add = lambda u, v, s=1.0: tuple(x + s * y for x, y in zip(u, v))
    mul = lambda u, s: tuple(x * s for x in u)
    d1 = add(mul(d0, math.cos(p)), mul(dn0, math.sin(p)))
    dn1 = add(mul(dn0, math.cos(p)), mul(d0, -math.sin(p)))
    rz = lambda v: (math.cos(a) * v[0] - math.sin(a) * v[1], math.sin(a) * v[0] + math.cos(a) * v[1], v[2])
    d, right, down = rz(d1), rz(r0), rz(dn1)
    right, down = add(mul(right, math.cos(r)), mul(down, math.sin(r))), add(mul(down, math.cos(r)), mul(right, -math.sin(r)))
    img = add(mul(right, math.cos(theta)), mul(down, math.sin(theta)))
    n = (d[1] * img[2] - d[2] * img[1], d[2] * img[0] - d[0] * img[2], d[0] * img[1] - d[1] * img[0])
    nn = math.sqrt(sum(x * x for x in n))
    return math.degrees(math.acos(min(1.0, abs(n[2]) / nn))) if nn > 0 else float("nan")


def _shoulder_line(a):
    """The plane through the ball line and the TRAIL shoulder at address, read as the hand plane is."""
    cd = a.get("clubDtl") or {}
    ball = (cd.get("summary") or {}).get("ball") or {}
    pose = a.get("poseDtl") or {}
    ev = {q.get("phase"): q["t_us"] for q in a.get("phases") or [] if isinstance(q, dict) and "t_us" in q}
    if not ball.get("found") or 0 not in ev or not pose.get("frames"):
        return float("nan")
    W, H = cd.get("frameWidth"), cd.get("frameHeight")
    hold = [f for f in pose["frames"] if f["t_us"] < ev[0] - 100000]
    if not hold or not W or not H:
        return float("nan")
    kp = [f["kp"] for f in hold[-15:]]
    # COCO body: 5 = left shoulder, 6 = right shoulder. Down the line, behind a right-handed golfer, the
    # trail (right) shoulder is the one nearer the camera's side of the body; the pose names it directly.
    xs = statistics.median(k[6 * 3] for k in kp) * W
    ys = statistics.median(k[6 * 3 + 1] for k in kp) * H
    cam = (a.get("club3d") or {}).get("camera") or {}
    th = math.atan2(ys - ball["y"], xs - ball["x"])
    return _view_plane_incl(th, cam.get("yawDeg", 0.0), cam.get("pitchDeg", 0.0), cam.get("rollDeg", 0.0))


def extract(root, out):
    cols = ["golfer", "session", "swing", "club"]
    cols += [f"{c}_{p}" for c in CURVES for p, _ in POSITIONS]
    cols += SCALARS + [f"{k}_P7" for k in AT_IMPACT] + ["shaftLie_P1", "lagAngle_P6"] + LM + ["shoulderLine_P1", "ballSource"]
    n = 0
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(cols)
        for g in GOLFERS:
            for f in sorted(glob.glob(f"{root}/{g}/*/swing_*/swing_phasegrid.json")):
                sd = os.path.dirname(f)
                with open(f) as jf:
                    pg = json.load(jf)
                M = {m["key"]: {x["phase"]: x["value"] for x in m["values"] if x.get("value") is not None}
                     for m in pg.get("metrics", [])}
                try:
                    a = load_swing(sd)["analysis"]
                except Exception:
                    continue
                L = {}
                for m in a.get("metrics", []):
                    if m["key"] in LM and m.get("phaseSamples"):
                        L[m["key"]] = m["phaseSamples"][0]["value"]
                row = {"golfer": g, "session": os.path.basename(os.path.dirname(sd)), "swing": os.path.basename(sd),
                       "club": pg.get("club", ""),
                       "ballSource": ((a.get("clubDtl") or {}).get("summary") or {}).get("ball", {}).get("source", "")}
                for c in CURVES:
                    for p, ph in POSITIONS:
                        row[f"{c}_{p}"] = M.get(c, {}).get(ph, "")
                for k in SCALARS:
                    v = M.get(k, {})
                    row[k] = next(iter(v.values())) if v else ""
                for k in AT_IMPACT:
                    row[f"{k}_P7"] = M.get(k, {}).get(5, "")
                row["shaftLie_P1"] = M.get("shaftLie", {}).get(0, "")
                row["lagAngle_P6"] = M.get("lagAngle", {}).get(9, "")
                for k in LM:
                    row[k] = L.get(k, "")
                sl = _shoulder_line(a)
                row["shoulderLine_P1"] = "" if math.isnan(sl) else round(sl, 2)
                w.writerow([row.get(c, "") for c in cols])
                n += 1
    print(f"{n} swings -> {out}")


def _rows(path):
    with open(path) as fh:
        return list(csv.DictReader(fh))


def _f(r, k):
    try:
        return float(r[k])
    except (KeyError, ValueError):
        return float("nan")


def _fit(xs, ys):
    import numpy as np
    x, y = np.array(xs), np.array(ys)
    b, a0 = np.polyfit(x, y, 1)
    r = float(np.corrcoef(x, y)[0, 1])
    sd = float(np.std(y - (a0 + b * x), ddof=2)) if len(x) > 2 else float("nan")
    return b, a0, r, sd


def fits(path):
    rows = _rows(path)
    pairs = [("swingPlaneCurve_P6", "lm.clubPath"), ("handPlaneSteepening", "lm.clubPath"),
             ("swingPlaneCurve_P6", "lm.attackAngle"), ("handPlaneSteepening", "lm.attackAngle"),
             ("handPathLoop", "lm.clubPath")]
    print("x | y | golfer | n | slope (y per unit x) | r | residual sd")
    for xk, yk in pairs:
        for g in ("all",) + GOLFERS:
            sel = [r for r in rows if g == "all" or r["golfer"] == g]
            xy = [(_f(r, xk), _f(r, yk)) for r in sel]
            xy = [(x, y) for x, y in xy if not (math.isnan(x) or math.isnan(y))]
            if len(xy) < 8:
                continue
            b, a0, r, sd = _fit(*zip(*xy))
            print(f"{xk} | {yk} | {g} | {len(xy)} | {b:+.2f} | {r:+.2f} | {sd:.2f}")


def medians(path):
    rows = _rows(path)
    by = collections.defaultdict(list)
    for r in rows:
        by[(r["golfer"], r["club"])].append(r)
    for (g, club), rs in sorted(by.items()):
        line = []
        for c in CURVES:
            vals = []
            for p, _ in POSITIONS:
                v = [_f(r, f"{c}_{p}") for r in rs]
                v = [x for x in v if not math.isnan(x)]
                vals.append(f"{statistics.median(v):.1f}" if v else "-")
            line.append(f"{c}: " + " ".join(vals))
        st = [x for x in (_f(r, "handPlaneSteepening") for r in rs) if not math.isnan(x)]
        sh = [x for x in (_f(r, "shoulderLine_P1") for r in rs) if not math.isnan(x)]
        tail = (f" | steepening {statistics.median(st):+.1f}" if st else "") + \
               (f" | shoulder line at address {statistics.median(sh):.1f}" if sh else "")
        print(f"{g} | {club} | n={len(rs)} | " + " | ".join(line) + tail)


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "extract":
        extract(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 3 and sys.argv[1] == "fits":
        fits(sys.argv[2])
    elif len(sys.argv) >= 3 and sys.argv[1] == "medians":
        medians(sys.argv[2])
    else:
        print(__doc__)
