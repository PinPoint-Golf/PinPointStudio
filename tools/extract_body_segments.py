#!/usr/bin/env python3
"""
extract_body_segments.py  —  split ybot.glb body bones into rigid segment GLBs.

Extends the arm-segment approach to the full skeleton.  Each bone's vertices
are extracted, transformed into bone-local space (joint at origin) using the
GLTF inverse-bind matrix so the joint sits at the origin, and written as a
standalone GLB.

For leaf-heavy bones (feet, head) child geometry is baked in — same technique
used for hand fingers in extract_arm_segments.py.

Also prints QML-ready bone world positions and parent-local offsets so the
Node hierarchy in BodyVizView.qml can be populated directly.

Bugs fixed vs. the arm script:
  • NO * 0.01 scale factor — ybot.glb stores positions in metres already
    (vertex Y range 0–1.8); the arm script comment "cm scale" was wrong.
    bind[:3,3] gives correct world positions directly (matches ArmVizView.qml).
  • Dominant-joint comparison fixed: np.argmax(w_arr) gives the weight-slot
    index (0–3), NOT the skin-joint index (0–64).  Must map through j_arr to
    get the actual joint index before comparing to `ji`.

Output (src/Resources/body/):
  body_Hips.glb   body_Spine.glb   body_Spine1.glb  body_Spine2.glb
  body_Head.glb
  body_LeftShoulder.glb   body_RightShoulder.glb
  body_LeftUpLeg.glb   body_LeftLeg.glb   body_LeftFoot.glb
  body_RightUpLeg.glb  body_RightLeg.glb  body_RightFoot.glb
  (Neck skipped if 0 triangles — no distinct skin geometry on Y-bot)
"""

import struct, json, sys, pathlib
import numpy as np
from pygltflib import GLTF2

GLB_PATH = pathlib.Path(__file__).parent.parent / "src/Resources/body/ybot.glb"
OUT_DIR  = GLB_PATH.parent

# ── Target bones — one GLB per entry ─────────────────────────────────────────
TARGET_BONES = [
    "mixamorig:Hips",
    "mixamorig:Spine",
    "mixamorig:Spine1",
    "mixamorig:Spine2",
    "mixamorig:Neck",
    "mixamorig:Head",
    "mixamorig:LeftShoulder",
    "mixamorig:RightShoulder",
    "mixamorig:LeftUpLeg",
    "mixamorig:LeftLeg",
    "mixamorig:LeftFoot",
    "mixamorig:RightUpLeg",
    "mixamorig:RightLeg",
    "mixamorig:RightFoot",
]

# Child bones baked into the parent GLB (geometry transformed to parent's
# bone-local space) — mirrors the FINGER_BONES hand approach.
BAKED_CHILDREN = {
    "mixamorig:LeftFoot":  ["mixamorig:LeftToeBase",  "mixamorig:LeftToe_End"],
    "mixamorig:RightFoot": ["mixamorig:RightToeBase", "mixamorig:RightToe_End"],
    "mixamorig:Head":      ["mixamorig:HeadTop_End"],
}

# Per-bone minimum number of triangle vertices that must have this bone as their
# DOMINANT influence (highest weight) before the triangle is included.
# Spine/shoulder bones have gradual weight transitions; threshold=1 allows any
# triangle where at least one vertex is dominated by this bone.
MAJORITY_THRESHOLD = {
    "mixamorig:Hips":          2,
    "mixamorig:Spine":         1,
    "mixamorig:Spine1":        1,
    "mixamorig:Spine2":        1,
    "mixamorig:Neck":          1,
    "mixamorig:Head":          2,
    "mixamorig:LeftShoulder":  1,
    "mixamorig:RightShoulder": 1,
}
DEFAULT_MAJORITY = 2   # for limb bones

# Kinematic parents — used only for the parent-local offset printout.
PARENT = {
    "mixamorig:Spine":          "mixamorig:Hips",
    "mixamorig:Spine1":         "mixamorig:Spine",
    "mixamorig:Spine2":         "mixamorig:Spine1",
    "mixamorig:Neck":           "mixamorig:Spine2",
    "mixamorig:Head":           "mixamorig:Neck",
    "mixamorig:LeftShoulder":   "mixamorig:Spine2",
    "mixamorig:RightShoulder":  "mixamorig:Spine2",
    "mixamorig:LeftUpLeg":      "mixamorig:Hips",
    "mixamorig:RightUpLeg":     "mixamorig:Hips",
    "mixamorig:LeftLeg":        "mixamorig:LeftUpLeg",
    "mixamorig:RightLeg":       "mixamorig:RightUpLeg",
    "mixamorig:LeftFoot":       "mixamorig:LeftLeg",
    "mixamorig:RightFoot":      "mixamorig:RightLeg",
}

# Uniform body colour — all segments use the same neutral tone.
SEGMENT_COLOR = {}
TORSO_COLOR = [0.38, 0.38, 0.42, 1.0]

# ── Helpers ───────────────────────────────────────────────────────────────────

def accessor_data(gltf, acc_idx):
    acc  = gltf.accessors[acc_idx]
    bv   = gltf.bufferViews[acc.bufferView]
    buf  = gltf.buffers[bv.buffer]
    data = gltf.get_data_from_buffer_uri(buf.uri)
    start = bv.byteOffset + (acc.byteOffset or 0)
    component_map = {5120: np.int8, 5121: np.uint8, 5122: np.int16,
                     5123: np.uint16, 5125: np.uint32, 5126: np.float32}
    dtype = component_map[acc.componentType]
    type_sizes = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4,
                  "MAT2": 4, "MAT3": 9, "MAT4": 16}
    n_comp = type_sizes[acc.type]
    stride = bv.byteStride if bv.byteStride else (np.dtype(dtype).itemsize * n_comp)
    if stride != np.dtype(dtype).itemsize * n_comp:
        item_bytes = np.dtype(dtype).itemsize * n_comp
        arr = np.array([
            np.frombuffer(data[start + i*stride : start + i*stride + item_bytes], dtype=dtype)
            for i in range(acc.count)
        ])
    else:
        raw = np.frombuffer(data[start : start + stride * acc.count], dtype=dtype)
        arr = raw.reshape(acc.count, n_comp) if n_comp > 1 else raw
    return arr.copy()

def pack_glb(json_dict, bin_data: bytes) -> bytes:
    json_bytes = json.dumps(json_dict, separators=(',', ':')).encode('utf-8')
    while len(json_bytes) % 4: json_bytes += b' '
    while len(bin_data)  % 4: bin_data  += b'\x00'
    json_chunk = struct.pack('<II', len(json_bytes), 0x4E4F534A) + json_bytes
    bin_chunk  = struct.pack('<II', len(bin_data),  0x004E4942) + bin_data
    total = 12 + len(json_chunk) + len(bin_chunk)
    return struct.pack('<III', 0x46546C67, 2, total) + json_chunk + bin_chunk

def bone_world_pos(ibm, ji):
    """World position of the joint = translation column of the bind matrix.
    No scale factor needed — ybot.glb vertex positions are already in metres."""
    bind = np.linalg.inv(ibm[ji])
    return bind[:3, 3]

def short(bone):
    return bone.replace("mixamorig:", "")

# ── Load GLB ──────────────────────────────────────────────────────────────────

gltf = GLTF2().load(str(GLB_PATH))
print(f"Nodes: {len(gltf.nodes)}  Meshes: {len(gltf.meshes)}  Skins: {len(gltf.skins)}")

node_name_to_idx = {n.name: i for i, n in enumerate(gltf.nodes) if n.name}
skin          = gltf.skins[0]
joint_indices = skin.joints
node_to_joint = {node_idx: ji for ji, node_idx in enumerate(joint_indices)}

# Resolve every requested bone to its skin-joint index.
all_bones = TARGET_BONES + [b for children in BAKED_CHILDREN.values() for b in children]
bone_joint = {}
print("\nBone → skin joint index:")
for bone in all_bones:
    ni = node_name_to_idx.get(bone)
    ji = node_to_joint.get(ni) if ni is not None else None
    print(f"  {short(bone):<25} node={str(ni):<5} {'ji='+str(ji) if ji is not None else 'NOT FOUND'}")
    if ji is not None:
        bone_joint[bone] = ji

# ── Read inverse-bind matrices ────────────────────────────────────────────────

ibm_raw   = accessor_data(gltf, skin.inverseBindMatrices)
n_joints  = len(joint_indices)
# Column-major 4×4 per GLTF spec → transpose each matrix to row-major.
ibm = ibm_raw.reshape(n_joints, 4, 4).transpose(0, 2, 1).astype(np.float64)

# World positions (metres, no scale factor — already confirmed against ArmVizView).
world_pos = {}
for bone in TARGET_BONES:
    ji = bone_joint.get(bone)
    if ji is not None:
        world_pos[bone] = bone_world_pos(ibm, ji)

# ── Find the skinned surface mesh ─────────────────────────────────────────────

chosen_mesh, chosen_prim = None, None
for mesh in gltf.meshes:
    for prim in mesh.primitives:
        if prim.attributes.JOINTS_0 is not None:
            if chosen_mesh is None or "Surface" in (mesh.name or ""):
                chosen_mesh = mesh
                chosen_prim = prim

if chosen_prim is None:
    sys.exit("No skinned primitive found.")
print(f"\nUsing mesh: '{chosen_mesh.name}'")

prim    = chosen_prim
pos_arr = accessor_data(gltf, prim.attributes.POSITION).astype(np.float64)
nrm_arr = (accessor_data(gltf, prim.attributes.NORMAL).astype(np.float64)
           if prim.attributes.NORMAL is not None else np.zeros_like(pos_arr))
j_arr   = accessor_data(gltf, prim.attributes.JOINTS_0).astype(np.int32)
w_arr   = accessor_data(gltf, prim.attributes.WEIGHTS_0)
idx_arr = accessor_data(gltf, prim.indices).flatten().astype(np.uint32)

w_ct = gltf.accessors[prim.attributes.WEIGHTS_0].componentType
if w_ct == 5121:   w_arr = w_arr.astype(np.float64) / 255.0
elif w_ct == 5123: w_arr = w_arr.astype(np.float64) / 65535.0
else:              w_arr = w_arr.astype(np.float64)

n_verts = len(pos_arr)
tris    = idx_arr.reshape(-1, 3)

# ── Dominant joint per vertex — actual skin-joint index, not weight-slot index.
# np.argmax(w_arr, axis=1) gives the slot (0-3) with the highest weight.
# j_arr[v, slot] maps that to the actual skin-joint index (0-64).
# This bug existed in the arm script's majority check (always silently bypassed
# because slot-index 0-3 never equalled skin-joint index 8+ for arm bones).
dominant_slot  = np.argmax(w_arr, axis=1)                   # shape (n_verts,) values 0-3
dominant_ji    = j_arr[np.arange(n_verts), dominant_slot]   # actual joint indices

print(f"Vertices: {n_verts}  Triangles: {len(tris)}")

# ── Extract segments ──────────────────────────────────────────────────────────

produced = []   # track which GLBs were actually written

for bone_name in TARGET_BONES:
    ji = bone_joint.get(bone_name)
    if ji is None:
        print(f"\nSKIP {short(bone_name)}: not found in skin")
        continue

    child_names = BAKED_CHILDREN.get(bone_name, [])
    child_jis   = [bone_joint[c] for c in child_names if c in bone_joint]
    all_jis     = np.array([ji] + child_jis, dtype=np.int32)

    threshold = MAJORITY_THRESHOLD.get(bone_name, DEFAULT_MAJORITY)

    dom0 = dominant_ji[tris[:, 0]]
    dom1 = dominant_ji[tris[:, 1]]
    dom2 = dominant_ji[tris[:, 2]]

    if child_jis:
        # Baked-children mode: any triangle with any vertex influenced by the set.
        has_any  = np.any(np.isin(j_arr, list(all_jis)), axis=1)
        sel_mask = has_any[tris[:, 0]] | has_any[tris[:, 1]] | has_any[tris[:, 2]]
        print(f"\n{short(bone_name)}: baked-children ({len(child_jis)} children)")
    else:
        # Dominant-majority mode: count vertices in each triangle whose dominant
        # joint (by highest weight) is this bone.
        dom_count = ((dom0 == ji).astype(int) +
                     (dom1 == ji).astype(int) +
                     (dom2 == ji).astype(int))
        sel_mask = dom_count >= threshold
        n_sel = sel_mask.sum()
        if n_sel == 0:
            # Fall back: any triangle with any weight reference to this bone.
            has_influence = np.any(j_arr == ji, axis=1)
            sel_mask = has_influence[tris[:, 0]] | has_influence[tris[:, 1]] | has_influence[tris[:, 2]]
            print(f"\n{short(bone_name)}: dominant≥{threshold} gave 0 — using any-influence fallback")
        else:
            print(f"\n{short(bone_name)}: dominant≥{threshold}")

    sel_tris = tris[sel_mask]
    print(f"  (ji={ji}): {len(sel_tris)} triangles selected")

    if len(sel_tris) == 0:
        print(f"  SKIP: no triangles — Neck may have no distinct geometry on Y-bot")
        continue

    used_verts  = np.unique(sel_tris)
    old_to_new  = np.full(n_verts, -1, dtype=np.int32)
    old_to_new[used_verts] = np.arange(len(used_verts))
    new_tris    = old_to_new[sel_tris].astype(np.uint32)

    raw_pos = pos_arr[used_verts]
    raw_nrm = nrm_arr[used_verts]
    ones    = np.ones((len(raw_pos), 1))
    pos4    = np.hstack([raw_pos, ones])
    new_pos = (ibm[ji] @ pos4.T).T[:, :3].astype(np.float32)

    nrm_mat = np.linalg.inv(ibm[ji][:3, :3]).T
    new_nrm  = (nrm_mat @ raw_nrm.T).T.astype(np.float32)
    lengths  = np.linalg.norm(new_nrm, axis=1, keepdims=True)
    lengths[lengths == 0] = 1.0
    new_nrm /= lengths

    print(f"  → {len(used_verts)} vertices, "
          f"local centre ({new_pos.mean(0)[0]:.3f}, {new_pos.mean(0)[1]:.3f}, {new_pos.mean(0)[2]:.3f})")

    color     = SEGMENT_COLOR.get(bone_name, TORSO_COLOR)
    pos_bytes = new_pos.tobytes()
    nrm_bytes = new_nrm.tobytes()
    idx_bytes = new_tris.flatten().astype(np.uint32).tobytes()
    bin_data  = pos_bytes + nrm_bytes + idx_bytes

    glb_json = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "materials": [{"name": f"{short(bone_name)}Mat", "pbrMetallicRoughness": {
            "baseColorFactor": color,
            "metallicFactor": 0.05,
            "roughnessFactor": 0.60
        }}],
        "meshes": [{"name": bone_name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1},
            "indices": 2, "mode": 4, "material": 0
        }]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(new_pos),
             "type": "VEC3",
             "min": new_pos.min(axis=0).tolist(),
             "max": new_pos.max(axis=0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": len(new_nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": int(new_tris.size), "type": "SCALAR"}
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0,                               "byteLength": len(pos_bytes)},
            {"buffer": 0, "byteOffset": len(pos_bytes),                  "byteLength": len(nrm_bytes)},
            {"buffer": 0, "byteOffset": len(pos_bytes) + len(nrm_bytes), "byteLength": len(idx_bytes)}
        ],
        "buffers": [{"byteLength": len(bin_data)}]
    }

    safe = bone_name.replace("mixamorig:", "body_")
    out  = OUT_DIR / f"{safe}.glb"
    out.write_bytes(pack_glb(glb_json, bin_data))
    print(f"  Wrote {out.name}  ({out.stat().st_size:,} bytes)")
    produced.append(safe)

# ── QML node position table ───────────────────────────────────────────────────

print("\n" + "─" * 72)
print("World positions (metres, Y-up) — use directly as Node.position in QML:")
print("─" * 72)
for bone in TARGET_BONES:
    if bone not in world_pos:
        continue
    p = world_pos[bone]
    print(f"  {short(bone)+':':<24} Qt.vector3d({p[0]:8.4f}, {p[1]:8.4f}, {p[2]:8.4f})")

print("\nParent-local offsets (child.world − parent.world):")
print("─" * 72)
for bone in TARGET_BONES:
    parent = PARENT.get(bone)
    if not parent:
        if bone in world_pos:
            p = world_pos[bone]
            print(f"  {short(bone)+':':<24} ROOT  world=({p[0]:.4f}, {p[1]:.4f}, {p[2]:.4f})")
        continue
    if bone not in world_pos or parent not in world_pos:
        print(f"  {short(bone)+':':<24} (missing world pos — skipped)")
        continue
    off = world_pos[bone] - world_pos[parent]
    print(f"  {short(bone)+':':<24} Qt.vector3d({off[0]:8.4f}, {off[1]:8.4f}, {off[2]:8.4f})")

print(f"\nProduced {len(produced)} GLB files: {', '.join(produced)}")
print("Done.")
