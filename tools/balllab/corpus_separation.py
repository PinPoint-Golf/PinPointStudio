#!/usr/bin/env python3
"""Ball Detection v2 corpus evidence (docs/design/ball_detection_v2.md §2).

Per swing: band-pass every sampled frame at ball scale (DoG), take median response
maps over ball-present and ball-absent frame sets, self-locate the ball as
argmax(medP - |medA|)  (present-positive AND absent-quiet — static distractors and
stance-shadow deltas cancel), then measure the per-frame response AT that locked
spot for present vs absent frames. Prints per-swing separation + located position.

Result on the 2026-06/07 corpus: 43/44 swings separate cleanly (pres10 > abs90),
including the fully-saturated 06-11 session. See the design doc before changing
anything here — this script is the executable form of the §2 evidence table."""
import cv2, json, os, glob
import numpy as np

ROOT = "/mnt/swingdata/Mark-Liversedge"

def dog(gray32, r):
    s1 = max(1.0, r / 1.6)
    return cv2.GaussianBlur(gray32, (0, 0), s1) - cv2.GaussianBlur(gray32, (0, 0), s1 * 3.2)

def band(w, h):
    if w >= 1200: return (300, 1000, 890, h - 2)
    return (100, 620, 890, h - 2)

def grab(cap, idxs):
    fs = []
    for i in idxs:
        cap.set(cv2.CAP_PROP_POS_FRAMES, i)
        ok, fr = cap.read()
        if ok: fs.append(cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY).astype(np.float32))
    return fs

rows = []
for sess in sorted(os.listdir(ROOT)):
    sd = os.path.join(ROOT, sess)
    if not os.path.isdir(sd): continue
    for swd in sorted(glob.glob(os.path.join(sd, "swing_*"))):
        name = f"{sess}/{os.path.basename(swd)}"
        vp = os.path.join(swd, "Face-On.mp4")
        if not os.path.exists(vp): continue
        sj = json.load(open(os.path.join(swd, "swing.json")))
        imp_us = sj.get("capture", {}).get("impactUs") or 3500000
        cap = cv2.VideoCapture(vp)
        n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = n / 5.0
        fi = int(imp_us * 1e-6 * fps)
        r_ball = 9.5 * (w / 1280.0)
        x0, x1, y0, y1 = band(w, h)

        pres_f = grab(cap, range(20, max(21, fi - 60), max(1, (fi - 80) // 14)))
        abs_f = grab(cap, range(min(fi + 45, n - 2), n - 1, max(1, (n - fi - 46) // 10)))
        cap.release()
        if len(pres_f) < 5 or len(abs_f) < 3: continue

        medP = np.median(np.stack([dog(f[y0:y1, x0:x1], r_ball) for f in pres_f]), axis=0)
        medA = np.median(np.stack([dog(f[y0:y1, x0:x1], r_ball) for f in abs_f]), axis=0)
        d = medP - np.abs(medA)              # present-positive AND absent-quiet
        resp = d
        _, mx, _, loc = cv2.minMaxLoc(resp)
        bx, by = loc[0] + x0, loc[1] + y0
        # second-best peak beyond 3r for distractor margin
        r3 = int(3 * r_ball)
        resp2 = resp.copy()
        cv2.circle(resp2, loc, r3, 0, -1)
        _, mx2, _, _ = cv2.minMaxLoc(resp2)

        # per-frame response at locked spot (3x3 max around it)
        def at_spot(f):
            rr = dog(f[y0:y1, x0:x1], r_ball)
            yy, xx = loc[1], loc[0]
            return float(rr[max(0, yy - 2):yy + 3, max(0, xx - 2):xx + 3].max())
        pv = np.array([at_spot(f) for f in pres_f])
        av = np.array([at_spot(f) for f in abs_f])
        p10 = np.percentile(pv, 10); a90 = np.percentile(av, 90)
        ok = p10 > a90 + 0.15 * max(1.0, abs(p10))
        rows.append((name, mx, mx / max(mx2, 1e-3), float(np.median(pv)), p10,
                     float(np.median(av)), a90, bx, by, ok))

print(f"{'swing':52s} {'dPeak':>6} {'marg':>5} {'presMed':>8} {'pres10':>7} {'absMed':>7} {'abs90':>6} {'bx':>5} {'by':>5}")
nok = 0
for nm, mx, marg, pm, p10, am, a90, bx, by, ok in rows:
    nok += ok
    print(f"{nm:52s} {mx:6.1f} {marg:5.2f} {pm:8.1f} {p10:7.1f} {am:7.1f} {a90:6.1f} {bx:5.0f} {by:5.0f}  {'OK' if ok else 'BAD'}")
print(f"\n{nok}/{len(rows)} swings separate cleanly at the locked spot")
