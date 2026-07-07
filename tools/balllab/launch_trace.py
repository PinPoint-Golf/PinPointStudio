#!/usr/bin/env python3
"""Ball Detection v2 launch-edge evidence (docs/design/ball_detection_v2.md §2, §4.5).

Traces the at-spot DoG response across the whole video for selected swings:
address stability (waggle/occlusion dips) vs launch collapse sharpness.

Result on the corpus: response steady (never < ~85% of median) through the full
~3.4 s address, collapses to < 10% within 2 frames (~13 ms at 149 fps) at impact
in healthy-exposure sessions; intermittently 0 during address in the fully
saturated 06-11 session (the §6 exposure-QA rationale)."""
import cv2, json, os
import numpy as np

ROOT = "/mnt/swingdata/Mark-Liversedge"
picks = [
    ("2026-07-04_Mark-Liversedge_Wrist_01/swing_0012", (677, 994)),
    ("2026-07-05_Mark-Liversedge_Wrist_02/swing_0006", (667, 992)),
    ("2026-06-11_Mark-Liversedge_Wrist_01/swing_0009", (414, 958)),
    ("2026-07-03_Mark-Liversedge_Wrist_01/swing_0002", (653, 945)),  # the BAD one
]

def dog(gray32, r):
    s1 = max(1.0, r / 1.6)
    return cv2.GaussianBlur(gray32, (0, 0), s1) - cv2.GaussianBlur(gray32, (0, 0), s1 * 3.2)

for sw, (bx, by) in picks:
    vp = os.path.join(ROOT, sw, "Face-On.mp4")
    sj = json.load(open(os.path.join(ROOT, sw, "swing.json")))
    imp_us = sj.get("capture", {}).get("impactUs") or 3500000
    cap = cv2.VideoCapture(vp)
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    fps = n / 5.0
    fi = int(imp_us * 1e-6 * fps)
    r_ball = 9.5 * (w / 1280.0)
    x0, x1 = max(0, bx - 40), bx + 40
    y0, y1 = max(0, by - 40), by + 40
    vals = []
    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
    for i in range(n):
        ok, fr = cap.read()
        if not ok: break
        g = cv2.cvtColor(fr[y0:y1, x0:x1], cv2.COLOR_BGR2GRAY).astype(np.float32)
        rr = dog(g, r_ball)
        cy, cx = by - y0, bx - x0
        vals.append(float(rr[cy - 2:cy + 3, cx - 2:cx + 3].max()))
    cap.release()
    v = np.array(vals)
    pre = v[20:fi - 5]
    print(f"\n{sw}  impactFrame~{fi} fps={fps:.0f}")
    print(f"  address window [20,{fi-5}]: median {np.median(pre):.1f}  min {pre.min():.1f} "
          f"(minFrame {20+int(pre.argmin())})  p5 {np.percentile(pre,5):.1f}")
    lo = np.median(pre) * 0.4
    below = np.where(v[:fi + 30] < lo)[0]
    below = below[below > 30]
    print(f"  first sustained drop below 40%med: ", end="")
    run = None
    for idx in below:
        if idx + 2 < len(v) and v[idx + 1] < lo and v[idx + 2] < lo:
            run = idx; break
    print(f"frame {run} (impact {fi})" if run is not None else "none before impact+30")
    a = v[fi - 8:fi + 12]
    print("  around impact:", " ".join(f"{x:5.0f}" for x in a))
