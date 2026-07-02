#!/usr/bin/env python3
"""Synthetic swing generator per design doc §12.1. Produces swing_synth.mp4 + truth.csv."""
import numpy as np, cv2, csv, math

W, H, FPS = 640, 800, 120.0
DT = 1.0 / FPS
T_EXP = DT * 0.5
SUBS = 8                    # sub-exposure renders per frame
ANCHOR0 = np.array([320.0, 380.0])
SHAFT_LEN = 300.0
RNG = np.random.default_rng(7)

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

def draw_shaft(img, grip, theta_deg, drop_mask):
    t = math.radians(theta_deg)
    d = np.array([math.cos(t), math.sin(t)])
    n = 60
    for k in range(n):
        if drop_mask[k]:
            continue
        r0 = 30 + (SHAFT_LEN - 30) * k / n
        r1 = 30 + (SHAFT_LEN - 30) * (k + 1) / n
        wpx = 6.0 - 3.0 * k / n
        p0 = (grip + r0 * d).astype(int)
        p1 = (grip + r1 * d).astype(int)
        b = int(140 + 100 * RNG.random())              # specular brightness variation
        cv2.line(img, tuple(p0), tuple(p1), (b, b, b), max(1, int(wpx)), cv2.LINE_AA)

def main():
    ts, th = theta_profile()
    # interpolator for sub-exposure sampling
    def theta_at(t):
        return float(np.interp(t, ts, th))
    vw = cv2.VideoWriter("/home/claude/swing_synth.mp4",
                         cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    truth = []
    n_frames = len(ts)
    for i in range(n_frames):
        t0 = i * DT
        grip = ANCHOR0 + np.array([2.0 * math.sin(2 * math.pi * t0),
                                   1.5 * math.cos(2 * math.pi * t0)])
        acc = np.zeros((H, W, 3), np.float32)
        drop = RNG.random(60) < 0.35                   # 35% specular dropout chunks
        for s in range(SUBS):
            sub = np.zeros((H, W, 3), np.uint8)
            th_s = theta_at(t0 + T_EXP * s / (SUBS - 1))
            draw_shaft(sub, grip, th_s, drop)
            acc += sub.astype(np.float32)
        frame = (acc / SUBS).astype(np.uint8)
        # dark textured background + hands blob + noise
        frame = cv2.add(frame, np.full_like(frame, 12))
        cv2.circle(frame, tuple(grip.astype(int)), 14, (90, 80, 70), -1)
        noise = RNG.normal(0, 4, frame.shape).astype(np.int16)
        frame = np.clip(frame.astype(np.int16) + noise, 0, 255).astype(np.uint8)
        vw.write(frame)
        truth.append([i, t0, theta_at(t0 + T_EXP / 2),                    # mid-exposure
                      (theta_at(t0 + T_EXP) - theta_at(t0)) / T_EXP,       # omega
                      grip[0], grip[1]])
    vw.release()
    with open("/home/claude/truth.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["frame", "t_s", "theta_true", "omega_true", "gx", "gy"])
        w.writerows(truth)
    print(f"wrote swing_synth.mp4 ({n_frames} frames) + truth.csv, "
          f"peak |omega| = {max(abs(r[3]) for r in truth):.0f} deg/s")

if __name__ == "__main__":
    main()
