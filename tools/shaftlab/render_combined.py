#!/usr/bin/env python3
"""Combined shaft+head overlay: stage-1 line (grip->head, red=meas/cyan=pred by
stage-1 kind) + stage-2 head marker (o solid = head meas, o dashed = head pred,
x at frame edge = off). HUD: stage-1 theta/kind + stage-2 r/conf/kind.

Usage: render_combined.py <clip> <track_csv> <head_csv> <out_mp4>
"""
import csv
import math
import sys

import cv2
import numpy as np

clip, track_csv, head_csv, out_mp4 = sys.argv[1:5]
track = {int(r["frame"]): r for r in csv.DictReader(open(track_csv))}
head = {int(r["frame"]): r for r in csv.DictReader(open(head_csv))}

cap = cv2.VideoCapture(clip)
W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
vw = cv2.VideoWriter(out_mp4, cv2.VideoWriter_fourcc(*"mp4v"), fps, (W, H))
fi = 0
while True:
    ok, frame = cap.read()
    if not ok:
        break
    t = track.get(fi)
    h = head.get(fi)
    if t is not None and t["theta_out"] not in ("", "nan"):
        gx, gy = float(t["grip_x"]), float(t["grip_y"])
        th = math.radians(float(t["theta_out"]))
        col = (0, 0, 255) if t["kind"] == "meas" else (255, 220, 0)
        # shaft line to the stage-2 head when available, else to L_pred/r_edge
        r_end = None
        if h is not None:
            for key in ("r_h", "L_pred", "r_edge"):
                if h.get(key):
                    r_end = float(h[key])
                    break
        if r_end is None:
            r_end = 0.45 * H
        ex = (int(gx + r_end * math.cos(th)), int(gy + r_end * math.sin(th)))
        cv2.line(frame, (int(gx), int(gy)), ex, col,
                 2 if t["kind"] == "meas" else 1, cv2.LINE_AA)
        cv2.circle(frame, (int(gx), int(gy)), 4, (255, 0, 0), -1)
        if h is not None:
            kh = h["kind_h"]
            if kh == "meas" and h["head_x"]:
                c = (int(float(h["head_x"])), int(float(h["head_y"])))
                cv2.circle(frame, c, 10, (0, 0, 255), 2, cv2.LINE_AA)
            elif kh == "pred" and h["head_x"]:
                c = (int(float(h["head_x"])), int(float(h["head_y"])))
                for a in range(0, 360, 45):
                    cv2.ellipse(frame, c, (10, 10), 0, a, a + 22, (255, 220, 0), 2, cv2.LINE_AA)
            elif kh == "off":
                re = float(h["r_edge"])
                exo = (int(gx + re * math.cos(th)), int(gy + re * math.sin(th)))
                cv2.line(frame, (exo[0] - 9, exo[1] - 9), (exo[0] + 9, exo[1] + 9), (0, 165, 255), 2)
                cv2.line(frame, (exo[0] - 9, exo[1] + 9), (exo[0] + 9, exo[1] - 9), (0, 165, 255), 2)
            hud2 = f"head: r={h['r_h'] or '-'} conf={h['conf_h']} {kh.upper()}"
        else:
            hud2 = "head: -"
        cv2.putText(frame, f"f{fi} shaft: {math.degrees(th):6.1f}deg {t['kind'].upper()}",
                    (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 1, cv2.LINE_AA)
        cv2.putText(frame, hud2, (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                    (255, 255, 255), 1, cv2.LINE_AA)
    vw.write(frame)
    fi += 1
vw.release()
cap.release()
print(f"[out] {out_mp4} ({fi} frames)")
