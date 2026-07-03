"""
shaft_annotate.py — PinPoint shaft detection exemplar (v7 working prototype,
2026-07-05).

Reference implementation and video markup tool for single-camera shaft tracking:
anchored polar unwrap, edge-pair + motion evidence, line/wedge regimes,
PER-SEGMENT KF + RTS, still-hold stacked re-acquisition, pose-derived body
gating, and a measured/predicted output model. Oracle for any C++ port.

STATUS: v7 working prototype (F19-F21 on top of v6; findings doc §8).
v7 adds: F19 any-speed strict scene-permanence veto at re-inits (golf-bag /
scenery locks), F20 in-motion permanence reference (bg0_move; AND-permanence,
immune to address/finish-hold poisoning, enables pre-swing vetoes), F21
sector-conditioned hold gates (fixes the hang-region body lock while
preserving the shouldered finish club). Fresh-baseline note: the v6 "freeze"
fixtures were not reproducible against current clip encodes; v7 baselines are
the reference (0008 labels: 0% bad>30 in BOTH tiers). Known remaining limit:
quasi-static finish body-line holds with bright-shoe blob credit (audit list
in findings §8; density gate tried and reverted). Post-impact coverage is
capture-bound (uncropped captures pending).

Design doc:   docs/design/shaft_detection_improvements.md   (original architecture)
Learnings:    docs/design/shaft_detection_exemplar_findings.md   (F1-F17 + methodology)
How to run:   docs/implementation/shaft_markup_exemplar_impl.md

Companion tools (same folder): prep_swing.py (swing dir -> clip + anchors +
skeleton), montage.py (visual review tiles), score_truth.py (numeric eval vs
truth.json).

Usage:
  prep_swing.py <swingDir> <outDir>
  shaft_annotate.py <outDir>/faceon_swing.mp4 --anchors <outDir>/anchors.csv \
      --fps-override <clipmeta.fps> [--out-dir out] [--debug-frames 186,310]

Output CSV: per frame theta_filt/omega_filt/theta_smooth/omega_smooth/conf/flags
plus `kind` (meas|pred) and `theta_out` — consumers use theta_out, treating
kind=meas as label-grade and kind=pred as clearly-marked kinematic prediction
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
HOLD_BLOB_MIN = 0.5      # (NMS credit only since F16 — see body gate)
HOLD_CHG_MIN = 0.15      # (retained for tuning experiments)
BODY_DIST_PX = 40.0      # F16: run samples within this of a body segment count as body
BODY_FRAC_MAX = 0.6      # F16: reject acquisition when > this fraction of the run is body
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

    def rts(self, marks=None):
        """Per-SEGMENT RTS. `marks[k]`: 'i' = (re)init frame (starts a segment),
        't' = tracked (predict/update), 'f' = free/lost (pseudo entry). The
        backward recursion runs only within a contiguous ['i','t',...,'t'] run —
        smoothing across an init boundary uses the init's pseudo-prediction
        (Pp == P0) as if it were a real one and detonates numerically (observed:
        smoothed omega 1e18..1e49 through long junk chains). 'f' frames pass
        through unsmoothed."""
        n = len(self.hist)
        xs = [None] * n
        Ps = [None] * n
        if marks is None:
            marks = ['i'] + ['t'] * (n - 1)
        i = 0
        while i < n:
            if marks[i] == 'f':
                xs[i], Ps[i] = self.hist[i][0], self.hist[i][1]
                i += 1
                continue
            j = i
            while j + 1 < n and marks[j + 1] == 't':
                j += 1
            xs[j], Ps[j] = self.hist[j][0], self.hist[j][1]
            for k in range(j - 1, i - 1, -1):
                x, P, _, _ = self.hist[k]
                _, _, xp1, Pp1 = self.hist[k + 1]
                C = P @ self.F.T @ np.linalg.inv(Pp1)
                dx = xs[k + 1] - xp1
                dx[0] = wrap180(dx[0])
                xs[k] = x + C @ dx
                Ps[k] = P + C @ (Ps[k + 1] - Pp1) @ C.T
            i = j + 1
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

class SkeletonSource:
    """skeleton.csv rows: frame, then x,y,conf (px) per joint in the order
    [Lshoulder, Rshoulder, Lhip, Rhip, Lknee, Rknee, Lankle, Rankle]. Provides
    per-frame torso/leg segments for the F16 body-collinearity gate. Arms and
    head are deliberately excluded (the club is a forearm continuation and may
    legitimately cross the head region at the finish)."""
    SEGS = [(0, 1), (2, 3), (0, 2), (1, 3), (2, 4), (3, 5), (4, 6), (5, 7)]

    def __init__(self, path):
        self.frames = {}
        try:
            with open(path) as f:
                for row in csv.reader(f):
                    if not row or not row[0].strip().lstrip('-').isdigit():
                        continue
                    v = [float(x) for x in row[1:]]
                    self.frames[int(row[0])] = [(v[3*j], v[3*j+1], v[3*j+2])
                                                for j in range(len(v) // 3)]
            self.ok = len(self.frames) > 0
        except OSError:
            self.ok = False

    def segments(self, idx):
        jt = self.frames.get(idx)
        if not jt:
            return []
        out = []
        for a, b in self.SEGS:
            if a < len(jt) and b < len(jt) and jt[a][2] > 0.3 and jt[b][2] > 0.3:
                out.append((jt[a][0], jt[a][1], jt[b][0], jt[b][1]))
        return out

    def arm_segments(self, idx, grip):
        """F19: shoulder->grip lines approximate the ARMS (skeleton has no
        elbows/wrists). Used ONLY by the fast-re-init arm-collinearity veto —
        the tracking-time F16 gate keeps excluding arms deliberately (the
        hanging club is legitimately arm-adjacent, but that is a quasi-static
        regime this veto never touches)."""
        jt = self.frames.get(idx)
        if not jt:
            return []
        out = []
        for j in (0, 1):
            if j < len(jt) and jt[j][2] > 0.3:
                out.append((jt[j][0], jt[j][1], float(grip[0]), float(grip[1])))
        return out


def body_fraction(grip, th_deg, rs_frac, ext_frac, segs, r_max, n=32):
    """F16: fraction of a candidate's supported run lying within BODY_DIST_PX of
    the golfer's torso/leg segments — a run that tracks the body is the body."""
    if not segs or ext_frac <= 0:
        return 0.0
    t = math.radians(th_deg)
    c, sn = math.cos(t), math.sin(t)
    hits = 0
    for k in range(n):
        r = (rs_frac + ext_frac * (k + 0.5) / n) * r_max
        px, py = grip[0] + r * c, grip[1] + r * sn
        best = 1e9
        for (x1, y1, x2, y2) in segs:
            dx, dy = x2 - x1, y2 - y1
            L2 = dx * dx + dy * dy
            u = 0.0 if L2 < 1e-6 else max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / L2))
            qx, qy = x1 + u * dx, y1 + u * dy
            best = min(best, math.hypot(px - qx, py - qy))
            if best < BODY_DIST_PX:
                break
        if best < BODY_DIST_PX:
            hits += 1
    return hits / float(n)


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
    ap.add_argument("--seg-dump", action="store_true",
                    help="write <stem>_segments.csv: per KF segment init path/"
                         "motion/accepts/length/conf/outcome (fix-development aid)")
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
    # overlay at the CONTAINER rate: mpeg4 rejects fractional timebases (a
    # fractional --fps-override made the writer fail on the c1 corpus); the
    # true rate only matters for dt/KF, playback speed is cosmetic
    vw_fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    if vw_fps != round(vw_fps):
        vw_fps = float(round(vw_fps)) or 30.0
    vw = cv2.VideoWriter(os.path.join(args.out_dir, f"{stem}_annotated.mp4"),
                         cv2.VideoWriter_fourcc(*"mp4v"), vw_fps, (W, H))

    dbg = set(x.strip() for x in args.debug_frames.split(",") if x.strip())
    anchor = AnchorSource(args.anchors)
    skel = SkeletonSource(os.path.join(os.path.dirname(os.path.abspath(args.anchors)),
                                       "skeleton.csv"))

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

    # F20: a SECOND permanence reference from IN-MOTION frames only. The
    # whole-clip median assumes no club pose dominates the clip; on
    # address-hold-dominant captures (c1: ~60% at address) it CONTAINS the
    # address club, corrupting the F12 veto (§6.4: the reference must not
    # contain the target). Frames chosen by CONSECUTIVE-frame grip-ROI motion
    # are guaranteed park-free (excludes address AND finish holds — the
    # finish-weighted-median regression of §6.4 item 2 cannot recur).
    # Used as an AND with bg0 (scene_perm): truly permanent structure is in
    # BOTH; the address club is only in bg0; a held finish club is in neither.
    bg0_move = None
    if ntot > 40:
        cand = []
        step = max(1, (ntot - 3) // 33)
        for k in range(0, ntot - 2, step):
            cap.set(cv2.CAP_PROP_POS_FRAMES, k)
            ok1, f1 = cap.read()
            ok2, f2 = cap.read()
            if not (ok1 and ok2):
                continue
            g1 = cv2.cvtColor(f1, cv2.COLOR_BGR2GRAY).astype(np.float32)
            g2 = cv2.cvtColor(f2, cv2.COLOR_BGR2GRAY).astype(np.float32)
            mo = float(np.abs(g2 - g1).mean())   # whole-frame consecutive motion
            cand.append((mo, g1))
        cand.sort(key=lambda c: -c[0])
        top = [g for mo, g in cand[:11] if mo > 1.0]
        if len(top) >= 5:
            bg0_move = np.median(np.stack(top), axis=0)
        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)

    from collections import deque
    grays_hist = deque(maxlen=HOLD_K)     # F10: recent grays for stacking
    grips_hist = deque(maxlen=HOLD_K)     # F10: recent anchors (median-stabilised)
    still_hist = deque(maxlen=HOLD_K)     # F10: windowed stillness votes
    unmeas_run = 99                       # frames since last accepted measurement
    hold_active = False                   # F10: static-hold tracking engaged
    seg_marks = []                        # per-frame RTS segment tags ('i'/'t'/'f')
    runaway = 0                           # F15: consecutive insane-state frames
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
        def _perm_in(ref, g, th_c, s_ref, rs_ref, ext_ref, s_fac, ov_fac):
            thp = np.arange(th_c - 2.0, th_c + 2.0 + DTHETA, DTHETA)
            Sp, _, _, Rp, _, Ep, _, _ = evidence_scan(ref, ref, None, g, thp, r_min, r_max)
            ip = int(np.argmax(Sp))
            if float(Sp[ip]) <= max(s_fac * s_ref, 0.8 * S_ACCEPT):
                return False
            # permanent only if the reference run RADIALLY overlaps the
            # candidate's run: far-radius permanent structure (lens-flare arc,
            # lamps) must not veto a club whose evidence lives at near radii;
            # the neon-parallel lock's run IS the neon's own pixels.
            sc, ec = rs_ref, rs_ref + ext_ref
            sp, ep = float(Rp[ip]), float(Rp[ip]) + float(Ep[ip])
            ov = max(0.0, min(ec, ep) - max(sc, sp))
            return ov > ov_fac * max(ext_ref, 0.05)

        def scene_perm(g, th_c, s_ref, rs_ref, ext_ref, strict=False):
            # F20: when the in-motion reference exists, permanence requires
            # the structure in BOTH references (the address club is only in
            # bg0 on hold-dominant clips; a held finish club is in neither —
            # §6.4 both ways). That also makes the veto safe BEFORE the swing
            # (the swing_seen guard existed to protect the address club in
            # bg0 — bg0_move provides that protection structurally), which
            # matters: the c1 golf-bag locks happen mid-BACKSWING, before
            # swing_seen ever arms (the reason the first strict-veto
            # experiment silently did nothing).
            if not swing_seen and bg0_move is None:
                return False
            # strict = FAST-scene re-inits: higher bar on evidence STRENGTH
            # (motion corroborates a real club, so only a self-standing
            # permanent line may veto) but the SAME overlap bar — junk runs
            # motion-smeared beyond the permanent structure dilute the overlap
            # fraction (s0010 f437: bag line covers 0.66 of a 0.97-ext run;
            # a 0.7 bar let it through).
            s_fac, ov_fac = (0.85, 0.5) if strict else (0.6, 0.5)
            r1 = _perm_in(bg0, g, th_c, s_ref, rs_ref, ext_ref, s_fac, ov_fac)
            r2 = (_perm_in(bg0_move, g, th_c, s_ref, rs_ref, ext_ref, s_fac, ov_fac)
                  if (r1 and bg0_move is not None) else None)
            if str(idx) in dbg:
                print(f"[dbg f{idx}] scene_perm th={th_c:.1f} s_ref={s_ref:.3f} "
                      f"rs={rs_ref:.2f} ext={ext_ref:.2f} strict={strict} "
                      f"perm_bg0={r1} perm_move={r2}")
            if not r1:
                return False
            if bg0_move is not None:
                return bool(r2)
            return True
        grip, phi = anchor.get(idx)
        if grip is None:
            sys.exit("anchors.csv gave no grip for frame 0")

        flag = FLAG_LOST
        seg_mark = 't'
        S_pk = sup = near = band = np.nan
        th_open = th_close = np.nan
        meas_used = False
        init_path = None          # seg-dump: which path (re)initialised this frame

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
                best_clear = None
                for ii in cands:
                    bf = body_fraction(g_med, float(gth[ii]), float(Rh[ii]),
                                       float(Eh[ii]), skel.segments(idx), r_max)
                    # F21: sector-conditioned hold gates (s9v2 hang adjudication,
                    # f299: the body line at 91.5 deg won via a SHOES blob —
                    # bf 0.91 / B 0.93 — while the true hanging club at 71.2 deg
                    # failed both gates, bf 0.84 / B 0.06: the body-adjacent +
                    # blob-free TRUE-club quadrant the F16 OR never covered).
                    # The forearm sector separates them (junk 124.6 deg from phi,
                    # truth 104.3): a sector-CONSISTENT candidate gets the blob
                    # rescue and a relaxed body bar (the hanging club is
                    # legitimately body-adjacent); a sector-INCONSISTENT one
                    # must be body-CLEAR — which is exactly how the shouldered
                    # finish club passes (bf 0.00 in the F16 freeze data), so
                    # the finish-wrap case F3 was disabled for stays alive.
                    in_sector = (phi is None
                                 or abs(wrap180(float(gth[ii]) - phi)) <= SECTOR_HALF)
                    if in_sector:
                        body_ok = bf < 0.9 or float(Bh[ii]) >= HOLD_BLOB_MIN
                    else:
                        body_ok = bf < BODY_FRAC_MAX
                    # NOTE: a hold-density requirement (Den >= DEN_MIN) was
                    # tried against the s4 f654 shoe/body drift lock (Den 0.32
                    # vs the true hang's 0.87) and REVERTED: on the dark c1
                    # stratum genuine holds are dropout-fragmented too, and the
                    # corpus gate rejected it (coverage collapse + one new
                    # confident-wrong from churn). The shoe-blob/hold-drift
                    # class stays on the audit list, mechanism known, no clean
                    # fix under A/B yet.
                    okc = (Sh[ii] > S_ACCEPT and float(Rh[ii]) < RSTART_MAX
                           and (Fh[ii] > F_MIN or (float(Eh[ii]) >= EXT_MIN
                                                   and float(Dh[ii]) >= DEN_MIN))
                           and body_ok
                           and not scene_perm(g_med, float(gth[ii]), float(Sh[ii]), float(Rh[ii]), float(Eh[ii])))
                    if str(idx) in dbg:
                        print(f"[dbg f{idx}] HOLD bf={bf:.2f} "
                              f"cand th={gth[ii]:.1f} S={Sh[ii]:.3f} "
                              f"F={Fh[ii]:.2f} Rs={Rh[ii]:.2f} B={Bh[ii]:.2f} "
                              f"Ext={Eh[ii]:.2f} Den={Dh[ii]:.2f} ok={okc} "
                              f"gmed=({g_med[0]:.0f},{g_med[1]:.0f})")
                    sc = float(Sh[ii]) * (1.0 + BLOB_GAIN * float(Bh[ii]))
                    if okc:
                        if best is None or sc > best[0]:
                            best = (sc, ii)
                        # F17: bodies ALWAYS produce candidate lines; a clear-of-
                        # body passer must outrank any body-adjacent one (a stale
                        # re-acquisition of a vacated hang position is fed by the
                        # body seam alone, while the real club rests elsewhere)
                        if bf < BODY_FRAC_MAX and (best_clear is None or sc > best_clear[0]):
                            best_clear = (sc, ii)
                if best_clear is not None:
                    best = best_clear
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
                    and (not quasi_static
                         or body_fraction(grip, float(th_g), float(Rg[ig]), float(Eg[ig]),
                                          skel.segments(idx), r_max) < BODY_FRAC_MAX
                         or float(Bg[ig]) >= HOLD_BLOB_MIN)
                    and not scene_perm(grip, float(th_g), float(Sg[ig]), float(Rg[ig]),
                                       float(Eg[ig]), strict=not quasi_static)):
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
            seg_marks.append('i')
            flag, meas_used = FLAG_REINIT, True
            confirm = 0
            unmeas_run = 0
            S_pk, sup, near = hold_vals
            cv2.accumulateWeighted(gray, bg, 0.02)
            prev_gray = gray
            frames_raw.append(frame)
            per_frame.append(dict(grip=grip, flag=flag, S=S_pk, sup=sup, near=near,
                                  band=np.nan, th_open=np.nan, th_close=np.nan,
                                  confirmed=False, still=still_now,
                                  init_path='holdx', roi=roi_mean, qs=quasi_static))
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
            seg_mark = 'i'
            initialised, flag, meas_used = True, FLAG_REINIT, True
            confirm = 0
            init_path = 'hold'
        elif not initialised and still_ok:
            # F11: still but the stacked hold path did not accept — a single-frame
            # init here is exactly how static distractors (collar+neon rays) lock
            # in; stay lost until hold evidence or motion resumes.
            kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
            seg_mark = 'f'
        elif not initialised:
            # F6: 180-flip disambiguation — compare the peak with its opposite ray,
            # crediting a bright distal blob at the supported run's end (design §7).
            j = (i_pk + int(round(180.0 / DTHETA))) % len(thetas)
            cand = []
            for ii in (i_pk, j):
                # F19: forearm-plausibility sector at FAST re-inits. The c1
                # corpus showed mid-swing full-circle re-inits locking the
                # golfer's own extended ARMS (bright sleeves outgun a dark
                # wood shaft — s0010 f437 adjudicated): an arm lock points
                # back up the forearm, ~180 deg outside F3's sector. F3 stays
                # OFF for quasi-static re-inits (the finish wrap breaks the
                # forearm-continuation assumption — those go through the hold
                # path anyway), so this closes the fast gap without touching
                # the finish regime.
                # Sector + arm-collinearity vetoes at fast re-inits were
                # TRIED and REMOVED: the sector fixed one swing and broke
                # another (s0007 vs s0008 — at takeaway the true club lies
                # near the forearm line, so the sector cannot separate), and
                # the shoulder->grip arm veto never fired (the c1 locks are
                # bag/scenery lines on the far side of the grip, not arms —
                # s0010 f437 adjudicated). The permanence veto below owns the
                # class.
                sector_ok = True
                arm_ok = True
                # NOTE: an F13/F16-style blob rescue on this veto was tried
                # and REJECTED by A/B — the blob window sits at the run's
                # DISTAL end, beyond the permanent structure, where the moving
                # golfer makes it "changed", so it rescued the junk locks too.
                perm = scene_perm(grip, float(thetas[ii]), float(S[ii]), float(Rs[ii]),
                                  float(Ext[ii]), strict=not quasi_static)
                okc = sector_ok and arm_ok \
                      and (S[ii] > S_ACCEPT) and (float(Rs[ii]) < RSTART_MAX) \
                      and (F[ii] > F_MIN or (float(Ext[ii]) >= EXT_MIN
                                             and float(Den[ii]) >= DEN_MIN)) \
                      and (not (swing_seen and quasi_static)
                           or body_fraction(grip, float(thetas[ii]), float(Rs[ii]),
                                            float(Ext[ii]), skel.segments(idx),
                                            r_max) < BODY_FRAC_MAX
                           or float(B[ii]) >= HOLD_BLOB_MIN) \
                      and not perm
                if str(idx) in dbg:
                    print(f"[dbg f{idx}] FLIP cand th={float(thetas[ii]):.1f} S={float(S[ii]):.3f} "
                          f"sector={sector_ok} arm={arm_ok} perm={perm} qs={quasi_static} "
                          f"swing_seen={swing_seen} move_ref={bg0_move is not None} okc={okc}")
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
                seg_mark = 'i'
                initialised, flag, meas_used = True, FLAG_REINIT, True
                confirm = 0
                init_path = 'flip'
            else:
                kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
                seg_mark = 'f'
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
            # F15: runaway kill — accepted-but-insane states (wedge-band omega
            # inflation) must not keep a dead track alive through impact
            runaway = runaway + 1 if abs(kf.x[1]) > OMEGA_MAX * 1.5 else 0
            if runaway >= 6:
                initialised = False
                runaway = 0
                misses = 0
                flag = FLAG_LOST
            misses = 0 if meas_used else misses + 1
            # F15: coast budget is TIME AT CURRENT SPEED, not frames — 12 coast
            # frames at 2000 deg/s is ~160 deg of blind drift; fast phases get 4.
            coast_budget = COAST_MAX if abs(kf.x[1]) < 800.0 else 4
            if misses > coast_budget:
                initialised = False
                misses = 0
                flag = FLAG_LOST

        # lostness accounting runs EVERY frame (incl. fully-LOST ones): only a
        # confirmed, omega-sane measurement counts as "found" (F10/F15)
        sane = (meas_used and initialised
                and abs(kf.x[1]) <= OMEGA_MAX and confirm >= 3)
        unmeas_run = 0 if sane else unmeas_run + 1

        cv2.accumulateWeighted(gray, bg, 0.02)
        prev_gray = gray
        frames_raw.append(frame)
        seg_marks.append(seg_mark)
        per_frame.append(dict(grip=grip, flag=flag, S=S_pk, sup=sup, near=near,
                              band=band, th_open=th_open, th_close=th_close,
                              confirmed=confirm >= 3, still=still_now,
                              init_path=init_path, roi=roi_mean, qs=quasi_static))
        rows.append([idx, idx * dt, kf.x[0], kf.x[1], S_pk, sup, near, band, flag,
                     grip[0], grip[1]])
        idx += 1
    cap.release()

    # ---- RTS smoothing + posterior-variance confidence ----
    xs, Ps = kf.rts(seg_marks) if kf.hist else ([], [])
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

    # ---- seg-dump: per-KF-segment diagnostics (fix-development aid) ----
    if args.seg_dump:
        seg_rows = []
        i = 0
        while i < nfr:
            if seg_marks[i] == 'i':
                j = i
                while j + 1 < nfr and seg_marks[j + 1] == 't':
                    j += 1
                pf0 = per_frame[i]
                accepts = sum(1 for k2 in range(i, j + 1)
                              if per_frame[k2]["flag"] in (FLAG_LINE, FLAG_WEDGE, FLAG_REINIT))
                om_seg = np.abs(om_arr[i:j + 1])
                om_seg = om_seg[np.isfinite(om_seg)]
                confs = conf_arr[i:j + 1]
                n_meas_seg = sum(1 for k2 in range(i, j + 1) if kind[k2] == "meas")
                seg_rows.append([i, j, j - i + 1, pf0.get("init_path") or "?",
                                 f"{pf0.get('roi', float('nan')):.1f}",
                                 int(bool(pf0.get("qs", False))),
                                 accepts,
                                 f"{float(np.median(om_seg)) if len(om_seg) else float('nan'):.0f}",
                                 f"{float(np.max(om_seg)) if len(om_seg) else float('nan'):.0f}",
                                 f"{float(np.max(confs)):.2f}",
                                 n_meas_seg])
                i = j + 1
            else:
                i += 1
        seg_csv = os.path.join(args.out_dir, f"{stem}_segments.csv")
        with open(seg_csv, "w", newline="") as f:
            wseg = csv.writer(f)
            wseg.writerow(["start", "end", "len", "init_path", "roi_at_init",
                           "quasi_static_at_init", "accepts", "omega_med",
                           "omega_max", "conf_max", "n_meas"])
            wseg.writerows(seg_rows)
        print(f"[seg-dump] {len(seg_rows)} segments -> {seg_csv}")

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
