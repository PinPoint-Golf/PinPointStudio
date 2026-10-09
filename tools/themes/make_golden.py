"""The golden fixture the C++ swing themes (src/Analysis/swing_themes.h) are tested against.

The Python prototype in this directory is the reference: the same ledgers, the same pack and the
same random numbers (theme_rng = std::mt19937_64, one seed per replicate) must give the C++ port
the same table, the same k, the same themes, tiers and words. This script writes both halves:

  strip <src athlete dir> <dst ledgers dir>
      Copies each session's diagnostics.json, cut down to what the themes read — per shot its
      shotId and rows; per row conditionId and state, and on rows that are not notAssessable
      drivingMeasureId, value, corridorLo, corridorHi, corridorShape and readings (measureId,
      value and the same three corridor fields). Compact JSON. Still parses with the app's
      fromJson(root["ledger"]) (diagnostic_ledger.h). Asserts that the swing table, the
      per-session fired/assessed counts and the in-band counts ("what you do well") read from the
      copies equal the originals'.

  expected <ledgers dir> <out json>
      Runs the whole pipeline on the copies with the repo's core.json — table, sparse drop,
      prepare, parallel analysis, fit, zero-fill check, 500 bootstraps, leave-one-session-out,
      clustering, members / when / startsAt / tier / trend, layer 1, "what you do well" (every
      row that passed rule v1's needs-work exclusion, uncapped, before the view drops the rows a
      shown theme touches, as doWellCandidates), each condition's drill and swing position
      (conditionFocus), the focus groups (focusOrder), the summary lines and the VIEW the
      home screen draws (with its focus and "next on your list") — and writes expected.json
      (non-finite numbers as null). Drills are the repo's src/Resources/diagnostics/drills.json.

    /opt/homebrew/bin/python3 -I -B tools/themes/make_golden.py strip \\
        /mnt/swingdata/Mark-Liversedge src/Analysis/tests/data/swing_themes/ledgers
    /opt/homebrew/bin/python3 -I -B tools/themes/make_golden.py expected \\
        src/Analysis/tests/data/swing_themes/ledgers src/Analysis/tests/data/swing_themes/expected.json

Regenerate whenever core.json's orientation (shape, signal directions, unwatchedTail), reducers
(anchor / window phases), golfer phrases (golfer, golferWell, golferWhy, golferHigh, golferLow),
condition kind / detection / prominence / detectedBy / drills change, drills.json's ids, labels
or instructions change, or the algorithm here does.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import theme_pca as tp  # noqa: E402
import theme_summary as ts  # noqa: E402
from theme_data import load_pack, load_table  # noqa: E402
from theme_rng import SEED_BASE  # noqa: E402

PA_SHUFFLES = 500
BOOT = 500
PA_QUANTILE = 95
CLUSTER_CUT = 0.7
TREND_SLOPE = 0.1


def session_dirs(root):
    return sorted(os.path.dirname(p) for p in glob.glob(os.path.join(root, "*", "diagnostics.json")))


# ── strip ────────────────────────────────────────────────────────────────────────────────────────
CORRIDOR = ("corridorLo", "corridorHi", "corridorShape")


def strip_row(row):
    out = {"conditionId": row["conditionId"], "state": row["state"]}
    if row["state"] == "notAssessable":
        return out
    for k in ("drivingMeasureId", "value") + CORRIDOR:
        if k in row:
            out[k] = row[k]
    if row.get("readings"):
        out["readings"] = [{"measureId": m.get("measureId", ""), "value": m.get("value"),
                            **{k: m[k] for k in CORRIDOR if k in m}}
                           for m in row["readings"]]
    return out


def strip_doc(doc):
    led = doc["ledger"]
    return {"schemaVersion": doc.get("schemaVersion"),
            "ledger": {"schemaVersion": led.get("schemaVersion"),
                       "shots": [{"shotId": s["shotId"], "rows": [strip_row(r) for r in s["rows"]]}
                                 for s in led["shots"]]}}


def same_table(a, b):
    return (a.sessions == b.sessions and a.swings == b.swings and a.measures == b.measures
            and np.array_equal(a.session, b.session)
            and np.array_equal(a.values, b.values, equal_nan=True)
            and sorted(a.fired) == sorted(b.fired)
            and all(np.array_equal(a.fired[c], b.fired[c], equal_nan=True) for c in a.fired))


def cmd_strip(src, dst):
    dirs = session_dirs(src)
    total = 0
    for d in dirs:
        name = os.path.basename(d)
        with open(os.path.join(d, "diagnostics.json")) as f:
            doc = json.load(f)
        os.makedirs(os.path.join(dst, name), exist_ok=True)
        out = os.path.join(dst, name, "diagnostics.json")
        with open(out, "w") as f:
            json.dump(strip_doc(doc), f, separators=(",", ":"), allow_nan=False)
        size = os.path.getsize(out)
        total += size
        print(f"  {name}: {len(doc['ledger']['shots'])} shots, "
              f"{os.path.getsize(os.path.join(d, 'diagnostics.json')) / 1e6:.2f} MB -> {size / 1e6:.2f} MB")
    pack = load_pack()
    copies = session_dirs(dst)
    assert [os.path.basename(d) for d in copies] == [os.path.basename(d) for d in dirs], \
        "destination holds sessions the source does not"
    assert same_table(load_table(dirs, pack), load_table(copies, pack)), "table differs"
    assert ts.session_rates(dirs) == ts.session_rates(copies), "fired/assessed counts differ"
    assert (ts.well_counts_from_ledgers(ts.read_ledgers(dirs))
            == ts.well_counts_from_ledgers(ts.read_ledgers(copies))), "in-band counts differ"
    print(f"fixture: {len(dirs)} sessions, {total / 1e6:.2f} MB; swing table, layer-1 and "
          f"in-band counts identical to the originals")


# ── expected ─────────────────────────────────────────────────────────────────────────────────────
def clean(x):
    """numpy -> python, non-finite -> None, recursively."""
    if isinstance(x, dict):
        return {k: clean(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [clean(v) for v in x]
    if isinstance(x, np.ndarray):
        return clean(x.tolist())
    if isinstance(x, (np.bool_, bool)):
        return bool(x)
    if isinstance(x, (np.integer, int)):
        return int(x)
    if isinstance(x, (np.floating, float)):
        return float(x) if math.isfinite(x) else None
    return x


def pipeline(ledgers_dir, pack):
    drills = ts.load_drills()
    cinfo = ts.condition_info(pack, drills)
    dirs = session_dirs(ledgers_dir)
    orient = tp.orientation(pack)
    table = load_table(dirs, pack)
    info = {}
    for m in table.measures:
        sign, how = orient.get(m, (1.0, "twoSided"))
        meta = table.meta.get(m, {})
        info[m] = {"sign": int(sign), "twoSided": how == "twoSided", "when": tp.when_of(meta),
                   "metricKey": meta.get("metricKey", "")}
    t, dropped = tp.drop_sparse_swings(table)
    present = np.unique(t.session)
    swings, sessions = len(t.swings), len(present)
    enough = swings >= ts.MIN_SWINGS and sessions >= ts.MIN_SESSIONS

    ledgers = ts.read_ledgers(dirs)
    off = ts.whats_off(ts.session_rates_from_ledgers(ledgers), pack)
    seen_most = [{"id": r["id"], "share": r["share"], "sessionsSeen": r["sessionsSeen"],
                  "sessionsJudged": r["sessionsJudged"], "trend": r["trend"], "pips": r["pips"]}
                 for r in off]
    all_swings = sum(len(led["shots"]) for _, led in ledgers)
    all_sessions = sum(1 for _, led in ledgers if led["shots"])

    doc = {
        "generator": "tools/themes/make_golden.py",
        "options": {"seed": SEED_BASE, "boot": BOOT, "pa": PA_SHUFFLES, "paQuantile": PA_QUANTILE,
                    "minCoverage": tp.MIN_COVERAGE, "minRowCoverage": tp.MIN_ROW_COVERAGE,
                    "winsor": tp.WINSOR, "trendClip": tp.WINSOR * 3, "loadMin": tp.LOAD_MIN,
                    "emIters": 200, "emTol": 1e-6, "varimaxIters": 500, "varimaxTol": 1e-8,
                    "clusterCut": CLUSTER_CUT, "trendSlope": TREND_SLOPE,
                    "minSwings": ts.MIN_SWINGS, "minSessions": ts.MIN_SESSIONS,
                    "minAssessed": ts.MIN_ASSESSED, "presentShare": ts.PRESENT_SHARE,
                    "recency": ts.RECENCY, "trendDelta": ts.TREND_DELTA,
                    "maxSeenMost": ts.MAX_SEEN_MOST, "maxTogether": ts.MAX_TOGETHER,
                    "maxParts": ts.MAX_PARTS, "wellMinProminence": ts.WELL_MIN_PROMINENCE,
                    "wellMinSessions": ts.WELL_MIN_SESSIONS, "wellMinSwings": ts.WELL_MIN_SWINGS,
                    "wellShare": ts.WELL_SHARE, "wellSession": ts.WELL_SESSION,
                    "maxDoWell": ts.MAX_DO_WELL, "maxNext": ts.MAX_NEXT,
                    "everySwing": ts.EVERY_SWING},
        "sessions": t.sessions,
        "swingsKept": swings, "sessionsKept": sessions, "enough": enough,
        "dropped": [[s, int(i)] for s, i in dropped],
        "measures": [], "measureInfo": info, "Z": [], "k": 0, "eig": [], "paThreshold": [],
        "loadings": [], "varianceShare": [], "themes": [], "clusterLabels": [],
        "seenMost": seen_most,
    }
    themes_for_lines = []
    if enough:
        Z, measures, Xo, spread = tp.prepare(t, orient)
        f = tp.fit(Z, pa_n=PA_SHUFFLES)
        k, L = f["k"], f["L"]
        ezf = tp.em_vs_zero_fill(Z, L, k)
        B = tp.boot_stability(Z, t.session, L, k, BOOT)
        loso = tp.loso_stability(Z, t.session, L, k)
        labels = tp.renumber_by_first_appearance(tp.cluster(Z, cut=CLUSTER_CUT)[0])
        scores, slopes, trends = tp.session_trend(L, f["F"], Xo, spread, t.session, TREND_SLOPE)
        themes = []
        for j in range(k):
            mem = tp.theme_members(L, j, measures, orient, t.meta)
            ids = [x["measure"] for x in mem]
            st = tp.stability_summary(B, loso, j)
            tier = tp.theme_tier(st["bootMedian"], st["bootP05"], st["losoMin"], st["losoMedian"])
            th = {"index": j + 1, "varianceShare": f["varshare"][j],
                  "members": [{"measure": x["measure"], "loading": x["loading"],
                               "rawHigh": x["rawHigh"], "when": x["when"]} for x in mem],
                  "startsAt": tp.starts_at(mem), "tier": tier, **st,
                  "losoPerSession": loso[:, j], "emVsZeroFill": ezf[j],
                  "clusterJaccard": tp.best_cluster(labels, measures, ids)[1],
                  "sessionScores": scores[j], "slope": slopes[j], "trend": int(trends[j])}
            themes.append(th)
        themes_for_lines = themes
        doc.update({"measures": measures, "Z": Z, "k": k, "eig": f["eigPa"],
                    "paThreshold": f["thr"], "loadings": L, "varianceShare": f["varshare"],
                    "themes": themes, "clusterLabels": labels})
    doc["lines"] = {"seenMost": ts.seen_most_lines(off, ts.condition_phrases(pack)),
                    "together": ts.together_lines(themes_for_lines, ts.measure_phrases(pack),
                                                  enough, swings, sessions)}
    well = ts.do_well(ts.well_counts_from_ledgers(ledgers), cinfo, off)
    doc["doWellCandidates"] = well
    doc["conditionFocus"] = {cid: {"drill": c["drill"], "when": c["when"]} for cid, c in sorted(cinfo.items())}
    focus = ts.focus_order(off, cinfo)
    doc["focusOrder"] = focus
    doc["view"] = ts.summary_view(all_swings, all_sessions, well, off, themes_for_lines,
                                  ts.condition_phrases(pack), ts.condition_well_phrases(pack),
                                  ts.measure_phrases(pack), enough, swings, sessions,
                                  focus, ts.condition_why_phrases(pack), drills)
    return clean(doc)


def cmd_expected(ledgers_dir, out):
    t0 = time.time()
    doc = pipeline(ledgers_dir, load_pack())
    wall = time.time() - t0
    with open(out, "w") as f:
        json.dump(doc, f, indent=1, allow_nan=False)
        f.write("\n")
    banned = ("because", "cause", "due to", "leads to")
    v = doc["view"]
    said = (doc["lines"]["seenMost"] + doc["lines"]["together"] + [v["subtitle"], v["note"]]
            + [x["text"] for x in v["doWell"]] + [x["text"] for x in v["needsWork"]]
            + [x["first"] + " " + x["second"] for x in v["together"]]
            + [v["focus"]["title"], v["focus"]["why"]] + v["focus"]["aimFor"]
            + [x["text"] for x in v["focus"]["rightNow"]] + [x["text"] for x in v["next"]])
    for line in said:
        assert not any(w in line.lower() for w in banned), line

    print(f"expected: {doc['swingsKept']} swings kept over {doc['sessionsKept']} sessions "
          f"(dropped {doc['dropped']}); {len(doc['measures'])} measures; k = {doc['k']}; "
          f"{os.path.getsize(out) / 1e3:.0f} kB; {wall:.1f} s")
    for th in doc["themes"]:
        print(f"  T{th['index']}  var {th['varianceShare']:.3f}  tier {th['tier']}  "
              f"boot {th['bootMedian']:.3f} (p05 {th['bootP05']:.3f})  loso min {th['losoMin']:.3f} "
              f"med {th['losoMedian']:.3f}  zero-fill {th['emVsZeroFill']:.3f}  "
              f"jaccard {th['clusterJaccard']:.2f}  startsAt {th['startsAt']}  trend {th['trend']:+d}")
        for m in th["members"]:
            print(f"      {m['loading']:+.3f}  P{m['when']:<2d} {'high' if m['rawHigh'] else 'low ':4s} "
                  f"{m['measure']}")
    print("  seen most:", [(r["id"], round(r["share"], 3), r["trend"]) for r in doc["seenMost"]])
    print("lines.seenMost:")
    for s in doc["lines"]["seenMost"]:
        print("  •", s)
    print("lines.together:")
    for s in doc["lines"]["together"]:
        print("  •", s)
    print("doWellCandidates:", [(r["id"], round(r["share"], 4), r["prominence"], r["sessionsJudged"],
                                 r["swingsJudged"]) for r in doc["doWellCandidates"]])
    print("view:", v["subtitle"])
    for x in v["doWell"]:
        print("  well   ", x["text"], "|", x["caption"], x["pips"])
    for x in v["needsWork"]:
        print("  needs  ", x["text"], "|", x["frequency"], f"{x['share']:.3f}", x["trend"], x["pips"])
    for x in v["together"]:
        print("  tog    ", x["tier"], "|", x["first"], "|", x["second"], "|", x["startStop"],
              x["startWords"], x["trend"])
    print("  note   ", repr(v["note"]))
    print("focusOrder:")
    for g in doc["focusOrder"]:
        print(f"  {g['key']:28s} P{g['when']:<2d} {g['conditionIds']}")
    f = v["focus"]
    print("  focus  ", f["present"], "|", f["title"], "|", f["reason"])
    for x in f["aimFor"]:
        print("    aim   ", x)
    for x in f["rightNow"]:
        print("    now   ", x["text"], "|", x["frequency"], f"{x['share']:.3f}", x["trend"], x["pips"])
    print("    why   ", f["why"])
    print("    drill ", f["practiseLabel"], "|", f["practise"][:60])
    for x in v["next"]:
        print("  next   ", x["text"], "|", x["frequency"], f"{x['share']:.3f}", x["trend"], x["pips"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("strip")
    s.add_argument("src")
    s.add_argument("dst")
    e = sub.add_parser("expected")
    e.add_argument("ledgers")
    e.add_argument("out")
    a = ap.parse_args()
    if a.cmd == "strip":
        cmd_strip(a.src, a.dst)
    else:
        cmd_expected(a.ledgers, a.out)


if __name__ == "__main__":
    main()
