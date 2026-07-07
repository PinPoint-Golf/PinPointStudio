#!/usr/bin/env python3
"""club_track_v3.py — v3.0 constrained global shaft estimator.

Design: docs/design/club_tracking_v3_design.md; plan:
docs/implementation/shaft_detection_v3_impl.md. FULL PEDAGOGICAL EXPLANATION
(the reference "bible", written for a reader new to golf / vision / the DP /
the statistics; the durable record for the C++ port):
docs/design/club_track_v3_exemplar_explained.md.

Physics-first. The four fundamentals are promoted to first-class constraints
and the shaft direction theta(t) is estimated GLOBALLY (Viterbi DP over a theta
grid across the whole clip) rather than per frame:

  C1  butt-termination   band locks must place the butt within r0 in (0,260mm]
                         behind the grip; a ridge that continues (off-arm) behind
                         the grip is a scene line -> penalised.
  C2  body-overlap veto  per-frame body polygon from skeleton.csv (8 joints +
                         margin, temporally smoothed). A candidate whose shaft
                         ray lies majority-inside the body is vetoed in
                         takeaway->thru; admitted-not-sufficient at addr/impact/
                         finish. The ROI is a GEOMETRIC convex-hull half-plane test
                         (body_polys) by default -- pure pose geometry, no raster;
                         --raster-c2 selects the original dilated-bitmap form
                         (body_masks), ~2x slower and accuracy-identical.
  C3  phase-signed rot   phase model from the HANDS ALONE (anchors.csv): the
                         swing reverses once, at the top. theta increases through
                         the backswing and decreases (monotone, wrapping mod 360)
                         through downswing->impact->thru->finish. A 180 deg flip
                         becomes structurally impossible; the impact gap is
                         bridged as a monotone bounded-rate sweep.
  C4  reachable cone     psi = theta - phi (phi = lead-arm dir, anchors.csv).
                         EMPIRICAL (s01): raw phi from pose is noisy at the top /
                         in the blur zone (frame-to-frame jumps to ~87 deg), so
                         the cone is WIDE (a robustly-smoothed phi + generous
                         bounds) and is disabled at address/finish where psi is
                         unbounded. It removes the reverse half-circle and the
                         into-forearm direction; C3 + DP smoothness do the fine
                         work.

Evidence engines are UNCHANGED and imported from stripe_fusion (E1 band matcher,
E2 polarity-aware ridge). Deterministic: no RNG; the DP is exact.

  club_track_v3.py <clip> --anchors a.csv --clubs clubs.json --club "7 IRON"
      [--skeleton skeleton.csv] [--clipmeta m.json] [--fps-override F]
      [--impact-frame N | --impact-us U] [--out-dir out] [--overlay]
      [--truth-out] [--phases-out]
"""
import argparse, csv, json, math, os, sys
import numpy as np, cv2
from scipy.ndimage import median_filter, gaussian_filter1d

from stripe_fusion import (ridge_sweep, frame_band_match, BG_HI, ARM_VETO_DEG)

# ---- grid / DP ---------------------------------------------------------
GRID = 1.0                       # theta grid step (deg)
NS = int(round(360.0 / GRID))
# per-phase max |dtheta| per frame (deg/frame). s01 peaks ~16 deg/f at impact
# (149 fps); generous ceilings so the true sweep is never clipped.
WMAX = {"addr": 3.0, "backswing": 9.0, "top": 5.0, "downswing": 16.0,
        "impact": 24.0, "thru": 16.0, "finish": 11.0}
# expected sign of dtheta relative to swing chirality (0 = either direction)
PHASE_SIGN = {"addr": 0, "backswing": +1, "top": 0, "downswing": -1,
              "impact": -1, "thru": -1, "finish": -1}
MIDSWING = {"backswing", "top", "downswing", "impact", "thru"}   # C2 vetoes here

# ---- cost weights (cost units; DP minimises) ---------------------------
W_E2 = 10.0          # emission: 0 (best evidence) .. W_E2 (none)
W_BAND = 8.0         # extra emission reward at a band-locked theta
W_ARM = 16.0         # C4 into-forearm veto
W_C1 = 10.0          # C1 off-arm reverse-support (scene line through hands)
W_C2 = 13.0          # C2 body-overlap veto
W_CONE = 4.0         # C4 wide-cone soft penalty
K_SMOOTH = 0.03      # transition theta-smoothness (per deg^2)
C_SIGN = 40.0        # wrong-direction transition penalty (soft, not inf)
CONE_HALF = 150.0    # C4 wide half-cone about phi (deg), chirality-centred
C1_TOL = 0.45        # reverse-ridge normalised strength above which C1 bites
RAY_EV_MIN = 0.45    # normalised E2 evidence needed to call a frame 'ray'
BAND_TOL = 6.0       # |theta* - theta_band| (deg) to claim the band tier
STILL_SPEED = 0.8    # grip px/frame below which (sustained) a frame is 'static'
STILL_MIN = 25       # min static-run length (frames)
SPAN_COLLAR_US = 100_000   # settling padding each side of the moving span [bs0,fin0]
                     # processed by the expensive evidence/veto ops, in MICROSECONDS
                     # (fps-independent: converted to frames per clip via fps, so a
                     # faster/slower recording keeps the same time margin). The club
                     # can move just before the hands cross SW_SPD (and settle just
                     # after); the collar keeps those in. Held addr/finish frames
                     # outside it get flat emission -> DP-held pred tier. 100 ms ~= 15
                     # frames at 149 fps; ~140-frame span + collar of 745 -> ~4-5x
                     # fewer heavy frames.
BAND_NEAR = 5        # a static ray is admissible within this many frames of a band lock

# ---- C4 recast: psi-monotonicity as TRUTH, reconciled by isotonic fit (v3.0-r1)
# psi = theta - phi obeys C3's one-reversal law on the WRIST: cocks (addr->top),
# reverses ONCE at transition, releases (transition->impact->finish). Re-hinge in
# the downswing / un-hinge in the backswing is anatomically impossible -- so a
# NON-monotone psi is not a fact to be penalised, it is a MEASUREMENT of error
# (Mark, 2026-07-06). We therefore treat monotone-psi as ground truth and FIT it
# (per-phase weighted robust ISOTONIC regression / Pool-Adjacent-Violators),
# reading the error off the residual, instead of a per-frame DP penalty (which
# fired on the pose-phi noise floor -- 25-62% of backswing steps show a ~3deg
# apparent reversal that is pure noise). Design: club_tracking_v3_design.md sec.8.
#
# Two regimes, one principle -- attribute the error to the UNCERTAIN source:
#   * blur zone (impact ONLY): the club blurs, theta is unreliable, the ARM is
#     the better witness -> reconstruct theta = psi_iso + phi (arm-witness bridge)
#     for non-band frames. This also rejects a psi-non-monotone counterfeit (a
#     bright line whose implied wrist re-hinges): it departs from the band-
#     anchored monotone psi and is pulled back.
#   * elsewhere (backswing/downswing/thru/finish): theta is well-measured, phi is
#     the noisy one -> KEEP the measured theta; the residual is a phi-error /
#     confidence map (phi_clean = theta - psi_iso is available). Never inject phi
#     noise into a good club measurement. NB the FOLLOW-THROUGH (thru) lives here,
#     NOT in the blur zone: post-impact the re-tracked club is accurate but the arm
#     folds, so its psi-residual is a roll-onset signal (see RECON_PHASES), not a
#     correction. The isotonic fit still SPANS impact+thru (one release block) so
#     the monotone law bridges the blur; only the theta WRITE-BACK is impact-only.
# Band locks are pinned (invariant 1) and weight the fit most; the fit is robust
# (Huber-IRLS) so one phi-glitch anchor cannot drag it. Deterministic (PAVA exact).
ARM_OUTLIER_DEG = 20.0   # smooth_phi Hampel gate: max plausible arm-rate (deg/f)
W_ISO = {"band": 8.0, "ray": 2.0, "pred": 0.3}   # isotonic weight by confidence
ISO_HUBER = 8.0      # Huber knee (deg): residuals beyond this are down-weighted
ISO_ITERS = 3        # IRLS reweight iterations
RECON_PHASES = ("impact",)   # blur zone: reconstruct non-band theta here. IMPACT
                     # ONLY (v3.0-r1 refinement 2026-07-07): the impact blur is the
                     # sole span where theta_club < phi_arm reliability. Through the
                     # FOLLOW-THROUGH (thru) the club is re-tracked & accurate while
                     # phi (folding arm) degrades, so psi_iso+phi would pull a GOOD
                     # ray off truth (corpus: thru p90 3.9->5.5, coverage 678->649
                     # when thru was reconstructed). Physics (Mark, 2026-07-07): psi
                     # is a DOUBLE reversal -- cock->top, release->impact, passive
                     # centripetal RE-HINGE through the follow-through -- and between
                     # them the dominant motion is forearm ROLL about the shaft long
                     # axis, a 3rd DOF that is near-unobservable face-on (axially
                     # symmetric shaft). So the single-tent release law holds only
                     # addr->impact; the thru psi-residual is kept as a roll-onset /
                     # release-complete SIGNAL, not a theta correction (deferred to
                     # club IMU / DTL / clubhead stage for a real 3rd dimension).
RECON_TOL = 6.0      # if reconstruction moves theta > this from its evidence, the
                     # frame is retiered 'recon' (physics, not a direct measurement)
PSI_WIN_BACK = 3     # psi-reversal free window (excluded from the fit): frames of
PSI_WIN_FWD = 12     # C3-top before / after (release lag: psi peaks ~8f post-top)

# ---- reused radii for body-mask ray sampling ---------------------------
BODY_MARGIN = 34.0   # px dilation of the body polygon
BODY_R = np.arange(45.0, 470.0, 14.0)   # ray radii for the inside-fraction test


def circ_wrap(a):
    return (a + 180.0) % 360.0 - 180.0


def smooth_phi(phi_deg, win=9, hampel_deg=ARM_OUTLIER_DEG):
    """Robust unit-vector smoothing of the lead-arm direction (handles wrap).

    Hardened for the v3.0-r1 psi reconciliation (docs/design §8): pose phi spikes
    to ~87 deg/frame at the top and through the impact blur (s01 gate-0). The
    isotonic fit reads psi = theta - phi, so a phi spike corrupts psi directly.
    BEFORE the median+Gaussian we Hampel-reject any sample whose angular deviation
    from its local circular median exceeds hampel_deg -- the arm cannot physically
    rotate that fast between frames at >=120 fps -- and replace it with that
    median. Idempotent on clean phi; the fixed physical gate never eats real arm
    motion (a few deg/frame). Consumed unchanged by address_theta_v3."""
    phr = np.radians(np.asarray(phi_deg, float))
    c, s = np.cos(phr), np.sin(phr)
    cm, sm = median_filter(c, win), median_filter(s, win)      # local circular median dir
    # |ang(v) - ang(median)| in deg: sin(dv)=s*cm-c*sm, cos(dv)=c*cm+s*sm
    dev = np.abs(np.degrees(np.arctan2(s * cm - c * sm, c * cm + s * sm)))
    bad = dev > hampel_deg
    c = np.where(bad, cm, c); s = np.where(bad, sm, s)
    cx = gaussian_filter1d(median_filter(c, win), 3)
    cy = gaussian_filter1d(median_filter(s, win), 3)
    return np.degrees(np.arctan2(cy, cx))


def _pava(y, w, increasing=True):
    """Weighted isotonic regression by Pool-Adjacent-Violators. Exact, O(n),
    deterministic. Returns the monotone sequence minimising sum w*(x-y)^2."""
    s = 1.0 if increasing else -1.0
    means = list(s * np.asarray(y, float))
    wts = list(np.asarray(w, float))
    idx = [[i] for i in range(len(means))]
    i = 0
    while i < len(means) - 1:
        if means[i] > means[i + 1] + 1e-12:                # adjacent violation -> pool
            nw = wts[i] + wts[i + 1]
            means[i] = (means[i] * wts[i] + means[i + 1] * wts[i + 1]) / nw
            wts[i] = nw; idx[i] += idx[i + 1]
            del means[i + 1]; del wts[i + 1]; del idx[i + 1]
            if i > 0:
                i -= 1
        else:
            i += 1
    out = np.zeros(len(y))
    for m, ii in zip(means, idx):
        for k in ii:
            out[k] = s * m
    return out


def robust_isotonic(y, w, increasing=True, huber=ISO_HUBER, iters=ISO_ITERS):
    """Huber-IRLS isotonic fit: down-weight anchors whose residual exceeds the
    Huber knee so a single phi-glitch measurement cannot drag the monotone fit."""
    y = np.asarray(y, float); w = np.asarray(w, float)
    x = _pava(y, w, increasing)
    for _ in range(iters):
        r = np.abs(y - x)
        hw = np.where(r <= huber, 1.0, huber / (r + 1e-9))
        x = _pava(y, w * hw, increasing)
    return x


def reconcile_psi(theta, phi_s, phase, band, ev_at, top, nf, tier_hint):
    """Treat monotone psi as ground truth and fit it per phase (robust weighted
    isotonic). Returns (theta_out, psi_resid, recon). In the blur zone
    (RECON_PHASES) non-band theta is reconstructed as psi_iso + phi (the arm is
    the witness); elsewhere theta is preserved and psi_resid is a phi-error map.
    The reversal window [top-B, top+F] is excluded (psi is free at transition)."""
    theta_out = np.array(theta, float)
    resid = np.full(nf, np.nan)
    recon = np.zeros(nf, bool)
    lo, hi = top - PSI_WIN_BACK, top + PSI_WIN_FWD
    # (block phases, increasing?) -- backswing cocks, the release un-cocks
    for phs, inc in ((("backswing",), True),
                     (("downswing", "impact", "thru"), False)):
        fs = [f for f in range(nf) if phase[f] in phs and not (lo <= f <= hi)
              and not np.isnan(theta[f])]
        if len(fs) < 4:
            continue
        th = np.array([theta[f] for f in fs]); ph = np.array([phi_s[f] for f in fs])
        psi = np.degrees(np.unwrap(np.radians(th - ph)))      # continuous psi (no 360 jumps)
        # weight the fit by confidence -- BUT in the blur zone a bright ridge is
        # untrustworthy (motion-smeared, or an outright counterfeit), so only
        # confirmed BAND anchors count there: the fit then INTERPOLATES psi across
        # the blur from the trusted measurements flanking it, rather than being
        # dragged onto the local evidence. This is what rejects a psi-non-monotone
        # counterfeit AND bridges the true blur (the arm carries phi through it).
        def wgt(f):
            if band[f] is None and phase[f] in RECON_PHASES:
                return W_ISO["pred"]
            return W_ISO[tier_hint(f)]
        w = np.array([wgt(f) for f in fs])
        iso = robust_isotonic(psi, w, increasing=inc)
        for i, f in enumerate(fs):
            resid[f] = abs(psi[i] - iso[i])
            if phase[f] in RECON_PHASES and band[f] is None:  # blur: arm is the witness
                theta_out[f] = (iso[i] + ph[i]) % 360.0
                recon[f] = True
    return theta_out, resid, recon


def segment_phases(gx, gy, nf, fps, impact_frame=None):
    """Hands-only phase model (C3). Returns (phase[nf] str, chir int, landmarks)."""
    spd = np.zeros(nf); spd[1:] = np.hypot(np.diff(gx), np.diff(gy))
    spd_s = gaussian_filter1d(median_filter(spd, 5), 2)
    SW_SPD = 8.0                                   # swing >> waggle (~4-6 px/f)
    mo = spd_s > SW_SPD
    runs, f = [], 0
    while f < nf:
        if mo[f]:
            g = f
            while g + 1 < nf and mo[g + 1]:
                g += 1
            if g - f >= 6:
                runs.append((f, g))
            f = g + 1
        else:
            f += 1
    if len(runs) < 1:
        # no swing detected: whole clip 'addr' (conservative fallback)
        return ["addr"] * nf, 0, 0, nf // 2, nf - 1, spd_s
    runs.sort(key=lambda r: -(r[1] - r[0]))
    big = sorted(runs[:2], key=lambda r: r[0])
    bs0 = big[0][0]                                # takeaway start
    if len(big) >= 2:
        gap_lo, gap_hi = big[0][1], big[1][0]
        top = gap_lo + int(np.argmin(spd_s[gap_lo:gap_hi + 1])) if gap_hi > gap_lo else big[0][1]
        ds_end = big[1][1]
    else:
        top = bs0 + int(np.argmax(gy[bs0:big[0][1] + 1] * -1))   # grip apex proxy
        ds_end = big[0][1]
    addr_gy = float(np.median(gy[:max(bs0, 1)]))
    if impact_frame is None:
        # hands-only: first post-top frame where the grip is back to address
        # height (club has returned to the ball line) inside the downswing run.
        cand = [f for f in range(top, ds_end + 1) if gy[f] >= addr_gy - 20.0]
        impact_frame = cand[0] if cand else (top + ds_end) // 2
    impact_frame = int(np.clip(impact_frame, top + 1, nf - 1))
    fin0 = min(ds_end, nf - 1)                      # finish begins as motion decays
    IMP = 12                                         # impact zone half-width (frames)
    phase = []
    for f in range(nf):
        if f < bs0:
            phase.append("addr")
        elif f < top - 2:
            phase.append("backswing")
        elif f <= top + 2:
            phase.append("top")
        elif abs(f - impact_frame) <= IMP:
            phase.append("impact")
        elif f < impact_frame:
            phase.append("downswing")
        elif f <= fin0:
            phase.append("thru")
        else:
            phase.append("finish")
    # chirality from the unwrapped smoothed phi over the backswing
    return phase, bs0, top, impact_frame, fin0, spd_s


def _smoothed_joints(skel_path, nf):
    """Per-frame skeleton joints (xs, ys each nf x nj), temporally smoothed.
    None if no skeleton. Shared by both C2 forms (raster masks and geometric ROI)."""
    if not skel_path or not os.path.exists(skel_path):
        return None
    J = {}
    for row in csv.reader(open(skel_path)):
        f = int(row[0]); vals = [float(x) for x in row[1:]]
        pts = [(vals[3 * i], vals[3 * i + 1], vals[3 * i + 2]) for i in range(len(vals) // 3)]
        J[f] = pts
    nj = len(next(iter(J.values())))
    xs = np.zeros((nf, nj)); ys = np.zeros((nf, nj))
    for f in range(nf):
        p = J.get(f) or J.get(min(J, key=lambda k: abs(k - f)))
        for i in range(nj):
            xs[f, i], ys[f, i] = p[i][0], p[i][1]
    for i in range(nj):                              # temporal smoothing per joint
        xs[:, i] = gaussian_filter1d(median_filter(xs[:, i], 5), 2)
        ys[:, i] = gaussian_filter1d(median_filter(ys[:, i], 5), 2)
    return xs, ys


def body_masks(skel_path, nf, W, H):
    """C2 body ROI as a per-frame filled+dilated raster (uint8 HxW) — the ORIGINAL
    image-processing form: rasterise the joint convex hull and Minkowski-dilate by
    BODY_MARGIN (a 2*k+1 ellipse). Correct but ~54% of the tracker's runtime (one
    full-res dilation/frame). See body_polys for the equivalent geometric ROI."""
    sj = _smoothed_joints(skel_path, nf)
    if sj is None:
        return None
    xs, ys = sj
    masks = []
    for f in range(nf):
        pts = np.stack([xs[f], ys[f]], 1).astype(np.float32)
        hull = cv2.convexHull(pts).astype(np.int32)
        m = np.zeros((H, W), np.uint8)
        cv2.fillConvexPoly(m, hull, 255)
        k = int(BODY_MARGIN)
        m = cv2.dilate(m, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * k + 1, 2 * k + 1)))
        masks.append(m)
    return masks


def body_polys(skel_path, nf):
    """C2 body ROI as per-frame convex-hull HALF-PLANES (outward unit normals n_i +
    offsets d_i) — the GEOMETRIC form of the same veto. A point p lies inside the
    body inflated by BODY_MARGIN iff max_i(n_i . p - d_i) <= BODY_MARGIN. Pure pose
    geometry: no raster, no dilation, ~50x cheaper than body_masks. Same smoothed
    joints, so it differs from the raster only by mitered-vs-rounded hull corners."""
    sj = _smoothed_joints(skel_path, nf)
    if sj is None:
        return None
    xs, ys = sj
    polys = []
    for f in range(nf):
        hull = cv2.convexHull(np.stack([xs[f], ys[f]], 1).astype(np.float32)).reshape(-1, 2)
        hull = hull.astype(np.float64)
        c = hull.mean(0)
        e = np.roll(hull, -1, axis=0) - hull                 # edge vectors
        n = np.stack([e[:, 1], -e[:, 0]], 1)                 # a normal per edge
        n /= (np.hypot(n[:, 0], n[:, 1])[:, None] + 1e-9)
        flip = np.einsum("ij,ij->i", n, hull + 0.5 * e - c) < 0   # orient outward
        n[flip] *= -1.0
        d = np.einsum("ij,ij->i", n, hull)                   # offsets (n_i . vertex_i)
        polys.append((n, d))
    return polys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--anchors", required=True)
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--skeleton", default=None, help="skeleton.csv (default: next to clip)")
    ap.add_argument("--clipmeta", default=None)
    ap.add_argument("--fps-override", type=float, default=None)
    ap.add_argument("--impact-frame", type=int, default=None)
    ap.add_argument("--impact-us", type=float, default=None)
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--overlay", action="store_true")
    ap.add_argument("--truth-out", action="store_true")
    ap.add_argument("--phases-out", action="store_true")
    ap.add_argument("--no-psi-rail", action="store_true",
                    help="disable the v3.0-r1 psi-monotonicity reconciliation "
                         "(reverts C4 to v3.0 wide-cone-only: no isotonic fit, no "
                         "arm-witness reconstruction; for A/B regression and the "
                         "synth counterfeit gate)")
    ap.add_argument("--raster-c2", action="store_true",
                    help="C2 body veto via the ORIGINAL rasterised+dilated body mask "
                         "instead of the DEFAULT geometric pose-ROI test. The raster "
                         "form is ~2x slower overall (a full-res dilation per frame) "
                         "and accuracy-identical (corpus A/B: median-err delta 0.000 "
                         "deg); kept as the byte-oracle / for regression.")
    ap.add_argument("--no-span-bound", action="store_true",
                    help="DISABLE swing-span bounding: run the expensive per-frame "
                         "evidence + veto ops on ALL frames instead of only the moving "
                         "span [bs0,fin0] (+/- SPAN_COLLAR_US). Default bounds them to "
                         "the swing (~5x fewer heavy frames; held addr/finish frames "
                         "are pred-tier holds). This flag is the byte-oracle for the "
                         "bounded-vs-unbounded A/B.")
    args = ap.parse_args()

    rec = json.load(open(args.clubs))[args.club]
    bands = [float(r) for r in rec["bandCentersMm"]]
    r_len = float(rec["lengthMm"])

    anchors, phi_raw = {}, {}
    for row in csv.reader(open(args.anchors)):
        f = int(row[0]); anchors[f] = (float(row[1]), float(row[2]))
        phi_raw[f] = float(row[3]) if (len(row) >= 5 and int(row[4])) else np.nan

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = args.fps_override or cap.get(cv2.CAP_PROP_FPS) or 30.0
    frames = []                                      # uint8 gray (memory: 16 GB box)
    while True:
        ok, fr = cap.read()
        if not ok:
            break
        frames.append(cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY))
    cap.release()
    nf = len(frames)
    rmax = 0.62 * H
    scene_med = np.median(np.stack(frames[::8]).astype(np.float32), axis=0)

    gx = np.array([anchors.get(f, (np.nan, np.nan))[0] for f in range(nf)])
    gy = np.array([anchors.get(f, (np.nan, np.nan))[1] for f in range(nf)])
    phv = np.array([phi_raw.get(f, np.nan) for f in range(nf)])
    # fill phi gaps then smooth
    idx = np.arange(nf); good = ~np.isnan(phv)
    if good.sum():
        phv = np.interp(idx, idx[good], phv[good])
    phi_s = smooth_phi(phv)                          # robustly de-spiked lead-arm dir

    impf = args.impact_frame
    if impf is None and args.impact_us is not None and args.clipmeta:
        tt = np.array(json.load(open(args.clipmeta))["t_us"], float)
        impf = int(np.argmin(np.abs(tt - args.impact_us)))
    phase, bs0, top, impf, fin0, spd_s = segment_phases(gx, gy, nf, fps, impf)
    phu = np.unwrap(np.radians(phi_s))
    chir = 1 if (phu[min(top, nf - 1)] - phu[bs0]) >= 0 else -1
    use_psi = not args.no_psi_rail                   # psi-monotonicity reconciliation

    skel = args.skeleton or os.path.join(os.path.dirname(args.video), "skeleton.csv")
    if args.raster_c2:
        masks = body_masks(skel, nf, W, H); polys = None  # original rasterised+dilated mask
    else:
        polys = body_polys(skel, nf); masks = None        # DEFAULT: geometric pose ROI (~2x faster)

    # static-run mask: a sustained low grip-speed span. A ray lock inside a
    # static run cannot be motion-verified (v2-adjudicated: static-period
    # counterfeits pass every appearance gate), so such rays are demoted to
    # 'pred' unless a real band lock sits within BAND_NEAR frames.
    still = spd_s < STILL_SPEED
    static = np.zeros(nf, bool)
    f = 0
    while f < nf:
        if still[f]:
            g = f
            while g + 1 < nf and still[g + 1]:
                g += 1
            if g - f + 1 >= STILL_MIN:
                static[f:g + 1] = True
            f = g + 1
        else:
            f += 1

    TR = np.radians(np.arange(0.0, 360.0, GRID))     # grid theta (rad)
    TDEG = np.degrees(TR)

    # ---- per-frame evidence + constraint penalties --------------------
    # Swing-span bounding: the evidence engines (ridge_sweep x2, frame_band_match)
    # and the C2 veto are ~85% of the runtime and dominate compute. Only ~19% of a
    # clip is the moving swing (measured on tape_20260705); the rest is the golfer
    # held at address/finish where the shaft is static and the answer is constant.
    # Run the heavy ops ONLY on [bs0,fin0] +/- SPAN_COLLAR_US; held frames keep flat
    # emission (W_E2) and are bridged to a pred-tier hold by the DP smoothness. The
    # DP + tiering still run over ALL frames (cheap), so the output shape is
    # unchanged. Degenerate span (segment_phases fallback -> bs0=0,fin0=nf-1) clamps
    # to the whole clip: safe by construction (full processing, never under-cover).
    # --no-span-bound processes everything = the byte-oracle for the A/B.
    collar = int(round(SPAN_COLLAR_US * 1e-6 * fps))   # us -> frames for this clip
    span_lo = 0 if args.no_span_bound else max(0, bs0 - collar)
    span_hi = nf - 1 if args.no_span_bound else min(nf - 1, fin0 + collar)
    n_proc = 0
    emis = np.full((nf, NS), W_E2)                   # emission cost
    EV = np.zeros((nf, NS))                          # normalised E2 evidence (pre-gate)
    band = [None] * nf                               # band-lock dict per frame
    inside = np.zeros((nf, NS))                      # C2 inside-fraction
    for f in range(nf):
        if f not in anchors or np.isnan(gx[f]):
            continue
        if not (span_lo <= f <= span_hi):            # held frame: skip heavy ops
            continue
        n_proc += 1
        g = (gx[f], gy[f]); gray = frames[f].astype(np.float32)
        s_raw, _, sup_raw = ridge_sweep(gray, g[0], g[1], TR)
        diff = np.abs(gray - scene_med)
        s_dif, _, sup_dif = ridge_sweep(diff, g[0], g[1], TR, bright_only=True)

        def norm(s):
            lo = np.percentile(s, 50); hi = np.percentile(s, 97)
            return np.clip((s - lo) / (hi - lo + 1e-6), 0.0, 1.0)
        ev = np.maximum(norm(s_raw), norm(s_dif))    # E2 evidence in [0,1]
        EV[f] = ev

        # band lock (E1) for discrete-bloom frames (needs uint8)
        bm = frame_band_match(frames[f], g[0], g[1], rmax, bands)
        if bm is not None and 0.0 < bm["r0"] <= 260.0:      # C1 (full form)
            band[f] = bm
            bi = int(round(bm["theta"] / GRID)) % NS
            ev[bi] = max(ev[bi], 1.0)

        em = W_E2 * (1.0 - ev)

        # C4 arm-veto: shaft never points INTO the lead forearm (phi+180)
        arm = (phi_s[f] + 180.0) % 360.0
        d_arm = np.abs(circ_wrap(TDEG - arm))
        em += np.where(d_arm < ARM_VETO_DEG, W_ARM, 0.0)

        # C4 wide reachable cone (chirality-centred), active off addr/finish
        if phase[f] not in ("addr", "finish", "top"):
            cen = phi_s[f] + chir * (CONE_HALF - 40.0)      # bias toward shaft side
            d_cone = np.abs(circ_wrap(TDEG - cen))
            em += np.where(d_cone > CONE_HALF, W_CONE, 0.0)

        # C1 (weak form for rays): reverse ridge OFF the forearm = scene line
        rev = norm(s_raw)[(np.arange(NS) + NS // 2) % NS]   # ridge along theta+180
        rev_arm = np.abs(circ_wrap(TDEG - ((phi_s[f]) % 360.0)))  # reverse ~ phi
        em += np.where((rev > C1_TOL) & (rev_arm > ARM_VETO_DEG), W_C1, 0.0)

        # C2 body-overlap veto (mid-swing only) — raster mask OR geometric ROI
        if phase[f] in MIDSWING and (masks is not None or polys is not None):
            ux, uy = np.cos(TR), np.sin(TR)
            Px = g[0] + np.outer(ux, BODY_R)                 # (NS, nR) ray sample pts
            Py = g[1] + np.outer(uy, BODY_R)
            if polys is not None:                            # geometric half-plane test
                n, d = polys[f]                              # (E,2), (E,)
                sd = (n[:, 0, None, None] * Px[None] + n[:, 1, None, None] * Py[None]
                      - d[:, None, None])                    # (E, NS, nR) signed line dist
                frac = (sd.max(axis=0) <= BODY_MARGIN).mean(axis=1)
            else:                                            # rasterised-mask lookup
                X = np.clip(Px.astype(np.int32), 0, W - 1)
                Y = np.clip(Py.astype(np.int32), 0, H - 1)
                frac = (masks[f][Y, X] > 0).mean(axis=1)
            inside[f] = frac
            em += np.where(frac > 0.5, W_C2, 0.0)

        # a ratio-verified, butt-terminated band lock is the strongest anchor:
        # a negative-emission well that dominates the gates and forces the DP
        # onto the true (wrapping) global path (else it takes the smoother
        # wrong branch through the evidence-free impact gap -- s01-adjudicated).
        if band[f] is not None:
            bi = int(round(band[f]["theta"] / GRID)) % NS
            em[bi] = -W_BAND

        emis[f] = em

    # ---- global Viterbi DP over the theta grid (C3: banded, sign-locked) ---
    # Pure C3 here -- the psi-monotonicity constraint is NOT a per-frame DP
    # penalty (that fired on the pose-phi noise floor); it is applied AFTER the DP
    # as a robust isotonic reconciliation (reconcile_psi below).
    INF = 1e9
    cost = emis[0].copy()
    back = np.zeros((nf, NS), np.int32)
    for f in range(1, nf):
        ph = phase[f]
        wmax = int(math.ceil(WMAX[ph] / GRID))
        sgn = PHASE_SIGN[ph]
        if sgn > 0:
            shifts = range(0, wmax + 1)
        elif sgn < 0:
            shifts = range(-wmax, 1)
        else:
            shifts = range(-wmax, wmax + 1)
        best = np.full(NS, INF); barg = np.zeros(NS, np.int32)
        for d in shifts:
            t = K_SMOOTH * (d * GRID) ** 2                    # C3 theta smoothness
            cand = np.roll(cost, d) + t
            src = (np.arange(NS) - d) % NS
            upd = cand < best
            best[upd] = cand[upd]; barg[upd] = src[upd]
        cost = best + emis[f]
        back[f] = barg
    thstar = np.zeros(nf, np.int32)
    thstar[-1] = int(np.argmin(cost))
    for f in range(nf - 1, 0, -1):
        thstar[f - 1] = back[f, thstar[f]]
    theta = TDEG[thstar]                              # deg per frame (C3 track)

    # ---- psi reconciliation: monotone-psi is truth, fit it (isotonic) ------
    # Build a per-frame confidence hint (band > ray > pred) for the fit weights,
    # then reconcile: reconstruct blur-zone (impact/thru) non-band theta from the
    # ARM (psi_iso + phi), keep measured theta elsewhere, and record the residual
    # as a phi-error / confidence map. reconcile_psi excludes the top window.
    ev_at = np.array([EV[f, thstar[f]] for f in range(nf)])

    def _tier_hint(f):
        if band[f] is not None:
            return "band"
        return "ray" if ev_at[f] >= RAY_EV_MIN else "pred"

    theta_out = np.array(theta, float)
    psi_resid = np.full(nf, np.nan)
    recon = np.zeros(nf, bool)
    if use_psi:
        theta_out, psi_resid, recon = reconcile_psi(
            theta, phi_s, phase, band, ev_at, top, nf, _tier_hint)

    # ---- tiering + rows ----------------------------------------------
    rows = []
    for f in range(nf):
        if f not in anchors or np.isnan(gx[f]):
            continue
        thi = int(thstar[f]); th_dp = theta[f]; th = float(theta_out[f])   # DP vs reconciled
        g = (gx[f], gy[f]); bm = band[f]
        tier = "pred"; s = r0 = None; conf = 0.30; hx = hy = ""
        if bm is not None and abs(circ_wrap(th_dp - bm["theta"])) <= BAND_TOL:
            tier = "band"; s = bm["s"]; r0 = bm["r0"]                       # band pinned (th==th_dp)
            ux, uy = math.cos(math.radians(th)), math.sin(math.radians(th))
            bx, by = g[0] - s * r0 * ux, g[1] - s * r0 * uy
            hx, hy = bx + s * r_len * ux, by + s * r_len * uy
            conf = min(0.9, 0.75 + 0.05 * (bm["n"] - 4))
        else:
            # ray tier (honesty): real E2 ridge support at theta* that clearly
            # beats its own reverse direction (dir-safety, as in stripe_fusion).
            # Purely DP-bridged frames (low evidence at theta*) stay 'pred'.
            evs = EV[f, thi]; evrev = EV[f, (thi + NS // 2) % NS]
            band_near = any(band[j] is not None
                            for j in range(max(0, f - BAND_NEAR), min(nf, f + BAND_NEAR + 1)))
            # design C2 policy: at finish the club overlaps/holds and a lone
            # ridge is a static counterfeit risk -> a finish ray is admissible
            # only if band-corroborated. Free-space phases need motion-
            # verifiability instead (a static ridge can't be club there either).
            if phase[f] == "finish":
                verifiable = band_near
            else:
                verifiable = (not static[f]) or band_near
            if phase[f] != "addr" and evs >= RAY_EV_MIN and evs > 1.15 * evrev and verifiable:
                tier = "ray"; conf = 0.55
        # psi reconciliation retiered this blur-zone frame off its own evidence:
        # it is physically RECONstructed from the arm + monotone-psi prior, not a
        # direct club measurement (excluded from truth, like pred but informative).
        if recon[f] and abs(circ_wrap(th - th_dp)) > RECON_TOL:
            tier = "recon"; conf = 0.40; s = r0 = None; hx = hy = ""
        rows.append(dict(frame=f, t_s=round(f / fps, 6), phase=phase[f], tier=tier,
                         theta_deg=round(th % 360.0, 2),
                         s_px_mm=round(s, 5) if s else "", r0_mm=round(r0, 1) if r0 else "",
                         head_x=round(hx, 1) if hx != "" else "",
                         head_y=round(hy, 1) if hy != "" else "",
                         inside=round(float(inside[f, thi]), 2), conf=conf,
                         psi_err=round(float(psi_resid[f]), 1) if not np.isnan(psi_resid[f]) else ""))

    stem = os.path.splitext(os.path.basename(args.video))[0]
    os.makedirs(args.out_dir, exist_ok=True)
    # fusion-style CSV (adjudication / scoring). psi_err = isotonic residual =
    # per-frame measurement-error / confidence map (blank where psi is free/unfit).
    fcsv = os.path.join(args.out_dir, f"{stem}_v3.csv")
    cols = ["frame", "t_s", "phase", "tier", "theta_deg", "s_px_mm", "r0_mm",
            "head_x", "head_y", "inside", "conf", "psi_err"]
    with open(fcsv, "w", newline="") as fo:
        w = csv.DictWriter(fo, fieldnames=cols); w.writeheader(); w.writerows(rows)
    # shaft-track contract CSV (frame,grip_x,grip_y,theta_out,kind,conf). theta_out
    # is the reconciled estimate; 'recon' (arm-witness) counts as pred (not direct).
    tcsv = os.path.join(args.out_dir, f"{stem}_v3_track.csv")
    with open(tcsv, "w", newline="") as fo:
        w = csv.writer(fo); w.writerow(["frame", "grip_x", "grip_y", "theta_out", "kind", "conf"])
        for r in rows:
            kind = "pred" if r["tier"] in ("pred", "recon") else "meas"
            w.writerow([r["frame"], f"{gx[r['frame']]:.2f}", f"{gy[r['frame']]:.2f}",
                        f"{r['theta_deg']:.3f}", kind, r["conf"]])

    tc = {}
    for r in rows:
        tc[r["tier"]] = tc.get(r["tier"], 0) + 1
    lm = f"bs0={bs0} top={top} impact={impf} fin0={fin0} chir={chir:+d}"
    _sb = "off" if args.no_span_bound else f"[{span_lo},{span_hi}]"
    print(f"span_bound={_sb} heavy_frames={n_proc}/{nf} "
          f"({100.0 * n_proc / max(nf, 1):.0f}%)")
    print(f"frames={nf}  {lm}  psi_recon={'on' if use_psi else 'off'} "
          f"free=[{max(0, top - PSI_WIN_BACK)},{min(nf - 1, top + PSI_WIN_FWD)}] "
          f"recon={int(recon.sum())}")
    print(f"v3 rows={len(rows)}  tiers: " + " ".join(f"{k}={v}" for k, v in sorted(tc.items())))
    print(f"[out] {fcsv}")

    if args.phases_out:
        pcsv = os.path.join(args.out_dir, f"{stem}_v3_phases.csv")
        with open(pcsv, "w", newline="") as fo:
            w = csv.writer(fo); w.writerow(["frame", "phase", "spd", "phi_s", "psi_err"])
            for f in range(nf):
                pe = round(float(psi_resid[f]), 1) if not np.isnan(psi_resid[f]) else ""
                w.writerow([f, phase[f], round(float(spd_s[f]), 2), round(float(phi_s[f]), 1), pe])
        print(f"[phases] {pcsv}")

    if args.overlay:
        cap = cv2.VideoCapture(args.video)
        vw_fps = float(round(cap.get(cv2.CAP_PROP_FPS) or 30.0)) or 30.0
        byf = {r["frame"]: r for r in rows}
        col = {"band": (0, 0, 255), "ray": (0, 200, 255), "pred": (120, 120, 120),
               "recon": (255, 180, 0)}          # arm-witness reconstruction (cyan-ish)
        vw = cv2.VideoWriter(os.path.join(args.out_dir, f"{stem}_v3.mp4"),
                             cv2.VideoWriter_fourcc(*"mp4v"), vw_fps, (W, H))
        f = 0
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            r = byf.get(f)
            if r is not None:
                g = anchors[f]; th = math.radians(r["theta_deg"])
                ux, uy = math.cos(th), math.sin(th)
                c = col[r["tier"]]
                if r["head_x"] != "":
                    cv2.line(frame, (int(g[0]), int(g[1])), (int(r["head_x"]), int(r["head_y"])), c, 2)
                    cv2.circle(frame, (int(r["head_x"]), int(r["head_y"])), 10, (255, 0, 255), 2)
                else:
                    L = 380
                    cv2.line(frame, (int(g[0]), int(g[1])), (int(g[0] + L * ux), int(g[1] + L * uy)), c, 2)
                cv2.putText(frame, f"f{f} {r['phase']} {r['tier']} th={r['theta_deg']:.0f} in={r['inside']}",
                            (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
            vw.write(frame)
            f += 1
        cap.release(); vw.release()
        print(f"[overlay] {os.path.join(args.out_dir, stem + '_v3.mp4')}")

    if args.truth_out:
        if not args.clipmeta:
            sys.exit("--truth-out requires --clipmeta")
        meta = json.load(open(args.clipmeta)); tt = meta["t_us"]
        entries = []
        for r in rows:
            if r["tier"] in ("pred", "recon"):
                continue                             # honesty: only DIRECT measurements to truth
            g = anchors[r["frame"]]
            e = dict(t_us=int(tt[r["frame"]]), theta=round(math.radians(r["theta_deg"]), 6),
                     grip=[round(g[0], 1), round(g[1], 1)], tier=r["tier"], conf=r["conf"])
            if r["head_x"] != "":
                e["head"] = [r["head_x"], r["head_y"]]
                e["len"] = round(math.hypot(r["head_x"] - g[0], r["head_y"] - g[1]), 1)
            entries.append(e)
        tj = dict(meta=dict(club=args.club, source="instrumented", tool="club_track_v3",
                            n=len(entries)), shaft=entries)
        tpath = os.path.join(meta["swingDir"], "truth.json")
        if os.path.exists(tpath):
            old = json.load(open(tpath))
            if old.get("meta", {}).get("source") != "instrumented":
                sys.exit(f"refusing to overwrite non-instrumented {tpath}")
        json.dump(tj, open(tpath, "w"), indent=1)
        print(f"[truth] {tpath} ({len(entries)} entries)")


if __name__ == "__main__":
    main()
