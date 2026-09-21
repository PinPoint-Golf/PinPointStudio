#!/usr/bin/env python3
"""dtl_posture_offline.py -- K0 for the down-the-line posture measures: what do the
persisted DTL pose (analysis.poseDtl), the face-on P ladder and the DTL ball say about
pelvis thrust, spine forward bend, knee flexion, ball reach and heel-toe balance, before
any C++ is written.

    dtl_posture_offline.py <session-dir> [--series swing_0006]

No truth exists for any of these, so what is graded is what CAN be: repeatability across
one golfer's swings with one club, the jitter of the raw pose against the smoothed one,
keypoint confidence at the instants each measure is read, and whether the numbers land
where a golf posture lives (30-45 deg of forward bend, 15-30 deg of knee flex, a few cm
of thrust).

Image convention: DTL camera behind the golfer looking at the target; the ball is
image-RIGHT of a right-hander, so "toward the ball" is +x. The sign is taken from the
ball's side of the hips, never from a handedness setting.
"""
import argparse, json, math, os, sys
import numpy as np

LSH, RSH, LHIP, RHIP, LKNE, RKNE, LANK, RANK = 5, 6, 11, 12, 13, 14, 15, 16
LBIG, LSML, LHEEL, RBIG, RSML, RHEEL = 17, 18, 19, 20, 21, 22
NOSE = 0
BALL_MM = 42.67


def kp_arrays(frames, W, H):
    t = np.array([f["t_us"] for f in frames], float)
    k = np.array([f["kp"] for f in frames], float).reshape(len(frames), -1, 3)
    xy = k[:, :, :2] * np.array([W, H])
    return t, xy, k[:, :, 2]


def at(t, series, tq, half_us=15000):
    m = np.abs(t - tq) <= half_us
    if not m.any():
        m = np.zeros_like(t, bool); m[np.argmin(np.abs(t - tq))] = True
    return np.nanmedian(series[m], axis=0)


def knee_flex(h, k, a):
    v1, v2 = h - k, a - k
    c = np.sum(v1 * v2, axis=1) / (np.linalg.norm(v1, axis=1) * np.linalg.norm(v2, axis=1) + 1e-9)
    return 180.0 - np.degrees(np.arccos(np.clip(c, -1, 1)))


def run(path, dump=False):
    a = json.load(open(path))["analysis"]
    pd, cd, fo = a.get("poseDtl"), a.get("clubDtl"), a.get("pose2d")
    if not pd or not cd:
        return None
    W, H = cd["frameWidth"], cd["frameHeight"]
    P = {p["p"]: p["t_us"] for p in a["club"].get("positions", [])}
    if not all(k in P for k in (1, 4, 7)):
        return {"skip": "ladder lacks P1/P4/P7"}
    t, xy, cf = kp_arrays(pd["smoothed"] or pd["frames"], W, H)
    tr, xyr, _ = kp_arrays(pd["frames"], W, H)
    ball = cd["summary"]["ball"]
    out = {}
    # scale: two independent rulers at the golfer's distance down the line
    s_ball = (BALL_MM / 10.0) / (2 * ball["radiusPx"]) if ball.get("found") and ball.get("radiusPx") else np.nan
    s_club = np.nan
    lf = cd["summary"].get("lFullPx")
    clubmm = (json.load(open(path)).get("capture", {}).get("club", {}) or {}).get("lengthMm")
    if lf and clubmm:
        s_club = (clubmm / 10.0) / lf
    out["cmPerPx_ball"], out["cmPerPx_club"] = s_ball, s_club
    s = s_club if np.isfinite(s_club) else s_ball

    hip = 0.5 * (xy[:, LHIP] + xy[:, RHIP]); sho = 0.5 * (xy[:, LSH] + xy[:, RSH])
    hipr = 0.5 * (xyr[:, LHIP] + xyr[:, RHIP])
    toward = 1.0
    if ball.get("found"):
        toward = 1.0 if ball["x"] > at(t, hip, P[1])[0] else -1.0
    thrust = toward * (hip[:, 0] - at(t, hip, P[1])[0]) * s
    thrust_raw = toward * (hipr[:, 0] - at(tr, hipr, P[1])[0]) * s
    v = sho - hip
    bend = np.degrees(np.arctan2(toward * v[:, 0], -v[:, 1]))          # 0 = upright, + = over the ball
    # which leg is nearer the camera is not knowable from a name; report both by side
    kL = knee_flex(xy[:, LHIP], xy[:, LKNE], xy[:, LANK]); kR = knee_flex(xy[:, RHIP], xy[:, RKNE], xy[:, RANK])

    def win(lo, hi, arr, fn):
        m = (t >= P[lo]) & (t <= P[hi]); return fn(arr[m]) if m.any() else np.nan
    out["thrustBack_cm"] = win(1, 4, thrust, np.max)
    out["thrustDown_cm"] = win(5, 7, thrust, np.max) if 5 in P else np.nan
    out["thrustP7_cm"] = float(at(t, thrust, P[7]))
    out["bendP1"] = float(at(t, bend, P[1])); out["bendP7"] = float(at(t, bend, P[7]))
    out["bendDive"] = (win(5, 6, bend, np.max) - out["bendP1"]) if (5 in P and 6 in P) else np.nan
    out["kneeL_P1"], out["kneeR_P1"] = float(at(t, kL, P[1])), float(at(t, kR, P[1]))
    out["kneeL_P4"], out["kneeR_P4"] = float(at(t, kL, P[4])), float(at(t, kR, P[4]))
    out["kneeL_P7"], out["kneeR_P7"] = float(at(t, kL, P[7])), float(at(t, kR, P[7]))
    # jitter: raw minus smoothed hip x, in cm, over the downswing
    m = (tr >= P[4]) & (tr <= P[7])
    if m.sum() > 3:
        sm = np.interp(tr[m], t, thrust); out["thrustJitter_cm"] = float(np.std(thrust_raw[m] - sm))
    # confidences where we read
    for nm, idx in (("hip", (LHIP, RHIP)), ("sho", (LSH, RSH)), ("kneeL", (LKNE,)), ("kneeR", (RKNE,)),
                    ("ankL", (LANK,)), ("ankR", (RANK,)), ("toe", (LBIG, RBIG)), ("heel", (LHEEL, RHEEL))):
        out["conf_" + nm] = float(min(at(t, cf[:, i], P[7]) for i in idx)) if nm in ("hip", "sho", "kneeL", "kneeR") \
            else float(min(at(t, cf[:, i], P[1]) for i in idx))
    # address geometry
    toe = toward * max(toward * at(t, xy[:, LBIG], P[1])[0], toward * at(t, xy[:, RBIG], P[1])[0])
    heel = toward * min(toward * at(t, xy[:, LHEEL], P[1])[0], toward * at(t, xy[:, RHEEL], P[1])[0])
    foot = toward * (toe - heel)
    out["footLen_cm"] = foot * s
    knee = 0.5 * (xy[:, LKNE] + xy[:, RKNE])
    com = 0.45 * hip + 0.35 * sho + 0.20 * knee
    out["balance_hip_pct"] = 100.0 * toward * (at(t, hip, P[1])[0] - heel) / foot
    out["balance_com_pct"] = 100.0 * toward * (at(t, com, P[1])[0] - heel) / foot
    if ball.get("found"):
        gap_px = toward * (ball["x"] - toe)
        out["ballGap_cm"] = gap_px * s
        # shoulder width is end-on here; take it from face-on through the vertical-extent ratio
        if fo and (fo.get("smoothed") or fo.get("frames")):
            fw, fh = a["club"]["frameWidth"], a["club"]["frameHeight"]
            tf, xyf, _ = kp_arrays(fo.get("smoothed") or fo["frames"], fw, fh)
            ext_f = abs(at(tf, 0.5 * (xyf[:, LANK] + xyf[:, RANK]), P[1])[1] - at(tf, xyf[:, NOSE], P[1])[1])
            ext_d = abs(at(t, 0.5 * (xy[:, LANK] + xy[:, RANK]), P[1])[1] - at(t, xy[:, NOSE], P[1])[1])
            r = ext_d / ext_f
            shw = np.linalg.norm(at(tf, xyf[:, LSH], P[1]) - at(tf, xyf[:, RSH], P[1])) * r
            out["scaleRatio"] = r; out["shoulderW_cm"] = shw * s
            out["ballGap_pctShoulder"] = 100.0 * gap_px / shw
    if dump:
        for i in range(0, len(t), 6):
            print("%8.0f ms  thrust %6.2f cm  bend %5.1f  kneeL %5.1f kneeR %5.1f" % ((t[i] - P[7]) / 1e3, thrust[i], bend[i], kL[i], kR[i]))
    return out


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("session"); ap.add_argument("--series")
    a = ap.parse_args()
    rows = {}
    for sw in sorted(os.listdir(a.session)):
        p = os.path.join(a.session, sw, "swing.json")
        if os.path.isfile(p):
            r = run(p, dump=(a.series == sw))
            if r and "skip" not in r: rows[sw] = r
            elif r: print(sw, r["skip"])
    keys = list(next(iter(rows.values())).keys())
    print("%-22s %8s %8s %8s %8s   per swing" % ("measure", "median", "sd", "min", "max"))
    for k in keys:
        v = np.array([rows[s].get(k, np.nan) for s in rows], float)
        print("%-22s %8.2f %8.2f %8.2f %8.2f   %s" % (k, np.nanmedian(v), np.nanstd(v, ddof=1), np.nanmin(v), np.nanmax(v),
                                                      " ".join("%.1f" % x for x in v)))


if __name__ == "__main__":
    sys.exit(main())
