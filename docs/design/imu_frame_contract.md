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
| **Anatomical body** | Per-segment frame from `imu_calibration::solveSegment`. Axis table in §4. | `imu_calibration.h` (`A`, `M`) |

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
