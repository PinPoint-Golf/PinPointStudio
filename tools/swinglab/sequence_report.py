#!/usr/bin/env python3
"""Tabulate the kinematic sequence over a swinglab run root.

Reads every ``<swing>/result.json`` under ``--root`` (the ``--out`` dirs a
``swinglab_run`` pass wrote) and summarises ``analysis.kinematicSequence``
(src/Analysis/kinematic_sequence_json.h) — the Stage 1 corpus read of
docs/design/kinematic_sequence_design.md §9:

  * per segment: how many swings produced a rate, how many PLACED the node, the
    median / IQR of the peak instant before impact, the median timing σ, the
    median peak angular speed, and which routes produced it;
  * overall: the orderResolved rate, the verdict histogram, the routeSummary
    histogram;
  * the club node against ``clubheadPeakLead`` (the LINEAR clubhead-speed peak's
    lead before impact, a live metric): median / IQR of the difference.

The last column carries Cheetham (2008) — golf_swing_normative_reference.md §2 —
as REFERENCE ONLY. Nothing here is a grade (NR-03): it is a distribution beside
a published one so a reader can see whether the nodes land where a sequence
lives at all.

Stdlib only. Prints a Markdown block and writes ``sequence_report.csv`` (one row
per swing) into the run root unless ``--csv`` says otherwise.

    python3 tools/swinglab/sequence_report.py --root /tmp/ks-corpus --tag corpus-pose2
"""

import argparse
import csv
import json
import os
import statistics
import sys
from collections import Counter

SEGMENTS = ["pelvis", "thorax", "leadArm", "club"]
LABEL = {"pelvis": "Pelvis", "thorax": "Thorax", "leadArm": "Lead arm", "club": "Club"}

# Cheetham et al. 2008, professionals (mean ± SD). Time of peak before impact (ms)
# and peak rotational speed (°/s). The club has no published timing (it peaks at
# the ball by definition).
CHEETHAM_MS = {"pelvis": "87 ± 19", "thorax": "68 ± 14", "leadArm": "65 ± 8", "club": "—"}
CHEETHAM_DPS = {"pelvis": "477 ± 53", "thorax": "727 ± 61", "leadArm": "980 ± 68", "club": "2254 ± 68"}


def find_results(root):
    out = []
    for dirpath, _dirs, files in os.walk(root):
        if "result.json" in files:
            out.append(os.path.join(dirpath, "result.json"))
    return sorted(out)


def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def club_peak_lead(analysis):
    """clubheadPeakLead's Impact phaseSample value (ms), or None."""
    for m in analysis.get("metrics", []):
        if m.get("key") != "clubheadPeakLead":
            continue
        for ps in m.get("phaseSamples", []):
            if ps.get("phase") == 5:          # Phase::Impact
                return float(ps.get("value"))
        for ps in m.get("phaseSamples", []):
            return float(ps.get("value"))
    return None


def med_iqr(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return "—"
    if len(xs) < 2:
        return f"{xs[0]:.0f}"
    q = statistics.quantiles(xs, n=4, method="inclusive")
    return f"{statistics.median(xs):.0f} [{q[0]:.0f}, {q[2]:.0f}]"


def med(xs, fmt="{:.0f}"):
    xs = [x for x in xs if x is not None]
    return fmt.format(statistics.median(xs)) if xs else "—"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True, help="run root holding <swing>/result.json files")
    ap.add_argument("--csv", default=None, help="CSV path (default: <root>/sequence_report.csv)")
    ap.add_argument("--tag", default="", help="label echoed in the header")
    args = ap.parse_args()

    results = find_results(args.root)
    if not results:
        print(f"no result.json under {args.root}", file=sys.stderr)
        return 2

    rows = []
    n_no_ks = 0
    for path in results:
        swing = os.path.relpath(os.path.dirname(path), args.root)
        try:
            doc = load(path)
        except (OSError, ValueError) as e:
            print(f"skip {swing}: {e}", file=sys.stderr)
            continue
        analysis = doc.get("analysis", {})
        ks = analysis.get("kinematicSequence")
        row = {"swing": swing, "clubheadPeakLead": club_peak_lead(analysis)}
        if not ks:
            n_no_ks += 1
            row.update({"orderResolved": "", "verdict": "", "routeSummary": ""})
            for s in SEGMENTS:
                row[f"{s}_produced"] = 0
            rows.append(row)
            continue
        nodes = {n.get("segment"): n for n in ks.get("nodes", [])}
        row["orderResolved"] = ks.get("orderResolved", False)
        row["verdict"] = ks.get("verdict", "")
        row["routeSummary"] = ks.get("routeSummary", "")
        row["order"] = " → ".join(ks.get("order", []))
        for s in SEGMENTS:
            n = nodes.get(s)
            row[f"{s}_produced"] = 1 if n else 0
            row[f"{s}_placed"] = (1 if n.get("placed") else 0) if n else ""
            row[f"{s}_beforeImpactMs"] = n.get("beforeImpactMs") if n else ""
            row[f"{s}_tSigmaMs"] = n.get("tSigmaMs") if n else ""
            row[f"{s}_peakDps"] = n.get("peakDps") if n else ""
            row[f"{s}_routeId"] = n.get("routeId", "") if n else ""
        rows.append(row)

    # ── CSV ───────────────────────────────────────────────────────────────────
    csv_path = args.csv or os.path.join(args.root, "sequence_report.csv")
    fields = ["swing"]
    for s in SEGMENTS:
        fields += [f"{s}_produced", f"{s}_placed", f"{s}_beforeImpactMs", f"{s}_tSigmaMs",
                   f"{s}_peakDps", f"{s}_routeId"]
    fields += ["order", "orderResolved", "verdict", "routeSummary", "clubheadPeakLead"]
    with open(csv_path, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)

    # ── Markdown ──────────────────────────────────────────────────────────────
    n = len(rows)
    with_ks = [r for r in rows if r.get("verdict") != "" or any(r.get(f"{s}_produced") for s in SEGMENTS)]
    tag = f" — {args.tag}" if args.tag else ""
    print(f"## Kinematic sequence{tag}")
    print()
    print(f"Run root `{args.root}` · {n} swings · {len(with_ks)} with a sequence object · "
          f"{n_no_ks} without")
    print()
    print("| Segment | produced | placed | peak before impact, ms (median [IQR]) | σt ms (median) | "
          "peak °/s (median) | routes | Cheetham 2008 pros: ms · °/s (reference) |")
    print("|---|---|---|---|---|---|---|---|")
    for s in SEGMENTS:
        prod = [r for r in rows if r.get(f"{s}_produced")]
        placed = [r for r in prod if r.get(f"{s}_placed") == 1]
        routes = Counter(r.get(f"{s}_routeId") for r in prod)
        print(f"| {LABEL[s]} | {len(prod)} | {len(placed)} | "
              f"{med_iqr([r[f'{s}_beforeImpactMs'] for r in placed])} | "
              f"{med([r[f'{s}_tSigmaMs'] for r in prod])} | "
              f"{med([r[f'{s}_peakDps'] for r in placed])} | "
              f"{', '.join(f'{k}×{v}' for k, v in sorted(routes.items()) if k)} | "
              f"{CHEETHAM_MS[s]} · {CHEETHAM_DPS[s]} |")
    print()

    resolved = sum(1 for r in with_ks if r.get("orderResolved") is True)
    print(f"**Order resolved**: {resolved} of {len(with_ks)} "
          f"({(100.0 * resolved / len(with_ks)) if with_ks else 0:.0f} %)")
    print()
    print("| Verdict | swings |")
    print("|---|---|")
    for k, v in sorted(Counter(r.get("verdict") for r in with_ks).items(), key=lambda kv: -kv[1]):
        print(f"| {k or '(none)'} | {v} |")
    print()
    print("| Route summary | swings |")
    print("|---|---|")
    for k, v in sorted(Counter(r.get("routeSummary") for r in with_ks).items(), key=lambda kv: -kv[1]):
        print(f"| {k or '(none)'} | {v} |")
    print()
    print("| Order (placed nodes) | swings |")
    print("|---|---|")
    for k, v in sorted(Counter(r.get("order", "") for r in with_ks).items(), key=lambda kv: -kv[1]):
        print(f"| {k or '(none placed)'} | {v} |")
    print()

    diffs = []
    for r in rows:
        if r.get("club_placed") == 1 and r.get("clubheadPeakLead") is not None:
            diffs.append(float(r["club_beforeImpactMs"]) - float(r["clubheadPeakLead"]))
    print(f"**Club node vs clubheadPeakLead** (angular peak lead − linear peak lead, ms; "
          f"{len(diffs)} swings with both): {med_iqr(diffs)}")
    print()
    print(f"CSV: `{csv_path}`")
    return 0


if __name__ == "__main__":
    sys.exit(main())
