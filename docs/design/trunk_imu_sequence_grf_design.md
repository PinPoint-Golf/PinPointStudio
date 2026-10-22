# Trunk IMUs, the kinematic sequence, load and ground reaction force — unified design

**Status:** design, not started (beyond role-keyed placement). Merged 14 October 2026.
**Owner:** Mark.
**Merges three sources, which this document replaces:**
- `docs/design/trunk_imu_design.md` (4 Oct; this file, renamed): pelvis belt and thorax vest, frames, the standing-only ceremony, the analysis changes, camera-route grading.
- *Thorax + Pelvis IMUs — Rotational Metrics and Lead-Side Load/Drive* (Fable brief, 13 Oct): the acceleration path, lever arm, COM, load and drive metrics, the ZMP lead fraction.
- *Ground Reaction Force Estimation — Research & Implementation Brief* (Fable brief, 13 Oct): the per-foot GRF programme, force platforms, the split solver, the reference session.

**Background, not replaced:** `grf_estimation_design_briefing.md` (July: the literature survey and the honesty table), `body_rotation_estimation.md`, `kinematic_sequence_design.md`, `ks_skeleton3d_route_design.md`, `imu_frame_contract.md`, `imu_rearchitecture.md`, `uncertainty_census_design.md`, `two_camera_capture_protocol.md`, `session_wizard_refactor_design.md`, `docs/reference/golf_swing_normative_reference.md` (§5 GRF and COP norms).

Where the sources disagreed, §2 states which way this document went and why. The decided items from 4 October (D1–D3, D10) stand.

---

## 0. How to read this

| Part | Sections | Needs |
|---|---|---|
| **A. Trunk IMUs: rotation** | §3–§9 | Two Witmotions and the cameras. The core of the work, and what everything else builds on. |
| **K. The kinematic sequence in the diagnostic model** | §10 | Nothing new: every route the KS already has. Part A makes the trunk nodes measured. |
| **B. Lead-side load and drive** | §11 | Part A, plus the acceleration path (§7.3–7.4). No new hardware. |
| **C. Per-foot ground reaction force** | §12 | Parts A and B, plus force platforms (Wii boards, then a DIY plate). |
| Shared | §13–§20 | Capture, V&V, code, order of work, results, decisions, risks, references |

Each part is useful on its own. Part A pays even if B and C never ship, because it is the truth the camera routes have been waiting for. Part K can be built now, on the camera routes, and gets better the day the trunk sensors are worn.

**Placement and the ceremony are defined once.** The two sensor positions are in §4.1 and the user's calibration is in §6.3. Every other section refers to those two; none restates them.

---

## 1. Summary

**One data path, three products.** Two body-worn IMUs, the **hip sensor** (belt, on the sacrum) and the **chest sensor** (vest pocket, on the upper back between the shoulder blades), placed as §4.1 says, feed:
1. **Rotation:** pelvis and thorax turn, X-factor and stretch, tilt and side bend, forward bend, angular rates and the kinematic sequence.
2. **Load and drive:** net vertical load (%BW) and lateral push from the centre-of-mass acceleration; the "sit" and the "drive"; bump-before-turn; the lead-foot load fraction once fused with the face-on camera.
3. **Ground reaction force:** per-foot vertical force and COP from cheap plates, net force and moment from kinematics, horizontal per-foot forces from a constrained split.

**The analysis side of (1) is mostly built and has never run.** `SegmentRole::Pelvis` and `Thorax` exist. The rotation producer, the KS IMU rung, the catalogue routes and an IMU term in the skeleton3d fit all read them. No shot has ever carried one. Since 4 October, session setup (6290b65f) made placement role-keyed, with pelvis and thorax *defined but not yet offered*. Nothing else in the 5–9 October plan has been built: `solveTrunkSegment()` does not exist.

**Where most of the value is.** Not the trunk metrics for the golfer wearing the vest, but **truth for the camera routes**, which every camera-only user depends on. One session worn with trunk IMUs on both cameras settles most of the open camera questions (§3.1). The IMU is the validation path, not the fix (memory note `feedback_camera_fallback_best_effort_honest`).

**The framing for (2) and (3)** stays "GRF-informed coaching cues", not "virtual force plate". Every load and GRF metric is `experimental` until the reference session (§14.6) says otherwise, and shows its measured error band.

**Three defects** (found 4 Oct, not re-checked since) must be fixed before any trunk number is believed:
1. **The vertical axis.** Calibration makes the reference-pose body frame the "world" (`A = conj(refRaw·M)`, `imu_calibration.h`). For the arm, that world is Y-down. `body_rotation.cpp:90` and `segment_rates.cpp:1268` treat world **Z** as vertical; `phase_segmenter.cpp:290` reads the pelvis axial rate on segment **Y**. The unit tests feed synthetic yaw about Z and never pass through a calibration, so none of this is caught. §5 fixes it.
2. **A pelvis IMU makes `hip_stall` unassessable.** The triangulated stage steps aside for any segment an IMU covers (`BodyRotationTriangulatedStage`, `analysis_pipeline.cpp:3207`; the Wrist analyzer became the analysis pipeline in 842095e7), but the IMU route for `pelvisRotationSigned` is still PLANNED (`metric_catalogue_manifest.cpp:661`), so the measure loses its only live producer.
3. **A trunk-only IMU job replaces good vision phases with a conf-0 clamp.** `SegResolveStage` (`analysis_pipeline.cpp:869`) adopted `segImu` whenever it existed (fixed 17 Oct: only when its confidence is above 0); `PhaseSegmenter` needs a hand or forearm sensor and otherwise returns `clampFallback()` at conf 0.

**Three smaller things already known to bite:**
- No Witmotion swing has ever passed `imuIntegrity` parity (memory note `imu-integrity-write-once`).
- Witmotion samples carry host-arrival timestamps from a bursty BLE link, with no clock fit.
- IMU rotation readings carry no σ (`body_rotation.cpp:112`).

---

## 2. Reconciliation — where the sources disagreed

| # | Topic | Fable briefs (13 Oct) | Repo design (4 Oct) | This document |
|---|---|---|---|---|
| R1 | **Ceremony motions** | Still hold, N-pose, functional turn, **hip-hinge forward bend**, address hold, **stool test**, **hop** | Stand tall, turn away, turn through; no hinge (D10) | **The 4 Oct three steps, plus a hop (D10 stands; D14).** The hinge solved the mount yaw δ; §6.1 shows δ comes from the mount and the swings instead. The briefs' still-hold gates run automatically inside Stand tall. **The stool test is not a user step:** the lever arm comes from a default refined by the cameras (§6.4). **The hop is (D14):** a small hop at the end of the ceremony sets the session's IMU ↔ camera clock offset (§7.1). The stool survives only as a validation test in the truth session (§13). |
| R2 | **What the turn solves** | The dominant gyro axis of the turn completes the mount | A standing turn carries no information about δ; it is a check | **A check.** Upright turning rotates about +Z, which gravity already gives; every mount yaw sees the same gyro. This is physics, not a missing algorithm. |
| R3 | **Frames** | World x = target line, z up; segment x = ML to the athlete's left, y longitudinal, z posterior; a new `docs/Frames.md` | Segment +X ML to the golfer's right, +Y anterior, +Z superior; trunk A heading-only, world gravity Z-up | **The repo trunk frame**, owned by `imu_frame_contract.md` (trunk rows) and `pinpoint_sign_conventions.md`. No new `Frames.md`: those two documents already are the single owner the brief asks for. ⚠ The 4 Oct text says stance +X points "toward the target for a right-hander"; with +X to the golfer's right and the golfer facing the ball, it points *away* from the target. To fix when the trunk rows are written (§5.2). |
| R4 | **Angle definitions** | Projections of segment axes (heading of the ML axis, elevation of the ML and long axes) | Cardan Z-Y-X in the stance frame, checked against ISB Part I | **Open: D13.** Both are written down (§5.3). The projection form avoids sequence dependence at large angles; Cardan is what ISB and the sign conventions speak. |
| R5 | **Placement** | Pelvis "at the belt line over the sacrum", rigid belt, keyed 3D-printed cradle; thorax T3–T4, harness with a rigid plate, or the sternum | Belt on the sacrum below the belt line; GPS-vest pocket, about T2–T5 | **The 4 Oct placement, defined once in §4.1:** hip sensor on the sacrum, below the waistband; chest sensor in the vest pocket between the shoulder blades. T3–T4 is the same spot within a vertebra. No cradle, no harness plate, no sternum: they add hardware the golfer has to own, and the sternum would break like-for-like with the Cheetham norms, which used upper-back sensors. |
| R6 | **Sensor and rate** | WT9011DCL, 200 Hz assumed | "WT901BLE67"; 100 Hz default, 200 Hz unmeasured with three links | **The code's driver is `wt9011dcl_*`.** Confirm the model on the units in hand. **200 Hz is a day-1 measurement, not an assumption** (§4.2). |
| R7 | **Orientation filter** | "PinPoint's own ESKF (existing)" | Live 6-axis Madgwick; offline re-fusion with a trunk profile | **Offline re-fusion with a trunk profile (§7.2),** on whichever filter re-fusion uses (`eskf_orientation_filter.h` exists beside the refuser). Gyro-dominant through the swing either way. |
| R8 | **IMU ↔ camera clock** | "Existing clock-bias estimator (P1)"; hop at address | Witmotion has no clock fit (HackMotion alone has `clockFit`); drop test once, then per-swing cross-correlation | **The brief's hop, each session (D14), then per-swing cross-correlation recorded (D6).** There is no Witmotion clock fit to reuse. The hop is in view of the cameras, so it measures this session's offset rather than a rig constant. |
| R9 | **Translation from the accelerometer** | COM acceleration drives load metrics | "Double-integrating the accelerometer is not credible" | **Both hold.** Load metrics use acceleration, never integrated position. Sway, slide, thrust and lift stay camera metrics. The one single integration (impulse closure) is a quality check, not a metric. |
| R10 | **Centripetal acceleration** | A transport correction, mandatory for load | A filter hazard: the accelerometer is not measuring gravity in the downswing | **Both, in different places.** The orientation filter gates the accelerometer out (§7.2); the load path subtracts the rigid-body terms with a lever arm (§7.3). |
| R11 | **Metric ids** | `rot.*`, `load.*`, `grf.*` | Existing catalogue keys (`pelvisRotation`, `xFactor` …) | **Catalogue keys.** Existing keys get IMU routes; new quantities get camelCase keys (§8.1, §11.4, §12.6). |
| R12 | **Code layout** | `src/Sensors/Imu/`, `src/Sensors/Calibration/`, `src/Devices/ForcePlatform/`, `src/Kinetics/` | `src/IMU/`, `src/Body/`, `src/Kinematics/`, `src/Skeleton3d/` | **Repo layout.** New code: `src/Kinetics/` (agreed) and `src/ForcePlatform/` beside `src/IMU/` and `src/LaunchMonitor/`. §15 maps every class. |
| R13 | **Ceremony controller** | A C++ `CalibrationCeremonyController` state machine with TTS | `TrunkCalibrationFlow.qml`, solve in `imu_calibration.h` | **Session setup's calibration routine shape** (6290b65f: routines behind a guide interface, gated on one activity flag). The solve is a pure C++ function. The brief's injectable gates and synthetic device are adopted (§15). |
| R14 | **Wizard rule** | — | Wizard edits need explicit approval | **Dropped (10 Oct).** `setup/` and `calibration/` are edited normally and verified with `session_setup_ui` (memory note `do-not-touch-the-session-wizard`). |
| R15 | **Per-swing heading** | Address repeats silently every swing | No address hold in the ceremony; slip detector baselines from the first swings | **Both.** Every swing's turn is relative to its own address (as before); the ceremony has no address step. |

---

# Part A — Trunk IMUs: rotation

## 3. What the placement buys, ranked

### 3.1 Truth for the camera routes (largest; benefits every user)

Every swing worn with trunk IMUs and recorded on face-on + DTL is a paired observation. Re-analysing it with the IMU bindings ignored gives the camera answer to the same swing (`kinematic_sequence_design.md` §9 stage 2).

| Open question | Where it is recorded | What the IMU measures |
|---|---|---|
| The pair route says pelvis and thorax are "still rising at impact" on 20/21 swings | memory note `ks-pair-route` | The real pelvis/thorax peak times |
| The skeleton fit turns the hips through impact at about half the DTL keypoints' rate | `ks_skeleton3d_route_design.md`, K0 §8 | Pelvis yaw rate through square, where face-on is blind |
| Thorax 103° and X-factor 66° at the top: real, or the fit and camera geometry? | memory note `rotation-triangulated` | Thorax and pelvis turn at P4 |
| The thorax shoulder line carries clavicle motion | K0 §12 | Thoracic twist without the clavicles; shoulder line minus thorax IMU *is* the clavicle term |
| Camera pelvis peak 640–840 °/s against Cheetham 477 ± 53 | KS design §13.9 #4 | The real peak magnitude |
| Golfer 15–19° open at address, or a span artefact? | `body_rotation_estimation.md` §6 Q1 | Address heading against the calibrated stance (§6.6) |
| `kTriScaleFrac` 0.10 (0.03 calibrated): is the camera-scale σ honest? | `body_rotation.cpp:148` | Coverage: fraction of \|camera − IMU\| ≤ kσ |
| Thorax KS placement is off by default on both camera rungs | `segment_rates.h:131` | Whether placements land within σ_t of truth |
| Video COM acceleration: usable for load at 150 fps? | new (Part B) | IMU COM acceleration as the reference for the double-differentiated video COM |

⚠ **One golfer.** All of this is Mark's swing. It grades methods on one body, which is what σ coverage needs. It cannot seat norms or corridors (memory note `wrist-perswing-closed`).

### 3.2 Measured trunk metrics for the wearer

These resolve `Measured` instead of `Bridged`, with a σ: `pelvisRotation`, `thoraxRotation`, `xFactor`, `xFactorStretch`, `pelvisRotationSigned` and a new `thoraxRotationSigned`, `pelvisAngularSpeed`, `thoraxAngularSpeed`, `pelvisPeakTime`, `thoraxPeakTime`.

**New quantities a calibrated triad allows.** One IMU per segment gives a full orientation, so the trunk gets tilt and side bend, not only turn:

| Metric | Definition | Diagnostic use |
|---|---|---|
| `pelvisTilt` (anterior/posterior) | Pelvis sagittal tilt from upright stance | Early extension's rotational half: posterior tilt address→impact. The camera gives only the thrust half (`pelvisThrust`, DTL). |
| `pelvisObliquity` | Pelvis frontal tilt, lead hip high + | IMU route for `hipLineTilt`; pelvis side bend at top, about 10–12° (NR, low confidence) |
| `thoraxForwardBend` | Thorax sagittal inclination | The PLANNED `trunkImus` route on `spineForwardBend`; loss of posture P1→P7 |
| `thoraxSideBend` | Thorax frontal inclination | IMU route for `spineSideBend`; side bend at impact; reverse spine angle at top |
| `xFactorAtTop` measure | xFactor at P4 | No such measure exists today |
| `pelvisDecel` present/absent | The pelvis rate falls before impact | NR-12 says report it as binary |
| `speedGainPelvisThorax` | Thorax peak − pelvis peak (°/s); and the ratio | Cheetham 250 ± 42 °/s pro |
| `pelvisThoraxPeakGap` | Thorax peak time − pelvis peak time (ms); sign is order | KS order on the trunk |
| `crunchFactor` | Thorax side bend × axial rate relative to the pelvis | NR-17: compute and log, do not display |

The KS norms (Cheetham) come from sensors on the pelvis and upper thorax: this layout. The "% of pro" in the KS tile becomes like-for-like for the first time.

### 3.3 Kinematic sequence complete on the trunk

The IMU rung is already first in the ladder. With both sensors bound, the pelvis and thorax nodes are placed from a gyro. `sequence_order` needs a placed chest peak: notAssessable on all 15 swings of 07-04 today, assessable on every worn swing.

### 3.4 A better 3-D model

The skeleton3d fit has an IMU orientation term (`skeleton3d_fit.cpp:734`). Trunk IMUs pin the two DOF the fit's priors paper over: pelvis yaw at square (`pelvisYawAccRad`) and pelvis tilt (`pelvisTiltAccRad`; two hip keypoints cannot see tilt). The chest sensor rides `Spine2` and separates spine twist from clavicle motion. The 3-D hip pause at impact (memory note `ks-skeleton3d-route`) should disappear on worn swings; if it does not, that is a finding about the fit.

### 3.5 Phase timing

Cleaner events than the arm for three positions:
- **Transition:** pelvis axial reversal (already coded, `phase_segmenter.cpp:283`). The pelvis typically reverses *before* the club; report both, and the club top from vision stays primary when available.
- **Top:** thorax reversal, today only a cross-check.
- **Takeaway:** thorax |ω_z| over 20 °/s sustained 50 ms. The trunk is far stiller at address than a waggling wrist (the wrist stillness detector fell back to continuous waggle on 11/11 wG3 swings).
- **Impact fallback:** thorax acceleration shock, only when the floor camera and mic are absent.

IMU-only sessions carry address, takeaway, top and impact; P-positions come from vision when present.

### 3.6 What it does not give

**Translation.** Double-integrating the accelerometer over 1.5 s is not credible for sway, slide, thrust or lift; those stay camera metrics. The IMU's contribution there is the posterior-tilt half of early extension, and the *acceleration* (not position) of the COM in Part B.

---

## 4. Hardware and error budget

### 4.1 Placement — the one definition

Every other section of this document refers to this table. The role ids are the ones session setup already defines (`ImuMounts.trunkRoles`); the names are what the golfer sees.

| | Hip sensor | Chest sensor |
|---|---|---|
| **Role id** | `pelvis` | `thorax` |
| **Golfer sees** | "Hip sensor (belt)" | "Chest sensor (vest)" |
| **Carrier** | A firm belt (not elastic) with a pouch | A snug GPS-style vest with a back pocket |
| **Where** | Back of the pelvis, on the spine's midline, on the flat of the sacrum (S1–S2), **below** the waistband | Upper back, on the spine's midline, between the shoulder blades (about T2–T5), in the vest's pocket |
| **Which way up** | USB end up, label facing out | USB end up, label facing out |
| **What the golfer is told** | "Belt sensor in the middle of your lower back, below your waistband, USB up, label out." | "Chest sensor in the vest's back pocket, USB up, label out. Vest snug." |
| **Segment it measures** | Pelvis | Upper thorax (spine at the sensor's level, not the shoulder line) |

**Why these two spots.** They are the sites the Cheetham KS norms were measured on (pelvis and upper thorax), so "% of pro" is like-for-like. The sacrum is also the standard COM-proxy site in the running-GRF literature, which Part B relies on. Both carriers are things a golfer can own and put on alone.

**What each spot brings, and what absorbs it:**
- **Chest — scapular motion:** retraction and protraction move the pocket relative to the thorax; the size for a GPS vest is unknown, and the truth session measures it (§13).
- **Chest — garment slip:** the vest can ride up or rotate across a session; the slip detector catches it (§6.8).
- **Chest — kyphosis:** the pocket leans 15–30° from vertical when standing. Stand tall absorbs it (§6.3); it is not an error.
- **Hip — belt ride:** a belt at the iliac crests rides up in a hip hinge, which is why the sensor sits on the sacrum, below the waistband.
- **Hip — skin slide** over the sacrum in deep flexion; part of the garment σ (§8.2).
- **Both — mount yaw δ:** how far the sensor is twisted in its pocket around the spine. "USB up, label out" fixes it to about ±10°; the swings refine it (§6.5).

**The published ceiling.** Kim et al. 2023: 0.6–1.7° Bland–Altman against lab mocap, sensors mounted directly on T1 and L4. Garment-mounted will be worse by an amount nobody has measured on us; §13's static poses and the end-of-session re-check measure it.

**User guide additions:** pad the sensor so it cannot rattle; close the pocket; put the belt and vest on before the session and leave them on.

### 4.2 Dynamics and range

Centripetal acceleration at a sensor on the back, a ≈ ω²r, r ≈ 0.10–0.15 m from the spinal axis:

| Segment | Peak rate | ω (rad/s) | Centripetal | Peak \|a\| incl. gravity |
|---|---|---|---|---|
| Thorax | ~730 °/s pro mean, up to ~1000 | 12.7–17.5 | **1.7–4.6 g** | 3–4 g |
| Pelvis | ~480 °/s, up to ~700 | 8.4–12.2 | **0.7–2.3 g** | 2–3 g |

- **Range:** gyro ±2000 °/s, accelerometer ±16 g. Neither saturates. Confirm both settings on the units in hand.
- **During the downswing the accelerometer is not measuring gravity.** A filter that corrects tilt toward the accel vector tilts the trunk by tens of degrees at exactly the moment we read it (§7.2).
- **Raw, not device AHRS.** Use raw accel and gyro and our own fusion, never the device's internal orientation, so orientation is reproducible and tied to our calibration.
- **Rate:** default 100 Hz, 200 Hz available (register `0x03`). KS peak timing wants 200 Hz with parabolic peak interpolation. Three Witmotions at 200 Hz, with and without a wG3, is **unmeasured** over BLE on both the Mac and GOLFSIMPC. Day-1 measurement.
- **Gyro scale factor:** 1–2 % turns a 90° shoulder turn into ±1–2° and a 700 °/s peak into ±10 °/s. Timing is unaffected. A σ term (§8.2), not a calibration step.
- **Bias:** a residual 0.1–0.5 °/s over a 1.5 s swing drifts 0.15–0.75° of yaw, negligible against the garment term.

### 4.3 The lever arm r

The hip sensor sits about 0.10–0.15 m posterior to the pelvis rotation centre (the midpoint of the hip joint centres, HJC-mid). Its measured acceleration includes rotation terms **larger than gravity** in the downswing:
- centripetal ω²r: 500 °/s ≈ 8.7 rad/s at r = 0.12 m → **9.1 m/s²**;
- tangential αr: α ≈ 5000 °/s² ≈ 87 rad/s² → **10 m/s²**;
- the load signal they swamp is about ±0.3–0.5 g.

**r** (sensor relative to HJC-mid, in the sensor frame) matters only to Part B and C. Rotation (Part A) and the KS (Part K) do not need it. Per golfer, from §6.4, with no extra step for the golfer. For the chest sensor, transport to a point on the spine at the sensor's level; a stature-based default (≈ 0.05 m posterior) is adequate.

---

## 5. Frames

### 5.1 The defect

`solveSegment` builds the segment basis with e_y the long axis, pointing distally: **down** at the hanging reference. It then sets `A = conj(refRaw·M)`, making the reference pose the identity, so the "world" q_anat lives in is the body frame at the reference pose. For the arm, world +Y is down. The trunk consumers disagree:

| Consumer | Vertical it assumes | File |
|---|---|---|
| `fillFromImu`: ML axis bearing in world XY | world Z | `body_rotation.cpp:90` |
| `axialFromImu`: `(qAnat·gyro).z` | world Z | `segment_rates.cpp:1268` |
| Transition: `axialRate(pel, (0,1,0))` | segment Y | `phase_segmenter.cpp:290` |
| skeleton3d IMU term: `Qi⁻¹·Rz(ψ)·R·M` | world Z, heading-only freedom | `skeleton3d_fit.cpp:734` |

The skeleton3d term is the subtle one: it gives the fit a free heading ψ between the IMU world and the fit's world but no free tilt. With Qi's world the reference-pose body frame, a constant tilt sits on the other side of the product where the mount cannot absorb it, and the term fights the cameras.

### 5.2 The trunk frame

The trunk segments and their calibration keep **the world gravity-aligned, Z-up**:

| Axis | Pelvis | Thorax |
|---|---|---|
| **+X** | medio-lateral, toward the golfer's right | same |
| **+Y** | anterior | same |
| **+Z** | superior | same |

Right-handed (right × anterior = up). +X stays the ML axis `fillFromImu` projects.

**A is heading-only for the trunk.** The fused world is already gravity Z-up (6-axis). The trunk A is a rotation about world Z alone, turning the arbitrary fusion yaw into the *stance frame*: +Y toward where the golfer faced during stand tall (toward the ball). ⚠ R3: for a right-hander, +X (golfer's right) then points **away from** the target, not toward it as the 4 Oct text said. Fix in the contract's trunk rows; the sign conventions doc carries the lead/trail flip.

So the trunk q_anat is the true segment orientation in a gravity-up, stance-headed world: forward bend at address reads as forward bend, and world z really is vertical for every consumer.

**Why not reuse the limb solve:** it would zero the reference posture (the trunk's posture *is* a measurement) and leave the vertical on Y. The limb convention stays as it is; the contract gains a trunk row group.

**Owners.** `imu_frame_contract.md` owns the frames; `pinpoint_sign_conventions.md` owns the signs. The briefs' request for a single `docs/Frames.md` is met by those two; reconcile the camera world (calibration, alignment-stick mode) against them in the same edit.

**Handedness.** Metrics are handedness-neutral: turn positive in the backswing direction, obliquity and side bend positive lead side up, regardless of hand. Where the flip happens is D15.

### 5.3 Angle definitions (D13)

Two candidates, both signed as above, both relative to address unless stated:

**(a) Cardan, from the 4 Oct design.** Rotation, then obliquity, then tilt (Z, Y, X on this frame's labels) in the stance frame. Check against ISB Part I (Wu 2002) before coding; Rule 0 applies if ISB names an order for segment-to-global angles.

**(b) Projections, from the brief.** Coaches' vocabulary maps onto projections of segment axes, which avoids sequence dependence at large angles:
- **Turn:** heading of the ML axis projected onto the horizontal plane, relative to its address heading.
- **Obliquity / side bend:** elevation of the ML axis above horizontal; lead side higher positive.
- **Forward bend / tilt:** inclination of the long axis from vertical, anterior positive.
- **X-factor:** thorax turn − pelvis turn. **X-factor stretch:** max X-factor in early downswing minus X-factor at top (Cheetham 2001; check against the NR entry).

(b) is what the 4 Oct δ-sensitivity table (§6.2) was computed with. Recommendation: **(b)**, with the axial-rotation definition stated next to it as the conventions doc requires.

**Measurement site.** Back-mounted thorax rotation is spine rotation at T2–T5, not the shoulder line used in some literature. Norms must match the site; each descriptor's provenance says so.

### 5.4 The KS rate definition (decided, D3)

Cheetham read pelvis and thorax rates about the **segment's superior–inferior axis**. `axialFromImu` reads about **world vertical**. They differ by the cosine of forward bend (thorax 35–45° at address), and the difference changes through the swing, moving peak values and possibly the order.

**Decided 4 Oct:**
- **Reported rate = the trunk's own S-I rate:** `(gyro_segment)·ẑ_segment`. The long axis comes from gravity at stand tall, so the rate does not depend on δ.
- **The cameras do not define the rate.** The skeleton3d IMU term solves the camera-to-IMU heading ψ; camera routes are graded (and where needed corrected) by projecting the fused trunk into what each camera sees. This is how a face-on camera not square to the target line, or a DTL off the hand line, is managed.
- **What the fusion learns carries to camera-only swings:** the per-session camera heading and each camera route's bias against the trunk, stored with the bay (§8.6).
- The world-vertical component is kept only as an internal series for grading (§14). Never shown.

---

## 6. The calibration ceremony — standing and turning only

**Mark's requirement (4 Oct, D10):** no hip hinge, no bending at the waist. Some golfers cannot do it reliably. Standing still and turning while standing only.

### 6.1 What standing and turning can and cannot give

For each sensor: **M**, the mount (3 DOF) = the sensor's tilt on the body (2 DOF) + the mount yaw δ (1 DOF); **A**, the heading (1 DOF).
- **Standing still gives "up" exactly** — gravity in each sensor is +Z, including the pocket's lean on a curved back.
- **Turning while standing gives nothing about δ** (R2). An upright trunk turns about +Z, which is known; every mount yaw sees the identical gyro. Only rotation about a horizontal axis (hinge, side bend) separates them.
- **So δ comes from the mount, then the swings.** The pocket and the belt pouch fix the sensor face-out on the midline (§4.1); "forward" is straight through its face (δ0). Swings refine δ (§6.5), because every address is a forward bend the golfer makes anyway.

### 6.2 What a wrong δ costs

Mount-yaw error δ, trunk bent forward by θ (thorax 30–45° at address, pelvis 15–25°):

| Metric | Sensitivity to δ | At δ = 10° |
|---|---|---|
| S-I rates (KS) | **None** | 0 |
| Peak times, sequence order | **None** | 0 |
| Turn from address, X-factor | atan(tan δ·cos θ), changing only as θ changes — for a turn about world vertical. For a turn about the bent spine itself it grows with the turn (`trunk_e2e_test`, 17 Oct) | about 1–2° turning about vertical; about the spine: pelvis (θ 20°) 0.5° at 45° and 1.2° at 90° of turn, thorax (θ 40°) 1.6° at 45° and **5.3° at 90°** |
| Forward bend / tilt | none with the projection definition (inclination of q·ẑ; `trunk_e2e_test`, 17 Oct) | 0 |
| **Side bend / obliquity** | **First order: asin(sin θ·sin δ)** | **4° at θ = 25°, 6.4° at θ = 40°** |
| Load: vertical | second order (gravity axis known) | small |
| **Load: lateral push, lead fraction** | **First order** (the target-line axis is the heading, which δ shifts through the bend) | to be computed in the synthetic test |

The headline uses (KS, peak times, turn, vertical load) are immune or nearly so. The side-bend family and the lateral load need δ, and their σ says so until δ is refined. The synthetic end-to-end test (§14.1) must reproduce this table.

### 6.3 The steps — the golfer's whole ceremony

**Four steps, about a minute, no equipment beyond the alignment stick.** This is the whole of it, for every part of this document. Load (Part B) and GRF (Part C) add **no** steps: what they need beyond this comes from the swings and the cameras (§6.4).

| Step | The golfer is asked | What it gives us | What it checks |
|---|---|---|---|
| **1. Stand tall** | "Stand tall, toes to the stick, arms relaxed. Hold still." (2 s; the hold bar fills) | **+Z** per sensor from gravity; **δ0** from the prescribed mount (forward = horizontal projection of the inward face normal); **A** from the facing direction; gyro bias; sample rate | **Mount:** long (USB) axis within 35° of up, face normal within 40° of horizontal. **Sensor health, automatic:** bias < 0.5 °/s each axis; \|f\| = 1 g ± 2 %; ≥ 95 % of expected samples. |
| **2. Turn away** | "Cross your arms on your chest. Turn slowly as if to the top, and back. Twice." | **Checks, not solves** | Rotation axis within 15° of +Z on both sensors; ≥ 60° range; "away" on both (catches upside-down; **not** face-in, 17 Oct — see below); chest turns more than hips (catches swapped sensors) |
| **3. Turn through** | "Now the same, toward the target. Twice." | Same checks, other direction: the sign confirmed both ways (the HackMotion lesson: sign is the only thing that falsifies a frame) | As step 2 |
| **4. Hop** | "Stand where you address the ball. One small hop, straight up." (in view of the cameras) | **The session's IMU ↔ camera clock offset** (§7.1): the landing spike in both sensors' vertical acceleration against the video pelvis's vertical acceleration | Residual < 5 ms; the two sensors agree within 5 ms. A golfer who cannot hop may skip it: the offset then falls back to per-swing cross-correlation, flagged. |

Then the live check (§6.7): the golfer turns freely and watches the figure follow.

**What is deliberately not in it,** and where each thing comes from instead:

| Not asked | Would have given | Comes from instead |
|---|---|---|
| Hip hinge or side bend | δ, the mount yaw | The prescribed mount, refined from the swings' addresses and the two-camera fit (§6.5) |
| Address hold | Heading zero, address posture | Every swing's own address (re-referenced per swing) |
| Stool rotation | The hip lever arm r | A stature-scaled default refined from the two-camera fit (§6.4). The stool is a validation test in the truth session only. |
| Walking | δ | Dropped (D11) |

If a gate fails, the golfer is told which sensor and what to do, and repeats only that step.

**Face-in cannot be caught by stand tall or the turns** (`trunk_e2e_test`, 17 Oct). A sensor worn face-in is a 180° rotation about the spine. Gravity cannot see it, and an upright turn is about that same axis, so turn and the S-I rate still read correctly. Forward bend and obliquity flip sign: a 40° address bend reads −40°. The check is therefore the first address: a forward bend that reads negative there means a face-in sensor (§6.8 baseline).

**The solve:** e_z = gravity-up at stand tall; e_y = the inward face normal made orthogonal to e_z; e_x = e_y × e_z. A is the yaw about world Z taking e_y onto stance +Y. `solveTrunkSegment()`, a pure function in `imu_calibration.h`, unit-tested, never inside QML.

**Failure messages name the sensor and the cause:** "Chest sensor reads the turn backwards — check it sits USB-up, label facing out"; "Hip and chest sensors look swapped"; "Hip sensor: turn it USB-up, label out".

### 6.4 The hip lever arm, without a golfer step

r (§4.3) is found the way δ is: a default, then refined from data the golfer already gives.

| Tier | Needs | How |
|---|---|---|
| **r0 Default** | Height (already in the golfer profile) | Sensor on the sacrum (§4.1), HJC-mid from a stature-scaled pelvis (Harrington 2007). σ_r about 2–3 cm per axis until measured. |
| **r1 Two-camera fit** | Face-on + DTL | skeleton3d gives the HJC-mid trajectory; its acceleration, rotated into the sensor frame, against the transported IMU acceleration, solves r by robust least squares over the session's swings, under the r0 prior. Pooled per session, like δ2. |

The binding records the tier and σ_r; every load metric carries σ_r (§11.4). Rotation and the KS never read r.

**The stool test, as validation only** (truth session, §13): on a swivel stool with the feet off the floor, the pelvis centre is roughly stationary, so in the sensor frame

$$
\mathbf{f}_S + R_{WS}^{\top}\mathbf{g}^W = [\boldsymbol{\alpha}]_\times \mathbf{r} + [\boldsymbol{\omega}]_\times[\boldsymbol{\omega}]_\times \mathbf{r} = M(\boldsymbol{\omega},\boldsymbol{\alpha})\,\mathbf{r}
$$

Stack M and the gravity-corrected specific force over samples with |ω| > 60 °/s and solve **r** by robust least squares (Huber), reporting residual and condition number. It measures how good r0 and r1 are; it is never asked of a golfer.

### 6.5 Refining δ from the swings

| Tier | Needs | How |
|---|---|---|
| **δ0 Prescribed** | Nothing | Stand tall. σ_δ ≈ 10° until the truth session measures the real pocket error. |
| **δ1 Address symmetry** | Any swings | At address the trunk bends mostly forward, so gravity in the segment frame should lie in the sagittal plane; the median over the first swings gives δ. Its bias is real address side tilt, corrected by the face-on spine side bend when present. Without it σ_δ stays wide on the thorax (irons and especially driver have side tilt). |
| **δ2 Two-camera fit** | Face-on + DTL | skeleton3d solves the mount with δ free under a σ_δ prior around δ0, pooled over the session. The best tier. |

The binding records which tier set δ and its σ, so a re-analysis can improve it (δ1 → δ2) without the golfer.

### 6.6 The heading reference

1. **"Face the camera."** No equipment; biases absolute turn by a few degrees, not turn from address.
2. **Alignment stick (recommended, D5).** Toes against the stick the camera ceremony uses (memory note `camera-calibration-thread`, G2), so the IMU stance frame and the camera world share one physical reference. When cameras are live, the IMU world heading is anchored to the camera's target line.
3. **Camera-solved per swing.** The skeleton3d term solves ψ per IMU per swing; with M known, ψ *is* the IMU-to-camera heading, giving an absolute pelvis heading at address on every swing.

**Drift between swings.** 6-axis yaw drifts at the residual gyro bias. Within-swing readings re-reference to each swing's address, so drift matters only to an absolute "open at address" reading, which needs tier 3. The magnetometer is not pursued (D9): heading comes from the stick and the cameras.

### 6.7 One ceremony with the arm; the guide and the live check

**Configurations** (D1, D2: users own three Witmotions; a wG3 on the wrist with Witmotions on the trunk is allowed):

| Configuration | Calibrate | Check |
|---|---|---|
| Trunk only (2 Witmotions) | Stand tall, turn away, turn through, hop — about a minute | Live trunk model |
| Trunk + HackMotion | wG3 device routine with its existing guide, then the four trunk steps | `ArmVizView` wrist check, then the live trunk model |
| Trunk + 1 arm Witmotion (lead upper arm) | Arm down = stand tall, T-pose, turn away, turn through, hop | Live whole-body model with the lead upper arm |
| Arm only (3 Witmotions) | Unchanged | Unchanged |

The trunk sensors ignore the T-pose; the arm sensors ignore the turns. A trunk-plus-arm session never has more than three Witmotion links, plus the wG3's.

**Built as a session-setup calibration routine** (R13): a trunk routine behind the guide interface, gated on the activity flag, so leaving stops it. The setup flow already declares the trunk group (`SetupContext.qml:386`, minimum pelvis AND thorax); offering it is the change. The raw ceremony data (samples and step boundaries) is **recorded with the session**, so swinglab and the unit tests replay the solve without the golfer. Verify with `session_setup_ui`.

**Demonstrate, then verify** (Mark, 4 Oct: required, not polish). Page 1, the avatar guide: `BodyVizView` demonstrates each step, then waits; a hold bar fills while still. Turn away shows arms crossing, hips about 45° and chest about 90° away, a pause, back. Page 2, the live check: `BodyVizView` in **IMU mode**, whole-body framing — `hipsNode` from the pelvis q_anat, the spine chain from the thorax relative to the pelvis, plus the lead upper arm when present. One line of numbers beside it, per the tile rule: **hips 45° · chest 88° · X 43°**. Turning away must turn the model away: the sign check made visible. Optional, never gated: "take your address" — if the avatar leans sideways while the golfer bends straight forward, a sensor is twisted in its pocket.

**To build:** `useTrunkOverride`, `hipsOverrideRotation`, `thoraxOverrideRotation` (spread over Spine/Spine1/Spine2 by segment length), mirroring the arm overrides (`BodyVizView` l.117–125). Crossed arms reuse the arm overrides.

**Rendering traps, all paid for once:**
- **Frames:** world→scene is a fixed Rx(−90°), but every bone also carries a per-GLB rest offset (`imu_frame_contract.md` §6). A trunk bone bound with only the basis change looks plausible and is wrong. Pin it with a viz golden test.
- **Render, don't reason:** check every animation and the live view through a `--probe-qml` script (memory note `verify-ui-by-probing`). Qt Quick 3D traps: `clipNear` 10, an explicit `camera:` inside a Repeater.
- **Soak:** View3D views have gone blank before (memory note `view3d-disappearance-watch`); same 6.5-minute render soak as the arm check.

### 6.8 After the ceremony: slip and staleness

- **Per-swing address residual.** Baseline = median address posture of the first three swings (pelvis tilt, thorax bend, thorax-relative-pelvis heading). A step change beyond a gate (about 8°, to tune) that persists is a **slip**: a session toast ("The chest sensor has moved — recalibrate when convenient") and the swing's trunk σ widened by the residual (D7).
- **End-of-session re-check.** Optional stand tall + turn away at the end: the mount drift over the session, the number §4.1 needs. Twenty seconds.
- **Persistence:** per session, snapshotted per shot into the binding: `alignA`, `mountM`, gate residuals, `calibratedAt`, the δ tier and σ_δ, the r tier and σ_r, sensor serial.

---

## 7. Signal pipeline

```
BLE packets → arrival stamps + sequence index
  → re-timed timeline (index × nominal period, robust fit to arrivals)   §7.1
  → bias / scale correction (calibration record)
  → trunk re-fusion: gyro-dominant, accel gated, two-sided tilt anchor   §7.2
      → q_anat (trunk frame, §5.2)  ─────────────►  Part A consumers (§8)
  → ω (bias-corrected, sensor frame); α = dω/dt (constant-jerk smoother + RTS)
  → transport acceleration to the segment centre                         §7.3
  → rotate to world, remove gravity → a_C per segment
  → two-point COM acceleration                                            §7.4
  → events (§3.5) → load producers (§11) → GRF (§12)
```

### 7.1 The clock

Witmotion samples are stamped at host arrival (`EventBuffer::nowMicros()` in the `quaternionUpdated` lambda, `imu_instance.cpp:128`), on the video clock, quantised to BLE connection events, with an unknown mean offset from exposure (video frames are stamped at exposure end; memory note `camera-timestamps-from-the-camera`). There is no clock fit; HackMotion alone has one.

1. **Re-time:** sample index × nominal period, anchored by a robust linear fit to the arrival stamps per stream. The HackMotion `clockFit` idea, needing no device clock. Keep the raw arrival times; derived timelines are recomputable.
2. **Measure the offset each session with the hop** (ceremony step 4, D14): cross-correlate both sensors' vertical acceleration through the landing with the video pelvis's vertical acceleration (face-on at 150 fps). One offset per session, with its residual.
3. **Check per swing:** cross-correlate the IMU pelvis yaw rate with the camera route's over Top→P8. Lag and σ written as provenance; applied only once the paired table shows it stable (D6); flagged over 10 ms.

Without step 3, the KS mixes IMU trunk nodes with camera arm and club nodes on two clocks.

### 7.2 The trunk orientation estimator

Re-fuse the trunk lanes offline from recorded raw accel + gyro (`imu_vision_fuser.cpp`, `filter.refuse`) with a trunk profile:
- gyro bias estimated over the address stillness;
- accelerometer correction gated off while |a| departs from 1 g or |ω| exceeds a threshold, so the swing integrates on the gyro alone;
- tilt re-anchored at the finish stillness by distributing the end-point gravity error linearly across the swing (two-sided); yaw stays one-sided.

It writes the corrected quaternion into `SegmentStream.qAnat`, so every consumer benefits unchanged. Gated on the role, so the arm lanes stay byte-identical.

**Measure before building:** re-fuse one worn swing both ways on day 1; the finish-versus-address gravity residual says how much tilt the live filter lost.

### 7.3 Acceleration transport (Part B and C only)

The sensor is rigid on the segment at offset **r** from the segment centre C:

$$
\mathbf{a}_C = \mathbf{a}_S - \boldsymbol{\alpha}\times\mathbf{r} - \boldsymbol{\omega}\times(\boldsymbol{\omega}\times\mathbf{r})
$$

Compute in the sensor frame (where ω, α and r are native), rotate with the §7.2 orientation, remove gravity. §4.3 shows why this is mandatory: both correction terms exceed the signal.

### 7.4 Two-point COM acceleration

$$
\mathbf{a}_{COM} \approx w_p\,\mathbf{a}_{C,pelvis} + w_t\,\mathbf{a}_{C,thorax}
$$

Initial weights from de Leva (1996), legs lumped with pelvis and head with thorax: w_p ≈ 0.55, w_t ≈ 0.45. Arms and club (≈ 11 % of system mass) are omitted, or added from video when the shaft track is present (§11.3). The weights are parameters fitted at the reference session; until then `experimental`.

### 7.5 Filtering

Zero-phase (RTS) throughout. **Cut-offs (D16, the brief's values for now):** angles 15 Hz; ω for peak detection 20 Hz; α via the constant-jerk smoother tuned on the logged corpus; load 12 Hz. The useful GRF band is about 0–15 Hz. Every metric's provenance records its cut-offs.

**Why these are low although the sensors are fast.** The cut-off is set by where the motion ends and the noise begins, not by the 150 fps / 200 Hz sample rates: trunk motion lies below about 10–15 Hz, and above it is sensor noise, garment wobble and the strike's shock arriving at impact. Differentiated signals (α, video COM acceleration) need low cut-offs because differentiation amplifies noise with frequency. The gyro rate is different: it is measured, not differentiated, and Cheetham used no filter, so 20 Hz probably understates the KS peaks (NR-09). Revisit when the attenuation is characterised; the §14.1 sensitivity study is the tool when it is wanted.

---

## 8. Analysis changes, by consumer

### 8.1 Rotation metrics: catalogue keys

The briefs' `rot.*` ids map onto the catalogue (R11). Existing keys gain an IMU route; the diagnostics pick it up through route precedence.

| Brief id | Catalogue key | Status |
|---|---|---|
| `rot.pelvis.turn.top/.impact` | `pelvisRotation` (at P4, P7) | IMU route exists, never run |
| `rot.thorax.turn.*` | `thoraxRotation` | as above |
| `rot.xfactor.top/.impact/.stretch` | `xFactor`, `xFactorStretch`; new measure `m_xFactorP4` | as above; measure new |
| `rot.pelvis.omega.peak/.time` | `pelvisAngularSpeed`, `pelvisPeakTime` | as above |
| `rot.thorax.omega.peak/.time` | `thoraxAngularSpeed`, `thoraxPeakTime` | as above |
| `rot.sequence.pelvisThoraxGap` | `pelvisThoraxPeakGap` | new |
| `rot.sequence.gainRatio` | `speedGainPelvisThorax` | new |
| `rot.pelvis.tilt.*` | `pelvisObliquity` → route on `hipLineTilt` | new route |
| `rot.thorax.sideBend.*` | `thoraxSideBend` → route on `spineSideBend` | new route |
| `rot.*.forwardBend.*` | `pelvisTilt`; `thoraxForwardBend` → `spineForwardBend` (PLANNED `trunkImus`) | new / PLANNED |
| `rot.*.turn.series`, `rot.xfactor.series` | `pelvisRotationSigned` (PLANNED IMU route), new `thoraxRotationSigned` | fixes defect 2 |

### 8.2 σ for IMU rotation

Per sample, in quadrature, `SigmaKind::Propagated`:
- **Garment / soft tissue:** 3° thorax, 2° pelvis as placeholders, replaced by §13's measurement.
- **Calibration:** the ceremony's axis residuals.
- **Gyro scale:** 1.5 % · |turn from address|.
- **Bias drift:** residual bias σ × time from address.
- **Slip:** §6.8, when flagged.
- **Mount yaw:** σ_δ through §6.2 — first order for side bend and obliquity, near zero for turn, zero for S-I rates and peak times.

Closes the census rows for the IMU tier ("IMU readings reach diagnostics with no σ at all"). Display per memory note `feedback_no_sigma_rounding`.

### 8.3 Body rotation

- **Signed series:** build the IMU route for `pelvisRotationSigned` and a new `thoraxRotationSigned` (defect 2).
- **X-factor from the fused skeleton first (D4).** With two cameras, skeleton3d fits the body with the IMU term in it, so the fused pelvis and thorax come from one fit with one heading and a σ that knows which source constrained each sample. That is the X-factor route above both "IMU only" and "cameras only". **Only where there is no fusion** (one camera, one trunk sensor): IMU segment + camera segment, both relative to address, σ in quadrature, route `trunkMixed`, Estimated. Today such a swing gets none (`body_rotation.h:196`).
- **Posture metrics** (§3.2) from the §5.3 decomposition.
- **Measures to add to `core.json`:** `m_xFactorP4`; `m_pelvisTiltChangeImpact` (companion to `m_pelvisThrustDown`); `m_thoraxBendChangeImpact` (loss of posture); pelvis deceleration present/absent. Norms wide and labelled one-golfer.
- Remove the stale foreshortening descriptions (`body_rotation.h:32-77`, catalogue `howToRead` for pelvis and thorax rotation).
- `BodyRotationStage::run` dereferences `ctx.job.cameraSources.front()`: must cope with an IMU-only job.

### 8.4 Kinematic sequence

- Rate per §5.4 (S-I, D3).
- The thorax node from the IMU is placed. `pairTrunkThoraxPlacement` and `skel3dThoraxPlacement` stay as they are for camera-only swings until §14.4 grades them.
- Mixed IMU and camera nodes carry the measured lag in σ_t (§7.1).
- Cheetham's speed gain and timing consistency across a session (KS design §11 Q3).

### 8.5 Phases

- **Fix defect 3 first:** `SegResolveStage` adopts the IMU segmentation only when `segImu->conf > 0`, or arbitrates per slot as `timeline-fusion.md` proposes.
- **Trunk evidence in the ladder** (§3.5), under the timeline-fusion arbitration, not beside it.
- `phase_segmenter` reads the trunk axial rate on world Z through q_anat (defect 1).

### 8.6 skeleton3d

The wiring exists (`analysis_pipeline.cpp:3050`: Pelvis → Hips, Thorax → Spine2). It needs:
- the trunk q_anat (gravity Z-up) so the heading-only ψ is the right freedom;
- `hasMount = true` from the calibration (never set today), mount prior 3°;
- the §8.2 σ per sample replacing the fixed `imuSigmaDeg` = 4;
- with a pelvis IMU, `pelvisYawAccRad` and `pelvisTiltAccRad` relaxed toward generic smoothness;
- IMU ψ in the session pool, one per sensor per session, so a camera epoch change and a vest slip are distinct events;
- the **clavicle split** as a diagnostic series (shoulder line minus spine twist), testing the lean rig's "clavicles follow the upper arm" rule (memory note `skeleton3d-lean-fit`);
- per session, each camera's heading against the trunk world and δ (tier δ2), written with the camera epoch; later camera-only swings in that epoch inherit them;
- a `kSkeleton3DStageVersion` bump (memory note `version-gated-reanalysis`).

### 8.7 Placement, roles, devices

- **Role-keyed placement: done** (6290b65f), with a one-time migration from slot letters and one claim per mount. Pelvis and thorax are defined, not yet offered. Offering them is the remaining step.
- Confirm the Witmotion binding (`shot_processor.cpp`, formerly `placement.value(deviceId)`) and the HackMotion scan read roles, and that `segmentRoleForSlot()` survives only for old swings (memory note `analysis-is-session-agnostic`).
- `SegmentRole` is persisted as an int: mark it APPEND-ONLY.
- **Integrity:** before the first trunk capture, run `--refuse-orientation` on one worn swing. If live and re-fused disagree (the Witmotion history says they will), the trunk lanes take the HackMotion-style exclusion until understood, so every shot does not carry a false ⚠.
- **Mixed instruments (D1):** HackMotion and Witmotion bindings coexist in one shot; the placement guard refuses only a second claim on the same role; `imuIntegrity` skips the wG3 lanes alone and judges trunk lanes on their own.

### 8.8 Held for the IMUs: the trunk-bend change at the top (22 October 2026)

`m_spineBendChangeTop` (forward bend, P1 → P4) is **held**: produced and shown, never graded, until a thorax and a pelvis sensor can corroborate it. It is a down-the-line reading of the hip-midpoint → shoulder-midpoint line, and at the top both midpoints move with the turn rather than with the posture. The shoulders go from end-on at address to spread across the image, and the trail hip rotates forward, so the line tips upright when the back has not changed. Mark reviewed annotated P1/P4 frames on 22 October and judged it measurement error.

| Golfer (Mark's coach view) | DTL shoulder-midpoint line, P1 → P4 | Hip → ear-midpoint line, same frames |
|---|---|---|
| Nick Henn, 28 swings (holds his spine angle, "rock solid") | −8.2° ± 2.2 | −3.6° ± 2.2 |
| Mark, 111 swings | −7.4° ± 1.8 | −1.9° ± 1.9 |
| Jess Ratcliffe, 23 swings (dips going back) | +2.0° ± 1.7 | +5.8° ± 1.8 |

Scored against 0 ± 2.5°, the reading made `loss_of_posture_backswing` fire on every swing Nick hit. The IMUs remove both failure modes: the sensors sit on the segments, so there is no occlusion and no keypoint travel with the turn.

**What the IMU work owes here:**
- `thoraxForwardBend` (§3.2) as the `trunkImus` route on `spineForwardBend`, from the §5.3 decomposition, with the §8.2 σ.
- The reading at the top **relative to the pelvis**: thorax bend minus pelvis tilt, so that a pelvis that tips under the trunk is not reported as the trunk standing up.
- A grading pass on worn swings recorded with both cameras (§14.4): the DTL line against the IMU at P1 and P4. That pass decides whether the camera line can be corrected (its error looks systematic, about 7°, but has been measured on two golfers only) or stays held on camera-only swings.
- Then release the hold: `m_spineBendChangeTop` back to `live`, with the IMU rung first in its ladder. The camera rung is live only if §14.4 passes it. Re-seat its norm from the worn swings, labelled one-golfer.

**What fires meanwhile.** "Standing up going back" (`loss_of_posture_backswing`) now fires only on a measured rise of the head seen down the line (`m_headRiseBackDtl`, ear midpoint, 0 ± 3 cm ceiling; `posture_metrics_design.md` §4.5). The trunk-bend change is shown as a reading and fires nothing.

---

## 9. What Part A delivers (definition of done)

| Deliverable | Done means |
|---|---|
| Trunk placement and calibration, in the app | Mark straps on vest and belt, runs the ceremony, sees both segments track in the live check. A deliberately mis-seated sensor fails and the message names it. |
| Trunk analysis | A captured swing yields measured pelvis and thorax rotation, signed series, rates and placed KS nodes, each with σ. `hip_stall` and `sequence_order` assessable. Trunk-only jobs keep the vision ladder. |
| skeleton3d fusion | Trunk IMUs constrain the fit with a calibrated mount; the pelvis no longer coasts through square on a worn swing. |
| Trunk-bend hold released (§8.8) | `m_spineBendChangeTop` reads from the thorax and pelvis IMUs on worn swings, graded against its DTL camera rung on two-camera swings, and is live again. |
| Camera-route grading | `docs/research/data/trunk_imu/` holds the per-swing IMU-vs-camera table and a note giving the measured σ and bias of every camera trunk route and the decisions they imply. A failed gate, written up honestly, counts. |
| Tests | The synthetic end-to-end test (§14.1). |

---

# Part K — The kinematic sequence in the diagnostic model

## 10. Scoring and ranking the kinematic sequence

The KS is measured (the `kinematicSequence` Sequence object, `kinematic_sequence_design.md` §3.2) and shown (the tile), but the diagnostic model barely reads it. This section adds it to the model: what can be wrong with a sequence, how each is judged, the nodes and edges that carry it, and how swings and faults are scored and ranked. It works on every route the KS already has. Worn trunk sensors (Part A) make the trunk checks measured, not estimated.

### 10.1 What the model has today

| Node | What it is | Gap |
|---|---|---|
| `m_pelvisPeakTime`, `m_thoraxPeakTime` | Peak times, ms from impact | — |
| `sig_sequenceOrder` → `sequence_order` | `order` test, pelvis before thorax, σ in quadrature | The only order check. The arm and club nodes never reach the model. |
| `m_clubheadPeakLead` → `sig_deceleration` → `deceleration` | Club peak before impact | — |
| `m_pelvisRotRateP6P7` → `sig_hipStall` → `hip_stall` | Pelvis rate late in the downswing | Loses its producer when a hip sensor is worn (defect 2) |
| `late_pelvis_rotation`, `transition_rush`, `hip_spin_out` | The start of the downswing, read from turn and tempo | No reversal-order check |
| `xfactor_deficit`, `excessive_separation_stretch` | Separation corridors | See D26 |

Not in the model at all: the peak **magnitudes in relation to each other** (the speed gains), pelvis **braking** (NR-12), the **arm and club** order, the **transition** order, and **consistency** across a session, which is the one thing the literature says discriminates skill (NR-04, NR-11).

### 10.2 The rules that bound it

From `golf_swing_normative_reference.md`:
- **Order is categorical** and the most robust surfaceable statement (§2.3; NR-05 as amended: ✓/✗ against the professional order).
- **No verdict on closeness to a published magnitude** (NR-03, NR-05). The tile may show each peak as a % of Cheetham's professional peak; a characteristic may not fire on it.
- **No single-swing timing verdict against a published mean** (NR-11). Timing is judged as dispersion over a set.
- **Pelvis braking is present/absent** (NR-12).
- **X-factor is descriptive** (NR-13); no elastic-energy language (NR-14).
- **Any composite score is PinPoint's own** and carries its own validation evidence (NR-16).
- **Smoothed peaks are not compared with Cheetham's unfiltered values** until the attenuation is characterised (NR-09).

**So "magnitudes in a good range in relation to each other" is judged as ratios between neighbouring peaks.** The principle is speed summation: each segment should end up faster than the one that drives it. **The threshold is 80 % of Cheetham's professional ratio (D23)**, which needs an amendment to NR-05 (§18). Tinmark 2010 found that in every group, every club and every shot length, which makes it a property of a working chain, not a skill benchmark. A ratio has two more virtues here:
- It is **dimensionless**, so a camera route's scale error (`kTriScaleFrac`) cancels when both peaks come from the same route.
- It does **not** separate pros from amateurs. Cheetham's ratios are almost identical in the two groups (thorax/pelvis 1.52 vs 1.48, arm/thorax 1.35 vs 1.31, club/arm 2.30 vs 2.35). So a failed hand-off is a **breakdown**, not "you are an amateur". That is the honest reading and the doc says so wherever it is shown.

### 10.3 The facets of a sequence

| # | Facet | Question | Test | Scope |
|---|---|---|---|---|
| F1 | **Trunk order** | Do the hips peak before the chest? | `order` | swing (exists) |
| F2 | **Arm order** | Does the chest peak before the lead arm? (Cheetham's amateur pattern: arm before thorax) | `order` | swing |
| F3 | **Club order** | Does the arm peak before the club? | `order` | swing |
| F4 | **Chest hand-off** | Is the chest's peak faster than the hips'? | `ratio`, thorax/pelvis | swing |
| F5 | **Arm hand-off** | Is the arm's peak faster than the chest's? | `ratio`, arm/thorax | swing |
| F6 | **Club hand-off** | Is the club's peak well above the arm's? | `ratio`, club/arm | swing |
| F7 | **Pelvis braking** | Have the hips slowed by impact? (every Cheetham professional did) | `ratio`, pelvis at impact / pelvis peak | swing |
| F8 | **Club peak at the ball** | Is the club still accelerating into impact? | corridor on `m_clubheadPeakLead` | swing (exists) |
| F9 | **Transition order** | Do the hips reverse before the chest? (Meister 2011: the transition sequence, distinct from the peak sequence) | `order` on reversal times | swing |
| F10 | **Consistency** | Is the order repeatable, and is the timing spread tight? | session statistics | session |

F1–F7 and F9 need placed nodes. A node that is `unresolved` (σ too wide to place) gives no reading, so the check is **unassessable**, never passed. On camera-only swings today the trunk peaks are often unresolved ("still rising at impact"), so F1, F4, F7 and F9 will mostly be unassessable until the trunk sensors are worn. That is correct.

### 10.4 New nodes

**Metrics** (catalogue). The peak values and times come from the Sequence object's nodes, so the tile and the model read one peak finder (`kinematic_sequence_design.md` §6) with one σ.

| Key | Type | From | Unit | σ |
|---|---|---|---|---|
| `pelvisPeakSpeed`, `thoraxPeakSpeed`, `leadArmPeakSpeed`, `clubPeakSpeed` | Summary | node `peakDps` | °/s | node `peakSigmaDps` |
| `leadArmPeakTime`, `clubPeakTime` | Summary | node `beforeImpactMs`, same convention as `pelvisPeakTime` | ms from impact | node `tSigmaMs` |
| `pelvisReversalTime`, `thoraxReversalTime` | Summary | zero crossing of the signed rate after the top (IMU S-I rate; camera signed series) | ms from impact | crossing σ from the slope |

Pelvis speed at impact needs no new key: `pelvisAngularSpeed` read `at` p7.

**Measures** (`core.json`), all `kind: provided`, `status: live` unless stated:

| Id | Metric | Reducer | Label |
|---|---|---|---|
| `m_leadArmPeakTime` | `leadArmPeakTime` | at p7 | Lead arm speed peak, ms relative to impact |
| `m_clubPeakTime` | `clubPeakTime` | at p7 | Club speed peak, ms relative to impact |
| `m_pelvisPeakSpeed` | `pelvisPeakSpeed` | at p7 | Pelvis peak turning speed |
| `m_thoraxPeakSpeed` | `thoraxPeakSpeed` | at p7 | Chest peak turning speed |
| `m_leadArmPeakSpeed` | `leadArmPeakSpeed` | at p7 | Lead arm peak speed |
| `m_clubPeakSpeed` | `clubPeakSpeed` | at p7 | Club peak speed |
| `m_pelvisSpeedImpact` | `pelvisAngularSpeed` | at p7 | Pelvis turning speed at impact |
| `m_pelvisReversalTime` | `pelvisReversalTime` | at p7 | Hips start down, ms relative to impact |
| `m_thoraxReversalTime` | `thoraxReversalTime` | at p7 | Chest starts down, ms relative to impact |
| `m_ksOrderShare` | session: share of swings in professional order (Wilson lower bound) | session (E2) | Swings in sequence, share of session — `status: noProducer` until E2 |
| `m_ksTimingSpread` | session: SD of each placed peak time over the session | session (E2) | Peak-timing spread over the session — `status: noProducer` until E2 |

Each gets `golferHigh` / `golferLow` text in the house voice, e.g. `m_thoraxReversalTime`: high "your chest starts down later, after your hips", low "your chest starts down earlier".

**Signals:**

| Id | Test | Measures (subject first) | Fires when | Threshold |
|---|---|---|---|---|
| `sig_sequenceOrder` | order | pelvis, thorax peak time | exists | — |
| `sig_armBeforeThorax` | order | `m_thoraxPeakTime`, `m_leadArmPeakTime` | arm peaks at or before the chest | — |
| `sig_clubBeforeArm` | order | `m_leadArmPeakTime`, `m_clubPeakTime` | club peaks at or before the arm | — |
| `sig_thoraxGainLow` | ratio, low | `m_thoraxPeakSpeed` / `m_pelvisPeakSpeed` | chest/hips below 80 % of the pro ratio | 1.22 (0.8 × 727/477; D23) |
| `sig_armGainLow` | ratio, low | `m_leadArmPeakSpeed` / `m_thoraxPeakSpeed` | arm/chest below 80 % of the pro ratio | 1.08 (0.8 × 980/727; D23) |
| `sig_clubGainLow` | ratio, low | `m_clubPeakSpeed` / `m_leadArmPeakSpeed` | club/arm below 80 % of the pro ratio | 1.84 (0.8 × 2254/980; D23) |
| `sig_noPelvisBraking` | ratio, high | `m_pelvisSpeedImpact` / `m_pelvisPeakSpeed` | hips still at ≥ 95 % of peak at impact | 0.95 (heuristic, D24) |
| `sig_transitionOrder` | order | `m_pelvisReversalTime`, `m_thoraxReversalTime` | chest reverses at or before the hips | — |
| `sig_ksOrderShare` | threshold, low | `m_ksOrderShare` | fewer than half the session's swings in order (lower bound) | 0.5 |
| `sig_ksTimingSpread` | outsideCorridor, high | `m_ksTimingSpread` | spread above twice the professional SD | norm: pelvis 19, thorax 14, arm 8 ms (Cheetham 2008, 19 PGA tour professionals) × 2 |

The hand-off thresholds are 80 % of the Cheetham professional ratios (19 PGA tour professionals, NR-01), stated with the cohort in `norms.json`; 0.95 for braking is a starting point marked `heuristic`, as `m_clubheadPeakLead` was. Ratios of amateur peaks sit within 3 % of the professional ones, so these fire on breakdowns, not on skill level.

**Conditions** (group `sequence`, kind `fault`, `confirmedBy: measured`):

| Id | Label | `golfer` / `golferWell` | Detected by | State |
|---|---|---|---|---|
| `sequence_order` | Out-of-order sequence | exists | `sig_sequenceOrder` | active |
| `arms_before_chest` | Arms outrun the chest | "your arms race ahead of your chest coming down" / "your chest turns and your arms follow" | `sig_armBeforeThorax` | active — the categorical amateur pattern, `tier: supported` (Cheetham 2008) |
| `upper_body_starts_down` | Shoulders start the downswing | "your shoulders start down before your hips" / "your hips start down first" | `sig_transitionOrder` | active (`tier: supported`, Meister 2011) |
| `chest_not_faster_than_hips` | Hips and chest turn as one block | "your chest doesn't speed up past your hips" / "your chest whips past your hips" | `sig_thoraxGainLow` | draft until §14.4 grades the trunk routes |
| `arms_ride_the_turn` | Arms ride the turn | "your arms don't speed up past your chest" / "your arms whip past your chest" | `sig_armGainLow` | draft |
| `club_not_released` | Club not released | "the club doesn't speed up much past your arms" / "the club whips past your hands" | `sig_clubGainLow` | draft |
| `no_pelvis_braking` | Hips don't brake | "your hips keep spinning at full speed into the ball" / "your hips slow just before impact and pass the speed on" | `sig_noPelvisBraking` | draft (NR-12: present/absent only) |
| `inconsistent_sequence` | Sequence not repeatable | "your body fires in a different order from swing to swing" / "your body fires the same way every swing" | `sig_ksOrderShare`, `sig_ksTimingSpread` (`detection: any`) | draft until E2 |

**And one change to an existing node** (as first drafted; superseded 17 Oct by the single-signal `club_before_arms`, §18 D27): `casting` gains `sig_clubBeforeArm` as a second route (`detection: any`). Cheetham: "casting is directly observable — the club races ahead of the body and peaks early". It changes when `casting` fires, so it goes through the rank-shift report first (D27).

Each new condition gets `consequence`, `golferWhy` and `provenance` in the existing style. `consequence` texts say what the chain loses ("the chest never gets faster than the hips, so the speed the hips made is not passed on") and never claim stored elastic energy (NR-14).

### 10.5 Edges

The edges do the ranking work. **Proximal faults cause distal ones**, so when several checks fail, the posterior ranking (`session_diagnostics_design.md` §A8.6) puts the earliest break in the chain first: fix the hips before the hands. No special KS ranking rule is needed.

**Causes into the KS faults:**

| From | To | Strength | Tier |
|---|---|---|---|
| `transition_rush` | `upper_body_starts_down` | strong | supported (10.1123/jab.27.3.242) |
| `transition_rush` | `arms_before_chest` | strong | practice |
| `poor_pelvic_disassociation` | `chest_not_faster_than_hips` | strong | practice |
| `poor_pelvic_disassociation` | `upper_body_starts_down` | moderate | practice |
| `limited_thoracic_rotation` | `chest_not_faster_than_hips` | moderate | indirect |
| `limited_thoracic_rotation` | `arms_before_chest` | moderate | practice |
| `limited_lead_hip_ir` | `no_pelvis_braking` | weak | practice |
| `late_buckle` | `no_pelvis_braking` | moderate | practice — the lead leg does not brace, so nothing stops the hips |
| `disconnection` | `arms_before_chest` | moderate | practice |
| `arms_over_connected` | `arms_ride_the_turn` | moderate | practice |
| `insufficient_set` | `club_not_released` | weak | practice |
| `tempo_habit` | `inconsistent_sequence` | moderate | practice |
| `transition_rush` | `inconsistent_sequence` | moderate | practice |

**Within the chain (proximal → distal):**

| From | To | Strength |
|---|---|---|
| `upper_body_starts_down` | `sequence_order` | strong |
| `upper_body_starts_down` | `arms_before_chest` | moderate |
| `no_pelvis_braking` | `chest_not_faster_than_hips` | moderate — the hips slowing is what lets the chest overtake (Cheetham's deceleration finding) |
| `sequence_order` | `chest_not_faster_than_hips` | moderate |
| `arms_before_chest` | `casting` | moderate |
| `chest_not_faster_than_hips` | `arms_ride_the_turn` | weak |

**Out to the swing and the ball:**

| From | To | Strength |
|---|---|---|
| `upper_body_starts_down` | `over_the_top` | strong |
| `arms_before_chest` | `over_the_top` | moderate |
| `no_pelvis_braking` | `hips_too_open_at_impact` | moderate |
| `club_not_released` | `face_held_open_impact` | moderate |
| `sequence_order` | `ball_speed_deficit` | moderate (no edge out of it today) |
| `chest_not_faster_than_hips` | `ball_speed_deficit` | moderate |
| `arms_ride_the_turn` | `ball_speed_deficit` | moderate |
| `club_not_released` | `ball_speed_deficit` | strong |

**Corroborates / excludes:**

| From | To | Type | Why |
|---|---|---|---|
| `late_pelvis_rotation` | `upper_body_starts_down` | corroborates | the same late start, read from the turn at P5 and from the reversal order |
| `hip_spin_out` | `no_pelvis_braking` | corroborates | hips open early and still spinning |
| `hip_stall` | `no_pelvis_braking` | excludes | opposite ends: stopped vs never slowing |

**Deliberately no edge** from `inconsistent_sequence` to the strike outcomes: Neal 2007 found that "timing" as golfers feel it tracks strike centredness, not KS lag. The model must not assert it.

Every edge carries `provenance` with `searchedOn` and `searchTerms`, as the pack requires; the `practice` ones are searched before they are authored, and any that cannot be supported are dropped rather than kept on intuition.

### 10.6 The score: what is counted, and what is not

**Per swing: "n of m checks".** *(17 Oct 2026: the count is off the tile and the list stays — §18 D22.)* The KS tile gains one line under the strip: **"Sequence checks: 6 of 7"**, listing each check with ✓, ✗ or "—" (not assessable). The checks are F1–F7 and F9, each read straight from its signal in the engine. This is the framing the literature supports (Meister 2011, §4.1: a count of factors outside the professional band, not a per-factor score), and NR-16 makes it PinPoint's own, labelled so.
- **What it counts:** order and hand-off checks. **What it never counts:** closeness of any magnitude or time to a published mean.
- **σ:** each check carries its signal's pFire. The count shown is the hard count; the engine keeps the expected count Σ(1 − pFire) for ranking. A check whose pFire sits between 0.2 and 0.8 is shown as "?" rather than ✓ or ✗.
- **One source of truth:** the tile reads the engine's readings for these signals (E4). It does not re-derive them from the Sequence object, so the tile and the diagnosis cannot disagree.
- **No headline number.** Per `session_diagnostics_design.md`, there is no session score. The count is per swing, beside the strip.

**Ranking swings in a session.** Order the session's swings by: checks failed with pFire > 0.5 (fewest first), then expected failures Σ pFire, then clubhead speed. That gives "your best-sequenced swing" and "your worst" for the replay's side-by-side, and a Session view that shows the spread rather than an average (memory note `feedback_this_shot_and_session_views`).

**Ranking faults.** The KS conditions compete with every other condition in the posterior ranking. The edges in §10.5 make a root (`transition_rush`, `poor_pelvic_disassociation`, a screened restriction) outrank the KS symptoms it explains, and make the most proximal KS break outrank the distal ones.

**Session consistency** (F10) is surfaced as dispersion (NR-04, NR-11): "In sequence on 9 of 14 swings" with the Wilson lower bound driving the signal, and the per-segment timing spread against twice the professional SD, with the cohort named (NR-01).

**Validation of the count (NR-16).** The count is shown from the start, labelled PinPoint's own (D22). Test alongside that it means something on our data: within a golfer, swings with more checks passed should not have lower clubhead speed and smash. Run on the corpus (one golfer, so evidence of no harm, not of benefit) and on the reference session (≥ 3 golfers). If there is no relation, or it runs the wrong way, the count comes off the tile and the checks stay.

### 10.7 Routes, σ and gating

- **Ratios need both peaks from one scale.** A ratio reading is available only when both nodes come from Direct rungs, or both from the same route (E3). A pelvis from the IMU over a thorax from the pair route would compare two scales, and is unavailable.
- **The ratio test carries no σ today** (`characteristic_engine.cpp`, `SignalTest::Ratio`). E1 adds it by the delta method, σ_q/q = √((σ_a/a)² + (σ_b/b)²), so the hand-off checks get a quantified pFire like the order checks.
- **Thorax checks on camera-only swings** stay unassessable while the thorax KS placement is off on both camera rungs (`segment_rates.h:131`); §14.4 decides whether that changes.
- **NR-09:** ratios are much less sensitive than raw % of Cheetham to smoother attenuation, but not immune: the club's peak is sharper and loses more. Characterise the attenuation per segment before seating the club/arm threshold.
- **With the hip and chest sensors worn**, the trunk nodes come from the IMU rung with S-I rates (D3), and F1, F4, F7 and F9 become measured checks.

### 10.8 Engine and producer work

| # | Work | Where |
|---|---|---|
| E1 | σ for `ratio` signals by the delta method | `characteristic_engine.cpp` |
| E2 | Session-scope measures: a reducer that reads one measure over the session's swings (share with Wilson bound; SD) | diagnostics engine + `session_diagnostics_model.cpp`; until then the two session measures are `noProducer` (memory note `model-completeness-is-the-point`) |
| E3 | Same-scale rule for ratios: the peak-speed measures carry the node's route; the ratio is unavailable across scales | `segment_rates` payload + measure facets |
| E4 | The KS tile reads the engine's check readings | sequence strip / tile |
| E5 | New catalogue keys (§10.4) from the Sequence nodes; reversal times from the signed rates | `segment_rates.cpp`, manifest; stage version bump |
| E6 | Author the nodes and edges; norms for the heuristic thresholds and the spread corridor | `core.json`, `norms.json`, `references.json` (Neal 2007 if absent) |

Tests: a synthetic Sequence per facet (each check firing alone, unresolved nodes giving "not assessable", mixed-scale ratios unavailable), the engine tests for E1 and E2, and the pack lint for the new nodes. Gates: the rank-shift report on the library before and after (the change in roots per session is the deliverable), and per-swing review of every condition that starts or stops firing.

### 10.9 Implementation reconciliations (14 October 2026)

Found when this part was planned against the code (HEAD cae12914):

| # | Issue | Decision |
|---|---|---|
| P1 | §10.4 gives the new peak times the `beforeImpactMs` convention, "same as `pelvisPeakTime`". The two are opposite in sign. | Follow `pelvisPeakTime`: (t_peak − impact) in ms, negative = before impact, one Impact sample (as `sequencePeakTimeSeries()`). The peak speeds and reversal times are emitted the same way. |
| P2 | `m_ksTimingSpread` is one measure with three per-segment norms | One normalised measure: the largest, over the placed segments, of session SD ÷ professional SD (pelvis 19, thorax 14, arm 8 ms). The signal is a `threshold` at 2.0. |
| P3 | Ratios of signed peaks | The peak-speed metrics are \|peak\|. Only placed nodes emit, so an unplaced node leaves the check "not assessable", never passed. |
| P4 | E3 needs to know each reading's route | `MetricSeries` carries `routeId` and `routeDirect` (persisted in swing.json when set); the reading carries them into the engine. A ratio between two readings that both carry routes is unavailable unless both are Direct or the routes match. |
| P5 | The rank-shift report (`rank_shift_report`) is synthetic and cannot compare two packs on the library | A read-only `diag_rank_diff` tool runs both packs over every session and never writes `diagnostics.json`. |
| P6 | Stage version for E5 | Segment rates and the KS have no stage version; they re-run on every analysis, so no bump is needed. |
| P7 | §10.4 and §10.6 feed the Wilson **lower** bound of the in-order share to a `threshold low 0.5` signal. That fires because the sample is small: 3 of 3 in order reads 0.44, 9 of 14 reads 0.39, and both would say "fewer than half in order". | Assert the fault on the Wilson lower bound of the **fault** rate, the ledger's own rule. `m_ksOrderShare` = 1 − `wilsonLowerBound(outOfOrder, resolved)`, the in-order share's Wilson upper bound, and the signal stays `< 0.5`. A swing counts only when F1, F2 and F3 were all assessed (all four peaks placed), and it is in order when none of the three fired. Counts are soft under `softTier` with the warm-up weights. The share is read from 5 such swings (`tuned::sequence::kSessionMinSwings`): at 3 or fewer it cannot fire whatever happens, and from 4 to 7 only a clean sweep out of order fires. The Wilson bound is the uncertainty, so the reading carries no σ and its pFire is hard. |
| P8 | `m_ksTimingSpread` read the raw SD. Placed peaks carry σ_t up to 40 ms (`kMaxPlaceSigmaMs`), against professional SDs of 8 to 19 ms, so the raw SD fires on camera noise. A club change also moves peak timing. | The SD is pooled within club (Σ(n_c − 1)s_c² / Σ(n_c − 1)), less the mean σ_t² of the placed peaks, over the professional SD. The value is the largest such ratio over the segments with at least 4 degrees of freedom. Its σ is s²·√(2/dof) / (2·proSD·max(s_true, proSD)), and the engine's threshold test turns that into pFire. The placed peak instants are persisted per shot (`ShotRecord::peakTimes`, ledger schema 6). |
| P9 | The order checks only compare neighbours, so a link that is not placed ends the check. On 4 July 2026 no chest peak is placed, and the hips peak 20 to 40 ms **after** impact on 14 of 15 swings, after the lead arm (−107 ms) and the club (−49 ms) on swing 8. Every hip check read "not assessable", and hip braking read clean (503 against 863 °/s) because the hips were still speeding up through the ball. (15 October, from Mark's review: "the sequence is related to impact"; "each of the 4 series should be checked independently … in all cases peaking after impact is not good"; "ideally the club peaks at impact and after the other 3"; a club peaking just before impact can be a held-off release, "a shot type as much as a fault".) | **Timing against impact, one fault per segment.** Each series is read against impact on its own, as a single-signal condition: `hips_peak_after_impact`, `chest_peak_after_impact`, `arms_peak_after_impact` and `club_peak_after_impact` (`threshold` 0 ms, high; pFire Φ(t/σ_t)), and `club_peak_early` (the club more than 20 ms before impact, a heuristic matching the `deceleration` ceiling; draft). Sources: Cheetham et al. 2008, professionals peaking the pelvis 87 ± 19, the thorax 68 ± 14 and the lead arm 65 ± 8 ms before impact; Lynn et al. 2013, every golfer's pelvis peaking and slowing before the ball. No study gives a club peak time, so both club lines are practice. Causes: `late_pelvis_rotation` → hips (Lynn 2013), then hips → chest → arm → club after impact (the proximal-to-distal chain, Cheetham 2008, indirect), and `casting` and `club_not_released` → `club_peak_early`. A held-off release played on purpose is the shot, not a fault; PPS does not model intent (Mark, 17 Oct), so the condition reports when the club peaked, not why. **Single-signal conditions are deliberate.** An `any` condition reads clean only when every signal was read, so an impact check beside an order check that could not be made would count only firings, and the pattern rate would read 100 %. **The order chain falls back:** an order signal's `fallback` lists the later links, and the engine compares the subject with the first one placed (`sig_sequenceOrder`: chest, then lead arm, then club; `sig_armBeforeThorax`: lead arm, then club). The literal `impact` may end a chain (engine support, unused by the shipped pack). The ledger records the link compared (`SignalRow::comparedWith`, schema 7), and the tile names it ("Hips before lead arm"). A fallen-back check does not count towards the in-order share. The tile shows I1–I5 beside F1–F9. **Braking needs a peak before impact:** `sig_noPelvisBraking` carries `requiresBeforeImpact: m_pelvisPeakTime`; at or after impact it is not assessable (`PeakAfterImpact`), never a pass. **D27 as drafted would dark `casting`:** it adds `sig_clubBeforeArm` under `detection: any`, so every swing without both peaks would read `casting` as not assessable unless it fired. It needs the same single-signal treatment before it is applied. |
| P10 | After the library re-analysis (15 October 2026: 136 swings on the studio, none failed, pose reused), `club_peak_early` fired on 106 of 127 swings and lifted `club_not_released` into the top three roots in four sessions. The camera club node is the shaft angle's rate, and through impact that rate collapses on 93 of 129 swings: on 4 July swing 1 it falls from 2217 °/s at −37 ms to 456 at +1 ms and returns to 1743 at +30 ms, which no club does. Where it holds, a ~45 ms wobble from the projection still leaves an early hump 40–110 ms out on about half of the rest. The clubhead's own speed (`clubheadPeakLead`) peaks about 2 ms before the ball in four sessions. (KB-18. Mark: "we should refuse to signal an issue with the club speed peaking, but the measure and charting is fine so long as sigma reflects the uncertainty in the measure.") | **Drawn but not judged.** The camera club node (routes `faceOnClub`, `faceOn+dtl`) stays placed and charted, and its σ_t becomes √(σ_t² + span²/12), where span is the time from its peak to impact: the true peak could be anywhere in between. `MetricSeries::withheld` carries the producer's reason onto `clubPeakTime` and `clubPeakSpeed`. It travels through the swing document, the phase grid (sidecar schema 7) and the measure source to `MeasureReading::withheld`. The engine refuses every signal that reads a withheld value (`SignalRefusal::Withheld`); an order chain walks past it like an unplaced link. The tile says "club peak is charted but not judged". So `club_peak_early`, `club_peak_after_impact` and the club hand-off ratios are not assessable from cameras. The club-sensor route is unaffected. Open: the projection wobble in the shaft-angle rate (its root cause), and a peak-speed σ that includes it. **17 Oct: root cause found, not a projection effect** (§17.3): through impact the face-on tracker reads the legs or a backward branch while the club blurs in front of the legs. A plausibility pass removes those readings, but on 77 of 139 swings nothing measured is left, so the withhold stays (`docs/research/data/kb18/`). |
| P11 | The session cards are ordered by how far a fault goes outside its corridor when it fires (`firingExcess`, the strength ladder). The sequence checks are yes/no tests against a line, so they have no corridor and scored zero. On 4 July `hips_peak_after_impact` (14 of 14) and `sequence_order` (13 of 14) sat at the bottom, below `lead_knee_drifts_in_at_top`. (Mark, 15 October: "my sequence has pelvis peaking after impact, that is so so bad … i'm not convinced this is about pro level norms"; "out of order and peaking after impact are both really bad. both should be a sharp finding".) | **Sharp findings.** `Condition::sharp` (pack key `sharp`) marks a condition where crossing the line is serious in itself. It is set on `hips_`, `chest_`, `arms_` and `club_peak_after_impact` and on `sequence_order`. The engine carries it onto `Finding::sharp` and `ConditionRow::sharp`; the ledger persists it only when true. `rowStrength()` reads a sharp firing as `kDiagSharpExcess` (1000), the top step of the meter and past every corridor excess, so sharp patterns rank first on the cards and in the work-ons. That number is a rank key and is never printed: the meter reads "a sharp finding: crossing this line is serious in itself", and the out-by columns show "sharp". Ranking only: no tier, firing or edge moves. It is not a pro-norm distance. The This-shot dot plot sizes its axis from the corridor rows alone and draws a sharp row at the right edge; the first cut let 1000 set the axis and flattened every other row (Mark's screenshot, 15 October). |

**Dropped edges** (K3, searched 14 October 2026). Each §10.5 edge was searched before it was authored. These six could not be supported, so they are not in `core.json`:

| Edge | Why it was dropped | Search terms |
|---|---|---|
| `limited_lead_hip_ir` → `no_pelvis_braking` | No source. The mechanism also runs the other way: a restricted lead hip reaches end range sooner and stops the pelvis. The pack already says so (`limited_lead_hip_ir` → `hip_stall`), and `hip_stall` excludes `no_pelvis_braking`. | lead hip internal rotation range of motion golf pelvis rotational velocity deceleration impact |
| `late_buckle` → `no_pelvis_braking` | No study links lead-knee flexion to pelvis deceleration. The pack already authors the opposite effect (`late_buckle` → `hip_stall`, strong), and `hip_stall` excludes `no_pelvis_braking`. | golf lead knee extension lead leg bracing downswing pelvis rotation velocity impact; lead leg braking knee extension golf downswing pelvis deceleration angular velocity |
| `insufficient_set` → `club_not_released` | No source for the amount of wrist set driving the club's gain over the arm. Madrid et al. 2020 (`ref.madrid2020`) found clubhead speed not well associated with wrist-cock angles or ranges. Sprigings and Neal 2000 concerns the timing of wrist torque, not the amount of set. | wrist cock angle top of backswing release clubhead speed golf study delayed release simulation wrist torque |
| `tempo_habit` → `inconsistent_sequence` | No source. `tempo_habit` is a *settled* rhythm carried between shots, so as authored it predicts a repeatable sequence, not a scattered one. | golf swing kinematic sequence variability consistency tempo timing between swings amateurs |
| `transition_rush` → `inconsistent_sequence` | Only instruction sources make this claim, and they mean shot consistency. Neal et al. 2007 shows that felt timing tracks strike, not the lag between peaks. No study measures how a rushed transition affects swing-to-swing dispersion of segment peaks. | golf swing transition tempo rushed downswing variability of timing between swings consistency backswing downswing ratio variability |
| `chest_not_faster_than_hips` → `arms_ride_the_turn` | No source, and it does not point one way. The two are ratios over a shared middle term, so a slow chest makes the arm/chest check *easier* to pass, not harder. | pelvis thorax dissociation separation velocity golf upper torso rotational velocity gain over pelvis segmental speed summation |

That left `club_not_released`, `no_pelvis_braking` and `inconsistent_sequence` with no authored cause. A second search (14 October 2026) found a mechanism-level source for the first two. `inconsistent_sequence` still has no cause and raises a `noCause` health warning. It is not given a cause on intuition.

**Added edges** (second search, 14 October 2026). Both are `indirect`: each source supports the mechanism but does not test the pair.

| Edge | Source and mechanism | Search terms |
|---|---|---|
| `late_pelvis_rotation` → `no_pelvis_braking` (moderate) | Lynn et al. 2013 (`ref.lynn2013`, 10.1123/ijgs.2013-0011). Elite golfers peaked the pelvis earlier than recreational golfers (101.7 against 79.6 ms before impact). The later the peak, the less time the pelvis has to slow before the ball. Okuda 2010 (`ref.okuda2010`) shows the late start itself in low-skilled golfers. **Limit:** every golfer in Lynn's sample still slowed the pelvis before impact, so the edge stops at `indirect`. **Not a contradiction of `late_pelvis_rotation` → `hips_closed_at_impact`:** that edge is about how far the pelvis has turned at impact, and this one is about how fast it is still turning. A late pelvis can be both closed and still turning flat out. The authored practice edge `no_pelvis_braking` → `hips_too_open_at_impact` still points the other way two hops on. That edge has no source and should be reviewed. | timing of peak pelvis angular velocity relative to impact skilled versus less skilled golfers; pelvis deceleration before impact peak to impact time |
| `casting` → `club_not_released` (moderate) | Cheng et al. 2025 (`ref.cheng2025`). Across 84 elite golfers, early-release styles showed smaller peak on-plane wrist uncocking velocity than delayed-release styles. On the swing plane, the club's angular speed is roughly the lead arm's plus the uncocking speed. A smaller uncocking peak therefore means a smaller gain of club over arm, which is exactly what `sig_clubGainLow` reads. **Limit:** clubhead speed did not differ between the release styles in that study. The edge rests on the gain ratio, not on speed. | wrist release style early release delayed release peak uncocking velocity golf; golf wrist release timing club angular velocity relative to arm |

Considered and not authored, because they contradict an existing edge or have no source: the lead-leg candidates `lead_knee_valgus_impact`, `insufficient_knee_flex` and `poor_hip_abductor_control` (the pack routes lead-leg failure to `hip_stall`, which excludes `no_pelvis_braking`). Takagi et al. 2021 (10.1080/14763141.2019.1585472) finds that hip torques, not knee torques, drive pelvis axial rotation, and it identifies no lead-leg braking mechanism. `chest_not_faster_than_hips` is already an effect of `no_pelvis_braking`, and the reverse edge would close a cycle. `excessive_lag`, `chicken_wing`, `flipping`, `limited_wrist_mobility` and `grip_*` have no source for the club's gain over the arm.

Tiers set differently from §10.5, because the citations would not hold the tier as written:
- `transition_rush` → `upper_body_starts_down` is `indirect`, not `supported`. Meister 2011 shows the professional transition order but does not test a rushed transition.
- `arms_before_chest` is `indirect`, not `supported`. Cheetham 2008 is a chapter in a proceedings volume and has only an ISBN. The pack refuses a peer tier on a book.
- Several `practice` edges were found to have mechanism-level sources and are cited as `indirect`:
  - `transition_rush` → `arms_before_chest`: Cheng 2025.
  - `poor_pelvic_disassociation` → `chest_not_faster_than_hips`: Myers 2008.
  - `limited_thoracic_rotation` → `chest_not_faster_than_hips`: Lephart 2007.
  - `late_pelvis_rotation` ↔ `upper_body_starts_down`: Okuda 2010.
- `club_not_released` → `ball_speed_deficit` is `moderate`, not `strong`. Its source, Chu 2010, is disputed by Madrid 2020, and Cheng 2025 found no clubhead-speed difference between release styles. For the same reason the condition's `golferWhy` says the club arrives with less of the body's speed, and does not say the ball comes off slower.

---

# Part B — Lead-side load and drive

## 11. Load and drive producers (category "Ground reaction")

Everything here comes from the two IMUs plus existing video. No force platform: the reference session validates these, it does not enable them.

### 11.1 From the IMUs alone

$$
\text{vertical load (BW)} = 1 + \frac{a_{COM,z}}{g}, \qquad
\text{lateral push (BW)} = \frac{a_{COM,x}}{g}\ \ \text{(along the target line)}
$$

| Brief id | Proposed key | Definition |
|---|---|---|
| `load.vertical.series` | `verticalLoad` (series) | 1 + a_COM,z / g |
| `load.vertical.min`, `.time` | `loadSitMin`, `loadSitTime` | The "sit": minimum vertical load between top and impact, and its time (ms before impact) |
| `load.vertical.peak`, `.time` | `loadDrivePeak`, `loadDriveTime` | The "drive": peak vertical load before impact, and its time |
| `load.vertical.riseRate` | `loadRiseRate` | Max d/dt of vertical load between sit and drive (BW/s): the "push" |
| `load.lateral.*` | `lateralPush` (series), `lateralPushPeak`, `…Time` | Push toward the target and its peak |
| `load.bump.onset` | `bumpOnset` | First sustained lateral push > 0.05 BW after top (ms) |
| `load.turn.onset` | `turnOnset` | Pelvis ω_z reversal (ms) — same event as §3.5 Transition |
| `load.bumpBeforeTurn` | `bumpBeforeTurn` | turnOnset − bumpOnset (ms); positive = bump leads |
| `load.pelvisTilt.leadDrop` | `leadHipDrop` | Pelvis obliquity change from top to the sit (lead hip drops) |
| `load.pelvisTilt.postUp` | `leadHipPostUp` | Pelvis obliquity change from the sit to impact (lead side rising) |
| `load.impulseClosure` | `loadImpulseClosure` (quality) | ∫(load − 1) dt over address→finish, BW·s; should be ≈ 0 |

`lateralPush`, `bumpOnset` and the lead fraction are first-order sensitive to δ and the heading (§6.2); their σ says so until δ2.

### 11.2 With the face-on camera (2D suffices)

Inverted-pendulum (ZMP) estimate of the net COP along the target line, using the video COM position (x_c, z_c) and the **IMU** COM acceleration:

$$
x_{cop} \approx x_c - \frac{z_c\,\ddot x_c}{\ddot z_c + g}, \qquad
\text{lead fraction} = \frac{x_{cop} - x_T}{x_L - x_T}
$$

with foot centres x_T, x_L from the same view. Positions from video, accelerations from the IMU: the natural fusion, avoiding double-differentiated video.

| Proposed key | Definition |
|---|---|
| `leadLoadFraction` (series), `…P4`, `…P7` | Lead-foot share of vertical load (0–1) at top and impact |
| `leadLoadCrossTime` | When the lead fraction crosses 0.5 (ms before impact) |
| `leadLoadRate` | Max rate of change after top (1/s) |
| pelvis lateral shift (% stance width) | Existing camera metrics where they exist (`pelvisSway`/slide family); check before adding |
| pelvis height drop / rise | Pelvis mid-z change top→min and min→impact; check the lift family first |
| lead knee flexion max; extension rate in the last 100 ms | From face-on pose (`lower_body_metrics`); check before adding |
| trail heel lift time | From the floor camera (`foot_metrics`); check before adding |

### 11.3 Arms and club (optional, video)

Add w_a · ẍ(arms + club) from the face-on hand keypoints and the shaft track (upper arms, forearms, hands and club ≈ 0.11 of system mass). Only with the shaft track; otherwise omit and flag.

### 11.4 Quality flags

Each load metric carries: calibration validity (all gates passed), lever-arm residual, impulse-closure residual, IMU ↔ video z̈ agreement when both exist, and `validationStatus` = `experimental` until the reference session. The session-diagnostics statistics layer decides what is surfaced. Norms from `golf_swing_normative_reference.md` §5 (lead/trail vertical GRF by phase, peak VGRF before impact) attach only after validation.

---

# Part C — Per-foot ground reaction force

## 12. The GRF programme

### 12.1 The physics and the one hard problem

For mass m, COM r_c and whole-body angular momentum about the COM H_c:

$$
\mathbf{F}_{net} = m(\ddot{\mathbf{r}}_c - \mathbf{g}), \qquad \mathbf{M}_{net,c} = \dot{\mathbf{H}}_c
$$

F_net is the **sum** of the ground contacts (the club is held, so part of the system; the ball impulse is small and brief — flag it, model it from the floor-camera impact time, D20). With a de Leva segment model scaled to the athlete, triangulated joints give r_c(t) and segment rates give H_c(t). The trunk IMUs give angular velocity directly for the two heaviest segments, avoiding differentiation.

**The indeterminacy.** The swing is double support from address to well past impact. Per foot: a 3-D force, a COP on the ground, a free vertical moment: **12 unknowns** against **6 equations**. Every strategy below adds information to close the six-dimensional null space.

**What per-foot F_z + COP buys.** Measured F_z,L, F_z,T, p_L, p_T leave six unknowns (four if the free moments are neglected) against three informative equations:

$$
F_{x,L} + F_{x,T} = F_{x,net}, \quad F_{y,L} + F_{y,T} = F_{y,net}
$$
$$
\sum_i \big[(p_{x,i} - x_c) F_{y,i} - (p_{y,i} - y_c) F_{x,i}\big] + T_{z,L} + T_{z,T} = M_{z,net}
$$

Closed by a regularised QP:

$$
\min \; \|\mathbf{F}_h\|^2 + \lambda \|\mathbf{T}_z\|^2 \quad \text{s.t. the equalities,}\ \ \|\mathbf{F}_{h,i}\| \le \mu F_{z,i}
$$

The M_z,net equation recovers the **anti-phase shear couple** between the feet — the mechanism of pelvis torque. A proportional split of F_h,net by vertical load cannot, and is only a baseline. The M_x, M_y equations do not help the split but are free **consistency checks** between plates and kinematics (§14.5).

**Consequence:** the quality of M_z,net = Ḣ_z,c bounds the horizontal split. That is the term the trunk IMUs improve most — which is why Part C sits on Part A.

### 12.2 Strategies

| | Strategy | Position |
|---|---|---|
| **S1** | Net GRF from kinematics: COM, F_net, M_net, net COP from triangulated pose + IMU rates. Risk is numerical: double differentiation at 150 fps. | **Do first.** Zero cost. |
| **S2** | Physics-based per-foot split by contact-model optimisation (Fluit 2014; OpenSim Moco; OpenCap). 0.04–0.16 BW RMSE in gait; little golf validation; needs good foot kinematics. | Later, offline reference estimator only. |
| **S3** | IMU-only: sacrum acceleration as COM proxy (Part B) → total vertical GRF; ML per-foot needs training data. | Supporting role. |
| **S4** | Learned (kinematics or video → per-foot GRF). Mori 2025: Bi-LSTM on mocap + plates, 1,032 swings, ICC up to 0.983 lead vertical. Needs paired plate data; generalisation weak; peaks underestimated. | Phase 3, seeded by our own reference session, on PinPoint's own features. |
| **S5** | Low-cost direct measurement of per-foot F_z + COP (table below) | The missing information. |
| **S6** | **Hybrid (recommended):** S5 per-foot F_z + COP, S1 (+IMU) net F and M_z, the QP split, validated at a reference session that also seeds S4. | |

**Direct measurement options:**

| Option | Gives | Rate | Cost | Notes |
|---|---|---|---|---|
| **Wii Balance Board ×2** | F_z + COP per foot | ~100 Hz, jittery | £20–40 each used | Validated vs AMTI for COP (Leach 2014; Bartlett 2014). No shear. **Bluetooth Classic HID, not BLE** — `QLowEnergyController` will not work; hidapi. Desktop-only, behind the factory. Fastest to first data. |
| PASCO PS-3229 / 3230 | F_z (3229); F_z + one shear axis (3230) | 1 kHz BLE | ~$939 / $1,099 | Proprietary protocol. The floating-top shear design is the one to copy. |
| **DIY plate on nRF5340** | F_z + COP; shear with a floating top on linear bearings + 2 horizontal cells | ~1 kHz | ~£150–300 per plate | 4 shear-beam cells + ADS1262/1256, through the planned hub with deterministic sync. Needs a calibration rig. **The product-direction device.** |
| Pressure insoles (DIY) | COP-ish | — | low | Poor accuracy, drift. Not recommended. |

### 12.3 Phases

| Phase | Deliverable | Hardware | Exit |
|---|---|---|---|
| **0** | `NetGrfEstimator`: COM, F_net, M_net, net COP from triangulated pose + trunk IMUs | none | §14.5 V0 checks |
| **1** | `ForcePlatformDevice` abstraction + Wii driver + synthetic device; per-foot F_z, COP; on the camera clock | 2 Wii boards | V1 checks |
| **2** | `GrfSplitSolver` (the QP); per-foot 3-D GRF with confidence; producers; charts | as Phase 1 | V2 cross-modal checks |
| **3** | Reference session; validation report; DIY plate; optional learned corrector | borrowed plates; DIY plates | §14.7 targets met, or revised with evidence |

**Deferred (D18, D19):** the reference session and the third camera are later developments. Phases 0–2 run on face-on + DTL and Wii boards, and every load and GRF output stays `experimental` until the reference session happens.

Each phase is useful alone: Phase 0 gives the vertical-force shape and timing; Phase 1 gives per-foot vertical loading and COP traces, the most coach-recognisable GRF outputs.

### 12.4 Architecture constraints

- Abstract base + factory for every device from day one: `ForcePlatformDevice` (start/stop; sample stream `{t, Fz[4] or Fz, COPx, COPy, Fx?, Fy?}`; calibration coefficients; device serial) with Wii, DIY, PASCO and synthetic concrete types.
- Every force sample re-timed onto the session clock (§7.1's approach); raw receipt times kept.
- Pure QML over C++; mobile-compatible modules only. Wii HID is desktop-only, behind the factory.
- Calibration per device serial in `AppSettings`.
- No menus or native dialogs (memory note `no-menus-all-in-app`).

### 12.5 Diagnostics

Measures and signals in `core.json` referencing GRF metrics; norms deferred until the reference session. Nodes without producers are expected, not debt (memory note `model-completeness-is-the-point`). Citations resolved via CrossRef/PubMed/arXiv before entering `src/Resources/diagnostics/references.json`; diagnostics content never cites commercial products.

### 12.6 Metrics

Category "Ground reaction", camelCase keys (R11): e.g. `leadFootFzPeak`, `leadFootFzPeakTime`, `loadRatioTransition`, `leadCopExcursion`, `footCoupleMzPeak`, and the quality metric `netFzClosureNrmse`. Each `MetricDescriptor` carries `validationStatus` (experimental / validated) and its error band. UI: per-foot F_z series, COP traces on a stance diagram, lead/trail ratio against P-positions, closure residuals in a capture-quality view — following the chart panel and typeset programme already in flight.

---

# Shared

## 13. The truth session (capture protocol)

Extends `two_camera_capture_protocol.md` and keeps everything it asks: LM on every swing, cameras untouched, the rig sheet.

1. **Ceremony at the start:** the golfer's four steps (§6.3), exactly as a user would do them. Then, as rig and validation items the user never does: the stool test for r (§6.4). The hop (step 4) gives the clock.
2. **Static held poses, five:** address, half-way back, top, impact position, finish; 2 s each. The cameras are sharp on a still body, so this is the cleanest grade of the camera rotation level and the garment term — and, bent, they let the two-camera fit solve δ (δ truth against δ0).
3. **Quiet standing 10 s:** vertical load must read 1.00 ± 0.02 BW (Part B sanity).
4. **20 full swings, 7-iron.**
5. **5 three-quarter, hips-first swings** (protocol block E): the first chance of a trunk peak inside the downswing (KS §13.9 #8).
6. **Fault blocks, five each:** deliberate early extension; sway. Ground truth for the posture metrics' signs, and for bump-before-turn.
7. **Drift hold:** 60 s at address, then 5 swings — the between-swing yaw drift.
8. **Ceremony at the end** (stand tall + turn away): mount drift over the session.

Each swing analysed twice: **bound** (normal) and **unbound** (trunk roles ignored; a swinglab switch `--ignore-role pelvis,thorax` to add). Sweeps on GOLFSIMPC (memory note `feedback_corpus_sweeps_on_studio`); run trees to `/mnt/swingdata/scratch/`.

---

## 14. Verification and validation

**Verification** = the code computes what the maths says (synthetic, analytic, invariants). **Validation** = the estimate agrees with an independent measurement of the real quantity. Every component gets both. Nothing is a graded measure in diagnostics until it passes its validation gate; until then it is `experimental`.

### 14.1 Synthetic (no hardware)

- **End-to-end trunk test** — the test whose absence let defect 1 survive. A synthetic body turns, tilts and bends with sensors at arbitrary mounts; generate raw quaternions, gyro and accel; run the calibration solve on synthetic ceremony data; check `body_rotation`, `segment_rates`, `phase_segmenter` and the skeleton3d term all recover the motion. Reproduce §6.2's δ table. Includes the left-hander sign flip and the per-swing heading reset.
- **Ceremony gates:** a flipped, swapped or face-in mount is rejected with the right message (synthetic IMU device behind the factory; injectable thresholds).
- **Transport:** a rigid body with known ω(t), α(t), r → sensor acceleration → recovered a_C to numerical precision; then with gyro noise and bias, error bounds documented.
- **Lever arm:** synthetic stool rotations with noise recover r within 5 mm; residual correct; graceful with poor excitation (condition number reported).
- **Events:** detectors on synthetic and recorded swings with hand-labelled events.
- **Filter sensitivity:** peak-ω timing and sit/drive timing against cut-off ±5 Hz on the logged corpus (D16).
- **GRF:** a synthetic skeleton rendered through the calibrated cameras → triangulated → COM within 2 mm, COM acceleration within 0.1 m/s² after filtering; segment model invariants (masses sum, symmetric pose on the mid-sagittal plane, monotone scaling); smoother step and impulse responses documented (cut-off, zero phase); rotating bodies with analytic Ḣ; load-cell maths (four corners → total + COP); split-solver ground truth with noise in M_z,net and against λ; null-space degradation with M_z,net withheld; friction-cone cases.

### 14.2 Bench

- **Turntable / rotating chair, sensor at a measured offset:** recovered r within 5 mm of the tape; transported a_C ≈ 0 up to 600 °/s.
- **Static tilt jig:** tilt metrics within 0.5°.
- **Re-mount ×5:** address angles repeat within 2°, r within 10 mm.
- **Force plates (per board, per session start):** static linearity with known masses (residual < 1 % FS, per-serial gain/offset); COP on a 3×3 grid (< 5 mm RMS, Bartlett 2014's method); drop test for bandwidth; cross-board agreement < 1 %; co-planar and level, shims logged; athlete on both = BW ± 2 %.

### 14.3 Human, no force plate

- **Stool as validation:** with r fixed from one trial, a second trial gives a_C RMS < 0.3 m/s².
- **Cross-modal rotation:** IMU turn vs triangulated hip and shoulder lines, within 5° through the swing; the thorax gap (shoulder line vs spine) quantified — it is itself a metric candidate (the clavicle term).
- **Cross-modal acceleration:** IMU z̈_COM vs double-differentiated video COM; sit and drive timing within 10 ms.
- **Impulse closure** on every swing; flag > 0.05 BW·s.
- **Quiet standing** 1.00 ± 0.02 BW; **jump:** video flight time vs impulse-derived take-off velocity (vs plate impulse when plates exist).
- **Sync:** a mechanical event visible to every modality; residual under one frame at 150 fps (6.7 ms) and stable over the session. Characterise Wii sample-interval jitter (histogram).
- **M_z noise floor** in quiet standing — the minimum detectable foot couple, published with every split result. Seated trunk rotation on a stool across two plates: kinematic M_z vs plate-implied M_z.

### 14.4 Grading the camera routes (the Part A paired table)

Per segment and per route (face-on, pair, skeleton3d), against the IMU:

| Quantity | Statistic | Decision it feeds |
|---|---|---|
| Turn at P1/P4/P7 (from address) | Median \|Δ\|, bias, σ coverage at 1σ and 2σ | `kTriScaleFrac`; the corridor tier (`body_rotation_estimation.md` §6 Q2) |
| Absolute address heading (stick stance) | Bias | The 15–19° "open at address" question |
| Peak rate | Ratio camera/IMU | The 640–840 °/s question |
| Peak time | Δt, coverage by σ_t | KS §9 stage 2: median \|Δt\| ≤ 20 ms and coverage ≥ 68 % ⇒ the rung ships Estimated; otherwise PLANNED with the measured σ |
| Shoulder line − IMU thorax | Series | The clavicle term; whether camera thorax placement can ever be on |
| Static poses | \|Δ\| per pose | The garment term (§8.2), and the camera's best case |

Judge per swing, not by an aggregate (memory notes `windows-reanalysis-divergence`, `reanalyze-library-workflow`). Diff every metric against an unbound control, not only the ones being chased (memory note `ks-sequence-tile`).

### 14.5 Cross-modal closures (Part C, no reference plate)

- **F_z closure:** F_z,L + F_z,T (plates) vs F_z,net (kinematics) across the swing — the primary continuous cross-check. nRMSE < 5 % BW; peak timing < 10 ms.
- **M_x, M_y closure:** kinematic M_x,net, M_y,net vs Σ(p_i − r_c) × (0,0,F_z,i) + h·F_h,net. Not used in the solve, so agreement is independent evidence.
- **Heel lift:** when the floor camera shows the trail heel up, trail COP must be at the forefoot and F_z,T falling.
- **IMU vs kinematic COM acceleration:** Part B against Phase 0.

### 14.6 The reference session

Borrow time on a 3-D force-plate system (a coach facility or university biomechanics lab; D18).
1. The full rig: cameras, IMUs, and the Wii/DIY plates placed **on top of** the reference plates, so both record the same swing.
2. Cameras calibrated in situ; static weighing; a sync event at start and end.
3. ≥ 3 golfers (varied handicap and build), ≥ 15 swings each with driver and 7-iron, plus the static, jump and seated-rotation checks.
4. Reference F_xyz, COP and T_z per plate at ≥ 1 kHz, raw exported.
5. **Pre-registered** analysis (estimators, filters, metrics) in `docs/validation/grf-reference-session.md` before looking at data.

**Analysis:** per channel, RMSE and nRMSE (% BW or % peak), Pearson r, ICC(2,1) on waveforms and discrete features, Bland–Altman on discrete features (peak lead F_z and its time, lead/trail ratio at transition, peak M_z couple, COP excursion, sit and drive timing, lead-fraction crossing, bump and turn onsets against plate COP and shear onsets). Leave-one-golfer-out for anything learned. Error reported by P-position bin. Fit w_p, w_t (and w_a) here. This dataset seeds S4 and the diagnostics' grade bands.

### 14.7 Acceptance targets (initial; revised with evidence)

| Quantity | Target |
|---|---|
| Turn angle vs camera (static poses) | < 5° |
| Transported acceleration, stool | RMS < 0.3 m/s² |
| Lever arm repeatability | < 10 mm |
| Vertical load, quiet standing | 1.00 ± 0.02 BW |
| Sit / drive timing vs video | < 10 ms |
| Impulse closure | < 0.05 BW·s |
| Sync | < 1 frame at 150 fps |
| Static F_z (plates and kinematics) | ± 2 % BW |
| COP per foot | < 10 mm RMS |
| *Reference:* vertical load series | nRMSE < 7 % BW, r > 0.9 |
| *Reference:* F_z per foot waveform | nRMSE < 5 % BW, r > 0.95 |
| *Reference:* peak F_z,L timing | < 10 ms |
| *Reference:* F_x, F_y per foot | nRMSE < 10 % BW |
| *Reference:* M_z foot couple | sign and timing right in > 90 % of swings; magnitude r > 0.8 |
| *Reference:* lead-fraction crossing time | < 20 ms |

Anything short of its target is still surfaced, as `experimental`, with the measured error band shown.

### 14.8 Continuous verification in production

Every session runs stand tall's sensor-health gates; every swing runs the clock cross-correlation; every swing computes impulse, F_z and M_x,y closures. Residuals feed the confidence on load and GRF metrics; the statistics layer decides what is surfaced. Recorded sessions replay through the pipeline in the regression suite against stored baselines.

---

## 15. Code map

| Brief name | Lives in | Notes |
|---|---|---|
| `ImuSample`, `ImuStream` | `src/IMU/` (existing types) | Re-timed timeline beside raw arrivals (§7.1) |
| `ImuCalibrationRecord` | `src/IMU/imu_calibration.h` | Adds δ tier, σ_δ, r, residuals, serial |
| `FunctionalCalibration` | `solveTrunkSegment()` in `imu_calibration.h` | Pure function; the turn is a check, not a solve |
| `LeverArmSolver` | `src/IMU/` | §6.4 |
| `ImuOrientationFilter` | `orientation_refuser.h`, `eskf_orientation_filter.h` | Trunk profile, role-gated (§7.2) |
| `SegmentKinematics`, `AccelerationTransport` | `src/Kinematics/` | Beside `segment_rates`, `angular_rate.h` |
| `CalibrationCeremonyController` | `src/Gui/calibration/` trunk routine + C++ gates | Session-setup routine shape (R13); `SyntheticImuDevice` behind the IMU factory |
| `RotationMetricsProducer` | existing `body_rotation`, `segment_rates`, catalogue routes | IMU routes on existing keys (§8.1) |
| `ComEstimator2Point`, `ZmpLeverRule`, `LoadEvents`, `ClosureChecks` | `src/Kinetics/` (new) | Shared by Parts B and C; pure C++, no GUI |
| `LoadMetricsProducer`, `BumpTurnTimingProducer` | analysis stages registering in `MetricCatalogue` | Provenance: cut-offs, calibration id, `validationStatus` |
| `SegmentModel`, `ComEstimator`, `NetGrfEstimator`, `GrfSplitSolver` | `src/Kinetics/` | Phase 0, 2 |
| `ForcePlatformDevice`, factory, `WiiBalanceBoard`, `SyntheticForcePlatform`, `NrfLoadCellPlate` | `src/ForcePlatform/` (new) | Beside `src/IMU/`, `src/LaunchMonitor/` |
| KS checks (Part K) | `core.json`, `norms.json`, engine (E1–E3), `segment_rates.cpp` (E5), the KS tile (E4) | §10.8 |
| `docs/Frames.md` | — | Not created; `imu_frame_contract.md` + `pinpoint_sign_conventions.md` (R3) |

**Before naming any struct:** grep all of `src` (memory note `odr-collision-in-pinpoint-analysis`). Stage version bumps on every producer change. Logging through `ppInfo/Warn/Error` to the app log. Tests silent, run through ctest.

---

## 16. Order of work

The 5–9 October week did not run; only role-keyed placement landed. The order and its reasons stand:

0. **Part K can start now, in parallel**, on the camera routes: E1–E6 (§10.8), the rank-shift report, then Mark's review of what fires. It needs no hardware, and the trunk checks become measured the day the sensors are worn.
1. **Frames, the trunk solve and the end-to-end test.** Every later number depends on the vertical being right, and a wrong axis is invisible in plausible output. (`imu_frame_contract.md` trunk rows including the R3 fix; `solveTrunkSegment()`; the phase_segmenter axis.)
2. **Hardware measurements before the estimator** (Mark, ~30 min): rate with 3 Witmotions and with 2 + wG3; a 5-minute still hold (bias); hops (clock repeatability); 5 worn swings; live vs re-fused tilt. Each decides whether a planned piece is needed at all.
3. **Offer the trunk mounts in session setup; the ceremony, guide and live check.** The capture needs a calibrated mount; the recorded ceremony makes the solve replayable.
4. **Defects 2 and 3, the σ model, signed series, posture metrics, KS rate, `--ignore-role`** — before the capture is analysed.
5. **The truth session** (Mark, ~90 min, §13).
6. **skeleton3d fusion and the per-swing clock check; GOLFSIMPC sweep bound and unbound.**
7. **Grading:** the §14.4 table and the research note; measured σ into the census and `kTriScaleFrac`; thorax placement recommendations; user guide section.
8. **Part B:** transport, lever arm (r0, then r1 from the two-camera fit), two-point COM, load producers, ZMP lead fraction — validated against video (§14.3) on the truth-session swings.
9. **Part C Phase 0** (net GRF from kinematics), and the split solver plus synthetic plate against synthetic ground truth — neither needs hardware.
10. **Part C Phase 1** once D17 is decided; Phase 2; then the reference session.

**Progress (17 Oct 2026):** Part K is done (step 0). Step 1 is built and tested, uncommitted at the time of writing. The step-2 tooling is built. The measurements are split (Mark, 17 Oct): the desk part (H0–H2) runs now; the worn part (H1b, H3–H5) follows step 3, the ceremony (§17.3).

**What slips first:** the mixed-route X-factor (D4), the posture measures in `core.json`, the clavicle series, Part C. The ceremony, the three defects and the paired table do not slip.

Standing rules: Mark judges results before anything is committed; commits at the end, push on his word; ≤ 2–3 builds per task; one item per agent run.

---

## 17. Results

*(Filled in as the work runs, with the run root named.)*

- Day-1 measurements: rate, BLE count, bias, hop clock offset and its repeatability.
- Live vs re-fused trunk tilt at impact.
- Garment term: static poses; session-start vs session-end mount.
- §14.4 paired table.
- Part K: rank-shift report before and after; per-condition firing counts on the library; the check count against clubhead speed and smash (§10.6).
- Part B: quiet standing, stool validation, IMU vs video sit/drive timing, impulse closure distribution.

### 17.1 Part K, 14 October 2026: before the library is re-analysed

**Run.** `diag_rank_diff --pack-a <HEAD pack> --pack-b src/Resources/diagnostics --out <dir> /mnt/swingdata/Mark-Liversedge/2026-*`.
- The HEAD pack is the six files from `git show HEAD:src/Resources/diagnostics/<f>.json`.
- The run covered 136 swings in 6 sessions, at 17 ms per swing per pack.
- The library was read only: each session is mirrored, and nothing is written into it.
- CSVs: `docs/research/data/ks_model/*_pre_reanalysis_20261014.csv`.

**State of the data.** The library's swing documents predate the new sequence metrics: the arm and club peak times, the four peak speeds and the reversal times are absent. The chest peak is not placed on either camera route (`segment_rates.h:130`, `:140`).

**Results.**
- Every check F1–F7 and F9 reads "—" on all 136 swings (`sequence_checks_*.csv`). This is the expected state, not a fault. It stays so for the trunk checks until the chest peak is placed or a chest sensor is worn.
- **No swing changes its firing.** `firing_changes_*.csv` is empty, and every per-condition count is unchanged. The seven new conditions are assessable on 0 swings.
- **The roots move in 4 of 6 sessions** (`roots_by_session_*.csv`). `upper_body_starts_down` enters at #2 (sessions 10-05, 10-07 and 10-08) or #3 (07-04), at a score of 0.361. It displaces `across_the_line` (0.155) as the explanation of `over_the_top`, which fires on 118 of 126 assessable swings.
  - Both are unassessable here. The new one ranks higher through its strong edge to `over_the_top`.
  - **Verdict: accepted under the §10.8 gate**, because the move follows an authored §10.5 edge.
  - **Flagged for review:** the session's #2 root is now a hypothesis no swing measured. The reversal times will either measure it or rule it out once the library is re-analysed.
- **D27 (`casting` gains `sig_clubBeforeArm`) is held as a patch:** `docs/research/data/ks_model/d27-casting-club-before-arm.patch`, not applied. Its gate needs `clubPeakTime` and `leadArmPeakTime`, which only a re-analysis produces.
- **§10.6 (checks passed against clubhead speed) is not yet runnable:** no swing has an assessed check.

**Done 15 October:** see §17.2.

### 17.2 Part K, 15 October 2026: after the library re-analysis

**Run.** The studio was built from `f29f79ec`, then `b4840854`, and ran a metrics-only write-back (reuse path) over the 136 library swings and Nick Henn's 2. None failed, and pose was reused on every swing.
- The ledgers were regraded with `regrade_ledger`.
- `diag_rank_diff` compared the pre-Part-K pack (`cae12914`) with the shipped pack.
- CSVs: `docs/research/data/ks_model/*_post_reanalysis_20261015.csv`, after the club change and before sharp findings (sharp moves only the display order).

**Firings over the library.**

| Condition | Fired / assessable |
|---|---|
| `hips_peak_after_impact` | 54 / 59 |
| `sequence_order` | 55 / 59 (it falls back to the lead arm) |
| `arms_peak_after_impact` | 8 / 132 |
| `club_peak_after_impact` | not assessable (camera club withheld, P10) |
| `club_peak_early` | not assessable (camera club withheld, P10) |

The hip peak is placed on 59 swings; the chest peak on none.

**Before the club change,** `club_peak_early` fired on 106 of 127 swings. It lifted `club_not_released` into the top-three roots in four sessions. That was the KB-18 artefact, and it is gone (P10).

**Roots.** `upper_body_starts_down` is #1 in 07-04, 10-05, 10-07 and 10-08, carried by `sequence_order` and `hips_peak_after_impact`. It is still unmeasured on these cameras (no chest reversal), so it stays a hypothesis the sequence faults point at.

**Display order (P11).** On the session cards, the sequence faults sat at the bottom (no corridor) until they became sharp findings. They now lead in every established session.
- **First fix:** the This-shot dot plot then sized its axis from the 1000 rank key, which pressed every corridor fault onto the fault line.
- **Fixed:** `PpDotPlot` sizes the axis from corridor rows only and draws a sharp row at the right edge.
- **Changed 17 Oct (Mark: "sharp first, as in sessions"):** the home screen's merged work-ons ranked by the recency-weighted Wilson lower bound across sessions, and excess only broke ties. Now a sharp pattern in its newest listing session ranks first, then the Wilson bound (`aggregateWorkOns`, work-on rule v3). Before the change: The sequence faults are assessable on 59 swings against ~130 for the posture faults, so they rank lower there, although every session's own list puts them first.

**§10.6 (checks passed against clubhead speed, Spearman within session):**

| Session | Swings | ρ |
|---|---|---|
| 07-04 | 15 | +0.21 |
| 09-09 | 7 | +0.21 |
| 10-05 | 28 | +0.12 |
| 10-07 | 45 | +0.22 |
| 10-08 | 31 | −0.07 |

These are weak and not consistent. By the plan the count should come off the tile; that is Mark's call, still open.

**D27** fired `casting` on 17 more swings before the club change, and its assessable count fell only 127 → 124. With the camera club now withheld, `sig_clubBeforeArm` is refused on every camera swing, so D27 as drafted would add nothing. It needs the single-signal redesign and a club timing it may judge.

**Open.**
- Is the late hip peak real or a camera-route lag? A worn pelvis sensor settles it.
- The shaft-angle rate's projection wobble (KB-18 root cause).
- ~~A "held off" shot type to excuse an intentional hold-off.~~ **Dropped 17 Oct (Mark):** "it is a coaching decision not for pps. we can't account for intent we just stay honest and report what we saw." `club_peak_early`'s wording no longer promises a declaration.
- The tile count (§10.6).
- Sharp findings in the home ranking.
- ~~The D27 redesign.~~ **Done 17 Oct:** the single-signal condition `club_before_arms` (§18 D27).
- ~~The tile count (§10.6).~~ **Count removed 17 Oct** (§18 D22).
- ~~Sharp findings in the home ranking.~~ **Done 17 Oct:** sharp first on home too (work-on rule v3).

---

### 17.3 After Part K, 17 October 2026: leftovers, KB-18, and §16 step 1

**Part K leftovers** (uncommitted at the time of writing):
- **Home work-ons:** sharp findings first (rule v3).
- **The tile:** the "n of m" count is gone and the list stays (D22).
- **Held off:** no intent modelling; the wording no longer promises a declaration.
- **D27:** the single-signal condition `club_before_arms`.
- **D27 gate:** `diag_rank_diff` from the HEAD pack to the new pack shows **no swing changing its firing, and the same top-3 roots in every session**. `club_before_arms` is assessable on 0 swings while the camera club is withheld (CSVs `docs/research/data/ks_model/d27_*_20261017.csv`).

**KB-18, the camera club through impact** (`docs/research/data/kb18/`).
- **The audit:** 12 swings rendered from −40 to +40 ms.
  - On 6, the face-on line sits on the legs or trousers at 85–110° while the club, a faint blur in front of the legs, is already past.
  - On 3, wedge and recon tiers read the shaft pointing back up the downswing.
  - 3 are clean.
- **Not a projection effect:** the 2-D image angle already stalls before de-projection, and the projection would *speed up* the 2-D rate at impact.
- **The pass tried:** an impact-window plausibility pass demoted the wrong readings. On GOLFSIMPC (two mirrors, one binary) it failed the library gate: the rate at ±8 ms is ≥ 0.8 of its peak on 25 of 139 swings against base's 32.
- **Why it fails:** on 77 swings the camera has fewer than 2 measured samples on one side of impact, so a bridge has nothing to measure. On the 62 swings covered on both sides, base and fix agree at a median 0.6. That is consistent with the camera's own clubhead speed, so the early club peak there may be real.
- **Shipped:** the pass is off, and the withhold stays.
- **For Mark:** judge the camera club per swing by two-sided coverage.

**§16 step 1, frames and the trunk solve** (uncommitted at the time of writing):
- **The contract:** `imu_frame_contract.md` §5 now has the trunk rows, and `pinpoint_sign_conventions.md` has "Trunk IMU readings".
- **The code:**
  - `solveTrunkSegment()`, with its gates and messages, and `trunk_angles.h` (D13);
  - the KS trunk rate is now the segment S-I rate (D3), and the phase segmenter reads the pelvis about world Z;
  - defect 3 is fixed: a conf-0 IMU segmentation no longer replaces vision phases.
- **The end-to-end test:** `trunk_e2e_test` covers Madgwick and ESKF worlds, arbitrary mounts, left-handers, drift and the gates. Two findings, written into §6.2 and §6.3:
  - face-in is not catchable at stand tall or in the turns;
  - the turn error from δ grows to 5.3° at 90° on a bent thorax.
- **Step-2 tooling:**
  - the dev switches `PINPOINT_IMU_RECORD_DIR` and `PINPOINT_DEV_TRUNK_MOUNTS`;
  - swinglab `--trunk-calib-from`, `--ignore-role`, and per-lane tilt in `--refuse-orientation`;
  - `tools/trunk/trunk_hw.py`;
  - the protocols, split 17 Oct (Mark): `docs/research/trunk_imu/hardware_desk_session.md` (H0–H2, at a desk, now) and `hardware_worn_session.md` (H1b, H3–H5, after the ceremony);
  - `build/run-me/trunk-hw-desk.sh`, `build/run-me/trunk-hw-worn.sh` and `tools/studio/trunk-hw*.cmd`.
- **Next, in order:** the desk session (H0–H2, needs nothing new); then the calibration ceremony (§16 step 3), built on the desk numbers (rate, still-hold thresholds); then the worn session (H1b, H3–H5) with the ceremony doing the calibration live. The stand-tall-shot route stays as a fallback.

## 18. Decisions

All decided with Mark on 14 October 2026 unless dated otherwise.

| # | Decision | Outcome |
|---|---|---|
| D1 | wG3 on the wrist with Witmotions on the trunk? | **Yes** (4 Oct) |
| D2 | How many units? | **Users have 3 Witmotions; 2 on the trunk** (4 Oct) |
| D3 | KS trunk rate | **The trunk's own S-I rate; fusion manages camera alignment** (4 Oct) |
| D10 | Ceremony motions | **Standing and turning only, no hinge** (4 Oct) |
| D5 | Heading reference for stand tall | **The alignment stick**, the same one the camera calibration uses, so the IMU and camera worlds share one reference |
| D11 | Walk three steps as a δ source | **Dropped.** δ comes from the mount, then the swings (§6.5) |
| D12 | The hip lever arm | **Height-scaled default, refined from the two-camera fit; no golfer step.** The stool test is truth-session validation only (§6.4) |
| D13 | Angle definitions | **Projection** (§5.3); check ISB Part I and state the axial-rotation definition in the sign conventions doc |
| D14 | IMU ↔ camera clock offset | **A golfer hop each session** — ceremony step 4 (§6.3, §7.1). Skippable; the offset then falls back to per-swing cross-correlation, flagged |
| D15 | Handedness flip | **At ingest**; stored metrics are handedness-neutral, handedness in the binding |
| D16 | Filter cut-offs | **The brief's values for now** (15 / 20 / 12 Hz). The gyro-rate cut-off is the first to revisit (§7.5) |
| D4 | X-factor across sources | **The fused skeleton is the X-factor route** whenever two cameras exist, since it already accounts for each source. With no fusion (one camera, one trunk sensor): from address, σ in quadrature, Estimated |
| D6 | Per-swing IMU ↔ camera lag | **Record and flag** (> 10 ms), into the KS timing σ; apply once the truth session shows it stable |
| D7 | Sensor slip | **Toast and widen σ** |
| D8 | One arm + trunk ceremony | **Yes, after the trunk-only routine has run on hardware** |
| D9 | Magnetometer heading | **No** |
| D22 | "Sequence checks: n of m" on the KS tile | **Checks and count shown now**, labelled PinPoint's own; the §10.6 validation runs alongside and can take the count off. **17 Oct (Mark): count removed, list kept.** ρ against clubhead speed was −0.07…+0.22 with no consistent sign, and the count read "1 of 1" on 79 of 147 swings: it measured how many checks the cameras could make, not the sequence |
| D23 | Hand-off thresholds | **80 % of the Cheetham professional ratio:** 1.22 / 1.08 / 1.84. **Needs an NR-05 amendment** (below) |
| D24 | Pelvis braking | **No braking = hips still at ≥ 95 % of peak at impact**; heuristic, re-seated from worn swings |
| D25 | The seven new KS characteristics | **Accepted as proposed** (§10.4); wording tuned later in the inspector |
| D26 | X-factor characteristics vs NR-13 | **Amend NR-13** (below); both nodes stay |
| D27 | `casting` gains club-before-arm | **Yes, after the rank-shift report** and a per-swing review of new firings. **Redesigned 17 Oct:** not a second signal on `casting` (the `any` trap, §10.9 P9) but its own single-signal condition `club_before_arms` (`sig_clubBeforeArm` alone; edge `casting → club_before_arms`, moderate, indirect). `casting` is unchanged. Not assessable from cameras while the camera club is withheld (P10) |
| D17 | Force plates | **Wii boards first**; the DIY nRF5340 plate follows as the product device |
| D18 | Reference session | **Deferred.** Load and GRF metrics stay `experimental`; Phase 3 waits |
| D19 | Third camera | **Deferred as a later development**; Phase 0 runs on face-on + DTL and states its weaker feet |
| D20 | Club and ball in the GRF model | **Club in** (mass per club, 3-D shaft track); **ball impulse modelled** at the floor-camera impact time |

**Amendments to the normative reference** (approved by Mark and applied to `golf_swing_normative_reference.md`, 14 October 2026):
- **NR-05, amended 2026-10-14 (Mark):** the kinematic-sequence hand-off checks fire when the ratio of a segment's peak speed to the previous segment's falls below 80 % of the ratio in Cheetham et al. (2008), 19 PGA tour professionals. The amateur ratios in the same study lie within 3 % of the professional ones, so the check marks a breakdown in the chain, not a skill gap, and is worded as such.
- **NR-13, amended 2026-10-14 (Mark):** X-factor stretch may fire a characteristic (`xfactor_deficit`, `excessive_separation_stretch`) at the extremes of a wide corridor, labelled one-golfer and heuristic until a cohort exists. X-factor at the top is never graded on its own (NR-10), and no elastic-energy language is used (NR-14).

---

## 19. Risks

- **The garment term may be large.** A vest pocket over moving scapulae could be worse than the camera on some quantities. The static poses measure it. If large, the thorax IMU stays a *timing* instrument while the level comes from the fusion. Nothing assumes the IMU is truth on every axis.
- **BLE capacity at 200 Hz** with three Witmotions (or two + wG3) may force 100 Hz. KS timing at 100 Hz with peak interpolation is about 2–3 ms; stated in σ_t. Load timing at 100 Hz needs checking against the 10 ms target.
- **Witmotion parity has never passed.** If the cause is deeper than the ESKF-capture hypothesis, the trunk lanes run on re-fused orientation only, and integrity needs a trunk rule.
- **One golfer.** Everything graded is Mark's swing. Corridors and norms stay where they are until the reference session (≥ 3 golfers).
- **The prescribed mount may be poor.** δ0 wrong weakens the side-bend family and the lateral load until δ1/δ2. KS, peak times, turn and vertical load are unaffected.
- **The lever arm.** Until r1 (the two-camera fit) lands, the load metrics carry r from a default; the transport terms are larger than the signal, so a 2–3 cm error in r is visible in the sit and drive. σ_r must flow into every load metric.
- **The two-point COM omits the arms and club** (≈ 11 % of mass) unless the shaft track is present; the lateral push near impact is where that bites.
- **Wii boards are Bluetooth Classic HID** — a desktop-only integration with jittery timing, behind the factory.
- **Double differentiation at 150 fps** dominates Phase 0's error; filter design is the work.
- **No public golf GRF dataset.** Anything learned depends on our own reference session.
- **KS checks mostly unassessable on camera-only swings** until the trunk peaks place. That is honest, but a tile full of "—" reads as broken; the tile must say why ("chest peak not placed from these cameras").
- **New KS conditions will re-rank sessions.** Proximal-first edges are meant to, but every root that moves is reviewed per session before anything ships.

---

## 20. References (resolve every DOI before it enters `references.json`)

**Rotation and calibration**
- Cheetham PJ, Martin PE, Mottram RE, St Laurent BF. The importance of stretching the "X-Factor" in the downswing of golf: the "X-Factor stretch". *Optimising Performance in Golf*, 2001. (verify)
- Cheetham PJ, Rose GA, Hinrichs RN, et al. Comparison of kinematic sequence parameters between amateur and professional golfers. *Science and Golf V*, 2008. (verify)
- Myers J, Lephart S, Tsai YS, Sell T, Smoliga J, Jolly J. The role of upper torso and pelvis rotation in driving performance during the golf swing. *J Sports Sci* 2008;26(2):181–188. (verify DOI)
- Kim et al. 2023 — IMUs on T1 and L4 vs mocap (as cited in the normative reference §5).
- Wu G et al. ISB recommendation on definitions of joint coordinate systems, Part I. *J Biomech* 2002. (verify)
- Favre J, Aissaoui R, Jolles BM, de Guise JA, Aminian K. Functional calibration procedure for 3D knee joint angle description using inertial sensors. *J Biomech* 2009;42(14):2330–2335. (verify DOI)
- Picerno P, Cereatti A, Cappozzo A. Joint kinematics estimate using wearable inertial and magnetic sensing modules. *Gait & Posture* 2008;28(4):588–595. (verify DOI)
- Solà J. Quaternion kinematics for the error-state Kalman filter. arXiv:1711.02508 (2017).
- Harrington ME, Zavatsky AB, Lawson SEM, Yuan Z, Theologis TN. Prediction of the hip joint centre in adults, children, and patients with cerebral palsy based on MRI. *J Biomech* 2007;40(3):595–602. (verify DOI)

**COM, load and GRF**
- de Leva P. Adjustments to Zatsiorsky-Seluyanov's segment inertia parameters. *J Biomech* 1996;29(9):1223–1230. doi:10.1016/0021-9290(95)00178-6
- Kajita S et al. Biped walking pattern generation by using preview control of zero-moment point. *ICRA* 2003. (verify)
- Ancillao A, Tedesco S, Barton J, O'Flynn B. Indirect measurement of ground reaction forces and moments by means of wearable inertial sensors: a systematic review. *Sensors* 2018;18(8):2564. doi:10.3390/s18082564
- Bartlett HL, Ting LH, Bingham JT. Accuracy of force and center of pressure measures of the Wii Balance Board. *Gait & Posture* 2014;39(1):224–228. (verify DOI)
- Leach JM, Mancini M, Peterka RJ, Hayes TL, Horak FB. Validating and calibrating the Nintendo Wii Balance Board to derive reliable center of pressure measures. *Sensors* 2014;14(10):18244–18267. doi:10.3390/s141018244
- Mori K. Estimation of the ground reaction forces during golf swing using recurrent neural networks. *ISBS Proceedings Archive* 2025;43(1):60.
- Louis N, Corso JJ, Templin TN, Eliason TD, Nicolella DP. Learning to estimate external forces of human motion in video. *ACM Multimedia* 2022. arXiv:2207.05845
- Katsu et al. GRF-MV: ground reaction force estimation from monocular video. *BMVC 2024 Workshops (ANIMA)*.
- Estimation of three-dimensional ground reaction force and center of pressure during walking using a machine-learning-based markerless motion capture system. *Bioengineering* 2025;12(6):588. doi:10.3390/bioengineering12060588
- Fluit R, Andersen MS, Kolk S, Verdonschot N, Koopman HFJM. Prediction of ground reaction forces and moments during various activities of daily living. *J Biomech* 2014;47(10):2321–2329. (verify DOI)
- Ren L, Jones RK, Howard D. Whole body inverse dynamics over a complete gait cycle based only on measured kinematics. *J Biomech* 2008;41(12):2750–2759. (verify DOI)
- Dembia CL, Bianco NA, Falisse A, Hicks JL, Delp SL. OpenSim Moco: musculoskeletal optimal control. *PLoS Comput Biol* 2020;16(12):e1008493. (verify DOI)
- Uhlrich SD et al. OpenCap: human movement dynamics from smartphone videos. *PLoS Comput Biol* 2023. (verify DOI)
- Song B et al. Estimating ground reaction forces from inertial sensors. arXiv:2311.02287 (2023).
- Bourgain M et al. (2020); Watson A et al. (2025); Okuda I et al. — GRF norms, as in the normative reference §5.
- PASCO PS-2142 / PS-3229 / PS-3230 product pages — engineering reference only, never in `references.json`.
