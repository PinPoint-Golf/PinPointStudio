#!/usr/bin/env python3
"""grade_coverage.py — the Phase C/E gates of the shaft uncertainty work, applied mechanically.

docs/design/shaft_uncertainty_propagation_design.md §8; the plan's gate rules.

  grade_coverage.py --base BASE --dark DARK --sigma SIGMA [--soft SOFT] [--oneimp ONEIMP] [--fb FB]
                    [--out-dir DIR]

BASE   = the pre-change binary's traced sweep (the byte-identity reference)
DARK   = the new binary, no overrides (uncertainty.enabled at its default)
SIGMA  = uncertainty.enabled=1 (soft anchors, oneImpact and fbPosterior off)
SOFT / ONEIMP / FB = SIGMA plus that one switch

Writes gates_<date>.md (the verdict table plus every list behind it) and coverage_metrics.csv.
"""
import argparse, bisect, collections, datetime, hashlib, json, math, os, re, statistics as st, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
import shaftcal as sc

SIGMA_KEYS = {"sigTheta", "pGross", "tier", "sigma", "sigmaKind", "grossRisk", "sigmaTUs", "addressBallSigmaPx",
              "synthKappa", "sigmaIotaBackDeg", "sigmaIotaDownDeg", "sigmaDeltaDeg", "pNeedle", "inclSigmaDeg",
              "sigmaDeg", "addressInclSigmaDeg", "sig_theta", "p_gross", "fb_sigma", "fb_palt", "fbTemps"}
# Stamps that the version bumps change by design.
VERSION_KEYS = {"versions", "stageVersion"}
IMPACT = 5


def wrap(d):
    return (d + 180.0) % 360.0 - 180.0


def is_timing(k):
    """Wall-clock fields (timings/*, *Ms, ms): they differ run to run on the same binary."""
    return k in ("timings", "ms", "wallMs", "wall") or (k.endswith("Ms") and k[:-2][-1:].islower())


def strip(o, keys):
    if isinstance(o, dict):
        return {k: strip(v, keys) for k, v in o.items() if k not in keys and not is_timing(k)}
    if isinstance(o, list):
        return [strip(v, keys) for v in o]
    return o


def diff_paths(a, b, path="", out=None, cap=8):
    out = [] if out is None else out
    if len(out) >= cap:
        return out
    if type(a) != type(b):
        out.append(path or "/")
    elif isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                out.append(f"{path}/{k}")
            else:
                diff_paths(a[k], b[k], f"{path}/{k}", out, cap)
    elif isinstance(a, list):
        if len(a) != len(b):
            out.append(f"{path}[len {len(a)}→{len(b)}]")
        else:
            for i, (x, y) in enumerate(zip(a, b)):
                diff_paths(x, y, f"{path}[{i}]", out, cap)
                if len(out) >= cap:
                    break
    elif a != b:
        out.append(path)
    return out


def load_any(p):
    if not os.path.exists(p):
        return None
    if p.endswith(".jsonl"):
        return [json.loads(l) for l in open(p, encoding="utf-8")]
    return json.load(open(p, encoding="utf-8"))


FILES = ["result.json", "trace.jsonl", "club_dtl.json", "trace_dtl.jsonl"]


def compare_roots(ra, rb, ids, drop):
    """→ {swing: [differing paths]} for swings that differ after `drop` keys are removed."""
    bad = {}
    for sw in ids:
        for fn in FILES:
            pa, pb = os.path.join(ra, sw, fn), os.path.join(rb, sw, fn)
            if fn == "result.json":
                pa, pb = sc.result_path(os.path.join(ra, sw)) or pa, sc.result_path(os.path.join(rb, sw)) or pb
            A, B = load_any(pa), load_any(pb)
            if A is None and B is None:
                continue
            if (A is None) != (B is None):
                bad.setdefault(sw, []).append(f"{fn}: present in one run only")
                continue
            d = diff_paths(strip(A, drop), strip(B, drop))
            if d:
                bad.setdefault(sw, []).extend(f"{fn}:{x}" for x in d)
    return bad


def metric(A, key):
    return next((m for m in A.get("metrics", []) if m.get("key") == key), None)


def phase_sample(m, phase):
    if not m:
        return None
    return next((p for p in m.get("phaseSamples", []) if p.get("phase") == phase), None)


def half_of(swing):
    return int(hashlib.md5(swing.encode()).hexdigest(), 16) % 2


def cov(z):
    """z = |r|/σ list → (n, within 1, within 2)."""
    n = len(z)
    return (n, sum(1 for x in z if x <= 1) / n, sum(1 for x in z if x <= 2) / n) if n else (0, float("nan"), float("nan"))


def spearman(x, y):
    def rank(v):
        o = sorted(range(len(v)), key=lambda i: v[i]); r = [0.0] * len(v)
        i = 0
        while i < len(o):
            j = i
            while j + 1 < len(o) and v[o[j + 1]] == v[o[i]]:
                j += 1
            for k in range(i, j + 1):
                r[o[k]] = (i + j) / 2.0
            i = j + 1
        return r
    if len(x) < 5:
        return float("nan")
    rx, ry = rank(x), rank(y)
    mx, my = st.mean(rx), st.mean(ry)
    num = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    den = math.sqrt(sum((a - mx) ** 2 for a in rx) * sum((b - my) ** 2 for b in ry))
    return num / den if den > 0 else float("nan")


# ── synth posterior κ ─────────────────────────────────────────────────────────────
def synth_rows(root, ids):
    rows = []
    for sw in ids:
        T = sc.load_truth(sw)
        rp = sc.result_path(os.path.join(root, sw))
        if not T or not T.get("shaft") or not rp:
            continue
        sy = json.load(open(rp, encoding="utf-8"))["analysis"].get("club", {}).get("synth", [])
        if len(sy) < 3:
            continue
        ts = [s["t_us"] for s in sy]
        th = [math.degrees(s["theta"]) for s in sy]
        for i in range(1, len(th)):
            th[i] = th[i - 1] + wrap(th[i] - th[i - 1])
        for m in T["shaft"]:
            t = m["t_us"]
            k = bisect.bisect_left(ts, t)
            if k <= 0 or k >= len(ts) or ts[k] - ts[k - 1] >= 20000:
                continue
            u = (t - ts[k - 1]) / max(1, ts[k] - ts[k - 1])
            v = th[k - 1] + u * (th[k] - th[k - 1])
            s0, s1 = sy[k - 1].get("sigTheta"), sy[k].get("sigTheta")
            if s0 is None or s1 is None or s0 <= 0 or s1 <= 0:
                continue
            rows.append({"swing": sw, "r": wrap(v - math.degrees(m["theta"])), "sig": max(s0, s1)})
    return rows


def q_scale(z):
    """The σ scale meeting BOTH targets on these |r|/σ: max(q68, q93 / 2) — the residuals' tails are
    heavier than a Gaussian's, so a scale fitted at 68 % alone misses ±2σ (calibrate_sigma.py)."""
    z = sorted(z)
    if not z:
        return 1.0
    q = lambda f: z[min(len(z) - 1, int(math.ceil(f * len(z))) - 1)]
    return max(q(0.683), q(0.93) / 2.0)


def fit_c(rows):
    """The synth σ scale c (κ_new ≈ κ_old / c²; the anchors' share of σ does not scale with κ, so the
    confirmation sweep re-checks the coverage)."""
    return q_scale([abs(x["r"]) / x["sig"] for x in rows if abs(x["r"]) <= 15.0])


# ── per-sample σ and forward–backward ─────────────────────────────────────────────
def sample_rows(root, ids):
    rows = []
    for sw in ids:
        rd = os.path.join(root, sw)
        rr = sc.residuals(rd, sw)
        if not rr:
            continue
        tr, summ = sc.load_trace(rd)
        temps = (summ.get("summary") or {}).get("fbTemps") or []
        fr = sc.swing_frames(rd)
        if not fr:
            continue
        ts = [x["t"] for x in fr[0]]
        for x in rr:
            j = ts.index(x["t"]) if x["t"] in ts else None
            x["temps"] = temps
            x["fbs"] = tr[j].get("fb_sigma") if (tr and j is not None and j < len(tr)) else None
            rows.append(x)
    return rows


# ── per-metric truth ──────────────────────────────────────────────────────────────
def lean_rows(root, ids):
    """Impact lean vs the hand mark at truth P7: lean = sgn·wrap(θ − 90°), the producer's convention."""
    out = []
    for sw in ids:
        T = sc.load_truth(sw)
        rp = sc.result_path(os.path.join(root, sw))
        if not T or not T.get("shaft") or not rp or "p7_s" not in T.get("events", {}):
            continue
        A = json.load(open(rp, encoding="utf-8"))["analysis"]
        m = metric(A, "impactShaftLean")
        ps = phase_sample(m, IMPACT)
        if not ps:
            continue
        t7 = T["events"]["p7_s"] * 1e6
        mk = min(T["shaft"], key=lambda q: abs(q["t_us"] - t7))
        if abs(mk["t_us"] - t7) > 10000:
            continue
        S = A["club"]["samples"]
        # The producer's sign (wrist_analyzer.cpp buildShaftLeanSeries): −1 only for handedness 2.
        # Every corpus swing is one right-handed golfer; inferring it per swing from the curve's slope
        # was tried and picked the wrong sign on most swings (a +5.5° "bias" that was the sign).
        sgn = -1.0 if (T.get("meta", {}).get("handedness") == 2) else 1.0
        truth = sgn * wrap(math.degrees(mk["theta"]) - 90.0)
        out.append({"swing": sw, "r": ps["value"] - truth, "sig": ps.get("sigma"), "gross": ps.get("grossRisk"),
                    "kind": ps.get("sigmaKind"), "value": ps["value"], "truth": truth})
    return out


def lm_rows(root, ids, key, lmkey):
    import pp_swingdoc as psd
    out = []
    for sw in ids:
        s, w = sw.split("__")
        d = f"{sc.CORPUS}/swings/{s}/{w}"
        if not psd.has_swing(d):
            continue
        doc = psd.load_swing(d)
        lm = phase_sample(metric(doc.get("analysis", {}), lmkey), IMPACT) or \
            next(iter((metric(doc.get("analysis", {}), lmkey) or {}).get("phaseSamples", [])), None)
        rp = sc.result_path(os.path.join(root, sw))
        if not lm or not rp:
            continue
        ps = phase_sample(metric(json.load(open(rp, encoding="utf-8"))["analysis"], key), IMPACT)
        if not ps:
            continue
        out.append({"swing": sw, "value": ps["value"], "truth": lm["value"], "sig": ps.get("sigma"),
                    "kind": ps.get("sigmaKind")})
    return out


def metric_cov(rows, ratio=False):
    """Coverage with the bias (or for speed the scale) removed and reported separately."""
    rows = [r for r in rows if r.get("sig") and r["sig"] > 0]
    if len(rows) < 5:
        return None
    if ratio:
        k = st.median(r["value"] / r["truth"] for r in rows if r["truth"])
        res = [r["value"] - k * r["truth"] for r in rows]
        bias = k
    else:
        res0 = [r["value"] - r["truth"] for r in rows]
        bias = st.median(res0)
        res = [x - bias for x in res0]
    # Gross readings (|e| > max(15, 4σ), the per-sample rule) are pGross's business, not σ's; they are
    # counted and listed with the grossRisk the reading carried.
    gross = [(r["swing"], e, r.get("gross")) for e, r in zip(res, rows) if abs(e) > max(15.0, 4.0 * r["sig"])]
    z = [abs(e) / r["sig"] for e, r in zip(res, rows) if abs(e) <= max(15.0, 4.0 * r["sig"])]
    n, c1, c2 = cov(z)
    infl = max(1.0, q_scale(z))
    return {"n": n, "c1": c1, "c2": c2, "bias": bias, "gross": gross, "medSig": st.median(r["sig"] for r in rows), "inflate": infl,
            "medAbsErr": st.median(abs(e) for e in res)}


def moved(ra, rb, ids, keys):
    """Swings whose impact readings of `keys` moved between two runs: (swing, key, a, b)."""
    out = []
    for sw in ids:
        pa, pb = sc.result_path(os.path.join(ra, sw)), sc.result_path(os.path.join(rb, sw))
        if not pa or not pb:
            continue
        A = json.load(open(pa, encoding="utf-8"))["analysis"]; B = json.load(open(pb, encoding="utf-8"))["analysis"]
        for k in keys:
            a, b = phase_sample(metric(A, k), IMPACT), phase_sample(metric(B, k), IMPACT)
            va, vb = (a or {}).get("value"), (b or {}).get("value")
            if va is None and vb is None:
                continue
            if va is None or vb is None or abs(va - vb) > 1e-9:
                out.append((sw, k, va, vb))
    return out


def walls(root):
    shaft, total = {}, {}
    for f in os.listdir(root):
        if not f.endswith(".log"):
            continue
        txt = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
        m = re.search(r"\[ShaftTracker\] v3 frames .*?, (\d+) ms", txt)
        a = re.search(r"analysis ok in (\d+) ms", txt)
        if m: shaft[f[:-4]] = int(m.group(1))
        if a: total[f[:-4]] = int(a.group(1))
    return shaft, total


def synth_err_by_region(root, ids):
    """synth |median| error per region (synth_eval.py's split) for the soft-anchor gate."""
    REG = {}
    for sw in ids:
        T = sc.load_truth(sw); rp = sc.result_path(os.path.join(root, sw))
        if not T or not T.get("shaft") or not rp:
            continue
        ev = T.get("events", {})
        sy = json.load(open(rp, encoding="utf-8"))["analysis"].get("club", {}).get("synth", [])
        if len(sy) < 3:
            continue
        ts = [s["t_us"] for s in sy]; th = [math.degrees(s["theta"]) for s in sy]
        for i in range(1, len(th)):
            th[i] = th[i - 1] + wrap(th[i] - th[i - 1])
        for m in T["shaft"]:
            t = m["t_us"]; k = bisect.bisect_left(ts, t)
            if k <= 0 or k >= len(ts) or ts[k] - ts[k - 1] >= 20000:
                continue
            u = (t - ts[k - 1]) / max(1, ts[k] - ts[k - 1])
            e = wrap(th[k - 1] + u * (th[k] - th[k - 1]) - math.degrees(m["theta"]))
            r = ("P1-P4" if t < ev.get("p4_s", 1e9) * 1e6 else "P4-P7" if t <= ev.get("p7_s", 1e9) * 1e6
                 else "P7-P8" if t <= ev.get("p8_s", 1e9) * 1e6 else "after P8")
            REG.setdefault(r, []).append(e)
    return {r: (len(v), st.median(abs(x) for x in v)) for r, v in REG.items()}


def p7_theta_err(root, ids):
    out = {}
    for sw in ids:
        T = sc.load_truth(sw); rp = sc.result_path(os.path.join(root, sw))
        if not T or not T.get("shaft") or not rp or "p7_s" not in T.get("events", {}):
            continue
        club = json.load(open(rp, encoding="utf-8"))["analysis"].get("club", {})
        p7 = next((p for p in club.get("positions", []) if p.get("p") == 7), None)
        t7 = T["events"]["p7_s"] * 1e6
        mk = min(T["shaft"], key=lambda q: abs(q["t_us"] - t7))
        if p7 is None or abs(mk["t_us"] - t7) > 10000:
            continue
        out[sw] = abs(wrap(math.degrees(p7["theta"]) - math.degrees(mk["theta"])))
    return out


def main():
    ap = argparse.ArgumentParser()
    for k in ("base", "dark", "sigma", "soft", "oneimp", "fb", "seqsig", "seqsig_off"):
        ap.add_argument("--" + k)
    ap.add_argument("--out-dir", default=None)
    a = ap.parse_args()
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    od = a.out_dir or os.path.join(repo, "docs/research/data/uncertainty")
    os.makedirs(od, exist_ok=True)
    ids = sc.swing_ids(a.sigma)
    L = [f"# Shaft uncertainty — gate verdicts ({datetime.date.today().isoformat()})", "",
         f"Runs: base `{a.base}`, dark `{a.dark}`, sigma `{a.sigma}`" +
         "".join(f", {k} `{getattr(a, k)}`" for k in ("soft", "oneimp", "fb") if getattr(a, k)), f"Swings: {len(ids)}", ""]
    verdict = []

    # 1. byte-identity (dark vs base), versions stripped
    if a.base and a.dark:
        bad = compare_roots(a.base, a.dark, ids, VERSION_KEYS)
        verdict.append(("Byte-identity, dark vs base (version stamps excluded)", not bad, f"{len(ids) - len(bad)}/{len(ids)} identical"))
        if bad:
            L += ["## Byte-identity failures", ""] + [f"- {sw}: {', '.join(p[:4])}" for sw, p in list(bad.items())[:30]] + [""]
    # 2. value-identity (sigma vs dark), σ keys stripped
    if a.dark:
        bad = compare_roots(a.dark, a.sigma, ids, VERSION_KEYS | SIGMA_KEYS | {"config", "configHash"})
        verdict.append(("Value-identity, sigma vs dark (σ keys excluded)", not bad, f"{len(ids) - len(bad)}/{len(ids)} identical"))
        if bad:
            L += ["## Value-identity differences", ""] + [f"- {sw}: {', '.join(p[:4])}" for sw, p in list(bad.items())[:40]] + [""]

    # 2b. the kinematic sequence under uncertainty.sequenceSigma (a value change, Mark's call)
    if a.seqsig:
        mv = []
        for sw in ids:
            pa, pb = sc.result_path(os.path.join(a.seqsig_off or a.sigma, sw)), sc.result_path(os.path.join(a.seqsig, sw))
            if not pa or not pb:
                continue
            ka = json.load(open(pa, encoding="utf-8"))["analysis"].get("kinematicSequence", {})
            kb = json.load(open(pb, encoding="utf-8"))["analysis"].get("kinematicSequence", {})
            fa = (ka.get("order"), ka.get("verdict"), [n.get("placed") for n in ka.get("nodes", [])])
            fb = (kb.get("order"), kb.get("verdict"), [n.get("placed") for n in kb.get("nodes", [])])
            if fa != fb:
                mv.append(f"- {sw}: order {fa[0]} → {fb[0]}, verdict {fa[1]} → {fb[1]}, placed {fa[2]} → {fb[2]}")
        L += ["## Kinematic sequence with uncertainty.sequenceSigma (NOT gated — Mark's decision)", "",
              f"{len(mv)} of {len(ids)} swings change order, verdict or node placement:", ""] + mv + [""]

    # 3. per-sample σ as the pipeline applied it
    rows = sample_rows(a.sigma, ids)
    pr = [x for x in rows if x.get("sig")]
    ng = [x for x in pr if abs(x["r"]) <= max(15.0, 4 * x["sig"])]
    n, c1, c2 = cov([abs(x["r"]) / x["sig"] for x in ng])
    L += ["## Per-sample σθ as published (sigma run)", "",
          f"{len(pr)} marked frames carry σ; {len(ng)} non-gross: within ±1σ {c1:.0%}, ±2σ {c2:.0%}.", ""]
    L += ["| tier | n | ±1σ | ±2σ | median σ |", "|---|---:|---:|---:|---:|"]
    for t in sc.TIERS + ["ball"]:
        xs = [x for x in ng if x["tier"] == t]
        if xs:
            nn, a1, a2 = cov([abs(x["r"]) / x["sig"] for x in xs])
            L.append(f"| {t} | {nn} | {a1:.0%} | {a2:.0%} | {st.median(x['sig'] for x in xs):.2f}° |")
    L.append("")
    verdict.append(("Per-sample σθ coverage (non-gross, all groups)", 0.60 <= c1 and c2 >= 0.90, f"{c1:.0%} / {c2:.0%}"))

    # 4. synth κ (fitted on halves, scored across)
    srows = synth_rows(a.sigma, ids)
    kap_now = None
    for sw in ids:
        rp = sc.result_path(os.path.join(a.sigma, sw))
        if rp:
            kap_now = json.load(open(rp, encoding="utf-8"))["analysis"].get("club", {}).get("synthKappa")
            if kap_now:
                break
    if srows:
        A_ = [x for x in srows if half_of(x["swing"]) == 0]; B_ = [x for x in srows if half_of(x["swing"]) == 1]
        cA, cB, cAll = fit_c(A_), fit_c(B_), fit_c(srows)
        zAB = [abs(x["r"]) / (x["sig"] * cA) for x in B_ if abs(x["r"]) <= 15]
        zBA = [abs(x["r"]) / (x["sig"] * cB) for x in A_ if abs(x["r"]) <= 15]
        n, h1, h2 = cov(zAB + zBA)
        n0, u1, u2 = cov([abs(x["r"]) / x["sig"] for x in srows if abs(x["r"]) <= 15])
        kap_new = (kap_now or 0.33) / (cAll * cAll)
        L += ["## Synth posterior κ", "",
              f"{len(srows)} marked synth ticks. As run (κ = {kap_now}): ±1σ {u1:.0%}, ±2σ {u2:.0%}.",
              f"σ scale fitted per half: A {cA:.2f}, B {cB:.2f}; held-out after scaling: ±1σ {h1:.0%}, ±2σ {h2:.0%}.",
              f"**κ to ship = {kap_new:.3f}** (scale {cAll:.2f} on all ticks).", ""]
        verdict.append(("Synth σ held-out coverage after the κ fit", h1 >= 0.60 and h2 >= 0.90, f"{h1:.0%} / {h2:.0%}; κ → {kap_new:.3f}"))

    # 5. forward–backward vs the U1 table
    fbr = [x for x in rows if x.get("fbs") and x.get("sig") and abs(x["r"]) <= 15]
    if fbr:
        temps = fbr[0]["temps"]
        A_ = [x for x in fbr if half_of(x["swing"]) == 0]; B_ = [x for x in fbr if half_of(x["swing"]) == 1]
        def cov_t(xs, ti):
            z = [abs(x["r"]) / x["fbs"][ti] for x in xs if x["fbs"][ti] and x["fbs"][ti] > 0]
            return cov(z)
        best = {}
        for nm, fit, held in (("A→B", A_, B_), ("B→A", B_, A_)):
            ti = min(range(len(temps)), key=lambda i: abs(cov_t(fit, i)[1] - 0.683) if cov_t(fit, i)[0] else 9)
            best[nm] = (temps[ti], cov_t(held, ti))
        L += ["## Forward–backward (U5) vs the U1 table, in-span marked frames", "",
              "| T | n | ±1σ | ±2σ | Spearman(σ, |r|) |", "|---:|---:|---:|---:|---:|"]
        rhoFb = {}
        for ti, T in enumerate(temps):
            xs = [x for x in fbr if x["fbs"][ti] and x["fbs"][ti] > 0]
            n, f1, f2 = cov([abs(x["r"]) / x["fbs"][ti] for x in xs])
            rhoFb[T] = spearman([x["fbs"][ti] for x in xs], [abs(x["r"]) for x in xs])
            L.append(f"| {T} | {n} | {f1:.0%} | {f2:.0%} | {rhoFb[T]:.2f} |")
        n, t1, t2 = cov([abs(x["r"]) / x["sig"] for x in fbr])
        rhoU1 = spearman([x["sig"] for x in fbr], [abs(x["r"]) for x in fbr])
        L += [f"| U1 table | {n} | {t1:.0%} | {t2:.0%} | {rhoU1:.2f} |", "",
              "Held-out T choice: " + "; ".join(f"{k}: T={v[0]} → ±1σ {v[1][1]:.0%}, ±2σ {v[1][2]:.0%}" for k, v in best.items()), ""]
        Tsel = st.median([v[0] for v in best.values()])
        hc2 = min(v[1][2] for v in best.values()); hc1 = min(v[1][1] for v in best.values())
        fb_ok = hc2 >= t2 and hc1 >= 0.60 and rhoFb.get(Tsel, -1) > rhoU1
        verdict.append(("U5 forward–backward replaces the U1 table", fb_ok,
                        f"T={Tsel}: held-out ±2σ {hc2:.0%} vs U1 {t2:.0%}; Spearman {rhoFb.get(Tsel, float('nan')):.2f} vs {rhoU1:.2f}"))

    # 6. per-metric coverage against truth
    mc = {"impactShaftLean (P7 marks)": metric_cov(lean_rows(a.sigma, ids)),
          "attackAngle (GC Quad)": metric_cov(lm_rows(a.sigma, ids, "attackAngle", "lm.attackAngle")),
          "clubheadSpeed (GC Quad, scale removed)": metric_cov(lm_rows(a.sigma, ids, "clubheadSpeed", "lm.clubheadSpeed"), ratio=True)}
    L += ["## Metric σ against truth (sigma run; bias / scale removed and reported)", "",
          "| metric | n | ±1σ | ±2σ | bias (or scale) | median σ | median |err| | inflation to 68 % |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
    with open(os.path.join(od, "coverage_metrics.csv"), "w") as f:
        f.write("metric,n,cov1,cov2,bias,median_sigma,median_abs_err,inflate\n")
        for k, v in mc.items():
            if not v:
                L.append(f"| {k} | <5 | | | | | | |")
                continue
            L.append(f"| {k} | {v['n']} | {v['c1']:.0%} | {v['c2']:.0%} | {v['bias']:+.3f} | {v['medSig']:.2f} | {v['medAbsErr']:.2f} | {v['inflate']:.2f} |")
            for sw, e, gr in v["gross"]:
                L.append(f"| ↳ gross: {sw} | | | | {e:+.1f} | | | grossRisk {gr} |")
            f.write(f"{k},{v['n']},{v['c1']:.3f},{v['c2']:.3f},{v['bias']:.4f},{v['medSig']:.4f},{v['medAbsErr']:.4f},{v['inflate']:.3f}\n")
            if v["n"] < 30:
                verdict.append((f"{k} σ coverage", None, f"NOT GATED (n={v['n']} < 30): {v['c1']:.0%} / {v['c2']:.0%}; ships propagated"))
            else:
                verdict.append((f"{k} σ coverage", v["c1"] >= 0.60 and v["c2"] >= 0.90,
                                f"{v['c1']:.0%} / {v['c2']:.0%} (n={v['n']}); inflation {v['inflate']:.2f} → calibrated if 1.00"))
    L.append("")

    # 7. soft anchors
    if a.soft:
        e0, e1 = synth_err_by_region(a.sigma, ids), synth_err_by_region(a.soft, ids)
        p0, p1 = p7_theta_err(a.sigma, ids), p7_theta_err(a.soft, ids)
        worse = [r for r in ("P1-P4", "P4-P7", "P7-P8") if r in e0 and r in e1 and e1[r][1] > e0[r][1] + 1e-9]
        pw = [sw for sw in p0 if sw in p1 and p1[sw] > p0[sw] + 1e-9]
        p0m = st.median(p0.values()) if p0 else float("nan"); p1m = st.median(p1.values()) if p1 else float("nan")
        ok = not worse and p1m <= p0m + 1e-9
        L += ["## Soft anchors", "", "| region | n | off |median| | on |median| |", "|---|---:|---:|---:|"] + \
             [f"| {r} | {e0[r][0]} | {e0[r][1]:.2f}° | {e1.get(r, (0, float('nan')))[1]:.2f}° |" for r in sorted(e0)] + \
             ["", f"P7 θ error vs the marked P7: median {p0m:.2f}° off, {p1m:.2f}° on; {len(pw)} of {len(p0)} swings worse.", ""]
        mv = moved(a.sigma, a.soft, ids, ["impactShaftLean", "attackAngle", "clubheadSpeed", "lowPointAhead", "clubheadPeakLead"])
        L += ["Moved readings (swing, metric, off → on):", ""] + [f"- {sw} {k}: {x} → {y}" for sw, k, x, y in mv[:80]] + [""]
        verdict.append(("Soft anchors ON", ok, f"regions worse: {worse or 'none'}; P7 median {p0m:.2f}→{p1m:.2f}°"))
    # 8. one impact instant
    if a.oneimp:
        l0 = {r["swing"]: abs(r["r"]) for r in lean_rows(a.sigma, ids)}
        l1 = {r["swing"]: abs(r["r"]) for r in lean_rows(a.oneimp, ids)}
        worse = [(sw, l0[sw], l1[sw]) for sw in l0 if sw in l1 and l1[sw] > l0[sw] + 0.05]
        mv = moved(a.sigma, a.oneimp, ids, ["impactShaftLean", "clubheadSpeed", "clubheadPeakLead", "handSpeed"])
        L += ["## One impact instant", "", f"Lean |error| vs the P7 marks: {len(worse)} of {len(l0)} swings worse by > 0.05°.", ""] + \
             [f"- {sw}: {x:.2f}° → {y:.2f}°" for sw, x, y in worse] + ["", "Moved readings (swing, metric, off → on):", ""] + \
             [f"- {sw} {k}: {x} → {y}" for sw, k, x, y in mv[:80]] + [""]
        verdict.append(("oneImpact ON", not worse, f"{len(worse)} truth swings worse"))
    # 9. cost
    if a.dark:
        s0, t0 = walls(a.dark); s1, t1 = walls(a.sigma)
        rs = [s1[k] / s0[k] for k in s0 if k in s1 and s0[k] > 0]
        rt = [t1[k] / t0[k] for k in t0 if k in t1 and t0[k] > 0]
        ms, mt = (st.median(rs) - 1) if rs else float("nan"), (st.median(rt) - 1) if rt else float("nan")
        L += ["## Cost", "", f"Shaft stage: median {ms:+.1%} over {len(rs)} swings; whole analysis: median {mt:+.1%}.", ""]
        verdict.append(("Cost ≤ +10 % (shaft stage; whole analysis reported)", ms <= 0.10, f"{ms:+.1%} shaft, {mt:+.1%} total"))

    V = ["## Verdicts", "", "| gate | result | detail |", "|---|---|---|"] + \
        [f"| {g} | {'—' if ok is None else 'PASS' if ok else 'FAIL'} | {d} |" for g, ok, d in verdict] + [""]
    out = "\n".join(L[:5] + V + L[5:])
    fn = os.path.join(od, f"gates_{datetime.date.today().strftime('%Y%m%d')}.md")
    open(fn, "w").write(out)
    print(out)


if __name__ == "__main__":
    main()
