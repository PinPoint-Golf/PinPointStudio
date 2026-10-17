# PinPoint IMU Frame Contract

*The device-agnostic orientation boundary every IMU consumer depends on. A future
non-Witmotion IMU must deliver into this contract; nothing downstream depends on the
Witmotion device directly. Established by the Track-B work in
[`imu_rearchitecture.md`](../implementation/imu_rearchitecture.md) §3.2 / §5; this is the authoritative
reference, that doc is the rationale.*

---

## 1. The three frames

| Frame | Definition | Owner |
|---|---|---|
| **World** (`imu_world`) | Right-handed, **gravity-aligned, +Z up**. Yaw is **arbitrary-but-fixed per session** (re-zeroable via calibration `A`) — **NOT** magnetic-north referenced (we run 6-axis, no magnetometer). REP-103 ENU *vocabulary*, REP-145 *no-mag semantics*. | Host fusion (`orientation_filter.h` Madgwick / `eskf_orientation_filter.h`) |
| **Sensor body** (`imu_link`) | Right-handed, **+X = device-right, +Y = USB-end (long), +Z = face-normal**. Identity quaternion at the neutral pose. | Device characterization ([`imu_axis_reference.md`](../reference/imu_axis_reference.md)) |
| **Anatomical body** | Per-segment frame from `imu_calibration::solveSegment` (limbs) or `solveTrunkSegment` (pelvis, thorax). Axis tables in §5. | `imu_calibration.h` (`A`, `M`) |

The fused quaternion produced by host fusion **is** the world→sensor-body orientation in
this contract — the contract *names and owns* the frame host fusion already produces; it
does not re-rotate the pipeline.

## 2. Quaternion convention

**Scalar-first `(w, x, y, z)`** everywhere — matches `Qt QQuaternion(scalar,x,y,z)`,
Witmotion's `Q0..Q3`, and the stored `ImuSample`. ⚠ The `(w,x,y,z)` vs ROS `(x,y,z,w)`
scalar-last order is the single most likely silent bug in any ROS interop — use a named,
tested converter, never a blind 4-float copy.

## 3. Units

| Quantity | Unit |
|---|---|
| Acceleration | g |
| Angular velocity | °/s (`deg/s`) |
| Orientation | unit quaternion (never Euler in storage/compute; Euler only for UI labels) |
| Angles (internal / UI) | radians internally, degrees at the UI |

## 4. Stored sample frame — `imu_sample.h` (schema `imu_sample_v2`)

The 40-byte `ImuSample` written to the EventBuffer holds accel, gyro, **and** the
quaternion **all in the raw sensor body frame** (§1). `makeImuSample()` is the single
source of truth; the exporter writes the struct fields verbatim into swing.json `data[]`
in order `[accel_x,y,z, gyro_x,y,z, quat_w,x,y,z]`.

> **v1 → v2:** v1 stored the *vectors* in a display frame (`X→X, Z→Y, −Y→Z`) while the
> quaternion was un-remapped — the two described different frames in one struct. v2 drops
> the vector remap (the quaternion is authoritative). The schema string is a forensic
> marker only — nothing branches on it; swing.json IMU `data[]` is never reloaded.

## 5. Anatomical calibration — `q_anat = A · q_raw · M`

`imu_calibration::toAnatomical(A, q_raw, M)` is the single composition site (live write +
offline scored path). `M` = anatomical-body→sensor-body mount (constant per strap); `A` =
world→anatomical, solved per session so `q_anat` is identity at the captured reference
pose. The world-frame choice is absorbed entirely by `A`: redefining world by a global
`W` (`q_raw → W·q_raw`) and re-solving `A` leaves every `q_anat` and joint angle
unchanged.

### Per-segment anatomical axis labels (authoritative — resolves §7 #4)

All segments share the `solveSegment` construction (`e_x = flexion, e_y = long, e_z =
e_x × e_y`):

| Segment | **+X** | **+Y** | **+Z** |
|---|---|---|---|
| Forearm | flexion axis (medio-lateral) | long axis, distal (elbow→wrist) | `e_x × e_y` (anterior) |
| Hand | flexion axis (medio-lateral) | long axis, distal (wrist→knuckles) | `e_x × e_y` (dorsal) |
| Upper arm | flexion axis (medio-lateral) | long axis, distal (shoulder→elbow) | `e_x × e_y` (anterior / abduction) |

Anatomical "up" at the hanging reference pose is **−Y**.

### Trunk segments — pelvis and thorax (`solveTrunkSegment`, 17 Oct 2026)

The trunk does **not** use the limb construction. The limb solve makes the reference-pose
body frame the world (`A = conj(refRaw·M)`), so its world is Y-down and the reference
posture is zeroed. Every trunk consumer reads world **Z** as vertical, and the trunk's
posture is itself a measurement. So the trunk keeps a **gravity-aligned, Z-up,
stance-headed world**.

| Segment | **+X** | **+Y** | **+Z** |
|---|---|---|---|
| Pelvis (hip sensor, sacrum) | medio-lateral, toward the golfer's **right** | anterior | superior |
| Thorax (chest sensor, T2–T5) | medio-lateral, toward the golfer's **right** | anterior | superior |

Right-handed: right × anterior = up.

**Stance frame (the trunk world).**
- +Z is up (against gravity).
- +Y is the horizontal direction the golfer faced during stand tall (toward the ball).
- +X = Y × Z, which is the golfer's right.
- ⚠ For a right-hander, stance +X therefore points **away from** the target. The 4 Oct text said "toward the target"; that was wrong (design R3).
- Lead/trail and the handedness flip are owned by `pinpoint_sign_conventions.md`.

**The solve,** from a stand-tall still window (≥ 2 s; mean accel `f̄` and mean fused quaternion `q̄` in the sensor frame):
- `e_z` = `f̄` normalised. At rest the accelerometer reads +1 g along "up", so this is up in the sensor frame.
- `e_y` = the **inward** face normal, sensor **−Z** (the label faces out, §1), made orthogonal to `e_z`.
- `e_x` = `e_y × e_z`.
- `M = fromAxes(e_x, e_y, e_z)`, so that `M·v_anat = v_sensor`.
- `A = Rz(ψ)·L`:
  - **L** is the smallest rotation taking the filter's own up, `q̄·e_z`, onto +Z. It is about identity for Madgwick and about Rx(180°) for ESKF (§7). It is solved, never assumed.
  - **ψ** is the yaw about +Z taking `L·q̄·e_y` (horizontal by construction) onto +Y.
- At stand tall, `q_anat = A·q̄·M` is the identity. After that, `q_anat` is the true segment orientation in the stance frame: forward bend at address reads as forward bend, and world z is vertical for every consumer.

**The nominal mount** ("USB end up, label facing out", design §4.1), before the solve refines it:

| Segment axis | Sensor axis |
|---|---|
| +Z (superior) | ≈ +Y (USB end) |
| +Y (anterior) | ≈ −Z (into the body) |
| +X (right) | ≈ +X (device right) |

**What stand tall cannot give.**
- Mount yaw about the spine (δ) is assumed zero (tier δ0, σ_δ = 10°). It is refined later from the swings and the two-camera fit (design §6.5).
- The S-I rate and peak times do not depend on δ (design §6.2).

**Gates** (a failure names the sensor and the cause):
- long (USB) axis within 35° of up;
- face normal within 40° of horizontal;
- |f̄| = 1 g ± 2 %;
- mean gyro < 0.5 °/s on each axis;
- ≥ 95 % of the expected samples.

**Consumers that read this frame:**
- `body_rotation` turn: the heading of `q_anat·x̂`.
- `segment_rates` KS rate: the S-I rate, `gyro_segment · ẑ` (design D3).
- `phase_segmenter` transition: the axial rate about world Z through `q_anat` (design §8.5).
- The skeleton3d IMU term, whose heading-only ψ freedom is right for a Z-up world.

### Joint DOFs — read from the RELATIVE rotation (not the segment-axis names above)

⚠ The segment **+X = flexion axis** name (above) is NOT the axis a wrist DOF is *read on*.
The joint angles come from the relative-rotation matrix (`wrist_angles.h`), in a different
space:

| Joint (relative quaternion) | DOF | Read on | Sign convention |
|---|---|---|---|
| Wrist  `qFore⁻¹·qHand` | flexion / extension (bow/cup) | **Z** | + = bowed |
| Wrist  | radial / ulnar deviation (hinge) | **X** | + = ulnar |
| Wrist  | axial (hand-vs-forearm roll) | Y | drops out |
| Elbow  `qUpper⁻¹·qFore` | forearm pronation / supination (roll) | Y (swing-twist) | + = pronated |
| Elbow  | elbow flexion | swing magnitude | [0, π] |

The flexion(Z)/deviation(X) assignment is hardware-locked (2026-06); an earlier
"flexion about X" form had the two swapped — do not restore it.

## 6. World (Z-up) → Qt Quick3D scene (Y-up) basis change

Scene is Y-up, right-handed, −Z forward. The basis change is a fixed `Rx(−90°)`
(det +1, no handedness flip): world +Z→scene +Y, +Y→scene −Z, +X→+X. ⚠ This basis change
is **not** the whole rendered bone rotation — the avatar needs rest→*hanging*, so each
viz renderer folds the basis change into a per-GLB rest offset (`R0`). See §3.4c of
[`imu_rearchitecture.md`](../implementation/imu_rearchitecture.md); the explicit `worldToScene()` helper is
Phase 2 (not yet landed).

## 7. Fusion filter gotcha (both implement `IOrientationFilter`)

Madgwick and ESKF are runtime-swappable but use **different world conventions**: seeding
gravity `(0,0,1)`, Madgwick → identity, ESKF → 180°-about-X. They feed the *same* `A·q·M`,
so swapping filters **without re-calibrating** flips orientation by 180°; the per-session
re-solve of `A` absorbs it. Don't assume the two produce interchangeable raw quaternions.
(Frozen by `orientation_filter_test`.)

## 8. Code references

| Concern | File |
|---|---|
| Stored frame + `makeImuSample` | `src/Buffer/imu_sample.h` |
| `A·q·M` composition (`toAnatomical`) + segment solve | `src/IMU/imu_calibration.h` |
| Joint-angle extraction | `src/Analysis/wrist_angles.h` |
| Host fusion (frame source) | `src/IMU/orientation_filter.h`, `src/IMU/eskf_orientation_filter.h` |
| Characterization goldens | `src/Analysis/tests/` (`imu_sample_test`, `imu_calibration_test`, `wrist_angles_test`, `orientation_filter_test`, `imu_driver_frame_test`, `live_wrist_angles_test`) |
