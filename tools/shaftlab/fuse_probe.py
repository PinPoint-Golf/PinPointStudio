#!/usr/bin/env python3
"""fuse_probe.py -- K0 for the shaft fusion: what do the two recorded shaft tracks
say when they are intersected, before a line of C++ is written.

Reads `analysis.club` (face-on) and `analysis.clubDtl` (down-the-line) from each
swing.json under a session folder. No video, no tracker, no pose: only what the
two trackers already published.

THE FUSION IS TWO PLANES, NOT TWO LENGTHS.  A published image angle says the shaft
lies in the plane spanned by that camera's view ray and the image line. Two views
give two planes; the shaft is their intersection:

    n_F = d_F x (cos th_F e_rF + sin th_F e_dF)       n_D likewise
    u   = +/- (n_F x n_D) / |n_F x n_D|               sign from the face-on image dir

so the 3-D direction needs NEITHER projected length -- and rho_F is the weakest
thing the face-on tracker publishes (dtl_shaft_tracker_design.md s4.2). The lengths
are then REDUNDANT, which is what makes them a check: rho predicted from u against
lenPx measured in each view. |n_F x n_D| is the conditioning (0 = the two planes
coincide = the shaft points along the baseline between the cameras' view rays).

World frame as the DTL design s4.1: X target line toward the target, Z up, Y from
the ball toward the golfer. Face-on camera looks along +Y (right=+X, down=-Z); the
DTL camera looks along +X (right=-Y, down=-Z), then is yawed about Z and pitched
down by --yaw/--pitch to ask how much the answer depends on the idealisation.

    fuse_probe.py <session-dir> [--yaw DEG] [--pitch DEG] [--out DIR] [--csv]
"""
import argparse
import json
import math
import os
import sys

import numpy as np

MEASURED, COASTED, HEAD_PROJ, SYNTH, IMPLAUS = 0x01, 0x04, 0x10, 0x100, 0x200


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1.0]])


def dtl_camera(yaw_deg, pitch_deg):
    """(d, e_r, e_d) for the DTL camera. yaw>0 turns the view ray from +X toward +Y
    (camera standing on the ball side of the hands line, looking across at the
    golfer -- which is where 'behind the ball' puts it). pitch>0 looks down."""
    d = np.array([1.0, 0, 0])
    er = np.array([0, -1.0, 0])
    ed = np.array([0, 0, -1.0])
    p = math.radians(pitch_deg)
    # pitch about the camera's right axis (er): d tips toward -Z, ed follows
    d2 = math.cos(p) * d + math.sin(p) * ed
    ed2 = math.cos(p) * ed - math.sin(p) * d
    R = rot_z(math.radians(yaw_deg))
    return R @ d2, R @ er, R @ ed2


FO_CAM = (np.array([0, 1.0, 0]), np.array([1.0, 0, 0]), np.array([0, 0, -1.0]))


def view_plane_normal(cam, theta):
    d, er, ed = cam
    img = math.cos(theta) * er + math.sin(theta) * ed
    n = np.cross(d, img)
    return n / np.linalg.norm(n), img


def project(cam, u):
    """orthographic projection of unit u: (theta, rho)"""
    d, er, ed = cam
    x, y = float(u @ er), float(u @ ed)
    return math.atan2(y, x), math.hypot(x, y)


def fuse(theta_f, theta_d, dcam):
    nf, img_f = view_plane_normal(FO_CAM, theta_f)
    nd, img_d = view_plane_normal(dcam, theta_d)
    c = np.cross(nf, nd)
    cond = float(np.linalg.norm(c))
    if cond < 1e-6:
        return None, cond, False
    u = c / cond
    sf, sd = float(u @ img_f), float(u @ img_d)
    # sign: the view that sees more of the shaft decides; the other one votes
    if (sf if abs(sf) >= abs(sd) else sd) < 0:
        u = -u
    agree = (float(u @ img_f) > 0) and (float(u @ img_d) > 0)
    return u, cond, agree


def load(swing_json):
    a = json.load(open(swing_json))["analysis"]
    return a.get("club"), a.get("clubDtl"), a.get("phases") or []


def phase_times(phases):
    # segment 9 = the P-ladder rows (phase 1=P1 .. ), segment 0 = the classic events
    out = {}
    for p in phases:
        out.setdefault((p["segment"], p["phase"]), p["t_us"])
    return out


def fo_interp(samples):
    t = np.array([s["t_us"] for s in samples], float)
    th = np.unwrap(np.array([s["theta"] for s in samples], float))
    fl = np.array([s["flags"] for s in samples], int)
    ln = np.array([s["lenPx"] or np.nan for s in samples], float)
    meas = ((fl & MEASURED) != 0) & ((fl & (COASTED | SYNTH | IMPLAUS)) == 0)
    head = meas & ((fl & HEAD_PROJ) == 0)
    return t, th, meas, head, ln


def plane_fit(U):
    """best plane through the origin for unit vectors U (n,3): normal = smallest
    singular vector. Returns normal, rms out-of-plane deg, p90."""
    _, _, vt = np.linalg.svd(U, full_matrices=False)
    n = vt[2]
    off = np.degrees(np.arcsin(np.clip(U @ n, -1, 1)))
    return n, float(np.sqrt(np.mean(off ** 2))), float(np.percentile(np.abs(off), 90))


def run_swing(path, dcam, rows):
    club, dtl, phases = load(path)
    name = os.path.basename(os.path.dirname(path))
    if not club or not dtl or not club.get("valid"):
        return {"swing": name, "skip": "no club / clubDtl"}
    t, th, meas, head, ln = fo_interp(club["samples"])
    LF = club["lengths"].get("fusedPx") or np.nan
    LD = dtl["summary"].get("lFullPx") or np.nan
    pt = phase_times(phases)
    t_top = pt.get((0, 2))
    t_imp = pt.get((0, 5))
    out = {"swing": name, "nDtlPub": 0, "nJoint": 0, "nFoUnmeasured": 0}
    U, T = [], []
    for f in dtl["frames"]:
        if f["theta"] is None:
            continue
        out["nDtlPub"] += 1
        tu = f["t_us"] + (dtl.get("clockOffsetUs") or 0)
        i = int(np.searchsorted(t, tu))
        if i <= 0 or i >= len(t):
            continue
        if not (meas[i - 1] and meas[i]) or (t[i] - t[i - 1]) > 12000:
            out["nFoUnmeasured"] += 1
            continue
        w = (tu - t[i - 1]) / (t[i] - t[i - 1])
        thf = th[i - 1] + w * (th[i] - th[i - 1])
        u, cond, agree = fuse(thf, f["theta"], dcam)
        if u is None:
            continue
        _, rf = project(FO_CAM, u)
        _, rd = project(dcam, u)
        lf = ln[i - 1] + w * (ln[i] - ln[i - 1]) if (head[i - 1] and head[i]) else np.nan
        ld = f["lenPx"] if f.get("lenSrc") == "snapLine" and f["lenPx"] else np.nan
        rows.append(dict(swing=name, t_us=int(tu), band=f["band"], thF=math.degrees(thf) % 360,
                         thD=math.degrees(f["theta"]) % 360, ux=u[0], uy=u[1], uz=u[2], cond=cond,
                         agree=int(agree), rhoF_pred=rf, rhoD_pred=rd,
                         rhoF_meas=lf / LF if LF else np.nan, rhoD_meas=ld / LD if LD else np.nan,
                         rel_top=(tu - t_top) / 1e3 if t_top else np.nan,
                         rel_imp=(tu - t_imp) / 1e3 if t_imp else np.nan))
        U.append(u)
        T.append(tu)
    out["nJoint"] = len(U)
    if not U:
        return out
    U, T = np.array(U), np.array(T, float)
    R = [r for r in rows if r["swing"] == name]
    out["signDisagree"] = sum(1 for r in R if not r["agree"])
    out["condP10"] = float(np.percentile([r["cond"] for r in R], 10))
    out["illCond"] = sum(1 for r in R if r["cond"] < 0.26)  # planes within 15 deg
    for k, lab in (("rhoF", "F"), ("rhoD", "D")):
        e = np.array([r[k + "_meas"] - r[k + "_pred"] for r in R], float)
        e = e[np.isfinite(e)]
        out["n_rho" + lab] = int(e.size)
        if e.size:
            out["rho%s_med" % lab] = float(np.median(e))
            out["rho%s_p90abs" % lab] = float(np.percentile(np.abs(e), 90))
    good = np.array([r["cond"] >= 0.26 and r["agree"] for r in R], bool)
    if t_top and t_imp:
        for lab, m in (("back", (T < t_top) & (T > t_top - 900e3)), ("down", (T >= t_top) & (T <= t_imp + 20e3))):
            m = m & good
            out["n_" + lab] = int(m.sum())
            if m.sum() >= 8:
                n, rms, p90 = plane_fit(U[m])
                if n[2] < 0:
                    n = -n
                out["incl_" + lab] = float(math.degrees(math.acos(abs(n[2]))))   # plane vs the ground
                out["azim_" + lab] = float(math.degrees(math.atan2(n[1], n[0])))  # normal's heading
                out["oop_rms_" + lab] = rms
                out["oop_p90_" + lab] = p90
    # frame-to-frame 3-D turn vs the face-on turn, on consecutive joint frames
    dt = np.diff(T)
    ok = (dt > 0) & (dt < 12000) & good[1:] & good[:-1]
    if ok.any():
        ang = np.degrees(np.arccos(np.clip(np.sum(U[1:] * U[:-1], axis=1), -1, 1)))
        w3 = ang[ok] / (dt[ok] / 1e6)
        out["w3_p50"] = float(np.median(w3))
        out["w3_max"] = float(w3.max())
        # a single-frame 3-D jump the neighbours do not share = somebody is wrong
        out["jumps>25deg"] = int((ang[ok] > 25).sum())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("session")
    ap.add_argument("--yaw", type=float, default=0.0)
    ap.add_argument("--pitch", type=float, default=0.0)
    ap.add_argument("--out")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    dcam = dtl_camera(a.yaw, a.pitch)
    rows, summ = [], []
    for s in sorted(os.listdir(a.session)):
        p = os.path.join(a.session, s, "swing.json")
        if os.path.isfile(p):
            summ.append(run_swing(p, dcam, rows))
    keys = ["swing", "nDtlPub", "nJoint", "nFoUnmeasured", "signDisagree", "illCond", "n_rhoF", "rhoF_med",
            "rhoF_p90abs", "n_rhoD", "rhoD_med", "rhoD_p90abs", "n_back", "incl_back", "azim_back", "oop_rms_back",
            "n_down", "incl_down", "azim_down", "oop_rms_down", "oop_p90_down", "w3_p50", "w3_max", "jumps>25deg"]
    if not a.quiet:
        print("\t".join(keys))
        for s in summ:
            print("\t".join(("%.2f" % s[k] if isinstance(s.get(k), float) else str(s.get(k, "-"))) for k in keys))
    agg = {}
    for k in keys[1:]:
        v = [s[k] for s in summ if isinstance(s.get(k), (int, float))]
        if v:
            agg[k] = (float(np.median(v)), float(np.min(v)), float(np.max(v)), float(np.sum(v)))
    print("# yaw %.1f pitch %.1f  swings %d" % (a.yaw, a.pitch, len(summ)))
    for k, (md, lo, hi, sm) in agg.items():
        print("#  %-14s median %9.2f   [%9.2f, %9.2f]   sum %9.1f" % (k, md, lo, hi, sm))
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        import csv
        with open(os.path.join(a.out, "fuse_probe_frames.csv"), "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            w.writeheader()
            for r in rows:
                w.writerow({k: ("%.5f" % v if isinstance(v, float) else v) for k, v in r.items()})
        json.dump(summ, open(os.path.join(a.out, "fuse_probe_summary.json"), "w"), indent=1)


if __name__ == "__main__":
    sys.exit(main())
