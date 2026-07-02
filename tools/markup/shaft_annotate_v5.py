"""
shaft_annotate.py — PinPoint shaft detection exemplar (v4, frozen 2026-07-02).

Reference implementation and video markup tool for single-camera shaft tracking:
anchored polar unwrap, edge-pair + motion evidence, line/wedge regimes, KF + RTS,
and a measured/predicted output model. This is the oracle for any C++ port.

Design doc:   docs/design/shaft_detection_improvements.md   (original architecture)
Learnings:    docs/design/shaft_detection_exemplar_findings.md   (fixes F1-F9 + why)
How to run:   docs/implementation/shaft_markup_exemplar_impl.md

Companion tools (same folder): prep_swing.py (swing dir -> clip + anchors),
montage.py (visual review tiles), score_truth.py (numeric eval vs truth.json).

Usage:
  prep_swing.py <swingDir> <outDir>
  shaft_annotate.py <outDir>/faceon_swing.mp4 --anchors <outDir>/anchors.csv \
      --fps-override <clipmeta.fps> [--out-dir out] [--debug-frames 186,310]

Output CSV: per frame theta_filt/omega_filt/theta_smooth/omega_smooth/conf/flags
plus `kind` (meas|pred) and `theta_out` — consumers use theta_out, treating
kind=meas as labels and kind=pred as clearly-marked kinematic prediction
(low-confidence detections are DISCARDED, never emitted as measurements).
"""
import argparse, csv, math, os, sys
import numpy as np
import cv2

# ----------------------------------------------------------------------------- config
DTHETA = 0.25            # deg per angular bin
R_MIN_FRAC = 0.06        # min radius as fraction of image height (skip hands)
F_MIN = 0.22             # min support fraction to accept a detection
NEAR_FRAC = 0.35         # conf shaping only in v3 (see RSTART_MAX gate)
NEAR_MIN = 0.12          # conf shaping only
RSTART_MAX = 0.55        # F1v3: supported run must BEGIN within this fraction of the ray
EXT_MIN = 0.10           # F8: min run extent (fraction of ray) — a real club is >=~46px
DEN_MIN = 0.45           # F8: min support density INSIDE the run
CONF_MEAS = 0.5          # output: conf >= this => measured; below => discarded/predicted
SEG_MIN = 4              # min consecutive measured frames to form a trusted segment
TAU_DECAY = 0.12         # s — omega decay for head/tail extrapolation (post-impact)
RUN_LEN = 8              # F1v3: samples of s>0.3 that constitute a run
REINIT_SCORE = 1.3       # F4v3: re-inits (not first) need S_pk > REINIT_SCORE*S_ACCEPT
BLOB_GAIN = 0.5          # F6: distal clubhead-blob credit at init (180-flip test)
# F10: static re-acquisition ("hold mode") — at a still hold (address/finish) the
# club is static and sharp; average the last HOLD_K frames to lift SNR and
# re-acquire with a full-circle scan. Acceptance additionally requires a distal
# clubhead blob (the static scene has no motion evidence to corroborate).
HOLD_K = 9               # frames stacked / anchor-median window
STILL_THR = 1.5          # mean |I-prev| (grey levels) in the grip ROI => still
HOLD_S_HIT = 0.25        # relaxed per-sample hit threshold on the stacked image
HOLD_BLOB_MIN = 0.5      # raw-luma blob at the run end (hold mode)
HOLD_CHG_MIN = 0.15      # AND change-vs-bg0 at the run end (it must have ARRIVED)
S_ACCEPT = 0.12          # min column score to accept
TAU_P = 90.0             # F2: edge-pair evidence scale (Sobel units)
TAU_M = 18.0             # motion evidence scale (grey levels)
SHAFT_W_PX = 5.0         # F2: shaft width prior (px)
S_CAP = 0.8
BAND_THR = 0.4           # wedge band segmentation threshold (frac of peak)
BETA_SWITCH = 2.0        # deg — line/wedge regime switch
COAST_MAX = 12           # frames of consecutive misses before re-init
GATE_SIGMA = 3.0
FAN_MIN = 10.0           # deg minimum half-fan
R_THETA = 1.5 ** 2       # deg^2 line-measurement variance (base)
SIGMA_JERK = 2.5e5       # deg/s^3 process noise
SECTOR_HALF = 120.0      # F3: plausibility half-sector around forearm extension (deg)
SECTOR_ATTEN = 0.05      # F3: S multiplier outside the sector
GLOBAL_EVERY = 7         # F4: frames between global escape scans
GLOBAL_MARGIN = 1.4      # F4: global peak must beat local by this factor
GLOBAL_DIFF = 30.0       # F4: and differ by more than this (deg)
OMEGA_MAX = 3000.0       # F5: max plausible |omega| (deg/s)

FLAG_LINE, FLAG_WEDGE, FLAG_COAST, FLAG_REINIT, FLAG_LOWSUP, FLAG_LOST = \
    "LINE_OK", "WEDGE_OK", "COASTED", "REINIT", "LOW_SUPPORT", "LOST"

# ----------------------------------------------------------------------------- helpers
def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0

class KF:
    """Constant-jerk KF on [theta, omega, alpha] (deg, deg/s, deg/s^2), theta unwrapped."""
    def __init__(self, dt):
        self.dt = dt
        self.x = np.zeros(3)
        self.P = np.diag([1e4, 1e6, 1e8])
        self.hist = []
        d = dt
        self.F = np.array([[1, d, 0.5 * d * d], [0, 1, d], [0, 0, 1]])
        q = SIGMA_JERK ** 2
        self.Q = q * np.array([[d**5/20, d**4/8, d**3/6],
                               [d**4/8,  d**3/3, d**2/2],
                               [d**3/6,  d**2/2, d]])

    def init(self, theta):
        self.x = np.array([theta, 0.0, 0.0])
        self.P = np.diag([4.0, 2e5, 2e7])

    def predict(self):
        xp = self.F @ self.x
        Pp = self.F @ self.P @ self.F.T + self.Q
        return xp, Pp

    def step(self, z, R, H):
        xp, Pp = self.predict()
        if z is None:
            self.x, self.P = xp, Pp
            self.hist.append((self.x.copy(), self.P.copy(), xp, Pp))
            return False
        z = np.atleast_1d(np.asarray(z, float))
        H = np.atleast_2d(H)
        y = z - H @ xp
        y[0] = wrap180(y[0])
        S = H @ Pp @ H.T + R
        md2 = float(y @ np.linalg.solve(S, y))
        if md2 > (GATE_SIGMA ** 2) * len(y):
            self.x, self.P = xp, Pp
            self.hist.append((self.x.copy(), self.P.copy(), xp, Pp))
            return False
        K = Pp @ H.T @ np.linalg.inv(S)
        self.x = xp + K @ y
        self.P = (np.eye(3) - K @ H) @ Pp
        self.hist.append((self.x.copy(), self.P.copy(), xp, Pp))
        return True

    def rts(self):
        n = len(self.hist)
        xs = [None] * n
        Ps = [None] * n
        xs[-1], Ps[-1] = self.hist[-1][0], self.hist[-1][1]
        for k in range(n - 2, -1, -1):
            x, P, _, _ = self.hist[k]
            _, _, xp1, Pp1 = self.hist[k + 1]
            C = P @ self.F.T @ np.linalg.inv(Pp1)
            dx = xs[k + 1] - xp1
            dx[0] = wrap180(dx[0])
            xs[k] = x + C @ dx
            Ps[k] = P + C @ (Ps[k + 1] - Pp1) @ C.T
        return xs, Ps

class AnchorSource:
    """anchors.csv rows: frame,x,y[,phi_deg,phi_ok] — grip + lead-forearm extension."""
    def __init__(self, path):
        self.grip = {}
        self.phi = {}
        with open(path) as f:
            for row in csv.reader(f):
                if not row or not row[0].strip().lstrip('-').isdigit():
                    continue
                i = int(row[0])
                self.grip[i] = (float(row[1]), float(row[2]))
                if len(row) >= 5 and int(float(row[4])) == 1:
                    self.phi[i] = float(row[3])
        self.pt = None
        self.last_phi = None

    def get(self, idx):
        if idx in self.grip:
            self.pt = self.grip[idx]
        if idx in self.phi:
            self.last_phi = self.phi[idx]
        return self.pt, self.last_phi

# ----------------------------------------------------------------------------- evidence
def evidence_scan(gray, bg, prev_gray, grip, thetas_deg, r_min, r_max, s_hit=0.3, blob_img=None):
    """Return S(theta), F(theta), F_near(theta) over the given angle set.
    Evidence = max(edge-pair (F2), motion). Anti-ghost motion = min(|I-B|, |I-prev|)."""
    H, W = gray.shape
    gxs = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    gys = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    fd = np.abs(gray - prev_gray) if prev_gray is not None else np.abs(gray - bg)
    motion = np.minimum(np.abs(gray - bg), fd)

    xg, yg = grip
    T = np.deg2rad(thetas_deg)[:, None].astype(np.float32)
    R = np.arange(r_min, r_max, 1.0, dtype=np.float32)[None, :]
    X = xg + R * np.cos(T)
    Y = yg + R * np.sin(T)
    valid = (X >= 1) & (X < W - 1) & (Y >= 1) & (Y < H - 1)

    # F2: paired-edge sampling at theta -/+ delta(r), delta = (w/2)/r
    D = (SHAFT_W_PX * 0.5) / np.maximum(R, 1.0)
    Xm = xg + R * np.cos(T - D); Ym = yg + R * np.sin(T - D)
    Xp = xg + R * np.cos(T + D); Yp = yg + R * np.sin(T + D)

    def samp(a, Xc, Yc):
        return cv2.remap(a.astype(np.float32), np.clip(Xc, 0, W - 1),
                         np.clip(Yc, 0, H - 1), cv2.INTER_LINEAR)

    nx, ny = -np.sin(T), np.cos(T)
    dot_m = samp(gxs, Xm, Ym) * nx + samp(gys, Xm, Ym) * ny
    dot_p = samp(gxs, Xp, Yp) * nx + samp(gys, Xp, Yp) * ny
    # antiparallel edge pair -> product negative; single/soft edge -> ~0
    E_pair = np.clip(np.sqrt(np.maximum(0.0, -dot_m * dot_p)) / TAU_P, 0, 1)
    Mo = samp(motion, X, Y)
    E_mot = np.clip((Mo - 6.0) / TAU_M, 0, 1)
    s = np.where(valid, np.maximum(E_pair, E_mot), 0.0)

    w = R * valid
    S = (w * np.minimum(s, S_CAP)).sum(1) / (w.sum(1) + 1e-6)
    F = ((s > s_hit) & valid).sum(1) / (valid.sum(1) + 1e-6)
    ncols = max(1, int(s.shape[1] * NEAR_FRAC))
    sn, vn = s[:, :ncols], valid[:, :ncols]
    F_near = ((sn > s_hit) & vn).sum(1) / (vn.sum(1) + 1e-6)

    # F1v3: fractional radius where the first sustained run of s>0.3 begins (1.0 = none).
    hit = ((s > s_hit) & valid).astype(np.float32)
    runk = np.ones(RUN_LEN, np.float32) / RUN_LEN
    runav = cv2.filter2D(hit, -1, runk[None, :], borderType=cv2.BORDER_CONSTANT)
    isrun = runav >= 0.75
    nr = s.shape[1]
    first = np.where(isrun.any(1), isrun.argmax(1), nr)
    r_start = first.astype(np.float32) / float(nr)
    # run end (last run sample) for the blob window
    last = np.where(isrun.any(1), nr - 1 - isrun[:, ::-1].argmax(1), 0)
    # F8: run extent + in-run density (replaces the full-ray F gate: a short
    # foreshortened/cropped club is dense within its run; full-ray F dilutes it)
    span = np.maximum(1, last - first + 1).astype(np.float32)
    extent = np.where(isrun.any(1), (span + RUN_LEN) / float(nr), 0.0)
    csum = np.cumsum(hit, axis=1)
    inrun = csum[np.arange(hit.shape[0]), last] - np.where(first > 0,
             csum[np.arange(hit.shape[0]), np.maximum(first - 1, 0)], 0)
    density = np.where(isrun.any(1), inrun / span, 0.0)

    # F6: distal clubhead-blob brightness NEAR the run end (chrome head is bright);
    # credit only within [end, end+25% of ray] so a far bright distractor (neon strip
    # crossing an unsupported ray) earns nothing.
    # F13: two blob channels — raw luma (something bright at the run end) and
    # CHANGE vs the pre-swing scene (it arrived; mat/neon brightness is permanent).
    L = samp(gray, X, Y)
    Lc = samp(blob_img, X, Y) if blob_img is not None else np.zeros_like(L)
    cols = np.arange(nr)[None, :]
    lo = last[:, None].astype(np.int64)
    blobwin = (cols >= lo) & (cols <= lo + int(0.25 * nr)) & valid & isrun.any(1)[:, None]
    B = np.where(blobwin, L, 0.0).max(1) / 255.0
    Bc = np.where(blobwin, Lc, 0.0).max(1) / 255.0

    k = cv2.getGaussianKernel(9, 2).ravel()
    S = np.convolve(S, k, mode="same")
    return S, F, F_near, r_start, B, extent, density, Bc

def sector_mask(thetas, S, phi):
    """F3: suppress S outside +/-SECTOR_HALF of the forearm extension phi."""
    if phi is None:
        return S
    d = np.abs(wrap180(np.asarray(thetas) - phi))
    out = S.copy()
    out[d > SECTOR_HALF] *= SECTOR_ATTEN
    return out

def fit_peak(thetas, S):
    i = int(np.argmax(S))
    off = 0.0
    if 0 < i < len(S) - 1:
        a, b, c = S[i - 1], S[i], S[i + 1]
        d = a - 2 * b + c
        if abs(d) > 1e-9:
            off = 0.5 * (a - c) / d
    return thetas[i] + off * DTHETA, i

def fit_wedge(thetas, S, i_pk):
    thr = BAND_THR * S[i_pk]
    lo = i_pk
    while lo > 0 and S[lo - 1] > thr:
        lo -= 1
    hi = i_pk
    while hi < len(S) - 1 and S[hi + 1] > thr:
        hi += 1
    return thetas[lo], thetas[hi]

# ----------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--anchors", required=True)
    ap.add_argument("--exposure", type=float, default=None)
    ap.add_argument("--fps-override", type=float, default=None)
    ap.add_argument("--r-max", type=float, default=None)
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--debug-frames", default="", help="comma list: dump gate values")
    args = ap.parse_args()

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    fps = args.fps_override or cap.get(cv2.CAP_PROP_FPS) or 30.0
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    dt = 1.0 / fps
    t_exp = args.exposure if args.exposure else 0.5 * dt
    r_min = max(20, int(R_MIN_FRAC * H))
    r_max = args.r_max or 0.45 * H

    stem = os.path.splitext(os.path.basename(args.video))[0]
    os.makedirs(args.out_dir, exist_ok=True)
    vw = cv2.VideoWriter(os.path.join(args.out_dir, f"{stem}_annotated.mp4"),
                         cv2.VideoWriter_fourcc(*"mp4v"), fps, (W, H))

    dbg = set(x.strip() for x in args.debug_frames.split(",") if x.strip())
    anchor = AnchorSource(args.anchors)

    # F12: permanent-scene snapshot. Frame 0 alone contains the ADDRESS CLUB —
    # and the club returns to near its address angle at impact, so a frame-0
    # snapshot vetoes legitimate impact-zone re-acquisitions (this killed the
    # entire downswing). The pixel-wise MEDIAN of frames spread over the clip
    # erases the moving club and keeps only what is truly permanent.
    ntot = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    med_stack = []
    if ntot > 10:
        for k in range(11):
            cap.set(cv2.CAP_PROP_POS_FRAMES, int(k * (ntot - 1) / 10))
            okf, fmed = cap.read()
            if okf:
                med_stack.append(cv2.cvtColor(fmed, cv2.COLOR_BGR2GRAY).astype(np.float32))
        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
    bg0_med = np.median(np.stack(med_stack), axis=0) if len(med_stack) >= 5 else None

    from collections import deque
    grays_hist = deque(maxlen=HOLD_K)     # F10: recent grays for stacking
    grips_hist = deque(maxlen=HOLD_K)     # F10: recent anchors (median-stabilised)
    still_hist = deque(maxlen=HOLD_K)     # F10: windowed stillness votes
    unmeas_run = 99                       # frames since last accepted measurement
    hold_active = False                   # F10: static-hold tracking engaged
    bg0 = None                            # F12: frozen first-frame scene snapshot
    swing_seen = False                    # F12: high-omega observed => re-acquisition mode
    kf = KF(dt)
    initialised = False
    misses = 0
    global_hits = 0
    confirm = 99            # accepted measurements since last (re)init
    w0 = 3.0
    bg = None
    prev_gray = None
    rows = []
    frames_raw = []
    per_frame = []

    idx = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
        if bg is None:
            bg = gray.copy()
            bg0 = bg0_med if bg0_med is not None else gray.copy()

        # F12: scene-permanence veto for RE-acquisitions. A candidate whose
        # edge-pair evidence already exists in the pre-swing scene (bg0) is
        # permanent structure (neon strips, door frames), not the club — the
        # club's post-swing resting position was empty at frame 0. Not applied
        # before the swing (the address club IS in bg0).
        def scene_perm(g, th_c, s_ref, rs_ref, ext_ref):
            if not swing_seen:
                return False
            thp = np.arange(th_c - 2.0, th_c + 2.0 + DTHETA, DTHETA)
            Sp, _, _, Rp, _, Ep, _, _ = evidence_scan(bg0, bg0, None, g, thp, r_min, r_max)
            ip = int(np.argmax(Sp))
            if float(Sp[ip]) <= max(0.6 * s_ref, 0.8 * S_ACCEPT):
                return False
            # permanent only if the bg0 run RADIALLY overlaps the candidate's run:
            # far-radius permanent structure (lens-flare arc, lamps) must not veto
            # a club whose evidence lives at near radii; the neon-parallel lock's
            # run IS the neon's own pixels, so it overlaps and is vetoed.
            sc, ec = rs_ref, rs_ref + ext_ref
            sp, ep = float(Rp[ip]), float(Rp[ip]) + float(Ep[ip])
            ov = max(0.0, min(ec, ep) - max(sc, sp))
            return ov > 0.5 * max(ext_ref, 0.05)
        grip, phi = anchor.get(idx)
        if grip is None:
            sys.exit("anchors.csv gave no grip for frame 0")

        flag = FLAG_LOST
        S_pk = sup = near = band = np.nan
        th_open = th_close = np.nan
        meas_used = False

        grays_hist.append(gray)
        grips_hist.append(grip)
        # F11: per-frame stillness around the grip (drives hold mode AND blocks
        # unreliable single-frame acquisition while the scene is static)
        still_now = False
        roi_mean = 99.0
        if len(grays_hist) >= 2:
            roi0 = np.abs(gray - grays_hist[-2])
            gy0, gx0 = int(max(0, grip[1] - 200)), int(max(0, grip[0] - 200))
            roi_mean = float(roi0[gy0:gy0 + 400, gx0:gx0 + 400].mean())
            still_now = roi_mean < STILL_THR
        still_hist.append(still_now)
        still_ok = sum(still_hist) >= 6            # majority of the window is still
        # F14: the hardened acquisition gates (scene-permanence veto, clubhead-blob
        # requirement) exist to kill QUASI-STATIC distractor locks (neon/collar/
        # body-mat rays at the finish hold). During fast motion the permissive
        # acquisition of v4 was correct — motion evidence corroborates — and the
        # hardened gates strangle mid-downswing re-acquisition. Gate them on the
        # measured scene motion.
        quasi_static = roi_mean < 3.0

        # F10: still-hold measurement. When the scene around the grip is still
        # and the track has produced nothing for a while (or hold tracking is
        # already engaged), stack the recent frames (noise ~ /sqrt(K)) and scan
        # with a median-stable anchor. An accepted hold detection is fed to the
        # KF as a NORMAL measurement (it must earn confirmation like any other),
        # so the finish hold builds real measured segments. Sector OFF; the
        # distal clubhead blob is required (no motion evidence to corroborate).
        hold_th = None
        hold_vals = None
        if len(grays_hist) == HOLD_K and (hold_active or unmeas_run >= HOLD_K):
            still = still_ok
            if str(idx) in dbg:
                print(f"[dbg f{idx}] HOLD gate: unmeas={unmeas_run} active={hold_active} still={still}")
            if still:
                stack = np.mean(np.stack(grays_hist), axis=0)
                g_med = (float(np.median([g[0] for g in grips_hist])),
                         float(np.median([g[1] for g in grips_hist])))
                if hold_active and initialised:
                    loc = kf.x[0] % 360.0
                    gth = np.arange(loc - 15.0, loc + 15.0 + DTHETA, DTHETA)
                else:
                    gth = np.arange(0.0, 360.0, DTHETA)
                Sh, Fh, Nh, Rh, Bh, Eh, Dh, Ch = evidence_scan(
                    stack, bg, None, g_med, gth, r_min, r_max, s_hit=HOLD_S_HIT,
                    blob_img=np.abs(stack - bg0))
                th_h, ih = fit_peak(gth, Sh)
                if hold_active and initialised:
                    cands = (ih,)
                else:
                    # top-4 NMS peaks + opposites: a dominant (blob-rejected) body
                    # line must not mask the true club elsewhere in the circle
                    order = np.argsort(Sh)[::-1]
                    picks = []
                    for jj in order:
                        if len(picks) >= 4 or Sh[jj] < S_ACCEPT:
                            break
                        if all(abs(wrap180(gth[jj] - gth[kk])) > 20.0 for kk in picks):
                            picks.append(int(jj))
                    cands = tuple(picks) + tuple(
                        (int(jj) + int(round(180.0 / DTHETA))) % len(gth) for jj in picks)
                best = None
                for ii in cands:
                    okc = (Sh[ii] > S_ACCEPT and float(Rh[ii]) < RSTART_MAX
                           and float(Bh[ii]) >= HOLD_BLOB_MIN
                           and (Fh[ii] > F_MIN or (float(Eh[ii]) >= EXT_MIN
                                                   and float(Dh[ii]) >= DEN_MIN))
                           and not scene_perm(g_med, float(gth[ii]), float(Sh[ii]), float(Rh[ii]), float(Eh[ii])))
                    if str(idx) in dbg:
                        print(f"[dbg f{idx}] HOLD cand th={gth[ii]:.1f} S={Sh[ii]:.3f} "
                              f"F={Fh[ii]:.2f} Rs={Rh[ii]:.2f} B={Bh[ii]:.2f} "
                              f"Ext={Eh[ii]:.2f} Den={Dh[ii]:.2f} ok={okc} "
                              f"perm={scene_perm(g_med, float(gth[ii]), float(Sh[ii]), float(Eh[ii]))} "
                              f"gmed=({g_med[0]:.0f},{g_med[1]:.0f})")
                    sc = float(Sh[ii]) * (1.0 + BLOB_GAIN * float(Bh[ii]))
                    if okc and (best is None or sc > best[0]):
                        best = (sc, ii)
                if best is not None:
                    ii = best[1]
                    hold_th = float(gth[ii])
                    hold_vals = (float(Sh[ii]), float(Fh[ii]), float(Nh[ii]))
                    grip = g_med           # stabilised anchor for this frame's record
            hold_active = hold_th is not None

        # F4: periodic global escape scan — can a distant peak decisively beat the lock?
        if hold_th is None and not still_ok and initialised and idx % GLOBAL_EVERY == 0 and idx > 0:
            gth = np.arange(0.0, 360.0, 1.0)
            Sg, Fg, Ng, Rg, Bg, Eg, Dg, Cg = evidence_scan(gray, bg, prev_gray, grip, gth, r_min, r_max,
                                                            blob_img=np.abs(gray - bg0))
            Sg = sector_mask(gth, Sg, phi)
            th_g, ig = fit_peak(gth, Sg)
            loc = kf.x[0] % 360.0
            if (Sg[ig] > S_ACCEPT * 1.2 and Rg[ig] < RSTART_MAX
                    and (Fg[ig] > F_MIN or (Eg[ig] >= EXT_MIN and Dg[ig] >= DEN_MIN))
                    and abs(wrap180(th_g - loc)) > GLOBAL_DIFF
                    and not (quasi_static
                             and scene_perm(grip, float(th_g), float(Sg[ig]), float(Rg[ig]), float(Eg[ig])))):
                # compare against evidence at the tracked angle
                i_loc = int(np.argmin(np.abs(wrap180(gth - loc))))
                if Sg[ig] > GLOBAL_MARGIN * max(Sg[i_loc], 1e-6):
                    global_hits += 1
                else:
                    global_hits = 0
            else:
                global_hits = 0
            if global_hits >= 2:
                initialised = False          # falls into the re-init path below
                global_hits = 0

        if initialised and hold_th is not None \
                and abs(wrap180(hold_th - kf.x[0])) > 45.0:
            # the still-hold detection contradicts the (junk) track — re-acquire
            kf.init(hold_th)
            kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
            flag, meas_used = FLAG_REINIT, True
            confirm = 0
            unmeas_run = 0
            S_pk, sup, near = hold_vals
            cv2.accumulateWeighted(gray, bg, 0.02)
            prev_gray = gray
            frames_raw.append(frame)
            per_frame.append(dict(grip=grip, flag=flag, S=S_pk, sup=sup, near=near,
                                  band=np.nan, th_open=np.nan, th_close=np.nan,
                                  confirmed=False, still=still_now))
            rows.append([idx, idx * dt, kf.x[0], kf.x[1], S_pk, sup, near, np.nan, flag,
                         grip[0], grip[1]])
            idx += 1
            continue

        if initialised:
            xp, Pp = kf.predict()
            sigma = math.sqrt(max(Pp[0, 0], 1e-6))
            beta_pred = abs(xp[1]) * t_exp
            half_fan = max(GATE_SIGMA * sigma, FAN_MIN) + 0.5 * beta_pred + 2.0
            th_c = xp[0] % 360.0
            thetas = np.arange(th_c - half_fan, th_c + half_fan + DTHETA, DTHETA)
        else:
            thetas = np.arange(0.0, 360.0, DTHETA)

        S, F, N, Rs, B, Ext, Den, Bchg = evidence_scan(gray, bg, prev_gray, grip, thetas, r_min, r_max,
                                                       blob_img=np.abs(gray - bg0))
        if str(idx) in dbg:
            order = np.argsort(S)[::-1][:6]
            print(f"[dbg f{idx}] init={initialised} grip=({grip[0]:.0f},{grip[1]:.0f}) phi={phi} "
                  f"fan=[{thetas[0]:.0f}..{thetas[-1]:.0f}]")
            for ii in order:
                print(f"    th={thetas[ii]:7.1f} S={S[ii]:.3f} F={F[ii]:.2f} "
                      f"Ext={Ext[ii]:.2f} Den={Den[ii]:.2f} Rs={Rs[ii]:.2f} B={B[ii]:.2f} "
                      f"good={(S[ii]>S_ACCEPT) and (Rs[ii]<RSTART_MAX) and (F[ii]>F_MIN or (Ext[ii]>=EXT_MIN and Den[ii]>=DEN_MIN))}")
        if initialised:
            S = sector_mask(thetas, S, phi)   # F3 in the tracking fan ONLY (v3:
        # the finish wrap breaks the forearm-continuation assumption, so full-circle
        # (re)init scans run unmasked and rely on the run-start + blob tests instead)
        th_meas, i_pk = fit_peak(thetas, S)
        S_pk, sup, near = float(S[i_pk]), float(F[i_pk]), float(N[i_pk])
        if hold_th is not None:            # F10: stacked measurement wins the frame
            th_meas = hold_th
            S_pk, sup, near = hold_vals

        okrun = (float(Ext[i_pk]) >= EXT_MIN) and (float(Den[i_pk]) >= DEN_MIN)
        good = (S_pk > S_ACCEPT) and (float(Rs[i_pk]) < RSTART_MAX) \
               and (sup > F_MIN or okrun)     # F8: global-F OR dense-run path
        if hold_th is not None:
            good = True                       # already passed the (stricter) hold gates
        if not initialised and hold_th is not None:
            kf.init(hold_th)
            kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
            initialised, flag, meas_used = True, FLAG_REINIT, True
            confirm = 0
        elif not initialised and still_ok:
            # F11: still but the stacked hold path did not accept — a single-frame
            # init here is exactly how static distractors (collar+neon rays) lock
            # in; stay lost until hold evidence or motion resumes.
            kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
        elif not initialised:
            # F6: 180-flip disambiguation — compare the peak with its opposite ray,
            # crediting a bright distal blob at the supported run's end (design §7).
            j = (i_pk + int(round(180.0 / DTHETA))) % len(thetas)
            cand = []
            for ii in (i_pk, j):
                okc = (S[ii] > S_ACCEPT) and (float(Rs[ii]) < RSTART_MAX) \
                      and (F[ii] > F_MIN or (float(Ext[ii]) >= EXT_MIN
                                             and float(Den[ii]) >= DEN_MIN)) \
                      and (not (swing_seen and quasi_static)
                           or (float(B[ii]) >= HOLD_BLOB_MIN
                               and float(Bchg[ii]) >= HOLD_CHG_MIN)) \
                      and not (quasi_static
                               and scene_perm(grip, float(thetas[ii]), float(S[ii]), float(Rs[ii]), float(Ext[ii])))
                cand.append((float(S[ii]) * (1.0 + BLOB_GAIN * float(B[ii])), okc, ii))
            cand.sort(reverse=True)
            score, okc, ii = cand[0]
            th_meas = thetas[ii] if ii != i_pk else th_meas
            S_pk, sup, near = float(S[ii]), float(F[ii]), float(N[ii])
            need = S_ACCEPT * (REINIT_SCORE if idx > 0 else 1.0)
            good = okc and (float(S[ii]) > need)
            if good:
                kf.init(th_meas)
                kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
                initialised, flag, meas_used = True, FLAG_REINIT, True
                confirm = 0
            else:
                kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
        else:
            xp, _ = kf.predict()
            beta_pred = abs(xp[1]) * t_exp
            wedge = beta_pred > BETA_SWITCH and hold_th is None
            if good and wedge:
                a, b = fit_wedge(thetas, S, i_pk)
                th_open, th_close, band = a, b, b - a
                th_mid = 0.5 * (a + b)
                z_th = xp[0] + wrap180(th_mid - xp[0])
                band_blur = max(0.0, band - w0)
                sign_ok = abs(xp[1]) > 200.0
                om = math.copysign(band_blur / t_exp, xp[1]) if sign_ok else 0.0
                if (args.exposure and band_blur > 1.5 and sign_ok
                        and abs(om) < OMEGA_MAX):                       # F5
                    Rm = np.diag([R_THETA * 2.0, max((0.15 * om) ** 2, 1e4)])
                    acc = kf.step([z_th, om], Rm, [[1, 0, 0], [0, 1, 0]])
                else:
                    acc = kf.step([z_th], [[R_THETA * 2.0]], [[1, 0, 0]])
                flag = FLAG_WEDGE if acc else FLAG_COAST
                meas_used = acc
            elif good:
                a, b = fit_wedge(thetas, S, i_pk)
                if abs(xp[1]) * t_exp < 0.5:
                    w0 = 0.9 * w0 + 0.1 * (b - a)
                z_th = xp[0] + wrap180(th_meas - xp[0])
                acc = kf.step([z_th], [[R_THETA]], [[1, 0, 0]])
                flag = FLAG_LINE if acc else FLAG_COAST
                meas_used = acc
            else:
                kf.step(None, None, None)
                flag = FLAG_LOWSUP if (float(Ext[i_pk]) < EXT_MIN or float(Den[i_pk]) < DEN_MIN
                                        or float(Rs[i_pk]) >= RSTART_MAX) else FLAG_COAST
            if meas_used:
                confirm += 1
            if abs(kf.x[1]) > 800.0:
                swing_seen = True          # F12: downswing omega observed
            # a measurement only counts as "found" if the updated state is sane —
            # runaway-omega wedge accepts (conf 0 anyway) must not mask lostness,
            # or the still-hold re-acquisition never engages (F10)
            sane = meas_used and abs(kf.x[1]) <= OMEGA_MAX and confirm >= 3
            unmeas_run = 0 if sane else unmeas_run + 1
            misses = 0 if meas_used else misses + 1
            if misses > COAST_MAX:
                initialised = False
                misses = 0
                flag = FLAG_LOST

        cv2.accumulateWeighted(gray, bg, 0.02)
        prev_gray = gray
        frames_raw.append(frame)
        per_frame.append(dict(grip=grip, flag=flag, S=S_pk, sup=sup, near=near,
                              band=band, th_open=th_open, th_close=th_close,
                              confirmed=confirm >= 3, still=still_now))
        rows.append([idx, idx * dt, kf.x[0], kf.x[1], S_pk, sup, near, band, flag,
                     grip[0], grip[1]])
        idx += 1
    cap.release()

    # ---- RTS smoothing + posterior-variance confidence ----
    xs, Ps = kf.rts() if kf.hist else ([], [])
    for i, r in enumerate(rows):
        if i < len(xs):
            sig = math.sqrt(max(Ps[i][0, 0], 1e-9))
            conf = max(0.0, min(1.0, 1.0 - sig / 10.0))
            if per_frame[i]["flag"] not in (FLAG_LINE, FLAG_WEDGE, FLAG_REINIT):
                conf *= 0.4
            if not per_frame[i].get("confirmed", True):
                conf = min(conf, 0.35)   # re-init not yet confirmed by 3 measurements
            om_s = xs[i][1]
            if abs(om_s) > OMEGA_MAX:
                conf *= max(0.0, 1.0 - (abs(om_s) - OMEGA_MAX) / 1500.0)
            r.insert(4, conf)
            r.insert(4, xs[i][1])   # omega_smooth
            r.insert(4, xs[i][0])   # theta_smooth
        else:
            r.insert(4, 0.0); r.insert(4, np.nan); r.insert(4, np.nan)

    # ---- F9: measured / predicted / discarded output model ----------------------
    # Frames with conf >= CONF_MEAS in runs of >= SEG_MIN form trusted MEASURED
    # segments. Everything else is DISCARDED as a measurement and replaced by a
    # PREDICTION: cubic-hermite bridges between segment boundaries (using the
    # smoother's theta/omega there), and a decaying-omega extrapolation for the
    # head/tail (post-impact is kinematically smooth, so predict rather than
    # report noise — per markup policy, low-confidence detections are discarded).
    nfr = len(rows)
    conf_arr = np.array([r[6] for r in rows], float)
    th_arr = np.array([r[4] for r in rows], float)
    om_arr = np.array([r[5] for r in rows], float)
    t_arr = np.array([r[1] for r in rows], float)
    # F11: still-period consistency. Within each contiguous still run (>=15 fr),
    # the club is static — measured thetas must agree. Demote outliers (>25 deg
    # from the conf-weighted circular mean) to the predicted tier.
    still_arr = [pf.get("still", False) for pf in per_frame]
    i = 0
    while i < nfr:
        if still_arr[i]:
            j = i
            while j + 1 < nfr and still_arr[j + 1]:
                j += 1
            if j - i + 1 >= 15:
                sel = [k for k in range(i, j + 1) if conf_arr[k] >= CONF_MEAS]
                if len(sel) >= 3:
                    cs = sum(conf_arr[k] * math.cos(math.radians(th_arr[k])) for k in sel)
                    sn = sum(conf_arr[k] * math.sin(math.radians(th_arr[k])) for k in sel)
                    mu = math.degrees(math.atan2(sn, cs))
                    for k in sel:
                        if abs(wrap180(th_arr[k] - mu)) > 25.0:
                            conf_arr[k] = min(conf_arr[k], 0.35)
                            rows[k][6] = conf_arr[k]
            i = j + 1
        else:
            i += 1

    meas = conf_arr >= CONF_MEAS
    segs = []
    i = 0
    while i < nfr:
        if meas[i]:
            j = i
            while j + 1 < nfr and meas[j + 1]:
                j += 1
            if j - i + 1 >= SEG_MIN:
                segs.append((i, j))
            i = j + 1
        else:
            i += 1
    kind = ["pred"] * nfr
    th_out = np.full(nfr, np.nan)
    for a, b in segs:
        for i in range(a, b + 1):
            kind[i] = "meas"
            th_out[i] = th_arr[i]

    def decay_sweep(om0, dt_s):
        return om0 * TAU_DECAY * (1.0 - math.exp(-abs(dt_s) / TAU_DECAY)) * (1 if dt_s >= 0 else -1)

    def clean_stretch(lo, hi):
        # RTS history is trustworthy iff no re-init/lost inside and omega stays sane
        for i in range(lo, hi + 1):
            if rows[i][11] in (FLAG_REINIT, FLAG_LOST):
                return False
            if abs(om_arr[i]) > OMEGA_MAX * 1.2:
                return False
        return True

    if segs:
        for (a0, b0), (a1, b1) in zip(segs, segs[1:]):
            if clean_stretch(b0 + 1, a1 - 1):
                for i in range(b0 + 1, a1):
                    th_out[i] = th_arr[i]        # smoother IS the prediction here
                continue
            tA, thA, omA = t_arr[b0], th_arr[b0], om_arr[b0]
            tB, thB, omB = t_arr[a1], th_arr[a1], om_arr[a1]
            gap = tB - tA
            expect = thA + decay_sweep(omA, gap)
            k360 = round((expect - thB) / 360.0)
            thB_al = thB + 360.0 * k360
            for i in range(b0 + 1, a1):
                u = (t_arr[i] - tA) / gap
                h00 = 2*u**3 - 3*u**2 + 1; h10 = u**3 - 2*u**2 + u
                h01 = -2*u**3 + 3*u**2;   h11 = u**3 - u**2
                th_out[i] = (h00*thA + h10*gap*omA + h01*thB_al + h11*gap*omB)
        a0, _ = segs[0]
        if clean_stretch(0, max(0, a0 - 1)):
            for i in range(0, a0):
                th_out[i] = th_arr[i]
        else:
            for i in range(0, a0):
                th_out[i] = th_arr[a0] + decay_sweep(om_arr[a0], t_arr[i] - t_arr[a0])
        _, b_last = segs[-1]
        if clean_stretch(min(nfr - 1, b_last + 1), nfr - 1):
            for i in range(b_last + 1, nfr):
                th_out[i] = th_arr[i]
        else:
            for i in range(b_last + 1, nfr):
                th_out[i] = th_arr[b_last] + decay_sweep(om_arr[b_last], t_arr[i] - t_arr[b_last])
    else:
        kind = ["none"] * nfr

    # ---- overlay pass (smoothed track) ----
    review = []
    for i, frame in enumerate(frames_raw):
        d = per_frame[i]
        gx, gy = int(d["grip"][0]), int(d["grip"][1])
        om_s, conf = rows[i][5], rows[i][6]
        th_s = th_out[i]
        k = kind[i]
        col = (0, 0, 255) if k == "meas" else (255, 220, 0)   # red measured / cyan predicted
        if not math.isnan(th_s):
            t = math.radians(th_s)
            e = (int(gx + r_max * math.cos(t)), int(gy + r_max * math.sin(t)))
            cv2.line(frame, (gx, gy), e, col, 2 if k == "meas" else 1, cv2.LINE_AA)
        if k == "meas" and not math.isnan(d["band"]):
            for a in (d["th_open"], d["th_close"]):
                t = math.radians(a)
                e = (int(gx + r_max * math.cos(t)), int(gy + r_max * math.sin(t)))
                cv2.line(frame, (gx, gy), e, (0, 165, 255), 1, cv2.LINE_AA)
        cv2.circle(frame, (gx, gy), 5, (255, 0, 0), -1)
        hud = (f"f{i:04d} th={th_s:7.1f} om={om_s:8.1f} c={conf:.2f} "
               f"{k.upper()} {d['flag']}")
        cv2.putText(frame, hud, (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                    (255, 255, 255), 1, cv2.LINE_AA)
        vw.write(frame)
        if k != "meas":
            review.append((i, frame.copy()))
    vw.release()

    csv_path = os.path.join(args.out_dir, f"{stem}_track.csv")
    with open(csv_path, "w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["frame", "t_s", "theta_filt", "omega_filt", "theta_smooth",
                       "omega_smooth", "conf", "S_peak", "support", "near",
                       "band_deg", "flag", "grip_x", "grip_y", "kind", "theta_out"])
        for i, r in enumerate(rows):
            wcsv.writerow(list(r) + [kind[i], th_out[i]])

    n_meas = kind.count("meas")
    print(f"frames={len(rows)}  measured={n_meas}  predicted={kind.count('pred')}  "
          f"segments={len(segs)}")
    print(f"outputs: {stem}_annotated.mp4  {stem}_track.csv")

if __name__ == "__main__":
    main()
