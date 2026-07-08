#!/usr/bin/env python3
"""Ball Detection v2 — C++ parity reference generator (V1).

Runs the python exemplar (ball_state_machine.BallTracker over the acceptance.py
pipeline) on a handful of golden corpus swings and dumps, per swing, everything
the C++ parity test (src/Pose/tests/ball_temporal_parity_test.cpp) needs to
reproduce the run bit-for-bit and diff the outputs:

  <out>/<tag>.json    fixture: video path, ROI (mp4 px), r_hat, fps, pad, noise0,
                      address_end_idx, bw/bh, source geometry, and the reference
                      outputs (lock idx/x/y/L0/medN, launch idx, position error).
  <out>/<tag>.B.f32   the response baseline B, raw float32, bh rows x bw cols,
                      row-major (the SAME B the C++ tracker loads — parity is
                      measured given the same baseline, bible §12).

MUST be generated on the SAME host that runs the C++ test (cross-host float
differs — the shaft-port lesson). The C++ side decodes the same clip and, on a
shared host toolchain, sees identical H.264 frames.

Usage:
  python gen_parity_ref.py --out DIR --swings 2026-07-04_.../swing_0001 ...
      [--root CORPUS] [--no-roi]
  # default 3 golden swings (healthy / weak-contrast / saturated) if --swings omitted
"""
import argparse, glob, json, os, sys
from collections import defaultdict

import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ball_state_machine import BallTracker, dog, robust_noise
from acceptance import read_faceon, read_ball, band, frame_index_for_us, SAT_THRESH, SAT_WARN

# Sensible defaults spanning the three lighting regimes (adjust to taste).
DEFAULT_SWINGS = [
    "2026-07-04_Mark-Liversedge_Wrist_01/swing_0001",   # healthy (ball pops)
    "2026-07-03_Mark-Liversedge_Wrist_01/swing_0002",   # weak contrast (pale mat)
    "2026-06-11_Mark-Liversedge_Wrist_01/swing_0001",   # saturated (clipped mat)
]


def session_roi(root, no_roi):
    """Per-session hitting-area ROI proxy from each session's labelled ball
    cluster — identical to acceptance.py's roi_for()."""
    if no_roi:
        return lambda sw: None
    sess_pts = defaultdict(list)
    for sw in sorted(glob.glob(os.path.join(root, "2026-*", "swing_*"))):
        b = read_ball(os.path.join(sw, "truth.json"))
        if not b:
            continue
        fo = read_faceon(os.path.join(sw, "swing.json"))
        if fo and fo["W"] and fo["H"]:
            sess_pts[os.path.dirname(sw)].append((b[0] / fo["W"], b[1] / fo["H"]))

    def roi_for(sw):
        pts = sess_pts.get(os.path.dirname(sw))
        if not pts:
            return None
        nx = float(np.median([p[0] for p in pts]))
        ny = float(np.median([p[1] for p in pts]))
        return (nx - 0.06, ny - 0.07, nx + 0.06, 1.0)
    return roi_for


def gen_swing(swing_dir, roi_norm, out_dir, tag):
    """Replicates acceptance.process_swing exactly, capturing the intermediates,
    then dumps the fixture + baseline. Returns a summary dict (or None on skip)."""
    fo = read_faceon(os.path.join(swing_dir, "swing.json"))
    ball = read_ball(os.path.join(swing_dir, "truth.json"))
    vp = os.path.join(swing_dir, fo["file"]) if fo else None
    if not fo or not vp or not os.path.exists(vp):
        return None

    cap = cv2.VideoCapture(vp)
    mp4w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    mp4h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    if roi_norm is not None:
        x0 = max(0, int(roi_norm[0] * mp4w)); x1 = min(mp4w, int(roi_norm[2] * mp4w))
        y0 = max(0, int(roi_norm[1] * mp4h)); y1 = min(mp4h, int(roi_norm[3] * mp4h))
    else:
        x0, x1, y0, y1 = band(mp4w, mp4h)
    r_hat = 9.5 * (mp4w / 1280.0)
    bw, bh = x1 - x0, y1 - y0

    pad = int(np.ceil(6.0 * r_hat))
    px0, py0 = max(0, x0 - pad), max(0, y0 - pad)
    px1, py1 = min(mp4w, x1 + pad), min(mp4h, y1 + pad)
    ox, oy = x0 - px0, y0 - py0

    resp, luma = [], []
    while True:
        ok, fr = cap.read()
        if not ok:
            break
        gpad = cv2.cvtColor(fr[py0:py1, px0:px1], cv2.COLOR_BGR2GRAY)
        Rpad = dog(gpad.astype(np.float32), r_hat)
        resp.append(Rpad[oy:oy + bh, ox:ox + bw])
        luma.append(gpad[oy:oy + bh, ox:ox + bw])
    cap.release()
    n = len(resp)
    if n < 60:
        return None

    fps = n / 5.0
    fi = frame_index_for_us(fo["t_us"], fo["impactUs"]) if fo["impactUs"] else int(0.7 * n)
    if fi < 0 or fi >= n:
        fi = int(0.7 * n)

    addr_idx = list(range(20, max(21, fi - 20), max(1, (fi - 40) // 8)))
    satFrac = float(np.mean([(luma[i] >= SAT_THRESH).mean() for i in addr_idx])) if addr_idx else 0.0

    tail_lo = min(fi + 45, n - 2)
    tail_idx = list(range(tail_lo, n - 1, max(1, (n - tail_lo) // 20)))
    if len(tail_idx) < 3:
        tail_idx = list(range(max(0, n - 20), n - 1))
    B = np.median(np.stack([resp[i] for i in tail_idx]), axis=0).astype(np.float32)
    noise0 = float(np.median([robust_noise(resp[i]) for i in addr_idx])) or 1.0

    address_end_idx = fi - max(3, int(round(0.15 * fps)))
    trk = BallTracker(r_hat, fps, B, noise0)
    trk.address_end_idx = address_end_idx
    for R in resp:
        trk.push(R)

    ref = {"false_launches": trk.false_launches}
    det_nx = det_ny = pos_err_px = None
    if trk.locked:
        det_nx = float((trk.locked["x"] + x0) / mp4w)
        det_ny = float((trk.locked["y"] + y0) / mp4h)
        ref.update({
            "lock_idx": trk.locked["idx"],
            "lock_x": float(trk.locked["x"]), "lock_y": float(trk.locked["y"]),
            "lock_ix": int(trk.locked["ix"]), "lock_iy": int(trk.locked["iy"]),
            "lock_L0": float(trk.locked["L0"]), "lock_medN": float(trk.locked["medN"]),
            "det_nx": det_nx, "det_ny": det_ny,
        })
        if ball:
            tnx, tny = ball[0] / fo["W"], ball[1] / fo["H"]
            pos_err_px = float(np.hypot((det_nx - tnx) * fo["W"], (det_ny - tny) * fo["H"]))
            ref["pos_err_px"] = pos_err_px
    if trk.launched:
        ref["launch_idx"] = int(trk.launched["idx"])

    # Dump the baseline B and the per-frame DoG response stack R (both row-major
    # float32) + the fixture json. The C++ parity test consumes python's OWN R —
    # NOT its own decode — because independent OpenCV versions decode this H.264
    # into different pixels (measured: cv2 4.13 vs system 4.10). That makes the
    # parity a byte-exact test of the TRACKER given identical R + B (the oracle
    # contract); the DoG+padding pipeline is verified separately, byte-for-byte,
    # by ball_temporal_test's paddedResponse-vs-recipe check.
    bfile = os.path.join(out_dir, f"{tag}.B.f32")
    B.tofile(bfile)
    rfile = os.path.join(out_dir, f"{tag}.R.f32")
    np.stack(resp).astype(np.float32).tofile(rfile)   # (n, bh, bw) contiguous
    fixture = {
        "swing": os.path.relpath(swing_dir),
        "video": os.path.abspath(vp),
        "mp4w": mp4w, "mp4h": mp4h,
        "roi": [x0, y0, x1, y1], "bw": bw, "bh": bh,
        "r_hat": r_hat, "fps": fps, "pad": pad,
        "noise0": noise0, "address_end_idx": address_end_idx,
        "n_frames": n, "fi": fi, "satFrac": satFrac,
        "source_w": fo["W"], "source_h": fo["H"],
        "truth_ball": list(ball) if ball else None,
        "baseline_file": os.path.basename(bfile),
        "response_file": os.path.basename(rfile),
        "ref": ref,
    }
    with open(os.path.join(out_dir, f"{tag}.json"), "w") as f:
        json.dump(fixture, f, indent=2)

    return {"tag": tag, "n": n, "satFrac": satFrac,
            "lock_idx": ref.get("lock_idx"), "launch_idx": ref.get("launch_idx"),
            "pos_err_px": pos_err_px}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("BALLLAB_ROOT", "/mnt/swingdata/Mark-Liversedge"))
    ap.add_argument("--out", default="/tmp/ball_parity")
    ap.add_argument("--swings", nargs="*", default=DEFAULT_SWINGS,
                    help="session_dir/swing_xxxx relative names (default: 3 golden swings)")
    ap.add_argument("--no-roi", action="store_true")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    roi_for = session_roi(a.root, a.no_roi)

    print(f"root={a.root}  out={a.out}  search={'loose band' if a.no_roi else 'per-session ROI'}\n")
    print(f"{'tag':28s} {'n':>4} {'sat':>5} {'lock':>5} {'launch':>6} {'posErr':>8}")
    print("-" * 62)
    ok = 0
    for rel in a.swings:
        swing_dir = os.path.join(a.root, rel)
        tag = rel.replace("/", "__")
        if not os.path.isdir(swing_dir):
            print(f"{tag:28s}  MISSING {swing_dir}"); continue
        r = gen_swing(swing_dir, roi_for(swing_dir), a.out, tag)
        if r is None:
            print(f"{tag:28s}  SKIP (no face-on / too few frames)"); continue
        ok += 1
        pe = f"{r['pos_err_px']:6.1f}px" if r['pos_err_px'] is not None else "   -   "
        print(f"{tag:28s} {r['n']:4d} {r['satFrac']*100:4.0f}% "
              f"{str(r['lock_idx']):>5} {str(r['launch_idx']):>6} {pe:>8}")
    print(f"\nwrote {ok}/{len(a.swings)} fixtures to {a.out}")


if __name__ == "__main__":
    main()
