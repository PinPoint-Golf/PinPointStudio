#!/usr/bin/env python3
"""pose_diff — is a re-posed swing the same pose? The keypoint gate of
docs/design/pose_inference_performance_plan.md §1.

Every step of the pose-performance plan (session cache, producer pool, batching, fp16,
TensorRT/CoreML options, frame budget) must leave the 2-D keypoints where today's path
put them. This compares two pose tracks of the SAME swing:

  * two swing documents (a swing dir, swing.json or swing.ppsw — read through
    tools/pp_swingdoc.py): analysis.pose2d.frames (face-on) and analysis.poseDtl.frames
    (down-the-line), each frame {t_us, kp:[x,y,c]×133 normalised to the full frame};
  * or two pinned pose JSONs ({"frames":[{t_us, kp}]}, the PoseRunner::loadFromJson shape).
    These carry no frame size: pass --size WxH (default 688x1024, the face-on camera).

Frames are paired by t_us (nearest, |Δt| ≤ 1 ms). For each body joint — COCO 0–16 and
the feet 17–22 — the pixel distance at the recorded frame size (the stream's
source.width/height in a document) gives a median and a p95. The GATE (plan §1):
over all paired body-joint samples, median ≤ 1 px AND p95 ≤ 3 px, AND at least 95 % of
the reference frames paired (a step that drops frames is not "the same pose").
A joint is counted in a frame only when BOTH tracks score it ≥ --min-conf (default
0.3, the runner's wrist gate) — a joint neither run could see is not a disagreement.
Face (23–90) and hands (91–132) are reported on a separate line and NEVER gate: the
hands are a cross-check only (memory: pose is 133-pt WholeBody; hands unreliable).

Usage:
    python3 tools/pose/pose_diff.py REF CAND                 # both cameras when both have them
    python3 tools/pose/pose_diff.py REF CAND --camera faceOn # or dtl
    python3 tools/pose/pose_diff.py ref_pose.json cand_pose.json --size 576x988
    python3 tools/pose/pose_diff.py REF CAND --json out.json # machine-readable result too

REF is today's path (the baseline); CAND the change under test.
Exit status: 0 = every compared camera passes the gate, 1 = any fails, 2 = usage/IO error.
numpy only (plus pp_swingdoc's libppswing reader for .ppsw documents).
"""
import argparse
import json
import os
import sys

import numpy as np

_TOOLS = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

N_KP = 133
BODY = list(range(0, 23))          # COCO body 0–16 + feet 17–22 — these gate
NONGATE = list(range(23, N_KP))    # face 23–90 + hands 91–132 — reported, never gate
NAMES = ["nose", "l_eye", "r_eye", "l_ear", "r_ear", "l_shoulder", "r_shoulder",
         "l_elbow", "r_elbow", "l_wrist", "r_wrist", "l_hip", "r_hip", "l_knee", "r_knee",
         "l_ankle", "r_ankle", "l_bigtoe", "l_smalltoe", "l_heel", "r_bigtoe", "r_smalltoe",
         "r_heel"]

GATE_MEDIAN_PX = 1.0
GATE_P95_PX = 3.0
GATE_PAIRED = 0.95
PAIR_TOL_US = 1000

CAMERAS = {"faceOn": ("pose2d", "FaceOn"), "dtl": ("poseDtl", "DownTheLine")}


def _parse_size(s):
    w, h = s.lower().split("x")
    return int(w), int(h)


def _frames_to_arrays(frames):
    """(t_us[N], kp[N,133,3]) from a list of {t_us, kp:[x,y,c]*133} frames."""
    t = np.array([int(round(float(f["t_us"]))) for f in frames], dtype=np.int64)
    kp = np.full((len(frames), N_KP, 3), np.nan)
    for i, f in enumerate(frames):
        a = np.asarray(f.get("kp", []), dtype=float)
        n = min(len(a) // 3, N_KP)
        kp[i, :n, :] = a[: n * 3].reshape(n, 3)
    order = np.argsort(t, kind="stable")
    return t[order], kp[order]


def _load(path):
    """{camera: (t, kp, (w,h) or None)} from a swing document or a pinned pose JSON."""
    if path.endswith(".json") and os.path.isfile(path):
        with open(path) as fh:
            doc = json.load(fh)
        if "frames" in doc:                     # pinned pose JSON — one camera, no size
            t, kp = _frames_to_arrays(doc["frames"])
            return {"pinned": (t, kp, None)}
    from pp_swingdoc import load_swing          # swing dir / swing.json / swing.ppsw
    doc = load_swing(path)
    an = doc.get("analysis", {})
    sizes = {}
    for s in doc.get("streams", []):
        name = (s.get("setup") or {}).get("perspectiveName")
        dims = s.get("source") or s.get("encoded") or {}
        if name and dims.get("width") and dims.get("height"):
            sizes[name] = (int(dims["width"]), int(dims["height"]))
    out = {}
    for cam, (key, persp) in CAMERAS.items():
        frames = (an.get(key) or {}).get("frames") or []
        if frames:
            t, kp = _frames_to_arrays(frames)
            out[cam] = (t, kp, sizes.get(persp))
    return out


def _pair(t_ref, t_cand):
    """Index pairs (i_ref, i_cand), nearest candidate within PAIR_TOL_US, one-to-one."""
    if len(t_ref) == 0 or len(t_cand) == 0:
        return np.array([], int), np.array([], int)
    j = np.searchsorted(t_cand, t_ref)
    j0 = np.clip(j - 1, 0, len(t_cand) - 1)
    j1 = np.clip(j, 0, len(t_cand) - 1)
    pick = np.where(np.abs(t_cand[j0] - t_ref) <= np.abs(t_cand[j1] - t_ref), j0, j1)
    ok = np.abs(t_cand[pick] - t_ref) <= PAIR_TOL_US
    ir, ic = np.nonzero(ok)[0], pick[ok]
    _, first = np.unique(ic, return_index=True)   # one-to-one: keep the first claimant
    return ir[first], ic[first]


def _stats(err):
    err = err[np.isfinite(err)]
    if err.size == 0:
        return {"n": 0, "median": None, "p95": None, "max": None}
    return {"n": int(err.size), "median": float(np.median(err)),
            "p95": float(np.percentile(err, 95)), "max": float(err.max())}


def compare(ref, cand, size, min_conf):
    t_r, kp_r, _ = ref
    t_c, kp_c, _ = cand
    w, h = size
    ir, ic = _pair(t_r, t_c)
    a, b = kp_r[ir], kp_c[ic]
    seen = (a[:, :, 2] >= min_conf) & (b[:, :, 2] >= min_conf)
    d = np.hypot((a[:, :, 0] - b[:, :, 0]) * w, (a[:, :, 1] - b[:, :, 1]) * h)
    d = np.where(seen, d, np.nan)
    res = {
        "size": [w, h],
        "refFrames": int(len(t_r)), "candFrames": int(len(t_c)), "paired": int(len(ir)),
        "pairedFraction": float(len(ir) / len(t_r)) if len(t_r) else 0.0,
        "joints": {NAMES[j]: _stats(d[:, j]) for j in BODY},
        "body": _stats(d[:, BODY].ravel()),
        "face": _stats(d[:, 23:91].ravel()),
        "hands": _stats(d[:, 91:].ravel()),
    }
    body = res["body"]
    res["pass"] = bool(body["n"] > 0 and body["median"] <= GATE_MEDIAN_PX
                       and body["p95"] <= GATE_P95_PX and res["pairedFraction"] >= GATE_PAIRED)
    return res


def _fmt(v):
    return "   -  " if v is None else f"{v:6.2f}"


def report(cam, r):
    w, h = r["size"]
    print(f"== {cam}: {r['paired']}/{r['refFrames']} reference frames paired "
          f"({100 * r['pairedFraction']:.1f} %), candidate has {r['candFrames']}; px at {w}x{h}")
    print(f"   {'joint':<15} {'n':>6} {'median':>7} {'p95':>7} {'max':>7}")
    rows = list(r["joints"].items()) + [("BODY 0-22", r["body"]), ("face (no gate)", r["face"]),
                                        ("hands (no gate)", r["hands"])]
    for name, s in rows:
        print(f"   {name:<15} {s['n']:>6} {_fmt(s['median'])} {_fmt(s['p95'])} {_fmt(s['max'])}")
    b = r["body"]
    verdict = "PASS" if r["pass"] else "FAIL"
    med = "-" if b["median"] is None else f"{b['median']:.2f}"
    p95 = "-" if b["p95"] is None else f"{b['p95']:.2f}"
    print(f"   {verdict}: body median {med} px (gate ≤ {GATE_MEDIAN_PX:g}), p95 {p95} px "
          f"(gate ≤ {GATE_P95_PX:g}), paired {100 * r['pairedFraction']:.1f} % "
          f"(gate ≥ {100 * GATE_PAIRED:g} %)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref", help="baseline: swing dir / swing.json / swing.ppsw / pinned pose JSON")
    ap.add_argument("cand", help="candidate, same forms")
    ap.add_argument("--camera", choices=["faceOn", "dtl", "both"], default="both")
    ap.add_argument("--size", help="WxH for pinned pose JSONs (or to override a document's)")
    ap.add_argument("--min-conf", type=float, default=0.3)
    ap.add_argument("--json", help="also write the result object here")
    args = ap.parse_args(argv)

    try:
        ref, cand = _load(args.ref), _load(args.cand)
    except Exception as e:  # noqa: BLE001 — say what could not be read
        print(f"pose_diff: cannot read input: {e}", file=sys.stderr)
        return 2

    if "pinned" in ref or "pinned" in cand:
        # A pinned file is one camera; pair it with the same-named camera of a document
        # when --camera names one, else with whatever the other side holds alone.
        cam = args.camera if args.camera != "both" else "faceOn"
        r = ref.get("pinned") or ref.get(cam)
        c = cand.get("pinned") or cand.get(cam)
        pairs = {cam if ("pinned" not in ref or "pinned" not in cand) else "pinned": (r, c)}
    else:
        cams = ["faceOn", "dtl"] if args.camera == "both" else [args.camera]
        pairs = {k: (ref.get(k), cand.get(k)) for k in cams if ref.get(k) or cand.get(k)}

    if not pairs:
        print("pose_diff: no pose frames to compare", file=sys.stderr)
        return 2

    results, ok = {}, True
    for cam, (r, c) in pairs.items():
        if r is None or c is None:
            print(f"== {cam}: present in only one input — FAIL")
            results[cam] = {"pass": False, "error": "missing in one input"}
            ok = False
            continue
        size = _parse_size(args.size) if args.size else (r[2] or c[2] or (688, 1024))
        res = compare(r, c, size, args.min_conf)
        report(cam, res)
        results[cam] = res
        ok = ok and res["pass"]

    print(f"pose_diff: {'PASS' if ok else 'FAIL'} ({', '.join(results)})")
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"pass": ok, "cameras": results}, fh, indent=1)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
