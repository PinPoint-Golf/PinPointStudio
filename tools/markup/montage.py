#!/usr/bin/env python3
"""Tile frames from an annotated clip into montages. Usage:
   montage.py <annotated.mp4> <outPrefix> [frame indices...]  (no indices = 24 uniform)"""
import sys, cv2, numpy as np
vid, pref = sys.argv[1], sys.argv[2]
cap = cv2.VideoCapture(vid)
n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
picks = [int(x) for x in sys.argv[3:]] or [int(round(i * (n - 1) / 23)) for i in range(24)]
frames = {}
i = 0
while True:
    ok, f = cap.read()
    if not ok: break
    if i in picks: frames[i] = f
    i += 1
cap.release()
picks = [p for p in picks if p in frames]
sc = 0.45
h, w = frames[picks[0]].shape[:2]
tw, th = int(w * sc), int(h * sc)
per = 8; cols = 4
for m in range(0, len(picks), per):
    chunk = picks[m:m + per]
    rows = (len(chunk) + cols - 1) // cols
    img = np.zeros((rows * th, cols * tw, 3), np.uint8)
    for k, p in enumerate(chunk):
        r, c = divmod(k, cols)
        t = cv2.resize(frames[p], (tw, th))
        cv2.putText(t, f"f{p}", (8, 30), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)
        img[r * th:(r + 1) * th, c * tw:(c + 1) * tw] = t
    out = f"{pref}_{m // per}.png"
    cv2.imwrite(out, img)
    print(out, chunk)
