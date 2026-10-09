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
  * Random numbers: theme_rng (std::mt19937_64, as the app's DetRng), ONE seed per replicate —
    parallel-analysis shuffle i from seed_for(1, i), bootstrap replicate b from seed_for(2, b) — so
    the C++ port (src/Analysis/swing_themes.h) draws the same rows; make_golden.py pins it.
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
from theme_rng import MT19937_64, STREAM_BOOT, STREAM_PA, seed_for  # noqa: E402

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


def shuffle_columns(Z, i):
    """Parallel-analysis shuffle i: one DetRng seeded seed_for(STREAM_PA, i) permutes a copy of
    each column in turn (NaNs travel with their column) by Fisher-Yates — the C++ draws the same."""
    rng = MT19937_64(seed_for(STREAM_PA, i))
    return np.column_stack([Z[rng.permutation_index(Z.shape[0]), j] for j in range(Z.shape[1])])


def parallel_analysis(Z, n=500, q=95):
    """On PAIRWISE-COMPLETE correlations of the raw (NaN-bearing) matrix, so imputed cells cannot
    lend structure to the observed eigenvalues. Columns shuffled with their NaNs."""
    w, _ = eig_desc(corr_pairwise(Z))
    sims = np.empty((n, Z.shape[1]))
    for i in range(n):
        sims[i] = eig_desc(corr_pairwise(shuffle_columns(Z, i)))[0]
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


def fit(Z, k=None, impute="em", pa_n=500):
    """-> dict(k, L (rotated, signed, ordered), eig (of the completed matrix), eigPa + thr (parallel
    analysis, when k was not given), F (completed), varshare)."""
    thr = w_pa = None
    if k is None:
        k, w_pa, thr = parallel_analysis(Z, n=pa_n)
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
    return {"k": k, "L": Lr, "eig": w, "eigPa": w_pa, "thr": thr, "F": F,
            "varshare": ss / Z.shape[1]}


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


def theme_members(L, j, measures, orient, meta):
    """Theme j's members: |loading| >= LOAD_MIN, by -|loading| (stable). rawHigh = the theme pushes
    the measure's RAW value up (loading x orientation sign > 0)."""
    out = []
    for i in np.argsort(-np.abs(L[:, j]), kind="stable"):
        if abs(L[i, j]) < LOAD_MIN:
            break
        m = measures[i]
        sign = orient.get(m, (1.0, "twoSided"))[0]
        out.append({"index": int(i), "measure": m, "loading": float(L[i, j]),
                    "rawHigh": bool(L[i, j] * sign > 0), "when": when_of(meta.get(m, {}))})
    return out


def starts_at(members):
    """The earliest swing position (1..10) any member is read at; 0 when none is placed."""
    return min((x["when"] for x in members if x["when"] > 0), default=0)


def describe(pack, meta, orient, measures, L, j, cov):
    members = []
    for x in theme_members(L, j, measures, orient, meta):
        m, i = x["measure"], x["index"]
        raw_dir = "high" if x["rawHigh"] else "low"
        members.append({
            "measure": m, "label": meta[m].get("label") or m, "loading": round(x["loading"], 3),
            "orientation": orient.get(m, (1, "twoSided"))[1], "rawDirection": raw_dir,
            "metricKey": meta[m].get("metricKey", ""), "when": x["when"],
            "status": STATUS.get(m, ""), "view": meta[m].get("view", ""), "coverage": round(float(cov[i]), 2),
            "conditions": member_conditions(pack, m, raw_dir),
        })
    w0 = starts_at(members)
    first = None
    if w0:
        first = {"phase": f"P{w0}", "measures": [x["label"] for x in members if x["when"] == w0]}
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


# ── stability, trend, tier ───────────────────────────────────────────────────────────────────────
def boot_indices(session, b):
    """Bootstrap replicate b's rows: one DetRng seeded seed_for(STREAM_BOOT, b); for each session
    present, ascending, draw len(g) rows of that session with replacement (g[below(len(g))])."""
    rng = MT19937_64(seed_for(STREAM_BOOT, b))
    idx = []
    for s in np.unique(session):
        g = np.where(session == s)[0]
        js = rng.raw_array(len(g)) % np.uint64(len(g))
        idx.extend(g[js.astype(np.int64)].tolist())
    return np.array(idx, dtype=int)


def boot_stability(Z, session, ref, k, n):
    """n x k: congruence of each reference theme with its match in replicate b's fit (fixed k)."""
    return np.array([match(ref, fit(Z[boot_indices(session, b)], k=k)["L"]) for b in range(n)])


def loso_stability(Z, session, ref, k):
    """sessions-present x k: congruence of each theme with its match when one session is left out.
    Only sessions that still hold a swing after the sparse drop are left out (ascending)."""
    return np.array([match(ref, fit(Z[session != s], k=k)["L"]) for s in np.unique(session)])


def theme_tier(boot_median, boot_p05, loso_min, loso_median):
    """How firmly a theme may be told (design table): 'firm', 'probably', 'possibly' or None."""
    if boot_p05 >= 0.84 and loso_min >= 0.85:
        return "firm"
    if boot_median >= 0.75 and loso_median >= 0.9:
        return "probably"
    if boot_median >= 0.7 and loso_median >= 0.85:
        return "possibly"
    return None


def stability_summary(B, loso, j):
    b = B[:, j]
    return {"bootMedian": float(np.median(b)), "bootP05": float(np.percentile(b, 5)),
            "losoMin": float(np.min(loso[:, j])), "losoMedian": float(np.median(loso[:, j]))}


def em_vs_zero_fill(Z, L, k):
    """Per theme: congruence with the zero-fill fit at the same k (recorded, never a gate)."""
    return match(L, fit(Z, k=k, impute="zero")["L"])


def best_cluster(labels, measures, member_ids):
    """The multi-member cluster (numbered by first appearance) sharing most with the theme, first
    on ties; with its Jaccard overlap. ([], 0.0) when no cluster has two members."""
    clusters = {}
    for i, c in enumerate(labels):
        clusters.setdefault(int(c), []).append(measures[i])
    multi = [v for v in clusters.values() if len(v) > 1]
    best = max(multi, key=lambda v: jaccard(v, member_ids), default=[])
    return best, (jaccard(best, member_ids) if best else 0.0)


def renumber_by_first_appearance(labels):
    seen = {}
    return np.array([seen.setdefault(int(c), len(seen) + 1) for c in labels], dtype=int)


def session_trend(L, F, Xo, spread, session, slope_gate=0.1, min_t=2.0):
    """Per theme: the session scores on the uncentred scale (in within-session SDs), the
    least-squares slope over session order, and the reading (+1 more of it, -1 less, 0 flat).

    Uncentred: (Xo - global median) / pooled spread, clipped at +-3 x WINSOR, unread -> 0, through
    the least-squares score weights W = L (L'L)^-1. Scale: sqrt of the mean over sessions of the
    within-session population variance of the EM-completed scores F W."""
    W = scores_weights(L)
    gmed = np.nanmedian(Xo, axis=0)
    Zu = np.clip((Xo - gmed) / spread, -WINSOR * 3, WINSOR * 3)
    Zu = np.where(np.isfinite(Zu), Zu, 0.0)
    Su, Sc = Zu @ W, F @ W
    present = np.unique(session)
    k = L.shape[1]
    within_sd = np.array([np.sqrt(np.mean([np.var(Sc[session == s, j]) for s in present]))
                          for j in range(k)])
    scores = np.array([[np.mean(Su[session == s, j]) / within_sd[j] for s in present]
                       for j in range(k)])
    x = np.arange(len(present), dtype=float)
    xc = x - x.mean()
    slopes = np.array([float(np.sum(xc * (scores[j] - scores[j].mean())) / np.sum(xc * xc))
                       if len(present) > 1 else 0.0 for j in range(k)])
    # ...and only when the sessions LINE UP: the slope stands min_t standard errors clear of zero,
    # the error from the sessions' own scatter about the line. Session means carry drift far larger
    # than their sampling error, so the slope gate alone reads drift as a trend.
    n = len(present)
    ses = np.full(k, np.inf)
    if n > 2:
        for j in range(k):
            r = scores[j] - scores[j].mean() - slopes[j] * xc
            ses[j] = np.sqrt(np.sum(r * r) / (n - 2) / np.sum(xc * xc))
    lined = np.abs(slopes) >= min_t * ses
    trends = np.where(~lined, 0, np.where(slopes > slope_gate, 1, np.where(slopes < -slope_gate, -1, 0)))
    return scores, slopes, trends


def run(dirs, label, pack, orient, pa_n=500):
    t, dropped = drop_sparse_swings(load_table(dirs, pack))
    print(f"{label}: dropped {len(dropped)} sparse swings {dropped}")
    Z, measures, Xo, spread = prepare(t, orient)
    f = fit(Z, pa_n=pa_n)
    return t, Z, measures, Xo, spread, f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--boot", type=int, default=500)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    pack = load_pack()
    orient = orientation(pack)
    STATUS.update({m["id"]: m.get("status", "") for m in pack["measures"]})

    lib_dirs = sorted(glob.glob(os.path.join(LIBRARY, "*/")))
    t, Z, measures, Xo, spread, f = run(lib_dirs, "library", pack, orient)
    meta = t.meta
    cov = np.mean(np.isfinite(Z), axis=0)
    k, L = f["k"], f["L"]
    print(f"library: {Z.shape[0]} swings x {Z.shape[1]} measures, {len(t.sessions)} sessions")
    print(f"parallel analysis keeps k={k}; eig {np.round(f['eigPa'][:6], 2)} "
          f"thr {np.round(f['thr'][:6], 2)}")

    # imputation check
    imp_cong = em_vs_zero_fill(Z, L, k)
    print("EM vs zero-fill congruence:", np.round(imp_cong, 3))

    # bootstrap / LOSO
    B = boot_stability(Z, t.session, L, k, a.boot)
    loso = loso_stability(Z, t.session, L, k)
    present = [t.sessions[s] for s in np.unique(t.session)]

    # families collapsed
    keep_idx, fams = families(measures, meta, cov)
    fc = fit(Z[:, keep_idx], k=None, pa_n=500)
    Lfull_on_keep = L[keep_idx]
    collapse_cong = match(Lfull_on_keep, fc["L"])
    collapse_cong_k = match(Lfull_on_keep, fit(Z[:, keep_idx], k=k)["L"])
    print(f"collapsed: {len(keep_idx)} measures, PA k={fc['k']}; congruence to full themes "
          f"(on kept rows) {np.round(collapse_cong, 3)}")

    # clustering
    labels, Rs = cluster(Z)
    labels = renumber_by_first_appearance(labels)
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
    fcorp = fit(Zc, k=None, pa_n=500)
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
    fo_free = fit(Zo, k=None, pa_n=500)
    corp_only_cong = match(L[[measures.index(m) for m in common_o]], fo["L"])
    corp_only_cong_free = match(L[[measures.index(m) for m in common_o]], fo_free["L"])
    print(f"corpus only: {Zo.shape[0]} swings, {len(to.sessions)} sessions, PA k={fo_free['k']}; "
          f"congruence at k={k}: {np.round(corp_only_cong, 3)}; at its own k: "
          f"{np.round(corp_only_cong_free, 3)}")

    # session trend (uncentred, projected through the same weights)
    scores, slopes, trends = session_trend(L, f["F"], Xo, spread, t.session)

    themes, stab_rows, sess_rows = [], [], []
    for j in range(k):
        members, first = describe(pack, meta, orient, measures, L, j, cov)
        mem_ids = [x["measure"] for x in members]
        best_c, best_j = best_cluster(labels, measures, mem_ids)
        sm = [float(v) for v in scores[j]]
        slope = float(slopes[j])
        ss = stability_summary(B, loso, j)
        st = {"theme": j + 1, "boot_median": ss["bootMedian"],
              "boot_p05": ss["bootP05"], "boot_frac_ge_085": float(np.mean(B[:, j] >= STABLE)),
              "loso_min": ss["losoMin"], "loso_median": ss["losoMedian"],
              "em_vs_zero_fill": float(imp_cong[j]), "families_collapsed_own_k": float(collapse_cong[j]),
              "families_collapsed_same_k": float(collapse_cong_k[j]),
              "plus_corpus_same_k": float(corp_cong[j]), "plus_corpus_own_k": float(corp_cong_free[j]),
              "corpus_only_same_k": float(corp_only_cong[j]),
              "corpus_only_own_k": float(corp_only_cong_free[j])}
        st["stable"] = bool(st["boot_median"] >= STABLE and st["loso_min"] >= 0.80)
        st["tier"] = theme_tier(ss["bootMedian"], ss["bootP05"], ss["losoMin"], ss["losoMedian"])
        stab_rows.append(st)
        for s, v in zip(present, sm):
            sess_rows.append({"theme": j + 1, "session": s, "n": int(np.sum(t.session ==
                              t.sessions.index(s))), "score_within_sd": round(v, 3)})
        themes.append({
            "theme": j + 1, "varianceShare": round(float(f["varshare"][j]), 3),
            "members": members, "startsAt": first, "candidateNames": names_for(members),
            "stability": {k_: (round(v, 3) if isinstance(v, float) else v) for k_, v in st.items()},
            "clusterAgreement": {"bestCluster": best_c, "jaccard": round(best_j, 2)},
            "sessionTrend": {"sessions": present, "scoreWithinSd": [round(v, 3) for v in sm],
                             "slopePerSession": round(slope, 3), "trend": int(trends[j]),
                             "reading": "worse (more of the fault)" if trends[j] > 0 else
                             "better (less of the fault)" if trends[j] < 0 else "flat"},
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
                    "k": k, "eigen": np.round(f["eigPa"][:8], 3).tolist(),
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
