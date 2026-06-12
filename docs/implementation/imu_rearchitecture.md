# IMU Device-Layer Rearchitecture — Findings & Implementation Plan

*Status: investigation complete, plan proposed (not yet started). PinPoint Studio @ `c48aed7`.
All `file:line` citations are against the repo root. The current-state facts were produced by
a parallel multi-agent investigation and **independently re-verified against source** for the
load-bearing claims (the fusion source, the storage frame split, the 6-axis register write,
the wrist-angle math, the calibration model).*

Companion docs: [`imu_axis_reference.md`](../reference/imu_axis_reference.md),
[`wt901ble67_protocol.md`](../reference/wt901ble67_protocol.md),
[`shot_analyzer_m1_wrist.md`](shot_analyzer_m1_wrist.md), [`wristcalibration.md`](../user/wristcalibration.md),
[`wristmetrics.md`](../reference/wristmetrics.md), and the source PDF
`~/Documents/Witmotion/WT9011DCL-BT50 Communication Protocol.pdf`.

---

## 0. Executive summary — read this first

The investigation **changes the premise of the task in three important ways.** None of this
invalidates the goal; it sharpens it.

1. **There is no `0x67` quaternion stream.** Neither the protocol PDF nor the codebase contains a
   `0x67` frame, and there is **no continuous quaternion auto-output frame at all**. The device's
   native quaternion (registers `0x51–0x54`, Q0=w…Q3=z, each `/32768`) is only obtainable by an
   on-demand **register *read*** (`FF AA 27 51 00` → a `0x55 0x71` response). The auto-stream
   content register `AGPVSEL (0x96)` only toggles *accel+gyro+angle* ⟷ *displacement+speed+angle* —
   neither includes quaternions. (A `0x59` quaternion *frame* is hinted at by the protocol PDF's §2.8 checksum
   formula and exists in our code as a serial-only enum, but is never on the BLE67 path.)

2. **We are not using the device's orientation today — and that was a deliberate, evidence-based
   choice.** The quaternion that feeds metrics, viz, and the EventBuffer is computed **host-side**
   by a 6-axis Madgwick filter (or ESKF), from the raw accel+gyro of the default `0x61` frame
   (`wt9011dcl_base.cpp:250`). The device's *own* on-board orientation was tried and **rejected as
   non-rigid** — the in-code comment records joint rotation axes "~15–50° off the truth"
   (`wt9011dcl_base.cpp:230-238`). So "switch to device-native quaternions" risks **reintroducing a
   problem we already solved**; it must be treated as a hardware-gated experiment, not a given.
   *(This also means CLAUDE.md's "the WT901BLE67 hardware delivers quaternions natively — use them
   directly" is **factually wrong** for the live path and should be corrected — see §8.)*

3. **9-axis mode is the wrong default for the *hand/forearm* sensors — but the decision is
   per-sensor, not all-or-nothing.** 9-axis buys a magnetometer-referenced *absolute* heading. Near
   the steel club (hand/forearm), the field is dominated by a hard-iron-like offset plus a
   continuously-changing soft-iron component as the club reorients — net heading there is
   untrustworthy. The **upper-arm sensor sits ~50–70 cm from the grip**, where the disturbance is
   ~1/r³ weaker, so a 9-axis heading *there* is the most defensible — a **hybrid (9-axis upper-arm +
   6-axis hand/forearm)** is a live option to test under Track C, not something to dismiss. The
   default recommendation is 6-axis: it pins roll/pitch to gravity (drift-free) and lets yaw drift,
   and the headline metrics are **relative quaternions between co-located segments where common-mode
   yaw cancels** over a ~1 s swing. ⚠ But common-mode cancellation only handles the *easy* half —
   the **differential** yaw drift between three sensors with independent gyro biases does *not*
   cancel and is the documented ~10–15° FE↔RUD cross-talk that limits RUD accuracy. See §3.3 and §7.

**What *is* solidly worth doing — and is the real content of goal #2 — is making the world
orientation a contract PinPoint owns, instead of one implied by the Witmotion device.** That
decouples every consumer from the hardware and is the keystone that lets a future non-Witmotion IMU
drop in. The good news: the frame the host fusion already produces (gravity-up, arbitrary-but-fixed
yaw) **is** the right target frame — we mostly have to *name it, document it, fix one latent
inconsistency, and make the scene conversion explicit*, not physically re-rotate the pipeline.

### Recommended scope (what the plan below delivers)

| Track | Description | Recommendation |
|---|---|---|
| **A — Test harness first** | Golden/characterization tests capturing today's quaternion→angle behaviour as a frozen reference. | **Do first, unconditionally.** Nothing else is safe without it. |
| **B — Frame contract** | Define & document an explicit, device-agnostic boundary frame (ROS REP-103 *vocabulary*, REP-145 "no-mag" *semantics*). Fix the accel/gyro-vs-quaternion storage split. Make the Qt Quick3D conversion explicit. Version the `ImuSample` schema. | **Do.** This is the high-value, low-risk core of the request. |
| **C — Device-native quaternions / 9-axis** | Add a register-poll native-quaternion path behind a feature flag; A/B it against host fusion on hardware; 9-axis experiment. | **Optional, hardware-gated, your call.** The evidence (point 2 & 3) argues against it for this app. Isolated so it never blocks Tracks A/B. |

**The one decision I need from you** is whether Track C is in or out of scope (see §6.4 and the
question at the end). Tracks A and B proceed regardless.

---

## 1. What the code does today (verified against source)

### 1.1 The live orientation data path

```
BLE notify (0x55 0x61 frame: accel+gyro+Euler, little-endian int16)
  → BleImuTransport::dataReceived            ble_imu_transport.cpp:413
  → WT9011DCL_Base::dispatchCombinedPacket    wt9011dcl_base.cpp (0x61 handler)
       ├─ parses device Euler  → emit eulerAnglesUpdated   (DISPLAY LABELS ONLY)
       └─ m_quat = fuseRawImu(accel, gyro)  ← HOST FUSION   wt9011dcl_base.cpp:250
                    → IOrientationFilter::update()           imu_base.cpp:61
                       (default MadgwickFilter, imu_base.cpp:27; or ESKF, runtime-swappable)
          → emit quaternionUpdated(m_quat)
  → ImuInstance lambda (DirectConnection, BLE thread)        imu_instance.cpp:263
       ├─ m_anatQuat = A · q_raw · M   (if calibrated)        imu_instance.cpp:212-215
       └─ writes pinpoint::ImuSample → EventBuffer ring       imu_instance.cpp:271-281
```

- **The orientation source is host-side fusion, not the device.** `fuseRawImu()` runs the active
  `IOrientationFilter`. The device's own Euler output is emitted only to drive the roll/pitch/yaw
  *text labels*; the in-code comment (`wt9011dcl_base.cpp:230-238`) explains the on-board fusion was
  found non-rigid (axes 15–50° off) and is intentionally bypassed.
- **The host filter stack is live, not dead code.** `MadgwickFilter` is the default
  (`imu_base.cpp:27`); `ESKF` is selectable at runtime via Settings → IMUs
  (`ImusPanel.qml` → `imu_manager.cpp:351` → `imu_instance.cpp:355`). `eulerToQuat()` and
  `parseQuaternion()` (the native `0x59`/`0x51` decoders) are present but **unreachable on the BLE67
  path** (`wt9011dcl_base.cpp:164-168`).
- **The device is in 6-axis mode.** `initializeDevice()` writes `sendCommand(RegAxis6, 0x0001)`
  (= 6-axis, relative heading) on every connect and after every rate change
  (`wt9011dcl_ble.cpp:122`), readback-verified (`wt9011dcl_base.cpp:383`). *Aside:* there is a stray
  `AppSettings` key `imuDefaultFusionMode = "9axis"` (`app_settings.h:178`) that does **not** drive
  the wire — dead config.

### 1.2 The three frames in play today

| Frame | Definition | Where |
|---|---|---|
| **Fusion-world** | Right-handed, **+Z = up** (gravity reaction). Yaw **unobservable / drifting** (no mag, no North). | Madgwick seed rotates world-up `(0,0,1)` onto measured gravity (`orientation_filter.h:69-71`); ESKF gravity `[0,0,−9.81]`. |
| **Sensor/case body** | Right-handed, **+X = device-right, +Y = USB-end (long), +Z = face-normal**. | Desk characterization, `imu_axis_reference.md §3`. |
| **Anatomical body** (post `A·q·M`) | Right-handed, **+Y = long axis (distal), +X = flexion axis, +Z = e_x×e_y**. Anatomical "up" = `−Y`. | `imu_calibration.h:121-129`; `imu_instance.cpp:577`. |

### 1.3 The latent frame split (a real bug to fix in Track B)

In the `ImuSample` written to the ring, **accel/gyro are remapped to a "display" frame but the
quaternion is stored un-remapped** (`imu_instance.cpp:271-281`):

```cpp
// sensor X → X, sensor Z → Y, −sensor Y → Z   (accel & gyro remapped)
s.accel_x =  a.x; s.accel_y =  a.z; s.accel_z = -a.y;
s.gyro_x  =  g.x; s.gyro_y  =  g.z; s.gyro_z  = -g.y;
s.quat_w  =  q.w; s.quat_x  =  q.x; s.quat_y  =  q.y; s.quat_z  =  q.z;   // NOT remapped
```

So within one struct, the vectors and the quaternion describe **different frames**. The
`imu_sample.h:27-28` doc-comment ("display/world frame") is only true of the vectors. This is
harmless today only because no consumer cross-references the quaternion against the accel/gyro in the
same sample — but it is a trap, and exactly the sort of thing a frame-contract migration must
resolve.

### 1.4 The wrist-angle math (the fragile core — and why it's actually robust)

`wrist_angles.h` extracts joint angles from **relative anatomical quaternions** `qProx⁻¹·qDist`:

- Flexion/extension about **Z**: `atan2(-R(0,1), R(1,1))` (+ = bowed) — `wrist_angles.h:118`.
- Radial/ulnar about **X**: `asin(R(2,1))` (+ = ulnar) — `wrist_angles.h:119`.
- Pronation = Y-axis swing-twist; elbow flexion = swing magnitude — `wrist_angles.h:136-143`.
- `leftArm` is `Q_UNUSED` — the left-handed-golfer mirror is **not implemented / not verified**
  (`wrist_angles.h:123,146`).
- Documented residual: **~10–15° FE↔RUD cross-talk** from unobservable inter-sensor yaw
  (`wrist_angles.h:42-44`).

⚠ **Easily-misread comment (clarify, don't "fix"):** the header (`wrist_angles.h:33`) defines the
per-segment *anatomical frame* ("+X = flexion axis") — a *correct* statement — but it sits right next
to the *joint-DOF extraction*, which reads flexion on **Z** (line 118, hardware-locked 2026-06). Both
are true (segment-axis definition vs. the axis the wrist DOF actually rotates about in the relative
quaternion); the danger is misreading one as the other. Track A clarifies the distinction in-comment;
it does **not** delete the (true) segment-frame line.

> **Note for any future migrator:** the shipped extraction (`wrist_angles.h:117-119`) is a plain
> rotation-matrix Cardan — it has **no swing-twist preamble**, unlike the snippet in
> `shot_analyzer_m1_wrist.md §4`. Axial-Y still drops out (verified by tests). Do not "restore" the
> swing-twist form from the M1 doc; it would silently change the shipped values.

**Key insight that de-risks the whole migration:** these angles are computed from *relative*
quaternions, and the world-frame choice is absorbed entirely by the calibration term `A`. If we
redefine the world frame by a global rotation `W` (`q_raw → W·q_raw`), then re-solving `A` from the
same reference pose gives `A_new = A_old·W⁻¹`, so **`q_anat` and therefore every joint angle are
unchanged**. The metrics math (nodes N4/N5 in §4) is *frame-invariant by construction*. The risk is
concentrated elsewhere — see §4.

---

## 2. The protocol reality (from the WT9011DCL-BT50 PDF)

| Topic | Fact | Register / command |
|---|---|---|
| Default stream | `0x55 0x61` = accel(XYZ)+gyro(XYZ)+Euler(XYZ), int16 LE. accel `/32768·16g`, gyro `/32768·2000°/s`, angle `/32768·180°`. Euler order ZYX. | auto |
| Register read | `0x55 0x71 <addrL><addrH>` + 8 register words. | `FF AA 27 <addr> 00` |
| **Native quaternion** | regs `0x51–0x54` = Q0,Q1,Q2,Q3 (**Q0=w**, Q1=x, Q2=y, Q3=z), each `/32768`. **Read-only, no auto-stream.** | `FF AA 27 51 00` |
| Auto-content toggle | `AGPVSEL` 0 = accel+gyro+angle; 1 = displacement+speed+angle. **No quaternion option.** | `0x96` |
| **9-axis / 6-axis** | `0x00` = **9-axis** (mag-fused, absolute heading) — *device default*; `0x01` = 6-axis (gyro-integral, relative). PinPoint forces `0x01`. | `AXIS6 (0x24)` |
| World frame | "East-North-Up celestial coordinate system" = **ENU** (matches ROS REP-103). Body axes stated X-left/Y-forward/Z-up (translation; handedness needs hardware confirmation). | — |
| Rate | `0x09` = 100 Hz (current), `0x0B` = 200 Hz. | `RATE (0x03)` |
| Config protocol | unlock → set → save. | `FF AA 69 88 B5` / `FF AA <reg> <lo> <hi>` / `FF AA 00 00 00` |
| Calibration cmds | accel cal `FF AA 01 01 00`; mag cal start/finish `…07/…00`; set-angle-ref `…08`; zero-Z `…04` (needs 6-axis). | `CALSW (0x01)` |

**Bottom line:** to get device-native quaternions over BLE we would have to **poll register `0x51`
at rate** (option C below) — there is no streaming frame to subscribe to. The request/response
plumbing already exists (`readRegisters()` builds the command; `dispatchReadResponse()` at
`wt9011dcl_base.cpp:347` parses `0x55 0x71` responses but currently ignores `startReg==0x51`).

> ⚠ **`0x51` is overloaded — don't confuse the two.** Here `0x51` means the **register address** Q0
> (read via `FF AA 27 51 00`, response `0x55 0x71 0x51 …`). In our serial *frame-type* enum,
> `0x51` is `PktAccel` and `0x59` is `PktQuaternion` (`wt9011dcl_base.h`). The register read and the
> serial auto-frame are unrelated mechanisms that happen to share the byte.

---

## 3. The world-orientation standard

### 3.1 ROS conventions (the vocabulary we adopt)

- **REP-103** — right-handed; world **ENU** (X-east, Y-north, Z-up); body **FLU** (X-forward,
  Y-left, Z-up); SI units, radians.
- **REP-105 / REP-145** — an IMU driver publishes `sensor_msgs/Imu` with orientation relative to a
  world frame; REP-145 explicitly accommodates **gyro-only / no-magnetometer** drivers whose yaw is
  not north-referenced. Quaternion order in ROS is **`(x,y,z,w)` — scalar-last**.

### 3.2 Recommendation: REP-103 vocabulary, REP-145 "no-mag" semantics

Adopt the **REP-103 ENU/FLU naming and handedness**, but with **REP-145 no-magnetometer world
semantics** — i.e. don't pretend yaw is north-referenced:

> **PinPoint IMU boundary contract**
> - **World (`imu_world`):** right-handed, **gravity-aligned, +Z up**, yaw **arbitrary-but-fixed per
>   session** (re-zeroable via calibration `A`). Labelled ENU-style for documentation; **not**
>   magnetic-north referenced.
> - **Sensor body (`imu_link`):** right-handed, +X device-right, +Y long axis, +Z face-normal;
>   identity quaternion at the neutral pose. (Anatomical `M`/`A` layer above this, unchanged.)
> - **Quaternion order: scalar-first `(w,x,y,z)`** — matches `Qt QQuaternion(scalar,x,y,z)`,
>   Witmotion's Q0..Q3, and the existing `ImuSample`.
> - **Units:** accel in g, gyro in °/s (or rad/s — pick one and document), angles in radians
>   internally / degrees at the UI.

This **ratifies what the host fusion already produces** — we are formalizing and naming the existing
frame, not rotating the pipeline. "Device-agnostic" is achieved by making *this contract*, not the
Witmotion device, the thing every consumer depends on; a future IMU must simply deliver into it.

### 3.3 Verdict — default to 6-axis, but the magnetometer decision is **per-sensor**

This is not an all-or-nothing call; the three sensors sit in three different magnetic environments:

| Sensor | Distance to club | Magnetic environment | 9-axis heading? |
|---|---|---|---|
| **Hand** | grip (~0 cm) | Club pose vs. this sensor is near-constant → dominantly *hard-iron-like* (partly calibratable), plus residual soft-iron as the grip flexes. | No — untrustworthy. |
| **Forearm** | ~15–25 cm | Hard-iron-like offset + meaningful soft-iron as the club reorients across the swing. | No — untrustworthy. |
| **Upper arm** | ~50–70 cm, across the elbow | Dipole field falls ~1/r³ → disturbance ~2 orders weaker; nearly clean. | **Most defensible** — worth testing. |

**Default recommendation: 6-axis on all three.** It pins roll/pitch to gravity (drift-free) and lets
yaw drift; because the metrics are *relative* quaternions between co-located segments, the
**common-mode yaw drift cancels** over a ~1 s swing.

⚠ **But common-mode cancellation solves only the easy half.** Three sensors have *independent* gyro
biases, so a **differential** yaw drift accumulates between them over the swing — it does **not**
cancel, and it is exactly the documented **~10–15° FE↔RUD cross-talk** (`wrist_angles.h:42-44`) that
makes RUD "the weakest IMU axis (~5° mean error)" per `wristmetrics.md` — the *same magnitude as the
M1 "HackMotion-grade" accuracy target*. So the 6-axis default is correct for FE/pronation, but it
leaves RUD accuracy bounded by a real, unsolved drift. **Two paths could constrain it** (both Track C
/ future): (a) a clean upper-arm 9-axis heading as an external reference, or (b) a kinematic-chain
heading constraint / inter-sensor gyro-bias estimation. This is promoted to a first-class open
question (§7), not parked as "future work."

### 3.4 Conversion math (all to be pinned by golden tests in Track A before use)

**(a) Witmotion-native quaternion → boundary** — both scalar-first, right-handed Z-up, so a direct
component copy (no axis flip). In the *live* path the Madgwick/ESKF output already *is* the boundary
quaternion; the contract just names it.
```cpp
QQuaternion q_boundary(Q0, Q1, Q2, Q3);   // (w,x,y,z)
```

**(b) ROS interop (if ever needed)** — reorder only, no axis remap (both RH Z-up):
```cpp
QQuaternion q_pp(ros.w, ros.x, ros.y, ros.z);          // ROS (x,y,z,w) → Qt (w,x,y,z)
ros = { q_pp.x(), q_pp.y(), q_pp.z(), q_pp.scalar() };  // Qt → ROS
```
⚠ **The `(w,x,y,z)` vs `(x,y,z,w)` gotcha is the single most likely silent bug** in any ROS-facing
work: a blind 4-float copy puts the scalar in the wrong slot and yields a plausible-but-wrong
rotation. Make it a named, tested converter — never a `memcpy`.

**(c) Boundary world (Z-up) → Qt Quick3D scene (Y-up, RH, −Z forward).** The **basis change** is a
fixed `Rx(−90°)` (det +1, no handedness flip): world +Z→scene +Y, +Y→scene −Z, +X→+X.
```cpp
const QQuaternion C(0.70710678f, -0.70710678f, 0, 0);   // Rx(-90°) = (cos-45°, sin-45°, 0, 0)
// basis change of an orientation expressed in world coords:
QQuaternion q_world_in_scene = C * q_boundary * C.conjugated();
```
The basis arithmetic is correct (verified). **But this similarity is NOT a drop-in for the existing
bone bindings, and `worldToScene` is not the whole rendered rotation.** A similarity preserves
rotation angle and maps the rest pose (`q_anat = I`) → identity = **T-pose**; the avatar needs
rest → *hanging*. The live code does that with a per-GLB rest offset:
`ArmVizView` builds `W = leftRestQuat · q_anat · rollFix` where
`leftRestQuat = (0.0237,−0.983,0.0583,−0.1727)` (≈177° about −X) is **distance 0.76 from `C`** — it is
*not* a pure `Rx(−90°)`. So the rendered bone is:

> `W = (basis change C) ∘ q_anat ∘ (per-GLB rest-forward / roll offset)`

The left factor `R0` must absorb **both** the `Rx(−90°)` basis change **and** the per-GLB rest offset;
it **cannot** be replaced by `C·q·C⁻¹` alone. `ImuVizView`'s cube is a *separate* case: it binds the
quaternion directly to a body `Node` (the cube *is* the orientation), so conjugating it would rotate
the cube relative to its labelled faces — leave it on a direct bind (with at most the basis change
folded into the node's parent). **All per-frame correction stays in quaternion constants — no Euler**
(project rule). *Every operand convention here MUST be pinned by the viz golden test (Track A, 2.1) —
which captures the ACTUAL `R0·anat·rollFix` chain — before any constant is touched (see §4 node N6).*

---

## 4. Fragile-coupling map & risk

```
[device 0x61 frame: raw gyro+accel, sensor frame]
   ▼
[N1 Madgwick/ESKF fusion]  ── world +Z-up SEED baked here ───────────── HIGH
   │ q_raw (sensor body, yaw-drifting)
   ▼
[N2 ImuInstance → EventBuffer ImuSample]  ── accel/gyro remapped, ───── HIGH
   │                                          quat NOT remapped (SPLIT)
   ├──────────────── live: anatQuat off QObject ───┐  ┌─ offline: raw samples + copied A,M
   ▼                  imu_instance.cpp:213          ▼  ▼  imu_vision_fuser.cpp:72  ◄─ TWO sites!
[N3 calibration  q_anat = A·q_raw·M]  ── A,M re-reference world ──────── HIGH
   ▼
[N4 relative quat  qProx⁻¹·qDist]  ── shared world rotation cancels ─── LOW
   │
   ├─► [N5 wrist_angles.h ZXY extraction]  flexion=Z / dev=X / pron=Y ── LOW
   │       ├─► MetricExtractor → SwingScorer → swing.json
   │       └─► LiveWristAngles "check sensors" readout
   └─► [N6 ArmVizView / ArmBoneController / ImuVizView] hand-tuned ───── MED (viz only)
                                                         consts; cube uses raw quat directly

[separate pipeline, never touches IMU quaternions]
[COCO keypoints] ─► BodyPoseAdapter (Z-only zRot, mirror flag) ─► BodyVizView body bones
```

| Node | Risk | Why |
|---|---|---|
| **N1** fusion seed | **HIGH** | The constant `(0,0,1)` / `gravity=[0,0,−9.81]` *defines* world-up; everything inherits it. |
| **N2** storage | **HIGH** | The stored quat frame is the live-replay + export contract. **Resolve the accel/gyro-vs-quat split here** and re-version schema (`imu_sample_v1` → `_v2`). *The v1 string is written in 3 places (`imu_sample.h:26`, `imu_instance.cpp:60`, `swing_exporter.cpp:406`) and read/branched-on by **nothing** — it is a forensic marker, so a bump cannot "break reload"; there is no IMU-vector reader to break.* |
| **N3** calibration A/M — **TWO sites** | **HIGH** | `A` *is* the world→anatomical map; its numeric value changes with the world convention even though the math is identical. `A·q·M` is composed in **two independent places**: the live write lambda (`imu_instance.cpp:213`) **and** the offline scored path (`imu_vision_fuser.cpp:72`). A frame-contract change must touch both or they silently diverge. The long-axis "down" sign pin (`imu_calibration.h:127`) depends on the up-convention; stale calibrations become wrong. |
| **N4** relative quat | **LOW** | Shared world rotation cancels between co-located sensors (`imu_vision_fuser.h:39-40`). Invariant *if both operands use the same `A`* — hence the two N3 sites must stay unified. |
| **N5** angle math | **LOW** | Pure anatomical/relative space. Invariant as long as `A` preserves anatomical-axis meaning. |
| **N6** viz | **MED** | `R0`, `restRollDeg=−99°`, `leftRestQuat`, and `ImuVizView`'s direct raw-quat binding are hand-tuned to the current frame and have **no test** — a world change silently breaks the avatar. |

### Test safety-net & gaps (Track A targets)

**Locked today** (standalone CTest at `src/Analysis/tests/`; root build forces `BUILD_TESTING OFF`,
`CMakeLists.txt:96,107`):
- `wrist_angles_test` — the **only** quaternion→angle characterization: `R(Z,20°)→fe≈+20,rud≈0`;
  `R(X,20°)→rud≈+20,fe≈0`; axial-Y doesn't leak; 180° elbow no-NaN; `leftArm` no-op.
- `pipeline_test` — segmenter + extractor end-to-end sign preservation; graceful degrade w/o upper-arm.
- `swing_scorer_test` — scoring machinery (deliberately **sign-independent** → would *not* catch a flip).
- `swing_doc_test` — swing.json schema/label round-trip.

**Untested (the dangerous blind spots):** the driver quaternion/Euler parse + remap; **both
orientation filters**; **`solveSegment` A/M solve (highest-value gap)**; the `q_anat = A·q·M`
composition; `live_wrist_angles.cpp`; **all QML viz mapping constants**; the left-handed-golfer mirror.

---

## 5. Target architecture

The end state is small and conservative:

1. **One documented contract** (§3.2) — written into `imu_sample.h` and a short
   `docs/design/imu_frame_contract.md`, referenced by every consumer.
2. **One declared frame in storage** (`ImuInstance`'s write lambda) — accel, gyro, **and** quaternion
   in a single declared frame (resolve the N2 split: either remap the quaternion to match the
   vectors, or stop remapping the vectors). Note what actually consumes each field: the ring
   **vectors** are read only by the **swing.json export** (`swing_exporter.cpp:394-397`, in the
   display frame); the live **stillness gate is quaternion-based** (`imu_instance.cpp:238`, never
   touches accel/gyro); and **calibration reads RAW accel off the QObject** (`imu_instance.cpp:573`,
   `m_imu->accelData()`), not the ring. So changing the vector remap changes only the *future
   exported accel/gyro values* — intended, and the payload of the v1→v2 bump (freeze a
   `swing_exporter` byte-golden first, then accept the diff). The quaternion-derived angles, scores,
   and replay are bit-stable across this change.
   **There are TWO `A·q·M` composition sites** (live `imu_instance.cpp:213`, offline
   `imu_vision_fuser.cpp:72`) — unify them on one helper, or add a golden asserting they agree for
   identical `(A, M, q_raw)`, so a frame-contract change can't make the live viz and the scored
   swing.json diverge.
3. **One explicit scene basis-change** (§3.4c) — `ArmVizView`, `ArmBoneController` (a *second* C++
   viz renderer — decide retire-or-migrate), and `ImuVizView` route through a named, tested
   `worldToScene()` basis change. ⚠ This is the basis change *only*: the rendered bone is
   `R0 · q_anat · restOffset`, and `R0` must fold in both the basis change **and** the per-GLB rest
   offset — it does **not** collapse to a single conjugation (§3.4c). The viz golden (2.1) pins the
   *actual* current binding chains before any constant moves.
4. **An optional, flag-gated alternate orientation source** (Track C) — host fusion remains the
   default; a native-quaternion register-poll path can be switched in for A/B testing without
   touching any consumer (because everything downstream binds to the contract, not the source).

The relative-quaternion invariance (§1.4) means **N4/N5 need no changes** — the migration is about
the *boundary and storage*, not the math. That is what makes "extreme care" tractable: we change the
edges and lock the middle with goldens.

---

## 6. Implementation plan (phased, test-gated)

> **Rule for every phase: a test is written/extended and passing *before* the corresponding
> production change, and the full suite is green *after*.** The existing analyzer tests are a
> standalone, **Qt-Core/Gui-only** CTest target (`src/Analysis/tests/CMakeLists.txt:24,32`; the root
> build forces `BUILD_TESTING OFF`). **The new driver/filter tests are NOT a no-op addition** — see
> Phase 0.0 — so "green before every change" only holds once their deps are provisioned.

### Phase 0 — Characterization harness (Track A) · *prerequisite, no production change*

Freeze today's behaviour as golden references so any later change that perturbs an angle, a stored
byte, or a bone rotation fails loudly.

- **0.0 Provision test deps *(do first — gates 0.3/0.4)*.** These are **not** the existing Qt-only
  standalone build. `orientation_filter_test` → `eskf_orientation_filter.cpp` → `ESKF.h` →
  `<Eigen.h>`; `imu_driver_frame_test` compiles `wt9011dcl_base.cpp`, which derives from `ImuBase`
  whose ctor unconditionally builds an `EskfOrientationFilter` (`imu_base.cpp:36` → Eigen) and which
  includes `pp_debug.h` (→ `whisper.h`) and `event_buffer.h` (Buffer lib, `EventBuffer::nowMicros()`).
  Tasks: add the Eigen include path (vendor `third_party/imu_ekf`); **stub `PpLogStream`** to avoid
  `pp_debug.cpp`+whisper (per the `standalone-test-harness-gotchas` memory); provide the Buffer types.
  **Prefer a thin fixture** exercising the static math (`eulerToQuat`, `parseQuaternion`, the filter
  `update()`) **without constructing the full `ImuBase`+ESKF**, to avoid dragging the whole stack
  into every gate. Decide and document this before writing 0.3/0.4.
- **0.1 `imu_calibration_test`** *(highest value)*: freeze `nominalArmMount()`/`nominalHandMount()`
  (tol 1e-3); `solveSegment` golden (arm-down `refRaw=I`, `down=(−1,0,0)`, `long=(1,0,0)`,
  `flex=(0,0,1)` → `M≈nominalArmMount`, `axisAngle≈90`, `valid`); reference-pose identity property
  `A·refRaw·M==I` over random quats (tol 1e-4); validity gate (parallel axes → `valid==false`);
  **the keystone end-to-end golden: fixed `(A,M,q_raw)` for a known flexed pose → freeze `q_anat`
  *and* the resulting `wristFlexExtDeviation` degrees.** Exercise this through the **`imu_vision_fuser`
  composition path** (`imu_vision_fuser.cpp:72`), not just the `imu_calibration.h` header, so the
  offline scored site is pinned too (the second N3 site, §4).
- **0.2 Extend `wrist_angles_test`** with ~10 named clinical-pose goldens (neutral, bowed/cupped,
  ulnar/radial, pron/sup, two combos, label-boundary deadband) — values captured from current code.
- **0.3 `imu_driver_frame_test`**: `parseQuaternion` byte golden (`le16/32768`, w,x,y,z); `eulerToQuat`
  WT901BLE67 override golden (Roll→X, Yaw→Y, −Pitch→Z); gimbal gate → `nullopt`.
- **0.4 `orientation_filter_test`** (Madgwick + ESKF): gravity-seed cases incl. the 180°-about-X edge
  `initFromAccel(0,0,−1)→(0,1,0,0)`; static convergence; constant-gyro integration ≈ ω·t.
- **0.5 `live_wrist_angles` math test**: drive the live `rel` path with the same synthetic poses as
  `pipeline_test`; **freeze the right-lead (`leftArm=true`) outputs** so the unverified left-handed
  mirror flags the instant it is implemented.
- **0.6 Clarify (don't delete) the comment** in `wrist_angles.h:32-35`: the "+X = flexion axis"
  segment-frame definition is *true*; add wording that distinguishes it from the joint-DOF the wrist
  rotates about (flexion read on Z, line 118), so neither future reader misapplies the other (§1.4).

**Gate to exit Phase 0:** all new tests green and runnable from a single documented command; the
Phase 0.0 dep provisioning checked in so the gate actually compiles.

### Phase 1 — Frame contract + storage fix (Track B) · *behaviour-preserving*

- **1.1** Write `docs/design/imu_frame_contract.md` (§3.2), including the **authoritative per-segment
  anatomical axis-label table** (Open-Q §7 #4 — none exists in code today), and reference it from
  `imu_sample.h`, `imu_calibration.h`, `wrist_angles.h`.
- **1.2** Resolve the N2 split (`imu_instance.cpp:271-281`): put accel, gyro, **and** quaternion in
  one declared frame. Recommend storing **raw sensor-frame** vectors (drop the display remap) since
  the quaternion is the authoritative field; document the choice in `imu_sample.h`. **This *does*
  change the future exported accel/gyro values** in swing.json (`swing_exporter.cpp:394-397`) — that
  is intentional. Freeze a `swing_exporter` byte-golden of the exported vectors *before* 1.2 and
  accept the diff in the same commit.
- **1.3 Unify the two `A·q·M` sites.** Factor the live (`imu_instance.cpp:213`) and offline
  (`imu_vision_fuser.cpp:72`) compositions onto one helper (or, minimally, add a golden asserting they
  agree for identical `(A,M,q_raw)`) so the contract has a single source of truth (§4 N3).
- **1.4** Bump the schema string `imu_sample_v1 → v2` in **lockstep across all three hardcode sites**
  (`imu_sample.h:26`, `imu_instance.cpp:60`, `swing_exporter.cpp:406`). State plainly that the string
  is a **forensic marker with no programmatic reader today** (nothing branches on it; swing.json's
  IMU `data[]` stream is never reloaded) — so the bump cannot "break reload." Add a v1↔v2 reader rule
  only if disambiguation ever becomes necessary.
- **1.5 Correct CLAUDE.md** (the "delivers quaternions natively" rotation-rule note — see §8).

**Gate:** Phase 0 goldens for **angles / `q_anat` / scores / replay are bit-stable** (the quaternion
field is untouched); the new `swing_exporter` vector golden shows the *expected* remap diff and
nothing else; a new `imu_sample` round-trip test asserts the v2 frame.

### Phase 2 — Explicit scene conversion + consumer cleanup (Track B)

- **2.1** Add a tested `worldToScene()` basis-change helper (§3.4c) **and** a viz golden that pins the
  *actual current* binding chains — `ArmVizView`'s `W = leftRestQuat · q_anat · rollFix` (with the
  relative joints conj'd by `rollFix`) and `ArmBoneController`'s `parentWorld⁻¹ · boneWorld`
  (`arm_bone_controller.cpp:186-191`) — for a set of known anatomical quaternions, *before* any
  constant moves. The golden must reproduce today's avatar, not an idealized `worldToScene`.
- **2.2 Enumerate and decide all four IMU-driven viz/calibration-application sites** (do not "barely
  mention" any): (a) `ArmVizView` (QML per-bone `R0·anat·rollFix` + `quatApplyCalib`); (b)
  `ArmBoneController` (C++ alt renderer — **decide retire-or-migrate**, it's wired as a context
  property in main.cpp); (c) `ImuVizView` cube (direct raw-quat body bind — separate case, §3.4c);
  (d) `quatApplyCalib`'s anatQuat-vs-legacy fallback (state whether in scope). Refold the basis change
  into each `R0` (keeping the per-GLB rest offset, §3.4c). Verify visually + against 2.1.
- **2.3** (Doc) `BodyPoseAdapter`/`BodyVizView` body bones are COCO-driven and **out of scope** — they
  never touch the IMU quaternion (`body_pose_adapter.cpp:121-135`).

**Gate:** Phase 0 + 2.1 goldens green; manual viz confirmation on hardware (the avatar must hang at
rest, not snap to T-pose — the §3.4c trap).

### Phase 3 — Native-quaternion / 9-axis (Track C) · *OPTIONAL, hardware-gated, your decision*

Only if you choose to pursue device-native orientation (see §6.4). Flagged so it cannot regress
Tracks A/B:

- **3.0 Characterize the differential inter-sensor yaw drift *(cheap, near-mandatory — needs only
  existing 6-axis data + the 3.3 jig)*.** Log three static-then-swung sensors over a real 1–5 s swing;
  measure the relative-yaw divergence between segments. This **bounds the achievable RUD accuracy**
  and tells you whether any of the rest of Track C is even worth it. Do this first.
- **3.1 *Feasibility spike* (not "low-risk additive"):** the write path has **no
  serialization/response-pacing** (`readRegisters()` → `writeToDevice` fires immediately,
  WriteWithoutResponse), and the `0x71` response shares the **single notify characteristic** with the
  `0x61` auto-stream; there is no precedent for high-rate register polling (only the 60 s battery read
  and one-shot connect reads exist). Spike it: add the `startReg==0x51` branch + poll timer, ramp
  1→10→50→100 Hz, and **prove `0x71` responses don't displace `0x61` frames or inflate
  `gimbalDropCount`**; add request-pacing if WriteWithoutResponse floods. Parse `le16(…)/32768` as
  `(w,x,y,z)`. Abort Track C here if the poll can't coexist with the stream.
- **3.2** Behind `AppSettings::imuOrientationSource = {hostFusion(default) | deviceNative}`, select
  which quaternion populates `quaternionUpdated`. No consumer changes (they bind the contract).
- **3.3 Hardware A/B** (answers Open Question §7 #2): in a known cup/bow jig, compare device-native vs
  host-fusion `q_anat` against the optical/jig reference. **Accept only if rigidity matches or beats
  host fusion** — the on-board *Euler* output was 15–50° off, and the `0x51` register may share that
  fusion.
- **3.4 Per-sensor 9-axis (hybrid), not all-or-nothing.** Per §3.3, the *upper-arm* sensor is the one
  defensible 9-axis candidate (far from the club). Test a **9-axis upper-arm + 6-axis hand/forearm**
  hybrid as an external heading reference to constrain the Phase 3.0 differential yaw — but first measure
  the mag disturbance at each sensor through a swing; do **not** enable 9-axis on hand/forearm.

**Gate:** all prior goldens green; Phase 3.0 measurement + §3.3 hardware acceptance documented.

### 6.4 The scope decision

Tracks A + B are unambiguously worth doing and carry the bulk of the value of goal #2 (a device-owned,
documented, testable world-frame contract) with low risk. **Track C is where the original premise
(9-axis + native `0x67` quaternions) lands — and the evidence argues against it for this golf-club
application.** I recommend **A + B now, C only as a later hardware-gated experiment** — but this is
your call, because C is what you explicitly asked for.

⚠ **One scope ambiguity to settle explicitly.** "Device-agnostic world orientation" has two
readings, and A + B deliver only the first:
- **(easy, in A+B)** *Name and own* the existing world frame — gravity-up, arbitrary-but-fixed yaw —
  so consumers depend on a PinPoint contract, not the Witmotion device. Real value, low risk.
- **(hard, NOT in A+B)** Establish a **shared, observable** world heading across the three sensors so
  the **differential inter-sensor yaw is bounded and RUD becomes trustworthy** (§3.3/§7). By defining
  the contract as explicitly *non*-North-referenced, A+B *bakes in* the RUD limitation rather than
  solving it. Closing it is Track C work (3.0/3.4) or a separate heading-fusion effort.

A+B is the right *first* step regardless; just be aware it improves *decoupling*, not RUD *accuracy*.
The questions at the end of this hand-off capture both decisions.

---

## 7. Open questions / hardware verification needed

1. **The `0x67` premise** — not in the PDF or code. If a firmware/module variant emits a continuous
   quaternion auto-frame, its existence and *enabling register* are unknown (`AGPVSEL` does not toggle
   quaternion). Needs hardware probing before any streaming-frame work.
2. **Would a device-native quaternion be rigid?** The on-board *Euler* output was rejected as
   non-rigid (15–50° off). It's unknown whether the `0x51` quaternion register comes from the same
   on-board fusion (likely) or is independently faithful. **Decisive A/B test required (Phase 3.3).**
3. **Definitive sensor-axis directions** — the PDF's stated body axes (X-left/Y-forward/Z-up) and
   PinPoint's desk characterization (X-right/Y-USB/Z-face-normal) are not obviously the same labeling.
   Confirm on hardware before changing any remap.
4. **No canonical per-segment anatomical axis-label table in code** — `solveSegment` builds a generic
   basis; comments variously call Z "anterior/abduction" (arm) vs "radial" (wrist). Write one
   authoritative table during Phase 1.
5. **⭐ Differential inter-sensor yaw drift — the RUD accuracy limiter (promoted, first-class).**
   Common-mode yaw cancels in relative quaternions; **differential** yaw between three
   independently-biased gyros does **not**, and it *is* the ~10–15° FE↔RUD cross-talk
   (`wrist_angles.h:42-44`) that bounds RUD at ~5° error — the same order as the M1 accuracy target.
   A+B do not touch it. Quantify it (Phase 3.0) and decide whether to close it (clean upper-arm 9-axis
   heading, or a kinematic-chain heading constraint / inter-sensor bias estimation). This is the gap
   between "device-agnostic frame" (delivered) and "trustworthy RUD" (not).
6. **Live vs offline lockstep** — `live_wrist_angles` resolves segments by slot string and offline by
   `SegmentRole` enum, and derive `leftArm` from different sources; they agree *only because*
   `leftArm` is a no-op. Test 0.5 guards this.
7. **Calibration is not persisted/reloaded at runtime** — confirmed: session-only, **no 30-min
   window**, no capture-time stamp in the live path (contradicting any CLAUDE.md/M1-doc "30-min
   validity" notion — that is *not implemented*). Lost on disconnect/restart; the migration must keep
   re-solve-per-session semantics. *Note the latent dead scaffolding to wire-up-or-remove:*
   `AppSettings::imuCalibration` (QVariantMap, `app_settings.h:89`) and `imu/saveCalibrationToFlash`
   (`app_settings.h:175`, default false) exist but are unused — mirroring the dead `imuDefaultFusionMode`
   key (§1.1).
8. **Left-handed-golfer mirror — a tracked deliverable, not just a frozen no-op.** `leftArm` is
   unimplemented/unverified (`wrist_angles.h:42-44,123,146`). Test 0.5 freezes today's no-op behaviour
   so it can't silently regress, **and** when the mirror is eventually implemented it must be validated
   against the same Phase 0 goldens (and the live/offline `leftArm`-source divergence in #6 resolved).

---

## 8. Doc corrections this work implies

- **CLAUDE.md** ("Rotations" section): "the WT901BLE67 hardware delivers quaternions natively — use
  them directly" is **false for the live path** — orientation is host-fused (Madgwick/ESKF) from raw
  accel+gyro; the device's on-board orientation was rejected as non-rigid. Update to reflect §1.1.
- **`imu_sample.h:27-28`**: the "display/world frame" comment applies only to accel/gyro today, not
  the quaternion (the N2 split). Corrected by Phase 1.2.
- **`wrist_angles.h:32-35`**: the "+X = flexion axis" segment-frame line is *correct* but easily
  misread next to the joint-DOF extraction (flexion read on Z, line 118). Phase 0.6 *clarifies the
  distinction* in-comment — it does not delete a true statement.
- Any reference to a "30-minute calibration validity window" (M1 doc §3) — **not implemented**;
  calibration is session-only. Clarify or implement separately.
```
