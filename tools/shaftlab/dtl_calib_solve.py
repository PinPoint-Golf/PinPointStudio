#!/usr/bin/env python3
"""dtl_calib_solve.py -- the down-the-line camera's pose relative to the face-on camera from
the two-camera protocol's stick clips (two_camera_capture_protocol.md §4 B), for
swinglab_run --dtl-calib and club3d.camera (dtl_continuous_track_design_update.md §4 item 1).

The SAME arithmetic as src/Analysis/camera_pose_sticks.h, in numpy, so the offline solve on
the protocol clips and the app's solve are one method; --selftest builds the unit test's
synthetic bay and must print the same numbers camera_pose_sticks_test prints.

Input: a JSON of IMAGE OBSERVATIONS per camera (measured on the B clips -- the line fits are
not done here; dtl_yaw_probe.py's stick finder is a start, a hand mark is acceptable):

  {
    "faceOn": { "f": 1500, "cx": 640, "cy": 512,
                "target":   [[x0,y0],[x1,y1]],   # two points on the target-line stick
                "hands":    [[x0,y0],[x1,y1]],   # the hands-line stick (parallel, golfer's side)
                "cross":    [[x0,y0],[x1,y1]],   # the cross stick through the ball spot
                "vertical": [[x0,y0],[x1,y1]],   # optional: the plumbed stick
                "ball": [u,v], "targetEnd": [u,v],           # the ball spot, the +X end of the target stick
                "verticalTop": [u,v] (optional), "stickLenM": 1.219, "verticalLenM": 1.219 },
    "dtl": { ...the same... }
  }

Output (--out): {"calibrated": true, "yawDeg", "pitchDeg", "rollDeg", "offset": [x,y,z],
"interCameraDeg", "faceOn": {...}, "dtl": {...}, "residuals": {...}} -- the file
swinglab_run --dtl-calib reads. Bay frame: ball at the origin, X along the target line to the
declared target end, Z up, Y = Z x X. Fusion frame: X = face-on image-right levelled,
Y = face-on view ray levelled, Z up (shaft_fusion.h).

  dtl_calib_solve.py obs.json --out calib.json
  dtl_calib_solve.py --selftest
"""
import argparse, json, math, sys
import numpy as np


def line_through(p0, p1):
    a = p1[1] - p0[1]; b = p0[0] - p1[0]; c = -(a * p0[0] + b * p0[1])
    n = math.hypot(a, b)
    return np.array([a / n, b / n, c / n])


def plane_normal(K, l):
    f, cx, cy = K
    n = np.array([l[0] * f, l[1] * f, l[0] * cx + l[1] * cy + l[2]])
    return n / np.linalg.norm(n)


def ray(K, u, v):
    f, cx, cy = K
    return np.array([(u - cx) / f, (v - cy) / f, 1.0])


def solve_camera_pose(K, o):
    """K = (f, cx, cy); o = dict of observations. Returns dict(R, C, yawDeg, pitchDeg, rollDeg, ...)."""
    nT = plane_normal(K, line_through(*o["target"]))
    nH = plane_normal(K, line_through(*o["hands"]))
    nC = plane_normal(K, line_through(*o["cross"]))
    r1 = np.cross(nT, nH)
    if np.linalg.norm(r1) < 1e-6:
        raise ValueError("the target and hands sticks coincide in the image")
    r1 /= np.linalg.norm(r1)
    b = ray(K, *o["ball"]); e = ray(K, *o["targetEnd"])
    vx = r1[0] - r1[2] * b[0]; vy = r1[1] - r1[2] * b[1]
    if vx * (e[0] - b[0]) + vy * (e[1] - b[1]) < 0:
        r1 = -r1
    r2 = np.cross(r1, nC)
    if np.linalg.norm(r2) < 1e-6:
        raise ValueError("the cross stick runs along the target line in the image")
    r2 /= np.linalg.norm(r2)
    r3 = np.cross(r1, r2)
    if r3[1] > 0:
        r2, r3 = -r2, -r3
    roll_resid = 0.0
    if o.get("vertical"):
        nV = plane_normal(K, line_through(*o["vertical"]))
        a, bb = r2.copy(), r3.copy()
        c1, s1 = nC @ a, nC @ bb
        c2, s2 = nV @ bb, -(nV @ a)
        M = np.array([[c1 * c1 + c2 * c2, c1 * s1 + c2 * s2], [c1 * s1 + c2 * s2, s1 * s1 + s2 * s2]])
        w, V = np.linalg.eigh(M)
        cphi, sphi = V[:, 0]
        r2n = a * cphi + bb * sphi
        r3n = np.cross(r1, r2n)
        if r3n[1] > 0:
            r2n, r3n = -r2n, -r3n
        rc = math.degrees(math.asin(min(1.0, abs(nC @ r2n))))
        rv = math.degrees(math.asin(min(1.0, abs(nV @ r3n))))
        roll_resid = max(rc, rv)
        r2, r3 = r2n, r3n
    R = np.column_stack([r1, r2, r3])          # world -> camera
    dB = ray(K, *o["ball"])

    def scale_from(Xw, uv):
        q = R @ np.array(Xw); dE = ray(K, *uv)
        A = np.cross(dB, dE); B = np.cross(q, dE)
        den = A @ A
        if den <= 1e-12:
            return None
        return -(B @ A) / den

    L = float(o.get("stickLenM", 1.219))
    sT = scale_from([L, 0, 0], o["targetEnd"])
    sV = scale_from([0, 0, float(o.get("verticalLenM", 1.219))], o["verticalTop"]) if o.get("verticalTop") else None
    if sT is None and sV is None:
        raise ValueError("no scale: the stick end coincides with the ball in the image")
    s = sT if sT is not None else sV
    scale_resid = 0.0
    if sT is not None and sV is not None:
        s = 0.5 * (sT + sV); scale_resid = sT / sV - 1.0
    if s <= 0:
        raise ValueError("the ball is behind the camera")
    t = dB * s
    C = -(R.T @ t)
    axis = R.T @ np.array([0, 0, 1.0]); right = R.T @ np.array([1.0, 0, 0])
    yaw = math.degrees(math.atan2(axis[1], axis[0]))
    pitch = -math.degrees(math.asin(max(-1.0, min(1.0, axis[2]))))
    lr = np.cross(axis, [0, 0, 1.0]); lr /= np.linalg.norm(lr)
    ld = np.cross(axis, lr); ld /= np.linalg.norm(ld)
    roll = math.degrees(math.atan2(right @ ld, right @ lr))
    return dict(R=R, C=C, yawDeg=yaw, pitchDeg=pitch, rollDeg=roll,
                rollResidualDeg=roll_resid, scaleResidual=scale_resid)


def dtl_camera(yaw, pitch, roll):
    """fusion::dtlCamera: (d, right, down) in the fusion frame."""
    p, a, r = map(math.radians, (pitch, yaw, roll))
    d0, r0, dn0 = np.array([1.0, 0, 0]), np.array([0, -1.0, 0]), np.array([0, 0, -1.0])
    d1 = d0 * math.cos(p) + dn0 * math.sin(p); dn1 = dn0 * math.cos(p) - d0 * math.sin(p)
    c, s = math.cos(a), math.sin(a)
    rz = lambda v: np.array([c * v[0] - s * v[1], s * v[0] + c * v[1], v[2]])
    right, down = rz(r0), rz(dn1)
    return rz(d1), right * math.cos(r) + down * math.sin(r), down * math.cos(r) - right * math.sin(r)


def dtl_relative_to_face_on(fo, dtl):
    Z = np.array([0, 0, 1.0])
    axF = fo["R"].T @ np.array([0, 0, 1.0])
    yF = np.array([axF[0], axF[1], 0.0]); yF /= np.linalg.norm(yF)
    xF = np.cross(yF, Z); xF /= np.linalg.norm(xF)
    to_fus = lambda v: np.array([v @ xF, v @ yF, v @ Z])
    axD = to_fus(dtl["R"].T @ np.array([0, 0, 1.0])); rtD = to_fus(dtl["R"].T @ np.array([1.0, 0, 0]))
    yaw = math.degrees(math.atan2(axD[1], axD[0]))
    pitch = -math.degrees(math.asin(max(-1.0, min(1.0, axD[2]))))
    _, mr, md = dtl_camera(yaw, pitch, 0.0)
    roll = math.degrees(math.atan2(rtD @ md, rtD @ mr))
    off = to_fus(dtl["C"] - fo["C"])
    lD = np.array([axD[0], axD[1], 0.0]); lD /= np.linalg.norm(lD)
    inter = math.degrees(math.acos(max(-1.0, min(1.0, lD @ np.array([0, 1.0, 0])))))
    return dict(yawDeg=yaw, pitchDeg=pitch, rollDeg=roll, offset=off.tolist(), interCameraDeg=inter,
                faceOnPitchDeg=fo["pitchDeg"], faceOnRollDeg=fo["rollDeg"])


# ---------------------------------------------------------------- self-test (the C++ test's bay)
def _truth_cam(C, look, roll_deg, K):
    C = np.array(C, float); look = np.array(look, float)
    z = look - C; z /= np.linalg.norm(z)
    x = np.cross(z, [0, 0, 1.0]); x /= np.linalg.norm(x)
    y = np.cross(z, x); y /= np.linalg.norm(y)
    r = math.radians(roll_deg)
    xr = x * math.cos(r) + y * math.sin(r); yr = y * math.cos(r) - x * math.sin(r)
    R = np.vstack([xr, yr, z])
    def project(P):
        p = R @ (np.array(P, float) - C)
        return (K[1] + K[0] * p[0] / p[2], K[2] + K[0] * p[1] / p[2])
    def line_of(a, b):
        pts = np.array([project(np.array(a) + (np.array(b) - np.array(a)) * f) for f in np.linspace(0, 1, 41)])
        m = pts.mean(axis=0); cov = np.cov((pts - m).T, bias=True)
        th = 0.5 * math.atan2(2 * cov[0, 1], cov[0, 0] - cov[1, 1])
        return [m.tolist(), (m + 100 * np.array([math.cos(th), math.sin(th)])).tolist()]
    return dict(R=R, C=C, project=project, line_of=line_of)


def selftest():
    L = 1.219
    ball = [0, 0, 0]; tB = [L, 0, 0]; hA = [-0.2, 0.55, 0]; hB = [L - 0.2, 0.55, 0]
    cA = [0, -0.6, 0]; cB = [0, 0.6, 0]; vB = [0, 0, L]
    KF = (1500.0, 700.0, 480.0); KD = (1400.0, 300.0, 540.0)
    fo = _truth_cam([0.15, -2.0, 1.05], [0.25, 0.4, 0.75], 1.5, KF)
    dtl = _truth_cam([-2.2, 0.7, 0.95], [0.2, 0.7 + 2.4 * math.tan(math.radians(6.0)), 0.95 - 2.4 * math.tan(math.radians(3.0))], -2.0, KD)
    def obs(c):
        return dict(target=c["line_of"](ball, tB), hands=c["line_of"](hA, hB), cross=c["line_of"](cA, cB),
                    vertical=c["line_of"](ball, vB), ball=list(c["project"](ball)), targetEnd=list(c["project"](tB)),
                    verticalTop=list(c["project"](vB)), stickLenM=L, verticalLenM=L)
    pf = solve_camera_pose(KF, obs(fo)); pd = solve_camera_pose(KD, obs(dtl))
    ok = True
    def ang(A, B):
        return math.degrees(math.acos(max(-1.0, min(1.0, 0.5 * (np.trace(A.T @ B) - 1.0)))))
    for name, p, t in (("face-on", pf, fo), ("dtl", pd, dtl)):
        e_r, e_c = ang(p["R"], t["R"]), np.linalg.norm(p["C"] - t["C"])
        print(f"{name}: yaw {p['yawDeg']:.4f} pitch {p['pitchDeg']:.4f} roll {p['rollDeg']:.4f} "
              f"C ({p['C'][0]:.4f}, {p['C'][1]:.4f}, {p['C'][2]:.4f})  rotation err {e_r:.2e} deg  centre err {e_c:.2e} m")
        ok = ok and e_r < 1e-6 and e_c < 1e-6
    r = dtl_relative_to_face_on(pf, pd)
    print(f"relative: yaw {r['yawDeg']:.4f} pitch {r['pitchDeg']:.4f} roll {r['rollDeg']:.4f} "
          f"offset ({r['offset'][0]:.4f}, {r['offset'][1]:.4f}, {r['offset'][2]:.4f}) inter-camera {r['interCameraDeg']:.3f}")
    # the fusion model rebuilt from those numbers reproduces the measured DTL axes
    d, rt, dn = dtl_camera(r["yawDeg"], r["pitchDeg"], r["rollDeg"])
    Z = np.array([0, 0, 1.0]); axF = fo["R"].T @ Z
    yF = np.array([axF[0], axF[1], 0.0]); yF /= np.linalg.norm(yF); xF = np.cross(yF, Z)
    to_fus = lambda v: np.array([v @ xF, v @ yF, v @ Z])
    e = max(np.linalg.norm(d - to_fus(dtl["R"].T @ Z)), np.linalg.norm(rt - to_fus(dtl["R"].T @ np.array([1.0, 0, 0]))),
            np.linalg.norm(dn - to_fus(dtl["R"].T @ np.array([0, 1.0, 0]))))
    print(f"fusion::dtlCamera(yaw, pitch, roll) reproduces the DTL axes to {e:.2e}")
    ok = ok and e < 1e-9
    print("SELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("obs", nargs="?")
    ap.add_argument("--out")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.obs:
        ap.error("an observations JSON or --selftest")
    d = json.load(open(a.obs))
    poses = {}
    for cam in ("faceOn", "dtl"):
        o = d[cam]
        poses[cam] = solve_camera_pose((o["f"], o["cx"], o["cy"]), o)
    r = dtl_relative_to_face_on(poses["faceOn"], poses["dtl"])
    out = dict(calibrated=True, yawDeg=r["yawDeg"], pitchDeg=r["pitchDeg"], rollDeg=r["rollDeg"], offset=r["offset"],
               interCameraDeg=r["interCameraDeg"],
               faceOn={k: (v.tolist() if hasattr(v, "tolist") else v) for k, v in poses["faceOn"].items()},
               dtl={k: (v.tolist() if hasattr(v, "tolist") else v) for k, v in poses["dtl"].items()},
               method="camera_pose_sticks.h / dtl_calib_solve.py: rotation from stick line directions, "
                      "origin from the ball, scale from the stick ends")
    print(json.dumps({k: out[k] for k in ("calibrated", "yawDeg", "pitchDeg", "rollDeg", "offset", "interCameraDeg")}, indent=1))
    if a.out:
        json.dump(out, open(a.out, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
