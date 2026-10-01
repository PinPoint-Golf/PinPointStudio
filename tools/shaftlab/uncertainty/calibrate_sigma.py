#!/usr/bin/env python3
"""calibrate_sigma.py — calibrate the per-sample shaft σθ / gross-risk table on hand marks.

docs/design/shaft_uncertainty_propagation_design.md §4.1–§4.3, stage U1.

Input: a run root written by sweep.py (one dir per swing: result.json + trace.jsonl, and for DTL
swings club_dtl.json), and the corpus truth (face-on: truth.json `shaft` marks; DTL: the band-
template held-out truth in corpus/dtl_heldout_truth/). For every marked frame the residual
r = wrap(θ_track − θ_mark) is binned by the tier that produced the sample × the phase group.

Per cell (pooled to the tier, then to the global prior, below MIN_N frames or MIN_SW swings):
  * gross: |r − median| > max(15°, 4σ), iterated twice from σ = 1.4826·MAD;
  * σ_noise = 1.4826·MAD of the non-gross residuals; bias = their median (both reported);
  * σ_ship = max(q68 |r|, q93 |r| / 2, SIG_FLOOR) over the non-gross residuals — COVERAGE-calibrated:
    the residuals' tails are heavier than a Gaussian's, so a MAD σ covers ±1σ but misses ±2σ (the
    first pass measured 75–80 % within ±2σ in the downswing). |r| is about ZERO, not the median: the
    published values are not bias-corrected, so the bias sits inside σ;
  * pGross = (k + ½)/(n + 1) — never zero on a finite sample;
  * RAY / WEDGE / RECON: σ = a + b·|θ̇| is ESTIMATED (least squares on |θ̇| terciles) and reported
    but NOT emitted: the phase groups already carry the speed, and the slope fitted on the pooled
    tier double-counts it against a group's own quantile σ. kSigSlope stays 0.
Pooling: cell → the tier within the same half of the swing (pre-impact: address … downswing;
post: impact … finish) → the tier → the design prior.
Lag-1 ρ of consecutive marked residuals (≤ 1.5 frame periods apart), per swing, pooled.

Held-out check: swings are split A/B by a stable hash; the table fitted on A is scored on B and
vice versa, coverage within ±1σ / ±2σ of the non-gross residuals per phase group.

Outputs (--out-dir, default docs/research/data/uncertainty/):
  sigma_table.csv, bias_table.csv, rho.csv, dtl_sigma.csv, coverage_U1.md
and with --emit-cpp the CALIBRATION_TABLE block of src/Core/pp_tuned_constants.h is rewritten.
"""
import argparse, bisect, collections, hashlib, json, math, os, re, statistics as st, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shaftcal as sc

TIERS = ["pred", "ray", "band", "recon", "wedge", "seg", "ball"]
GROUPS = sc.GROUPS
MIN_N, MIN_SW = 30, 5
SIG_FLOOR = 0.5
# The design's priors (pp_tuned_constants.h placeholders) for a tier with too little truth.
PRIOR_SIG = {"pred": 15.0, "ray": 3.0, "band": 1.0, "recon": 10.0, "wedge": 6.0, "seg": 2.0, "ball": 4.0}
PRIOR_PG = {"pred": 0.3, "ray": 0.05, "band": 0.01, "recon": 0.2, "wedge": 0.1, "seg": 0.03, "ball": 0.05}
SLOPE_TIERS = ("ray", "wedge", "recon")
DTL_TIERS = ["RAY", "BAND", "HELD"]
POST = ("impact", "through", "finish")
DTL_TRUTH = "/mnt/swingdata/corpus/dtl_heldout_truth"


def mad_sigma(v):
    if len(v) < 2:
        return float("nan")
    m = st.median(v)
    return 1.4826 * st.median([abs(x - m) for x in v])


def split_gross(v, prior_sig):
    """Twice-iterated gross split. Returns (clean, n_gross)."""
    clean = list(v)
    sig = mad_sigma(clean) if len(clean) >= 5 else prior_sig
    for _ in range(2):
        if not clean:
            break
        m = st.median(clean)
        thr = max(15.0, 4.0 * max(sig, SIG_FLOOR))
        clean = [x for x in v if abs(x - m) <= thr]
        if len(clean) >= 5:
            sig = mad_sigma(clean)
    return clean, len(v) - len(clean)


def cell_stats(v, prior_sig):
    clean, k = split_gross(v, prior_sig)
    n = len(v)
    if len(clean) >= 2:
        sig_n = max(mad_sigma(clean), 1e-9)
        bias = st.median(clean)
        a = sorted(abs(x) for x in clean)
        q = lambda f: a[min(len(a) - 1, int(math.ceil(f * len(a))) - 1)]
        sig = max(SIG_FLOOR, q(0.683), q(0.93) / 2.0)
    else:
        sig_n, bias, sig = prior_sig, 0.0, prior_sig
    return {"n": n, "nGross": k, "sigNoise": sig_n, "bias": bias,
            "sig": sig, "pGross": (k + 0.5) / (n + 1.0)}


def half_of(swing):
    return int(hashlib.md5(swing.encode()).hexdigest(), 16) % 2


def fit_table(rows):
    """rows: residual dicts (tier, group, r, swing, thdot). → (table, slope, report)."""
    by_cell = collections.defaultdict(list)
    by_tier = collections.defaultdict(list)
    for x in rows:
        if x["tier"] is None or x["group"] is None:
            continue
        by_cell[(x["tier"], x["group"])].append(x)
        by_tier[x["tier"]].append(x)
    tier_stats = {t: cell_stats([x["r"] for x in by_tier[t]], PRIOR_SIG[t]) if len(by_tier[t]) >= MIN_N else None
                  for t in TIERS}
    by_half = collections.defaultdict(list)
    for x in rows:
        if x["tier"] is not None and x["group"] is not None:
            by_half[(x["tier"], x["group"] in POST)].append(x)
    half_stats = {}
    for key, xs in by_half.items():
        if len(xs) >= MIN_N and len({x["swing"] for x in xs}) >= MIN_SW:
            half_stats[key] = cell_stats([x["r"] for x in xs], PRIOR_SIG[key[0]])
    table = {}
    for t in TIERS:
        for g in GROUPS:
            c = by_cell.get((t, g), [])
            nsw = len({x["swing"] for x in c})
            if len(c) >= MIN_N and nsw >= MIN_SW:
                s = cell_stats([x["r"] for x in c], PRIOR_SIG[t]); s["src"] = "cell"
            elif (t, g in POST) in half_stats:
                s = dict(half_stats[(t, g in POST)]); s["src"] = "half"
            elif tier_stats[t] is not None:
                s = dict(tier_stats[t]); s["src"] = "tier"
            else:
                s = {"n": len(c), "nGross": 0, "sigNoise": PRIOR_SIG[t], "bias": 0.0,
                     "sig": PRIOR_SIG[t], "pGross": PRIOR_PG[t], "src": "prior"}
            s["nCell"], s["swCell"] = len(c), nsw
            table[(t, g)] = s
    # σ = a + b·|θ̇|: fit on the tier's non-gross residuals; a replaces nothing — the slope is applied
    # on top of each cell's base, so b is estimated on residuals CENTRED on their cell's σ.
    slope = {t: 0.0 for t in TIERS}
    for t in SLOPE_TIERS:
        xs = by_tier.get(t, [])
        if not xs:
            continue
        clean, _ = split_gross([x["r"] for x in xs], PRIOR_SIG[t])
        cs = set(id(x) for x in xs if x["r"] in clean)
        pts = sorted((x["thdot"], x["r"]) for x in xs if id(x) in cs)
        if len(pts) < 45:
            continue
        k = len(pts) // 3
        bins = [pts[:k], pts[k:2 * k], pts[2 * k:]]
        X = [st.mean(p[0] for p in b) for b in bins]
        Y = [mad_sigma([p[1] for p in b]) for b in bins]
        mx, my = st.mean(X), st.mean(Y)
        den = sum((a - mx) ** 2 for a in X)
        b = sum((a - mx) * (y - my) for a, y in zip(X, Y)) / den if den > 1e-12 else 0.0
        slope[t] = max(0.0, b)
    return table, slope


def sigma_for(table, slope, x):
    return table[(x["tier"], x["group"])]["sig"]   # slope reported, not applied (see the docstring)


def coverage(table, slope, rows):
    """Per group: (n_clean, cov1, cov2, n_all, frac_gross_observed). Gross = |r| > max(15, 4σ)."""
    out = {}
    for g in GROUPS + ["ALL"]:
        rs = [x for x in rows if x["tier"] is not None and x["group"] is not None and (g == "ALL" or x["group"] == g)]
        if not rs:
            continue
        c1 = c2 = n = gross = 0
        for x in rs:
            s = sigma_for(table, slope, x)
            if abs(x["r"]) > max(15.0, 4.0 * s):
                gross += 1
                continue
            n += 1
            c1 += abs(x["r"]) <= s
            c2 += abs(x["r"]) <= 2.0 * s
        out[g] = (n, c1 / n if n else float("nan"), c2 / n if n else float("nan"), len(rs), gross / len(rs))
    return out


def lag1_rho(rows, period_of):
    pairs = []
    by_sw = collections.defaultdict(list)
    for x in rows:
        if x["tier"] is not None and abs(x["r"]) < 15.0:
            by_sw[x["swing"]].append(x)
    for sw, xs in by_sw.items():
        xs.sort(key=lambda x: x["t"])
        per = period_of.get(sw, 6635.0)
        for a, b in zip(xs, xs[1:]):
            if 0 < b["t"] - a["t"] <= 1.5 * per and a["tier"] == b["tier"]:
                pairs.append((a["r"] - 0.0, b["r"] - 0.0, a["tier"]))
    res = {}
    for t in TIERS + ["ALL"]:
        p = [(u, v) for u, v, tt in pairs if t == "ALL" or tt == t]
        if len(p) < 20:
            continue
        mu, mv = st.mean(u for u, _ in p), st.mean(v for _, v in p)
        su = math.sqrt(sum((u - mu) ** 2 for u, _ in p)); sv = math.sqrt(sum((v - mv) ** 2 for _, v in p))
        res[t] = (len(p), sum((u - mu) * (v - mv) for u, v in p) / (su * sv) if su > 0 and sv > 0 else float("nan"))
    return res


def dtl_residuals(root, ids):
    rows = []
    for sw in ids:
        tp = os.path.join(DTL_TRUTH, sw + ".json")
        cp = os.path.join(root, sw, "club_dtl.json")
        if not (os.path.exists(tp) and os.path.exists(cp)):
            continue
        T = json.load(open(tp)); C = json.load(open(cp))
        fr = [f for f in C.get("frames", []) if f.get("theta") is not None]
        ts = [f["t_us"] for f in fr]
        if not ts:
            continue
        for m in T["frames"]:
            k = bisect.bisect_left(ts, m["t_us"])
            js = [j for j in (k - 1, k) if 0 <= j < len(ts)]
            if not js:
                continue
            j = min(js, key=lambda j: abs(ts[j] - m["t_us"]))
            if abs(ts[j] - m["t_us"]) > 2000:
                continue
            f = fr[j]
            r = sc.wrap(math.degrees(f["theta"] - m["theta"]))
            rows.append({"swing": sw, "tier": f.get("tier"), "r": r, "rho": f.get("rhoPred"), "t": m["t_us"]})
    return rows


def emit_cpp(path, table, slope, rho, dtl):
    src = open(path, encoding="utf-8").read()
    b = src.index("CALIBRATION_TABLE_BEGIN"); e = src.index("CALIBRATION_TABLE_END")
    b = src.index("\n", b) + 1; e = src.rindex("\n", 0, e) + 1
    block = src[b:e]

    def arr2(name, key, fmt):
        rows = []
        for t in TIERS:
            rows.append("    { " + ", ".join(fmt % table[(t, g)][key] for g in GROUPS) + " },   // " + t)
        return rows

    def sub(pat, rep):
        nonlocal block
        new, n = re.subn(pat, rep, block, flags=re.S)
        if n != 1:
            raise SystemExit(f"emit-cpp: pattern not found once: {pat}")
        block = new

    sig_rows = "\n".join(arr2("kSigBaseDeg", "sig", "%.2f"))
    pg_rows = "\n".join(arr2("kPGross", "pGross", "%.4f"))
    sub(r"(inline constexpr double kSigBaseDeg\[kTiers\]\[kGroups\] = \{\n).*?(\n\};)", r"\g<1>" + sig_rows.replace("\\", "\\\\") + r"\g<2>")
    sub(r"(inline constexpr double kPGross\[kTiers\]\[kGroups\] = \{\n).*?(\n\};)", r"\g<1>" + pg_rows + r"\g<2>")
    if "ALL" in rho and math.isfinite(rho["ALL"][1]):
        sub(r"(inline constexpr double kRho\s*=\s*)[0-9.eE+-]+", r"\g<1>" + "%.2f" % max(0.0, min(0.95, rho["ALL"][1])))
    if dtl:
        sub(r"(inline constexpr double kDtlSigDeg\[3\]\s*=\s*\{)[^}]*(\})", r"\g<1> " + ", ".join("%.2f" % dtl[t]["sig"] for t in DTL_TIERS) + r" \g<2>")
        sub(r"(inline constexpr double kDtlPGross\[3\]\s*=\s*\{)[^}]*(\})", r"\g<1> " + ", ".join("%.4f" % dtl[t]["pGross"] for t in DTL_TIERS) + r" \g<2>")
    open(path, "w", encoding="utf-8").write(src[:b] + block + src[e:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out-dir", default=None)
    ap.add_argument("--emit-cpp", default=None, help="path to pp_tuned_constants.h to rewrite")
    a = ap.parse_args()
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    od = a.out_dir or os.path.join(repo, "docs/research/data/uncertainty")
    os.makedirs(od, exist_ok=True)

    ids = sc.swing_ids(a.root)
    rows, period_of = [], {}
    for sw in ids:
        rr = sc.residuals(os.path.join(a.root, sw), sw)
        rows += rr
        r = sc.swing_frames(os.path.join(a.root, sw))
        if r:
            period_of[sw] = r[1]
    rows = [x for x in rows if x["tier"] is not None]
    swings = sorted({x["swing"] for x in rows})

    table, slope = fit_table(rows)
    rho = lag1_rho(rows, period_of)
    A = [x for x in rows if half_of(x["swing"]) == 0]; B = [x for x in rows if half_of(x["swing"]) == 1]
    tA, sA = fit_table(A); tB, sB = fit_table(B)
    covAB = coverage(tA, sA, B); covBA = coverage(tB, sB, A); covIn = coverage(table, slope, rows)
    covPool = {}
    for g in set(covAB) | set(covBA):
        parts = [c for c in (covAB.get(g), covBA.get(g)) if c and c[0]]
        n = sum(c[0] for c in parts); na = sum(c[3] for c in parts)
        if n:
            covPool[g] = (n, sum(c[0] * c[1] for c in parts) / n, sum(c[0] * c[2] for c in parts) / n, na,
                          sum(c[3] * c[4] for c in parts) / na)
    covPool = {g: covPool[g] for g in GROUPS + ["ALL"] if g in covPool}
    # Gate rule: a gated group (≥ 30 held-out frames) that misses ±1σ ≥ 60 % or ±2σ ≥ 90 % gets ONE
    # inflation factor, the smallest (0.05 steps) that brings its pooled held-out coverage to target;
    # it multiplies that group's column in the shipped table. A σ never ships narrower than its
    # held-out coverage supports.
    inflate = {}
    for g in GROUPS:
        if g not in covPool or covPool[g][0] < MIN_N:
            continue
        f = 1.0
        while f < 3.0:
            ok = True; tot1 = tot2 = n = 0
            for tt, held in ((tA, B), (tB, A)):
                for x in held:
                    if x["group"] != g:
                        continue
                    sg = tt[(x["tier"], g)]["sig"]
                    if abs(x["r"]) > max(15.0, 4.0 * sg):
                        continue
                    n += 1; tot1 += abs(x["r"]) <= f * sg; tot2 += abs(x["r"]) <= 2 * f * sg
            if n == 0 or (tot1 / n >= 0.60 and tot2 / n >= 0.90):
                break
            f += 0.05
        if f > 1.0:
            inflate[g] = round(f, 2)
            for t in TIERS:
                table[(t, g)]["sig"] *= f
            for tt in (tA, tB):
                for t in TIERS:
                    tt[(t, g)]["sig"] *= f
    covInfl = {}
    if inflate:
        cAB = coverage(tA, sA, B); cBA = coverage(tB, sB, A)
        for g in GROUPS + ["ALL"]:
            parts = [c for c in (cAB.get(g), cBA.get(g)) if c and c[0]]
            n = sum(c[0] for c in parts); na = sum(c[3] for c in parts)
            if n:
                covInfl[g] = (n, sum(c[0] * c[1] for c in parts) / n, sum(c[0] * c[2] for c in parts) / n, na,
                              sum(c[3] * c[4] for c in parts) / na)

    # DTL rows: base σ on near-address frames (ρ̂ ≥ 0.9), then check σ/ρ̂ beyond.
    drows = dtl_residuals(a.root, ids)
    dtl = {}
    for t in DTL_TIERS:
        near = [x["r"] for x in drows if x["tier"] == t and (x["rho"] or 0) >= 0.9]
        allv = [x["r"] for x in drows if x["tier"] == t]
        base = near if len(near) >= MIN_N else allv
        dtl[t] = cell_stats(base, {"RAY": 2.0, "BAND": 1.0, "HELD": 4.0}[t]) if len(base) >= 10 else \
            {"n": len(base), "nGross": 0, "sigNoise": float("nan"), "bias": 0.0,
             "sig": {"RAY": 2.0, "BAND": 1.0, "HELD": 4.0}[t], "pGross": {"RAY": 0.05, "BAND": 0.02, "HELD": 0.1}[t]}
        dtl[t]["nNear"], dtl[t]["nAll"] = len(near), len(allv)
    dcov = {}
    for t in DTL_TIERS:
        xs = [x for x in drows if x["tier"] == t]
        if not xs:
            continue
        c1 = c2 = n = 0
        for x in xs:
            s = dtl[t]["sig"] / max(x["rho"] or 1.0, 0.5)
            if abs(x["r"]) > max(15.0, 4 * s):
                continue
            n += 1; c1 += abs(x["r"]) <= s; c2 += abs(x["r"]) <= 2 * s
        dcov[t] = (n, c1 / n if n else float("nan"), c2 / n if n else float("nan"), len(xs))

    with open(os.path.join(od, "sigma_table.csv"), "w") as f:
        f.write("tier,group,sigma_deg,p_gross,source,n_cell,swings_cell,n_used,n_gross,slope_deg_per_deg_frame\n")
        for t in TIERS:
            for g in GROUPS:
                s = table[(t, g)]
                f.write(f"{t},{g},{s['sig']:.3f},{s['pGross']:.4f},{s['src']},{s['nCell']},{s['swCell']},{s['n']},{s['nGross']},{slope[t]:.3f}\n")
    with open(os.path.join(od, "bias_table.csv"), "w") as f:
        f.write("tier,group,bias_deg,sigma_noise_deg,source\n")
        for t in TIERS:
            for g in GROUPS:
                s = table[(t, g)]
                f.write(f"{t},{g},{s['bias']:.3f},{s['sigNoise']:.3f},{s['src']}\n")
    with open(os.path.join(od, "rho.csv"), "w") as f:
        f.write("tier,pairs,lag1_rho\n")
        for t, (n, r) in rho.items():
            f.write(f"{t},{n},{r:.3f}\n")
    with open(os.path.join(od, "dtl_sigma.csv"), "w") as f:
        f.write("tier,sigma_deg,p_gross,bias_deg,n_near_address,n_all,cov1_inflated,cov2_inflated\n")
        for t in DTL_TIERS:
            d = dtl[t]; c = dcov.get(t, (0, float("nan"), float("nan"), 0))
            f.write(f"{t},{d['sig']:.3f},{d['pGross']:.4f},{d['bias']:.3f},{d['nNear']},{d['nAll']},{c[1]:.3f},{c[2]:.3f}\n")

    def covtab(cv):
        L = ["| group | n (non-gross) | within ±1σ | within ±2σ | n all | observed gross |", "|---|---:|---:|---:|---:|---:|"]
        for g, (n, c1, c2, na, gf) in cv.items():
            L.append(f"| {g} | {n} | {c1:.0%} | {c2:.0%} | {na} | {gf:.1%} |")
        return "\n".join(L)

    with open(os.path.join(od, "coverage_U1.md"), "w") as f:
        f.write(f"# U1 per-sample σθ — coverage on hand marks\n\nRun root `{a.root}`; {len(rows)} marked face-on frames on "
                f"{len(swings)} swings. Gross = |r| > max(15°, 4σ). Targets: 60–76 % within ±1σ, 90–98 % within ±2σ "
                f"(non-gross frames).\n\n## Held out, both rotations pooled (the per-group verdict; groups under 30 frames are not gated)\n\n{covtab(covPool)}\n\n## Group inflation (gate rule) — {inflate if inflate else "none needed"}\n\n" + (f"Held out, pooled, AFTER the inflation:\n\n{covtab(covInfl)}\n\n" if inflate else "") + f"## Held out — fit on half A, scored on half B\n\n{covtab(covAB)}\n\n"
                f"## Held out — fit on half B, scored on half A\n\n{covtab(covBA)}\n\n## In sample (the shipped table)\n\n{covtab(covIn)}\n\n"
                f"## Lag-1 ρ of consecutive marked residuals\n\n" + "\n".join(f"- {t}: {r:.2f} over {n} pairs" for t, (n, r) in rho.items()) +
                "\n\n## DTL (band-template truth; σ fitted at ρ̂ ≥ 0.9, scored everywhere as σ/max(ρ̂, 0.5))\n\n"
                "| tier | σ | pGross | bias | n near address | n all | within ±1σ | within ±2σ |\n|---|---:|---:|---:|---:|---:|---:|---:|\n" +
                "\n".join(f"| {t} | {dtl[t]['sig']:.2f}° | {dtl[t]['pGross']:.3f} | {dtl[t]['bias']:+.2f}° | {dtl[t]['nNear']} | {dtl[t]['nAll']} | "
                          f"{dcov.get(t,(0,float('nan'),float('nan'),0))[1]:.0%} | {dcov.get(t,(0,float('nan'),float('nan'),0))[2]:.0%} |" for t in DTL_TIERS) + "\n")
    if a.emit_cpp:
        emit_cpp(a.emit_cpp, table, slope, rho, dtl)
    print(open(os.path.join(od, "coverage_U1.md")).read())
    print("slopes:", {t: round(v, 3) for t, v in slope.items() if v})


if __name__ == "__main__":
    main()
