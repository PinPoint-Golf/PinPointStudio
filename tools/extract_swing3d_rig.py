#!/usr/bin/env python3
"""
extract_swing3d_rig.py — the 3-D swing view's own Y-bot: segment meshes AND the rig header.

The swing view (docs/design/swing_3d_viz_design.md) shares NOTHING with the calibration
views (BodyVizView / ArmVizView / CapturePage / the wizard). So it has its own extraction,
its own meshes (src/Resources/swing3d/) and its own rig constants
(src/Analysis/skeleton3d/ybot_rig.h), all generated here from the one read-only source,
src/Resources/body/ybot.glb.

  python3 tools/extract_swing3d_rig.py          # numpy only; no pygltflib

WHY THE RIG COMES FROM THE NODE ARRAY. ybot.glb is in metres at scale 1 and its node
rest pose equals inverse(IBM) to <1e-6, so each node's glTF translation/rotation IS the
parent-local rest offset/rotation. Nothing is copied from BodyVizView.qml: those literals
are rounded, and body_pose_adapter.h's UpLeg seeds are scrambled.

WHY THE VERTEX ASSIGNMENT IS THE BODY SCRIPT'S. A triangle belongs to a bone when enough
of its vertices have that bone as their DOMINANT influence — the dominant slot mapped
through JOINTS_0 to a skin-joint index. extract_arm_segments.py compares the raw slot
(0-3) and silently falls back to "any influence"; that is not repeated here.

Each segment is written in its bone-local frame (joint at the origin, +Y along the bone)
by the bone's inverse-bind matrix, with leaf geometry (fingers, toes, head top) baked in.
"""

import json
import pathlib
import struct
import sys

import numpy as np

ROOT     = pathlib.Path(__file__).resolve().parent.parent
GLB_PATH = ROOT / "src/Resources/body/ybot.glb"
OUT_DIR  = ROOT / "src/Resources/swing3d"
HDR_PATH = ROOT / "src/Analysis/skeleton3d/ybot_rig.h"

P = "mixamorig:"

# ── The rig: every joint the fit and the view know, in parent-before-child order ──
# (name, parent, length-child). Length-child names the joint whose rest offset gives the
# bone's length; None = an end effector (no bone of its own).
RIG = [
    ("Hips",          None,           "Spine"),
    ("Spine",         "Hips",         "Spine1"),
    ("Spine1",        "Spine",        "Spine2"),
    ("Spine2",        "Spine1",       "Neck"),
    ("Neck",          "Spine2",       "Head"),
    ("Head",          "Neck",         "HeadTop_End"),
    ("HeadTop_End",   "Head",         None),
    ("LeftShoulder",  "Spine2",       "LeftArm"),
    ("LeftArm",       "LeftShoulder", "LeftForeArm"),
    ("LeftForeArm",   "LeftArm",      "LeftHand"),
    ("LeftHand",      "LeftForeArm",  "LeftHandMiddle1"),
    ("LeftHandMiddle1", "LeftHand",   None),
    ("RightShoulder", "Spine2",       "RightArm"),
    ("RightArm",      "RightShoulder","RightForeArm"),
    ("RightForeArm",  "RightArm",     "RightHand"),
    ("RightHand",     "RightForeArm", "RightHandMiddle1"),
    ("RightHandMiddle1", "RightHand", None),
    ("LeftUpLeg",     "Hips",         "LeftLeg"),
    ("LeftLeg",       "LeftUpLeg",    "LeftFoot"),
    ("LeftFoot",      "LeftLeg",      "LeftToeBase"),
    ("LeftToeBase",   "LeftFoot",     "LeftToe_End"),
    ("LeftToe_End",   "LeftToeBase",  None),
    ("RightUpLeg",    "Hips",         "RightLeg"),
    ("RightLeg",      "RightUpLeg",   "RightFoot"),
    ("RightFoot",     "RightLeg",     "RightToeBase"),
    ("RightToeBase",  "RightFoot",    "RightToe_End"),
    ("RightToe_End",  "RightToeBase", None),
]

# ── Segment meshes: (bone, baked children, majority threshold) ──
FINGERS = lambda side: [f"{side}Hand{f}{i}" for f in ("Thumb", "Index", "Middle", "Ring", "Pinky")
                        for i in range(1, 5)]
SEGMENTS = [
    ("Hips", [], 2), ("Spine", [], 1), ("Spine1", [], 1), ("Spine2", [], 1),
    ("Head", ["HeadTop_End"], 2),
    ("LeftShoulder", [], 1), ("RightShoulder", [], 1),
    ("LeftArm", [], 2), ("LeftForeArm", [], 2), ("LeftHand", FINGERS("Left"), 2),
    ("RightArm", [], 2), ("RightForeArm", [], 2), ("RightHand", FINGERS("Right"), 2),
    ("LeftUpLeg", [], 2), ("LeftLeg", [], 2), ("LeftFoot", ["LeftToeBase", "LeftToe_End"], 2),
    ("RightUpLeg", [], 2), ("RightLeg", [], 2), ("RightFoot", ["RightToeBase", "RightToe_End"], 2),
]
COLOR = [0.36, 0.40, 0.46, 1.0]


# ── Minimal GLB reader ─────────────────────────────────────────────────────────
def read_glb(path):
    b = path.read_bytes()
    magic, _ver, _total = struct.unpack("<III", b[:12])
    if magic != 0x46546C67:
        sys.exit(f"{path}: not a GLB")
    off, js, bin_ = 12, None, None
    while off < len(b):
        ln, typ = struct.unpack("<II", b[off:off + 8])
        chunk = b[off + 8:off + 8 + ln]
        if typ == 0x4E4F534A:
            js = json.loads(chunk)
        elif typ == 0x004E4942:
            bin_ = chunk
        off += 8 + ln
    return js, bin_


def accessor(js, bin_, idx):
    acc = js["accessors"][idx]
    bv = js["bufferViews"][acc["bufferView"]]
    dt = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16,
          5125: np.uint32, 5126: np.float32}[acc["componentType"]]
    n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[acc["type"]]
    item = np.dtype(dt).itemsize * n
    stride = bv.get("byteStride", item)
    start = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    if stride == item:
        arr = np.frombuffer(bin_, dtype=dt, count=acc["count"] * n, offset=start)
    else:
        arr = np.concatenate([np.frombuffer(bin_, dtype=dt, count=n, offset=start + i * stride)
                              for i in range(acc["count"])])
    return (arr.reshape(acc["count"], n) if n > 1 else arr).copy()


def pack_glb(js, bin_data):
    jb = json.dumps(js, separators=(",", ":")).encode()
    jb += b" " * (-len(jb) % 4)
    bin_data += b"\x00" * (-len(bin_data) % 4)
    body = struct.pack("<II", len(jb), 0x4E4F534A) + jb + struct.pack("<II", len(bin_data), 0x004E4942) + bin_data
    return struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body


def quat_to_mat(q):  # q = (x, y, z, w)
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def main():
    js, bin_ = read_glb(GLB_PATH)
    nodes = js["nodes"]
    by_name = {n.get("name"): i for i, n in enumerate(nodes)}

    # ── Rig table from the node array ──
    rig = []
    world_R, world_T = {}, {}
    for name, parent, _lc in RIG:
        n = nodes[by_name[P + name]]
        t = np.array(n.get("translation", [0, 0, 0]), dtype=np.float64)
        q = np.array(n.get("rotation", [0, 0, 0, 1]), dtype=np.float64)
        q /= np.linalg.norm(q)
        R = quat_to_mat(q)
        if parent is None:
            world_R[name], world_T[name] = R, t
        else:
            world_R[name] = world_R[parent] @ R
            world_T[name] = world_T[parent] + world_R[parent] @ t
        rig.append((name, parent, t, q))
    lengths = {}
    for name, _p, lc in RIG:
        lengths[name] = float(np.linalg.norm(dict((r[0], r[2]) for r in rig)[lc])) if lc else 0.0

    # Cross-check the node rest pose against inverse(IBM): it is the claim the rig rests on.
    skin = js["skins"][0]
    joints = skin["joints"]
    ibm = accessor(js, bin_, skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1).astype(np.float64)
    node_to_ji = {ni: ji for ji, ni in enumerate(joints)}
    worst = 0.0
    for name, *_ in RIG:
        ji = node_to_ji[by_name[P + name]]
        bind = np.linalg.inv(ibm[ji])
        worst = max(worst, float(np.abs(bind[:3, 3] - world_T[name]).max()),
                    float(np.abs(bind[:3, :3] - world_R[name]).max()))
    print(f"node rest pose vs inverse(IBM): worst |Δ| = {worst:.2e}")
    if worst > 1e-4:
        sys.exit("rest pose disagrees with the inverse-bind matrices — refusing to write the rig")

    write_header(rig, lengths, world_T)
    # The Y-bot's own segment meshes are no longer the view's figure (they read as a robot at
    # swing speed; tools/generate_swing3d_mannequin.py makes it). `--segments` still writes them.
    if "--segments" in sys.argv:
        write_segments(js, bin_, by_name, node_to_ji, ibm)


def write_header(rig, lengths, world_T):
    idx = {r[0]: i for i, r in enumerate(rig)}
    lines = []
    for name, parent, t, q in rig:
        lines.append(
            f'    {{ "{name}", {idx[parent] if parent else -1}, '
            f'{{ {t[0]:.9g}, {t[1]:.9g}, {t[2]:.9g} }}, '
            f'{{ {q[3]:.9g}, {q[0]:.9g}, {q[1]:.9g}, {q[2]:.9g} }}, {lengths[name]:.9g}, '
            f'{{ {world_T[name][0]:.9g}, {world_T[name][1]:.9g}, {world_T[name][2]:.9g} }} }},')
    enum = "\n".join(f"    {r[0]} = {i}," for i, r in enumerate(rig))
    HDR_PATH.parent.mkdir(parents=True, exist_ok=True)
    HDR_PATH.write_text(f"""// GENERATED by tools/extract_swing3d_rig.py from src/Resources/body/ybot.glb — do not edit.
//
// The Y-bot rig for the 3-D swing view and the skeleton3d fit
// (docs/design/swing_3d_viz_design.md §3.2). Parent-before-child order. Units: metres.
// Frame: the glTF one (Y up, +Z = the model's front, +X = the model's LEFT).
// restT/restQ are each joint's PARENT-LOCAL rest offset and rotation (quaternion w,x,y,z),
// taken straight from the node array (checked against inverse(IBM) at generation).
// length = |restT| of the joint named by the bone's length-child; 0 = an end effector.
// restWorld = the joint's rest position in the model frame — the §8.1 (a) test oracle.
//
// Shared with NOTHING on the calibration side (BodyVizView / body_pose_adapter).

#pragma once

namespace pinpoint::skeleton3d::ybot {{

struct RigJoint {{
    const char *name;
    int         parent;        // index into kJoints, -1 = root
    double      restT[3];
    double      restQ[4];      // w, x, y, z
    double      length;
    double      restWorld[3];
}};

enum Joint : int {{
{enum}
    JointCount = {len(rig)}
}};

inline constexpr RigJoint kJoints[JointCount] = {{
{chr(10).join(lines)}
}};

}} // namespace pinpoint::skeleton3d::ybot
""")
    print(f"wrote {HDR_PATH.relative_to(ROOT)} ({len(rig)} joints)")


def write_segments(js, bin_, by_name, node_to_ji, ibm):
    prim = None
    for mesh in js["meshes"]:
        for pr in mesh["primitives"]:
            if "JOINTS_0" in pr["attributes"] and (prim is None or "Surface" in mesh.get("name", "")):
                prim = pr
    a = prim["attributes"]
    pos = accessor(js, bin_, a["POSITION"]).astype(np.float64)
    nrm = accessor(js, bin_, a["NORMAL"]).astype(np.float64)
    jnt = accessor(js, bin_, a["JOINTS_0"]).astype(np.int32)
    wts = accessor(js, bin_, a["WEIGHTS_0"])
    ct = js["accessors"][a["WEIGHTS_0"]]["componentType"]
    wts = wts.astype(np.float64) / (255.0 if ct == 5121 else 65535.0 if ct == 5123 else 1.0)
    tris = accessor(js, bin_, prim["indices"]).astype(np.int64).reshape(-1, 3)
    nv = len(pos)
    dom = jnt[np.arange(nv), np.argmax(wts, axis=1)]   # skin-joint index, NOT the slot

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    total = 0
    for bone, baked, thr in SEGMENTS:
        ji = node_to_ji[by_name[P + bone]]
        if baked:
            ids = [ji] + [node_to_ji[by_name[P + c]] for c in baked]
            anyv = np.any(np.isin(jnt, ids) & (wts > 0), axis=1)
            mask = anyv[tris[:, 0]] | anyv[tris[:, 1]] | anyv[tris[:, 2]]
        else:
            cnt = sum((dom[tris[:, k]] == ji).astype(int) for k in range(3))
            mask = cnt >= thr
            if not mask.any():
                infl = np.any((jnt == ji) & (wts > 0), axis=1)
                mask = infl[tris[:, 0]] | infl[tris[:, 1]] | infl[tris[:, 2]]
        sel = tris[mask]
        if len(sel) == 0:
            sys.exit(f"{bone}: no triangles")
        used = np.unique(sel)
        remap = np.full(nv, -1, dtype=np.int64)
        remap[used] = np.arange(len(used))
        new_tris = remap[sel].astype(np.uint32)
        p4 = np.hstack([pos[used], np.ones((len(used), 1))])
        new_pos = (ibm[ji] @ p4.T).T[:, :3].astype(np.float32)
        nm = np.linalg.inv(ibm[ji][:3, :3]).T
        new_nrm = (nm @ nrm[used].T).T
        new_nrm /= np.maximum(np.linalg.norm(new_nrm, axis=1, keepdims=True), 1e-12)
        new_nrm = new_nrm.astype(np.float32)
        pb, nb, ib = new_pos.tobytes(), new_nrm.tobytes(), new_tris.flatten().tobytes()
        doc = {
            "asset": {"version": "2.0", "generator": "extract_swing3d_rig.py"},
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
            "materials": [{"name": f"{bone}Mat", "pbrMetallicRoughness": {
                "baseColorFactor": COLOR, "metallicFactor": 0.05, "roughnessFactor": 0.6}}],
            "meshes": [{"name": bone, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1},
                                                       "indices": 2, "mode": 4, "material": 0}]}],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": len(new_pos), "type": "VEC3",
                 "min": new_pos.min(0).tolist(), "max": new_pos.max(0).tolist()},
                {"bufferView": 1, "componentType": 5126, "count": len(new_nrm), "type": "VEC3"},
                {"bufferView": 2, "componentType": 5125, "count": int(new_tris.size), "type": "SCALAR"}],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": len(pb)},
                {"buffer": 0, "byteOffset": len(pb), "byteLength": len(nb)},
                {"buffer": 0, "byteOffset": len(pb) + len(nb), "byteLength": len(ib)}],
            "buffers": [{"byteLength": len(pb) + len(nb) + len(ib)}],
        }
        out = OUT_DIR / f"seg_{bone}.glb"
        out.write_bytes(pack_glb(doc, pb + nb + ib))
        total += out.stat().st_size
        print(f"  {out.name:<24} {len(new_tris):6d} tris")
    print(f"wrote {len(SEGMENTS)} segments to {OUT_DIR.relative_to(ROOT)} ({total / 1e6:.2f} MB)")


if __name__ == "__main__":
    main()
