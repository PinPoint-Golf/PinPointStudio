#!/opt/homebrew/bin/python3
"""Posture chart, stage 0 (posture_chart_design.md §10, Appendix A): the per-position posture readings.

Reads swing_phasegrid.json under a library root (one folder per golfer) and either writes one row
per swing, reading and position, or prints the median change from address per golfer from that CSV
(the §2.1 table).

    posture_positions_extract.py extract <root> <out.csv>
    posture_positions_extract.py medians <csv>

The committed CSV was extracted from the re-analysed copies of 22 Oct 2026
(/mnt/swingdata/scratch/posture-20261021/copies/), the same swings posture_composites_2026-10-22.md
describes. Standard library only.
"""
import csv, glob, json, os, statistics, sys, collections

GOLFERS = ("Mark-Liversedge", "Nick-Henn", "Jess-Ratcliffe")
# Phase ids from src/Analysis/swing_analysis.h, in swing order.
POSITIONS = [("P1", 0), ("P2", 12), ("P3", 8), ("P4", 2), ("P5", 13), ("P6", 9), ("P7", 5), ("P8", 14)]
KEYS = ["headLift", "pelvisThrust", "spineForwardBend", "leadKneeFlexion", "trailKneeFlexion", "pelvisLift"]


def extract(root, out):
    n = 0
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["golfer", "session", "swing", "reading", "position", "value", "change_from_address"])
        for g in GOLFERS:
            for f in sorted(glob.glob(f"{root}/{g}/*/swing_*/swing_phasegrid.json")):
                swing_dir = os.path.dirname(f)
                session, swing = os.path.basename(os.path.dirname(swing_dir)), os.path.basename(swing_dir)
                with open(f) as jf:
                    metrics = {m["key"]: m for m in json.load(jf).get("metrics", [])}
                for k in KEYS:
                    if k not in metrics:
                        continue
                    v = {x["phase"]: x["value"] for x in metrics[k]["values"] if x.get("value") is not None}
                    for name, ph in POSITIONS:
                        if ph not in v:
                            continue
                        change = f"{v[ph] - v[0]:.4f}" if 0 in v else ""
                        w.writerow([g, session, swing, k, name, f"{v[ph]:.4f}", change])
                        n += 1
    print(f"{n} rows -> {out}")


def medians(path):
    acc = collections.defaultdict(lambda: collections.defaultdict(lambda: collections.defaultdict(list)))
    with open(path) as fh:
        for r in csv.DictReader(fh):
            if r["change_from_address"]:
                acc[r["golfer"]][r["reading"]][r["position"]].append(float(r["change_from_address"]))
    for g in GOLFERS:
        print(g)
        for k in KEYS:
            cells = acc[g][k]
            print(" ", k, [f"{statistics.median(cells[n]):.1f}({len(cells[n])})" if cells[n] else "-"
                           for n, _ in POSITIONS])


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "extract":
        extract(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "medians":
        medians(sys.argv[2])
    else:
        sys.exit(__doc__)
