#!/usr/bin/env python3
"""club_track_report.py — the club rate filter's developer diagnostic, library-wide
(docs/design/club_rate_filter_design.md §4).

Every swing analysed with the filter on carries analysis.club.filter: how far the tracker's frames
sat off the filtered shaft angle through the downswing (the wobble), how many it gated or
down-weighted, and the humps in the raw and filtered rate. This lists them worst first and asks the
question the diagnostic exists for: does the wobble mark measurement error? On every swing with a
launch-monitor club speed it reports the rank correlation of each statistic with the clubhead-speed
error at impact, |1 − ours/LM|.

Usage:
  club_track_report.py --runs RUNROOT [--ref-runs OFFROOT] --out DIR
      RUNROOT/<session>/<swing>/result.json from a filter-on sweep; the error is that run's own.
      --ref-runs adds the same correlation against a filter-off sweep's error (what the wobble says
      about the speed the app shows today).
  club_track_report.py --out DIR      # the stored documents (after the library is re-analysed)
"""
import argparse, csv, json, math, os, statistics as st, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import load_swing, find_swing_dirs  # noqa: E402

ROOTS = ["/mnt/swingdata/Mark-Liversedge", "/mnt/swingdata/corpus/swings"]
STATS = ["rateWobble", "residualRmsDeg", "nDownweighted", "nGated", "nisMean", "humpsRaw"]


def impact_ratio(a, src=None):
    def phase5(key):
        for m in a.get("metrics", []):
            if m.get("key") == key:
                for s in m.get("phaseSamples") or []:
                    if s.get("phase") == 5 and s.get("value") is not None:
                        return float(s["value"])
        return None
    ours, lm = phase5("clubheadSpeed"), phase5("lm.clubheadSpeed")
    if lm is None and src:
        lm = (src.get("launchMonitor") or {}).get("clubheadSpeed")
    return (ours / lm) if (ours and lm) else None


def ranks(x):
    order = sorted(range(len(x)), key=lambda i: x[i])
    r = [0.0] * len(x)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and x[order[j + 1]] == x[order[i]]:
            j += 1
        for k in range(i, j + 1):
            r[order[k]] = (i + j) / 2.0
        i = j + 1
    return r


def spearman(x, y):
    if len(x) < 5:
        return None
    rx, ry = ranks(x), ranks(y)
    mx, my = st.mean(rx), st.mean(ry)
    sx = math.sqrt(sum((a - mx) ** 2 for a in rx)); sy = math.sqrt(sum((b - my) ** 2 for b in ry))
    return None if sx == 0 or sy == 0 else sum((a - mx) * (b - my) for a, b in zip(rx, ry)) / (sx * sy)


def sources():
    out = {}
    for r in ROOTS:
        for sd in find_swing_dirs(r):
            out.setdefault("/".join(sd.rstrip("/").split("/")[-2:]), sd)
    return out


def from_runs(root, srcs):
    rows = {}
    for s in sorted(os.listdir(root)):
        for w in sorted(os.listdir(os.path.join(root, s))):
            p = os.path.join(root, s, w, "result.json")
            if not os.path.isfile(p):
                continue
            name = f"{s}/{w}"
            a = json.load(open(p)).get("analysis") or {}
            src = load_swing(srcs[name]) if name in srcs else None
            rows[name] = {"filter": (a.get("club") or {}).get("filter"), "ratio": impact_ratio(a, src)}
    return rows


def from_store(srcs):
    rows = {}
    for name, sd in srcs.items():
        try:
            d = load_swing(sd)
        except Exception:
            continue
        a = d.get("analysis") or {}
        rows[name] = {"filter": (a.get("club") or {}).get("filter"), "ratio": impact_ratio(a, d)}
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs")
    ap.add_argument("--ref-runs")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    srcs = sources()
    rows = from_runs(a.runs, srcs) if a.runs else from_store(srcs)
    ref = from_runs(a.ref_runs, srcs) if a.ref_runs else {}
    have = {k: v for k, v in rows.items() if v["filter"]}

    with open(os.path.join(a.out, "club_track_report.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["swing"] + STATS + ["humpsFiltered", "nEvidence", "ratio", "ratio_ref"])
        for k in sorted(have, key=lambda k: -(have[k]["filter"].get("rateWobble") or 0)):
            fl = have[k]["filter"]
            w.writerow([k] + [fl.get(s) for s in STATS] + [fl.get("humpsFiltered"), fl.get("nEvidence"),
                       have[k]["ratio"], (ref.get(k) or {}).get("ratio")])

    L = ["# Club track diagnostic", "",
         f"{len(have)} swings carry analysis.club.filter.", ""]
    if have:
        L += ["| statistic | median | 90th percentile | max |", "|---|---:|---:|---:|"]
        for s in STATS:
            xs = sorted(v["filter"][s] for v in have.values() if v["filter"].get(s) is not None and v["filter"][s] >= 0)
            if xs:
                L.append(f"| {s} | {st.median(xs):.2f} | {xs[int(0.9 * (len(xs) - 1))]:.2f} | {xs[-1]:.2f} |")
        L += ["", "## Does the wobble mark error? (Spearman with |1 − clubhead speed / launch monitor| at impact)", "",
              "| statistic | n | vs this run's error | vs the reference run's error |", "|---|---:|---:|---:|"]
        for s in STATS:
            p = [(v["filter"][s], abs(1 - v["ratio"])) for v in have.values()
                 if v["ratio"] is not None and v["filter"].get(s) is not None]
            q = [(have[k]["filter"][s], abs(1 - ref[k]["ratio"])) for k in have
                 if k in ref and ref[k]["ratio"] is not None and have[k]["filter"].get(s) is not None]
            r1 = spearman([x for x, _ in p], [y for _, y in p])
            r2 = spearman([x for x, _ in q], [y for _, y in q])
            L.append(f"| {s} | {len(p)} | {'' if r1 is None else f'{r1:+.2f}'} | {'' if r2 is None else f'{r2:+.2f}'} |")
        L += ["", "## Worst ten by rate wobble", "", "| swing | rateWobble | residual ° | gated | down-weighted | humps raw → filtered |",
              "|---|---:|---:|---:|---:|---|"]
        for k in sorted(have, key=lambda k: -(have[k]["filter"].get("rateWobble") or 0))[:10]:
            fl = have[k]["filter"]
            L.append(f"| {k} | {fl.get('rateWobble', 0):.2f} | {fl.get('residualRmsDeg', 0):.1f} | {fl.get('nGated')} | "
                     f"{fl.get('nDownweighted')} | {fl.get('humpsRaw')} → {fl.get('humpsFiltered')} |")
    text = "\n".join(L) + "\n"
    with open(os.path.join(a.out, "club_track_report.md"), "w") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    main()
