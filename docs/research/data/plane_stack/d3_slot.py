"""Plane stack D3, read-only: where the shoulder plane at address reads, and clubSlotTop over the library.

    python3 docs/research/data/plane_stack/d3_slot.py <out.jsonl>

Reads every swing under /mnt/swingdata (never writes there), one document at a time, and writes one JSON line per swing
to <out.jsonl>, then prints the per-golfer table in plane_stack_design.md §11 D3. Each part is sampled as the grader
samples it (extract.py grader_at): swingPlaneCurve, a fitted curve, interpolated AT the ladder instant at P1 and P3;
shoulderPlane off its address phase sample. clubSlotTop = (P3 - P1) / (shoulder - P1); its σ is first order through
the ratio, as club_slot_top.h computes it. Norm (norms.json m_clubSlotTop): mu 0.5, tolerance 0.25, fires < 0 / > 1.
"""
import sys, os, json, glob, gc, math, statistics as st, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract import ladder_of, series_map, grader_at, ROOT, ATHLETES, load_swing

MIN_WIDTH = 4.0   # club.slotMinWidthDeg


def probe(outpath):
    fo = open(outpath, "w")
    for ath in ATHLETES:
        for sess in sorted(glob.glob(os.path.join(ROOT, ath, "20*"))):
            if not os.path.isdir(sess):
                continue
            for sw in sorted(glob.glob(os.path.join(sess, "swing_*"))):
                rec = {"path": sw, "golfer": ath.split("-")[0], "session": os.path.basename(sess)}
                try:
                    d = load_swing(sw)
                except Exception as e:
                    rec["error"] = repr(e)[:200]
                    fo.write(json.dumps(rec) + "\n")
                    continue
                an = d.get("analysis") or {}
                rec["excluded"] = bool((d.get("summary") or {}).get("excluded"))
                rec["club"] = (d.get("summary") or {}).get("club")
                lad = ladder_of(an)
                sm = series_map(an)
                sp, spc = sm.get("shoulderPlane"), sm.get("swingPlaneCurve")
                rec["dtlPose"] = an.get("poseDtl") is not None
                rec["dtlBall"] = bool((((an.get("clubDtl") or {}).get("summary") or {}).get("ball") or {}).get("found"))
                rec["shoulder"] = grader_at(sp, 0, lad) if sp else None
                rec["shoulderSigma"] = sp.get("sigma") if sp else None
                rec["p1"] = grader_at(spc, 0, lad) if spc else None
                rec["p3"] = grader_at(spc, 8, lad) if spc else None
                rec["curveSigma"] = spc.get("sigma") if spc else None
                del d, an, sm
                gc.collect()
                fo.write(json.dumps(rec) + "\n")
    fo.close()


def report(path):
    rows = [json.loads(l) for l in open(path)]
    rows = [r for r in rows if "error" not in r and not r["excluded"]]
    by = collections.defaultdict(list)
    print("golfer swings dtlPose dtlBall shoulderPlane")
    for g in sorted({r["golfer"] for r in rows}):
        rs = [r for r in rows if r["golfer"] == g]
        print(g, len(rs), sum(r["dtlPose"] for r in rs), sum(r["dtlBall"] for r in rs),
              sum(r["shoulder"] is not None for r in rs))
    for r in rows:
        if None in (r["shoulder"], r["p1"], r["p3"]):
            continue
        w = r["shoulder"] - r["p1"]
        if w < MIN_WIDTH:
            continue
        s = (r["p3"] - r["p1"]) / w
        sg = math.sqrt(r["curveSigma"] ** 2 * (1 + (1 - s) ** 2) + s * s * r["shoulderSigma"] ** 2) / w
        by[r["golfer"]].append((s, sg, w))
    print("golfer n median width | median slot | <0 | 0-.25 | .25-.75 | .75-1 | >1 | median sigma")
    for g, v in by.items():
        s = [a[0] for a in v]
        print(g, len(v), "%.1f" % st.median(a[2] for a in v), "| %.2f" % st.median(s),
              *("| %d" % sum(f(x) for x in s) for f in (lambda x: x < 0, lambda x: 0 <= x < .25,
                                                       lambda x: .25 <= x <= .75, lambda x: .75 < x <= 1,
                                                       lambda x: x > 1)),
              "| %.3f" % st.median(a[1] for a in v))


if __name__ == "__main__":
    probe(sys.argv[1])
    report(sys.argv[1])
