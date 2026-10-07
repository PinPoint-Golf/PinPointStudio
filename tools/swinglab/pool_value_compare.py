#!/usr/bin/env python3
"""pool_value_compare.py — what the skeleton3d session pool changes (swing_3d_viz_design.md §13.7).

    python3 tools/swinglab/pool_value_compare.py <root> [--csv out.csv]

<root> holds U/<session>/swing_* (re-analysed with no session pool: what a live shot and pass 1
produce) and P/<session>/swing_* (the app's session-end path: poolSkeletonSession, then every swing
re-analysed holding the pool). Pairs swings by session/swing and reports, per session:

  fit        skeleton3d's own diagnostics, U → P: reprojection median px (face-on / DTL), final
             cost, fitted camera focal/DTL yaw, shaft-plane rms
  metrics    every phase sample of every metric that differs, |P − U| against the metric's own σ:
             how many move at all, how many by more than 0.5 σ and 1 σ, and the worst
  ks         kinematic-sequence nodes whose route, placed flag or peak changed

A change smaller than the metric's σ is one the user cannot see as a difference.
"""
import argparse
import csv
import math
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import load_swing, has_swing  # noqa: E402


def med(xs):
    xs = sorted(x for x in xs if x is not None and math.isfinite(x))
    if not xs:
        return float("nan")
    n = len(xs)
    return xs[n // 2] if n % 2 else 0.5 * (xs[n // 2 - 1] + xs[n // 2])


def phase_values(a):
    out = {}
    for m in a.get("metrics", []):
        for ps in m.get("phaseSamples", []) or []:
            v = ps.get("value")
            if isinstance(v, (int, float)) and math.isfinite(v):
                out[(m["key"], ps.get("phase"))] = (v, ps.get("sigma") or m.get("sigma"))
    return out


def sk_diag(a):
    sk = a.get("skeleton3d") or {}
    d = sk.get("diagnostics") or {}
    cam = sk.get("camera") or {}
    pl = d.get("plane") or {}
    return {
        "valid": bool(sk.get("valid")),
        "pooled": bool((sk.get("grip") or {}).get("calibFixed")),
        "reprojFo": d.get("reprojMedPxFo"), "reprojDtl": d.get("reprojMedPxDtl"),
        "cost": d.get("costFinal"), "fF": cam.get("fF"), "fD": cam.get("fD"),
        "psiD": cam.get("psiDDeg"), "pF": cam.get("pFDeg"),
        "planeDown": (pl.get("down") or {}).get("rmsDeg"),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--csv")
    args = ap.parse_args()
    U, P = os.path.join(args.root, "U"), os.path.join(args.root, "P")
    rows = []
    for sess in sorted(os.listdir(U)):
        if not os.path.isdir(os.path.join(P, sess)):
            continue
        fit = defaultdict(list)
        nPhase = nMoved = nHalf = nOne = 0
        worst = []
        ksChanged = 0
        nSw = nPooled = 0
        for sw in sorted(os.listdir(os.path.join(U, sess))):
            du, dp = os.path.join(U, sess, sw), os.path.join(P, sess, sw)
            if not (sw.startswith("swing_") and has_swing(du) and has_swing(dp)):
                continue
            au, apo = load_swing(du)["analysis"], load_swing(dp)["analysis"]
            su, sp = sk_diag(au), sk_diag(apo)
            if not (su["valid"] and sp["valid"]):
                continue
            nSw += 1
            nPooled += sp["pooled"]
            for k in ("reprojFo", "reprojDtl", "cost", "fF", "fD", "psiD", "pF", "planeDown"):
                fit[k + "U"].append(su[k])
                fit[k + "P"].append(sp[k])
            vu, vp = phase_values(au), phase_values(apo)
            for key in vu.keys() & vp.keys():
                (x, s), (y, _) = vu[key], vp[key]
                nPhase += 1
                d = abs(y - x)
                if d > 1e-9:
                    nMoved += 1
                    z = d / s if s and s > 0 else float("inf")
                    nHalf += z > 0.5
                    nOne += z > 1.0
                    worst.append((z, key[0], key[1], sw, x, y, s))
                    rows.append({"session": sess, "swing": sw, "metric": key[0], "phase": key[1],
                                 "U": x, "P": y, "sigma": s, "z": z})
            ku = {n.get("segment"): n for n in (au.get("kinematicSequence") or {}).get("nodes", [])}
            kp = {n.get("segment"): n for n in (apo.get("kinematicSequence") or {}).get("nodes", [])}
            for seg in ku.keys() & kp.keys():
                a, b = ku[seg], kp[seg]
                if (a.get("routeId"), a.get("placed")) != (b.get("routeId"), b.get("placed")) or \
                        abs((a.get("peakDps") or 0) - (b.get("peakDps") or 0)) > 1e-6:
                    ksChanged += 1
        print(f"\n== {sess}: {nSw} swings with a valid fit in both arms, {nPooled} held a pool")
        for k in ("reprojFo", "reprojDtl", "cost", "planeDown", "fF", "fD", "psiD", "pF"):
            u, p = fit[k + "U"], fit[k + "P"]
            spread = lambda xs: (max(x for x in xs if x is not None) - min(x for x in xs if x is not None)) if any(x is not None for x in xs) else float("nan")
            print(f"  {k:10s} median U {med(u):9.2f}  P {med(p):9.2f}   range U {spread(u):8.2f}  P {spread(p):8.2f}")
        print(f"  metric phase samples {nPhase}: moved {nMoved}, > 0.5 σ {nHalf}, > 1 σ {nOne}")
        for z, key, ph, sw, x, y, s in sorted(worst, reverse=True)[:8]:
            print(f"    {z:6.2f} σ  {key} phase {ph} {sw}: {x:.3f} → {y:.3f} (σ {s if s else float('nan'):.3f})")
        print(f"  kinematic-sequence nodes changed: {ksChanged}")
    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["session", "swing", "metric", "phase", "U", "P", "sigma", "z"])
            w.writeheader()
            w.writerows(rows)


if __name__ == "__main__":
    main()
