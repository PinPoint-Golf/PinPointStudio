#!/usr/bin/env python3
"""rotation_triangulated_check.py — the checks behind the two-camera body-rotation route.

docs/design/body_rotation_estimation.md §Two-camera route. There is NO IMU truth for trunk turn in
the corpus (every swing is bindings 0), so the route cannot be calibrated here; it is checked for
what can be checked without truth:

  coverage     how many two-camera swings produce each series
  plausibility the readings at Top / Impact / Finish against the catalogue's own guides
  consistency  shot-to-shot spread of each reading within a session (one golfer, one club)
  agreement    d/dt of pelvisRotationSigned against pelvisAngularSpeed — the kinematic sequence's
               INDEPENDENT pelvis rate (face-on + down-the-line signed separations, a different
               construction off the same two poses) — Spearman over the downswing, per swing

  rotation_triangulated_check.py --run /mnt/swingdata/scratch/rot3d/lib [--out report.md]
"""
import argparse, json, math, os, statistics as st, collections

PH = {"address": 0, "top": 2, "impact": 5, "finish": 7}
KEYS = ["pelvisRotation", "pelvisRotationSigned", "thoraxRotation", "xFactor", "xFactorStretch"]


def spearman(a, b):
    def rank(v):
        o = sorted(range(len(v)), key=lambda i: v[i]); r = [0.0] * len(v)
        for k, i in enumerate(o): r[i] = k
        return r
    if len(a) < 5: return float("nan")
    ra, rb = rank(a), rank(b); ma, mb = st.mean(ra), st.mean(rb)
    num = sum((x - ma) * (y - mb) for x, y in zip(ra, rb))
    den = math.sqrt(sum((x - ma) ** 2 for x in ra) * sum((y - mb) ** 2 for y in rb))
    return num / den if den else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    swings = sorted(d for d in os.listdir(a.run) if "__swing_" in d and os.path.isdir(os.path.join(a.run, d)))
    cover = collections.Counter(); vals = collections.defaultdict(list); sess = collections.defaultdict(lambda: collections.defaultdict(list))
    rhos = []; n2cam = 0
    for sw in swings:
        rp = os.path.join(a.run, sw, "result.json")
        if not os.path.exists(rp): continue
        A = json.load(open(rp, encoding="utf-8"))["analysis"]
        sk = A.get("skeleton3d", {})
        if sk.get("valid") and sk.get("dtl"): n2cam += 1
        M = {m["key"]: m for m in A.get("metrics", [])}
        s = sw.split("__")[0]
        for k in KEYS:
            if k not in M: continue
            cover[k] += 1
            for ps in M[k].get("phaseSamples", []):
                for name, pid in PH.items():
                    if ps["phase"] == pid:
                        vals[(k, name)].append(ps["value"]); sess[s][(k, name)].append(ps["value"])
        # agreement: numerical derivative of the signed pelvis vs pelvisAngularSpeed, top → impact
        if "pelvisRotationSigned" in M and "pelvisAngularSpeed" in M:
            ph = {p["phase"]: p["t_us"] for p in A.get("phases", [])}
            if 2 in ph and 5 in ph:
                ps_, pa = M["pelvisRotationSigned"], M["pelvisAngularSpeed"]
                t, v = ps_["t_us"], ps_["value"]; va = ps_.get("valid") or [1] * len(t)
                ta, sa = pa["t_us"], pa["value"]; vaa = pa.get("valid") or [1] * len(ta)
                d1, d2 = [], []
                for i in range(1, len(t) - 1):
                    if not (ph[2] <= t[i] <= ph[5]) or not (va[i - 1] and va[i + 1]): continue
                    rate = (v[i + 1] - v[i - 1]) / ((t[i + 1] - t[i - 1]) * 1e-6)
                    j = min(range(len(ta)), key=lambda k: abs(ta[k] - t[i]))
                    if abs(ta[j] - t[i]) > 10000 or not vaa[j]: continue
                    d1.append(rate); d2.append(sa[j])
                r = spearman(d1, d2)
                if r == r: rhos.append((sw, r, len(d1)))
    L = [f"# Two-camera body rotation — checks", "", f"{len(swings)} swings swept; {n2cam} with a two-camera skeleton fit.", "",
         "## Coverage", ""] + [f"- {k}: {cover[k]} swings" for k in KEYS] + ["", "## Readings (median, p10–p90, n)", ""]
    for k in KEYS:
        for name in PH:
            v = sorted(vals.get((k, name), []))
            if v:
                q = lambda f: v[min(len(v) - 1, int(f * (len(v) - 1)))]
                L.append(f"- {k} at {name}: {st.median(v):+.1f}° ({q(.1):+.1f}…{q(.9):+.1f}), n={len(v)}")
    L += ["", "## Within-session spread (SD of the reading across a session's shots)", ""]
    for s in sorted(sess):
        parts = []
        for k, name in (("pelvisRotation", "top"), ("pelvisRotationSigned", "impact"), ("thoraxRotation", "top"), ("xFactor", "top")):
            v = sess[s].get((k, name), [])
            if len(v) >= 3: parts.append(f"{k}@{name} {st.mean(v):+.0f}±{st.stdev(v):.0f}°")
        if parts: L.append(f"- {s}: " + "; ".join(parts))
    L += ["", "## Agreement with the kinematic sequence's pelvis rate (Spearman, top → impact)", ""]
    if rhos:
        rr = sorted(r for _, r, _ in rhos)
        L.append(f"{len(rhos)} swings: median ρ {st.median(rr):+.2f}, p10 {rr[int(.1*(len(rr)-1))]:+.2f}, p90 {rr[int(.9*(len(rr)-1))]:+.2f}")
    out = "\n".join(L)
    print(out)
    if a.out:
        open(a.out, "w").write(out + "\n")


if __name__ == "__main__":
    main()
