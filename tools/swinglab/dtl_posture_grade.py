#!/usr/bin/env python3
"""dtl_posture_grade.py -- what a swinglab run's down-the-line posture series and swingPlane
say, per session: median, sd, range of each phase reading, and how many swings carry it.

    dtl_posture_grade.py <run-root>

There is no truth for these; this is repeatability and plausibility (dtl_posture_design.md s4).
"""
import json, os, sys, collections
import numpy as np

PH = {0: "P1", 2: "P4", 13: "P5", 9: "P6", 5: "P7", 14: "P8"}
KEYS = ["pelvisThrust", "spineForwardBend", "leadKneeFlexion", "trailKneeFlexion", "ballBodyDistance",
        "balanceHeelToe", "swingPlane"]


def main():
    root = sys.argv[1]
    data = collections.defaultdict(lambda: collections.defaultdict(list))
    n = collections.Counter()
    for sw in sorted(os.listdir(root)):
        p = os.path.join(root, sw, "result.json")
        if not os.path.isfile(p):
            continue
        d = json.load(open(p)); a = d.get("analysis", d)
        sess = sw[:10]
        n[sess] += 1
        ms = {m["key"]: m for m in a.get("metrics", [])}
        for k in KEYS:
            m = ms.get(k)
            if not m:
                continue
            for ps in m.get("phaseSamples", []):
                data[sess][(k, PH.get(ps["phase"], str(ps["phase"])), m.get("unit", ""))].append(ps["value"])
            if k == "pelvisThrust" and m.get("t_us"):
                t = np.array(m["t_us"]); v = np.array(m["value"]); ok = np.array(m.get("valid") or [1] * len(t)) > 0
                pos = {x["p"]: x["t_us"] for x in (a.get("club") or {}).get("positions", [])}
                if 5 in pos and 7 in pos:
                    w = ok & (t >= pos[5]) & (t <= pos[7])
                    if w.any(): data[sess][(k, "max P5-P7", "cm")].append(float(v[w].max()))
                if 1 in pos and 4 in pos:
                    w = ok & (t >= pos[1]) & (t <= pos[4])
                    if w.any(): data[sess][(k, "max P1-P4", "cm")].append(float(v[w].max()))
    for sess in sorted(data):
        print("\n### %s  (%d swings)\n" % (sess, n[sess]))
        print("| measure | at | unit | n | median | sd | min | max |")
        print("|---|---|---|---|---|---|---|---|")
        for (k, ph, unit), v in sorted(data[sess].items()):
            v = np.array(v, float)
            print("| %s | %s | %s | %d | %.1f | %.1f | %.1f | %.1f |" % (k, ph, unit, len(v), np.median(v),
                  np.std(v, ddof=1) if len(v) > 1 else 0, v.min(), v.max()))


if __name__ == "__main__":
    main()
