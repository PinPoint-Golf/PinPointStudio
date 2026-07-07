#!/usr/bin/env python3
"""Why don't the weak-contrast (07-03) swings lock? Report, at the human ball spot,
the novelty N the tracker would see during address vs k_appear=5, whether is_blob
passes there, and what the global top peak is instead."""
import sys, os, json, glob
import numpy as np, cv2
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ball_state_machine import dog, robust_noise, is_blob, _at_spot, K_APPEAR

ROOT = os.environ.get("BALLLAB_ROOT", "/mnt/swingdata/Mark-Liversedge")
PICKS = ["2026-07-03_Mark-Liversedge_Wrist_01/swing_0002",
         "2026-07-04_Mark-Liversedge_Wrist_01/swing_0001"]   # weak vs healthy control

def band(w, h):
    return (300, 1000, 890, h - 2) if w >= 1200 else (100, 620, 890, h - 2)

for sw in PICKS:
    d = os.path.join(ROOT, sw)
    j = json.load(open(os.path.join(d, "swing.json")))
    vids = [s for s in j["streams"] if s.get("kind") == "video"]
    fojson = next(s for s in vids if s.get("setup", {}).get("perspective", -1) == 2)
    W, H = fojson["source"]["width"], fojson["source"]["height"]
    t_us = [int(t) for t in fojson["frames"]["t_us"]]
    impactUs = j["capture"]["impactUs"]
    ball = json.load(open(os.path.join(d, "truth.json")))["ball"]
    tnx, tny = ball[0] / W, ball[1] / H

    cap = cv2.VideoCapture(os.path.join(d, "Face-On.mp4"))
    mw = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); mh = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    x0, x1, y0, y1 = band(mw, mh); bw, bh = x1 - x0, y1 - y0
    r_hat = 9.5 * (mw / 1280.0)
    pad = int(np.ceil(6 * r_hat))
    px0, py0 = max(0, x0 - pad), max(0, y0 - pad)
    px1, py1 = min(mw, x1 + pad), min(mh, y1 + pad)
    ox, oy = x0 - px0, y0 - py0
    resp = []
    while True:
        ok, fr = cap.read()
        if not ok: break
        g = cv2.cvtColor(fr[py0:py1, px0:px1], cv2.COLOR_BGR2GRAY).astype(np.float32)
        R = dog(g, r_hat)
        resp.append(R[oy:oy + bh, ox:ox + bw])
    cap.release()
    n = len(resp); fps = n / 5.0
    fi = int(np.argmin(np.abs(np.asarray(t_us) - impactUs)))
    B = np.median(np.stack([resp[i] for i in range(min(fi + 45, n - 2), n - 1,
                                                    max(1, (n - min(fi + 45, n - 2)) // 20))]), axis=0)
    # ball spot in band-local coords
    bx = int(round(tnx * mw)) - x0
    by = int(round(tny * mh)) - y0
    nballs, blobs, topN, topblob = [], [], [], []
    for i in range(20, fi - 30, max(1, (fi - 50) // 25)):
        R = resp[i]; noise = robust_noise(R) or 1.0
        N = (R - B) / noise
        nballs.append(_at_spot(N, bx, by))
        blobs.append(is_blob(R, bx, by, r_hat))
        cy, cx = np.unravel_index(int(np.argmax(N)), N.shape)
        topN.append(float(N[cy, cx])); topblob.append(is_blob(R, int(cx), int(cy), r_hat))
    print(f"\n{sw}  fi={fi} fps={fps:.0f}  ball band-local=({bx},{by})")
    print(f"  N at ball spot  : median {np.median(nballs):5.1f}  max {np.max(nballs):5.1f}  "
          f">=k_appear({K_APPEAR:.0f}) in {int(np.mean([v>=K_APPEAR for v in nballs])*100)}% of address frames")
    print(f"  is_blob at ball : {int(np.mean(blobs)*100)}% of address frames")
    print(f"  global top peak : median N {np.median(topN):5.1f}  blob-rate {int(np.mean(topblob)*100)}%")
