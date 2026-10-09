"""Themes in one golfer's swings, from first principles: which measures rise and fall TOGETHER.

A theme is a group of measures that co-vary from swing to swing once session drift is removed —
"when the pelvis thrusts toward the ball the chest also stands up and the head rises". Two
independent methods look for them, and only what both find is trusted:

  1. PCA on the correlation matrix, components kept by parallel analysis, varimax-rotated.
  2. Average-linkage clustering on 1 - |Spearman rho| (pairwise-complete).

Then the themes are stress-tested: bootstrap (swings resampled within session), leave-one-session-
out, collapsing structural families (one metricKey read in several windows), and adding the corpus
sessions that are not copies of library sessions.

Method choices (all small enough to port to hand-written C++):
  * Orientation: + = more of the fault. Measure `shape` decides first (ceiling: +1, floor: -1);
    else, if every signal on the measure watches one tail, that tail is the fault (+1 high,
    -1 low); else `unwatchedTail` names the benign tail; else the measure is two-sided, raw sign
    kept and marked.
  * Centring: per-session median, pooled within-session MAD (theme_data.centre_per_session), then
    winsorised at +-4 robust z so one mis-tracked swing cannot make a component.
  * Missing values: EM-PCA imputation (iterate: fill NaN from a rank-k reconstruction of the
    standardised matrix, re-fit, until the fill stops moving). Checked against zero-fill (= the
    session median on the centred scale) by Tucker congruence of the rotated loadings.
  * Retention: parallel analysis on pairwise-complete correlations (observed vs column-shuffled,
    95th percentile, 500 shuffles) — imputed cells never enter the retention decision.
  * Rotation: Kaiser varimax. Components are signed so their largest loading is positive.
  * Matching across resamples: Hungarian assignment on |congruence|.
  * numpy only, so it runs under `python3 -I`; the PNG is drawn only if matplotlib imports.

    python3 -I tools/themes/theme_pca.py [--out DIR] [--boot 500]
"""
from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from theme_data import centre_per_session, load_pack, load_table  # noqa: E402

LIBRARY = "/mnt/swingdata/Mark-Liversedge"
CORPUS = "/mnt/swingdata/corpus/swings"
# Corpus sessions that are NOT copies of library sessions (07-04 and 09-09 are).
CORPUS_EXTRA = ["2026-06-11_*", "2026-07-03_*", "2026-07-05_*", "2026-07-08_*", "2026-07-09_*",
                "2026-07-10_*", "2026-08-18_*"]
OUT = "/mnt/swingdata/scratch/themes-20261009/themes"

MIN_COVERAGE = 0.6
MIN_ROW_COVERAGE = 0.5   # a swing with most measures unread is a capture failure, not a pattern
WINSOR = 4.0
LOAD_MIN = 0.4
STABLE = 0.85

# Golfer-facing phrase per (metricKey, raw direction), in the anatomy vocabulary's body words
# (hips = pelvis, chest = thorax). Only used to PROPOSE names; wording is settled later.
PHRASE = {
    ("pelvisSway", "high"): "Your hips slide toward the target",
    ("pelvisSway", "low"): "Your hips sway away from the target",
    ("leadKneeDrift", "low"): "Your lead knee folds in",
    ("pelvisThrust", "high"): "Your hips push toward the ball",
    ("pelvisThrust", "low"): "Your hips back away from the ball",
    ("pelvisLift", "high"): "Your hips rise going back",
    ("pelvisRotation", "low"): "Your hips turn too little",
    ("pelvisRotation", "high"): "Your hips open early",
    ("pelvisRotationSigned", "low"): "Your hips stop turning into the ball",
    ("spineForwardBend", "high"): "You bend over more",
    ("spineForwardBend", "low"): "You stand up through the ball",
    ("secondaryAxisTilt", "high"): "You tilt away from the target",
    ("secondaryAxisTilt", "low"): "You lean toward the target",
    ("headLift", "high"): "Your head comes up",
    ("headLift", "low"): "Your head dips",
    ("headSway", "low"): "Your head moves off the ball",
    ("headSway", "high"): "Your head drifts toward the target",
    ("thoraxRotation", "low"): "Your shoulder turn is short",
    ("thoraxRotation", "high"): "Your shoulders over-turn",
    ("thoraxLateralDrift", "high"): "Your chest lunges toward the target",
    ("thoraxLateralDrift", "low"): "Your chest hangs back",
    ("leadArmToTorso", "high"): "Your lead arm folds after impact",
    ("comOverLeadFoot", "high"): "You can't hold your finish",
    ("leadHandWidth", "low"): "Your arms get narrow at the top",
    ("trailForearmAngle", "high"): "Your trail elbow flies",
    ("trailKneeFlexion", "low"): "Your trail knee straightens",
    ("leadKneeFlexion", "high"): "Your lead knee bends more",
    ("handPathLoop", "high"): "Your hands loop over the top",
    ("lagAngle", "low"): "You throw the club early",
    ("tempoRatio", "low"): "You rush the change of direction",
    ("clubheadPeakLead", "high"): "The club peaks too soon",
    ("leadUpperArmToChest", "high"): "Your lead arm leaves your chest",
    ("ballPosition", "high"): "The ball sits back in your stance",
    ("stanceWidth", "low"): "Your stance is narrow",
}


# ── preparation ──────────────────────────────────────────────────────────────────────────────────
def orientation(pack: dict) -> dict:
    """measure id -> (sign, how). + = more of the fault after multiplying by sign."""
    sig_dirs = {}
    for s in pack["signals"]:
        for mid in s.get("measures", [])[:1]:
            if s.get("direction"):
                sig_dirs.setdefault(mid, set()).add(s["direction"])
    out = {}
    for m in pack["measures"]:
        shape, dirs = m.get("shape"), sig_dirs.get(m["id"], set())
        if shape == "ceiling":
            out[m["id"]] = (1.0, "ceiling")
        elif shape == "floor":
            out[m["id"]] = (-1.0, "floor")
        elif dirs == {"high"}:
            out[m["id"]] = (1.0, "signal:high")
        elif dirs == {"low"}:
            out[m["id"]] = (-1.0, "signal:low")
        elif m.get("unwatchedTail") == "high":
            out[m["id"]] = (-1.0, "unwatched:high")
        elif m.get("unwatchedTail") == "low":
            out[m["id"]] = (1.0, "unwatched:low")
        else:
            out[m["id"]] = (1.0, "twoSided")
    return out


def drop_sparse_swings(t, min_cov=MIN_COVERAGE, min_row=MIN_ROW_COVERAGE):
    """Remove swings that read under `min_row` of the well-covered measures. One such swing in
    2026-10-05 (87 % unread) alone made the first principal component when kept."""
    from theme_data import Table
    ok = np.isfinite(t.values)
    cols = np.mean(ok, axis=0) >= min_cov
    keep = np.mean(ok[:, cols], axis=1) >= min_row
    dropped = [t.swings[i] for i in np.where(~keep)[0]]
    t2 = Table(t.values[keep], t.session[keep], t.sessions, [s for s, k in zip(t.swings, keep) if k],
               t.measures, t.meta, {c: v[keep] for c, v in t.fired.items()})
    return t2, dropped


def prepare(table, orient, min_cov=MIN_COVERAGE):
    """Oriented, session-centred, robust-scaled, winsorised matrix; measures kept; the scale."""
    X = table.values.copy()
    signs = np.array([orient.get(m, (1.0, "twoSided"))[0] for m in table.measures])
    X = X * signs
    cov = np.mean(np.isfinite(X), axis=0)
    Z = centre_per_session(X, table.session)
    keep = (cov >= min_cov) & np.isfinite(np.nanstd(Z, axis=0)) & (np.nanstd(Z, axis=0) > 0)
    # pooled within-session spread on the oriented raw scale (for the uncentred projection)
    dev = np.full_like(X, np.nan)
    for s in np.unique(table.session):
        r = table.session == s
        dev[r] = X[r] - np.nanmedian(X[r], axis=0)
    spread = 1.4826 * np.nanmedian(np.abs(dev), axis=0)
    Z = np.clip(Z[:, keep], -WINSOR, WINSOR)
    measures = [m for m, k in zip(table.measures, keep) if k]
    return Z, measures, X[:, keep], spread[keep]


def em_impute(Z, k=3, iters=200, tol=1e-6):
    """Fill NaN from a rank-k reconstruction of the column-standardised matrix, iterated."""
    miss = ~np.isfinite(Z)
    F = np.where(miss, 0.0, Z)
    for _ in range(iters):
        mu, sd = F.mean(0), F.std(0)
        sd[sd == 0] = 1
        S = (F - mu) / sd
        U, s, Vt = np.linalg.svd(S, full_matrices=False)
        R = (U[:, :k] * s[:k]) @ Vt[:k] * sd + mu
        new = np.where(miss, R, Z)
        if np.max(np.abs(new - F)) < tol:
            F = new
            break
        F = new
    return F


def corr(F):
    C = np.corrcoef(F, rowvar=False)
    return np.nan_to_num(C)


def corr_pairwise(Z):
    """Pearson over the swings where both measures were read (no imputation)."""
    M = np.isfinite(Z).astype(float)
    X = np.where(M > 0, Z, 0.0)
    n = M.T @ M
    sx = X.T @ M                  # sx[i, j] = sum of x_i over rows where i and j both read
    sxx = (X * X).T @ M
    sxy = X.T @ X
    num = n * sxy - sx * sx.T
    den = np.sqrt(np.maximum(n * sxx - sx ** 2, 0) * np.maximum(n * sxx.T - sx.T ** 2, 0))
    with np.errstate(invalid="ignore", divide="ignore"):
        C = num / den
    C[n < 10] = 0
    C = np.nan_to_num(C)
    np.fill_diagonal(C, 1.0)
    return C


def eig_desc(C):
    w, V = np.linalg.eigh(C)
    o = np.argsort(w)[::-1]
    return w[o], V[:, o]


def parallel_analysis(Z, n=500, q=95, rng=None):
    """On PAIRWISE-COMPLETE correlations of the raw (NaN-bearing) matrix, so imputed cells cannot
    lend structure to the observed eigenvalues. Columns shuffled with their NaNs."""
    rng = rng or np.random.default_rng(0)
    w, _ = eig_desc(corr_pairwise(Z))
    sims = np.empty((n, Z.shape[1]))
    for i in range(n):
        P = np.column_stack([rng.permutation(Z[:, j]) for j in range(Z.shape[1])])
        sims[i] = eig_desc(corr_pairwise(P))[0]
    thr = np.percentile(sims, q, axis=0)
    k = 0
    while k < len(w) and w[k] > thr[k]:
        k += 1
    return max(k, 1), w, thr


def varimax(L, gamma=1.0, iters=500, tol=1e-8):
    """Kaiser-normalised varimax. Returns rotated loadings."""
    h = np.sqrt(np.sum(L ** 2, axis=1))
    h[h == 0] = 1
    A = L / h[:, None]
    p, k = A.shape
    R = np.eye(k)
    d = 0
    for _ in range(iters):
        B = A @ R
        u, s, vt = np.linalg.svd(A.T @ (B ** 3 - (gamma / p) * B @ np.diag(np.sum(B ** 2, axis=0))))
        R = u @ vt
        d_new = np.sum(s)
        if d_new < d * (1 + tol):
            break
        d = d_new
    return (A @ R) * h[:, None]


def fit(Z, k=None, impute="em", pa_n=500, rng=None):
    """-> dict(k, L (rotated, signed, ordered), eig, thr, F (completed), varshare)."""
    thr = None
    if k is None:
        k, w, thr = parallel_analysis(Z, n=pa_n, rng=rng)
    F = em_impute(Z, k=k) if impute == "em" else np.where(np.isfinite(Z), Z, 0.0)
    w, V = eig_desc(corr(F))
    L = V[:, :k] * np.sqrt(np.maximum(w[:k], 0))
    Lr = varimax(L) if k > 1 else L
    for j in range(k):
        if Lr[np.argmax(np.abs(Lr[:, j])), j] < 0:
            Lr[:, j] *= -1
    ss = np.sum(Lr ** 2, axis=0)
    o = np.argsort(ss)[::-1]
    Lr, ss = Lr[:, o], ss[o]
    return {"k": k, "L": Lr, "eig": w, "thr": thr, "F": F, "varshare": ss / Z.shape[1]}


def congruence(a, b):
    return float(a @ b / np.sqrt((a @ a) * (b @ b)))


def hungarian_max(A):
    """Maximum-weight assignment of rows to columns (Kuhn-Munkres, O(n^3)); rectangular OK.
    Returns [(row, col)]. Hand-portable: the classic potentials formulation."""
    n0, m0 = A.shape
    n = max(n0, m0)
    cost = np.zeros((n, n))
    cost[:n0, :m0] = -A
    INF = 1e18
    u, v = np.zeros(n + 1), np.zeros(n + 1)
    p, way = np.zeros(n + 1, dtype=int), np.zeros(n + 1, dtype=int)
    for i in range(1, n + 1):
        p[0], j0 = i, 0
        minv, used = np.full(n + 1, INF), np.zeros(n + 1, dtype=bool)
        while True:
            used[j0] = True
            i0, delta, j1 = p[j0], INF, 0
            for j in range(1, n + 1):
                if not used[j]:
                    cur = cost[i0 - 1, j - 1] - u[i0] - v[j]
                    if cur < minv[j]:
                        minv[j], way[j] = cur, j0
                    if minv[j] < delta:
                        delta, j1 = minv[j], j
            for j in range(n + 1):
                if used[j]:
                    u[p[j]] += delta
                    v[j] -= delta
                else:
                    minv[j] -= delta
            j0 = j1
            if p[j0] == 0:
                break
        while j0:
            j1 = way[j0]
            p[j0] = p[j1]
            j0 = j1
    return [(p[j] - 1, j - 1) for j in range(1, n + 1) if p[j] - 1 < n0 and j - 1 < m0]


def match(Lref, L):
    """Best assignment of L's columns to Lref's; returns congruence per ref column (signed fixed)."""
    k1, k2 = Lref.shape[1], L.shape[1]
    C = np.array([[congruence(Lref[:, i], L[:, j]) for j in range(k2)] for i in range(k1)])
    A = np.abs(C)
    out = np.full(k1, np.nan)
    for i, j in hungarian_max(A):
        out[i] = A[i, j]
    return out


def scores_weights(L):
    """Least-squares component scores: s = z @ W, W = L (L'L)^-1."""
    return L @ np.linalg.inv(L.T @ L)


# ── second method: clustering ───────────────────────────────────────────────────────────────────
def rankdata(x):
    """Average ranks (ties share the mean rank)."""
    o = np.argsort(x, kind="mergesort")
    r = np.empty(len(x))
    xs = x[o]
    i = 0
    while i < len(x):
        j = i
        while j + 1 < len(x) and xs[j + 1] == xs[i]:
            j += 1
        r[o[i:j + 1]] = (i + j) / 2.0 + 1
        i = j + 1
    return r


def average_linkage_cut(D, cut):
    """UPGMA on a distance matrix, merging while the closest pair is <= cut. -> labels."""
    groups = [[i] for i in range(D.shape[0])]
    while len(groups) > 1:
        best, bi, bj = np.inf, -1, -1
        for a in range(len(groups)):
            for b in range(a + 1, len(groups)):
                d = np.mean(D[np.ix_(groups[a], groups[b])])
                if d < best:
                    best, bi, bj = d, a, b
        if best > cut:
            break
        groups[bi] = groups[bi] + groups[bj]
        del groups[bj]
    lab = np.zeros(D.shape[0], dtype=int)
    for g, members in enumerate(groups):
        lab[members] = g + 1
    return lab


def spearman_pairwise(Z):
    p = Z.shape[1]
    R = np.eye(p)
    for i in range(p):
        for j in range(i + 1, p):
            m = np.isfinite(Z[:, i]) & np.isfinite(Z[:, j])
            if m.sum() < 10:
                continue
            a, b = rankdata(Z[m, i]), rankdata(Z[m, j])
            R[i, j] = R[j, i] = np.corrcoef(a, b)[0, 1]
    return np.nan_to_num(R)


def cluster(Z, cut=0.7):
    R = spearman_pairwise(Z)
    D = 1 - np.abs(R)
    np.fill_diagonal(D, 0)
    return average_linkage_cut(D, cut), R


def jaccard(a, b):
    a, b = set(a), set(b)
    return len(a & b) / max(1, len(a | b))


# ── families ─────────────────────────────────────────────────────────────────────────────────────
def families(measures, meta, cov):
    fam = {}
    for j, m in enumerate(measures):
        fam.setdefault(meta[m].get("metricKey") or m, []).append(j)
    keep = []
    for key, js in fam.items():
        keep.append(max(js, key=lambda j: cov[j]))
    return sorted(keep), {k: [measures[j] for j in v] for k, v in fam.items() if len(v) > 1}


# ── description ─────────────────────────────────────────────────────────────────────────────────
def member_conditions(pack, mid, raw_dir):
    """Condition labels on the tail the theme pushes this measure toward ('high'/'low')."""
    conds = {c["id"]: c for c in pack["conditions"]}
    out = []
    for s in pack["signals"]:
        if (s.get("measures") or [None])[0] != mid:
            continue
        if s.get("direction") and s["direction"] != raw_dir:
            continue
        for c in pack["conditions"]:
            if s["id"] in c.get("detectedBy", []):
                out.append(conds[c["id"]]["label"])
    return sorted(set(out))


STATUS = {}
ANCHOR_WHEN = {"transition": 4, "finish": 10}


def when_of(meta_m):
    w = meta_m.get("when", 0)
    if w:
        return w
    r = meta_m.get("reducer") or {}
    return max([ANCHOR_WHEN.get(a, 0) for a in [r.get("anchor")] + list(r.get("window") or [])],
               default=0)


def describe(pack, meta, orient, measures, L, j, cov):
    members = []
    for i in np.argsort(-np.abs(L[:, j])):
        if abs(L[i, j]) < LOAD_MIN:
            break
        m = measures[i]
        sign = orient.get(m, (1.0, ""))[0]
        raw_dir = "high" if L[i, j] * sign > 0 else "low"
        members.append({
            "measure": m, "label": meta[m].get("label") or m, "loading": round(float(L[i, j]), 3),
            "orientation": orient.get(m, (1, "twoSided"))[1], "rawDirection": raw_dir,
            "metricKey": meta[m].get("metricKey", ""), "when": when_of(meta[m]),
            "status": STATUS.get(m, ""), "view": meta[m].get("view", ""), "coverage": round(float(cov[i]), 2),
            "conditions": member_conditions(pack, m, raw_dir),
        })
    whens = [x["when"] for x in members if x["when"] > 0]
    first = None
    if whens:
        w0 = min(whens)
        first = [x["label"] for x in members if x["when"] == w0]
        first = {"phase": f"P{w0}", "measures": first}
    return members, first


def names_for(members):
    """2-3 candidate golfer-facing names: the leading body phrase, the two leading phrases joined,
    the leading condition label. Phrases follow each member's direction in the theme."""
    phrases = []
    for x in members:
        if x["loading"] < 0 and x["orientation"] != "twoSided":
            continue  # this member moves AWAY from its fault in this theme; don't name by it
        ph = PHRASE.get((x["metricKey"], x["rawDirection"]))
        if ph and ph not in phrases:
            phrases.append(ph)
    out = phrases[:1]
    if len(phrases) > 1:
        out.append(f"{phrases[0]} and {phrases[1][0].lower()}{phrases[1][1:]}")
    conds = [c for x in members for c in x["conditions"]]
    if conds:
        out.append(conds[0])
    return out[:3]


# ── driver ───────────────────────────────────────────────────────────────────────────────────────
def boot_stability(Z, session, ref, k, n, rng):
    cs = []
    groups = [np.where(session == s)[0] for s in np.unique(session)]
    for _ in range(n):
        idx = np.concatenate([rng.choice(g, size=len(g), replace=True) for g in groups])
        f = fit(Z[idx], k=k)
        cs.append(match(ref, f["L"]))
    return np.array(cs)


def run(dirs, label, pack, orient, rng, pa_n=500):
    t, dropped = drop_sparse_swings(load_table(dirs, pack))
    print(f"{label}: dropped {len(dropped)} sparse swings {dropped}")
    Z, measures, Xo, spread = prepare(t, orient)
    f = fit(Z, pa_n=pa_n, rng=rng)
    return t, Z, measures, Xo, spread, f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--boot", type=int, default=500)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rng = np.random.default_rng(20261009)
    pack = load_pack()
    orient = orientation(pack)
    STATUS.update({m["id"]: m.get("status", "") for m in pack["measures"]})

    lib_dirs = sorted(glob.glob(os.path.join(LIBRARY, "*/")))
    t, Z, measures, Xo, spread, f = run(lib_dirs, "library", pack, orient, rng)
    meta = t.meta
    cov = np.mean(np.isfinite(Z), axis=0)
    k, L = f["k"], f["L"]
    print(f"library: {Z.shape[0]} swings x {Z.shape[1]} measures, {len(t.sessions)} sessions")
    print(f"parallel analysis keeps k={k}; eig {np.round(f['eig'][:6], 2)} "
          f"thr {np.round(f['thr'][:6], 2)}")

    # imputation check
    fz = fit(Z, k=k, impute="zero")
    imp_cong = match(L, fz["L"])
    print("EM vs zero-fill congruence:", np.round(imp_cong, 3))

    # bootstrap / LOSO
    B = boot_stability(Z, t.session, L, k, a.boot, rng)
    loso = []
    for s in range(len(t.sessions)):
        r = t.session != s
        loso.append(match(L, fit(Z[r], k=k)["L"]))
    loso = np.array(loso)

    # families collapsed
    keep_idx, fams = families(measures, meta, cov)
    fc = fit(Z[:, keep_idx], k=None, pa_n=500, rng=rng)
    Lfull_on_keep = L[keep_idx]
    collapse_cong = match(Lfull_on_keep, fc["L"])
    collapse_cong_k = match(Lfull_on_keep, fit(Z[:, keep_idx], k=k)["L"])
    print(f"collapsed: {len(keep_idx)} measures, PA k={fc['k']}; congruence to full themes "
          f"(on kept rows) {np.round(collapse_cong, 3)}")

    # clustering
    labels, Rs = cluster(Z)
    clusters = {}
    for i, c in enumerate(labels):
        clusters.setdefault(int(c), []).append(measures[i])
    multi = {c: v for c, v in clusters.items() if len(v) > 1}

    # corpus
    extra = sorted(d for g in CORPUS_EXTRA for d in glob.glob(os.path.join(CORPUS, g + "/")))
    tc, _ = drop_sparse_swings(load_table(lib_dirs + extra, pack))
    Zc_all, mc, _, _ = prepare(tc, orient)
    common = [m for m in measures if m in mc]
    Zc = Zc_all[:, [mc.index(m) for m in common]]
    fcorp = fit(Zc, k=None, pa_n=500, rng=rng)
    fcorp_k = fit(Zc, k=k)
    Lc_ref = L[[measures.index(m) for m in common]]
    corp_cong = match(Lc_ref, fcorp_k["L"])
    corp_cong_free = match(Lc_ref, fcorp["L"])
    print(f"corpus+library: {Zc.shape[0]} swings, {len(tc.sessions)} sessions, {len(common)} "
          f"measures; PA k={fcorp['k']}; congruence at k={k}: {np.round(corp_cong, 3)}; "
          f"at its own k: {np.round(corp_cong_free, 3)}")
    # corpus-only (no library) — the strictest replication
    to, _ = drop_sparse_swings(load_table(extra, pack))
    Zo_all, mo, _, _ = prepare(to, orient)
    common_o = [m for m in measures if m in mo]
    Zo = Zo_all[:, [mo.index(m) for m in common_o]]
    fo = fit(Zo, k=k)
    fo_free = fit(Zo, k=None, pa_n=500, rng=rng)
    corp_only_cong = match(L[[measures.index(m) for m in common_o]], fo["L"])
    corp_only_cong_free = match(L[[measures.index(m) for m in common_o]], fo_free["L"])
    print(f"corpus only: {Zo.shape[0]} swings, {len(to.sessions)} sessions, PA k={fo_free['k']}; "
          f"congruence at k={k}: {np.round(corp_only_cong, 3)}; at its own k: "
          f"{np.round(corp_only_cong_free, 3)}")

    # session trend (uncentred, projected through the same weights)
    W = scores_weights(L)
    gmed = np.nanmedian(Xo, axis=0)
    Zu = np.clip((Xo - gmed) / spread, -WINSOR * 3, WINSOR * 3)
    Zu = np.where(np.isfinite(Zu), Zu, 0.0)
    Su = Zu @ W
    Sc = f["F"] @ W
    within_sd = np.array([np.sqrt(np.mean([np.var(Sc[t.session == s, j])
                                           for s in range(len(t.sessions))]))
                          for j in range(k)])

    themes, stab_rows, sess_rows = [], [], []
    for j in range(k):
        members, first = describe(pack, meta, orient, measures, L, j, cov)
        mem_ids = [x["measure"] for x in members]
        best_c = max(multi.values(), key=lambda v: jaccard(v, mem_ids), default=[])
        sm = [float(np.mean(Su[t.session == s, j]) / within_sd[j])
              for s in range(len(t.sessions))]
        x = np.arange(len(sm))
        slope = float(np.polyfit(x, sm, 1)[0])
        b = B[:, j]
        st = {"theme": j + 1, "boot_median": float(np.median(b)),
              "boot_p05": float(np.percentile(b, 5)), "boot_frac_ge_085": float(np.mean(b >= STABLE)),
              "loso_min": float(np.min(loso[:, j])), "loso_median": float(np.median(loso[:, j])),
              "em_vs_zero_fill": float(imp_cong[j]), "families_collapsed_own_k": float(collapse_cong[j]),
              "families_collapsed_same_k": float(collapse_cong_k[j]),
              "plus_corpus_same_k": float(corp_cong[j]), "plus_corpus_own_k": float(corp_cong_free[j]),
              "corpus_only_same_k": float(corp_only_cong[j]),
              "corpus_only_own_k": float(corp_only_cong_free[j])}
        st["stable"] = bool(st["boot_median"] >= STABLE and st["loso_min"] >= 0.80)
        stab_rows.append(st)
        for s, v in zip(t.sessions, sm):
            sess_rows.append({"theme": j + 1, "session": s, "n": int(np.sum(t.session ==
                              t.sessions.index(s))), "score_within_sd": round(v, 3)})
        themes.append({
            "theme": j + 1, "varianceShare": round(float(f["varshare"][j]), 3),
            "members": members, "startsAt": first, "candidateNames": names_for(members),
            "stability": {k_: (round(v, 3) if isinstance(v, float) else v) for k_, v in st.items()},
            "clusterAgreement": {"bestCluster": best_c, "jaccard": round(jaccard(best_c, mem_ids), 2)},
            "sessionTrend": {"sessions": t.sessions, "scoreWithinSd": [round(v, 3) for v in sm],
                             "slopePerSession": round(slope, 3),
                             "reading": "worse (more of the fault)" if slope > 0.1 else
                             "better (less of the fault)" if slope < -0.1 else "flat"},
        })

    # ── write ──
    with open(os.path.join(a.out, "loadings.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["measure", "label", "metricKey", "status", "orientation", "when", "coverage",
                    "cluster"]
                   + [f"T{j + 1}" for j in range(k)])
        for i, m in enumerate(measures):
            w.writerow([m, meta[m].get("label", ""), meta[m].get("metricKey", ""), STATUS.get(m, ""),
                        orient.get(m, (1, "twoSided"))[1], when_of(meta[m]),
                        round(float(cov[i]), 2), int(labels[i])]
                       + [round(float(L[i, j]), 3) for j in range(k)])
    with open(os.path.join(a.out, "stability.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(stab_rows[0].keys()))
        w.writeheader()
        for r in stab_rows:
            w.writerow({k_: (round(v, 3) if isinstance(v, float) else v) for k_, v in r.items()})
    with open(os.path.join(a.out, "session_scores.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["theme", "session", "n", "score_within_sd"])
        w.writeheader()
        w.writerows(sess_rows)
    doc = {
        "method": __doc__.strip().split("\n\n")[2],
        "library": {"swings": int(Z.shape[0]), "measures": len(measures), "sessions": t.sessions,
                    "k": k, "eigen": np.round(f["eig"][:8], 3).tolist(),
                    "paThreshold": np.round(f["thr"][:8], 3).tolist()},
        "themes": themes,
        "clusters": {str(c): v for c, v in multi.items()},
        "families": fams,
        "collapsed": {"measures": len(keep_idx), "k": fc["k"],
                      "themes": [[{"measure": measures[keep_idx[i]], "loading": round(float(fc["L"][i, j]), 3)}
                                  for i in np.argsort(-np.abs(fc["L"][:, j])) if abs(fc["L"][i, j]) >= LOAD_MIN]
                                 for j in range(fc["k"])]},
        "corpus": {"sessions": [os.path.basename(os.path.normpath(d)) for d in extra],
                   "swingsWithLibrary": int(Zc.shape[0]), "kOwn": fcorp["k"],
                   "corpusOnlySwings": int(Zo.shape[0]), "corpusOnlyKOwn": fo_free["k"],
                   "themesOwnK": [[{"measure": common[i], "loading": round(float(fcorp["L"][i, j]), 3)}
                                   for i in np.argsort(-np.abs(fcorp["L"][:, j])) if abs(fcorp["L"][i, j]) >= LOAD_MIN]
                                  for j in range(fcorp["k"])]},
    }
    with open(os.path.join(a.out, "themes.json"), "w") as fh:
        json.dump(doc, fh, indent=1)

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        order = np.lexsort((-np.abs(L).max(1), np.argmax(np.abs(L), 1)))
        fig, ax = plt.subplots(figsize=(2 + 1.2 * k, 0.24 * len(measures) + 1.5))
        im = ax.imshow(L[order], cmap="RdBu_r", vmin=-1, vmax=1, aspect="auto")
        ax.set_yticks(range(len(measures)))
        ax.set_yticklabels([(meta[measures[i]].get("label") or measures[i])[:48] for i in order],
                           fontsize=7)
        ax.set_xticks(range(k))
        ax.set_xticklabels([f"T{j + 1}" for j in range(k)])
        fig.colorbar(im, ax=ax, shrink=0.5)
        fig.tight_layout()
        fig.savefig(os.path.join(a.out, "loadings.png"), dpi=110)
        plt.close(fig)
    except ImportError:
        pass

    for th in themes:
        print(f"\nT{th['theme']}  var {th['varianceShare']:.3f}  stable={th['stability']['stable']}"
              f"  boot {th['stability']['boot_median']:.2f} (p05 {th['stability']['boot_p05']:.2f})"
              f"  loso_min {th['stability']['loso_min']:.2f}  collapsed {th['stability']['families_collapsed_same_k']:.2f}/{th['stability']['families_collapsed_own_k']:.2f}"
              f"  +corpus {th['stability']['plus_corpus_same_k']:.2f}/{th['stability']['plus_corpus_own_k']:.2f}"
              f"  corpus-only {th['stability']['corpus_only_same_k']:.2f}")
        for x in th["members"]:
            print(f"   {x['loading']:+.2f}  P{x['when']}  {x['label'][:55]:55s} {x['orientation']:14s} "
                  f"{x['rawDirection']:4s} {x['conditions']}")
        print("   starts:", th["startsAt"], " names:", th["candidateNames"])
        print("   cluster:", th["clusterAgreement"])
        print("   sessions:", th["sessionTrend"]["scoreWithinSd"], th["sessionTrend"]["reading"])
    print("\nclusters (multi):")
    for c, v in multi.items():
        print("  ", c, v)
    print("families:", fams)
    print("collapsed themes:", json.dumps(doc["collapsed"]["themes"]))
    print("corpus own-k themes:", json.dumps(doc["corpus"]["themesOwnK"]))


if __name__ == "__main__":
    main()
