#!/usr/bin/env python3
"""dtl_yaw_probe.py -- can the alignment stick on the mat tell us where the down-the-line
camera is pointing?

The fused shaft plane's HEADING (swing direction, and so club path) moves one for one with
the DTL camera's yaw (shaft_fusion_design.md s2). A stick laid along the target line is a
ground line parallel to the camera's intended axis: under a pinhole camera every such line
runs to one vanishing point, and that point's column is

    x_vp = cx + f * tan(yaw)          (yaw + = the view ray turned toward the golfer)

ONE stick gives ONE line, so the vanishing point is only known to lie ON it. What saves the
estimate is that the stick images almost vertical: extended to ANY plausible horizon row it
lands in a few-pixel column range. What does not get saved is f and cx, which this footage
does not record -- so the probe reports x_vp as measured and yaw as a TABLE over the focal
lengths the scene's own scale allows:  f = D / (cm per px),  D = camera-to-golfer distance.

    dtl_yaw_probe.py <session-dir> [--cm-per-px 0.27] [--save DIR]

Reads DTL.mp4 of each swing, takes a median of the first frames (the address hold), finds
the stick as the longest thin bright near-vertical line on the mat below the hands.
"""
import argparse, math, os, sys
import cv2
import numpy as np


def address_plate(path, n=20):
    cap = cv2.VideoCapture(path)
    fr = []
    while len(fr) < n:
        ok, f = cap.read()
        if not ok: break
        fr.append(cv2.cvtColor(f, cv2.COLOR_BGR2GRAY))
    cap.release()
    return np.median(np.stack(fr), axis=0).astype(np.uint8) if fr else None


def find_stick(g):
    H, W = g.shape
    y0 = int(0.70 * H)                                  # the mat, below the hands
    roi = g[y0:, :]
    # thin bright ridge: the image minus a horizontally blurred copy
    ridge = cv2.subtract(roi, cv2.blur(roi, (15, 1)))
    _, bw = cv2.threshold(ridge, 25, 255, cv2.THRESH_BINARY)
    lines = cv2.HoughLinesP(bw, 1, np.pi / 720, threshold=40, minLineLength=int(0.08 * H), maxLineGap=12)
    best = None
    if lines is None: return None
    for x1, y1, x2, y2 in lines[:, 0]:
        ang = math.degrees(math.atan2(abs(x2 - x1), abs(y2 - y1)))   # from vertical
        L = math.hypot(x2 - x1, y2 - y1)
        if ang < 25 and (best is None or L > best[0]):
            best = (L, x1, y1 + y0, x2, y2 + y0)
    if best is None: return None
    _, x1, y1, x2, y2 = best
    # refine: sub-pixel ridge centroid per row between the endpoints, then a line fit x = a + b*y
    ys, xs = [], []
    for y in range(min(y1, y2), max(y1, y2) + 1):
        xc = x1 + (x2 - x1) * (y - y1) / float((y2 - y1) or 1)
        lo, hi = int(max(xc - 6, 0)), int(min(xc + 7, W))
        row = g[y, lo:hi].astype(float); row = row - row.min()
        if row.sum() <= 0: continue
        xs.append(lo + float((row * np.arange(len(row))).sum() / row.sum())); ys.append(y)
    if len(ys) < 20: return None
    b, a = np.polyfit(ys, xs, 1)
    resid = float(np.std(np.array(xs) - (a + b * np.array(ys))))
    return dict(a=a, b=b, y_lo=min(ys), y_hi=max(ys), resid=resid, n=len(ys))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("session"); ap.add_argument("--cm-per-px", type=float, default=0.27); ap.add_argument("--save")
    a = ap.parse_args()
    rows = []
    for sw in sorted(os.listdir(a.session)):
        v = os.path.join(a.session, sw, "DTL.mp4")
        if not os.path.isfile(v): continue
        g = address_plate(v)
        if g is None: continue
        H, W = g.shape
        s = find_stick(g)
        if not s:
            print("%s  no stick found" % sw); continue
        xs = [s["a"] + s["b"] * (f * H) for f in (0.30, 0.40, 0.50, 0.60)]
        rows.append((sw, s, xs, W, H))
        print("%s  stick rows %d-%d  tilt from vertical %+.2f deg  resid %.2f px  x at horizon rows 30/40/50/60%% H: %s"
              % (sw, s["y_lo"], s["y_hi"], math.degrees(math.atan(s["b"])) * -1, s["resid"], " ".join("%.0f" % x for x in xs)))
        if a.save:
            os.makedirs(a.save, exist_ok=True)
            c = cv2.cvtColor(g, cv2.COLOR_GRAY2BGR)
            cv2.line(c, (int(s["a"] + s["b"] * H), H), (int(s["a"] + s["b"] * 0.3 * H), int(0.3 * H)), (0, 0, 255), 1)
            cv2.line(c, (W // 2, 0), (W // 2, H), (255, 128, 0), 1)
            cv2.imwrite(os.path.join(a.save, sw + "_stick.png"), c)
    if not rows: return 1
    W = rows[0][3]
    # A stick imaged far from vertical is a different camera placement (07-04 s1-2: the camera was
    # moved before s4) and its vanishing column depends heavily on the unknown horizon row. The
    # summary is over the near-vertical group; the others are listed above and left out here.
    near = [r for r in rows if abs(math.degrees(math.atan(r[1]["b"]))) < 5.0]
    if len(near) < len(rows):
        print("\n%d swing(s) with the stick > 5 deg off vertical left out of the summary: %s"
              % (len(rows) - len(near), " ".join(r[0] for r in rows if r not in near)))
    rows = near or rows
    allx = np.array([r[2] for r in rows])
    lo, hi, med = allx.min(), allx.max(), np.median(allx)
    print("\nvanishing column x_vp: median %.0f  range %.0f-%.0f over horizon rows 30-60%% of the frame, %d swings (frame centre %d)"
          % (med, lo, hi, len(rows), W // 2))
    print("yaw = atan((x_vp - cx)/f), cx = frame centre (NOT recorded: an ROI offset moves it), f = D / %.3f cm/px:" % a.cm_per_px)
    print("  D (m)    f (px)   yaw at x_vp lo / median / hi")
    for D in (2.0, 2.5, 3.0, 3.5, 4.0):
        f = D * 100 / a.cm_per_px
        print("  %.1f     %6.0f   %5.1f / %5.1f / %5.1f deg" % (D, f, *(math.degrees(math.atan((x - W / 2) / f)) for x in (lo, med, hi))))


if __name__ == "__main__":
    sys.exit(main())
