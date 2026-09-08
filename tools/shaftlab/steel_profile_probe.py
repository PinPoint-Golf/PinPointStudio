#!/usr/bin/env python3
"""
steel_profile_probe — Phase 0 of the markerless club tracker
(docs/design/markerless_club_tracker_design.md §6).

Question: how bright is the BARE STEEL of the shaft, per swing phase and per
background regime, compared with the retro bands — measured with the tracker's
own ridge reduction (E2, shaft_tracker_math.cpp ridgeSweep) so the numbers mean
what the tracker sees, not what a human sees.

For every 150 fps face-on swing with a recorded club track, every frame inside
the swing span is sampled along the tracked shaft direction (±6°, best E2 score
wins) from the grip anchor outward, 1 px radial step, with E2's lateral
reduction: background = median of 4 samples at ±9/±12 px; on-ridge = max of 5
at 0/±1/±2 px when background ≤ 200 (bright-line regime) else min of 5 (dark
line over a blown background); evidence e = ±(on − bg) − 12 clipped to
[−30, 90]. The longest evidenced run (e > 8, holes ≤ 4 px) is "the shaft".

On taped clubs the run is split into band plateaus (≥ 0.8 × the run's p95
on-ridge level, separated by dips to ≤ 0.6 ×) and the bare-steel gaps between them; the gap statistics are the within-frame,
lighting-fair measure of bare steel. On unmarked clubs the whole run is steel.

Outputs one CSV row per frame, and (--summary) markdown tables per
condition × phase × background regime.

Usage:
  steel_profile_probe.py --root /mnt/swingdata/Mark-Liversedge --out probe.csv
  steel_profile_probe.py --csv probe.csv --summary probe_summary.md
"""
import argparse, bisect, collections, csv, glob, json, os, sys
import numpy as np
import cv2

# ── conditions ───────────────────────────────────────────────────────────────
STEEL_CLUBS = {"7 IRON", "9 IRON", "GAP WEDGE", "GW", "PITCHING WEDGE", "SAND WEDGE",
               "LOB WEDGE", "8 IRON", "6 IRON", "5 IRON", "4 IRON"}
TAPED_ON = "2026-07-04"          # clubs.json: the 7 IRON was taped on this date
TAPED_CLUB = "7 IRON"

def condition(date, club, bands):
    steel = club in STEEL_CLUBS
    taped = bool(bands) or (club == TAPED_CLUB and date >= TAPED_ON)
    if taped: return "taped-steel"
    return "untaped-steel" if steel else "untaped-graphite"

# swing_analysis.h Phase enum ids → P-ladder
P = {"P1": 0, "P2": 12, "P3": 8, "P4": 2, "P5": 13, "P6": 9, "P7": 5, "P8": 14, "P9": 11, "P10": 7}

def phase_bins(phases, impact_us):
    t = {p["phase"]: p["t_us"] for p in phases}
    p4 = t.get(P["P4"]); p2 = t.get(P["P2"]); p6 = t.get(P["P6"])
    p7 = t.get(P["P7"], impact_us); p9 = t.get(P["P9"]); p10 = t.get(P["P10"])
    p1 = t.get(P["P1"])
    if p7 is None: return None
    if p4 is None: p4 = p7 - 300_000
    if p2 is None: p2 = p4 - 350_000
    if p6 is None: p6 = p7 - 40_000
    if p9 is None: p9 = p7 + 250_000
    if p10 is None: p10 = p9 + 400_000
    if p1 is None: p1 = p2 - 300_000
    edges = [("address", p1 - 150_000, p2), ("backswing", p2, p4 - 50_000),
             ("top", p4 - 50_000, p4 + 50_000), ("downswing", p4 + 50_000, p6),
             ("delivery", p6, p7), ("through", p7, p9), ("finish", p9, p10 + 150_000)]
    return edges

def phase_of(edges, t):
    for name, a, b in edges:
        if a <= t < b: return name
    return None

# ── E2-style profile ─────────────────────────────────────────────────────────
OFFS = np.array([-12, -9, -6, -2, -1, 0, 1, 2, 6, 9, 12], dtype=np.float64)
I_BG = [0, 1, 9, 10]; I_ON = [3, 4, 5, 6, 7]; I_WIDE = [2, 8]

def sample(img32, gx, gy, theta, r):
    """nearest-neighbour clamp gather, shape (len(OFFS), len(r))"""
    H, W = img32.shape
    ux, uy = np.cos(theta), np.sin(theta)
    nx, ny = -uy, ux
    px = gx + ux * r[None, :] + nx * OFFS[:, None]
    py = gy + uy * r[None, :] + ny * OFFS[:, None]
    xi = np.clip(px.astype(np.int32), 0, W - 1)
    yi = np.clip(py.astype(np.int32), 0, H - 1)
    return img32[yi, xi]

def profile(img32, gx, gy, theta, r):
    S = sample(img32, gx, gy, theta, r)
    bg = np.median(S[I_BG], axis=0)
    on5 = S[I_ON]
    bright = bg <= 200.0
    on = np.where(bright, on5.max(axis=0), on5.min(axis=0))
    e = np.where(bright, on - bg - 12.0, bg - on - 12.0)
    e = np.clip(e, -30.0, 90.0)
    wide = S[I_WIDE].mean(axis=0)
    return e, on, bg, wide, bright

def e2_score(e, j0):
    cum = np.cumsum(e)
    norm = cum / np.sqrt(np.arange(len(e)) + 8.0)
    norm[:j0] = -np.inf
    j = int(np.argmax(norm))
    return float(norm[j]), j

def longest_run(mask, max_hole=4, min_start=4):
    """longest stretch of True allowing holes ≤ max_hole; returns (a, b) inclusive or None"""
    best = None; a = None; last = None
    for i, m in enumerate(mask):
        if i < min_start: continue
        if m:
            if a is None: a = i
            last = i
        elif a is not None and i - last > max_hole:
            if best is None or last - a > best[1] - best[0]: best = (a, last)
            a = None
    if a is not None and (best is None or last - a > best[1] - best[0]): best = (a, last)
    return best

def band_gaps(on, bg, bright, a, b):
    """Band plateaus inside the run [a,b]: bright-regime samples at ≥ 0.8 × the run's
    p95 on-ridge level (bands in the dark arc read ~220, below saturation), split by
    dips to ≤ 0.6 × that level. Returns (plateau runs, gap spans between them)."""
    seg = slice(a, b + 1)
    p95 = float(np.percentile(on[seg][bright[seg]], 95)) if bright[seg].any() else 0.0
    if p95 < 120.0: return [], []
    hi = (on >= 0.8 * p95) & bright
    lo = on <= 0.6 * p95
    runs = []; s = None
    for i in range(a, b + 1):
        if hi[i]:
            if s is None: s = i
        elif s is not None:
            if i - s >= 3: runs.append((s, i - 1))
            s = None
    if s is not None and b - s + 1 >= 3: runs.append((s, b))
    # keep only plateaus separated by a real dip (≥ 2 px at ≤ 0.6·p95) — specular
    # ripple on a bare shaft rarely dips that far
    kept = []
    for (s0, e0), (s1, e1) in zip(runs, runs[1:]):
        if lo[e0 + 1:s1].sum() >= 2 and s1 - e0 <= 90:
            if not kept or kept[-1] != (s0, e0): kept.append((s0, e0))
            kept.append((s1, e1))
    gaps = []
    for (s0, e0), (s1, e1) in zip(kept, kept[1:]):
        g0, g1 = e0 + 3, s1 - 3
        if g1 - g0 >= 2 and s1 - e0 <= 90: gaps.append((g0, g1))
    return kept, gaps

# ── per swing ────────────────────────────────────────────────────────────────
def load_swing(d):
    if not os.path.exists(os.path.join(d, "swing.json")): return None
    j = json.load(open(os.path.join(d, "swing.json")))
    sts = [s for s in j.get("streams", []) if "Face" in (s.get("alias") or s.get("file") or "")]
    if not sts: return None
    st = sts[0]
    ts = st["frames"]["t_us"]
    if len(ts) < 100: return None
    dts = np.diff(ts); mdt = float(np.median(dts))
    if mdt > 8000: return None                     # not 150 fps
    an = j.get("analysis") or {}
    club = an.get("club") or {}
    samples = club.get("samples") or []
    if not samples: return None
    cap = j.get("capture") or {}
    crec = cap.get("club") or {}
    impact = cap.get("impactUs")
    if impact is None:
        for m in an.get("metrics", []):
            pass
        ph = {p["phase"]: p["t_us"] for p in an.get("phases", [])}
        impact = ph.get(P["P7"])
    if impact is None: return None
    return dict(j=j, st=st, ts=ts, samples=samples, phases=an.get("phases", []),
                impact=impact, clubname=crec.get("name"), bands=crec.get("bandCentersMm") or [],
                W=club.get("frameWidth") or st["encoded"]["width"],
                H=club.get("frameHeight") or st["encoded"]["height"],
                exposure=(st.get("capture") or {}).get("exposureUs"))

def tier_of(smp):
    c = round(float(smp.get("conf", 0)), 2); f = int(smp.get("flags", 0))
    if c >= 0.7: return "band"
    if c == 0.55: return "ray"
    if f & 0x08: return "wedge"
    if c >= 0.4: return "other"
    return "pred"

def probe_swing(d, date, corpus_club, writer):
    sw = load_swing(d)
    if sw is None: return 0
    club = sw["clubname"] or corpus_club or "?"
    cond = condition(date, club, sw["bands"])
    edges = phase_bins(sw["phases"], sw["impact"])
    if edges is None: return 0
    ts = sw["ts"]; W, H = sw["W"], sw["H"]
    by_t = {s["t_us"]: s for s in sw["samples"]}
    rmax = 0.62 * H
    r = np.arange(4.0, rmax, 1.0); j0 = 90
    lo_t, hi_t = edges[0][1], edges[-1][2]
    i0 = max(0, bisect.bisect_left(ts, lo_t)); i1 = min(len(ts) - 1, bisect.bisect_right(ts, hi_t))
    cap = cv2.VideoCapture(os.path.join(d, sw["st"]["file"]))
    n = 0; idx = 0
    while True:
        ok, frame = cap.read()
        if not ok: break
        if idx < i0: idx += 1; continue
        if idx > i1: break
        t = ts[idx]; smp = by_t.get(t)
        idx += 1
        if smp is None: continue
        ph = phase_of(edges, t)
        if ph is None: continue
        g = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
        gx, gy = smp["grip"][0] * W, smp["grip"][1] * H
        th0 = float(smp["theta"])
        best = None
        for dd in (-6, -4, -2, 0, 2, 4, 6):
            th = th0 + np.deg2rad(dd)
            e, on, bg, wide, bright = profile(g, gx, gy, th, r)
            sc, jend = e2_score(e, j0)
            if best is None or sc > best[0]: best = (sc, dd, e, on, bg, wide, bright, jend)
        sc, dd, e, on, bg, wide, bright, jend = best
        run = longest_run(e > 8.0)
        row = dict(session=os.path.basename(os.path.dirname(d)), swing=os.path.basename(d), date=date, club=club, cond=cond, frame=idx - 1,
                   t_ms=round((t - sw["impact"]) / 1000.0, 1), phase=ph, tier=tier_of(smp),
                   theta_deg=round(np.degrees(th0) % 360, 1), dtheta=dd, e2_score=round(sc, 1),
                   e2_rend=int(r[jend]), exposure_us=sw["exposure"])
        if run is None:
            row.update(run_a=-1, run_b=-1, run_len=0, support=0, e_med=0, e_p90=0, on_med=0,
                       clip_frac=0, bg_med=0, regime="none", frac_dark=0, frac_mid=0, frac_blown=0,
                       n_bands=0, gap_e_med="", gap_e_min="", gap_on_med="", gap_bg_med="")
        else:
            a, b = run; seg = slice(a, b + 1)
            es, ons, bgs, brs = e[seg], on[seg], bg[seg], bright[seg]
            bgm = float(np.median(bgs))
            regime = "dark" if bgm < 40 else ("blown" if bgm > 200 else "mid")
            row.update(run_a=int(r[a]), run_b=int(r[b]), run_len=int(r[b] - r[a] + 1),
                       support=round(float((es > 8).mean()), 3), e_med=round(float(np.median(es)), 1),
                       e_p90=round(float(np.percentile(es, 90)), 1), on_med=round(float(np.median(ons)), 1),
                       clip_frac=round(float(((ons >= 250) & brs).mean()), 3), bg_med=round(bgm, 1),
                       regime=regime, frac_dark=round(float((bgs < 40).mean()), 3),
                       frac_mid=round(float(((bgs >= 40) & (bgs <= 200)).mean()), 3),
                       frac_blown=round(float((bgs > 200).mean()), 3))
            runs, gaps = band_gaps(on, bg, bright, a, b)
            if gaps:
                ge = np.concatenate([e[g0:g1 + 1] for g0, g1 in gaps])
                go = np.concatenate([on[g0:g1 + 1] for g0, g1 in gaps])
                gb = np.concatenate([bg[g0:g1 + 1] for g0, g1 in gaps])
                row.update(n_bands=len(runs), gap_e_med=round(float(np.median(ge)), 1),
                           gap_e_min=round(float(min(np.median(e[g0:g1 + 1]) for g0, g1 in gaps)), 1),
                           gap_on_med=round(float(np.median(go)), 1), gap_bg_med=round(float(np.median(gb)), 1))
            else:
                row.update(n_bands=len(runs),
                           gap_e_med="", gap_e_min="", gap_on_med="", gap_bg_med="")
        writer.writerow(row); n += 1
    cap.release()
    return n

FIELDS = ["session", "swing", "date", "club", "cond", "frame", "t_ms", "phase", "tier", "theta_deg", "dtheta",
          "e2_score", "e2_rend", "exposure_us", "run_a", "run_b", "run_len", "support", "e_med",
          "e_p90", "on_med", "clip_frac", "bg_med", "regime", "frac_dark", "frac_mid", "frac_blown",
          "n_bands", "gap_e_med", "gap_e_min", "gap_on_med", "gap_bg_med"]

def run_probe(root, out):
    corpus = json.load(open(os.path.join(root, "corpus.json")))
    club_of = {s["path"]: (s.get("conditions") or {}).get("club") for s in corpus["swings"]}
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS); w.writeheader()
        total = 0; swings = 0
        for d in sorted(glob.glob(os.path.join(root, "2026-*", "swing_*"))):
            date = os.path.basename(os.path.dirname(d))[:10]
            n = probe_swing(d, date, club_of.get(d), w)
            if n:
                swings += 1; total += n
                print(f"{date} {os.path.basename(d)} {club_of.get(d)} frames={n}", flush=True)
        print(f"done: {swings} swings, {total} frames → {out}")

# ── summary ──────────────────────────────────────────────────────────────────
PHASES = ["address", "backswing", "top", "downswing", "delivery", "through", "finish"]

def q(v, p): return float(np.percentile(v, p)) if len(v) else float("nan")

# Condition is decided per swing from EVIDENCE, not from the recorded club record
# (pre-0.1.10011 the app stamped the athlete's single record whatever club was
# hit) and not from corpus.json `conditions.club` (the July "DRIVER" sessions
# show an iron head at address and a banded shaft at the top):
#   taped   = the recorded track carries ≥ 20 BAND-tier frames (E1 locked on the
#             band ratios — impossible without tape), OR the session is in
#             TAPED_SESSIONS (07-04 = the tape_20260704 pilot per
#             docs/research/club_detection_from_video.md; 07-08 = banded shaft +
#             iron head on inspection, but the record had no bands so E1 never ran).
#   material: every taped swing is the lab 7-iron (steel). Untaped swings take
#             the corpus label — irons/wedges = steel; wood/hybrid/driver labels
#             are reported as "nonsteel-label" because those labels are unverified.
# The probe's own plateau count is NOT used: specular ripple on bare steel in the
# dark arc produces dash-like highlights (06-11, 07-03 on inspection).
TAPED_SESSIONS = {"2026-07-04", "2026-07-08"}

def assign_conditions(rows):
    bandn = collections.Counter(r["swing_key"] for r in rows if r["tier"] == "band")
    for r in rows:
        t = bandn[r["swing_key"]] >= 20 or r["date"] in TAPED_SESSIONS
        if t: mat = "steel"
        else: mat = "steel" if r["club"] in STEEL_CLUBS else "nonsteel-label"
        r["cond"] = ("taped-" if t else "untaped-") + mat
        if r["date"] == "2026-06-11": r["cond"] += "-720w"

def summarise(csv_path, out_md):
    rows = list(csv.DictReader(open(csv_path)))
    for r in rows: r["swing_key"] = r["session"] + "/" + r["swing"]
    assign_conditions(rows)
    for r in rows:
        for k in ("run_len", "support", "e_med", "e_p90", "on_med", "clip_frac", "bg_med", "e2_score"):
            r[k] = float(r[k])
        r["gap_e_med"] = float(r["gap_e_med"]) if r["gap_e_med"] != "" else None
        r["gap_on_med"] = float(r["gap_on_med"]) if r["gap_on_med"] != "" else None
    L = []
    inv = collections.Counter((r["cond"], r["date"], r["club"]) for r in rows)
    sw = collections.defaultdict(set)
    for r in rows: sw[(r["cond"], r["date"], r["club"])].add(r["swing_key"])
    L.append("### Population\n\n| condition | session | club | swings | frames |\n|---|---|---|---|---|")
    for k in sorted(inv): L.append(f"| {k[0]} | {k[1]} | {k[2]} | {len(sw[k])} | {inv[k]} |")
    L.append("\nCondition is decided per swing from evidence: taped = ≥ 20 BAND-tier frames in the recorded track (E1 locked on the band ratios), or a session documented/inspected as taped (07-04 tape pilot, 07-08). Neither the recorded club record (pre-0.1.10011 it was the athlete's single record) nor corpus.json `conditions.club` (the July \"DRIVER\" sessions show an iron head and a banded shaft) is trusted. 06-11 is a 720-px-wide capture and is kept separate.")
    conds = sorted({r["cond"] for r in rows}, key=lambda c: (not c.startswith("taped"), c))

    L.append("\n### A. Shaft run per phase — all span frames, best θ within ±6° of the tracked shaft\n")
    L.append("Detectable = run ≥ 90 px with support ≥ 0.40 (the tracker's own RAY gates). e is the E2 evidence (grey levels above local background, −12 bias, clipped at 90); on = on-ridge grey level.\n")
    L.append("| condition | phase | frames | detectable | run len p50 (px) | support p50 | e p50 | e p10 | on p50 | clipped p50 | regime dark/mid/blown |\n|---|---|---|---|---|---|---|---|---|---|---|")
    for c in conds:
        for ph in PHASES:
            R = [r for r in rows if r["cond"] == c and r["phase"] == ph]
            if not R: continue
            det = np.mean([(r["run_len"] >= 90 and r["support"] >= 0.4) for r in R])
            reg = collections.Counter(r["regime"] for r in R)
            n = len(R)
            L.append(f"| {c} | {ph} | {n} | {det*100:.0f}% | {q([r['run_len'] for r in R],50):.0f} | "
                     f"{q([r['support'] for r in R],50):.2f} | {q([r['e_med'] for r in R],50):.0f} | {q([r['e_med'] for r in R],10):.0f} | "
                     f"{q([r['on_med'] for r in R],50):.0f} | {q([r['clip_frac'] for r in R],50)*100:.0f}% | "
                     f"{reg['dark']/n*100:.0f}/{reg['mid']/n*100:.0f}/{reg['blown']/n*100:.0f} |")

    L.append("\n### B. Bare shaft between the bands on the taped clubs (within-frame, lighting-fair)\n")
    L.append("Frames where ≥ 2 band plateaus were found on the run; the gaps between them are bare steel under the same light as the bands.\n")
    L.append("| phase | frames with gaps | gap steel e p50 | e p10 | gap on p50 | band on p50 | steel/band on ratio | regime dark/mid/blown |\n|---|---|---|---|---|---|---|---|")
    for ph in PHASES:
        R = [r for r in rows if r["cond"].startswith("taped") and r["phase"] == ph and r["gap_e_med"] is not None]
        if not R: continue
        n = len(R); reg = collections.Counter(r["regime"] for r in R)
        ge = [r["gap_e_med"] for r in R]; go = [r["gap_on_med"] for r in R]
        bo = [r["on_med"] for r in R]
        L.append(f"| {ph} | {n} | {q(ge,50):.0f} | {q(ge,10):.0f} | {q(go,50):.0f} | {q(bo,50):.0f} | "
                 f"{q(go,50)/max(q(bo,50),1):.2f} | {reg['dark']/n*100:.0f}/{reg['mid']/n*100:.0f}/{reg['blown']/n*100:.0f} |")

    L.append("\n### C. Bare steel by background regime — untaped steel run vs taped-club gaps\n")
    L.append("| condition | regime | frames | e p50 | e p10 | on p50 | detectable |\n|---|---|---|---|---|---|---|")
    for c in conds:
        for reg in ("dark", "mid", "blown"):
            R = [r for r in rows if r["cond"] == c and r["regime"] == reg]
            if not R: continue
            det = np.mean([(r["run_len"] >= 90 and r["support"] >= 0.4) for r in R])
            L.append(f"| {c} | {reg} | {len(R)} | {q([r['e_med'] for r in R],50):.0f} | {q([r['e_med'] for r in R],10):.0f} | "
                     f"{q([r['on_med'] for r in R],50):.0f} | {det*100:.0f}% |")
    for reg in ("dark", "mid", "blown"):
        R = [r for r in rows if r["cond"].startswith("taped") and r["regime"] == reg and r["gap_e_med"] is not None]
        if not R: continue
        L.append(f"| taped GAPS | {reg} | {len(R)} | {q([r['gap_e_med'] for r in R],50):.0f} | {q([r['gap_e_med'] for r in R],10):.0f} | "
                 f"{q([r['gap_on_med'] for r in R],50):.0f} | — |")

    L.append("\n### D. Recorded tracker tier per phase (context for selection bias)\n")
    L.append("| condition | phase | band | ray | wedge | other | pred |\n|---|---|---|---|---|---|---|")
    for c in conds:
        for ph in PHASES:
            R = [r for r in rows if r["cond"] == c and r["phase"] == ph]
            if not R: continue
            t = collections.Counter(r["tier"] for r in R); n = len(R)
            L.append(f"| {c} | {ph} | {t['band']/n*100:.0f}% | {t['ray']/n*100:.0f}% | {t['wedge']/n*100:.0f}% | {t['other']/n*100:.0f}% | {t['pred']/n*100:.0f}% |")
    open(out_md, "w").write("\n".join(L) + "\n")
    print("\n".join(L))

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--root"); ap.add_argument("--out")
    ap.add_argument("--csv"); ap.add_argument("--summary")
    a = ap.parse_args()
    if a.root and a.out: run_probe(a.root, a.out)
    if a.csv and a.summary: summarise(a.csv, a.summary)
    if not ((a.root and a.out) or (a.csv and a.summary)): ap.print_help(); sys.exit(1)
