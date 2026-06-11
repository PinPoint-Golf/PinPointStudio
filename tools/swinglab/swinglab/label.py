# Minimal hand-labelling tool (Tier-3 truth): step frames, click grip then
# head; writes/updates truth.json next to swing.json. Needs a display and the
# full opencv-python (not headless).
#
#   keys: SPACE next labelled frame slot · a/d step one frame · u undo click
#         1..6 mark P-position at the current frame (1=ADR 2=TKW 3=TOP
#         4=IMP 5=REL 6=FIN) · q save+quit

import json
from pathlib import Path

import numpy as np

from . import Swing, save_json

EVENT_KEYS = {ord("1"): "address_s", ord("2"): "takeaway_s", ord("3"): "top_s",
              ord("4"): "impact_s", ord("5"): "release_s", ord("6"): "finish_s"}


def label(swing_dir, every_n=20):
    import cv2
    swing = Swing(swing_dir)
    video = swing.face_on()
    if not video:
        print("no video stream")
        return
    cap = cv2.VideoCapture(str(swing.path / video["file"]))
    t_us = video["frames"]["t_us"]
    t0 = t_us[0]

    truth_path = swing.path / "truth.json"
    truth = json.load(open(truth_path)) if truth_path.exists() else {}
    shaft = {f["t_us"]: f for f in truth.get("shaft", [])}
    events = truth.get("events", {})
    events["t0_us"] = t0

    clicks = []

    def on_mouse(ev, x, y, *_):
        if ev == cv2.EVENT_LBUTTONDOWN:
            clicks.append((x, y))

    cv2.namedWindow("label")
    cv2.setMouseCallback("label", on_mouse)

    idx = 0
    while True:
        cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
        ok, img = cap.read()
        if not ok:
            break
        view = img.copy()
        tu = t_us[idx]
        rec = shaft.get(tu)
        msg = f"frame {idx}/{len(t_us)-1}  t={(tu-t0)/1e6:.3f}s  clicks={len(clicks)}"
        if rec:
            cv2.line(view, tuple(map(int, rec["grip"])), tuple(map(int, rec["head"])),
                     (0, 255, 0), 2)
        for c in clicks:
            cv2.circle(view, c, 4, (0, 0, 255), -1)
        cv2.putText(view, msg, (8, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 1)
        cv2.imshow("label", view)
        k = cv2.waitKey(30) & 0xFF

        if len(clicks) == 2:
            import math
            g, h = clicks
            shaft[tu] = {"t_us": tu, "grip": list(map(float, g)),
                         "head": list(map(float, h)),
                         "theta": round(math.atan2(h[1] - g[1], h[0] - g[0]), 5),
                         "len": round(float(np.hypot(h[0] - g[0], h[1] - g[1])), 1)}
            clicks.clear()
            idx = min(idx + every_n, len(t_us) - 1)

        if k == ord("q"):
            break
        elif k == ord(" "):
            clicks.clear()
            idx = min(idx + every_n, len(t_us) - 1)
        elif k == ord("d"):
            idx = min(idx + 1, len(t_us) - 1)
        elif k == ord("a"):
            idx = max(idx - 1, 0)
        elif k == ord("u"):
            clicks and clicks.pop()
        elif k in EVENT_KEYS:
            events[EVENT_KEYS[k]] = round((tu - t0) / 1e6, 4)
            print(f"  {EVENT_KEYS[k]} = {(tu-t0)/1e6:.3f}s")

    cv2.destroyAllWindows()
    truth["shaft"] = sorted(shaft.values(), key=lambda f: f["t_us"])
    truth["events"] = events
    save_json(truth_path, truth)
    print(f"[label] {truth_path}: {len(shaft)} shaft frames, "
          f"{len([k for k in events if k.endswith('_s')])} events")
