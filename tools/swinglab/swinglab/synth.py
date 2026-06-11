# Synthetic swing-dir generator — the harness's ground-truthed regression
# fixture (no real data needed). Produces a PinPointStudio-shaped swing dir:
# Face-On.mp4 (rendered shaft over noise), swing.json (streams with t_us, two
# inline IMU streams whose quaternions project EXACTLY onto the rendered shaft
# angle, identity-calibration bindings, an Impact phase), pose.json (injected
# PoseTrack2D — there is no human for ViTPose to find), and truth.json
# (per-frame theta/grip/head + P-position times).
#
# Geometry (all derivable in closed form):
#   phi(t): address still -> waggle burst -> backswing smoothstep to -120 deg
#           -> downswing u^2 to +20 at impact -> follow-through to +90 -> still
#   theta_img = deg2rad(90 - phi)          (y-down image convention)
#   hand IMU  = Ry(phi) * Rx(-80): rawTheta(q, x_hat) == phi, so the s_hand
#               fit must engage with sign=-1, delta=90 deg (corr ~ 1.0)
#   forearm   = Ry(0.95*phi) * Rx(-80) (inclination crosses 0 at phi = +-90)

import math
import numpy as np

from . import save_json

W, H = 640, 640
FPS = 60
T_END = 5.0
T0_US = 1_000_000
IMPACT_S = 3.5
TAKEAWAY_S, TOP_S, FOLLOW_END_S = 1.9, 3.18, 4.0
GRIP = (320.0, 280.0)
SHAFT_LEN = 210.0


def smoothstep(u):
    u = min(max(u, 0.0), 1.0)
    return u * u * (3 - 2 * u)


def phi_at(t, waggle=True):
    if t < TAKEAWAY_S:
        p = 0.0
        if waggle and 0.5 <= t <= 0.9:
            u = (t - 0.5) / 0.4
            p += 6.0 * math.sin(2 * math.pi * 2.5 * (t - 0.5)) * math.sin(math.pi * u) ** 2
        return p
    if t < TOP_S:
        return -120.0 * smoothstep((t - TAKEAWAY_S) / (TOP_S - TAKEAWAY_S))
    if t < IMPACT_S:
        u = (t - TOP_S) / (IMPACT_S - TOP_S)
        return -120.0 + 140.0 * u * u
    if t < FOLLOW_END_S:
        return 20.0 + 70.0 * smoothstep((t - IMPACT_S) / (FOLLOW_END_S - IMPACT_S))
    return 90.0


def shaft_at(t):
    """(theta_rad_imgconv, grip_px, head_px, length_px)"""
    phi = phi_at(t)
    theta = math.radians(90.0 - phi)
    L = SHAFT_LEN * (1.0 - 0.55 * math.exp(-(((phi + 120.0) / 18.0) ** 2)))
    gx = GRIP[0] + 6.0 * math.sin(math.radians(phi))
    gy = GRIP[1] - 4.0 * (1 - math.cos(math.radians(phi)))
    hx, hy = gx + L * math.cos(theta), gy + L * math.sin(theta)
    return theta, (gx, gy), (hx, hy), L


def quat_mul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return (w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2)


def quat_axis(axis, deg):
    h = math.radians(deg) / 2
    s = math.sin(h)
    return (math.cos(h), axis[0] * s, axis[1] * s, axis[2] * s)


RX_M80 = quat_axis((1, 0, 0), -80.0)


def imu_stream(scale, rate_hz=200):
    """t_us[], samples[[ax..az gx..gz qw qx qy qz]] with FD-consistent gyro."""
    n = int(T_END * rate_hz) + 1
    t_us, samples = [], []
    prev_phi = None
    for i in range(n):
        t = i / rate_hz
        phi = scale * phi_at(t)
        q = quat_mul(quat_axis((0, 1, 0), phi), RX_M80)
        # body gyro for q(t) = Ry(phi)*C: world rate is phi_dot about Y; body
        # rate = C^-1 applied... For the segmentation envelope only magnitude
        # and a consistent axis matter; use the exact world-Y FD rate rotated
        # into the body frame of the CONSTANT part (Rx80 * y_hat).
        dphi = 0.0 if prev_phi is None else (phi - prev_phi) * rate_hz
        prev_phi = phi
        gy_world = (0.0, dphi, 0.0)
        # body = (Ry*C)^-1 * world * (Ry*C): for axis Y, Ry leaves it fixed ->
        # body axis = C^-1 * y_hat = Rx(+80) * y_hat
        c80, s80 = math.cos(math.radians(80)), math.sin(math.radians(80))
        gyro = (0.0, dphi * c80, dphi * s80)
        # accel = gravity (unit, world +Z up) in the body frame: q^-1 * z_hat
        w, x, y, z = q
        az = (2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y))
        t_us.append(T0_US + int(round(t * 1e6)))
        samples.append([round(az[0], 6), round(az[1], 6), round(az[2], 6),
                        round(gyro[0], 4), round(gyro[1], 4), round(gyro[2], 4),
                        round(w, 6), round(x, 6), round(y, 6), round(z, 6)])
    return t_us, samples


def pose_frames(frame_t_us):
    """Injected PoseTrack2D: a plausible static figure + hands at the grip."""
    fixed = {
        0: (320, 100), 1: (310, 92), 2: (330, 92), 3: (300, 98), 4: (340, 98),
        5: (290, 150), 6: (350, 150), 7: (272, 215), 8: (368, 215),
        11: (300, 300), 12: (340, 300), 13: (305, 390), 14: (335, 390),
        15: (305, 470), 16: (335, 470),
    }
    frames = []
    for i, t in enumerate(frame_t_us):
        ts = (t - T0_US) / 1e6
        _, grip, _, _ = shaft_at(ts)
        kp = []
        for j in range(17):
            if j == 9:    px, py = grip[0] - 4, grip[1]      # left wrist
            elif j == 10: px, py = grip[0] + 4, grip[1]      # right wrist
            else:         px, py = fixed[j]
            kp += [round(px / W, 5), round(py / H, 5), 0.9]
        frames.append({"t_us": t, "kp": kp,
                       "lead":  [round((grip[0] - 3) / W, 5), round(grip[1] / H, 5)],
                       "trail": [round((grip[0] + 3) / W, 5), round(grip[1] / H, 5)],
                       "handConf": 0.85})
    return frames


def generate(out_dir, seed=7, waggle=True, clutter=False):
    import cv2
    from pathlib import Path
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(seed)

    n_frames = int(T_END * FPS) + 1
    frame_t_us = [T0_US + int(round(i / FPS * 1e6)) for i in range(n_frames)]

    vw = cv2.VideoWriter(str(out / "Face-On.mp4"),
                         cv2.VideoWriter_fourcc(*"mp4v"), FPS, (W, H))
    truth_frames = []
    for t_us in frame_t_us:
        ts = (t_us - T0_US) / 1e6
        theta, grip, head, L = shaft_at(ts)
        img = rng.integers(28, 52, (H, W, 3), dtype=np.uint8)   # noisy floor
        cv2.rectangle(img, (270, 90), (370, 480), (60, 55, 50), -1)  # body blob
        if clutter:  # alignment stick
            cv2.line(img, (80, 600), (560, 560), (170, 170, 170), 3)
        cv2.line(img, (int(grip[0]), int(grip[1])), (int(head[0]), int(head[1])),
                 (230, 230, 230), 4, cv2.LINE_AA)
        cv2.circle(img, (int(head[0]), int(head[1])), 6, (250, 250, 250), -1)
        vw.write(img)
        truth_frames.append({"t_us": t_us, "theta": round(theta, 5),
                             "grip": [round(grip[0], 1), round(grip[1], 1)],
                             "head": [round(head[0], 1), round(head[1], 1)],
                             "len": round(L, 1)})
    vw.release()

    hand_t, hand_s = imu_stream(1.0)
    fore_t, fore_s = imu_stream(0.95)
    identity = [1.0, 0.0, 0.0, 0.0]
    doc = {
        "schema": "pinpoint.swing/2",
        "swing": {"id": "synth", "index": 1},
        "athlete": {"name": "Synth", "handedness": "Right"},
        "session": {"dir": "synthetic"},
        "window": {"t0_us": T0_US, "duration_us": int(T_END * 1e6)},
        "streams": [
            {"kind": "video", "alias": "Face-On", "file": "Face-On.mp4",
             "source": {"pixelFormat": "BGR24", "width": W, "height": H},
             "encoded": {"width": W, "height": H},
             "capture": {"fps_num": FPS, "fps_den": 1},
             "frames": {"count": n_frames, "t_us": frame_t_us}},
            {"kind": "imu", "alias": "Synth Hand", "schema": "imu_sample_v2",
             "source": {"serial": "SYNTH-HAND"},
             "units": {"accel": "g", "gyro": "deg/s", "quat": "wxyz"},
             "samples": {"count": len(hand_t), "t_us": hand_t, "data": hand_s}},
            {"kind": "imu", "alias": "Synth Forearm", "schema": "imu_sample_v2",
             "source": {"serial": "SYNTH-FORE"},
             "units": {"accel": "g", "gyro": "deg/s", "quat": "wxyz"},
             "samples": {"count": len(fore_t), "t_us": fore_t, "data": fore_s}},
        ],
        "analysis": {
            "phases": [{"phase": 5, "t_us": T0_US + int(IMPACT_S * 1e6), "conf": 1.0}],
            "bindings": [
                {"serial": "SYNTH-HAND", "role": 6, "alignA": identity, "mountM": identity},
                {"serial": "SYNTH-FORE", "role": 5, "alignA": identity, "mountM": identity},
            ],
        },
    }
    save_json(out / "swing.json", doc)
    save_json(out / "pose.json", {"frames": pose_frames(frame_t_us)})
    save_json(out / "truth.json", {
        "shaft": truth_frames,
        "events": {"takeaway_s": TAKEAWAY_S, "top_s": TOP_S,
                   "impact_s": IMPACT_S, "finish_s": FOLLOW_END_S,
                   "t0_us": T0_US},
        "expected": {"sHandSign": -1, "sHandDeltaDeg": 90.0,
                     "downswingSweepDeg": 140.0},
    })
    print(f"[synth] wrote {out} ({n_frames} frames, impact {IMPACT_S}s)")
    return out
