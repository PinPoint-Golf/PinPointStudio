#!/usr/bin/env python3
"""dtl_synth3d_preview.py -- render the 3-D synthetic shaft's DTL projection over the DTL video
at the face-on P-instants, beside the measured track, so the preview can be looked at
(dtl_continuous_track_design_update.md §3.2; the --out run in dtl_continuous_20261002.md).

  dtl_synth3d_preview.py <swing_dir> <run_dir> --out preview.png [--extra-ms 0,...]

<run_dir> holds club_dtl.json (frames + synth3d) and result.json (the face-on ladder). One tile
per P1..P8 (and any --extra-ms instants), the DTL frame nearest that instant, cropped to a fixed
golfer box. Drawn: the MEASURED line (amber, RAY/BAND; grey when HELD), the SYNTHESISED line
(dashed cyan) and, top-left, the tier and the synth's plane. A banner states whether the camera
was calibrated -- on the corpus it is not, and the banner says PREVIEW.
"""
import argparse, json, math, os, sys
from pathlib import Path
import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from pp_swingdoc import load_swing  # noqa: E402

PLANE = {0: "no plane", 1: "address", 2: "backswing", 3: "downswing", 4: "downswing (held)"}
ANCHOR = {0: "-", 1: "tracker grip", 2: "skeleton hands"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("swing"); ap.add_argument("run"); ap.add_argument("--out", required=True)
    ap.add_argument("--extra-ms", default="")
    a = ap.parse_args()
    doc = load_swing(a.swing)
    dtl = next(s for s in doc["streams"] if s.get("kind") == "video" and s.get("setup", {}).get("perspective") == 1)
    ts = np.array(dtl["frames"]["t_us"], dtype=np.int64)
    c = json.load(open(os.path.join(a.run, "club_dtl.json")))
    W, H = c["frameWidth"], c["frameHeight"]
    res = json.load(open(os.path.join(a.run, "result.json")))["analysis"]
    pos = {p["p"]: p["t_us"] for p in res["club"]["positions"]}
    frames = {f["t_us"]: f for f in c["frames"]}
    synth = {s["t_us"]: s for s in c.get("synth3d", [])}
    summ = c["summary"].get("synth3d", {})
    instants = [(f"P{p}", pos[p]) for p in range(1, 9) if p in pos]
    for ms in [x for x in a.extra_ms.split(",") if x.strip()]:
        instants.append((f"t={ms}ms", int(float(ms) * 1000)))
    instants.sort(key=lambda x: x[1])
    cap = cv2.VideoCapture(os.path.join(a.swing, dtl["file"]))
    # crop box from the published grips: fixed for the swing
    gs = [f["grip"] for f in c["frames"] if f.get("grip")]
    gx = np.array([g[0] * W for g in gs]); gy = np.array([g[1] * H for g in gs])
    x0 = int(max(0, gx.min() - 0.45 * W)); x1 = int(min(W, gx.max() + 0.45 * W))
    y0 = int(max(0, gy.min() - 0.45 * H)); y1 = int(min(H, gy.max() + 0.35 * H))
    tiles = []
    for label, t in instants:
        i = int(np.argmin(np.abs(ts - t)))
        cap.set(cv2.CAP_PROP_POS_FRAMES, i)
        ok, img = cap.read()
        if not ok:
            continue
        tf = int(ts[i])
        f = frames.get(tf); s = synth.get(tf)
        def px(p):
            return (int(round(p[0] * W)), int(round(p[1] * H)))
        if f and f.get("grip") and f.get("head") and f["tier"] in ("RAY", "BAND", "SEG", "HELD"):
            col = (40, 190, 255) if f["tier"] != "HELD" else (170, 170, 170)
            cv2.line(img, px(f["grip"]), px(f["head"]), col, 3, cv2.LINE_AA)
        if s and s.get("grip") and s.get("head"):
            p0, p1 = np.array(px(s["grip"]), float), np.array(px(s["head"]), float)
            n = int(np.linalg.norm(p1 - p0) / 14) or 1
            for k in range(0, n, 2):
                q0 = p0 + (p1 - p0) * k / n; q1 = p0 + (p1 - p0) * min(k + 1, n) / n
                cv2.line(img, tuple(q0.astype(int)), tuple(q1.astype(int)), (255, 220, 60), 2, cv2.LINE_AA)
        crop = img[y0:y1, x0:x1].copy()
        tier = f["tier"] if f else "?"
        rho = f.get("rhoPred") if f else None
        cv2.putText(crop, f"{label}  {tier}" + (f"  rho {rho:.2f}" if isinstance(rho, (int, float)) else ""),
                    (8, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2, cv2.LINE_AA)
        if s:
            cv2.putText(crop, f"synth: {PLANE.get(s['plane'], '?')}, {ANCHOR.get(s['anchorSrc'], '?')}, rhoD {s['rhoD']:.2f}",
                        (8, 48), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 220, 60), 1, cv2.LINE_AA)
        else:
            cv2.putText(crop, "synth: none", (8, 48), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 220, 60), 1, cv2.LINE_AA)
        tiles.append(crop)
    cap.release()
    if not tiles:
        print("no tiles"); return 1
    h = max(t.shape[0] for t in tiles)
    row = np.concatenate([cv2.copyMakeBorder(t, 0, h - t.shape[0], 0, 4, cv2.BORDER_CONSTANT, value=(20, 20, 20)) for t in tiles], axis=1)
    banner = np.zeros((70, row.shape[1], 3), np.uint8)
    cam = summ.get("camera", {})
    text1 = ("PREVIEW - uncalibrated DTL camera (yaw/pitch/roll assumed 0): the synthesised heading is off by the unknown yaw"
             if summ.get("preview", True) else
             f"calibrated DTL camera: yaw {cam.get('yawDeg', 0):.1f} pitch {cam.get('pitchDeg', 0):.1f} roll {cam.get('rollDeg', 0):.1f}")
    cv2.putText(banner, text1, (10, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (80, 80, 255) if summ.get("preview", True) else (120, 255, 120), 2, cv2.LINE_AA)
    cv2.putText(banner, f"{os.path.basename(os.path.normpath(a.swing))}: solid amber = measured DTL track (grey = HELD), dashed cyan = 3-D synthetic shaft projected; "
                        f"{len(synth)} synth frames, read by no metric", (10, 56), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (230, 230, 230), 1, cv2.LINE_AA)
    out = np.concatenate([banner, row], axis=0)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    cv2.imwrite(a.out, out, [cv2.IMWRITE_PNG_COMPRESSION, 6])
    print("wrote", a.out, out.shape)
    return 0


if __name__ == "__main__":
    sys.exit(main())
