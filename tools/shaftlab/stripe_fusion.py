#!/usr/bin/env python3
"""Stripe fusion — full-swing instrumented shaft measurement (design:
docs/design/stripe_fusion_design.md).

Three evidence terms over candidate rays from the pose grip anchor:
  E1  band-pattern ratio match (validated v1 machinery, imported)
  E2  polarity-agnostic sign-coherent ridge: the steel shaft is BRIGHT over
      dark cloth and DARK over the blown mat in the same frame; credit only
      accrues when the sign of (on-ray - background) matches what the local
      background level implies
  E3  temporal: pixel-median stacks over still runs (the club is physically
      static there — perfectly registered), scene-median subtraction in
      motion phases (static junk vanishes)

Truth tiers: band/still = full (theta + s + head); ray = theta-only.
Misses are absent rows. No tracker state carries across a measurement gap
wider than RAY_GAP frames.

  stripe_fusion.py --selftest                      synthetic accuracy gate
  stripe_fusion.py <clip> --anchors a.csv --clubs clubs.json --club "7 IRON"
      [--clipmeta m.json] [--fps-override F] [--out-dir out] [--overlay]
      [--truth-out]
"""
import argparse, csv, json, math, os, sys
import numpy as np, cv2
from scipy.signal import find_peaks
from scipy.ndimage import percentile_filter, gaussian_filter1d

from stripe_annotate import (SAT_T, detect_blobs, match_pattern, flip_rms,
                             gap_dark_ok, RMS4, RMS5, GRIP_GATE, LAT_TOL)
import itertools

R_STEP = 2.0
R_LO, R_HI = 8.0, 470.0
BG_HI = 200.0        # background above this: shaft must be DARK (blown mat)
E_CLIP_NEG, E_CLIP_POS = 30.0, 90.0
MIN_LEN_PX = 90.0    # shortest credible visible shaft (heavy foreshortening)
RAY_SCORE_MIN = 260.0   # sqrt-normalized cumulative ridge score gate
RAY_SUPP_MIN = 0.55     # fraction of supported samples along accepted length
RAY_GAP = 20         # max lock-to-lock span for ray interpolation (theta
                     # sweeps >180 deg through impact in ~37 frames — long
                     # interpolation is invalid there; s01-adjudicated)
RAY_ONESIDED = 4     # max frames extending past the last lock (an 18-deg
                     # sweep window is stale after ~4 frames at peak rate)
S_DEV_MAX = 0.20     # band-lock s must sit within this of the swing median
RAY_SWEEP = 18.0     # deg around prediction for per-frame ray search
ARM_VETO_DEG = 12.0  # no still lock within this of the grip->elbow ray
STILL_SPEED = 0.8    # px/frame grip speed below which a frame is "still"
STILL_MIN = 30       # min run length (frames)


def _sample(img, X, Y, offs, ux, uy, reduce):
    H, W = img.shape
    v = []
    for o in offs:
        xs = np.clip((X - o * uy).astype(np.int32), 0, W - 1)
        ys = np.clip((Y + o * ux).astype(np.int32), 0, H - 1)
        v.append(img[ys, xs])
    return reduce(np.stack(v), axis=0)


def ridge_sweep(img, gx, gy, thetas, bright_only=False):
    """E2 over many rays at once. img float32 gray (or |frame-median| with
    bright_only=True). Returns (score, r_end, support) arrays per theta."""
    R = np.arange(R_LO, R_HI, R_STEP)
    ux, uy = np.cos(thetas)[:, None], np.sin(thetas)[:, None]
    X, Y = gx + ux * R[None, :], gy + uy * R[None, :]
    H, W = img.shape
    bg = _sample(img, X, Y, (-12.0, -9.0, 9.0, 12.0), ux, uy, np.median)
    if bright_only:
        on = _sample(img, X, Y, (-1.0, 0.0, 1.0), ux, uy, np.mean)
        e = np.clip(on - bg, -E_CLIP_NEG, E_CLIP_POS)
    else:
        # polarity applies to the SAMPLING, not just the sign: a 2-3 px
        # bright line over dark cloth wants the lateral max, a 2-3 px dark
        # shaft over the blown mat wants the lateral min — a mean washes
        # both out under sub-pixel ray misalignment (s01-adjudicated)
        on_max = _sample(img, X, Y, (-2.0, -1.0, 0.0, 1.0, 2.0), ux, uy, np.max)
        on_min = _sample(img, X, Y, (-2.0, -1.0, 0.0, 1.0, 2.0), ux, uy, np.min)
        # -12: max/min-of-5 noise bias — flat background must not accrue
        e = np.where(bg > BG_HI,
                     np.clip(bg - on_min - 12.0, -E_CLIP_NEG, E_CLIP_POS),
                     np.clip(on_max - bg - 12.0, -E_CLIP_NEG, E_CLIP_POS))
    inb = (X >= 0) & (X < W) & (Y >= 0) & (Y < H)
    e = np.where(inb, e, 0.0)
    cum = np.cumsum(e, axis=1)
    norm = cum / np.sqrt(np.arange(len(R)) + 8.0)
    j0 = int(MIN_LEN_PX / R_STEP)
    j = np.argmax(norm[:, j0:], axis=1) + j0
    rows = np.arange(len(thetas))
    score = norm[rows, j]
    pos = np.cumsum(e > 8.0, axis=1)
    support = pos[rows, j] / (j + 1.0)
    return score, R[j], support


def profile_arrays(img, gx, gy, theta, r_hi=R_HI):
    """on/bg/steel intensity profiles along one ray (1 px steps)."""
    R = np.arange(0.0, r_hi, 1.0)
    ux, uy = math.cos(theta), math.sin(theta)
    X, Y = (gx + ux * R)[None, :], (gy + uy * R)[None, :]
    on = _sample(img, X, Y, (-2.0, -1.0, 0.0, 1.0, 2.0), ux, uy, np.max)[0]
    bg = _sample(img, X, Y, (-12.0, -9.0, 9.0, 12.0), ux, uy, np.median)[0]
    # 25th percentile, not median: the tight tip trio (bands+gaps ~43 px at
    # s=0.35) fits inside the 51 px window, so >50% of samples are band
    # pixels and a median reads the BAND level — the group masks itself
    # (synth-gated). The lower quartile still lands on gap steel.
    steel = percentile_filter(on, 25, size=51)
    return R, on, bg, steel


BAND_MIN_C = 18.0    # smoothed per-band contrast floor over local steel
GRID_SCORE_MIN = 35.0  # net assignment score (matched - 0.3*unexplained)
N_OBS_MIN = 3        # matched bands needed for a lock


STEEL_HEADROOM = 230.0   # saturated steel: band indistinguishable, skip


def _profile_peaks(R, on, bg, steel):
    """Band candidates: peaks that fall away on BOTH sides within a
    band-scale window (find_peaks prominence with bounded wlen). Any scalar
    baseline (median/percentile) turns sloped specular steel into dozens of
    phantom peaks (synth-gated: 26+ peaks/ray drowned position
    verification); a band physically has gaps beside it, a slope crest does
    not. Non-evaluable stretches (blown bg, bloom-saturated steel) are
    hard-masked."""
    evaluable = (bg <= BG_HI) & (steel <= STEEL_HEADROOM)
    sm = gaussian_filter1d(np.where(evaluable, on, 0.0), 1.5)
    pk, props = find_peaks(sm, prominence=25.0, wlen=51, distance=6)
    keep = evaluable[pk] & (on[pk] > bg[pk] + 25.0)
    return R[pk][keep], np.clip(props["prominences"][keep], 0.0, 60.0)


def _grid_score(R, on, bg, steel, radii_mm):
    """Best (score, s, r0, n_match) over a dense (s, r0) grid using peak
    ASSIGNMENT: matched peak strength minus a penalty for prominent peaks
    the hypothesis leaves unexplained. A wrong (s, r0) necessarily strands
    the true band peaks, so cherry-picking bright spots cannot win (the
    exploit adjudicated in the synth gate: a wrong fit parks its unmatched
    predictions in bloom-saturated stretches where they cost nothing)."""
    tpk, w = _profile_peaks(R, on, bg, steel)
    if len(tpk) < N_OBS_MIN:
        return -1e9, 0.0, 0.0, 0
    Sv = np.arange(0.20, 0.551, 0.005)
    R0v = np.arange(40.0, 261.0, 3.0)
    SS, RR = np.meshgrid(Sv, R0v, indexing="ij")
    pos = SS[..., None] * (np.asarray(radii_mm)[None, None, :] - RR[..., None])
    inb = (pos >= 0.0) & (pos <= R[-1] - 2.0)
    # (S, R0, bands, peaks) distances; tol scales with the tightest gap
    dist = np.abs(pos[..., None] - tpk[None, None, None, :])
    dist = np.where(inb[..., None], dist, 1e9)
    tol = np.maximum(3.0, 0.2 * 46.0 * SS)[..., None]
    matched = dist.min(axis=-2) <= tol            # per peak: near any band?
    n_match = (dist.min(axis=-1) <= tol).sum(axis=-1)  # per band: got a peak
    wsum = np.where(matched, w[None, None, :], 0.0).sum(axis=-1)
    upen = np.where(~matched, w[None, None, :], 0.0).sum(axis=-1)
    score = np.where(n_match >= N_OBS_MIN, wsum - 0.3 * upen, -1e9)
    k = int(np.argmax(score))
    ks, kr = np.unravel_index(k, score.shape)
    return float(score[ks, kr]), float(Sv[ks]), float(R0v[kr]), \
        int(n_match[ks, kr])


def profile_band_fit(R, on, bg, steel, bands, r_end=None):
    """E1 (dense form): matched-filter grid proposes (s, r0); sub-pixel band
    peak positions decide the lock and the flip (the 2-1-3 asymmetry is only
    ~8 mm against a 25 mm band width — intensity at predicted positions
    cannot discriminate a flip, positions at ~1 px rms can, as validated in
    v1). r_end (E2's visible shaft endpoint) independently vetoes locks
    whose implied club length contradicts the ridge. Returns
    (locked, score, s, r0, n_pts)."""
    sc, s, r0, n = _grid_score(R, on, bg, steel, bands)
    if sc < GRID_SCORE_MIN:
        return False, sc, None, None, 0
    tpk, w = _profile_peaks(R, on, bg, steel)
    # unique assignment: each band claims the nearest UNCLAIMED peak inside
    # tol — overlapping refit windows double-counted one peak into several
    # bands at small s (adjudicated in the synth gate: phantom n_pts)
    tol = max(3.0, 0.2 * 46.0 * s)
    cands = []
    for b in bands:
        p = s * (b - r0)
        if 0 <= p < R[-1] - 2 and len(tpk):
            j = int(np.argmin(np.abs(tpk - p)))
            d = abs(float(tpk[j]) - p)
            if d <= tol:
                cands.append((d, j, b))
    cands.sort()
    used, tj, rk = set(), [], []
    for d, j, b in cands:
        if j in used:
            continue
        used.add(j)
        tj.append(float(tpk[j]))
        rk.append(b)
    if len(tj) < 4:
        return False, sc, None, None, len(tj)
    order = np.argsort(rk)
    tj, rk = np.array(tj)[order], np.array(rk)[order]
    if np.any(np.diff(tj) <= 0):
        return False, sc, None, None, len(tj)
    A = np.vstack([rk, np.ones_like(rk)]).T
    coef, *_ = np.linalg.lstsq(A, tj, rcond=None)
    s2 = float(coef[0])
    if not 0.18 <= s2 <= 0.58:
        return False, sc, None, None, len(tj)
    r02 = float(-coef[1] / s2)
    rms = float(np.sqrt(np.mean((tj - A @ coef) ** 2)))
    fl = flip_rms(tj, rk)
    locked = rms <= 1.8 and fl > max(3.0 * rms, 2.0)
    if locked and len(tpk) >= 8 and len(tj) < 5:
        locked = False        # junky ray: demand broader consensus
    if locked and r_end is not None and r_end < R[-1] - 20.0:
        pred_len = s2 * (940.0 - r02)
        if abs(pred_len - r_end) > 40.0:
            locked = False    # implied club length contradicts the ridge
    return locked, sc, s2 if locked else None, r02 if locked else None, len(tj)


def still_search(img, gx, gy, bands, arm_theta=None):
    """Full-sweep (theta, s, r0) search on a stacked still image. Candidate
    thetas come from BOTH evidence terms: E2 ridges (top by score) and a
    cheap band-peak sweep — at address the shaft is one thin line among
    many (trouser creases, leg edges beat it on E2 alone; s01-adjudicated),
    but it is the only line carrying strong narrow peaks.

    arm_theta (deg, grip->elbow): candidates within ARM_VETO_DEG of it are
    discarded — the shaft never points from the hands back INTO the lead
    forearm, but the white-sleeve ridge + floral-shirt texture peaks form a
    lockable counterfeit exactly there (s01-adjudicated: the entire address
    still run flipped onto the arm line, self-consistent scale included)."""
    thetas = np.radians(np.arange(0.0, 360.0, 0.5))
    score, r_end, supp = ridge_sweep(img, gx, gy, thetas)
    pk_score = np.zeros(len(thetas))
    coarse = np.radians(np.arange(0.0, 360.0, 1.0))
    for th in coarse:
        R, on, bg, steel = profile_arrays(img, gx, gy, float(th))
        _, w = _profile_peaks(R, on, bg, steel)
        i = int(round(math.degrees(th) / 0.5)) % len(thetas)
        pk_score[i] = w.sum()
    order = np.argsort(-score)
    pk_order = np.argsort(-pk_score)
    picked = []
    for src, lim in ((pk_order, 5), (order, 6)):
        cnt = 0
        for i in src:
            if cnt >= lim:
                break
            if (src is pk_order and pk_score[i] <= 0) or i in picked:
                continue
            if all(min(abs(thetas[i] - thetas[k]),
                       2 * math.pi - abs(thetas[i] - thetas[k])) >
                   math.radians(5) for k in picked):
                picked.append(i)
                cnt += 1
    best = None
    for i in picked:
        if arm_theta is not None:
            d = abs((math.degrees(thetas[i]) - arm_theta + 180.0) % 360.0
                    - 180.0)
            if d < ARM_VETO_DEG:
                continue
        # local refine on E2
        fine = thetas[i] + np.radians(np.arange(-0.6, 0.61, 0.1))
        fs, fre, fsupp = ridge_sweep(img, gx, gy, fine)
        k = int(np.argmax(fs))
        th, e2, sp = float(fine[k]), float(fs[k]), float(fsupp[k])
        # direction safety: a ray tier must clearly beat its own reverse
        rs, _, _ = ridge_sweep(img, gx, gy, np.array([th + math.pi]))
        dir_ok = e2 >= 1.25 * max(float(rs[0]), 1.0)
        R, on, bg, steel = profile_arrays(img, gx, gy, th)
        locked, bsc, s, r0, n = profile_band_fit(R, on, bg, steel, bands,
                                                 r_end=float(fre[k]))
        key = (1 if locked else 0, bsc if locked else 0.0, e2)
        if best is None or key > best[0]:
            best = (key, dict(theta=math.degrees(th) % 360.0, e2=e2,
                              support=sp, dir_ok=dir_ok,
                              n=n if locked else 0, band_score=bsc,
                              s=s if locked else None,
                              r0=r0 if locked else None))
    return best[1] if best else None


# ---------------------------------------------------------------- selftest

def synth_scene(rng, easy=False):
    """easy=True: no bloom saturation, light speckle — all bands discrete
    (the regime where a full lock is REQUIRED). Default: harsh — long
    bloom-saturated steel stretches (address regime), heavy speckle; the
    machinery must abstain from (s, r0) rather than guess."""
    H, W = 1024, 1280
    img = rng.normal(38.0, 5.0, (H, W))
    my0 = int(H * rng.uniform(0.58, 0.72))
    img[my0:, :] = rng.normal(252.0, 2.0, (H - my0, W))
    for _ in range(2):                       # cloth panels (legs)
        x0 = int(rng.uniform(0.25, 0.65) * W)
        img[int(H*0.3):my0, x0:x0 + int(rng.uniform(60, 140))] = \
            rng.normal(rng.uniform(55, 90), 6.0)
    bx0 = int(rng.uniform(0.1, 0.8) * W)     # a bright shirt-ish blob
    img[int(H*0.15):int(H*0.28), bx0:bx0+120] = rng.normal(238.0, 6.0)
    gx, gy = W * 0.5 + rng.uniform(-60, 60), H * 0.45 + rng.uniform(-40, 40)
    theta = rng.uniform(0, 2 * math.pi)
    s = rng.uniform(0.25, 0.45)
    r0 = rng.uniform(80.0, 220.0)
    bands = [308.0, 362.0, 560.0, 758.0, 808.0, 854.0]
    ux, uy = math.cos(theta), math.sin(theta)
    bxp, byp = gx - s * r0 * ux, gy - s * r0 * uy
    phase = rng.uniform(0, math.pi)
    steel_amp = 70.0 if easy else 140.0
    pristine = img.copy()   # background reads must NOT see drawn shaft px
    for rmm in np.arange(0.0, 940.0, 0.8):
        px, py = bxp + s * rmm * ux, byp + s * rmm * uy
        ix, iy = int(px), int(py)
        if not (1 <= ix < W - 1 and 1 <= iy < H - 1):
            continue
        bgv = 252.0 if iy >= my0 else pristine[iy, ix]
        onband = any(abs(rmm - b) <= 12.5 for b in bands)
        if bgv > BG_HI:
            if onband:
                continue                      # white-on-white: invisible
            val = rng.uniform(120.0, 165.0)   # dark steel on blown mat
        elif onband:
            val = 255.0
        else:
            val = min(255.0, bgv + 25.0 + steel_amp * (0.5 + 0.5 * math.sin(
                phase + rmm / 180.0)))        # slow specular variation
        img[iy - 1:iy + 2, ix - 1:ix + 2] = val
    for _ in range(10 if easy else 40):      # saturated speckle junk
        sx, sy = int(rng.uniform(0, W)), int(rng.uniform(0, H))
        img[sy:sy + int(rng.uniform(1, 3)), sx:sx + int(rng.uniform(1, 3))] = 255.0
    img = np.clip(img + rng.normal(0, 6.0, (H, W)), 0, 255)
    n_vis = sum(1 for b in bands
                if 0 <= int(byp + s*b*uy) < my0)   # bands over non-mat
    ax, ay = gx + rng.uniform(-3, 3), gy + rng.uniform(-3, 3)
    return img.astype(np.float32), ax, ay, math.degrees(theta) % 360, s, r0, n_vis


def selftest():
    rng = np.random.default_rng(7)
    bands = [308.0, 362.0, 560.0, 758.0, 808.0, 854.0]

    def run(trial, easy):
        img, ax, ay, th_true, s_true, r0_true, n_vis = synth_scene(rng, easy)
        res = still_search(img, ax, ay, bands)
        measured = res is not None and (res["s"] is not None or res["dir_ok"])
        if not measured:
            print(f"  {'easy' if easy else 'hard'} {trial}: ABSTAIN "
                  f"(true theta {th_true:.1f}, vis={n_vis})")
            return None, None
        dth = abs((res["theta"] - th_true + 180.0) % 360.0 - 180.0)
        serr = abs(res["s"] - s_true) / s_true if res["s"] is not None else None
        print(f"  {'easy' if easy else 'hard'} {trial}: dtheta={dth:5.2f} "
              f"e2={res['e2']:5.0f} n={res['n']} vis={n_vis}"
              + (f" s_err={100*serr:.1f}%" if serr is not None else ""))
        return dth, serr

    hard = [run(t, False) for t in range(20)]
    easy = [run(t, True) for t in range(6)]
    ht = [d for d, _ in hard if d is not None]
    hs = [e for _, e in hard if e is not None]
    et = [d for d, _ in easy if d is not None]
    es = [e for _, e in easy if e is not None]
    print(f"hard: measured {len(ht)}/20 theta max={max(ht):.2f} "
          f"locks={len(hs)}" + (f" s_err max={100*max(hs):.1f}%" if hs else ""))
    print(f"easy: measured {len(et)}/6 theta max={max(et):.2f} "
          f"locks={len(es)}/6" + (f" s_err max={100*max(es):.1f}%" if es else ""))
    # honesty-first: every EMITTED theta right in both regimes; every
    # emitted (s, r0) right; easy scenes (discrete bands) MUST lock;
    # hard scenes may abstain from (s, r0) but must not guess
    ok = (len(ht) >= 15 and max(ht) <= 2.0
          and len(et) >= 5 and max(et) <= 2.0
          and len(es) >= 5 and (not es or max(es) <= 0.05)
          and (not hs or max(hs) <= 0.08))
    print("SELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


# ---------------------------------------------------------------- clip mode

def frame_band_match(gray, gx, gy, rmax, bands):
    """v1 per-frame blob detection + ratio match (validated machinery,
    identical constants) for motion frames where bands bloom discrete."""
    blobs = detect_blobs(gray, gx, gy, rmax)
    res = None
    if len(blobs) < 2:
        return None
    pts = np.array([(b[0], b[1]) for b in blobs])
    tried = []
    for (a, b) in itertools.combinations(range(len(pts)), 2):
        d = pts[b] - pts[a]
        nd = math.hypot(*d)
        if nd < 8:
            continue
        u = d / nd
        ang = math.atan2(u[1], u[0]) % math.pi
        if any(abs((ang - t + math.pi / 2) % math.pi - math.pi / 2) < 0.03
               for t in tried):
            continue
        tried.append(ang)
        rel = pts - pts[a]
        lat = np.abs(rel[:, 0] * u[1] - rel[:, 1] * u[0])
        idx = np.where(lat < LAT_TOL)[0]
        if len(idx) < 2:
            continue
        grel = np.array((gx, gy)) - pts[a]
        if abs(float(grel[0] * u[1] - grel[1] * u[0])) > GRIP_GATE:
            continue
        t0 = float(np.dot(grel, u))
        for sgn in (1.0, -1.0):
            tp = [sgn * (float(np.dot(p - pts[a], u)) - t0) for p in pts[idx]]
            n, rms, s, r0, pairs, fl = match_pattern(tp, bands)
            gate = RMS4 if n == 4 else RMS5
            if n >= 4 and rms <= gate:
                mpr = sorted(((pts[idx[j]][0], pts[idx[j]][1], bands[k])
                              for j, k in pairs), key=lambda m: m[2])
                if gap_dark_ok(gray, mpr, s) == "bright" or \
                        (n == 4 and gap_dark_ok(gray, mpr, s) != "dark"):
                    continue
                c = (n, -rms, s, r0, sgn * u,
                     [tuple(pts[idx[j]]) for j, _ in pairs])
                if res is None or c[:2] > res[:2]:
                    res = c
    if res is None:
        return None
    n, nrms, s, r0, u, mpts = res
    return dict(n=n, rms=-nrms, s=s, r0=r0,
                theta=math.degrees(math.atan2(u[1], u[0])) % 360.0,
                mbx=float(np.mean([p[0] for p in mpts])),
                mby=float(np.mean([p[1] for p in mpts])))


def circ_interp(f1, a1, f2, a2, f):
    d = (a2 - a1 + 180.0) % 360.0 - 180.0
    return (a1 + d * (f - f1) / max(f2 - f1, 1e-9)) % 360.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--anchors", required=True)
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--clipmeta", default=None)
    ap.add_argument("--fps-override", type=float, default=None)
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--overlay", action="store_true")
    ap.add_argument("--truth-out", action="store_true")
    args = ap.parse_args()

    rec = json.load(open(args.clubs))[args.club]
    bands = [float(r) for r in rec["bandCentersMm"]]
    r_len = float(rec["lengthMm"])
    anchors, phi = {}, {}
    for row in csv.reader(open(args.anchors)):
        anchors[int(row[0])] = (float(row[1]), float(row[2]))
        if len(row) >= 5 and int(row[4]):
            phi[int(row[0])] = float(row[3])

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = args.fps_override or cap.get(cv2.CAP_PROP_FPS) or 30.0
    frames = []
    while True:
        ok, fr = cap.read()
        if not ok:
            break
        frames.append(cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY))
    cap.release()
    nf = len(frames)
    rmax = 0.62 * H

    scene_med = np.median(np.stack(frames[::8]).astype(np.float32), axis=0)

    # still runs from grip-anchor speed
    speed = np.full(nf, 99.0)
    for f in range(1, nf):
        if f in anchors and f - 1 in anchors:
            speed[f] = math.hypot(anchors[f][0] - anchors[f-1][0],
                                  anchors[f][1] - anchors[f-1][1])
    from scipy.ndimage import median_filter as mf1
    sm_speed = mf1(speed, size=11)
    still = sm_speed < STILL_SPEED
    runs, f = [], 0
    while f < nf:
        if still[f]:
            g = f
            while g + 1 < nf and still[g + 1]:
                g += 1
            if g - f + 1 >= STILL_MIN:
                runs.append((f, g))
            f = g + 1
        else:
            f += 1

    out = {}                       # frame -> row dict
    # NOTE: the stacked still tier is CUT for v2.0 — corpus-adjudicated
    # unsafe (s02/s05: whole address runs locked a shirt-texture
    # counterfeit at s~0.21 and self-corroborated; s03 flipped). Still-run
    # machinery stays (synth-gated) for the address follow-up; see
    # RESULTS.md tape_20260705.

    # tier: band — v1 blob machinery per non-still frame
    for f in range(nf):
        if f in out or f not in anchors:
            continue
        gx, gy = anchors[f]
        r = frame_band_match(frames[f], gx, gy, rmax, bands)
        if r is not None:
            out[f] = dict(frame=f, tier="band", theta=r["theta"], n=r["n"],
                          s=r["s"], r0=r["r0"], e2=0.0, support=0.0,
                          mbx=r["mbx"], mby=r["mby"],
                          conf=min(0.9, 0.75 + 0.05 * (r["n"] - 4)))

    # NOTE: no swing-median scale gate — s legitimately shrinks with
    # foreshortening (finish holds measure s~0.20 vs 0.33 in-plane; that IS
    # the rho measurement). Scene-static junk is killed by the blob-motion
    # corroboration below instead.

    # corroboration pass: a band lock is kept only if another lock within
    # 4 frames agrees within rate tolerance. At peak speed the exposure-arc
    # spans ~25-30 deg and streaked-band centroids leave the shaft line —
    # f516 of s01 locked 130 deg off two frames before impact and chained
    # flipped rays after it (adjudicated); no lone measurements.
    # corroborating pair = theta agreement AND the matched blobs travel
    # with the hands (mat speckle does not ride a hand-held club — s04/s07
    # adjudicated; v1 made the same wrong locks). In static periods motion
    # cannot separate club from counterfeit and the counterfeits pass every
    # appearance gate, so static locks are UNVERIFIABLE -> never emitted
    # (costs a handful of finish-hold frames; buys zero junk).
    locks = sorted(out)
    keep = set()
    for f in locks:
        for g in locks:
            if g == f or abs(g - f) > 4:
                continue
            d = abs((out[f]["theta"] - out[g]["theta"] + 180.0) % 360.0
                    - 180.0)
            if d > 12.0 + 6.0 * abs(g - f):
                continue
            adisp = math.hypot(anchors[f][0] - anchors[g][0],
                               anchors[f][1] - anchors[g][1])
            bdisp = math.hypot(out[f]["mbx"] - out[g]["mbx"],
                               out[f]["mby"] - out[g]["mby"])
            # 1.5 px floor: junk centroid jitter (~0.3 px) defeats a purely
            # proportional threshold when the hands move slowly
            if adisp >= 1.0 and bdisp > max(1.5, 0.25 * adisp):
                keep.add(f)
                break
    out = {f: r for f, r in out.items() if f in keep}

    # tier: ray — scene-median-subtracted E2 near temporal prediction
    locked = sorted(out)
    lf = np.array(locked, dtype=float)
    for f in range(nf):
        if f in out or f not in anchors or not len(lf):
            continue
        i = int(np.searchsorted(lf, f))
        lo = locked[i - 1] if i > 0 else None
        hi = locked[i] if i < len(locked) else None
        if lo is not None and hi is not None and hi - lo <= RAY_GAP:
            dlh = abs((out[hi]["theta"] - out[lo]["theta"] + 180.0) % 360.0
                      - 180.0)
            if dlh > 100.0:
                continue   # theta path ambiguous (impact spans >180 deg)
            pred = circ_interp(lo, out[lo]["theta"], hi, out[hi]["theta"], f)
        elif lo is not None and f - lo <= RAY_ONESIDED:
            pred = out[lo]["theta"]
        elif hi is not None and hi - f <= RAY_ONESIDED:
            pred = out[hi]["theta"]
        else:
            continue
        gx, gy = anchors[f]
        diff = np.abs(frames[f].astype(np.float32) - scene_med)
        sweep = np.radians(pred + np.arange(-RAY_SWEEP, RAY_SWEEP + 0.01, 0.3))
        fs, _, fsupp = ridge_sweep(diff, gx, gy, sweep, bright_only=True)
        k = int(np.argmax(fs))
        if fs[k] < RAY_SCORE_MIN or fsupp[k] < RAY_SUPP_MIN:
            continue
        rev, _, _ = ridge_sweep(diff, gx, gy,
                                np.array([float(sweep[k]) + math.pi]),
                                bright_only=True)
        if fs[k] < 1.25 * max(float(rev[0]), 1.0):
            continue
        out[f] = dict(frame=f, tier="ray",
                      theta=math.degrees(float(sweep[k])) % 360.0,
                      n=0, s=None, r0=None, e2=float(fs[k]),
                      support=float(fsupp[k]), conf=0.55)

    rows = []
    for f in sorted(out):
        r = out[f]
        gx, gy = anchors[f]
        th = math.radians(r["theta"])
        ux, uy = math.cos(th), math.sin(th)
        if r["s"]:
            bx, by = gx - r["s"] * r["r0"] * ux, gy - r["s"] * r["r0"] * uy
            hx, hy = bx + r["s"] * r_len * ux, by + r["s"] * r_len * uy
        else:
            hx = hy = ""
        rows.append(dict(frame=f, t_s=round(f / fps, 6), tier=r["tier"],
                         n_match=r["n"], theta_deg=round(r["theta"], 2),
                         s_px_mm=round(r["s"], 5) if r["s"] else "",
                         r0_mm=round(r["r0"], 1) if r["r0"] else "",
                         head_x=round(hx, 1) if hx != "" else "",
                         head_y=round(hy, 1) if hy != "" else "",
                         e2=round(r["e2"], 1), support=round(r["support"], 2),
                         conf=r["conf"]))

    stem = os.path.splitext(os.path.basename(args.video))[0]
    os.makedirs(args.out_dir, exist_ok=True)
    out_csv = os.path.join(args.out_dir, f"{stem}_fusion.csv")
    cols = ["frame", "t_s", "tier", "n_match", "theta_deg", "s_px_mm",
            "r0_mm", "head_x", "head_y", "e2", "support", "conf"]
    with open(out_csv, "w", newline="") as fo:
        wcsv = csv.DictWriter(fo, fieldnames=cols)
        wcsv.writeheader()
        wcsv.writerows(rows)
    tc = {}
    for r in rows:
        tc[r["tier"]] = tc.get(r["tier"], 0) + 1
    print(f"frames={nf}  fusion-meas={len(rows)}  tiers: " +
          " ".join(f"{k}={v}" for k, v in sorted(tc.items())))
    print(f"[out] {out_csv}")

    if args.overlay:
        cap = cv2.VideoCapture(args.video)
        vw_fps = float(round(cap.get(cv2.CAP_PROP_FPS) or 30.0)) or 30.0
        byf = {r["frame"]: r for r in rows}
        vw = cv2.VideoWriter(os.path.join(args.out_dir, f"{stem}_fusion.mp4"),
                             cv2.VideoWriter_fourcc(*"mp4v"), vw_fps, (W, H))
        f = 0
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            r = byf.get(f)
            if r is not None:
                gx, gy = anchors[f]
                th = math.radians(r["theta_deg"])
                ux, uy = math.cos(th), math.sin(th)
                if r["head_x"] != "":
                    cv2.line(frame, (int(gx - 60 * ux), int(gy - 60 * uy)),
                             (int(r["head_x"]), int(r["head_y"])),
                             (0, 0, 255), 2)
                    cv2.circle(frame, (int(r["head_x"]), int(r["head_y"])),
                               10, (255, 0, 255), 2)
                else:
                    cv2.line(frame, (int(gx), int(gy)),
                             (int(gx + 380 * ux), int(gy + 380 * uy)),
                             (0, 200, 255), 2)
                cv2.putText(frame, f"f{f} {r['tier']} th={r['theta_deg']:.1f}"
                            f" n={r['n_match']} conf={r['conf']}",
                            (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                            (255, 255, 255), 2)
            else:
                cv2.putText(frame, f"f{f} absent", (10, 26),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (128, 128, 128), 2)
            vw.write(frame)
            f += 1
        cap.release()
        vw.release()

    if args.truth_out:
        if not args.clipmeta:
            sys.exit("--truth-out requires --clipmeta")
        meta = json.load(open(args.clipmeta))
        tt = meta["t_us"]
        entries = []
        for r in rows:
            gx, gy = anchors[r["frame"]]
            e = dict(t_us=int(tt[r["frame"]]),
                     theta=round(math.radians(r["theta_deg"]), 6),
                     grip=[round(gx, 1), round(gy, 1)],
                     tier=r["tier"], conf=r["conf"])
            if r["head_x"] != "":
                e["head"] = [r["head_x"], r["head_y"]]
                e["len"] = round(math.hypot(r["head_x"] - gx,
                                            r["head_y"] - gy), 1)
                e["n_bands"] = r["n_match"]
            entries.append(e)
        tj = dict(meta=dict(club=args.club, source="instrumented",
                            tool="stripe_fusion", n=len(entries)),
                  shaft=entries)
        tpath = os.path.join(meta["swingDir"], "truth.json")
        if os.path.exists(tpath):
            old = json.load(open(tpath))
            if old.get("meta", {}).get("source") != "instrumented":
                sys.exit(f"refusing to overwrite non-instrumented {tpath}")
        json.dump(tj, open(tpath, "w"), indent=1)
        print(f"[truth] {tpath} ({len(entries)} entries)")


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    main()
