#!/usr/bin/env python3
"""
extract_arm_segments.py  —  split ybot.glb arm bones into rigid segment GLBs.

Extracts triangles from the Alpha_Surface mesh whose vertices are majority-
influenced by each target arm bone, then transforms them into bone-local space
using the GLTF inverse-bind matrix so the joint sits at the origin.

Also prints QML-ready bone world positions (translation part of bind matrix,
in 0.01-scale Y-bot model space) so the QML hierarchy can be placed correctly.

Output (src/Resources/body/):
  arm_LeftArm.glb  arm_LeftForeArm.glb  arm_LeftHand.glb
  arm_RightArm.glb arm_RightForeArm.glb arm_RightHand.glb
"""

import struct, json, sys, pathlib
import numpy as np
from pygltflib import GLTF2

GLB_PATH = pathlib.Path(__file__).parent.parent / "src/Resources/body/ybot.glb"
OUT_DIR  = GLB_PATH.parent

TARGET_BONES = [
    "mixamorig:LeftArm",
    "mixamorig:LeftForeArm",
    "mixamorig:LeftHand",
    "mixamorig:RightArm",
    "mixamorig:RightForeArm",
    "mixamorig:RightHand",
]

# Finger/thumb joints whose geometry is baked into the hand GLB in rest pose.
# They are not extracted as separate files — the hand segment just includes them
# all, transformed by the hand bone's inverse-bind matrix so they sit at the
# correct wrist-relative positions and follow the hand Node when it rotates.
FINGER_BONES = {
    "mixamorig:LeftHand": [
        f"mixamorig:LeftHand{d}{n}"
        for d in ("Thumb", "Index", "Middle", "Ring", "Pinky") for n in (1, 2, 3, 4)
    ],
    "mixamorig:RightHand": [
        f"mixamorig:RightHand{d}{n}"
        for d in ("Thumb", "Index", "Middle", "Ring", "Pinky") for n in (1, 2, 3, 4)
    ],
}

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

# ── load ───────────────────────────────────────────────────────────────────────

gltf = GLTF2().load(str(GLB_PATH))
print(f"Nodes: {len(gltf.nodes)}  Meshes: {len(gltf.meshes)}  Skins: {len(gltf.skins)}")

node_name_to_idx = {n.name: i for i, n in enumerate(gltf.nodes) if n.name}
skin = gltf.skins[0]
joint_indices = skin.joints
node_to_joint = {node_idx: ji for ji, node_idx in enumerate(joint_indices)}

print("\nArm bone → node_idx → skin_joint_idx:")
arm_joints = {}
for bone in TARGET_BONES:
    ni = node_name_to_idx.get(bone)
    ji = node_to_joint.get(ni) if ni is not None else None
    print(f"  {bone}: node={ni}  skin_ji={ji}")
    if ji is not None:
        arm_joints[bone] = ji

# Resolve finger joint indices (used only for vertex selection, not GLB output).
finger_joint_indices = {}   # bone_name → joint_index
for hand_bone, fingers in FINGER_BONES.items():
    for fb in fingers:
        ni = node_name_to_idx.get(fb)
        ji = node_to_joint.get(ni) if ni is not None else None
        if ji is not None:
            finger_joint_indices[fb] = ji

# ── read inverse bind matrices ─────────────────────────────────────────────────

ibm_raw = accessor_data(gltf, skin.inverseBindMatrices)
# Shape: (n_joints, 16) — column-major 4x4 per GLTF spec
# Reshape to (n_joints, 4, 4), then transpose each matrix (column→row major)
n_joints = len(joint_indices)
ibm = ibm_raw.reshape(n_joints, 4, 4).transpose(0, 2, 1).astype(np.float64)
# ibm[ji] is now a row-major 4x4 that transforms model-space → bone-rest space

def bone_world_pos(ji):
    """World position of bone joint = translation part of bind matrix (inv of inv-bind)."""
    bind = np.linalg.inv(ibm[ji])
    return bind[:3, 3]

print("\nBone world positions (model space, Y-up, scale 1.0):")
bone_positions = {}
for bone, ji in arm_joints.items():
    pos = bone_world_pos(ji)
    bone_positions[bone] = pos
    print(f"  {bone} (ji={ji}): x={pos[0]:.4f}  y={pos[1]:.4f}  z={pos[2]:.4f}")

# ── find the surface (skin) mesh — prefer Alpha_Surface ───────────────────────

chosen_mesh = None
chosen_prim = None
for mesh in gltf.meshes:
    for prim in mesh.primitives:
        if prim.attributes.JOINTS_0 is not None:
            if chosen_mesh is None or "Surface" in (mesh.name or ""):
                chosen_mesh = mesh
                chosen_prim = prim

if chosen_prim is None:
    sys.exit("No skinned primitive found.")

print(f"\nUsing mesh: '{chosen_mesh.name}'")

prim = chosen_prim
pos_arr = accessor_data(gltf, prim.attributes.POSITION).astype(np.float64)
nrm_arr = (accessor_data(gltf, prim.attributes.NORMAL).astype(np.float64)
           if prim.attributes.NORMAL is not None else np.zeros_like(pos_arr))
j_arr   = accessor_data(gltf, prim.attributes.JOINTS_0).astype(np.int32)
w_arr   = accessor_data(gltf, prim.attributes.WEIGHTS_0)
idx_arr = accessor_data(gltf, prim.indices).flatten().astype(np.uint32)

# Normalise weights to float in [0,1] regardless of component type
w_componentType = gltf.accessors[prim.attributes.WEIGHTS_0].componentType
if w_componentType == 5121:   # UNSIGNED_BYTE
    w_arr = w_arr.astype(np.float64) / 255.0
elif w_componentType == 5123:  # UNSIGNED_SHORT
    w_arr = w_arr.astype(np.float64) / 65535.0
else:
    w_arr = w_arr.astype(np.float64)

n_verts = len(pos_arr)
n_tris  = len(idx_arr) // 3
print(f"Vertices: {n_verts}  Triangles: {n_tris}")
print(f"WEIGHTS_0 componentType: {w_componentType}")

# Dominant joint per vertex (highest weight wins)
dominant_joint = np.argmax(w_arr, axis=1)
print(f"\nDominant joint values — unique count: {len(np.unique(dominant_joint))}")
for bone, ji in arm_joints.items():
    n_dom = np.sum(dominant_joint == ji)
    print(f"  {bone} (ji={ji}): dominant in {n_dom} vertices")

# ── extract segments ───────────────────────────────────────────────────────────

tris = idx_arr.reshape(-1, 3)

for bone_name in TARGET_BONES:
    ji = arm_joints.get(bone_name)
    if ji is None:
        print(f"\nSKIP {bone_name}: not in skin")
        continue

    # For hand bones: include all finger/thumb vertices (baked in rest pose).
    # Build the full joint-index set for this segment.
    extra_jis = np.array(
        [fji for fb, fji in finger_joint_indices.items()
         if fb in FINGER_BONES.get(bone_name, [])],
        dtype=np.int32
    )
    all_jis = np.concatenate([[ji], extra_jis]) if extra_jis.size else np.array([ji])

    dom0 = dominant_joint[tris[:, 0]]
    dom1 = dominant_joint[tris[:, 1]]
    dom2 = dominant_joint[tris[:, 2]]

    if extra_jis.size:
        # Hand bone: select any triangle where ≥1 vertex has ANY weight slot
        # referencing any joint in the hand+finger set.  (dominant_joint gives
        # weight-slot ordinals 0-3, not skin joint indices — check j_arr instead.)
        has_any = np.any(np.isin(j_arr, list(all_jis)), axis=1)
        sel_mask = has_any[tris[:, 0]] | has_any[tris[:, 1]] | has_any[tris[:, 2]]
        print(f"\n{bone_name}: hand+finger mode — {extra_jis.size} finger joints included")
    else:
        has_bone = np.any(j_arr == ji, axis=1)
        majority_mask = ((dom0 == ji).astype(int) + (dom1 == ji).astype(int) +
                         (dom2 == ji).astype(int)) >= 2
        tri_mask  = has_bone[tris[:, 0]] | has_bone[tris[:, 1]] | has_bone[tris[:, 2]]
        sel_mask  = tri_mask & majority_mask
        if sel_mask.sum() == 0:
            print(f"\n{bone_name}: majority filter gave 0 — using any-reference fallback")
            sel_mask = tri_mask

    sel_tris = tris[sel_mask]
    print(f"\n{bone_name} (ji={ji}): {len(sel_tris)} triangles selected")

    if len(sel_tris) == 0:
        print(f"  SKIP: no triangles found even with fallback")
        continue

    used_verts = np.unique(sel_tris)
    old_to_new = np.full(n_verts, -1, dtype=np.int32)
    old_to_new[used_verts] = np.arange(len(used_verts))
    new_tris = old_to_new[sel_tris].astype(np.uint32)

    # Transform vertices to bone-local space: v_local = ibm[ji] @ v_model (homogeneous)
    raw_pos = pos_arr[used_verts]          # (N, 3) model-space
    raw_nrm = nrm_arr[used_verts]          # (N, 3)
    ones     = np.ones((len(raw_pos), 1))
    pos4     = np.hstack([raw_pos, ones])  # (N, 4)
    new_pos  = (ibm[ji] @ pos4.T).T[:, :3].astype(np.float32)

    # Normals use the normal matrix (inverse-transpose of the 3x3 upper left)
    nrm_mat = np.linalg.inv(ibm[ji][:3, :3]).T
    new_nrm  = (nrm_mat @ raw_nrm.T).T.astype(np.float32)
    # Renormalise
    lengths = np.linalg.norm(new_nrm, axis=1, keepdims=True)
    lengths[lengths == 0] = 1.0
    new_nrm /= lengths

    print(f"  → {len(used_verts)} vertices  "
          f"local-space centre: ({new_pos.mean(0)[0]:.3f}, {new_pos.mean(0)[1]:.3f}, {new_pos.mean(0)[2]:.3f})")

    pos_bytes = new_pos.tobytes()
    nrm_bytes = new_nrm.tobytes()
    idx_bytes = new_tris.flatten().astype(np.uint32).tobytes()
    bin_data  = pos_bytes + nrm_bytes + idx_bytes

    # Uniform body colour — matches all other body segments.
    base_color = [0.38, 0.38, 0.42, 1.0]

    glb_json = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "materials": [{"name": "ArmMat", "pbrMetallicRoughness": {
            "baseColorFactor": base_color,
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
             "min": new_pos.min(axis=0).tolist(), "max": new_pos.max(axis=0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": len(new_nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": int(new_tris.size), "type": "SCALAR"}
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0,                          "byteLength": len(pos_bytes)},
            {"buffer": 0, "byteOffset": len(pos_bytes),             "byteLength": len(nrm_bytes)},
            {"buffer": 0, "byteOffset": len(pos_bytes)+len(nrm_bytes), "byteLength": len(idx_bytes)}
        ],
        "buffers": [{"byteLength": len(bin_data)}]
    }

    safe = bone_name.replace("mixamorig:", "arm_")
    out  = OUT_DIR / f"{safe}.glb"
    out.write_bytes(pack_glb(glb_json, bin_data))
    print(f"  Wrote {out.name}  ({out.stat().st_size:,} bytes)")

print("\n── QML Node positions (Y-bot scale 0.01, Y-up) ──")
for bone, ji in arm_joints.items():
    pos = bone_positions[bone] * 0.01   # Y-bot exports at cm scale
    print(f"  {bone.replace('mixamorig:','')}: Qt.vector3d({pos[0]:.4f}, {pos[1]:.4f}, {pos[2]:.4f})")

print("\nDone.")
