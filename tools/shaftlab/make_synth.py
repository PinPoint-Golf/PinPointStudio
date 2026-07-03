#!/usr/bin/env python3
"""Synthetic swing generator per design doc §12.1, extended for clubhead
stage 2 (H0): foreshortening (projected-length) profile, clubhead blob with
motion streak, and ground truth in the truth.json schema.

Outputs (in --out-dir, default "."):
  swing_synth.mp4   the clip
  truth.csv         legacy per-frame theta/omega truth (shaft tests)
  truth.json        {shaft:[{t_us,theta,grip,head,len}], events, meta}
  clipmeta.json     {swingDir:<out-dir>, fps, W, H, t_us:[...]}  (score_truth-ready)
  synth_track.csv   shaft-track contract v1 (theta_out = truth + --track-noise)
                    so stage-2 tools run without a live stage-1 pass
"""
import argparse, csv, json, math, os
import numpy as np, cv2

W, H, FPS = 640, 800, 120.0
DT = 1.0 / FPS
T_EXP = DT * 0.5
SUBS = 8                    # sub-exposure renders per frame
ANCHOR0 = np.array([320.0, 380.0])
SHAFT_LEN = 300.0
RNG = np.random.default_rng(7)

# swing-phase event times (seconds), aligned with theta_profile segments
EVENTS = {"p1_s": 0.15, "p2_s": 0.30, "p3_s": 0.50, "p4_s": 0.65, "p5_s": 0.70,
          "p6_s": 0.75, "p7_s": 0.78, "p8_s": 0.85, "p9_s": 0.90, "p10_s": 1.00,
          "t0_us": 0}

def theta_profile():
    """address hold -> backswing -> fast downswing -> follow-through (degrees)."""
    ts, th = [], []
    def seg(t0, t1, a0, a1, n, ease=True):
        for k in range(n):
            u = k / n
            if ease:
                u = 0.5 - 0.5 * math.cos(math.pi * u)
            ts.append(t0 + (t1 - t0) * k / n)
            th.append(a0 + (a1 - a0) * u)
    seg(0.0, 0.15, 100, 100, 18, ease=False)          # address
    seg(0.15, 0.65, 100, -80, 60)                      # backswing
    seg(0.65, 0.78, -80, 100, 16)                      # downswing (fast)
    seg(0.78, 1.0, 100, 250, 26)                       # follow-through
    return np.array(ts), np.array(th)

# projected-length ratio rho(t): 1 at address/impact, dips under foreshortening
# (shape mirrors the swing_0008 hand-label observation)
RHO_T = np.array([0.0, 0.15, 0.40, 0.65, 0.72, 0.78, 0.90, 1.00])
RHO_V = np.array([1.0, 1.00, 0.55, 0.80, 0.60, 1.00, 0.70, 0.45])

def rho_at(t):
    return float(np.interp(t, RHO_T, RHO_V))

def draw_shaft(img, grip, theta_deg, length_px, drop_mask):
    t = math.radians(theta_deg)
    d = np.array([math.cos(t), math.sin(t)])
    n = 60
    for k in range(n):
        if drop_mask[k]:
            continue
        r0 = 30 + (length_px - 30) * k / n
        r1 = 30 + (length_px - 30) * (k + 1) / n
        wpx = 6.0 - 3.0 * k / n
        p0 = (grip + r0 * d).astype(int)
        p1 = (grip + r1 * d).astype(int)
        b = int(140 + 100 * RNG.random())              # specular brightness variation
        cv2.line(img, tuple(p0), tuple(p1), (b, b, b), max(1, int(wpx)), cv2.LINE_AA)
    # clubhead blob at the distal end (bright; streaks across sub-exposures)
    hp = (grip + length_px * d).astype(int)
    cv2.ellipse(img, tuple(hp), (11, 7), theta_deg, 0, 360, (225, 225, 225), -1, cv2.LINE_AA)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--track-noise", type=float, default=2.0,
                    help="sigma (deg) of theta noise in the emitted contract track CSV")
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    ts, th = theta_profile()
    def theta_at(t):
        return float(np.interp(t, ts, th))
    clip = os.path.join(a.out_dir, "swing_synth.mp4")
    vw = cv2.VideoWriter(clip, cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    truth_rows, shaft_json, track_rows, t_us_list = [], [], [], []
    n_frames = len(ts)
    for i in range(n_frames):
        t0 = i * DT
        grip = ANCHOR0 + np.array([2.0 * math.sin(2 * math.pi * t0),
                                   1.5 * math.cos(2 * math.pi * t0)])
        acc = np.zeros((H, W, 3), np.float32)
        drop = RNG.random(60) < 0.35                   # 35% specular dropout chunks
        for s in range(SUBS):
            sub = np.zeros((H, W, 3), np.uint8)
            t_s = t0 + T_EXP * s / (SUBS - 1)
            draw_shaft(sub, grip, theta_at(t_s), SHAFT_LEN * rho_at(t_s), drop)
            acc += sub.astype(np.float32)
        frame = (acc / SUBS).astype(np.uint8)
        # dark textured background + hands blob + noise
        frame = cv2.add(frame, np.full_like(frame, 12))
        cv2.circle(frame, tuple(grip.astype(int)), 14, (90, 80, 70), -1)
        noise = RNG.normal(0, 4, frame.shape).astype(np.int16)
        frame = np.clip(frame.astype(np.int16) + noise, 0, 255).astype(np.uint8)
        vw.write(frame)
        t_mid = t0 + T_EXP / 2
        th_mid = theta_at(t_mid)
        L_mid = SHAFT_LEN * rho_at(t_mid)
        dvec = np.array([math.cos(math.radians(th_mid)), math.sin(math.radians(th_mid))])
        head = grip + L_mid * dvec
        truth_rows.append([i, t0, th_mid,
                           (theta_at(t0 + T_EXP) - theta_at(t0)) / T_EXP,
                           grip[0], grip[1]])
        t_us = int(round(t0 * 1e6))
        t_us_list.append(t_us)
        shaft_json.append({"t_us": t_us, "theta": math.radians(th_mid),
                           "grip": [float(grip[0]), float(grip[1])],
                           "head": [float(head[0]), float(head[1])],
                           "len": float(L_mid)})
        track_rows.append([i, f"{grip[0]:.2f}", f"{grip[1]:.2f}",
                           f"{th_mid + RNG.normal(0, a.track_noise):.3f}", "meas", "0.90"])
    vw.release()
    with open(os.path.join(a.out_dir, "truth.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["frame", "t_s", "theta_true", "omega_true", "gx", "gy"])
        w.writerows(truth_rows)
    with open(os.path.join(a.out_dir, "truth.json"), "w") as f:
        json.dump({"shaft": shaft_json, "events": EVENTS,
                   "meta": {"synthetic": True, "shaftLenPx": SHAFT_LEN,
                            "rho_t": RHO_T.tolist(), "rho_v": RHO_V.tolist()}}, f)
    with open(os.path.join(a.out_dir, "clipmeta.json"), "w") as f:
        json.dump({"swingDir": os.path.abspath(a.out_dir), "frame0": 0, "fps": FPS,
                   "W": W, "H": H, "t_us": t_us_list}, f)
    with open(os.path.join(a.out_dir, "synth_track.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["frame", "grip_x", "grip_y", "theta_out", "kind", "conf"])
        w.writerows(track_rows)
    print(f"wrote {clip} ({n_frames} frames) + truth.csv/.json + clipmeta.json + synth_track.csv, "
          f"peak |omega| = {max(abs(r[3]) for r in truth_rows):.0f} deg/s")

if __name__ == "__main__":
    main()
