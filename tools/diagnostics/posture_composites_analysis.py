#!/opt/homebrew/bin/python3
"""Posture composites — the measurement stage (posture_metrics_design.md §9, V1–V5).

Reads the CSVs `measure_dump` wrote on the cabin PC from re-analysed COPIES of the libraries
(run root /mnt/swingdata/scratch/posture-20261021/, see the write-up for the commands) and prints
the tables the write-up quotes. numpy + scipy only (no pandas on this Mac).

    posture_composites_analysis.py <dump dir> [section ...]     sections: pop v1 v2 v3 v4 v5

The dump dir holds mark.csv, nick.csv, jess.csv (the three athlete libraries) and corpus.csv (the
curated corpus copy of the same swings, its own pose run) — V5 compares the two.
"""
import csv, math, sys, collections
import numpy as np
from scipy import stats

GOLFERS = ("mark", "nick", "jess")

# The composites as authored in core.json (kept here so the analysis can recompute a composite
# from its parts under a candidate norm/ρ without rebuilding the tool).
IMPACT = [("trunk", ["m_spineBendChangeImpact", "m_trunkRiseImpact"], [-1, 1], 0.9),
          ("pelvisDepth", ["m_pelvisThrustImpact"], [1], 0.8),
          ("head", ["m_headLiftImpact"], [1], 0.6),
          ("pelvisHeight", ["m_pelvisLiftImpact"], [1], 0.4)]
TOP = [("trunk", ["m_spineBendChangeTop"], [-1], 0.9),
       ("trailKnee", ["m_trailKneeFlex"], [-1], 0.8),
       ("pelvisHeight", ["m_pelvisSinkTop"], [1], 0.6),   # hip-joint lift dropped: migrates 18-37 % low at P4
       ("head", ["m_headLiftBack"], [1], 0.6),
       ("pelvisDepth", ["m_pelvisThrustBack"], [1], 0.5)]
SETUP = [("trunkHinge", ["m_spineBendAtAddress"], 0.9, True),
         ("kneeFlex", ["m_kneeFlexAddress"], 0.7, False),
         ("balance", ["m_balanceHeelToeAddress"], 0.6, False),
         ("armHang", ["m_armHangAddress"], 0.6, False),
         ("upperBack", ["m_thoracicCurve"], 0.5, False),
         ("lowBack", ["m_lumbarCurve"], 0.5, False),
         ("sideTilt", ["m_axisTiltAddress"], 0.4, False),
         ("head", ["m_headPitchAddress"], 0.3, False)]

PARTS = sorted({m for p in IMPACT + TOP for m in p[1]} | {m for p in SETUP for m in p[1]})
FAMILY = ["m_postureHeldImpact", "m_postureHeldTop", "m_setupPosture"]


def fnum(s):
    try:
        v = float(s)
        return v if math.isfinite(v) else None
    except (TypeError, ValueError):
        return None


def load(path, pop):
    """{(pop, session, swing): {measure: row}} for plain rows, dropping capture-withheld shots."""
    shots = collections.OrderedDict()
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["captureWithheld"] == "1":
                continue
            key = (pop, r["session"], r["swing"])
            d = shots.setdefault(key, {"_club": r["club"], "_ctx": r["context"],
                                       "_fo": r["faceOn"] == "1", "_dtl": r["dtl"] == "1"})
            d[r["measure"]] = r
    return shots


def val(shot, m):
    r = shot.get(m)
    return fnum(r["value"]) if r else None


def zed(shot, m):
    r = shot.get(m)
    return fnum(r["z"]) if r else None


def spearman(x, y):
    if len(x) < 4:
        return float("nan"), len(x)
    return stats.spearmanr(x, y).statistic, len(x)


def coherent(terms, rho):
    """terms: [(w, s, z)] → Z (posture_metrics_design.md §4.3)."""
    w = np.array([t[0] for t in terms]); sz = np.array([t[1] * t[2] for t in terms])
    den2 = (w * w).sum() + rho * (w.sum() ** 2 - (w * w).sum())
    return float((w * sz).sum() / math.sqrt(den2))


def distance(terms, rho):
    """terms: [(w, z)] → Z_eq (§5.2), clamped like the engine."""
    w = np.array([t[0] for t in terms]); z = np.array([t[1] for t in terms])
    q = (w * z * z).sum(); e = w.sum()
    v = 2 * ((w * w).sum() + rho * rho * (w.sum() ** 2 - (w * w).sum()))
    g, h = v / (2 * e), 2 * e * e / v
    return float(np.clip(-stats.norm.ppf(stats.chi2.sf(q / g, h)), -8.3, 8.3))


def read_parts(shot, spec, zfun, faceon_only=False):
    """Walk each part's ladder like the engine; returns [(name, measure, w, s, z)] for parts read."""
    out = []
    for name, ladder, signs, w in spec:
        for m, s in zip(ladder, signs):
            if faceon_only and m in DTL_ONLY:
                continue
            z = zfun(shot, m)
            if z is not None:
                out.append((name, m, w, s, z)); break
    return out


DTL_ONLY = {"m_spineBendChangeImpact", "m_pelvisThrustImpact", "m_spineBendChangeTop",
            "m_trailKneeFlex", "m_pelvisThrustBack", "m_spineBendAtAddress", "m_kneeFlexAddress",
            "m_balanceHeelToeAddress", "m_armHangAddress", "m_headPitchAddress"}


def verdict(z):
    if z is None:
        return "unread"
    return "high" if z > 2 else ("low" if z < -2 else "clean")


# ── sections ────────────────────────────────────────────────────────────────────────────────
def sec_pop(data):
    print("## Population")
    for pop, shots in data.items():
        two = sum(1 for s in shots.values() if s["_fo"] and s["_dtl"])
        dtl = sum(1 for s in shots.values() if s["_dtl"])
        fo = sum(1 for s in shots.values() if s["_fo"])
        print(f"{pop:7s} shots {len(shots):4d}  faceOn {fo:4d}  dtl {dtl:4d}  both {two:4d}")
    print()
    print("Part measures: n with a value / n with a z (library populations)")
    for m in PARTS:
        row = []
        for pop in GOLFERS:
            sh = data[pop].values()
            nv = sum(1 for s in sh if val(s, m) is not None)
            nz = sum(1 for s in sh if zed(s, m) is not None)
            row.append(f"{pop} {nv:3d}/{nz:3d}")
        print(f"  {m:26s} " + "  ".join(row))
    print()


def sec_v1(data):
    print("## V1 — face-on trunkRise(P7) against DTL forward-bend change P1→P7 (two-camera swings)")
    allx, ally = [], []
    for pop in GOLFERS:
        xs, ys = [], []
        for s in data[pop].values():
            a, b = val(s, "m_trunkRiseImpact"), val(s, "m_spineBendChangeImpact")
            if a is not None and b is not None:
                xs.append(a); ys.append(-b)
        rho, n = spearman(xs, ys)
        slope = (np.polyfit(ys, xs, 1)[0] if n >= 3 else float("nan"))
        print(f"{pop:5s} n {n:3d}  Spearman {rho:+.2f}  trunkRise % per ° lost {slope:+.2f}"
              + (f"  medians: rise {np.median(xs):+.1f} %, bend lost {np.median(ys):+.1f}°"
                 f"  SD: rise {np.std(xs, ddof=1):.1f} %, bend lost {np.std(ys, ddof=1):.1f}°" if n > 2 else ""))
        allx += xs; ally += ys
    rho, n = spearman(allx, ally)
    print(f"pooled n {n:3d}  Spearman {rho:+.2f}  (between golfers as well as within)")
    print()


def sec_dist(data):
    print("## Part distributions per golfer: n, mean, SD, median, IQR (raw values)")
    for m in PARTS:
        cells = []
        for pop in GOLFERS:
            xs = [v for v in (val(s, m) for s in data[pop].values()) if v is not None]
            if len(xs) > 2:
                q1, q3 = np.percentile(xs, [25, 75])
                cells.append(f"{pop} {len(xs):3d} {np.mean(xs):+7.2f} ±{np.std(xs, ddof=1):5.2f} md {np.median(xs):+7.2f} [{q1:+.1f},{q3:+.1f}]")
        if cells:
            print(f"  {m:24s} " + " | ".join(cells))
    print()


def part_z_table(shots, spec, coherent_mode):
    """[{part: signed z}] per shot — the ladder walked like the engine; coherent parts loss-signed."""
    rows = []
    for sh in shots:
        d = {}
        for part in spec:
            name, ladder = part[0], part[1]
            signs = part[2] if coherent_mode else [1] * len(ladder)
            if not ladder:
                continue
            for m, sg in zip(ladder, signs):
                z = zed(sh, m)
                if z is not None:
                    d[name] = sg * z; break
        rows.append(d)
    return rows


def mean_pairwise_r(rows, names, centre_by=None):
    """Mean of the pairwise Pearson r over shots reading both parts; `centre_by` (a parallel list of
    golfer keys) removes each golfer's own mean first, so only swing-to-swing co-movement counts."""
    rs, detail = [], []
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            pts = [(r[a], r[b], g) for r, g in zip(rows, centre_by or [None] * len(rows)) if a in r and b in r]
            if len(pts) < 8:
                continue
            x = np.array([p[0] for p in pts]); y = np.array([p[1] for p in pts])
            if centre_by:
                gs = [p[2] for p in pts]
                for g in set(gs):
                    k = np.array([q == g for q in gs])
                    x[k] -= x[k].mean(); y[k] -= y[k].mean()
            if x.std() == 0 or y.std() == 0:
                continue
            r = float(np.corrcoef(x, y)[0, 1]); rs.append(r); detail.append((a, b, len(pts), r))
    return (float(np.mean(rs)) if rs else float("nan")), detail


HELD = {"m_headPitchAddress", "m_trunkRiseImpact"}   # held measures are not read by the engine
COMPOSITES = (("m_postureHeldImpact", [(n, [m for m in l if m not in HELD], [sg for m, sg in zip(l, s) if m not in HELD], w)
                                       for n, l, s, w in IMPACT], True),
              ("m_postureHeldTop", TOP, True),
              ("m_setupPosture", [(n, [m for m in l if m not in HELD], None, w) for n, l, w, _ in SETUP], False))


def sec_v2(data):
    print("## V2 — ρ per composite: mean pairwise r of the parts' z (coherent: loss-signed sᵢzᵢ)")
    for cid, spec, coh in COMPOSITES:
        names = [p[0] for p in spec]
        print(f"### {cid}")
        allrows, allg = [], []
        for pop in GOLFERS:
            rows = part_z_table(data[pop].values(), spec, coh)
            rbar, det = mean_pairwise_r(rows, names)
            print(f"  {pop:5s} shots {len(rows):3d}  mean r {rbar:+.2f}  pairs " +
                  ", ".join(f"{a}/{b} {r:+.2f} (n {n})" for a, b, n, r in det))
            allrows += rows; allg += [pop] * len(rows)
        rw, detw = mean_pairwise_r(allrows, names, centre_by=allg)
        rp, detp = mean_pairwise_r(allrows, names)
        print(f"  WITHIN-golfer pooled (each golfer centred)  mean r {rw:+.2f}  floored {max(0.0, rw):.2f}")
        print("    " + ", ".join(f"{a}/{b} {r:+.2f} (n {n})" for a, b, n, r in detw))
        print(f"  RAW pooled (between golfers too)            mean r {rp:+.2f}")
    print()


NEW = ["loss_of_posture", "posture_sinks_impact", "loss_of_posture_backswing", "posture_sinks_backswing",
       "poor_setup_posture", "weight_on_toes_address", "arms_reaching_at_address", "reverse_tilt_at_address"]
MEMBERS = {   # the family's members: the †-edge single faults of §7.3
    "loss_of_posture": ["early_extension", "coming_out_of_it"],
    "posture_sinks_impact": ["diving"],
    "loss_of_posture_backswing": ["trail_knee_straighten", "pelvis_rise_backswing", "head_rise_backswing"],
    "posture_sinks_backswing": ["pelvis_sink_backswing", "head_drop_backswing"],
    "poor_setup_posture": ["posture_too_upright", "posture_too_bent", "excessive_knee_flex", "insufficient_knee_flex",
                           "c_posture", "s_posture", "weight_in_heels_address", "weight_on_toes_address",
                           "arms_reaching_at_address", "reverse_tilt_at_address"],
}


def load_ledgers(root):
    """{pop: [{condition: state}]} from the regraded diagnostics.json under <root>/<library>/<session>/."""
    import glob, json, os
    pops = {"mark": "Mark-Liversedge", "nick": "Nick-Henn", "jess": "Jess-Ratcliffe"}
    out = {}
    for pop, lib in pops.items():
        shots = []
        for f in sorted(glob.glob(os.path.join(root, lib, "*", "diagnostics.json"))):
            led = json.load(open(f, encoding="utf-8"))["ledger"]
            for sh in led["shots"]:
                shots.append({r["conditionId"]: r["state"] for r in sh["rows"]})
        out[pop] = shots
    return out


def sec_v4(ledger_root):
    print("## V4 — firing table (regraded ledgers): fired / assessable per golfer")
    led = load_ledgers(ledger_root)
    hdr = "  ".join(f"{p:>13s}" for p in GOLFERS)
    print(f"  {'condition':28s} {hdr}")
    for c in NEW + sorted({m for ms in MEMBERS.values() for m in ms} - set(NEW)):
        cells = []
        for pop in GOLFERS:
            st = [s.get(c) for s in led[pop]]
            ass = sum(1 for x in st if x in ("fired", "clean"))
            fir = sum(1 for x in st if x == "fired")
            cells.append(f"{fir:4d}/{ass:<4d}({len(st):3d})")
        print(f"  {c:28s} " + "  ".join(f"{x:>13s}" for x in cells))
    print()
    print("  family without a member firing / member firing without the family (both assessable)")
    for fam, mem in MEMBERS.items():
        cells = []
        for pop in GOLFERS:
            fwm = mwf = 0
            for s in led[pop]:
                if s.get(fam) not in ("fired", "clean"):
                    continue
                anym = any(s.get(m) == "fired" for m in mem)
                if s[fam] == "fired" and not anym: fwm += 1
                if s[fam] == "clean" and anym: mwf += 1
            cells.append(f"{pop} {fwm}/{mwf}")
        print(f"  {fam:28s} " + "   ".join(cells))
    print()


def sec_v5(data):
    """Library copy against corpus copy of the SAME swing: each has its own pose run."""
    print("## V5 — repeatability: library pose against the corpus's pose on the same swings")
    lib = {}
    for pop in GOLFERS:
        for (p, sess, sw), sh in data[pop].items():
            lib[(sess, sw)] = sh
    pairs = [(lib[(sess, sw)], sh) for (_, sess, sw), sh in data["corpus"].items() if (sess, sw) in lib]
    print(f"  matched swings: {len(pairs)}")
    ident = lambda a, b: all(val(a, m) == val(b, m) for m in ("m_spineBendAtAddress", "m_headLiftImpact", "m_axisTiltAddress"))
    same = sum(1 for a, b in pairs if ident(a, b))
    print(f"  identical (the corpus copy carries the SAME pose run, so it tests nothing): {same}")
    pairs = [(a, b) for a, b in pairs if not ident(a, b)]
    print(f"  independent pose runs compared below: {len(pairs)}")
    print(f"  {'measure':26s} {'n':>4s} {'median |diff|':>14s} {'p90 |diff|':>11s} {'verdict flips':>14s}")
    for m in FAMILY + ["m_headPitchAddress", "m_armHangAddress", "m_spineBendChangeImpact", "m_spineBendChangeTop",
                       "m_trailKneeFlex", "m_headLiftBack", "m_axisTiltAddress", "m_pelvisThrustImpact", "m_headLiftImpact",
                       "m_pelvisLiftImpact",
                       # control: established measures behind long-shipped conditions
                       "m_spineBendAtAddress", "m_spineBendLossDown", "m_pelvisThrustDown"]:
        d, flips, n = [], 0, 0
        for a, b in pairs:
            va, vb = val(a, m), val(b, m)
            if va is None or vb is None:
                continue
            n += 1; d.append(abs(va - vb))
            if verdict(zed(a, m)) != verdict(zed(b, m)):
                flips += 1
        if n:
            print(f"  {m:26s} {n:4d} {np.median(d):14.2f} {np.percentile(d, 90):11.2f} {flips:6d} of {n:<4d}")
    print()


def main():
    d = sys.argv[1]
    want = sys.argv[2:] or ["pop", "v1"]
    if want == ["v4"]:
        return sec_v4(d)
    data = collections.OrderedDict((p, load(f"{d}/{p}.csv", p)) for p in GOLFERS)
    if "v5" in want:
        data["corpus"] = load(f"{d}/corpus.csv", "corpus")
    for w in want:
        globals()["sec_" + w](data)


if __name__ == "__main__":
    main()
