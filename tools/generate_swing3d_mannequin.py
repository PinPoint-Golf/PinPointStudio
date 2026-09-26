#!/usr/bin/env python3
"""
generate_swing3d_mannequin.py — the 3-D swing view's figure: a smooth, simplified mannequin.

    python3 tools/generate_swing3d_mannequin.py        # numpy only

The Y-bot's hard, faceted shells read as a robot at swing speed. This builds a plain mannequin
instead — tapered capsules for the limbs, ellipsoids for pelvis, torso, head, mitten hands and
wedge feet — authored in the Y-bot's REST pose (the same skeleton the fit solves, ybot_rig.h),
so it rides the rig unchanged. Each segment is written in its joint's LOCAL frame (joint at the
origin) exactly as the extracted Y-bot segments were, one GLB per jointed segment, to
src/Resources/swing3d/man_<Joint>.glb.

Rounded ends overlap across every joint, so nothing gaps as the limbs bend — the Y-bot's shells
did. Sizes are human proportions at the Y-bot's own height (~1.80 m); the view scales each
segment with the fitted lengths. Shares nothing with the calibration views.
"""

import importlib.util
import json
import pathlib
import struct
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "src/Resources/swing3d"
_spec = importlib.util.spec_from_file_location("rigtool", ROOT / "tools/extract_swing3d_rig.py")
rigtool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rigtool)

COLOR = [0.80, 0.82, 0.86, 1.0]


# ── rest pose (rig frame: +X the model's left, +Y up, +Z front) ───────────────
def rest_frames():
    js, _ = rigtool.read_glb(rigtool.GLB_PATH)
    nodes = js["nodes"]
    by_name = {n.get("name"): i for i, n in enumerate(nodes)}
    R, T = {}, {}
    for name, parent, _lc in rigtool.RIG:
        n = nodes[by_name[rigtool.P + name]]
        t = np.array(n.get("translation", [0, 0, 0]), float)
        q = np.array(n.get("rotation", [0, 0, 0, 1]), float)
        Rl = rigtool.quat_to_mat(q / np.linalg.norm(q))
        if parent is None:
            R[name], T[name] = Rl, t
        else:
            R[name] = R[parent] @ Rl
            T[name] = T[parent] + R[parent] @ t
    return R, T


# ── primitives (vertices, triangles, normals) in the rig frame ────────────────
def ellipsoid(centre, axes, radii, n_lat=18, n_lon=28):
    """axes: 3×3, columns the ellipsoid's own x, y, z directions; radii along them."""
    V, N, F = [], [], []
    a = np.asarray(radii, float)
    A = np.asarray(axes, float)
    for i in range(n_lat + 1):
        th = np.pi * i / n_lat
        for j in range(n_lon):
            ph = 2 * np.pi * j / n_lon
            u = np.array([np.sin(th) * np.cos(ph), np.cos(th), np.sin(th) * np.sin(ph)])
            V.append(centre + A @ (u * a))
            n = A @ (u / a)
            N.append(n / np.linalg.norm(n))
    for i in range(n_lat):
        for j in range(n_lon):
            p = i * n_lon + j
            q = i * n_lon + (j + 1) % n_lon
            r = (i + 1) * n_lon + j
            s = (i + 1) * n_lon + (j + 1) % n_lon
            F += [(p, r, q), (q, r, s)]
    return np.array(V), np.array(F), np.array(N)


def frame_for(d, hint=np.array([0.0, 0.0, 1.0])):
    """A right-handed frame with its Y along d."""
    y = d / np.linalg.norm(d)
    if abs(hint @ y) > 0.95:
        hint = np.array([1.0, 0.0, 0.0])
    x = np.cross(y, hint)
    x /= np.linalg.norm(x)
    z = np.cross(x, y)
    return np.column_stack([x, y, z])


def capsule(a, b, ra, rb, flat=1.0, hint=np.array([0.0, 0.0, 1.0]), n_ring=24, n_len=12, n_cap=7):
    """A tapered capsule a→b (radius ra at a, rb at b, spherical ends). `flat` squashes the
    cross-section along the frame's z (1 = round)."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    L = np.linalg.norm(b - a)
    A = frame_for(b - a, hint)
    prof = []                          # (y along the axis, radius) from the a-cap to the b-cap
    for i in range(n_cap, 0, -1):
        t = (np.pi / 2) * i / n_cap
        prof.append((-ra * np.sin(t), ra * np.cos(t)))
    for i in range(n_len + 1):
        s = i / n_len
        prof.append((s * L, ra + (rb - ra) * s))
    for i in range(1, n_cap + 1):
        t = (np.pi / 2) * i / n_cap
        prof.append((L + rb * np.sin(t), rb * np.cos(t)))
    prof = [(-ra, 0.0)] + prof + [(L + rb, 0.0)]
    V = []
    for (y, r) in prof:
        for j in range(n_ring):
            ph = 2 * np.pi * j / n_ring
            V.append(a + A @ np.array([r * np.cos(ph), y, r * np.sin(ph) * flat]))
    F = []
    for i in range(len(prof) - 1):
        for j in range(n_ring):
            p = i * n_ring + j
            q = i * n_ring + (j + 1) % n_ring
            r = (i + 1) * n_ring + j
            s = (i + 1) * n_ring + (j + 1) % n_ring
            F += [(p, q, r), (q, s, r)]
    V = np.array(V)
    F = np.array(F)
    return V, F, smooth_normals(V, F)


def smooth_normals(V, F):
    N = np.zeros_like(V)
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    for k in range(3):
        np.add.at(N, F[:, k], fn)
    ln = np.linalg.norm(N, axis=1, keepdims=True)
    ln[ln == 0] = 1
    return N / ln


def orient_outward(V, F, N, centre):
    """Make each triangle wind counter-clockwise seen from outside (normals agree)."""
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    c = (V[F[:, 0]] + V[F[:, 1]] + V[F[:, 2]]) / 3.0
    nrm = (N[F[:, 0]] + N[F[:, 1]] + N[F[:, 2]])
    flip = np.einsum("ij,ij->i", fn, nrm) < 0
    F = F.copy()
    F[flip] = F[flip][:, [0, 2, 1]]
    return F


def merge(parts):
    V, F, N, off = [], [], [], 0
    for (v, f, n) in parts:
        f = orient_outward(v, f, n, v.mean(axis=0))
        V.append(v); N.append(n); F.append(f + off)
        off += len(v)
    return np.vstack(V), np.vstack(F), np.vstack(N)


def write_glb(name, V, F, N):
    V = V.astype(np.float32)
    N = N.astype(np.float32)
    F = F.astype(np.uint32)
    pb, nb, ib = V.tobytes(), N.tobytes(), F.flatten().tobytes()
    doc = {
        "asset": {"version": "2.0", "generator": "generate_swing3d_mannequin.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "materials": [{"name": "Mannequin", "pbrMetallicRoughness": {
            "baseColorFactor": COLOR, "metallicFactor": 0.0, "roughnessFactor": 0.55}}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1},
                                                   "indices": 2, "mode": 4, "material": 0}]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(V), "type": "VEC3",
             "min": V.min(0).tolist(), "max": V.max(0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": len(N), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": int(F.size), "type": "SCALAR"}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pb)},
            {"buffer": 0, "byteOffset": len(pb), "byteLength": len(nb)},
            {"buffer": 0, "byteOffset": len(pb) + len(nb), "byteLength": len(ib)}],
        "buffers": [{"byteLength": len(pb) + len(nb) + len(ib)}],
    }
    out = OUT_DIR / f"man_{name}.glb"
    out.write_bytes(rigtool.pack_glb(doc, pb + nb + ib))
    return out


def main():
    R, T = rest_frames()
    X, Y, Z = np.eye(3)
    I = np.eye(3)
    seg = {}

    # Trunk: ONE smooth tapered torso (waist → chest, flattened front-to-back) on the mid-spine,
    # a slim shoulder girdle on the upper spine, the pelvis below, and a waist filler that sits
    # inside the overlap. Stacked ellipsoids read as a string of beads side-on; one torso does
    # not, and the spine's three segments bend together (coupled), so it stays aligned.
    hips = T["Hips"]
    seg["Hips"] = [ellipsoid(hips + np.array([0, -0.03, -0.005]), I, (0.150, 0.120, 0.105))]
    seg["Spine"] = [ellipsoid(T["Spine"] + np.array([0, 0.02, -0.01]), I, (0.105, 0.075, 0.068))]
    seg["Spine1"] = [capsule(T["Spine"] + np.array([0, 0.02, 0.005]), T["Spine2"] + np.array([0, 0.085, 0.0]),
                             0.122, 0.150, flat=0.66)]
    seg["Spine2"] = [ellipsoid(T["Spine2"] + np.array([0, 0.115, -0.01]), I, (0.160, 0.070, 0.085))]
    # Neck and head: the neck turns WITH the head (the head's DoFs sit at the neck base).
    seg["Neck"] = [capsule(T["Neck"] - np.array([0, 0.02, 0]), T["Head"], 0.048, 0.045)]
    seg["Head"] = [ellipsoid(T["Head"] + np.array([0, 0.095, 0.015]), I, (0.080, 0.110, 0.095))]

    for side, L in (("Left", 1.0), ("Right", -1.0)):
        sh, arm, fa, hd, mid = (T[side + "Shoulder"], T[side + "Arm"], T[side + "ForeArm"],
                                T[side + "Hand"], T[side + "HandMiddle1"])
        # Clavicle + a rounded shoulder cap.
        seg[side + "Shoulder"] = [capsule(sh, arm, 0.040, 0.050),
                                  ellipsoid(arm + np.array([-L * 0.005, 0.005, 0]), I, (0.058, 0.055, 0.058))]
        seg[side + "Arm"] = [capsule(arm, fa, 0.047, 0.037)]
        seg[side + "ForeArm"] = [capsule(fa, hd, 0.036, 0.026, flat=0.8, hint=Y)]
        # Mitten hand: palm (down, -Y, in the rest pose) and a thumb forward (+Z).
        along = (mid - hd) / np.linalg.norm(mid - hd)
        hand_axes = np.column_stack([np.cross(Y, along), along, Y])
        hand_axes[:, 0] /= np.linalg.norm(hand_axes[:, 0])
        palm_c = hd + along * 0.085
        thumb_a = hd + along * 0.035 + Z * 0.025 - Y * 0.005
        seg[side + "Hand"] = [ellipsoid(palm_c - along * 0.01, hand_axes, (0.038, 0.072, 0.024)),
                              capsule(thumb_a, thumb_a + along * 0.038 + Z * 0.016, 0.012, 0.010)]
        # Legs.
        ul, lg, ft, toe, toe_end = (T[side + "UpLeg"], T[side + "Leg"], T[side + "Foot"],
                                    T[side + "ToeBase"], T[side + "Toe_End"])
        seg[side + "UpLeg"] = [capsule(ul, lg, 0.072, 0.050)]
        seg[side + "Leg"] = [capsule(lg, ft, 0.049, 0.034)]
        # Foot: a rounded shoe from under the heel to the toes, flat on the floor at rest.
        heel = np.array([ft[0], 0.040, ft[2] - 0.045])
        tip = np.array([toe_end[0], 0.035, toe_end[2] - 0.03])
        c = (heel + tip) / 2
        d = tip - heel
        foot_axes = np.column_stack([np.cross(Y, d / np.linalg.norm(d)), d / np.linalg.norm(d), Y])
        foot_axes[:, 0] /= np.linalg.norm(foot_axes[:, 0])
        seg[side + "Foot"] = [ellipsoid(c, foot_axes, (0.042, np.linalg.norm(d) / 2 + 0.01, 0.038)),
                              capsule(ft, ft + np.array([0, -0.03, 0.005]), 0.034, 0.038)]

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    total = 0
    for name, parts in seg.items():
        V, F, N = merge(parts)
        # Into the joint's LOCAL frame: p_local = Rᵀ (p − T).
        Rj, Tj = R[name], T[name]
        Vl = (V - Tj) @ Rj
        Nl = N @ Rj
        out = write_glb(name, Vl, F, Nl)
        total += out.stat().st_size
        print(f"  {out.name:<26} {len(F):6d} tris")
    print(f"wrote {len(seg)} mannequin segments to {OUT_DIR.relative_to(ROOT)} ({total / 1e6:.2f} MB)")


if __name__ == "__main__":
    sys.exit(main())
