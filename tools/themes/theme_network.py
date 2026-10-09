"""Which of a golfer's measures move together DIRECTLY, and does the authored causal model agree?

The network half of the themes prototype (theme_pca.py is the other half). One golfer's sessions in,
a table of measure-to-measure links out, each tested three ways, then laid against the edges the
diagnostics pack (core.json) authors between conditions.

Method (kept simple enough to hand-port to C++):

  1. Table: theme_data.load_table, measures kept at >= MIN_COVERAGE of swings and non-zero spread.
     Values are session-centred (per-session median removed, pooled within-session MAD as the unit),
     so camera/club/day drift cannot make a link.
  2. Marginal association: Spearman rank r per pair over pairwise-complete swings, n per pair,
     p from Fisher z (sd = sqrt(1.06/(n-3)); numpy only, portable);
     pairs with n < MIN_N are not tested. Benjamini–Hochberg over the tested pairs.
  3. Direct association: partial correlation from a Ledoit–Wolf shrinkage covariance.
     NaN handling: a missing reading is imputed as 0 on the centred scale (= its session's median),
     after clipping centred values to +/-CLIP robust units. Imputation pulls covariances toward 0, so
     it can only hide a link, not invent one. Columns are standardised before shrinkage, so the
     target is the identity (a correlation-matrix shrinkage) and the partial r are scale-free.
  4. Stability: bootstrap of swings resampled WITHIN each session (sign agreement + percentile CI of
     the partial r), and leave-one-session-out partial r (the sign must hold every time).
     STABLE = BH q < Q and bootstrap sign agreement >= AGREE and LOSO sign kept and the partial r
     has the same sign as the Spearman r (a flip is a suppression artefact, not a link).
  5. Structural pairs (same metric at two windows, or one measure arithmetic of the other) are kept
     and flagged but never counted as insight.
  6. Direction: the phase each measure reads (latest P-position of its reducer) orders a stable link
     earlier -> later; same phase is undirected. Time order is necessary, not sufficient, for cause.
  7. Authored model: each `causes` edge between conditions maps to measure pairs through the
     conditions' signals (signal.measures[0] and its direction: high/low; else shape ceiling=high,
     floor=low; else either). "A causes B" predicts a MARGINAL association (a mediated path is
     conditioned away in the partial), so the comparison reads both:
       found_direct    partial link STABLE (as above), Spearman sign as the faults predict
       found_indirect  Spearman stable (q, within-session bootstrap sign >= AGREE, LOSO sign) but the
                       partial is not: they move together through other measures
       *_opposite_sign stable but against the fault directions
       not_found_absent  Spearman bootstrap CI inside +/-ABSENT_R: confidently no useful association
       not_found_unclear neither: too noisy to tell
       not_read / same_measure / found_structural
     Stable partial links with no authored edge are the candidate gaps ("unexpected").
     LW shrinkage (delta ~0.4 here) scales every partial r toward 0, so partial r are for sign and
     ranking, not for comparing to a fixed size; the Spearman r is the readable effect size.

    python3 -I tools/themes/theme_network.py                       # library + library-with-corpus
    python3 -I tools/themes/theme_network.py --sessions DIR... --out DIR
"""
from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import sys

import math

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from theme_data import centre_per_session, load_pack, load_table  # noqa: E402

MIN_COVERAGE = 0.6
MIN_N = 40
Q = 0.05
AGREE = 0.90
ABSENT_R = 0.3
CLIP = 5.0
N_BOOT = 1000

LIBRARY = "/mnt/swingdata/Mark-Liversedge/*/"
CORPUS_ROOT = "/mnt/swingdata/corpus/swings/"
# Corpus sessions that are NOT copies of library sessions (07-04 and 09-09 are).
CORPUS_EXTRA = ["2026-06-11_Mark-Liversedge_Wrist_01", "2026-07-03_Mark-Liversedge_Wrist_01",
                "2026-07-05_Mark-Liversedge_Wrist_02", "2026-07-08_Mark-Liversedge_Wrist_01",
                "2026-07-09_Mark-Liversedge_Wrist_01", "2026-07-10_Mark-Liversedge_Wrist_01",
                "2026-07-10_Mark-Liversedge_Wrist_02", "2026-08-18_Mark-Liversedge_Wrist_01",
                "2026-08-18_Mark-Liversedge_Wrist_02"]
DEFAULT_OUT = "/mnt/swingdata/scratch/themes-20261009/network"

# Swing position of each reducer anchor; theme_data leaves 'finish' and 'transition' at 0.
PHASE = {"p1": 1, "p2": 2, "p3": 3, "p4": 4, "transition": 4.5, "p5": 5, "p6": 6, "p7": 7,
         "p8": 8, "p9": 9, "p10": 10, "finish": 10}
# The hand-path loop is reduced at P4 but describes the hands' path through the transition.
WHEN_OVERRIDE = {"m_handPathLoop": 4.5}
# Measures that are arithmetic of others (x-factor = chest turn - pelvis turn around P4-P5;
# the P6-P7 pelvis rate is the difference of pelvis turn over that window).
ARITHMETIC = {
    "m_xFactorStretch": {"m_thoraxRotP4", "m_pelvisRotP4", "m_pelvisRotP5"},
    "m_pelvisRotRateP6P7": {"m_pelvisRotP7", "m_pelvisRotP5"},
}


# ------------------------------------------------------------------ statistics (portable)

def rank_avg(x):
    """Ranks 1..n with ties given their average rank (sort, then average each run of equals)."""
    _, inv, cnt = np.unique(x, return_inverse=True, return_counts=True)
    return (np.cumsum(cnt) - (cnt - 1) / 2.0)[inv]


def spearman(x, y):
    """Spearman r and two-sided p (Fisher z with the Fieller 1.06 variance factor; no scipy)."""
    n = len(x)
    rx, ry = rank_avg(x), rank_avg(y)
    rx -= rx.mean()
    ry -= ry.mean()
    den = math.sqrt(float(np.sum(rx * rx) * np.sum(ry * ry)))
    if den == 0 or n < 4:
        return float("nan"), float("nan")
    r = float(np.sum(rx * ry)) / den
    z = math.atanh(max(min(r, 0.999999), -0.999999)) * math.sqrt((n - 3) / 1.06)
    return r, math.erfc(abs(z) / math.sqrt(2.0))


def spearman_pairs(Z):
    """Pairwise-complete Spearman r, two-sided p and n for every column pair."""
    p = Z.shape[1]
    R, P, N = np.full((p, p), np.nan), np.full((p, p), np.nan), np.zeros((p, p), int)
    ok = np.isfinite(Z)
    for i in range(p):
        for j in range(i + 1, p):
            m = ok[:, i] & ok[:, j]
            n = int(m.sum())
            N[i, j] = N[j, i] = n
            if n < 4:
                continue
            r, pv = spearman(Z[m, i], Z[m, j])
            R[i, j] = R[j, i] = r
            P[i, j] = P[j, i] = pv
    return R, P, N


def bh(pvals):
    """Benjamini–Hochberg q-values (NaN in, NaN out)."""
    p = np.asarray(pvals, float)
    q = np.full_like(p, np.nan)
    ok = np.isfinite(p)
    v = p[ok]
    m = len(v)
    if m == 0:
        return q
    order = np.argsort(v)
    ranked = v[order] * m / np.arange(1, m + 1)
    ranked = np.minimum.accumulate(ranked[::-1])[::-1]
    out = np.empty(m)
    out[order] = np.minimum(ranked, 1.0)
    q[ok] = out
    return q


def ledoit_wolf(X):
    """Ledoit–Wolf (2004) shrinkage toward mu*I. X: n x p, columns already centred.
    Returns (Sigma, delta)."""
    n, p = X.shape
    S = X.T @ X / n
    mu = np.trace(S) / p
    F = mu * np.eye(p)
    d2 = np.sum((S - F) ** 2)
    row_sq = np.sum(X ** 2, axis=1)
    b2bar = np.sum(row_sq ** 2) / n ** 2 - np.sum(S ** 2) / n
    b2 = min(max(b2bar, 0.0), d2)
    delta = b2 / d2 if d2 > 0 else 1.0
    return delta * F + (1 - delta) * S, delta


def partial_corr(Zf):
    """Partial correlations from a filled (no-NaN) matrix: standardise, shrink, invert."""
    X = Zf - Zf.mean(axis=0)
    sd = X.std(axis=0)
    sd[sd == 0] = 1.0
    X = X / sd
    Sig, delta = ledoit_wolf(X)
    Pm = np.linalg.inv(Sig)
    d = np.sqrt(np.diag(Pm))
    Rp = -Pm / np.outer(d, d)
    np.fill_diagonal(Rp, 1.0)
    return Rp, delta


def fill(Z):
    """Clip to +/-CLIP robust units and impute missing as 0 (the session median)."""
    F = np.clip(Z, -CLIP, CLIP)
    F[~np.isfinite(F)] = 0.0
    return F


# ------------------------------------------------------------------ pack / model

def when_of(mid, meta):
    if mid in WHEN_OVERRIDE:
        return WHEN_OVERRIDE[mid]
    r = meta.get("reducer") or {}
    ph = [x for x in [r.get("anchor")] + list(r.get("window") or []) if x]
    return max((PHASE.get(x, 0) for x in ph), default=0)


def structural(a, b, meta):
    ka = (meta[a].get("metricKey") or "").replace("Signed", "")
    kb = (meta[b].get("metricKey") or "").replace("Signed", "")
    if ka and ka == kb:
        return f"same metric ({ka})"
    if b in ARITHMETIC.get(a, ()) or a in ARITHMETIC.get(b, ()):
        return "arithmetic"
    return ""


def condition_measures(pack):
    """condition id -> [(measure id, fault direction 'high'/'low'/'either')] via its signals."""
    sig = {s["id"]: s for s in pack["signals"]}
    shape = {m["id"]: m.get("shape", "") for m in pack["measures"]}
    out = {}
    for c in pack["conditions"]:
        lst = []
        for sid in c.get("detectedBy", []):
            s = sig.get(sid)
            if not s or not s.get("measures"):
                continue
            mid = s["measures"][0]
            d = s.get("direction")
            if d not in ("high", "low"):
                d = {"ceiling": "high", "floor": "low"}.get(shape.get(mid, ""), "either")
            lst.append((mid, d))
        out[c["id"]] = lst
    return out


def authored_relations(pack, cm):
    """(measure a, measure b) unordered -> set of relation tags from the authored edges."""
    causes = {}
    for e in pack["edges"]:
        if e.get("type") == "causes":
            causes.setdefault(e["from"], set()).add(e["to"])
    rel = {}

    def tag(ca, cb, t):
        for ma, _ in cm.get(ca, []):
            for mb, _ in cm.get(cb, []):
                if ma != mb:
                    rel.setdefault(frozenset((ma, mb)), set()).add(t)

    for e in pack["edges"]:
        tag(e["from"], e["to"], e.get("type", "?"))
    for a, bs in causes.items():          # two-step causal paths a -> x -> c
        for x in bs:
            for c in causes.get(x, ()):
                if c != a:
                    tag(a, c, "causes-2step")
    return rel


# ------------------------------------------------------------------ the analysis

def analyse(session_dirs, n_boot=N_BOOT, seed=1, pack=None, n_boot_sp=500):
    pack = pack or load_pack()
    t = load_table(session_dirs, pack=pack)
    cov = np.mean(np.isfinite(t.values), axis=0)
    Z_all = centre_per_session(t.values, t.session)
    spread_ok = np.isfinite(np.nanmax(np.abs(Z_all), axis=0)) & (np.nanstd(Z_all, axis=0) > 0)
    keep = (cov >= MIN_COVERAGE) & spread_ok
    mids = [m for m, k in zip(t.measures, keep) if k]
    Z = Z_all[:, keep]
    meta = {m: t.meta[m] for m in mids}
    sess = t.session
    p = len(mids)

    R, P, N = spearman_pairs(Z)
    iu = np.triu_indices(p, 1)
    tested = N[iu] >= MIN_N
    pv = np.where(tested, P[iu], np.nan)
    qv = bh(pv)
    Qm = np.full((p, p), np.nan)
    Qm[iu] = qv
    Qm.T[iu] = qv

    F = fill(Z)
    Rp, delta = partial_corr(F)

    rng = np.random.default_rng(seed)
    groups = [np.flatnonzero(sess == s) for s in np.unique(sess)]
    boots = np.empty((n_boot, len(iu[0])))
    for b in range(n_boot):
        idx = np.concatenate([g[rng.integers(0, len(g), len(g))] for g in groups])
        boots[b] = partial_corr(F[idx])[0][iu]
    point = Rp[iu]
    agree = np.mean(np.sign(boots) == np.sign(point), axis=0)
    lo, hi = np.percentile(boots, [2.5, 97.5], axis=0)

    loso = []
    for s in np.unique(sess):
        m = sess != s
        loso.append(partial_corr(F[m])[0][iu])
    loso = np.array(loso)
    loso_ok = np.all(np.sign(loso) == np.sign(point), axis=0)

    # Marginal (Spearman) stability, for the authored-model comparison: an authored "A causes B"
    # predicts a marginal association (a mediated path is conditioned away in the partial).
    pairs = list(zip(*iu))
    sp_boot = np.full((n_boot_sp, len(pairs)), np.nan)
    for b in range(n_boot_sp):
        idx = np.concatenate([g[rng.integers(0, len(g), len(g))] for g in groups])
        Zb = Z[idx]
        okb = np.isfinite(Zb)
        for k, (i, j) in enumerate(pairs):
            if not tested[k]:
                continue
            m = okb[:, i] & okb[:, j]
            sp_boot[b, k] = spearman(Zb[m, i], Zb[m, j])[0]
    sp_point = R[iu]
    sp_lo, sp_hi = np.nanpercentile(sp_boot, [2.5, 97.5], axis=0)
    sp_agree = np.nanmean(np.sign(sp_boot) == np.sign(sp_point), axis=0)
    sp_loso = np.full((len(groups), len(pairs)), np.nan)
    for gi, s in enumerate(np.unique(sess)):
        keep_rows = sess != s
        Zl = Z[keep_rows]
        okl = np.isfinite(Zl)
        for k, (i, j) in enumerate(pairs):
            m = okl[:, i] & okl[:, j]
            if m.sum() >= 10:
                sp_loso[gi, k] = spearman(Zl[m, i], Zl[m, j])[0]
    sp_loso_ok = np.all((np.sign(sp_loso) == np.sign(sp_point)) | ~np.isfinite(sp_loso), axis=0)

    # Within-session Spearman sign, sessions with >= 10 complete pairs (descriptive only).
    sess_sign = []
    for k, (i, j) in enumerate(zip(*iu)):
        agreeing = total = 0
        for g in groups:
            zi, zj = Z[g, i], Z[g, j]
            m = np.isfinite(zi) & np.isfinite(zj)
            if m.sum() >= 10 and np.std(zi[m]) > 0 and np.std(zj[m]) > 0:
                r = spearman(zi[m], zj[m])[0]
                total += 1
                agreeing += int(np.sign(r) == np.sign(point[k]))
        sess_sign.append((agreeing, total))

    cm = condition_measures(pack)
    rel = authored_relations(pack, cm)
    when = {m: when_of(m, meta[m]) for m in mids}

    links = []
    for k, (i, j) in enumerate(zip(*iu)):
        a, b = mids[i], mids[j]
        st = structural(a, b, meta)
        q = Qm[i, j]
        flip = np.isfinite(R[i, j]) and np.sign(R[i, j]) != np.sign(point[k])
        stable = bool(np.isfinite(q) and q < Q and agree[k] >= AGREE and loso_ok[k] and not flip)
        wa, wb = when[a], when[b]
        if not wa or not wb or wa == wb:
            direction = "undirected"
        else:
            direction = f"{a} -> {b}" if wa < wb else f"{b} -> {a}"
        rtags = sorted(rel.get(frozenset((a, b)), ()))
        if "causes" in rtags:
            auth = "authored"
        elif rtags:
            auth = "authored-" + "+".join(rtags)
        else:
            auth = "none"
        links.append({
            "a": a, "b": b, "label_a": meta[a].get("label") or a, "label_b": meta[b].get("label") or b,
            "when_a": wa, "when_b": wb, "n": int(N[i, j]),
            "spearman_r": R[i, j], "p": P[i, j], "q": q,
            "partial_r": point[k], "ci_lo": lo[k], "ci_hi": hi[k], "boot_agree": agree[k],
            "loso_ok": bool(loso_ok[k]), "loso_partials": " ".join(f"{x:+.2f}" for x in loso[:, k]),
            "session_sign": f"{sess_sign[k][0]}/{sess_sign[k][1]}",
            "sign_flip": bool(flip), "structural": st, "stable": stable,
            "insight": stable and not st, "direction": direction if stable else "",
            "authored": auth,
            "sp_ci_lo": sp_lo[k], "sp_ci_hi": sp_hi[k], "sp_boot_agree": sp_agree[k],
            "sp_loso_ok": bool(sp_loso_ok[k]),
            "marginal_stable": bool(np.isfinite(q) and q < Q and sp_agree[k] >= AGREE and sp_loso_ok[k]),
        })

    lk = {frozenset((L["a"], L["b"])): L for L in links}
    comparison = compare_model(pack, cm, lk, set(mids))

    # Candidate gaps: stable, non-structural, no authored causes edge between their conditions.
    for L in links:
        L["classification"] = ""
        if L["insight"]:
            L["classification"] = "found_authored" if L["authored"] == "authored" else "unexpected"
    for c in comparison:
        L = lk.get(frozenset((c["measure_from"], c["measure_to"])))
        if L is not None and c["class"].startswith("found") and L["authored"] == "authored":
            L["classification"] = c["class"]

    # Data checks: clipped readings per measure and per session, and which session moves a link.
    clipped = {m: int(np.sum(np.abs(Z[:, j]) > CLIP)) for j, m in enumerate(mids)}
    infl = {}
    for k, L in enumerate(links):
        if L["stable"]:
            d = np.abs(loso[:, k] - point[k])
            infl[f"{L['a']}|{L['b']}"] = t.sessions[int(np.argmax(d))]
    info = {
        "sessions": [{"name": s, "swings": int(np.sum(sess == i))} for i, s in enumerate(t.sessions)],
        "swings": len(t.swings), "measures_kept": mids,
        "measures_dropped": [m for m, k in zip(t.measures, keep) if not k],
        "ledoit_wolf_delta": delta, "n_boot": n_boot, "pairs": len(links),
        "pairs_tested": int(np.sum(tested)), "pairs_fdr": int(np.sum(qv < Q)),
        "stable": sum(L["stable"] for L in links),
        "stable_structural": sum(L["stable"] and bool(L["structural"]) for L in links),
        "stable_insight": sum(L["insight"] for L in links),
        "clipped_readings": {m: v for m, v in clipped.items() if v},
        "most_influential_session": infl,
    }
    return links, comparison, info, meta


def compare_model(pack, cm, lk, kept):
    conds = {c["id"]: c for c in pack["conditions"]}
    rows = []
    for e in pack["edges"]:
        if e.get("type") != "causes":
            continue
        for ma, da in cm.get(e["from"], []):
            for mb, db in cm.get(e["to"], []):
                exp = "either" if "either" in (da, db) else ("+" if da == db else "-")
                row = {"from": e["from"], "to": e["to"], "strength": e.get("strength", ""),
                       "from_kind": conds.get(e["from"], {}).get("kind", ""),
                       "to_kind": conds.get(e["to"], {}).get("kind", ""),
                       "measure_from": ma, "fault_from": da, "measure_to": mb, "fault_to": db,
                       "expected_sign": exp}
                L = lk.get(frozenset((ma, mb)))
                if ma == mb:
                    cls = "same_measure"
                elif ma not in kept or mb not in kept or L is None or L["n"] < MIN_N:
                    cls = "not_read"
                else:
                    for f in ("n", "spearman_r", "q", "sp_ci_lo", "sp_ci_hi", "sp_boot_agree",
                              "sp_loso_ok", "marginal_stable", "partial_r", "ci_lo", "ci_hi",
                              "boot_agree", "loso_ok", "structural", "stable"):
                        row[f] = L[f]
                    sgn = "+" if L["spearman_r"] > 0 else "-"
                    row["observed_sign"] = sgn
                    sign_ok = exp in ("either", sgn)
                    if L["structural"] and (L["stable"] or L["marginal_stable"]):
                        cls = "found_structural"
                    elif L["stable"]:
                        cls = "found_direct" if sign_ok else "found_direct_opposite_sign"
                    elif L["marginal_stable"]:
                        cls = "found_indirect" if sign_ok else "found_indirect_opposite_sign"
                    elif L["sp_ci_lo"] > -ABSENT_R and L["sp_ci_hi"] < ABSENT_R:
                        cls = "not_found_absent"
                    else:
                        cls = "not_found_unclear"
                row["class"] = cls
                rows.append(row)
    return rows


# ------------------------------------------------------------------ output

def _fmt(v):
    if isinstance(v, (float, np.floating)):
        return "" if not np.isfinite(v) else f"{v:.4g}"
    return v


def write_csv(path, rows, cols):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for r in rows:
            w.writerow([_fmt(r.get(c, "")) for c in cols])


LINK_COLS = ["a", "b", "label_a", "label_b", "when_a", "when_b", "n", "spearman_r", "p", "q",
             "partial_r", "ci_lo", "ci_hi", "boot_agree", "loso_ok", "loso_partials",
             "session_sign", "sign_flip", "structural", "stable", "insight", "direction",
             "sp_ci_lo", "sp_ci_hi", "sp_boot_agree", "sp_loso_ok", "marginal_stable",
             "authored", "classification"]
CMP_COLS = ["from", "to", "strength", "from_kind", "to_kind", "measure_from", "fault_from",
            "measure_to", "fault_to", "expected_sign", "class", "observed_sign", "n", "spearman_r",
            "q", "sp_ci_lo", "sp_ci_hi", "sp_boot_agree", "sp_loso_ok", "marginal_stable", "partial_r", "ci_lo", "ci_hi", "boot_agree", "loso_ok", "structural", "stable"]

FOCUS = ["over_the_top", "early_extension", "pelvis_thrust_backswing", "chicken_wing",
         "reverse_spine_p4", "trail_knee_straighten", "hips_closed_at_impact",
         "off_balance_finish", "sway"]
FOCUS_MEASURES = ["m_handPathLoop", "m_pelvisThrustDown", "m_spineBendLossDown",
                  "m_pelvisThrustBack", "m_leadArmToTorso", "m_axisTiltAtTop", "m_trailKneeFlex",
                  "m_pelvisRotP7", "m_comOverLeadFootFinish", "m_pelvisSwayBack"]


def summarise(links, comparison, info):
    from collections import Counter
    ins = sorted((L for L in links if L["insight"]), key=lambda L: -abs(L["partial_r"]))
    focus_pairs = []
    fm = set(FOCUS_MEASURES)
    for L in links:
        if L["a"] in fm and L["b"] in fm:
            focus_pairs.append({k: _fmt(L[k]) for k in ("a", "b", "n", "spearman_r", "q", "partial_r",
                                                         "ci_lo", "ci_hi", "boot_agree", "loso_ok",
                                                         "stable", "sp_ci_lo", "sp_ci_hi",
                                                         "marginal_stable", "structural", "authored")})
    focus_edges = [{k: _fmt(c.get(k, "")) for k in CMP_COLS} for c in comparison
                   if c["from"] in FOCUS and c["to"] in FOCUS]
    readable = [c for c in comparison if c["class"] not in ("not_read", "same_measure")]
    return {
        "info": info,
        "stable_insight_links": [
            {"a": L["a"], "b": L["b"], "label_a": L["label_a"], "label_b": L["label_b"],
             "partial_r": round(L["partial_r"], 3), "ci": [round(L["ci_lo"], 3), round(L["ci_hi"], 3)],
             "spearman_r": round(L["spearman_r"], 3), "q": float(f"{L['q']:.3g}"),
             "direction": L["direction"], "authored": L["authored"],
             "classification": L["classification"]} for L in ins],
        "stable_structural_links": [f"{L['a']} ~ {L['b']} ({L['structural']}, {L['partial_r']:+.2f})"
                                    for L in links if L["stable"] and L["structural"]],
        "model_comparison_counts": dict(Counter(c["class"] for c in comparison)),
        "model_comparison_readable_counts": dict(Counter(c["class"] for c in readable)),
        "focus_pairs": focus_pairs,
        "focus_authored_edges": focus_edges,
    }


def run(session_dirs, out, n_boot=N_BOOT, seed=1, quiet=False):
    os.makedirs(out, exist_ok=True)
    links, comparison, info, _ = analyse(session_dirs, n_boot=n_boot, seed=seed)
    write_csv(os.path.join(out, "links.csv"), links, LINK_COLS)
    write_csv(os.path.join(out, "model_comparison.csv"), comparison, CMP_COLS)
    summ = summarise(links, comparison, info)
    with open(os.path.join(out, "summary.json"), "w") as f:
        json.dump(summ, f, indent=1, default=float)
    if not quiet:
        print(f"{out}: {info['swings']} swings, {len(info['measures_kept'])} measures, "
              f"{info['pairs_tested']} pairs tested, {info['pairs_fdr']} pass FDR, "
              f"{info['stable']} stable ({info['stable_structural']} structural), "
              f"{info['stable_insight']} insight; LW delta {info['ledoit_wolf_delta']:.2f}")
        for L in summ["stable_insight_links"]:
            print(f"  {L['partial_r']:+.2f} [{L['ci'][0]:+.2f},{L['ci'][1]:+.2f}]  "
                  f"{L['label_a'] or L['a']}  ~  {L['label_b'] or L['b']}   ({L['direction']}; {L['authored']})")
        print("  model:", summ["model_comparison_readable_counts"])
    return links, comparison, summ


def sensitivity(lib_links, both_links):
    """How the stable set moves when the extra corpus sessions are added."""
    key = lambda L: frozenset((L["a"], L["b"]))
    a = {key(L): L for L in lib_links}
    b = {key(L): L for L in both_links}
    common = set(a) & set(b)
    sa = {k for k in common if a[k]["insight"]}
    sb = {k for k in common if b[k]["insight"]}
    nm = lambda k: " ~ ".join(sorted(k))
    return {
        "common_pairs": len(common),
        "insight_library_only": sorted(nm(k) for k in sa - sb),
        "insight_with_corpus_only": sorted(nm(k) for k in sb - sa),
        "insight_both": sorted(nm(k) for k in sa & sb),
        "library_insight_sign_kept_with_corpus": sum(
            np.sign(a[k]["partial_r"]) == np.sign(b[k]["partial_r"]) for k in sa),
        "library_insight_pairs_dropped_from_corpus_table": sorted(
            nm(k) for k in {key(L) for L in lib_links if L["insight"]} - set(b)),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sessions", nargs="*", help="session dirs (default: library, then +corpus)")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--boot", type=int, default=N_BOOT)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    if args.sessions:
        run(args.sessions, args.out, args.boot, args.seed)
        return
    lib = sorted(glob.glob(LIBRARY))
    extra = [os.path.join(CORPUS_ROOT, s) for s in CORPUS_EXTRA if os.path.isdir(os.path.join(CORPUS_ROOT, s))]
    l1, _, s1 = run(lib, args.out, args.boot, args.seed)
    l2, _, s2 = run(lib + extra, os.path.join(args.out, "with_corpus"), args.boot, args.seed)
    sens = sensitivity(l1, l2)
    s1["corpus_sensitivity"] = sens
    with open(os.path.join(args.out, "summary.json"), "w") as f:
        json.dump(s1, f, indent=1, default=float)
    print("corpus sensitivity:", json.dumps(sens, indent=1, default=float))


if __name__ == "__main__":
    main()
