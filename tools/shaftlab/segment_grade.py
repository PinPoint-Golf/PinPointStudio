#!/usr/bin/env python3
"""
segment_grade — Phase 2 of the markerless club tracker
(docs/design/markerless_club_tracker_design.md §5.1).

Grades the E4 steel-segment lock against the E1 band lock, frame by frame, from
swinglab_run --trace output (trace.jsonl, one line per frame carrying seg_* and
band_* when `shaft.seg.enabled` was set). The band lock is the per-frame
reference: it exists only where E1 matched ≥ 4 collinear saturated blobs at the
recorded band ratios, and is corpus-validated to 0.3° — so a segment lock on the
same frame can be scored on θ, s and r0 with no hand truth.

Where no band lock exists the segment lock is scored against the tracker's
final θ (theta_out) on RAY-tier frames — a weaker reference (1.7° class) — and
its lock RATE per phase is what §5.1 asks for at address and the finish.

Usage:
  segment_grade.py --runs <run_root> --out-csv frames.csv --out-md summary.md
"""
import argparse, collections, csv, glob, json, math, os
import numpy as np

# shaft_track_assembly.h SwingPhase
PHASE = {0: "addr", 1: "back", 2: "top", 3: "down", 4: "impact", 5: "thru", 6: "finish"}
PORDER = ["addr", "back", "top", "down", "impact", "thru", "finish"]

def wrap(d):
    d = (d + 180.0) % 360.0 - 180.0
    return d

def q(v, p):
    return float(np.percentile(v, p)) if len(v) else float("nan")

def load(run_root):
    rows = []
    for tf in sorted(glob.glob(os.path.join(run_root, "*", "trace.jsonl"))):
        run = os.path.basename(os.path.dirname(tf))
        for line in open(tf):
            r = json.loads(line)
            if "tier" not in r: continue
            rows.append(dict(run=run, frame=r.get("frame", r.get("f")), phase=PHASE.get(r.get("phase"), "?"),
                             tier=r["tier"], theta_out=r.get("theta_out"), theta_dp=r.get("theta_dp"),
                             span=("raw_p97" in r),
                             seg_mode=r.get("seg_mode", 0), seg_pass=r.get("seg_pass"), seg_theta=r.get("seg_theta"),
                             seg_s=r.get("seg_s"), seg_r0=r.get("seg_r0"), seg_n=r.get("seg_n"),
                             seg_sup=r.get("seg_sup"), seg_distal=r.get("seg_distal"), seg_stage=r.get("seg_stage"),
                             seg_rg=r.get("seg_rg"), seg_rf=r.get("seg_rf"), seg_onset=r.get("seg_onset"),
                             band_theta=r.get("band_theta"), band_s=r.get("band_s"), band_r0=r.get("band_r0"),
                             band_n=r.get("band_n")))
    return rows

RUN_ROOT = [""]

def grade(rows, out_md):
    L = []
    runs = sorted({r["run"] for r in rows})
    L.append(f"### Population\n\n{len(runs)} swings, {len(rows)} traced frames.\n")

    # ── 0. Yardstick: band lock vs segment lock on the SAME denominator ──────
    # Mark, 2026-09-08: quantify progress against the marked club's existing
    # approach. Every span frame (the tracker probed it) counts; a lock is a lock.
    L.append("### 0. Yardstick — the marked club's band lock vs the segment lock, all span frames\n")
    L.append("θ is scored against the DP's direction (the tracker's own answer) for both; band is the corpus-validated 0.3° reference.\n")
    L.append("| phase | span frames | band lock | segment lock | either | band θ vs DP p50/p90 | seg θ vs DP p50/p90 | tier band | tier seg | tier ray | tier wedge | tier pred |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for ph in PORDER + ["ALL"]:
        R = [r for r in rows if r["span"] and (ph == "ALL" or r["phase"] == ph)]
        if not R: continue
        n = len(R)
        B = [r for r in R if r["band_n"]]; S = [r for r in R if r["seg_mode"] > 0]
        E = [r for r in R if r["band_n"] or r["seg_mode"] > 0]
        tb = [abs(wrap(r["band_theta"] - r["theta_dp"])) for r in B if r["theta_dp"] is not None]
        ts = [abs(wrap(r["seg_theta"] - r["theta_dp"])) for r in S if r["theta_dp"] is not None]
        t = collections.Counter(r["tier"] for r in R)
        L.append(f"| {ph} | {n} | {len(B)/n*100:.0f}% | {len(S)/n*100:.0f}% | {len(E)/n*100:.0f}% | "
                 f"{q(tb,50):.1f}°/{q(tb,90):.1f}° | {q(ts,50):.1f}°/{q(ts,90):.1f}° | "
                 f"{t['band']/n*100:.0f}% | {t['seg']/n*100:.0f}% | {t['ray']/n*100:.0f}% | {t['wedge']/n*100:.0f}% | {t['pred']/n*100:.0f}% |")
    L.append("\nBand and segment scale, where both exist on a frame, are compared in A; the band's own frame-to-frame scale jitter (the reference's precision) is in A4.\n")
    # still frames outside the evidence span: no band, no ridge — segment only
    L.append("### 0b. Still frames OUTSIDE the evidence span (address hold, held finish) — segment lock only, no band reference exists there\n")
    L.append("| phase | frames | segment lock | FULL | tier seg | tier pred |\n|---|---|---|---|---|---|")
    for ph in ("addr", "finish", "ALL"):
        R = [r for r in rows if not r["span"] and (ph == "ALL" or r["phase"] == ph)]
        if not R: continue
        n = len(R); S = [r for r in R if r["seg_mode"] > 0]; F = [r for r in R if r["seg_mode"] == 1]
        t = collections.Counter(r["tier"] for r in R)
        L.append(f"| {ph} | {n} | {len(S)/n*100:.0f}% | {len(F)/n*100:.0f}% | {t['seg']/n*100:.0f}% | {t['pred']/n*100:.0f}% |")
    L.append("")

    # ── A. vs the band lock ─────────────────────────────────────────────────
    L.append("### A. Segment lock vs band lock, same frame (band = reference)\n")
    L.append("| phase | band frames | seg any | seg FULL | θ err p50 | θ err p90 | θ >15° | s err p50 | s err p90 | r0 err p50 (mm) | conflict >6° |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|")
    allrows = []
    for ph in PORDER + ["ALL"]:
        R = [r for r in rows if r["band_n"] and (ph == "ALL" or r["phase"] == ph)]
        if not R: continue
        S = [r for r in R if r["seg_mode"] > 0]
        F = [r for r in R if r["seg_mode"] == 1]
        th = [abs(wrap(r["seg_theta"] - r["band_theta"])) for r in S]
        se = [abs(r["seg_s"] - r["band_s"]) / r["band_s"] * 100 for r in F]
        r0 = [abs(r["seg_r0"] - r["band_r0"]) for r in F]
        conf = np.mean([e > 6.0 for e in th]) * 100 if th else float("nan")
        big = np.mean([e > 15.0 for e in th]) * 100 if th else float("nan")
        L.append(f"| {ph} | {len(R)} | {len(S)/len(R)*100:.0f}% | {len(F)/len(R)*100:.0f}% | {q(th,50):.1f}° | {q(th,90):.1f}° | {big:.1f}% | "
                 f"{q(se,50):.1f}% | {q(se,90):.1f}% | {q(r0,50):.0f} | {conf:.1f}% |")

    # ── A2. landmark anatomy against the band geometry ──────────────────────
    # The band lock's (s, r0) predicts where the steel's ends sit on the same ray:
    # rG* = s·(gripEnd − r0), rF* = s·(hoselTop − ferrule − r0). Scoring the two
    # ends separately says WHICH landmark carries the scale error.
    L.append("\n### A2. Landmark error vs the band geometry (same frame; grip end 265 mm, steel end 870 mm)\n")
    L.append("| phase | FULL locks | rG err p50 (px) | rG err p90 | rF err p50 (px) | rF err p90 | rF err p50 (% of steel) | TERMINUS locks | rF err p50 (px) | rF err p90 |")
    L.append("|---|---|---|---|---|---|---|---|---|---|")
    for ph in PORDER + ["ALL"]:
        R = [r for r in rows if r["band_n"] and r["seg_mode"] > 0 and (ph == "ALL" or r["phase"] == ph)
             and abs(wrap(r["seg_theta"] - r["band_theta"])) <= 6.0]
        if not R: continue
        F = [r for r in R if r["seg_mode"] == 1]; T = [r for r in R if r["seg_mode"] == 2]
        rg = [abs(r["seg_rg"] - r["band_s"] * (265.0 - r["band_r0"])) for r in F]
        rfF = [abs(r["seg_rf"] - r["band_s"] * (870.0 - r["band_r0"])) for r in F]
        rfFp = [abs(r["seg_rf"] - r["band_s"] * (870.0 - r["band_r0"])) / (r["band_s"] * 605.0) * 100 for r in F]
        rfT = [abs(r["seg_rf"] - r["band_s"] * (870.0 - r["band_r0"])) for r in T]
        L.append(f"| {ph} | {len(F)} | {q(rg,50):.0f} | {q(rg,90):.0f} | {q(rfF,50):.0f} | {q(rfF,90):.0f} | {q(rfFp,50):.0f}% | "
                 f"{len(T)} | {q(rfT,50):.0f} | {q(rfT,90):.0f} |")
    L.append("\n(rows restricted to locks within 6° of the band direction, so the landmark error is measured on the right ray)")
    # ── A2b. terminus placement vs the two candidate millimetres ─────────────
    L.append("\n### A2b. Terminus by distal tag: signed error vs the steel end (870 mm) and vs the hosel end (922 mm)\n")
    L.append("| distal | locks | vs 870: p50 (px) | vs 922: p50 (px) | within ±15 px of the better | ")
    L.append("|---|---|---|---|---|")
    for dt, name in ((1, "ferrule resolved"), (2, "head after"), (3, "dark end")):
        R = [r for r in rows if r["band_n"] and r["seg_mode"] > 0 and r["seg_distal"] == dt
             and abs(wrap(r["seg_theta"] - r["band_theta"])) <= 6.0]
        if not R: continue
        e870 = [r["seg_rf"] - r["band_s"] * (870.0 - r["band_r0"]) for r in R]
        e922 = [r["seg_rf"] - r["band_s"] * (922.0 - r["band_r0"]) for r in R]
        better = e870 if abs(q(e870, 50)) < abs(q(e922, 50)) else e922
        L.append(f"| {name} | {len(R)} | {q(e870,50):+.0f} | {q(e922,50):+.0f} | {np.mean([abs(x) <= 15 for x in better])*100:.0f}% |")

    # ── A3. onset anatomy: which landmark, and what millimetre it really sat at ─
    L.append("\n### A3. Proximal landmark by onset type (FULL locks on band frames within 6°)\n")
    L.append("| phase | onset | locks | s err p50 | s err p90 | measured m_G p50 (mm from butt) | m_G p10 | m_G p90 | assumed |")
    L.append("|---|---|---|---|---|---|---|---|---|")
    for ph in PORDER + ["ALL"]:
        for on_t, assumed in ((1, 265), (2, 180)):
            R = [r for r in rows if r["band_n"] and r["seg_mode"] == 1 and r.get("seg_onset") == on_t
                 and (ph == "ALL" or r["phase"] == ph) and abs(wrap(r["seg_theta"] - r["band_theta"])) <= 6.0]
            if not R: continue
            se = [abs(r["seg_s"] - r["band_s"]) / r["band_s"] * 100 for r in R]
            mg = [r["seg_rg"] / r["band_s"] + r["band_r0"] for r in R]
            L.append(f"| {ph} | {'grip end' if on_t == 1 else 'hands edge'} | {len(R)} | {q(se,50):.1f}% | {q(se,90):.1f}% | {q(mg,50):.0f} | {q(mg,10):.0f} | {q(mg,90):.0f} | {assumed} |")

    # ── A4. the reference's own precision: band s / r0 frame-to-frame ─────────
    L.append("\n### A4. Reference precision — band lock scale and offset, consecutive-frame relative change\n")
    L.append("| quantity | p50 | p90 | n pairs |\n|---|---|---|---|")
    byrun = collections.defaultdict(list)
    for r in rows:
        if r["band_n"]: byrun[r["run"]].append(r)
    ds, dr = [], []
    for run, R in byrun.items():
        R.sort(key=lambda r: r["frame"])
        for a, b in zip(R, R[1:]):
            if b["frame"] - a["frame"] <= 2:
                ds.append(abs(b["band_s"] - a["band_s"]) / a["band_s"] * 100); dr.append(abs(b["band_r0"] - a["band_r0"]))
    L.append(f"| band s, % change between adjacent band frames | {q(ds,50):.1f}% | {q(ds,90):.1f}% | {len(ds)} |")
    L.append(f"| band r0, mm change between adjacent band frames | {q(dr,50):.0f} | {q(dr,90):.0f} | {len(dr)} |")
    L.append("\n(a segment-vs-band scale error at or below the band's own adjacent-frame change is at the reference's floor)")

    # ── B. where the band lock is absent ────────────────────────────────────
    L.append("\n### B. Segment lock where the band lock is ABSENT (θ vs the tracker's final θ on RAY frames)\n")
    L.append("| phase | frames | seg any | seg FULL | seg TERMINUS | RAY frames | θ err p50 | θ err p90 | θ >15° |")
    L.append("|---|---|---|---|---|---|---|---|---|")
    for ph in PORDER + ["ALL"]:
        R = [r for r in rows if not r["band_n"] and (ph == "ALL" or r["phase"] == ph)]
        if not R: continue
        S = [r for r in R if r["seg_mode"] > 0]
        F = [r for r in R if r["seg_mode"] == 1]; T = [r for r in R if r["seg_mode"] == 2]
        ray = [r for r in S if r["tier"] == "ray" and r["theta_out"] is not None]
        th = [abs(wrap(r["seg_theta"] - r["theta_out"])) for r in ray]
        big = np.mean([e > 15.0 for e in th]) * 100 if th else float("nan")
        L.append(f"| {ph} | {len(R)} | {len(S)/len(R)*100:.0f}% | {len(F)/len(R)*100:.0f}% | {len(T)/len(R)*100:.0f}% | {len(ray)} | "
                 f"{q(th,50):.1f}° | {q(th,90):.1f}° | {big:.1f}% |")

    # ── C. lock anatomy ────────────────────────────────────────────────────
    L.append("\n### C. Lock anatomy (all segment locks)\n")
    S = [r for r in rows if r["seg_mode"] > 0]
    dist = collections.Counter(r["seg_distal"] for r in S)
    pas = collections.Counter(r["seg_pass"] for r in S)
    nb = collections.Counter(r["seg_n"] for r in S)
    L.append(f"- locks: {len(S)} of {len(rows)} frames ({len(S)/max(len(rows),1)*100:.0f}%); pass 1 {pas.get(1,0)}, pass 2 {pas.get(2,0)}")
    L.append(f"- distal: ferrule {dist.get(1,0)}, hosel {dist.get(2,0)}, dark end {dist.get(3,0)}")
    L.append(f"- landmarks n: " + ", ".join(f"{k}: {v}" for k, v in sorted(nb.items())))
    sup = [r["seg_sup"] for r in S]
    L.append(f"- support p50 {q(sup,50):.2f}, p10 {q(sup,10):.2f}")
    U = [r for r in rows if r["seg_mode"] == 0 and r.get("seg_stage") is not None]
    st = collections.Counter(r["seg_stage"] for r in U)
    names = {0: "not probed", 1: "no run", 2: "support", 3: "off-frame", 4: "no distal landmark", 5: "distal edge", 6: "no onset, no prior", 7: "s/r0 gate", 8: "length gate"}
    L.append("- unlocked frames by furthest stage reached: " + ", ".join(f"{names.get(k,k)} {v}" for k, v in sorted(st.items())))

    # ── E. lengths per swing from result.json: the fused club length against the
    #      ball's address measurement, and which ladder rung fed the projection ──
    L.append("\n### E. Club length per swing (result.json `analysis.club.lengths`)\n")
    L.append("| run | ball px | band px | fused px | fused / ball | ladder rung | estimators |\n|---|---|---|---|---|---|---|")
    for run in runs:
        rp = os.path.join(RUN_ROOT[0], run, "result.json")
        if not os.path.exists(rp): continue
        try: c = json.load(open(rp))["analysis"]["club"]; Ln = c.get("lengths", {})
        except Exception: continue
        ball, band, fused = Ln.get("ballPx", -1), Ln.get("bandPx", -1), Ln.get("fusedPx", -1)
        ratio = f"{fused/ball:.3f}" if ball and ball > 0 and fused and fused > 0 else "—"
        L.append(f"| {run} | {ball:.0f} | {band:.0f} | {fused:.0f} | {ratio} | {Ln.get('ladderRung','?')} | {Ln.get('nEstimators','?')} |")

    # ── D. per swing ───────────────────────────────────────────────────────
    L.append("\n### D. Per swing\n\n| run | frames | band | seg | both | θ err p50 (both) | θ >6° (both) | s err p50 |\n|---|---|---|---|---|---|---|---|")
    for run in runs:
        R = [r for r in rows if r["run"] == run]
        B = [r for r in R if r["band_n"]]; S = [r for r in R if r["seg_mode"] > 0]
        both = [r for r in R if r["band_n"] and r["seg_mode"] > 0]
        th = [abs(wrap(r["seg_theta"] - r["band_theta"])) for r in both]
        se = [abs(r["seg_s"] - r["band_s"]) / r["band_s"] * 100 for r in both if r["seg_mode"] == 1]
        c6 = np.mean([e > 6 for e in th]) * 100 if th else float("nan")
        L.append(f"| {run} | {len(R)} | {len(B)} | {len(S)} | {len(both)} | {q(th,50):.1f}° | {c6:.0f}% | {q(se,50):.1f}% |")
    open(out_md, "w").write("\n".join(L) + "\n")
    print("\n".join(L))

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", required=True); ap.add_argument("--out-csv", required=True); ap.add_argument("--out-md", required=True)
    a = ap.parse_args()
    RUN_ROOT[0] = a.runs
    rows = load(a.runs)
    with open(a.out_csv, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    grade(rows, a.out_md)
