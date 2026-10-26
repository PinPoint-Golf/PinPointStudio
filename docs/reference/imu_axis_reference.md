# WT901BLE67 IMU — Axis Reference & Known State

**Authoritative reference** for the Witmotion WT901BLE67 sensors used by PinPoint Studio: the enforced connect
state, the sensor→case axis mapping, the orientation-fusion convention, and the cross-device consistency
result. Established from first-principles desk characterization (6 cube faces + 3 gyro spins) on all three
physical units (2026-05-30). When in doubt about an axis or convention, this file wins over inline comments.

## 1. Enforced connect state (runtime-only, no flash writes)

Every device is forced into one known state on **each** connect, in `WT9011DCL_BLE::initializeDevice()`:

| Register | Value | Meaning | Verified |
|----------|-------|---------|----------|
| `RegOrient` (0x23) | `0x0001` | Vertical installation | readback logged `[orient]` |
| `RegAxis6` (0x24)  | `0x0001` | 6-axis fusion (gyro+accel, no magnetometer) | readback logged `[axis6]` |
| `RegRRate` (0x03)  | rate     | Output rate (default 100 Hz) | — |

These are **runtime-only — we never `SAVE` to the device's flash.** The device's persisted (flash) config is
the **WitMotion app's** domain (e.g. accel/gyro bias `CALSW=0x01` is a 6-point calibration done in that app,
documented in the user manual — never performed in-app; a flat-still + SAVE attempt corrupted sensors once).
So a unit's saved config may differ (one shipped 9-axis), but at runtime PinPoint Studio always overrides to
vertical + 6-axis, and now confirms both via readback.

## 2. Sensor → case axis map (intrinsic to the device)

Measured from gravity (raw accelerometer) with the device resting on each face. Landmarks: the **labelled
face**, the **USB connector** end, and left/right with *face toward you, USB pointing down* (the label's
printing then reads the right way up). An accelerometer at rest reads **+1 g on the axis pointing UP** (the
reaction to gravity), so "USB_DOWN → +1 g Y" means +Y points up when the USB end is down.

| Sensor axis | Physical direction (device case) | Verify position → reading |
|-------------|----------------------------------|---------------------------|
| **+X** | **your RIGHT** with face toward you, USB **down** (= your LEFT with USB up) | RIGHT_DOWN → +1 g X |
| **+Y** | **away from the USB-connector end** (long axis) | USB_DOWN → +1 g Y |
| **+Z** | **out of the labelled face** (face-normal) | FACE_UP → +1 g Z |

> **Correction, 26 Oct 2026.** Until this date the table said +X = your right *with USB up* and +Y =
> *toward* the USB end. The measured column never changed and contradicts those words: under the same
> reading that gives FACE_UP → +1 g Z ⇒ +Z out of the face, USB_DOWN → +1 g Y puts +Y up when the USB end is
> down, i.e. **away from the USB end**, and RIGHT_DOWN → +1 g X puts +X on the side that is up, i.e. the
> side **opposite** the right-hand edge (with USB up) that was put down. Live confirmation on the worn trunk
> sensors (26 Oct): held USB-up, label out, the accelerometer put sensor +Y 166° from up; flipped USB-down
> (label then reading the right way up), stand tall passed at 13° / 26°. The arm nominal agrees too:
> watch-style on the lead forearm (face out, USB forward, arm hanging) the measured gravity puts +X distal
> and abduction about sensor −Y, which is only possible if +Y points away from the USB end. Only the words
> were wrong; every solve (`solveTrunkSegment`, `nominalArmMount`, `ImuInstance::longAxisFromUpDeg`) works
> from the measured axes and is unchanged. **Right-handedness of the corrected map:** face toward you, USB
> down — +X = your right, +Y = up, +Z = toward you; right × up = toward you, so X × Y = Z. (The old words
> flipped X and Y together, a 180° turn about Z, which is why they also looked right-handed.)

Right-handed. The **gyroscope axes coincide with the accelerometer axes** (same chip triad): a rotation
about case +X/+Y/+Z reads on gyro +X/+Y/+Z. Confirmed by spins: SPIN_FACE→Z, SPIN_LONG(long/USB axis)→Y,
SPIN_LAT(left↔right)→X.

## 3. Orientation fusion convention

Orientation is **not** taken from the device's on-board Euler output (proved non-rigid). It is fused locally
from raw gyro+accel by a Madgwick 6-axis filter (`src/IMU/orientation_filter.h`) in
`WT9011DCL_Base::dispatchCombinedPacket()`. Convention: **world +Z = up** (gravity reaction); roll/pitch are
observable from gravity, **yaw is free (drifts)** — manage drift by re-referencing at use, not by trusting
absolute heading. `eulerToQuat()` in the drivers is **legacy/unused** for the 0x61 combined frame (kept only
for the old 0x55/0x53 serial path and human-readable Euler labels).

## 4. Cross-device consistency — PASS

All three units (`DC:78:5B:33:7A:80`, `C6:05:20:9D:04:76`, `DC:51:F8:AE:C7:CC`) reported the **same**
sensor→case map; per-position gravity vectors agreed across devices to **Δ ≈ 0.4–2.6°** (within sensor
noise). The units are therefore **interchangeable**: strapped the same way, they present the same axis frame,
so a single set of calibration constants applies to all three.

Note: the desk map above is the sensor↔**case** relationship. The anatomical mounting term `M` in
`src/IMU/imu_calibration.h` (`nominalArmMount`, `nominalHandMount`) is a separate **strap-geometry** layer
(sensor↔limb-segment, watch-style: face out, USB forward — so sensor +Y points backward, §2). The desk result validates that all three devices
share the same sensor↔case frame, so the existing per-segment nominals are valid across all units; it does
**not** by itself re-derive `M` (that depends on how the case sits on the limb — see `.claude/calibration.md`
and `project_imu_calibration_transform`).

## 5. Re-running the characterization

Temporary scaffolding (now removed) drove a 9-step capture in the wizard's Confirm-panel diagnostic
(`diagWrap`): 6 static rest positions (FACE_UP/DOWN, USB_DOWN/UP, LEFT/RIGHT_DOWN) + 3 spins
(turntable/face-normal, pencil-roll/long, wheel-tumble/left-right), each `beginRawDump→endRawDump` to
`~/pinpoint_imu_raw.log`, tagged per device. Connect **one** device at a time. Analysis: average raw accel
per static position (axis map + cross-device consistency); PCA of the gyro stream per spin (gyro axis map).
