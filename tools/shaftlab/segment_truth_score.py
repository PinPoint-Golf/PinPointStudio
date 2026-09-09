#!/usr/bin/env python3
"""
segment_truth_score — score the tracker and the steel-segment lock against HAND
TRUTH (truth.json, markup lab) on an unmarked club, where no band lock exists.

For every marked shaft frame: the tracker's published direction and head (result.json
samples[]), and the segment lock's direction (trace.jsonl seg_*), against the mark's
theta and head pixel. Marks are split into SEEN (P1–P6) and INFERRED (P7–P10: Mark's
standing rule for the 2026-09-09 6-iron — through impact the shaft is invisible and
the mark is a line from the hands to the clubhead), because the detector builds its
line the same way there and agreement is two constructions coinciding.

Yardstick (the marked club, docs/research/club_detection_from_video.md): band tier
0.3° / ray tier 1.7° vs dense taped truth; Stage-2 measured head 0.8–2.4 px median.

Usage: segment_truth_score.py --runs <run_root> --truth-root <session dir> --out-md f.md
"""
import argparse, glob, json, math, os, collections
import numpy as np

def wrap(d): return (d + 180.0) % 360.0 - 180.0
def q(v, p): return float(np.percentile(v, p)) if len(v) else float("nan")

def load_swing(run_dir, truth_path):
    res = json.load(open(os.path.join(run_dir, "result.json")))["analysis"]
    club = res["club"]; W, H = club["frameWidth"], club["frameHeight"]
    samples = club["samples"]
    t_index = {s["t_us"]: i for i, s in enumerate(samples)}
    times = np.array([s["t_us"] for s in samples])
    trace = {}
    tp = os.path.join(run_dir, "trace.jsonl")
    if os.path.exists(tp):
        for line in open(tp):
            r = json.loads(line)
            if "tier" in r: trace[r["frame"]] = r
    truth = json.load(open(truth_path))
    if truth.get("meta", {}).get("source") == "instrumented" or len(truth.get("shaft", [])) > 120:
        return []   # auto-generated band truth (a different time domain, not a human mark) — not this scorer's business
    ev = truth.get("events", {}); t0 = ev.get("t0_us", 0)
    pmarks = {}
    for k, v in ev.items():
        if k.startswith("p") and k.endswith("_s"):
            pmarks[int(k[1:-2])] = t0 + v * 1e6
    out = []
    for m in truth.get("shaft", []):
        i = int(np.argmin(np.abs(times - m["t_us"])))
        if abs(times[i] - m["t_us"]) > 4000: continue
        p = min(pmarks, key=lambda k: abs(pmarks[k] - m["t_us"])) if pmarks else None
        if p is not None and abs(pmarks[p] - m["t_us"]) > 4000: p = None
        s = samples[i]; tr = trace.get(i, {})
        th_truth = math.degrees(m["theta"]) % 360
        th_trk = math.degrees(s["theta"]) % 360
        head_trk = (s["head"][0] * W, s["head"][1] * H)
        head_err = math.hypot(head_trk[0] - m["head"][0], head_trk[1] - m["head"][1])
        # decompose along the TRUTH line: radial (+ = tracker head beyond the marked head) and lateral
        ux, uy = math.cos(m["theta"]), math.sin(m["theta"])
        dx, dy = head_trk[0] - m["head"][0], head_trk[1] - m["head"][1]
        head_rad, head_lat = dx * ux + dy * uy, abs(-dx * uy + dy * ux)
        len_trk = math.hypot(head_trk[0] - s["grip"][0] * W, head_trk[1] - s["grip"][1] * H)
        head_kind = ("off" if int(s.get("flags", 0)) & 0x80 else "projected" if int(s.get("flags", 0)) & 0x10 else "measured")
        grip_off = math.hypot(s["grip"][0] * W - m["grip"][0], s["grip"][1] * H - m["grip"][1])
        out.append(dict(frame=i, p=p, group=("inferred" if (p or 0) >= 7 else "seen"), tier=tr.get("tier", "?"),
                        th_err=abs(wrap(th_trk - th_truth)),
                        seg=tr.get("seg_mode", 0) > 0,
                        seg_th_err=(abs(wrap(tr["seg_theta"] - th_truth)) if tr.get("seg_mode", 0) > 0 else None),
                        head_err=head_err, head_rad=head_rad, head_lat=head_lat, head_kind=head_kind, len_trk=len_trk, head_measured=bool(int(s.get("flags", 0)) & 0x01) and not (int(s.get("flags", 0)) & 0x10),
                        grip_off=grip_off, len_truth=m["len"]))
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", required=True); ap.add_argument("--truth-root", required=True); ap.add_argument("--out-md", required=True)
    a = ap.parse_args()
    rows = []
    for td in sorted(glob.glob(os.path.join(a.truth_root, "swing_*", "truth.json"))):
        sw = os.path.basename(os.path.dirname(td))
        runs = glob.glob(os.path.join(a.runs, f"*__{sw}"))
        if not runs: continue
        for r in load_swing(runs[0], td): r["swing"] = sw; rows.append(r)
    L = [f"### Hand truth vs tracker and segment lock — {len({r['swing'] for r in rows})} swings, {len(rows)} marked frames\n",
         "Yardstick, marked club vs dense taped truth: band tier 0.3°, ray tier 1.7°; Stage-2 measured head 0.8–2.4 px median.\n",
         "| group | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) | measured-head frames | seg lock present | seg θ err p50 / p90 | tiers |",
         "|---|---|---|---|---|---|---|---|"]
    for g in ("seen", "inferred", "all"):
        R = [r for r in rows if g == "all" or r["group"] == g]
        if not R: continue
        th = [r["th_err"] for r in R]; he = [r["head_err"] for r in R]
        S = [r for r in R if r["seg"]]; st = [r["seg_th_err"] for r in S]
        hm = [r for r in R if r["head_measured"]]
        tiers = collections.Counter(r["tier"] for r in R)
        L.append(f"| {g} | {len(R)} | {q(th,50):.1f}° / {q(th,90):.1f}° | {q(he,50):.0f} / {q(he,90):.0f} | {len(hm)} ({q([r['head_err'] for r in hm],50):.0f} px p50) | {len(S)/len(R)*100:.0f}% | {q(st,50):.1f}° / {q(st,90):.1f}° | {dict(tiers)} |")
    L.append("\n| P | marks | tracker θ err p50 | head err p50 (px) | seg present | seg θ err p50 |\n|---|---|---|---|---|---|")
    for p in range(1, 11):
        R = [r for r in rows if r["p"] == p]
        if not R: continue
        S = [r for r in R if r["seg"]]
        L.append(f"| P{p} | {len(R)} | {q([r['th_err'] for r in R],50):.1f}° | {q([r['head_err'] for r in R],50):.0f} | {len(S)}/{len(R)} | {q([r['seg_th_err'] for r in S],50):.1f}° |")
    L.append("\n| tier (seen marks) | marks | tracker θ err p50 / p90 | head err p50 / p90 (px) |\n|---|---|---|---|")
    for t in ("band", "seg", "ray", "wedge", "pred"):
        R = [r for r in rows if r["group"] == "seen" and r["tier"] == t]
        if not R: continue
        L.append(f"| {t} | {len(R)} | {q([r['th_err'] for r in R],50):.1f}° / {q([r['th_err'] for r in R],90):.1f}° | {q([r['head_err'] for r in R],50):.0f} / {q([r['head_err'] for r in R],90):.0f} |")
    L.append("\n| head, seen marks | marks | radial err p50 (signed, + = beyond the mark) | |radial| p50 / p90 | lateral p50 / p90 | tracker len / truth len p50 |\n|---|---|---|---|---|---|")
    for kind in ("measured", "projected", "off", "all"):
        R = [r for r in rows if r["group"] == "seen" and (kind == "all" or r["head_kind"] == kind)]
        if not R: continue
        L.append(f"| {kind} | {len(R)} | {q([r['head_rad'] for r in R],50):+.0f} px | {q([abs(r['head_rad']) for r in R],50):.0f} / {q([abs(r['head_rad']) for r in R],90):.0f} | {q([r['head_lat'] for r in R],50):.0f} / {q([r['head_lat'] for r in R],90):.0f} | {q([r['len_trk']/r['len_truth'] for r in R],50):.2f} |")
    L.append("\n| P | head kind counts | radial p50 (signed) | lateral p50 |\n|---|---|---|---|")
    for p in range(1, 7):
        R = [r for r in rows if r["p"] == p]
        if not R: continue
        L.append(f"| P{p} | {dict(collections.Counter(r['head_kind'] for r in R))} | {q([r['head_rad'] for r in R],50):+.0f} | {q([r['head_lat'] for r in R],50):.0f} |")
    L.append(f"\nPose grip anchor vs Mark's grip mark: {q([r['grip_off'] for r in rows],50):.0f} px p50, {q([r['grip_off'] for r in rows],90):.0f} px p90 (the anchor is the hands' midpoint; the mark is where the shaft leaves the hands).")
    open(a.out_md, "w").write("\n".join(L) + "\n"); print("\n".join(L))

if __name__ == "__main__":
    main()
