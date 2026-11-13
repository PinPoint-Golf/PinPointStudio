"""Statistics for the plane-stack analysis from swings.jsonl (extract.py). Writes per_swing.csv and
prints the tables RESULTS.md is built from."""
import json, os, sys, csv, math, collections
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
rows = [json.loads(l) for l in open(f"{HERE}/swings.jsonl")]
SHORT = {"Mark-Liversedge": "Mark", "Nick-Henn": "Nick", "Jess-Ratcliffe": "Jess", "Chris-Marriott": "Chris",
         "Paddy-Marriott": "Paddy"}
for r in rows:
    r["golfer"] = SHORT[r["athlete"]]
    r["stvb"] = r.get("L_m_shoulderTiltVsBend", r.get("stvb_doc"))

errs = [r for r in rows if "error" in r]
excl = [r for r in rows if r.get("excluded")]
R = [r for r in rows if "error" not in r and not r.get("excluded")]
GOLFERS = ["Mark", "Nick", "Jess", "Paddy", "Chris"]
VALID4 = ["Mark", "Nick", "Jess", "Paddy"]

Q = [("takeaway_P2_P1", "Takeaway: swingPlaneCurve P2 - P1 (deg)"),
     ("climb_P3_P1", "Climb: swingPlaneCurve P3 - P1 (deg)"),
     ("spc_P3", "swingPlaneCurve at P3 (deg)"),
     ("armlift_a1", "Arm lift (a1) 3-D: lead-arm incl - shoulderPlaneAngle3d@P4 (deg)"),
     ("armlift_a1_joint", "Arm lift (a1j) 3-D: same, shoulder incl from the same joints/frames (deg)"),
     ("armlift_a2_plane", "Arm lift (a2) 3-D: lead arm vs shoulder-turn plane (deg)"),
     ("armlift_b_dtl", "Arm lift (b) DTL image: lead arm vs shoulder line (deg)")]


def ok(x):
    return x is not None and isinstance(x, (int, float)) and math.isfinite(x)


def pct(v, ps):
    return [float(np.percentile(v, p)) for p in ps]


def f1(x):
    return "-" if x is None else f"{x:.1f}"


def rankdata(a):
    a = np.asarray(a, float)
    order = a.argsort(kind="mergesort")
    ranks = np.empty(len(a))
    ranks[order] = np.arange(len(a))
    # average ties
    vals = a[order]
    i = 0
    while i < len(a):
        j = i
        while j + 1 < len(a) and vals[j + 1] == vals[i]:
            j += 1
        if j > i:
            ranks[order[i:j + 1]] = (i + j) / 2.0
        i = j + 1
    return ranks + 1


def spearman(x, y):
    if len(x) < 3:
        return None
    rx, ry = rankdata(x), rankdata(y)
    if rx.std() == 0 or ry.std() == 0:
        return None
    return float(np.corrcoef(rx, ry)[0, 1])


def within_session(rs, kx, ky, min_n=3, nperm=5000, seed=1):
    """Rank within each session (standardised ranks), pool, Pearson on pooled; permutation p within sessions."""
    groups = collections.defaultdict(list)
    for r in rs:
        if ok(r.get(kx)) and ok(r.get(ky)):
            groups[r["session"]].append((r[kx], r[ky]))
    xs, ys, ns = [], [], 0
    for s, pairs in groups.items():
        if len(pairs) < min_n:
            continue
        x = np.array([p[0] for p in pairs]); y = np.array([p[1] for p in pairs])
        rx, ry = rankdata(x), rankdata(y)
        if rx.std() == 0 or ry.std() == 0:
            continue
        xs.append((rx - rx.mean()) / rx.std()); ys.append((ry - ry.mean()) / ry.std()); ns += 1
    if not xs:
        return None
    X = np.concatenate(xs); Y = np.concatenate(ys)
    rho = float(np.mean(X * Y))
    rng = np.random.default_rng(seed)
    cnt = 0
    for _ in range(nperm):
        Yp = np.concatenate([rng.permutation(y) for y in ys])
        if abs(np.mean(X * Yp)) >= abs(rho) - 1e-12:
            cnt += 1
    return {"rho": rho, "n": int(len(X)), "sessions": ns, "p": (cnt + 1) / (nperm + 1)}


out = []
P = lambda *a: out.append(" ".join(str(x) for x in a))

P(f"docs read {len(rows)}; load errors {len(errs)}; excluded {len(excl)}; analysed {len(R)}")
P("handedness", collections.Counter(r.get("handedness") for r in R))
P("sessions", collections.Counter((r["golfer"]) for r in R))

# ---------- coverage ----------
P("\n## COVERAGE")
covkeys = ["spc_P1", "spc_P2", "spc_P3", "takeaway_P2_P1", "climb_P3_P1", "sk_valid", "spa3d_P4", "arm_incl3d_P4",
           "armlift_a1", "armlift_a2_plane", "armlift_b_dtl", "stvb", "L_m_trailElbowRise", "L_m_leadUpperArmToChest",
           "L_m_handPathLoop", "L_m_clubPathAtImpact", "L_m_spinAxis", "L_m_faceToPath", "L_m_shaftPlaneBackswing",
           "L_m_backswingPlaneAngle"]
P("| key | " + " | ".join(GOLFERS) + " |")
P("|---|" + "---|" * len(GOLFERS))
P("| swings | " + " | ".join(str(sum(1 for r in R if r["golfer"] == g)) for g in GOLFERS) + " |")
for k in covkeys:
    cells = []
    for g in GOLFERS:
        gs = [r for r in R if r["golfer"] == g]
        n = sum(1 for r in gs if (r.get(k) is True if k == "sk_valid" else ok(r.get(k))))
        cells.append(str(n))
    P(f"| {k} | " + " | ".join(cells) + " |")

# ---------- per golfer ----------
P("\n## PER GOLFER (n, median [Q1, Q3])")
P("| quantity | " + " | ".join(GOLFERS) + " |")
P("|---|" + "---|" * len(GOLFERS))
for k, lab in Q + [("spa3d_P4", "shoulderPlaneAngle3d at P4"), ("arm_incl3d_P4", "lead-arm inclination 3-D at P4"),
                   ("stvb", "shoulderTiltVsBend"), ("leadArmToTorso_P4", "leadArmToTorso at P4 (doc)"),
                   ("L_m_leadUpperArmToChest", "m_leadUpperArmToChest"), ("L_m_trailElbowRise", "m_trailElbowRise"),
                   ("L_m_handPathLoop", "m_handPathLoop"), ("L_m_clubPathAtImpact", "club path"),
                   ("L_m_spinAxis", "spin axis"), ("L_m_faceToPath", "face to path")]:
    cells = []
    for g in GOLFERS:
        v = [r[k] for r in R if r["golfer"] == g and ok(r.get(k))]
        if v:
            q1, m, q3 = pct(v, [25, 50, 75])
            cells.append(f"{len(v)}: {m:.1f} [{q1:.1f}, {q3:.1f}]")
        else:
            cells.append("-")
    P(f"| {lab} | " + " | ".join(cells) + " |")

# ---------- same day ----------
P("\n## SAME DAY 2026-10-20")
day = [r for r in R if r["date"] == "2026-10-20"]
P("| quantity | " + " | ".join(g for g in ["Jess", "Nick", "Mark"]) + " |")
P("|---|---|---|---|")
for k, lab in Q + [("spa3d_P4", "shoulderPlaneAngle3d at P4"), ("stvb", "shoulderTiltVsBend"),
                   ("L_m_clubPathAtImpact", "club path"), ("L_m_spinAxis", "spin axis"),
                   ("L_m_handPathLoop", "m_handPathLoop")]:
    cells = []
    for g in ["Jess", "Nick", "Mark"]:
        v = [r[k] for r in day if r["golfer"] == g and ok(r.get(k))]
        if v:
            q1, m, q3 = pct(v, [25, 50, 75]); cells.append(f"{len(v)}: {m:.1f} [{q1:.1f}, {q3:.1f}]")
        else:
            cells.append("-")
    P(f"| {lab} | " + " | ".join(cells) + " |")
P("sessions that day:", sorted(set((r["golfer"], r["session"]) for r in day)))
P("per-session medians that day:")
for s in sorted(set(r["session"] for r in day)):
    ss = [r for r in day if r["session"] == s]
    P(f"  {s}: n={len(ss)} " + " ".join(
        f"{k}={f1(float(np.median([r[k] for r in ss if ok(r.get(k))])) if any(ok(r.get(k)) for r in ss) else None)}"
        for k, _ in Q))

# ---------- agreement a vs b ----------
P("\n## AGREEMENT OF THE ARM-LIFT VERSIONS")
V4 = [r for r in R if r["golfer"] in VALID4]
pairs = [("armlift_a1", "armlift_b_dtl"), ("armlift_a2_plane", "armlift_b_dtl"), ("armlift_a1", "armlift_a2_plane"),
         ("armlift_a1", "armlift_a1_joint"), ("spa3d_P4", "sh_incl3d_joint_P4")]
for a, b in pairs:
    xs = [(r[a], r[b]) for r in V4 if ok(r.get(a)) and ok(r.get(b))]
    rho = spearman([x[0] for x in xs], [x[1] for x in xs])
    ws = within_session(V4, a, b)
    P(f"{a} vs {b}: pooled-4 Spearman {f1(rho) if rho is None else f'{rho:.2f}'} n={len(xs)}; within-session "
      + (f"rho={ws['rho']:.2f} n={ws['n']} sessions={ws['sessions']} p={ws['p']:.3f}" if ws else "-"))
    if a.startswith("armlift") and b.startswith("armlift"):
        d = [x[0] - x[1] for x in xs]
        if d:
            P(f"   difference {a}-{b}: median {np.median(d):.1f}, IQR [{np.percentile(d,25):.1f}, {np.percentile(d,75):.1f}]")
    for g in GOLFERS:
        xg = [(r[a], r[b]) for r in R if r["golfer"] == g and ok(r.get(a)) and ok(r.get(b))]
        rg = spearman([x[0] for x in xg], [x[1] for x in xg])
        P(f"   {g}: n={len(xg)} Spearman {'-' if rg is None else f'{rg:.2f}'}")

# ---------- within-session correlations ----------
P("\n## WITHIN-SESSION RANK CORRELATIONS (Mark, Nick, Jess, Paddy; sessions with >= 3 pairs)")
P("| x | y | rho | n | sessions | p (within-session permutation) |")
P("|---|---|---|---|---|---|")
XS = ["armlift_a1", "armlift_a2_plane", "armlift_b_dtl", "takeaway_P2_P1"]
YS = ["L_m_clubPathAtImpact", "L_m_spinAxis", "L_m_faceToPath", "L_m_handPathLoop", "stvb"]
for x in XS:
    for y in YS:
        ws = within_session(V4, x, y)
        if ws:
            P(f"| {x} | {y} | {ws['rho']:+.2f} | {ws['n']} | {ws['sessions']} | {ws['p']:.3f} |")
        else:
            P(f"| {x} | {y} | - | 0 | 0 | - |")
P("\nChris alone (one session):")
CH = [r for r in R if r["golfer"] == "Chris"]
for x in XS:
    for y in YS:
        ws = within_session(CH, x, y)
        if ws:
            P(f"  {x} vs {y}: rho {ws['rho']:+.2f} n={ws['n']} p={ws['p']:.3f}")

# ---------- norm seating ----------
def norm_table(title, filt):
  P("\n## " + title)
  P("| quantity | n (M/N/J/P) | p10 | p25 | median | p75 | p90 | golfer-weighted median [p10, p90] |")
  P("|---|---|---|---|---|---|---|---|")
  for k, lab in Q:
    v, wts, ng = [], [], []
    for g in VALID4:
        gv = [r[k] for r in R if r["golfer"] == g and ok(r.get(k)) and filt(r)]
        ng.append(len(gv))
        v += gv; wts += [1.0 / len(gv)] * len(gv) if gv else []
    if not v:
        continue
    p10, p25, p50, p75, p90 = pct(v, [10, 25, 50, 75, 90])
    order = np.argsort(v); vs = np.array(v)[order]; ws_ = np.array(wts)[order]
    cw = np.cumsum(ws_) / ws_.sum()
    wq = lambda q: float(vs[min(len(vs)-1, np.searchsorted(cw, q))])
    P(f"| {lab} | {len(v)} ({'/'.join(map(str, ng))}) | {p10:.1f} | {p25:.1f} | {p50:.1f} | {p75:.1f} | {p90:.1f} | "
      f"{wq(0.5):.1f} [{wq(0.1):.1f}, {wq(0.9):.1f}] |")
norm_table("POOLED DISTRIBUTION, irons 7-8 only (Mark 7i, Nick 7i, Jess 8i; Paddy's driver and Mark's gap wedge out)",
           lambda r: r.get("club") in ("7 IRON", "8 IRON"))
P("\n## POOLED DISTRIBUTION, four golfers (Chris out)")
P("| quantity | n (M/N/J/P) | p10 | p25 | median | p75 | p90 | golfer-weighted median [p10, p90] |")
P("|---|---|---|---|---|---|---|---|")
for k, lab in Q:
    v, wts, ng = [], [], []
    for g in VALID4:
        gv = [r[k] for r in R if r["golfer"] == g and ok(r.get(k))]
        ng.append(len(gv))
        v += gv; wts += [1.0 / len(gv)] * len(gv) if gv else []
    if not v:
        continue
    p10, p25, p50, p75, p90 = pct(v, [10, 25, 50, 75, 90])
    order = np.argsort(v); vs = np.array(v)[order]; ws_ = np.array(wts)[order]
    cw = np.cumsum(ws_) / ws_.sum()
    wq = lambda q: float(vs[np.searchsorted(cw, q)])
    P(f"| {lab} | {len(v)} ({'/'.join(map(str, ng))}) | {p10:.1f} | {p25:.1f} | {p50:.1f} | {p75:.1f} | {p90:.1f} | "
      f"{wq(0.5):.1f} [{wq(0.1):.1f}, {wq(0.9):.1f}] |")

# ---------- data quality ----------
P("\n## DATA QUALITY")
# ledger cross-check
d1 = [abs(r["climb_P3_P1"] - r["L_m_shaftPlaneBackswing"]) for r in R if ok(r.get("climb_P3_P1")) and ok(r.get("L_m_shaftPlaneBackswing"))]
d2 = [abs(r["spc_P3"] - r["L_m_backswingPlaneAngle"]) for r in R if ok(r.get("spc_P3")) and ok(r.get("L_m_backswingPlaneAngle"))]
d3 = [abs(r["stvb_doc"] - r["L_m_shoulderTiltVsBend"]) for r in R if ok(r.get("stvb_doc")) and ok(r.get("L_m_shoulderTiltVsBend"))]
for nm, d in [("climb vs m_shaftPlaneBackswing", d1), ("spc_P3 vs m_backswingPlaneAngle", d2), ("stvb doc vs ledger", d3)]:
    if d:
        P(f"{nm}: n={len(d)} median |diff| {np.median(d):.3f} max {max(d):.2f}; >0.5 deg: {sum(1 for x in d if x > 0.5)}")
big = [(r["session"], r["swing"], r["climb_P3_P1"], r["L_m_shaftPlaneBackswing"]) for r in R
       if ok(r.get("climb_P3_P1")) and ok(r.get("L_m_shaftPlaneBackswing")) and abs(r["climb_P3_P1"] - r["L_m_shaftPlaneBackswing"]) > 0.5]
for b in big[:15]:
    P("  mismatch", b)
big2 = [(r["session"], r["swing"], r["spc_P3"], r["L_m_backswingPlaneAngle"]) for r in R
        if ok(r.get("spc_P3")) and ok(r.get("L_m_backswingPlaneAngle")) and abs(r["spc_P3"] - r["L_m_backswingPlaneAngle"]) > 0.5]
for b in big2[:15]:
    P("  mismatch P3", b)
# ledger present without doc value and vice versa
P("ledger m_backswingPlaneAngle but no doc spc_P3:", sum(1 for r in R if ok(r.get("L_m_backswingPlaneAngle")) and not ok(r.get("spc_P3"))))
P("doc spc_P3 but no ledger value:", sum(1 for r in R if ok(r.get("spc_P3")) and not ok(r.get("L_m_backswingPlaneAngle"))))
P("routes spa3d:", collections.Counter((r["golfer"], r.get("spa3d_route")) for r in R if ok(r.get("spa3d_P4"))))
P("calibrated rigs:", collections.Counter((r["golfer"], r.get("sk_calibrated")) for r in R if r.get("sk_present")))
P("sk present/valid/dtl:", collections.Counter((r["golfer"], r.get("sk_present"), r.get("sk_valid"), r.get("sk_dtl")) for r in R))
for g in GOLFERS:
    gs = [r for r in R if r["golfer"] == g]
    top_f = [r.get("sk_top_frames") for r in gs if r.get("sk_top_frames") is not None]
    top_ok = [r.get("sk_top_ok") for r in gs if r.get("sk_top_ok") is not None]
    ht = collections.Counter(r.get("sk_top_handtier_min") for r in gs if r.get("sk_top_frames"))
    P(f"{g}: sk frames in P4 window median {np.median(top_f) if top_f else '-'}; usable median {np.median(top_ok) if top_ok else '-'}; "
      f"swings with 0 usable {sum(1 for x in top_ok if x == 0)}; lead-hand min tier {dict(ht)}")
    cl = [r.get("dtl_conf_leadSh") for r in gs if ok(r.get("dtl_conf_leadSh"))]
    ct = [r.get("dtl_conf_trailSh") for r in gs if ok(r.get("dtl_conf_trailSh"))]
    cw = [r.get("dtl_conf_leadWr") for r in gs if ok(r.get("dtl_conf_leadWr"))]
    if cl:
        P(f"   DTL conf at top (median of swing medians; share < 0.30): lead sh {np.median(cl):.2f} ({sum(1 for x in cl if x < .3)}), "
          f"trail sh {np.median(ct):.2f} ({sum(1 for x in ct if x < .3)}), lead wrist {np.median(cw):.2f} ({sum(1 for x in cw if x < .3)}); "
          f"p10 lead wrist {np.percentile(cw,10):.2f}, p10 trail sh {np.percentile(ct,10):.2f}; DTL top usable 0 frames: "
          f"{sum(1 for r in gs if r.get('dtl_top_frames') and r.get('dtl_top_ok') == 0)}")
    v = [r["spa3d_P4"] for r in gs if ok(r.get("spa3d_P4"))]
    vj = [r["sh_incl3d_joint_P4"] for r in gs if ok(r.get("sh_incl3d_joint_P4"))]
    vf = [r["shoulderPlaneAngle_P4"] for r in gs if ok(r.get("shoulderPlaneAngle_P4"))]
    b = [r["spineForwardBend_P1"] for r in gs if ok(r.get("spineForwardBend_P1"))]
    if v:
        P(f"   shoulder tilt @P4 3-D series median {np.median(v):.1f} (range {min(v):.1f}..{max(v):.1f}); joints {np.median(vj) if vj else float('nan'):.1f}; "
          f"2-D shoulderPlaneAngle@P4 {np.median(vf) if vf else float('nan'):.1f}; forward bend @P1 {np.median(b) if b else float('nan'):.1f}")
    lt = [r["leadArmToTorso_P4"] for r in gs if ok(r.get("leadArmToTorso_P4"))]
    if lt:
        P(f"   leadArmToTorso@P4 median {np.median(lt):.1f} [{np.percentile(lt,25):.1f}, {np.percentile(lt,75):.1f}] n={len(lt)}")
# sessions per golfer with spc
P("per-session coverage:")
for s in sorted(set(r["session"] for r in R)):
    ss = [r for r in R if r["session"] == s]
    P(f"  {s}: swings {len(ss)}, spc_P2 {sum(ok(r.get('takeaway_P2_P1')) for r in ss)}, a1 {sum(ok(r.get('armlift_a1')) for r in ss)}, "
      f"a2 {sum(ok(r.get('armlift_a2_plane')) for r in ss)}, b {sum(ok(r.get('armlift_b_dtl')) for r in ss)}, "
      f"path {sum(ok(r.get('L_m_clubPathAtImpact')) for r in ss)}, spinAx {sum(ok(r.get('L_m_spinAxis')) for r in ss)}, "
      f"loop {sum(ok(r.get('L_m_handPathLoop')) for r in ss)}, stvb {sum(ok(r.get('stvb')) for r in ss)}")
P("load errors:", [(r["path"], r["error"]) for r in errs])

open(f"{HERE}/analysis_out.txt", "w").write("\n".join(out))
print("\n".join(out))

# ---------- CSV ----------
cols = ["golfer", "session", "date", "swing", "shotId", "handedness", "club", "excluded",
        "spc_P1", "spc_P2", "spc_P3", "spc_P4", "takeaway_P2_P1", "climb_P3_P1",
        "L_m_shaftPlaneBackswing", "L_m_backswingPlaneAngle",
        "spa3d_P4", "spa3d_route", "sh_incl3d_joint_P4", "arm_incl3d_P4", "armlift_a1", "armlift_a1_joint",
        "armlift_a2_plane", "armlift_b_dtl", "sk_valid", "sk_calibrated", "sk_top_frames", "sk_top_ok",
        "sk_top_handtier_min", "dtl_top_frames", "dtl_top_ok", "dtl_conf_leadSh", "dtl_conf_trailSh", "dtl_conf_leadWr",
        "stvb", "stvb_doc", "L_m_shoulderTiltVsBend", "spineForwardBend_P1", "shoulderPlaneAngle_P4",
        "leadArmToTorso_P4", "L_m_trailElbowRise", "L_m_leadUpperArmToChest", "L_m_handPathLoop",
        "L_m_clubPathAtImpact", "L_m_spinAxis", "L_m_faceToPath", "lm_kind", "path"]
with open(f"{HERE}/per_swing.csv", "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(cols)
    for r in rows:
        r.setdefault("golfer", SHORT.get(r["athlete"]))
        w.writerow([("" if r.get(c) is None else (f"{r[c]:.3f}" if isinstance(r.get(c), float) else r.get(c))) for c in cols])
