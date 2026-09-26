#!/usr/bin/env python3
"""
generate_swing3d_hull.py — the 3-D swing figure as ONE skinned hull.

    python3 tools/generate_swing3d_hull.py              # numpy only

The segment mannequin (generate_swing3d_mannequin.py) is a set of rigid pieces, and rigid
pieces show their joins — a torso of stacked beads. This builds a single continuous skin
instead: the body is modelled as a smooth union of anatomical shapes (a flat abdomen, a rib
cage, pecs and trapezius, buttocks, calves…) as a signed-distance field, meshed with surface
nets, and SKINNED — every vertex weighted to the bones of its body part — so Qt Quick 3D bends
it at the joints the fit drives. Nothing new is estimated: it rides the same skeleton.

The bind pose is the rig's θ = 0 pose (skeleton3d_rig.h): the Y-bot T-pose with the upper arms
hanging 75° down, in the RIG frame (+X the model's left, +Y up, +Z front), unit scale. Its joint
positions are written into the GLB (extras.bindJoints) so the app can check its own inverse
bind poses against them.

Output: src/Resources/swing3d/hull.glb — POSITION, NORMAL, JOINTS_0 (u16×4, ybot joint index),
WEIGHTS_0 (f32×4), uint32 indices.
"""

import importlib.util
import json
import pathlib
import struct
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "src/Resources/swing3d/hull.glb"
_spec = importlib.util.spec_from_file_location("rigtool", ROOT / "tools/extract_swing3d_rig.py")
rigtool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rigtool)

VOXEL = 0.0085                 # metres
COLOR = [0.80, 0.82, 0.86, 1.0]


def axis_angle(axis, ang):
    a = np.asarray(axis, float)
    a = a / np.linalg.norm(a)
    x, y, z = a
    c, s = np.cos(ang), np.sin(ang)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


# ── the bind pose: the rig at θ = 0, rig frame ────────────────────────────────
def bind_pose():
    js, _ = rigtool.read_glb(rigtool.GLB_PATH)
    nodes = js["nodes"]
    by_name = {n.get("name"): i for i, n in enumerate(nodes)}
    Rl, Tl, par, order = {}, {}, {}, []
    for name, parent, _lc in rigtool.RIG:
        n = nodes[by_name[rigtool.P + name]]
        Tl[name] = np.array(n.get("translation", [0, 0, 0]), float)
        q = np.array(n.get("rotation", [0, 0, 0, 1]), float)
        Rl[name] = rigtool.quat_to_mat(q / np.linalg.norm(q))
        par[name] = parent
        order.append(name)
    # T-pose world rotations, for the neutral's axis (skeleton3d_rig.h buildRig).
    Wt = {}
    for name in order:
        p = par[name]
        Wt[name] = Rl[name] if p is None else Wt[p] @ Rl[name]
    N = {name: np.eye(3) for name in order}
    for arm, L in (("LeftArm", 1.0), ("RightArm", -1.0)):
        front_local = Wt[arm].T @ np.array([0, 0, 1.0])
        N[arm] = axis_angle(front_local, -L * 75.0 * np.pi / 180.0)
    W, P = {}, {}
    for name in order:
        p = par[name]
        if p is None:
            W[name] = Rl[name] @ N[name]
            P[name] = Tl[name]
        else:
            W[name] = W[p] @ Rl[name] @ N[name]
            P[name] = P[p] + W[p] @ Tl[name]
    return order, W, P, Wt


# ── signed distance primitives (vectorised over points p: (M,3)) ─────────────
def sd_ellipsoid(p, c, R, r):
    q = (p - c) @ R / np.asarray(r)
    k0 = np.linalg.norm(q, axis=1)
    k1 = np.linalg.norm(q / np.asarray(r), axis=1)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


def sd_roundcone(p, a, b, r1, r2):
    """Inigo Quilez's exact round cone: spheres r1 at a and r2 at b, tangent-joined."""
    ba = b - a
    l2 = ba @ ba
    rr = r1 - r2
    a2 = l2 - rr * rr
    il2 = 1.0 / l2
    pa = p - a
    y = pa @ ba
    z = y - l2
    xv = pa * l2 - np.outer(y, ba)
    x2 = np.einsum("ij,ij->i", xv, xv)
    y2 = y * y * l2
    z2 = z * z * l2
    k = np.sign(rr) * rr * rr * x2
    out = np.empty(len(p))
    c1 = np.sign(z) * a2 * z2 > k
    c2 = np.sign(y) * a2 * y2 < k
    out[c1] = np.sqrt(x2[c1] + z2[c1]) * il2 - r2
    m = ~c1 & c2
    out[m] = np.sqrt(x2[m] + y2[m]) * il2 - r1
    m = ~c1 & ~c2
    out[m] = (np.sqrt(x2[m] * a2 * il2) + y[m] * rr) * il2 - r1
    return out


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1 - h) + a * h - k * h * (1 - h)


def frame_y(d, hint):
    y = d / np.linalg.norm(d)
    x = np.cross(y, hint)
    if np.linalg.norm(x) < 1e-6:
        x = np.cross(y, [1.0, 0, 0])
    x /= np.linalg.norm(x)
    z = np.cross(x, y)
    return np.column_stack([x, y, z])


# ── the body ─────────────────────────────────────────────────────────────────
def build_parts(W, P, Wt):
    I = np.eye(3)
    X, Y, Z = np.eye(3)
    hips, sp, sp1, sp2, neck, head = P["Hips"], P["Spine"], P["Spine1"], P["Spine2"], P["Neck"], P["Head"]
    parts = {}

    # Torso: pelvis, buttocks, a FLAT abdomen, rib cage, pecs, trapezius, neck, head.
    def torso(p):
        # The pelvis stops AT the hip joints, not below them: skin between the thighs folds in on
        # itself when they move (a dark crotch at the top of the backswing).
        d = sd_ellipsoid(p, hips + [0, 0.005, -0.005], I, (0.140, 0.092, 0.098))
        for L in (1, -1):
            d = smin(d, sd_ellipsoid(p, hips + [L * 0.060, -0.055, -0.048], I, (0.074, 0.088, 0.066)), 0.03)
        # Abdomen and rib cage blended WIDE, so the waist is a gentle taper, not an hourglass —
        # and the abdomen's front is flat (no paunch).
        d = smin(d, sd_ellipsoid(p, sp + [0, 0.050, 0.000], I, (0.138, 0.135, 0.088)), 0.07)
        d = smin(d, sd_ellipsoid(p, sp2 + [0, 0.030, 0.008], I, (0.155, 0.160, 0.100)), 0.07)
        d = smin(d, sd_ellipsoid(p, sp2 + [0, 0.110, 0.020], I, (0.170, 0.075, 0.090)), 0.05)   # pecs / upper chest
        d = smin(d, sd_roundcone(p, P["LeftShoulder"] + [0, 0.02, -0.02], P["RightShoulder"] + [0, 0.02, -0.02],
                                 0.055, 0.055), 0.04)                                          # trapezius
        d = smin(d, sd_roundcone(p, neck - [0, 0.03, 0.005], head + [0, 0.01, 0.01], 0.052, 0.046), 0.03)
        d = smin(d, sd_ellipsoid(p, head + [0, 0.095, 0.015], I, (0.080, 0.108, 0.094)), 0.025)
        return d
    parts["torso"] = torso

    for side, L in (("Left", 1.0), ("Right", -1.0)):
        arm, fa, hd, mid = P[side + "Arm"], P[side + "ForeArm"], P[side + "Hand"], P[side + "HandMiddle1"]
        along = (mid - hd) / np.linalg.norm(mid - hd)
        palm = W[side + "Hand"] @ (Wt[side + "Hand"].T @ np.array([0, -1.0, 0]))
        thumb = W[side + "Hand"] @ (Wt[side + "Hand"].T @ np.array([0, 0, 1.0]))
        side_ax = np.cross(palm, along)
        HR = np.column_stack([side_ax / np.linalg.norm(side_ax), along, palm])

        def arm_sdf(p, arm=arm, fa=fa, hd=hd, along=along, thumb=thumb, HR=HR, L=L):
            d = sd_ellipsoid(p, arm + [L * 0.008, 0.005, 0], I, (0.058, 0.060, 0.060))                # deltoid
            d = smin(d, sd_roundcone(p, arm, fa, 0.046, 0.036), 0.03)
            d = smin(d, sd_roundcone(p, fa, hd, 0.037, 0.025), 0.02)
            d = smin(d, sd_ellipsoid(p, hd + along * 0.075, HR, (0.036, 0.072, 0.022)), 0.018)       # mitten
            t0 = hd + along * 0.03 + thumb * 0.022
            d = smin(d, sd_roundcone(p, t0, t0 + along * 0.035 + thumb * 0.015, 0.012, 0.010), 0.008)
            return d
        parts["arm" + side] = arm_sdf

        ul, lg, ft, te = P[side + "UpLeg"], P[side + "Leg"], P[side + "Foot"], P[side + "Toe_End"]

        def leg_sdf(p, ul=ul, lg=lg, ft=ft, te=te, L=L):
            d = sd_roundcone(p, ul + [L * 0.012, 0.02, 0], lg, 0.080, 0.050)                         # thigh
            d = smin(d, sd_ellipsoid(p, lg + (ft - lg) * 0.30 + [0, 0, -0.018], I, (0.050, 0.110, 0.052)), 0.03)  # calf
            d = smin(d, sd_roundcone(p, lg, ft, 0.049, 0.032), 0.025)
            heel = np.array([ft[0], 0.040, ft[2] - 0.045])
            tip = np.array([te[0], 0.034, te[2] - 0.025])
            c = (heel + tip) / 2
            dd = tip - heel
            FR = frame_y(dd, np.array([0, 1.0, 0]))
            FR = np.column_stack([FR[:, 0], FR[:, 1], np.cross(FR[:, 0], FR[:, 1])])
            d = smin(d, sd_ellipsoid(p, c, FR, (0.042, np.linalg.norm(dd) / 2 + 0.012, 0.038)), 0.03)
            return d
        parts["leg" + side] = leg_sdf
    return parts


def body_sdf(parts, p):
    t = parts["torso"](p)
    a = np.minimum(parts["armLeft"](p), parts["armRight"](p))
    l = np.minimum(parts["legLeft"](p), parts["legRight"](p))
    d = smin(t, a, 0.012)            # a narrow crease at the shoulder, not a web to the ribs
    return smin(d, l, 0.02)


# ── surface nets ─────────────────────────────────────────────────────────────
def surface_nets(F, origin, h):
    nx, ny, nz = F.shape
    inside = F < 0
    c = [F[:-1, :-1, :-1], F[1:, :-1, :-1], F[:-1, 1:, :-1], F[1:, 1:, :-1],
         F[:-1, :-1, 1:], F[1:, :-1, 1:], F[:-1, 1:, 1:], F[1:, 1:, 1:]]
    offs = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0], [0, 0, 1], [1, 0, 1], [0, 1, 1], [1, 1, 1]], float)
    edges = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]
    acc = np.zeros(c[0].shape + (3,))
    cnt = np.zeros(c[0].shape)
    for a, b in edges:
        fa, fb = c[a], c[b]
        m = (fa < 0) != (fb < 0)
        t = np.where(m, fa / np.where(m, fa - fb, 1.0), 0.0)
        pt = offs[a][None, None, None, :] + t[..., None] * (offs[b] - offs[a])[None, None, None, :]
        acc += np.where(m[..., None], pt, 0.0)
        cnt += m
    active = cnt > 0
    idx = -np.ones(active.shape, dtype=np.int64)
    idx[active] = np.arange(active.sum())
    ci = np.argwhere(active)
    V = origin + (ci + acc[active] / cnt[active][:, None]) * h
    quads = []
    # x-edges: grid points (i,j,k)-(i+1,j,k), cells around it: (i, j-1..j, k-1..k)
    s0, s1 = inside[:-1, 1:-1, 1:-1], inside[1:, 1:-1, 1:-1]
    m = s0 != s1
    ii, jj, kk = np.nonzero(m)
    jj, kk = jj + 1, kk + 1
    q = np.stack([idx[ii, jj - 1, kk - 1], idx[ii, jj, kk - 1], idx[ii, jj, kk], idx[ii, jj - 1, kk]], 1)
    quads.append(np.where(s0[m][:, None], q, q[:, ::-1]))
    s0, s1 = inside[1:-1, :-1, 1:-1], inside[1:-1, 1:, 1:-1]
    m = s0 != s1
    ii, jj, kk = np.nonzero(m)
    ii, kk = ii + 1, kk + 1
    q = np.stack([idx[ii - 1, jj, kk - 1], idx[ii - 1, jj, kk], idx[ii, jj, kk], idx[ii, jj, kk - 1]], 1)
    quads.append(np.where(s0[m][:, None], q, q[:, ::-1]))
    s0, s1 = inside[1:-1, 1:-1, :-1], inside[1:-1, 1:-1, 1:]
    m = s0 != s1
    ii, jj, kk = np.nonzero(m)
    ii, jj = ii + 1, jj + 1
    q = np.stack([idx[ii - 1, jj - 1, kk], idx[ii, jj - 1, kk], idx[ii, jj, kk], idx[ii - 1, jj, kk]], 1)
    quads.append(np.where(s0[m][:, None], q, q[:, ::-1]))
    Q = np.vstack(quads)
    Q = Q[(Q >= 0).all(1)]
    F3 = np.vstack([Q[:, [0, 1, 2]], Q[:, [0, 2, 3]]])
    return V, F3


def main():
    order, W, P, Wt = bind_pose()
    parts = build_parts(W, P, Wt)
    lo = np.min([P[n] for n in order], axis=0) - 0.14
    hi = np.max([P[n] for n in order], axis=0) + 0.14
    lo[1] = -0.02
    n = np.ceil((hi - lo) / VOXEL).astype(int) + 1
    gx, gy, gz = [lo[a] + np.arange(n[a]) * VOXEL for a in range(3)]
    print(f"grid {n[0]}×{n[1]}×{n[2]} = {n.prod() / 1e6:.1f} M points")
    F = np.empty(tuple(n))
    pts_yz = np.stack(np.meshgrid(gy, gz, indexing="ij"), -1).reshape(-1, 2)
    for i, x in enumerate(gx):
        p = np.column_stack([np.full(len(pts_yz), x), pts_yz])
        F[i] = body_sdf(parts, p).reshape(n[1], n[2])
    V, Fc = surface_nets(F, lo, VOXEL)

    # Outward normals from the field; faces wound to agree with them.
    e = 1e-4
    grad = np.column_stack([body_sdf(parts, V + [e, 0, 0]) - body_sdf(parts, V - [e, 0, 0]),
                            body_sdf(parts, V + [0, e, 0]) - body_sdf(parts, V - [0, e, 0]),
                            body_sdf(parts, V + [0, 0, e]) - body_sdf(parts, V - [0, 0, e])])
    N = grad / np.maximum(np.linalg.norm(grad, axis=1, keepdims=True), 1e-12)
    fn = np.cross(V[Fc[:, 1]] - V[Fc[:, 0]], V[Fc[:, 2]] - V[Fc[:, 0]])
    flip = np.einsum("ij,ij->i", fn, N[Fc[:, 0]] + N[Fc[:, 1]] + N[Fc[:, 2]]) < 0
    Fc[flip] = Fc[flip][:, [0, 2, 1]]

    # ── skin weights: each vertex to the bones of ITS body part, by distance ──
    J = {name: i for i, name in enumerate(order)}
    def seg(a, b, ext=1.0):
        A, B = P[a], P[b]
        return (A, A + (B - A) * ext)
    bones = {
        "Hips": seg("Hips", "Spine"), "Spine": seg("Spine", "Spine1"), "Spine1": seg("Spine1", "Spine2"),
        "Spine2": seg("Spine2", "Neck"), "Neck": seg("Neck", "Head"), "Head": seg("Head", "HeadTop_End"),
    }
    for s in ("Left", "Right"):
        bones.update({
            s + "Shoulder": seg(s + "Shoulder", s + "Arm"), s + "Arm": seg(s + "Arm", s + "ForeArm"),
            s + "ForeArm": seg(s + "ForeArm", s + "Hand"), s + "Hand": seg(s + "Hand", s + "HandMiddle1", 1.8),
            s + "UpLeg": seg(s + "UpLeg", s + "Leg"), s + "Leg": seg(s + "Leg", s + "Foot"),
            s + "Foot": seg(s + "Foot", s + "Toe_End"),
        })
    allowed = {
        "torso": ["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", "LeftShoulder", "RightShoulder",
                  "LeftUpLeg", "RightUpLeg"],
        "armLeft": ["LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "Spine2"],
        "armRight": ["RightShoulder", "RightArm", "RightForeArm", "RightHand", "Spine2"],
        "legLeft": ["LeftUpLeg", "LeftLeg", "LeftFoot", "Hips"],
        "legRight": ["RightUpLeg", "RightLeg", "RightFoot", "Hips"],
    }
    pd = np.column_stack([parts[k](V) for k in allowed])
    label = np.array(list(allowed))[np.argmin(pd, axis=1)]

    def dist_to(V, A, B):
        ab = B - A
        t = np.clip(((V - A) @ ab) / (ab @ ab), 0, 1)
        return np.linalg.norm(V - (A + t[:, None] * ab), axis=1)

    names = list(bones)
    D = np.column_stack([dist_to(V, *bones[b]) for b in names])
    Wt4 = np.zeros((len(V), 4))
    Ji4 = np.zeros((len(V), 4), dtype=np.uint16)
    for part, bl in allowed.items():
        m = label == part
        cols = [names.index(b) for b in bl]
        w = 1.0 / np.power(D[m][:, cols] + 0.012, 5)
        top = np.argsort(-w, axis=1)[:, :4]
        wt = np.take_along_axis(w, top, 1)
        wt /= wt.sum(1, keepdims=True)
        Wt4[m] = wt
        Ji4[m] = np.array([J[bl[c]] for c in range(len(bl))], dtype=np.uint16)[top]

    # ── GLB ──
    Vf, Nf = V.astype(np.float32), N.astype(np.float32)
    Wf, Jf = Wt4.astype(np.float32), Ji4.astype(np.uint16)
    If = Fc.astype(np.uint32).flatten()
    blobs = [Vf.tobytes(), Nf.tobytes(), Jf.tobytes(), Wf.tobytes(), If.tobytes()]
    offs, o = [], 0
    for b in blobs:
        offs.append(o)
        o += len(b)
    doc = {
        "asset": {"version": "2.0", "generator": "generate_swing3d_hull.py",
                  "extras": {"bindJoints": {name: [round(float(v), 6) for v in P[name]] for name in order}}},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "materials": [{"name": "Hull", "pbrMetallicRoughness": {"baseColorFactor": COLOR, "metallicFactor": 0.0,
                                                               "roughnessFactor": 0.55}}],
        "meshes": [{"name": "hull", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2,
                                                                   "WEIGHTS_0": 3}, "indices": 4, "mode": 4,
                                                    "material": 0}]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(Vf), "type": "VEC3",
             "min": Vf.min(0).tolist(), "max": Vf.max(0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": len(Nf), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5123, "count": len(Jf), "type": "VEC4"},
            {"bufferView": 3, "componentType": 5126, "count": len(Wf), "type": "VEC4"},
            {"bufferView": 4, "componentType": 5125, "count": int(If.size), "type": "SCALAR"}],
        "bufferViews": [{"buffer": 0, "byteOffset": offs[i], "byteLength": len(b)} for i, b in enumerate(blobs)],
        "buffers": [{"byteLength": o}],
    }
    OUT.write_bytes(rigtool.pack_glb(doc, b"".join(blobs)))
    counts = {k: int((label == k).sum()) for k in allowed}
    print(f"hull: {len(V)} vertices, {len(Fc)} triangles, {OUT.stat().st_size / 1e6:.2f} MB; parts {counts}")


if __name__ == "__main__":
    sys.exit(main())
