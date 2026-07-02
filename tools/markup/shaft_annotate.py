#!/usr/bin/env python3
"""
shaft_annotate.py — PinPoint shaft detection reference implementation & video markup tool.

Implements the design in shaft-detection-design.md (single-camera, anchored polar
unwrap, motion+gradient evidence, line/wedge regimes, KF + RTS smoother).

Outputs, per input video:
  <stem>_annotated.mp4   overlay video (smoothed shaft line, wedge edges, HUD)
  <stem>_track.csv       per-frame: theta/omega (filtered & smoothed), quality, evidence
  <stem>_review.png      contact sheet of flagged frames for human review

Anchor (grip) sources, in order of preference:
  --anchors file.csv     frame,x,y  (export from your pose pipeline — recommended)
  --grip X Y --anchor-mode lk        track from first-frame point with LK optical flow
  --grip X Y --anchor-mode constant  fixed anchor (tripod + tidy swing only)

Usage:
  python3 shaft_annotate.py swing.mp4 --grip 432 800 --seed-deg 100 \
      [--exposure 0.004] [--fps-override 120] [--out-dir out/]
"""
import argparse, csv, math, os, sys
import numpy as np
import cv2

# ----------------------------------------------------------------------------- config
DTHETA = 0.25            # deg per angular bin
R_MIN_FRAC = 0.06        # min radius as fraction of image height (skip hands)
F_MIN = 0.22             # min support fraction to accept a detection
S_ACCEPT = 0.12          # min column score to accept
TAU_G = 60.0             # gradient evidence scale
TAU_B = 35.0             # brightness evidence scale
TAU_M = 18.0             # motion evidence scale (grey levels)
S_CAP = 0.8
BAND_THR = 0.4           # wedge band segmentation threshold (frac of peak)
BETA_SWITCH = 2.0        # deg — line/wedge regime switch
COAST_MAX = 12           # frames of consecutive misses before re-init
GATE_SIGMA = 3.0
FAN_MIN = 10.0           # deg minimum half-fan
R_THETA = 1.5 ** 2       # deg^2 line-measurement variance (base)
SIGMA_JERK = 2.5e5       # deg/s^3 process noise (must tolerate ~4000 deg/s^2 downswing)

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
        self.hist = []          # (x, P, x_pred, P_pred) for RTS
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
        """z may be None (coast). Returns (accepted, innovation_sigma)."""
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
        if md2 > (GATE_SIGMA ** 2) * len(y):        # gate
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
        return xs

class AnchorSource:
    def __init__(self, args, first_gray):
        self.mode = args.anchor_mode
        self.table = None
        if args.anchors:
            self.table = {}
            with open(args.anchors) as f:
                for row in csv.reader(f):
                    if row and row[0].strip().isdigit():
                        self.table[int(row[0])] = (float(row[1]), float(row[2]))
            self.mode = "csv"
        self.pt = np.array(args.grip, np.float32) if args.grip else None
        self.prev_gray = first_gray.astype(np.uint8)
        self.ok = True

    def get(self, idx, gray):
        gray8 = gray.astype(np.uint8)
        if self.mode == "csv":
            if idx in self.table:
                self.pt = np.array(self.table[idx], np.float32)
                self.ok = True
            else:
                self.ok = False                    # hold last
        elif self.mode == "lk" and idx > 0:
            p0 = self.pt.reshape(1, 1, 2)
            p1, st, err = cv2.calcOpticalFlowPyrLK(
                self.prev_gray, gray8, p0, None,
                winSize=(31, 31), maxLevel=3,
                criteria=(cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 30, 0.01))
            if st[0][0] == 1 and err[0][0] < 40:
                self.pt = p1.reshape(2)
                self.ok = True
            else:
                self.ok = False                    # hold last position
        self.prev_gray = gray8
        return tuple(self.pt), self.ok

# ----------------------------------------------------------------------------- evidence
def evidence_scan(gray, bg, prev_gray, grip, thetas_deg, r_min, r_max):
    """Return S(theta), F(theta) over the given angle set."""
    H, W = gray.shape
    gxs = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    gys = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    mag = np.sqrt(gxs * gxs + gys * gys)
    # anti-ghost: require BOTH frame-difference and bg-difference (a background
    # ghost of the departed shaft has ~zero frame difference; a moving shaft has both)
    fd = np.abs(gray - prev_gray) if prev_gray is not None else np.abs(gray - bg)
    motion = np.minimum(np.abs(gray - bg), fd)

    xg, yg = grip
    T = np.deg2rad(thetas_deg)[:, None]
    R = np.arange(r_min, r_max, 1.0)[None, :]
    X = (xg + R * np.cos(T)).astype(np.float32)
    Y = (yg + R * np.sin(T)).astype(np.float32)
    valid = (X >= 1) & (X < W - 1) & (Y >= 1) & (Y < H - 1)
    Xc, Yc = np.clip(X, 0, W - 1), np.clip(Y, 0, H - 1)

    def samp(a):
        return cv2.remap(a.astype(np.float32), Xc, Yc, cv2.INTER_LINEAR)

    Gx, Gy, M, Mo = samp(gxs), samp(gys), samp(mag), samp(motion)
    nx, ny = -np.sin(T), np.cos(T)
    dot = Gx * nx + Gy * ny
    E_grad = np.clip((dot * dot) / (M + 1e-3) / TAU_G, 0, 1)
    E_mot = np.clip((Mo - 6.0) / TAU_M, 0, 1)
    s = np.where(valid, np.maximum(E_grad, E_mot), 0.0)

    w = R * valid
    S = (w * np.minimum(s, S_CAP)).sum(1) / (w.sum(1) + 1e-6)
    F = ((s > 0.3) & valid).sum(1) / (valid.sum(1) + 1e-6)
    k = cv2.getGaussianKernel(9, 2).ravel()
    S = np.convolve(S, k, mode="same")
    return S, F

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
    ap.add_argument("--grip", nargs=2, type=float, default=None)
    ap.add_argument("--seed-deg", type=float, default=None,
                    help="optional first-frame shaft angle hint (deg, image coords)")
    ap.add_argument("--anchors", default=None, help="CSV frame,x,y from pose pipeline")
    ap.add_argument("--anchor-mode", choices=["lk", "constant"], default="lk")
    ap.add_argument("--exposure", type=float, default=None,
                    help="exposure time in seconds; enables direct omega from wedge")
    ap.add_argument("--fps-override", type=float, default=None)
    ap.add_argument("--r-max", type=float, default=None, help="shaft search radius px")
    ap.add_argument("--out-dir", default=".")
    args = ap.parse_args()
    if not args.grip and not args.anchors:
        sys.exit("need --grip X Y or --anchors csv")

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    fps = args.fps_override or cap.get(cv2.CAP_PROP_FPS) or 30.0
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    dt = 1.0 / fps
    t_exp = args.exposure if args.exposure else 0.5 * dt   # assumption if unknown
    r_min = max(20, int(R_MIN_FRAC * H))
    r_max = args.r_max or 0.45 * H

    stem = os.path.splitext(os.path.basename(args.video))[0]
    os.makedirs(args.out_dir, exist_ok=True)
    vw = cv2.VideoWriter(os.path.join(args.out_dir, f"{stem}_annotated.mp4"),
                         cv2.VideoWriter_fourcc(*"mp4v"), fps, (W, H))

    kf = KF(dt)
    initialised = False
    misses = 0
    w0 = 3.0                        # static S(theta) profile width (deg), calibrated online
    bg = None                       # running background (exp. average, slow)
    prev_gray = None
    rows = []
    frames_raw = []                 # keep raw frames for second-pass overlay
    per_frame = []                  # per-frame detection extras for overlay

    anchor = None
    idx = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
        if bg is None:
            bg = gray.copy()
            anchor = AnchorSource(args, gray)
        grip, a_ok = anchor.get(idx, gray)

        flag = FLAG_LOST
        S_pk = sup = band = np.nan
        th_open = th_close = np.nan
        meas_used = False

        if initialised:
            xp, Pp = kf.predict()
            sigma = math.sqrt(max(Pp[0, 0], 1e-6))
            beta_pred = abs(xp[1]) * t_exp
            half_fan = max(GATE_SIGMA * sigma, FAN_MIN) + 0.5 * beta_pred + 2.0
            th_c = xp[0] % 360.0
            thetas = np.arange(th_c - half_fan, th_c + half_fan + DTHETA, DTHETA)
        else:
            thetas = np.arange(0.0, 360.0, DTHETA)
            if args.seed_deg is not None and idx == 0:
                thetas = np.arange(args.seed_deg - 45, args.seed_deg + 45, DTHETA)

        S, F = evidence_scan(gray, bg, prev_gray, grip, thetas, r_min, r_max)
        th_meas, i_pk = fit_peak(thetas, S)
        S_pk, sup = float(S[i_pk]), float(F[i_pk])

        good = (S_pk > S_ACCEPT) and (sup > F_MIN)
        if not initialised:
            if good:
                kf.init(th_meas)
                kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
                initialised, flag, meas_used = True, FLAG_REINIT, True
            else:
                kf.hist.append((kf.x.copy(), kf.P.copy(), kf.x.copy(), kf.P.copy()))
        else:
            xp, _ = kf.predict()
            beta_pred = abs(xp[1]) * t_exp
            wedge = beta_pred > BETA_SWITCH
            if good and wedge:
                a, b = fit_wedge(thetas, S, i_pk)
                th_open, th_close, band = a, b, b - a
                th_mid = 0.5 * (a + b)
                # unwrap measurement near prediction
                z_th = xp[0] + wrap180(th_mid - xp[0])
                band_blur = max(0.0, band - w0)      # remove intrinsic profile width
                sign_ok = abs(xp[1]) > 200.0          # sign from prediction unreliable near reversal
                if args.exposure and band_blur > 1.5 and sign_ok:
                    om = math.copysign(band_blur / t_exp, xp[1])
                    Rm = np.diag([R_THETA * 2.0, max((0.15 * om) ** 2, 1e4)])
                    acc = kf.step([z_th, om], Rm, [[1, 0, 0], [0, 1, 0]])
                else:
                    acc = kf.step([z_th], [[R_THETA * 2.0]], [[1, 0, 0]])
                flag = FLAG_WEDGE if acc else FLAG_COAST
                meas_used = acc
            elif good:
                a, b = fit_wedge(thetas, S, i_pk)   # measure static profile width
                if abs(xp[1]) * t_exp < 0.5:         # genuinely static: calibrate w0
                    w0 = 0.9 * w0 + 0.1 * (b - a)
                z_th = xp[0] + wrap180(th_meas - xp[0])
                acc = kf.step([z_th], [[R_THETA]], [[1, 0, 0]])
                flag = FLAG_LINE if acc else FLAG_COAST
                meas_used = acc
            else:
                kf.step(None, None, None)
                flag = FLAG_LOWSUP if sup <= F_MIN else FLAG_COAST
            misses = 0 if meas_used else misses + 1
            if misses > COAST_MAX:
                initialised = False
                misses = 0
                flag = FLAG_LOST

        # slow background update, avoiding fast-motion pixels
        cv2.accumulateWeighted(gray, bg, 0.02)
        prev_gray = gray
        frames_raw.append(frame)
        per_frame.append(dict(grip=grip, a_ok=a_ok, flag=flag, S=S_pk, sup=sup,
                              band=band, th_open=th_open, th_close=th_close))
        rows.append([idx, idx * dt, kf.x[0], kf.x[1], S_pk, sup, band, flag,
                     grip[0], grip[1]])
        idx += 1
    cap.release()

    # ---- RTS smoothing ----
    xs = kf.rts() if kf.hist else []
    for i, r in enumerate(rows):
        if i < len(xs):
            r[2:2] = []  # no-op, keep structure clear
            r.insert(4, xs[i][1])   # omega_smooth
            r.insert(4, xs[i][0])   # theta_smooth  -> cols: f,t,th_f,om_f,th_s,om_s,...
        else:
            r.insert(4, np.nan); r.insert(4, np.nan)

    # ---- overlay pass (uses smoothed track) ----
    review = []
    for i, frame in enumerate(frames_raw):
        d = per_frame[i]
        gx, gy = int(d["grip"][0]), int(d["grip"][1])
        th_s = rows[i][4]
        ok_flag = d["flag"] in (FLAG_LINE, FLAG_WEDGE, FLAG_REINIT)
        col = (0, 0, 255) if ok_flag else (0, 255, 255)
        if not math.isnan(th_s):
            t = math.radians(th_s)
            e = (int(gx + r_max * math.cos(t)), int(gy + r_max * math.sin(t)))
            cv2.line(frame, (gx, gy), e, col, 2, cv2.LINE_AA)
        if not math.isnan(d["band"]):
            for a in (d["th_open"], d["th_close"]):
                t = math.radians(a)
                e = (int(gx + r_max * math.cos(t)), int(gy + r_max * math.sin(t)))
                cv2.line(frame, (gx, gy), e, (0, 165, 255), 1, cv2.LINE_AA)
        cv2.circle(frame, (gx, gy), 5, (255, 0, 0), -1)
        hud = f"f{i:04d} th={rows[i][4]:7.1f} om={rows[i][5]:8.1f}d/s {d['flag']}"
        cv2.putText(frame, hud, (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                    (255, 255, 255), 1, cv2.LINE_AA)
        vw.write(frame)
        if not ok_flag:
            review.append((i, frame.copy()))
    vw.release()

    # ---- CSV ----
    csv_path = os.path.join(args.out_dir, f"{stem}_track.csv")
    with open(csv_path, "w", newline="") as f:
        wcsv = csv.writer(f)
        wcsv.writerow(["frame", "t_s", "theta_filt", "omega_filt", "theta_smooth",
                       "omega_smooth", "S_peak", "support", "band_deg", "flag",
                       "grip_x", "grip_y"])
        wcsv.writerows(rows)

    # ---- review contact sheet ----
    sheet_path = os.path.join(args.out_dir, f"{stem}_review.png")
    if review:
        sel = review[:: max(1, len(review) // 24)][:24]
        th = 180
        tiles = []
        for i, fr in sel:
            tw = int(fr.shape[1] * th / fr.shape[0])
            tiles.append(cv2.resize(fr, (tw, th)))
        cols = 6
        rows_n = math.ceil(len(tiles) / cols)
        tw = tiles[0].shape[1]
        sheet = np.zeros((rows_n * th, cols * tw, 3), np.uint8)
        for k, tile in enumerate(tiles):
            r, c = divmod(k, cols)
            sheet[r * th:(r + 1) * th, c * tw:c * tw + tile.shape[1]] = tile
        cv2.imwrite(sheet_path, sheet)

    n_ok = sum(1 for r in rows if r[9] in (FLAG_LINE, FLAG_WEDGE, FLAG_REINIT))
    print(f"frames={len(rows)}  tracked_ok={n_ok}  flagged={len(rows)-n_ok}")
    print(f"outputs: {stem}_annotated.mp4  {stem}_track.csv  "
          f"{stem}_review.png ({len(review)} flagged frames)")

if __name__ == "__main__":
    main()
