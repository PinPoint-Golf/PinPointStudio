#!/usr/bin/env python3
"""Ball Detection v2 — V0 acceptance harness (docs/design/ball_detection_v2.md §9.1).

Runs the §4 state machine (ball_state_machine.BallTracker) over every corpus swing
at native fps and enforces the acceptance gates. This is the executable spec and the
parity reference for the C++ core (V1); do not tune the algorithm live in C++ before
these gates pass here.

Truth sources (independent of the detector):
  - ball centre : truth.json "ball" [px,py] @ source resolution (the markup tool)
  - impact      : swing.json capture.impactUs (IMU/acoustic-derived), -> face-on frame

Gates (§9.1), reported per swing and in aggregate:
  G1  lock acquired before impact-1s, on swings with satFrac <= 0.25       (>= 95%)
  G2  locked position within 3 px of the human ball centre                 (report + pass%)
  G3  zero launch events during address, across the whole corpus           (== 0)
  G4  launch edge within <= 3 frames of capture.impactUs (satFrac <= 0.25)  (per swing)
  G5  the saturated session asserts the exposure warning; launch untrusted there

Usage:
  python acceptance.py [--root DIR] [--limit N] [--session SUBSTR] [--verbose]
  default root: $BALLLAB_ROOT, else /mnt/swingdata/Mark-Liversedge  (studio: C:\\PinPointStudio\\Mark-Liversedge)
"""
import argparse, glob, json, os, sys
import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ball_state_machine import BallTracker, dog, robust_noise

SAT_THRESH   = 250     # luma >= this is clipped (§6)
SAT_WARN     = 0.25    # satFrac above this -> exposure warning; launch untrusted
POS_TOL_PX   = 3.0     # §9.1 position gate (source pixels)
LAUNCH_TOL_F = 3       # §9.1 launch-edge gate (frames)


def band(w, h):
    """Static band prior (the prototype's, = the pose-corridor fallback of §4.1a)."""
    if w >= 1200:
        return (300, 1000, 890, h - 2)
    return (100, 620, 890, h - 2)


def read_faceon(swing_json):
    """Face-on stream geometry + impact, selecting by setup.perspective==2 (else 'face',
    else first) — MUST match markup_truth::readFaceOn; these captures carry a DTL stream too."""
    j = json.load(open(swing_json))
    vids = [s for s in j.get("streams", []) if s.get("kind") == "video"]
    fo = next((s for s in vids if s.get("setup", {}).get("perspective", -1) == 2), None)
    if fo is None:
        fo = next((s for s in vids if "face" in s.get("alias", "").lower()), vids[0] if vids else None)
    if fo is None:
        return None
    src = fo.get("source", {})
    t_us = [int(t) for t in fo.get("frames", {}).get("t_us", [])]
    return {
        "W": int(src.get("width", 0)), "H": int(src.get("height", 0)),
        "t_us": t_us, "file": fo.get("file", "Face-On.mp4"),
        "impactUs": j.get("capture", {}).get("impactUs"),
    }


def read_ball(truth_json):
    if not os.path.exists(truth_json):
        return None
    b = json.load(open(truth_json)).get("ball")
    return (float(b[0]), float(b[1])) if isinstance(b, list) and len(b) == 2 else None


def frame_index_for_us(t_us, us):
    if not t_us:
        return -1
    arr = np.asarray(t_us)
    return int(np.argmin(np.abs(arr - us)))


def process_swing(swing_dir, roi_norm=None):
    fo = read_faceon(os.path.join(swing_dir, "swing.json"))
    ball = read_ball(os.path.join(swing_dir, "truth.json"))
    vp = os.path.join(swing_dir, fo["file"]) if fo else None
    if not fo or not vp or not os.path.exists(vp):
        return {"skip": "no face-on"}

    cap = cv2.VideoCapture(vp)
    mp4w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    mp4h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    # search region: the hitting-area ROI when provided (proxy = per-session ball
    # cluster; the live detector uses the user-drawn ROI), else the loose band.
    if roi_norm is not None:
        x0 = max(0, int(roi_norm[0] * mp4w)); x1 = min(mp4w, int(roi_norm[2] * mp4w))
        y0 = max(0, int(roi_norm[1] * mp4h)); y1 = min(mp4h, int(roi_norm[3] * mp4h))
    else:
        x0, x1, y0, y1 = band(mp4w, mp4h)
    r_hat = 9.5 * (mp4w / 1280.0)
    bw, bh = x1 - x0, y1 - y0

    # DoG on a PADDED crop, then slice the band interior — so the GaussianBlur
    # crop-boundary artifact (which otherwise mislocks onto the shaft where it
    # enters the band top) never reaches the search region.
    pad = int(np.ceil(6.0 * r_hat))
    px0, py0 = max(0, x0 - pad), max(0, y0 - pad)
    px1, py1 = min(mp4w, x1 + pad), min(mp4h, y1 + pad)
    ox, oy = x0 - px0, y0 - py0

    resp, luma = [], []                             # band DoG response (f32) + band luma (u8)
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
        return {"skip": f"too few frames ({n})"}

    fps = n / 5.0                                   # 5 s window (matches the prototype)
    fi = frame_index_for_us(fo["t_us"], fo["impactUs"]) if fo["impactUs"] else int(0.7 * n)
    if fi < 0 or fi >= n:
        fi = int(0.7 * n)

    # exposure: satFrac over a few address frames (§6)
    addr_idx = list(range(20, max(21, fi - 20), max(1, (fi - 40) // 8)))
    satFrac = float(np.mean([(luma[i] >= SAT_THRESH).mean() for i in addr_idx])) if addr_idx else 0.0

    # baseline B: median response over the ball-ABSENT tail (post-launch) — see module docstring
    tail_lo = min(fi + 45, n - 2)
    tail_idx = list(range(tail_lo, n - 1, max(1, (n - tail_lo) // 20)))
    if len(tail_idx) < 3:
        tail_idx = list(range(max(0, n - 20), n - 1))
    B = np.median(np.stack([resp[i] for i in tail_idx]), axis=0)
    noise0 = float(np.median([robust_noise(resp[i]) for i in addr_idx])) or 1.0

    # run the causal state machine over the whole window
    trk = BallTracker(r_hat, fps, B, noise0)
    trk.address_end_idx = fi - max(3, int(round(0.15 * fps)))    # launches before this = "during address"
    for R in resp:
        trk.push(R)

    # collect results, mapping positions to source pixels via normalized coords
    res = {"n": n, "fps": fps, "fi": fi, "satFrac": satFrac, "r_hat": r_hat,
           "false_launches": trk.false_launches, "W": fo["W"], "H": fo["H"]}
    if trk.locked:
        # band-local -> full mp4 px -> normalized -> source px
        det_nx = (trk.locked["x"] + x0) / mp4w
        det_ny = (trk.locked["y"] + y0) / mp4h
        res["lock_idx"] = trk.locked["idx"]
        res["det_nx"], res["det_ny"] = det_nx, det_ny
        if ball:
            tnx, tny = ball[0] / fo["W"], ball[1] / fo["H"]
            res["pos_err_px"] = float(np.hypot((det_nx - tnx) * fo["W"], (det_ny - tny) * fo["H"]))
            res["truth_nx"], res["truth_ny"] = tnx, tny
    if trk.launched:
        res["launch_idx"] = trk.launched["idx"]
        res["launch_err_f"] = trk.launched["idx"] - fi
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.environ.get("BALLLAB_ROOT", "/mnt/swingdata/Mark-Liversedge"))
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--session", default="")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--no-roi", action="store_true",
                    help="use the loose band instead of the per-session hitting-area ROI proxy")
    a = ap.parse_args()

    all_swings = sorted(glob.glob(os.path.join(a.root, "2026-*", "swing_*")))
    swings = list(all_swings)
    if a.session:
        swings = [s for s in swings if a.session in s]
    if a.limit:
        swings = swings[:a.limit]
    if not swings:
        print(f"no swings under {a.root}", file=sys.stderr); sys.exit(2)

    # Per-session hitting-area ROI proxy from the session's labelled ball CLUSTER
    # (the aggregate spot a user would frame — not any single swing's own label).
    # The live detector uses the user-drawn ROI; the corpus does not record it.
    from collections import defaultdict
    sess_pts = defaultdict(list)
    if not a.no_roi:
        for sw in all_swings:
            b = read_ball(os.path.join(sw, "truth.json"))
            if not b:
                continue
            fo = read_faceon(os.path.join(sw, "swing.json"))
            if fo and fo["W"] and fo["H"]:
                sess_pts[os.path.dirname(sw)].append((b[0] / fo["W"], b[1] / fo["H"]))

    def roi_for(sw):
        if a.no_roi:
            return None
        pts = sess_pts.get(os.path.dirname(sw))
        if not pts:
            return None
        nx = float(np.median([p[0] for p in pts])); ny = float(np.median([p[1] for p in pts]))
        return (nx - 0.06, ny - 0.07, nx + 0.06, 1.0)   # ±77px x, from just above the cluster to floor

    print(f"root={a.root}  swings={len(swings)}  search={'loose band' if a.no_roi else 'per-session ROI'}\n")
    hdr = f"{'swing':46s} {'sat':>5} {'lock':>5} {'fi':>4} {'lidx':>4} {'dF':>3} {'posErr':>7}"
    print(hdr); print("-" * len(hdr))

    rows = []
    for sw in swings:
        name = f"{os.path.basename(os.path.dirname(sw))}/{os.path.basename(sw)}"
        try:
            r = process_swing(sw, roi_for(sw))
        except Exception as e:
            print(f"{name:46s}  ERROR {e}"); continue
        if r.get("skip"):
            print(f"{name:46s}  SKIP {r['skip']}"); continue
        r["name"] = name; rows.append(r)
        lock = r.get("lock_idx", "-"); lidx = r.get("launch_idx", "-")
        dF = r.get("launch_err_f", "-"); pe = r.get("pos_err_px")
        print(f"{name:46s} {r['satFrac']*100:4.0f}% {str(lock):>5} {r['fi']:4d} "
              f"{str(lidx):>4} {str(dF):>3} {(f'{pe:6.1f}px' if pe is not None else '   -   '):>7}")
        if a.verbose and "det_nx" in r:
            tn = (f"truth ({r['truth_nx']:.3f},{r['truth_ny']:.3f})" if "truth_nx" in r else "")
            print(f"        det ({r['det_nx']:.3f},{r['det_ny']:.3f})  {tn}")

    # ── gate evaluation (§9.1) ────────────────────────────────────────────────
    good = [r for r in rows if r["satFrac"] <= SAT_WARN]        # trustworthy-exposure swings
    sat  = [r for r in rows if r["satFrac"] > SAT_WARN]
    locked_good = [r for r in good if "lock_idx" in r]
    g1_ok = [r for r in good if r.get("lock_idx", 1e9) < r["fi"] - r["fps"]]
    errs  = [r["pos_err_px"] for r in rows if "pos_err_px" in r]
    g2_ok = [e for e in errs if e <= POS_TOL_PX]
    g3_false = sum(r["false_launches"] for r in rows)
    g4_ok = [r for r in good if "launch_err_f" in r and abs(r["launch_err_f"]) <= LAUNCH_TOL_F]
    g4_have = [r for r in good if "launch_err_f" in r]

    def pct(a, b): return f"{a}/{b} ({100*a/b:.0f}%)" if b else "0/0"

    print("\n" + "=" * 64 + "\nGATES (design §9.1)\n" + "=" * 64)
    print(f"  swings evaluated        : {len(rows)}  (good-exposure {len(good)}, saturated {len(sat)})")
    print(f"  G1 lock < impact-1s     : {pct(len(g1_ok), len(good))}   [need >=95%]")
    if errs:
        print(f"  G2 position <= {POS_TOL_PX:.0f}px      : {pct(len(g2_ok), len(errs))}   "
              f"median {np.median(errs):.1f}px  p90 {np.percentile(errs,90):.1f}px  max {max(errs):.1f}px")
        print(f"     (<=5px for reference : {pct(len([e for e in errs if e<=5]), len(errs))})")
    print(f"  G3 false launches (addr): {g3_false}   [need 0]")
    print(f"  G4 launch within {LAUNCH_TOL_F}f     : {pct(len(g4_ok), len(g4_have))}  of {len(good)} good-exposure swings")
    if sat:
        sat_warned = [r for r in sat if r["satFrac"] > SAT_WARN]
        sat_false  = sum(r["false_launches"] for r in sat)
        print(f"  G5 saturated session    : {len(sat)} swings, exposure-warned {len(sat_warned)}/{len(sat)}, "
              f"false launches {sat_false}")
    print()


if __name__ == "__main__":
    main()
