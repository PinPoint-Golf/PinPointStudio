# Trunk IMUs — pelvis belt and thorax vest

**Status:** design, not started. Written 4 October 2026 for the week of 5–9 October.
**Owner:** Mark.
**Hardware:** Witmotion WT901BLE67 units. One sits in the pocket of a GPS vest between the shoulder blades (rear thorax). One sits on a belt over the sacrum (rear centre pelvis).
**Builds on:**
- `body_rotation_estimation.md`: the IMU tier is written and waiting for a placement.
- `kinematic_sequence_design.md`: §4 has the IMU rung first; §9 stage 2 is the sacrum/sternum truth capture.
- `ks_skeleton3d_route_design.md` §13: "only the Witmotion capture can grade placement".
- `imu_frame_contract.md`, `imu_rearchitecture.md`: frames and the 6-axis decision.
- `uncertainty_census_design.md`: the trunk rows this work fills.
- `two_camera_capture_protocol.md`: the capture this extends.

---

## 0. Summary

**The analysis side is mostly built and has never run.**
- `SegmentRole::Pelvis` and `Thorax` exist.
- The rotation producer, the kinematic-sequence IMU rung, the catalogue routes and an IMU term in the skeleton3d fit all read them.
- No shot has ever carried one. `segmentRoleForSlot()` maps only the Wrist session's A/B/C to arm segments, so a pelvis or thorax sensor is bound as `Unknown` and dropped without a word.

So the week is not mainly about new producers. It is about:
1. getting a trunk sensor bound;
2. calibrating it;
3. fixing the latent defects that every never-exercised path collects;
4. using the first real data to grade what the cameras have been estimating.

**Where most of the value is.** It is not the trunk metrics for the golfer wearing the vest. It is **truth for the camera routes**, which every camera-only user depends on.

These open questions all wait on rotation truth:
- the pair route's "trunk still rising at impact";
- the skeleton fit coasting through square;
- thorax 103° / X-factor 66° at the top;
- pelvis rates of 640–840 °/s against Cheetham's 477;
- the 15–19° "open at address" bias;
- thorax KS placement being switched off;
- the census's camera-scale σ (`kTriScaleFrac` 0.10).

One session of swings worn with trunk IMUs and recorded on both cameras settles most of them. That is the arbiter Mark asked for ((memory note `feedback_camera_fallback_best_effort_honest`): an IMU is the validation path, not the fix).

**Three defects found while writing this.** All three must be fixed before any trunk number is believed.

1. **The vertical axis.** Calibration makes the reference-pose body frame the "world" (`A = conj(refRaw·M)`, `imu_calibration.h`). For the arm, that world is Y-down.
   - `body_rotation.cpp:90` and `segment_rates.cpp:1268` treat world **Z** as vertical.
   - `phase_segmenter.cpp:290` reads the pelvis axial rate on segment **Y**.
   - The unit tests feed synthetic yaw about Z directly and never pass through a calibration, so none of this is caught. §4 fixes it with a trunk frame whose calibration keeps the world Z-up.
2. **Adding a pelvis IMU makes `hip_stall` unassessable.** The triangulated stage steps aside for any segment an IMU covers ("an IMU reading always wins", `wrist_analyzer.cpp:2735`). But the IMU route for `pelvisRotationSigned` is still PLANNED (`metric_catalogue_manifest.cpp:684`), so the measure loses its only live producer.
3. **A trunk-only IMU job replaces good vision phases with a conf-0 clamp.** `SegResolveStage` (`wrist_analyzer.cpp:727`) adopts `segImu` whenever it exists. `PhaseSegmenter` needs a hand or forearm sensor and otherwise returns `clampFallback()` at conf 0. A golfer wearing only the vest and belt would lose their whole phase ladder.

**Three smaller things already known to bite:**
- **No Witmotion swing has ever passed `imuIntegrity` parity** ((memory note `imu-integrity-write-once`)). Every trunk shot would carry a ⚠ badge until that is understood.
- **Witmotion samples carry host-arrival timestamps** from a bursty BLE link, with no clock fit. Kinematic-sequence timing to ±10 ms needs a stated offset and jitter against video.
- **IMU rotation readings carry no σ.** `body_rotation.cpp:112` leaves it unset on purpose.

---

## 1. Deliverables and definition of done

| Deliverable | What "done" means |
|---|---|
| This document, kept current through the week | Every decision in §12 answered or deferred by Mark; §11's results filled in with numbers from a named run |
| Trunk placement and calibration, in the app | Mark straps on the vest and belt, runs the ceremony, and sees both segments track in a live check view. A deliberately mis-seated sensor fails, and the message names it. |
| Trunk analysis | A captured swing yields measured pelvis and thorax rotation, signed series, rates and placed KS nodes, each with a σ. `hip_stall` and `sequence_order` are assessable. Trunk-only jobs keep the vision phase ladder. |
| skeleton3d fusion | Trunk IMUs constrain the fit with a calibrated mount. The pelvis no longer coasts through square on a worn swing. |
| Camera-route grading | `docs/research/data/trunk_imu/` holds the per-swing IMU-vs-camera table and a note. The note gives the measured σ and bias of every camera trunk route, and the decisions it implies. A failed gate, written up honestly, counts as done. |
| Tests | A synthetic end-to-end test: a known trunk motion, through the calibration solve, through every consumer. It is the test whose absence let defect 1 survive. |

Per standing rules:
- Nothing is committed until Mark has tested it ((memory note `feedback_commit_only_at_the_end`)).
- Grading sweeps run on GOLFSIMPC ((memory note `feedback_corpus_sweeps_on_studio`)).
- ImuCalibrationFlow / wizard edits each need explicit approval and a way to run them ((memory note `do-not-touch-the-session-wizard`)).

---

## 2. What the placement buys, ranked

### 2.1 Truth for the camera routes (largest, benefits every user)

Every swing worn with trunk IMUs and recorded on face-on + DTL is a paired observation. Re-analysing it with the IMU bindings ignored gives the camera answer to the same swing. This is `kinematic_sequence_design.md` §9 stage 2, now possible. It also covers the rotation level, not only KS timing. What it settles:

| Open question | Where it is recorded | What the IMU measures |
|---|---|---|
| The pair route says the pelvis and thorax are "still rising at impact" on 20/21 swings | (memory note `ks-pair-route`) | The real pelvis/thorax peak times |
| The skeleton fit turns the hips through impact at about half the DTL keypoints' rate | `ks_skeleton3d_route_design.md`, K0 §8 | Pelvis yaw rate through square, where the face-on view is blind |
| Thorax 103° and X-factor 66° at the top. Real, or the fit and camera geometry? | (memory note `rotation-triangulated`) | Thorax and pelvis turn at P4 |
| The thorax shoulder line carries clavicle motion; the cameras cannot split it | K0 §12 | Thoracic segment twist without the clavicles. The shoulder line minus the thorax IMU *is* the clavicle term. |
| Camera pelvis peak 640–840 °/s against Cheetham 477 ± 53 | KS design §13.9 #4 | The real peak magnitude |
| Golfer set 15–19° open at address, or a span artefact? | `body_rotation_estimation.md` §6 Q1 | Address heading against the calibrated stance (§5.5) |
| `kTriScaleFrac` 0.10 (0.03 once calibrated): is the 10% camera-scale σ honest? | `body_rotation.cpp:148` | Coverage: the fraction of \|camera − IMU\| ≤ kσ |
| Thorax KS placement is off by default on both camera rungs | `segment_rates.h:131` | Whether placements land within σ_t of truth |

⚠ **One golfer.** All of this is Mark's swing. It grades the methods on one body, which is what σ coverage needs. It cannot seat norms or corridors ((memory note `wrist-perswing-closed`)). The note must say so.

### 2.2 Measured trunk metrics for the wearer

These metrics resolve `Measured` instead of `Bridged`, and gain a σ:
- `pelvisRotation`, `thoraxRotation`, `xFactor`, `xFactorStretch`;
- `pelvisRotationSigned` and a new `thoraxRotationSigned`;
- `pelvisAngularSpeed`, `thoraxAngularSpeed`, `pelvisPeakTime`, `thoraxPeakTime`.

**New quantities a calibrated triad allows.** A camera cannot give these honestly. One IMU per segment gives a full orientation, so the trunk gets real tilt and side bend, not only turn:

| Metric | Definition | Diagnostic use |
|---|---|---|
| `pelvisTilt` (anterior/posterior) | Pelvis sagittal tilt from upright stance | Early extension's rotational half: posterior tilt address→impact. Today the camera gives only the thrust half (`pelvisThrust`, DTL). |
| `pelvisObliquity` | Pelvis frontal tilt (lead hip high +) | IMU route for `hipLineTilt`; pelvis side bend at top, about 10–12° in the coaching source (NR, low confidence) |
| `thoraxForwardBend` | Thorax sagittal inclination | The PLANNED `trunkImus` route on `spineForwardBend` (`manifest.cpp:869`); loss of posture P1→P7 |
| `thoraxSideBend` | Thorax frontal inclination | IMU route for `spineSideBend`; side bend at impact; reverse spine angle at top |
| `xFactorAtTop` measure | xFactor at P4 | No such measure exists today (agent finding) |
| `pelvisDecel` present/absent | The pelvis rate falls before impact | NR-12 says report it as binary |
| `speedGainPelvisThorax` | Thorax peak − pelvis peak | Cheetham 250 ± 42 °/s pro (normative reference) |
| `crunchFactor` | Thorax side bend × axial rate relative to the pelvis | NR-17: compute and log, do not display |

The KS norms in the reference (Cheetham) come from sensors on the pelvis and upper thorax. That is this layout, so the "% of pro" in the KS tile becomes like-for-like for the first time.

### 2.3 Kinematic sequence complete on the trunk

- The IMU rung is already first in the ladder.
- With both sensors bound, the pelvis and thorax nodes are placed from a gyro, not a span or a fit.
- `sequence_order` needs a placed chest peak. It is notAssessable on all 15 swings of 07-04 today, and should become assessable on every worn swing.

### 2.4 A better 3-D model

The skeleton3d fit already has an IMU orientation term (`skeleton3d_fit.cpp:734`). Trunk IMUs pin the two degrees of freedom the fit's priors exist to paper over:
- pelvis yaw at square (`pelvisYawAccRad`);
- pelvis tilt (`pelvisTiltAccRad`, needed because two hip keypoints cannot see tilt).

The thorax sensor rides `Spine2`. With the lean rig it constrains whole-spine twist relative to the pelvis, which separates spine twist from clavicle motion. The 3-D view's hip pause at impact ((memory note `ks-skeleton3d-route`), still open) should disappear on worn swings. If it does not, that is a finding about the fit.

### 2.5 Phase timing

The trunk gives cleaner events than the arm for three positions:
- **Transition:** the pelvis axial reversal, already coded (`phase_segmenter.cpp:283`).
- **Top:** the thorax reversal, today only a cross-check.
- **Takeaway onset:** the thorax starts turning. The trunk is far stiller at address than a waggling wrist; the wrist IMU stillness detector "fell back to continuous waggle on 11/11 wG3 swings" (`timeline-fusion.md`).

### 2.6 Later, not this week

- **Live biofeedback:** turn and X-factor shown live, K-Vest style.
- **Pelvis acceleration as a GRF and weight-shift timing proxy.** The sacrum sits near the centre of mass (`grf_estimation_design_briefing.md`, approach A).
- **Hip internal rotation:** needs thigh IMUs (`hipInternalRotation`, PLANNED).

### 2.7 What it does not give

**Translation.**
- Double-integrating the accelerometer over 1.5 s is not credible for sway, slide, thrust or lift.
- Those stay camera metrics.
- The IMU's only contribution there is the posterior-tilt half of early extension (§2.2).

---

## 3. The hardware, and its error budget

### 3.1 Placement and the artefacts it brings

**The vest pocket sits over roughly T2–T5.** The sensor rides fabric over the scapulae and the upper thoracic spine:
- **Scapular motion.** Retraction and protraction through the backswing and release move the pocket relative to the thorax. The magnitude for a GPS vest in a golf swing is unknown.
- **Garment slip.** The vest can ride up or rotate on the torso across a session.
- **Kyphosis.** The pocket leans 15–30° from vertical in standing. The mount calibration absorbs that; it is not an error.

**The belt sits on the sacrum.** The sacrum is rigid with the pelvis, but:
- A belt at the iliac crests rides up in a hip hinge. Mount on the flat of the sacrum, below the belt line.
- Skin slides over the sacrum in deep flexion.

**The published ceiling.** Kim et al. 2023 report 0.6–1.7° Bland–Altman against lab motion capture, with IMUs mounted directly on T1 and L4 (normative reference §5). Garment-mounted sensors will be worse by an amount nobody has measured on us. **§11's capture measures it** with static held poses checked against the two-camera fit, and with session-start versus session-end calibration.

**Mounting guidance**, to go in the user guide:
- Sensor in the pocket USB-up, label face out, padded so it cannot rattle, the pocket closed.
- Vest snug.
- Belt sensor USB-up, face out, centred on the sacrum.

A prescribed nominal orientation lets the ceremony catch a gross mis-seat, as the arm flow does today.

### 3.2 Dynamics the filter must survive

Centripetal acceleration at a sensor on the back, a ≈ ω²r with r ≈ 0.10–0.15 m from the spinal axis:

| Segment | Peak rate | ω (rad/s) | Centripetal |
|---|---|---|---|
| Thorax | ~730 °/s (pro mean), up to ~1000 | 12.7–17.5 | **1.7–4.6 g** |
| Pelvis | ~480 °/s, up to ~700 | 8.4–12.2 | **0.7–2.3 g** |

During the downswing the accelerometer is **not** measuring gravity. A complementary filter that corrects tilt toward the accel vector will tilt the trunk by tens of degrees at exactly the moment we read it.

The refuser's adaptive gain (`orientation_refuser.h`, `refuseOrientationAdaptive`) is the existing lever. The trunk estimator (§6.1) must be **gyro-dominant through the swing window**:
- anchored by gravity during the address and finish stillness;
- bias estimated at address.

Over a ~1.5 s window, a residual bias of 0.1–0.5 °/s drifts 0.15–0.75° of yaw. That is negligible against the garment term.

**Range.** The gyro is ±2000 °/s, the accelerometer ±16 g. Neither saturates on the trunk.

**Rate.** The default is 100 Hz, with 200 Hz available (register `0x03`). KS peak timing wants 200 Hz with parabolic peak interpolation. Three Witmotions at 200 Hz, with and without a wG3 streaming alongside, is **unmeasured** over BLE, on the Mac and on GOLFSIMPC. It is a day-1 measurement (§10).

**Gyro scale factor.** A MEMS scale error of about 1–2% turns a 90° shoulder turn into about ±1–2°, and a 700 °/s peak into about ±10 °/s. Timing is unaffected. It is a σ term (§6.2), not a calibration step.

### 3.3 The clock

**How Witmotion samples are stamped today:**
- They are stamped at host arrival (`wt9011dcl_base.cpp:156`), on the same clock as the video.
- There is no clock fit; HackMotion alone has one.
- BLE delivers samples in bursts, which is why re-fusion integrates on the nominal period rather than on timestamp deltas (`orientation_refuser.h`).

**What that means:**
- Per-sample timestamps are quantised to connection events.
- Their mean offset from the exposure instant is unknown. Video frames are stamped at exposure ((memory note `camera-timestamps-from-the-camera`)).

**Plan:**
1. **Re-time the samples.** Use sample index × nominal period, anchored by a robust linear fit to the arrival stamps per stream. This is the same idea as the HackMotion `clockFit`, but needs no device clock.
2. **Measure the offset once on the rig** with a drop test: drop the sensor onto a pad in view of the face-on camera at 150 fps, and compare the accelerometer spike to the contact frame. Record the offset as a per-backend constant.
3. **Check it per swing.** Cross-correlate the IMU pelvis yaw rate with the camera route's (pair or skeleton) over Top→P8. The lag, with its σ, is written into the swing as provenance. It is applied only if Mark agrees (§12 D6), and flagged when it exceeds 10 ms.

Without step 3, the KS mixes IMU trunk nodes with camera arm and club nodes on two clocks.

---

## 4. Frames — the trunk frame contract

### 4.1 The defect

`solveSegment` builds the segment basis with e_y = the long axis, pointing distally. That is **down** at the hanging reference. It then sets `A = conj(refRaw·M)`, which makes the reference pose the identity, so the "world" that q_anat lives in is the body frame at the reference pose. For the arm, world +Y is down. The trunk consumers disagree:

| Consumer | Vertical it assumes | File |
|---|---|---|
| `fillFromImu`: ML axis bearing in world XY | world Z | `body_rotation.cpp:90` |
| `axialFromImu`: `(qAnat·gyro).z` | world Z | `segment_rates.cpp:1268` |
| Transition: `axialRate(pel, (0,1,0))` | segment Y | `phase_segmenter.cpp:290` |
| skeleton3d IMU term: `Qi⁻¹·Rz(ψ)·R·M` | world Z, heading-only freedom | `skeleton3d_fit.cpp:734` |

The skeleton3d term is the subtle one. It gives the fit a free **heading** ψ between the IMU world and the fit's world, but no free tilt. If Qi's world is the reference-pose body frame, a constant world tilt sits between the two. The 3-DOF mount cannot absorb it, because it sits on the other side of the product. The term would fight the cameras.

### 4.2 The trunk frame

Define the trunk segments, and their calibration, so that **the world stays gravity-aligned, Z-up**:

| Axis | Pelvis | Thorax |
|---|---|---|
| **+X** | medio-lateral, toward the golfer's right | same |
| **+Y** | anterior | same |
| **+Z** | superior | same |

This is right-handed (right × anterior = up). +X stays the medio-lateral axis, which `fillFromImu` already projects.

**A is heading-only for the trunk.** The fused world is already gravity Z-up (6-axis Madgwick). A trunk A is a rotation about world Z alone. It turns the arbitrary fusion yaw into the *stance frame*: +Y toward where the golfer faced during calibration, +X toward the target for a right-hander.

So `q_anat` for the trunk is the true segment orientation in a gravity-up, stance-headed world:
- forward bend at address reads as forward bend, instead of being zeroed;
- `world z` really is vertical, for every consumer above.

**Why not reuse the limb solve:**
- It would zero the reference posture, and the trunk's posture *is* a measurement.
- It would leave the vertical on Y, contradicting three consumers.

The limb convention stays as it is. The contract gains a "trunk" row group and a sentence saying the trunk A is heading-only.

**Reporting conventions.** Pelvis and thorax tilt, obliquity and rotation are reported as a Cardan decomposition in the stance frame. The proposed sequence is rotation, then obliquity, then tilt (Z, then Y, then X on this frame's labels). Before it is fixed in code, it must be checked against ISB Part I (Wu 2002, the companion to the `ref.wu2005` the wrist follows). Rule 0 applies if ISB names an order for segment-to-global angles. The signs go into `pinpoint_sign_conventions.md`:
- turn: lead-relative, opening positive, as for `pelvisRotationSigned`;
- tilt: anterior positive;
- obliquity and side bend: lead side up positive.

The axial-rotation definition must be stated alongside them. Rule 0 does not govern it, as the conventions doc already says.

**Fixes that follow:**
- `phase_segmenter` reads the trunk axial rate on world Z through q_anat. This matches the other two consumers.
- **The new end-to-end test:** a synthetic body turns, tilts and bends with the sensors at arbitrary mount rotations. The test generates raw quaternions and gyro, runs the calibration solve on synthetic ceremony data, then checks body_rotation, segment_rates, phase_segmenter and the skeleton3d term all recover the motion. Today's tests skip the solve. That is why defect 1 survived.

### 4.3 The kinematic-sequence rate definition

The normative reference (line 43) notes that Cheetham read the pelvis and thorax rates about the **segment's superior–inferior axis**. Our `axialFromImu` reads the rate about **world vertical**. The two differ by the cosine of forward bend, about 35–45° for the thorax at address, and the difference changes through the swing. That moves both peak values and possibly the order.

**Decided (Mark, 4 Oct, D3): the trunk rate, with fusion managing the camera alignment.**
- **The reported rate is the trunk's own:** the S-I axis component `(gyro_segment)·ẑ_segment`, about the segment's long axis. This is also what the norms measured. The long axis comes from gravity at S1, so this rate does not depend on the mount yaw δ (§5.2).
- **The cameras do not define the rate.** They see a bearing in the horizontal plane, from wherever they happen to be placed and aimed. The fusion reconciles them with the trunk:
  - the skeleton3d IMU term solves the camera-to-IMU heading ψ, and with it the trunk long axis in the camera world;
  - camera routes are graded, and where needed corrected, by projecting the fused trunk into what each camera sees, rather than reporting a world-vertical rate.
  This is how camera misalignment is corrected and managed: a face-on camera not square to the target line, or the DTL off the hand line (memory note `corpus-dtl-clips`).
- **What the fusion learns carries over to camera-only swings.** The per-session camera heading and the camera routes' bias against the trunk are recorded with the bay, for swings without the IMUs (§6.6).
- The world-vertical component is kept only as an internal series for the grading in §8. It is never shown.

---

## 5. The calibration ceremony — standing, turning only

**Mark's requirement (4 Oct):** no hip hinge, no bending at the waist. Some golfers cannot do it reliably. The ceremony uses only standing still and turning while standing.

### 5.1 What has to be found, and what a standing turn can and cannot tell us

For each trunk sensor:
- **M**, the mount, 3 DOF. Split it into two parts:
  - **the sensor's tilt on the body (2 DOF):** which way is "up" in the sensor;
  - **the mount yaw δ (1 DOF):** which way the sensor faces around the body's vertical axis.
- **A**, the heading, 1 DOF: which way the golfer faced.
- **Afterwards:** a check that the result is right, and the record needed to notice when the vest moves later.

**What standing and turning give:**
- **Standing still gives "up" exactly.** Gravity in each sensor is the superior axis (+Z) in sensor coordinates. This includes the vest pocket's lean on a curved upper back, so kyphosis is absorbed.
- **Turning while standing gives nothing about δ. This is physics, not a missing algorithm.** An upright trunk turning about the vertical rotates about +Z, which is already known. A sensor fitted at any yaw around that axis sees the identical gyro signal. No standing turn, at any speed or range, can tell them apart. Only a rotation about a *horizontal* axis can, such as a hinge or a side bend.
- **So δ comes from the mount, then from the swings.** The pocket and the belt fix the sensor face-out on the spine midline, and the ceremony takes "forward" to be straight through the sensor's face (§5.3). Swings then refine δ (§5.4), because every address *is* a forward bend: one the golfer makes naturally, not a calibration motion.

### 5.2 What a wrong δ costs, by metric

Take a mount-yaw error δ, with the trunk bent forward by θ (thorax about 30–45° at address, pelvis about 15–25°):

| Metric | Sensitivity to δ | At δ = 10° |
|---|---|---|
| Rates about the segment's long axis (KS, §4.3 S-I definition) | **None**: the long axis comes from gravity alone | 0 |
| Peak times, sequence order | **None** | 0 |
| Turn from address, X-factor | Bearing offset atan(tan δ·cos θ) changes only as θ changes | about 1–2° over a swing where θ moves 20° |
| Forward bend / pelvic tilt | cos-like, second order | under 0.5° |
| **Side bend / pelvic obliquity** | **First order: asin(sin θ·sin δ)** | **4° at θ = 25°, 6.4° at θ = 40°** |

(The table's values come from that geometry. The synthetic end-to-end test in §4.2 must reproduce them.)

**So the headline uses of the trunk sensors are immune or nearly immune to δ:** the kinematic sequence, the peak times and the turn. Only the side-bend family needs δ to be good, and its σ says so until δ is refined (§6.2).

### 5.3 The steps

| Step | What the golfer does | What it gives |
|---|---|---|
| **S1 Stand tall** | Upright, square to the target line (alignment stick at the toes, or facing the face-on camera), arms relaxed, still for 2 s | **+Z** per sensor from gravity. **δ's starting value** from the prescribed mount: forward = the horizontal projection of the sensor's inward face normal. **A** from the facing direction. Mount gate: the sensor's long (USB) axis within 35° of up, and its face normal within 40° of horizontal. Otherwise "re-seat: USB up, label facing out". |
| **S2 Turn away** | Arms crossed on the chest, turn slowly as if to the top of the backswing, then back to square. Twice. | **Checks, not solves:** the rotation axis lies within 15° of +Z on both sensors; the turn reads "away" on both, which catches a sensor upside-down or face-in; and the thorax turns more than the pelvis, which catches the vest and belt sensors swapped. |
| **S3 Turn through** | The same, turned toward the target as if to the finish, then back. Twice. | The same checks, in the other direction. It confirms the sign symmetrically: the HackMotion lesson that sign is the only thing that falsifies a frame. It also shows the golfer a full range on the check view. |

There is no address hold. §5.8's slip detector takes its baseline from the session's first swings instead.

**The solve:** e_z = gravity-up at S1; e_y = forward, the sensor's inward face normal made orthogonal to e_z; e_x = e_y × e_z. A is the yaw about world Z that takes e_y at S1 onto stance +Y. It is `solveTrunkSegment()`, a pure function in `imu_calibration.h` with unit tests, never inside QML.

**Failure messages name the sensor and the cause**, for example "Chest sensor reads the turn backwards — check it sits USB-up, label facing out", or "Hip and chest sensors look swapped".

**A candidate to try on day 1, not in the plan:** "walk three steps toward the target and stop". The trunk accelerates and brakes along its forward direction at the start and end of walking, which would give δ without any bending. Whether it holds to ±5° through gait sway is unknown. Record it once on day 1, then decide.

### 5.4 Refining δ from the swings

δ starts at the prescribed mount, with σ_δ about 10° as a placeholder. The truth session measures how far the vest pocket really sits from the prescription, and that number replaces the placeholder. If the prescription proves good to a few degrees, the refinement is a refinement, not a necessity.

| Tier | Needs | How |
|---|---|---|
| **δ0 Prescribed** | Nothing | §5.3 S1. σ_δ ≈ 10° until measured. |
| **δ1 Address symmetry** | Any swings | At address the trunk bends mostly forward, so gravity in the segment frame should lie in the sagittal plane. The median over the session's first swings gives δ. Its bias is the golfer's real side tilt at address, which the face-on camera's spine side bend corrects when present. Without that correction σ_δ stays wide on the thorax, because irons and especially the driver have side tilt at address. |
| **δ2 Two-camera fit** | Face-on + DTL | skeleton3d solves the mount with δ free under a σ_δ prior around δ0. It is pooled over the session, because address and the swing bend the trunk, which makes δ observable. This is the best tier. |

The binding records which tier set δ, and its σ, so a re-analysis can improve it (δ1 → δ2) without the golfer.

### 5.5 The heading reference

The stance frame's heading is only as good as S1's facing. There are three tiers, best last:

1. **"Face the camera."** No equipment needed. It biases every *absolute* turn by a few degrees, but not the turn from address. Most readings (P4, P7, rates) are from address, so they are unaffected.
2. **Alignment stick.** Toes against a stick on the target line. This is the same stick the camera calibration ceremony uses (memory note `camera-calibration-thread`, G2), so the IMU stance frame and the camera world share one physical reference.
3. **Camera-solved per swing.** The skeleton3d term solves a heading ψ per IMU per swing. With the trunk M known, ψ *is* the IMU-to-camera heading, and once the cameras are calibrated against the target line it gives an absolute pelvis heading at address on every swing.

**Drift between swings.** In 6-axis mode, yaw drifts across a session at the residual gyro bias. Within-swing readings are re-referenced to each swing's own address, so drift matters only to an absolute "open at address" reading. That needs tier 3, or the magnetometer experiment (`imu_rearchitecture.md` Track C, off the critical path).

### 5.6 One ceremony with the arm

**With three units**, a user's options are:
- **2 Witmotions on the trunk and a HackMotion on the wrist.** Mark confirmed on 4 Oct that this is allowed (see §6.8).
- **2 on the trunk and 1 on the arm.**
- **The existing 3 on the arm.**

The trunk ceremony needs no arm motion, and S1 is the same standing posture as the arm flow's "arm down":
- **Witmotion arm + trunk:** arm-down / S1 → arm T-pose → S2 → S3. The arm sensors ignore the turns; the trunk sensors ignore the T-pose.
- **HackMotion + trunk:** the wG3's own device routine (forearm across the chest, then raise), then S1 → S2 → S3. The two are independent; neither reads the other's motion.
- **Trunk only:** S1 → S2 → S3, under a minute.

**How it gets built, given the wizard rule:**
- The trunk ceremony lives in a **new** `TrunkCalibrationFlow.qml`. It reuses the hold-gate and avatar components, and drives the solve through a new `ImuInstance` method.
- The wizard needs one change: show the Calibrate step when any trunk or arm sensor is assigned (today `hasCalibrateStep` is Wrist-only, `ScreenSessionWizard.qml:82`), and run the flows in sequence.
- That change, and any later merge into `ImuCalibrationFlow.qml`, is proposed to Mark separately with a `--probe-qml` script that drives it.
- The raw ceremony data (samples and step boundaries) is **recorded with the session**, so swinglab and the unit tests can replay the solve without the golfer.

### 5.7 Demonstrate, then verify: the avatar guide and the live trunk model

**The pattern.** The trunk ceremony follows the pattern the Witmotion and HackMotion arm flows already use, over the same two wizard pages:
1. **Calibrate.** The `BodyVizView` avatar demonstrates each step before the golfer does it. A hold bar fills while they hold still, and resets if they move.
2. **Check your sensors.** A live model driven by the calibrated sensors, which the golfer moves freely to confirm it follows them.

Mark, 4 Oct: this is required, not optional polish.

**Page 1 — the avatar guide.** For each step the avatar performs the motion first, then waits in the pose for the golfer:

| Step | The avatar shows | The golfer sees |
|---|---|---|
| S1 Stand tall | Standing square, arms relaxed, facing the target-line marker | The hold bar while still. A mount-gate failure names the sensor ("Chest sensor: turn it USB-up, label out"). |
| S2 Turn away | Arms cross on the chest, then hips about 45° and chest about 90° turn away, a pause, then back to square. Twice, at the pace asked for. | The same hold-and-move cadence. Pass/fail per sensor with the cause (backwards, wrong axis, swapped). |
| S3 Turn through | The same, toward the target | The same |

**What has to be built:** `BodyVizView` gains trunk overrides, mirroring its arm ones (`useLeadArmOverride` …, l.117–125):
- `useTrunkOverride`;
- `hipsOverrideRotation`, replacing `adapter.hipsRotation` on `hipsNode`;
- `thoraxOverrideRotation`, distributed over Spine/Spine1/Spine2 by segment length, the way the lean rig shares spine twist.

The crossed arms reuse the existing lead and trail arm overrides. The animation is a QML sequence over these properties, with no change to the guide's own state machine.

**Page 2 — the live trunk check.** `ArmVizView` is arm-chain only, so the trunk needs its own view. The proposal is a `BodyVizView` in **IMU mode**, at whole-body framing:
- `hipsNode` from the pelvis q_anat;
- the spine chain from the thorax relative to the pelvis;
- for a single Witmotion on the lead upper arm, that arm too.

Beside it, one line of numbers, per the tile rule (memory note `ks-sequence-tile`): **hips 45° · chest 88° · X 43°**.

The golfer turns freely and the avatar must follow. Turning away must turn the model away, and opening must open it. That is the sign check made visible.

**Optional, informative, not gated: "take your address".** If the avatar bends forward with them, the mount yaw δ0 is good. If the avatar leans sideways while they bend straight forward, a sensor is twisted in its pocket: re-seat it. It is the one place a golfer can *see* δ (§5.2), from a posture they make anyway. It is never a gate, because real address side tilt is legitimate.

**Per configuration:**

| Configuration | Page 1 | Page 2 |
|---|---|---|
| Trunk only | S1–S3 | Live trunk model |
| Trunk + HackMotion | wG3 device routine with its existing avatar guide, then S1–S3 | The existing `ArmVizView` wrist check, then the live trunk model |
| Trunk + 1 arm Witmotion | Arm-down/S1, T-pose, S2, S3 | The live whole-body model with the lead upper arm |
| Arm only (3 Witmotions) | Unchanged | Unchanged |

**Rendering traps, all already paid for once:**
- **Frames.** The world→scene basis change is a fixed Rx(−90°), but every bone also carries a per-GLB rest offset (`imu_frame_contract.md` §6, `imu_rearchitecture.md` §3.4c). A trunk bone bound with only the basis change will look plausible and be wrong. Pin it with a viz golden test, as the arm chain was.
- **Render, don't reason.** The HackMotion guide passed its own numeric self-check with the wrong motion (memory note `hackmotion-integration`). Every animation and the live view are checked by rendering them through a `--probe-qml` script: a synthetic turn in, a frame grab out. The Qt Quick 3D traps are `clipNear` 10 and an explicit `camera:` inside a Repeater.
- **Soak.** View3D views have gone blank before (memory note `view3d-disappearance-watch`), so the new view gets the same soak as the arm check (E2: a 6.5-minute render soak).
- **Wizard rule.** The overrides and the IMU mode are `BodyVizView` changes, outside the protected files. The page wiring is a wizard change: proposed separately, with the probe that runs it (memory note `do-not-touch-the-session-wizard`).

### 5.8 After the ceremony: slip, and staleness

The vest will move during a session. Two checks catch it:
- **Per-swing address residual.** The baseline is the median address posture of the session's first three swings: pelvis tilt, thorax bend, and thorax-relative-pelvis heading. Each later swing is compared with it.
  - A step change beyond a gate (about 8°, to tune) that persists over the following swings is a **slip**.
  - The response: a session toast ("The chest sensor has moved — recalibrate when convenient"), and the swing's trunk σ widened by the residual.
- **End-of-session re-check.** An optional S1 + S2 at the end gives the mount drift over the session. It is the number §3.1 needs, and it costs 20 seconds.

**Persistence:**
- Per session, as today, snapshotted per shot into the binding: `alignA`, `mountM`, the gate residuals, `calibratedAt`, the δ tier and σ_δ.

---

## 6. Analysis changes, by consumer

### 6.1 The trunk orientation estimator

The trunk lanes are re-fused offline from the recorded raw accel + gyro (`imu_vision_fuser.cpp`, `filter.refuse`), with a trunk profile:
- the gyro bias is estimated over the address stillness;
- the accelerometer correction is gated off while |a| departs from 1 g or |ω| exceeds a threshold, so the swing is integrated on the gyro alone;
- the tilt is re-anchored at the finish stillness by distributing the end-point gravity error linearly across the swing. This is a two-sided correction; yaw stays one-sided.

It writes the corrected quaternion into the same `SegmentStream.qAnat`, so every consumer benefits without change. Gating it on the role keeps the arm lanes byte-identical.

**Why it is needed** is §3.2: the live Madgwick tilt is wrong by design during the downswing. Measure it before building it:
- Day 1: re-fuse one worn swing both ways.
- The finish-versus-address gravity residual says how much tilt the live filter lost.

### 6.2 σ for IMU rotation

Per sample, in quadrature:
- **Garment / soft-tissue term.** Constant per segment; starts at 3° (thorax) and 2° (pelvis) as placeholders, replaced by §11's measurement.
- **Calibration term.** The ceremony's axis residuals carried through.
- **Gyro scale term.** 1.5% · |turn from address|.
- **Bias drift.** Residual bias σ × time from address.
- **Slip term.** §5.8, when flagged.
- **Mount-yaw term.** σ_δ through §5.2's sensitivities. It is first order for side bend and obliquity, near zero for turn, and zero for the S-I rate and peak times.

`SigmaKind::Propagated`. This closes the census rows for the IMU tier ("IMU readings reach diagnostics with no σ at all").

### 6.3 Body rotation

- **Signed series.** Build the IMU route for `pelvisRotationSigned` (catalogue line 684, PLANNED) and a new `thoraxRotationSigned`. This fixes defect 2.
- **Mixed-route X-factor.** Pelvis from the IMU with thorax from the cameras, or the reverse. Today such a swing gets no X-factor (`body_rotation.h:196`). With both on one clock and the σ stated, the difference is honest only if the two share a heading reference. That means tier 3 of §5.5 or relative-to-address on both. Proposal: allow it relative to address, σ in quadrature, route `trunkMixed`, Estimated. Mark's call (§12 D4).
- **New posture metrics** (§2.2): `pelvisTilt`, `pelvisObliquity`, `thoraxForwardBend`, `thoraxSideBend`, from the §4.2 decomposition. Each gets an IMU route on the existing key where one exists (`spineForwardBend`, `spineSideBend`, `hipLineTilt`), not a new key, so the diagnostics pick it up through route precedence.
- **Measures to add to `core.json`:**
  - `m_xFactorP4`;
  - `m_pelvisTiltChangeImpact`, the companion to `m_pelvisThrustDown` for early extension;
  - `m_thoraxBendChangeImpact`, loss of posture;
  - pelvis deceleration present/absent.
  Norms are seated wide and labelled one-golfer until a cohort exists. Characteristics come later, not this week.
- Remove the stale foreshortening descriptions the agent found: `body_rotation.h:32-77` and the catalogue `howToRead` text at `manifest.cpp:592` and `713`.
- `BodyRotationStage::run` dereferences `ctx.job.cameraSources.front()`. It must cope with an IMU-only job.

### 6.4 Kinematic sequence

- Rate definition per §4.3: the trunk S-I rate (D3).
- The thorax node from the IMU is placed. `pairTrunkThoraxPlacement` and `skel3dThoraxPlacement` stay as they are for camera-only swings until §11 grades them.
- Clock: per §3.3. Mixed IMU and camera nodes carry the measured lag in their σ_t.
- Gain measures: Cheetham's speed gain (§2.2), and timing consistency across a session (KS design §11 Q3).

### 6.5 Phases

- **Fix defect 3 first.** `SegResolveStage` adopts the IMU segmentation only when `segImu->conf > 0`, or it arbitrates per slot as `timeline-fusion.md` proposes. A trunk-only job then keeps the vision ladder.
- **Add trunk evidence to the ladder:**
  - Transition from the pelvis reversal, emitted even without an arm sensor;
  - Top from the thorax reversal, voted with the club track;
  - P1 from thorax onset.
  This sits under the existing timeline-fusion arbitration, not beside it.

### 6.6 skeleton3d

The wiring exists (`wrist_analyzer.cpp:2580`: Pelvis → Hips, Thorax → Spine2). It needs:
- **Feed it the gravity-world orientation and a known mount.**
  - Pass the trunk q_anat, which is gravity-Z-up per §4.2, so the term's heading-only ψ is the right freedom.
  - Set `hasMount = true` from the calibration. It is never set today, so a constant segment offset is indistinguishable from a mount error.
  - The mount prior is 3°.
- **σ:** the term's fixed `imuSigmaDeg` = 4 becomes the §6.2 σ per sample.
- **Priors:** with a pelvis IMU bound, relax `pelvisYawAccRad` and `pelvisTiltAccRad` toward the generic smoothness. Those priors exist only because the cameras cannot see these DOF.
- **Pooling:** add IMU ψ to the session pool. One ψ per sensor per session, so a camera epoch change and a vest slip show up as distinct events.
- **The clavicle split:** with the thorax sensor on Spine2, the fit's shoulder line minus spine twist is clavicle motion. Expose it as a diagnostic series, and test the lean rig's "clavicles follow the upper arm" rule ((memory note `skeleton3d-lean-fit`), which failed on cameras alone) against it.
- **Camera alignment (D3).** Per session, the fit solves each camera's heading against the trunk world through ψ, and the mount yaw δ (§5.4 tier δ2).
  - Both are written to the session pool with the camera epoch.
  - Later camera-only swings in the same epoch inherit the measured camera heading, and the camera trunk routes' measured bias against the fused trunk. This is how a session worn with the IMUs improves the sessions without them, until the cameras move.
- `kSkeleton3DStageVersion` bump.

### 6.7 Placement, roles, devices

- **Placement is role-keyed, not slot-keyed.** `imu/placement` values become role names (`pelvis`, `thorax`, `leadForearm`, `leadHand`, `leadUpperArm`).
  - A one-time migration maps today's A/B/C via the Wrist map.
  - `segmentRoleForSlot()` loses its `sessionType == 1` gate. It survives only to read old swings. This is required by (memory note `analysis-is-session-agnostic`): a pelvis sensor is a pelvis sensor in every session type.
  - The Witmotion binding's direct `placement.value(deviceId)` (`shot_processor.cpp:1307`) goes through the resolver.
  - The HackMotion A/B/C scan (`:1373`) reads roles.
- **The Settings panel** offers Pelvis (belt, sacrum) and Thorax (vest, upper back), alongside the arm roles, in its existing combo. The slot letters disappear from the UI. The current A–D labels already say "Thorax / Lumbar"; those are replaced by the role names.
- **The wizard's IMU step** already lists trunk requirements for the coming-soon session types (`imuRequirements`). For a Wrist Motion session it accepts trunk sensors as optional extras. This needs approval like any wizard change.
- **`SegmentRole`** is persisted as an int: mark it APPEND-ONLY as `Phase` is.
- **Integrity.** Before the first trunk capture, run `--refuse-orientation` on one worn trunk swing:
  - If live and re-fused agree, `imuIntegrity` is fine.
  - If they do not (the Witmotion history says they will not), the trunk lanes take the HackMotion-style exclusion until it is understood, so every shot does not carry a false ⚠. This is the 179.8° HackMotion lesson again.

### 6.8 HackMotion on the wrist, Witmotions on the trunk

**Decided (Mark, 4 Oct, D1): allowed.** (memory note `no-dual-instrument-wear`) is about two instruments strapped together on the **arm**. A wG3 on the wrist and Witmotions on the back and sacrum are not co-located, and their roles are disjoint, so `streamFor` cannot collide.

What it needs:
- **Bindings:** the HackMotion and Witmotion bindings coexist in one shot.
- **The placement guard:** `ImuManager` refuses only a second claim on the same **role**, never a mix of instruments.
- **`imuIntegrity`:** applies the HackMotion skip to the wG3 lanes alone, and judges the trunk lanes on their own (§6.7).

**Units (Mark, 4 Oct, D2):** a user is expected to own **three** Witmotions. The trunk uses **two** of them. The configurations to support:
- **Trunk only:** 2 Witmotions.
- **Trunk and HackMotion:** 2 Witmotions plus the wG3.
- **Trunk and one arm Witmotion:** 2 plus 1. The single arm unit goes on the **lead upper arm**, where it feeds the KS arm rung (`segment_rates.cpp` takes LeadUpperArm first). No wrist angles come from a single unit.
- **Arm only:** 3 Witmotions, as today.

A trunk-plus-arm session never has more than three Witmotion links, plus the wG3's.

---

## 7. The capture protocol for the truth session

It extends `two_camera_capture_protocol.md` and keeps everything that document asks for: LM on every swing, cameras untouched, the rig sheet. Trunk additions:

1. **Ceremony at the start**: S1–S3, plus the walk-three-steps candidate (§5.3) and the drop test for the clock (§3.3) in view of the face-on camera.
2. **Static held poses, five:** address; half-way back; top; impact position; finish. Hold each 2 s. The cameras are sharp on a still body, so the two-camera fit and the IMU are compared with motion blur out of the picture. This is the cleanest grade of the camera rotation level, and of the garment term.
3. **20 full swings, 7-iron.**
4. **Block E from the protocol: 5 three-quarter, hips-first swings.** This is the first chance of a trunk peak inside the downswing (KS §13.9 #8).
5. **Fault blocks, five each:** deliberate early extension (stand up through impact); sway. Ground truth for the posture metrics' signs.
6. **Drift hold:** stand at address for 60 s, then 5 swings. This measures the between-swing yaw drift.
7. **Ceremony at the end** (S1 + S2): mount drift over the session.
8. **δ truth:** the static held poses in item 2 are bent, so the two-camera fit solves δ on them; that against δ0 is the prescription's real error.

Each swing is analysed twice:
- **bound**, the normal path;
- **unbound**, with the trunk roles ignored, which is a swinglab switch to add (`--ignore-role pelvis,thorax`).

The paired table is the deliverable of §2.1.

---

## 8. Grading: what the paired table must report

Per segment and per route (face-on, pair, skeleton3d), against the IMU:

| Quantity | Statistic | Decision it feeds |
|---|---|---|
| Turn at P1/P4/P7 (from address) | Median \|Δ\|, bias, σ coverage at 1σ and 2σ | `kTriScaleFrac`; the corridor tier question (`body_rotation_estimation.md` §6 Q2) |
| Absolute address heading (tier 2 stance) | Bias | The 15–19° "open at address" question |
| Peak rate | Ratio camera/IMU | The 640–840 °/s question |
| Peak time | Δt, coverage by σ_t | §9 stage 2 rule: median \|Δt\| ≤ 20 ms and coverage ≥ 68% ⇒ the rung ships Estimated; otherwise it stays PLANNED with the measured σ |
| Thorax shoulder line − IMU thorax | Series | The clavicle term's size; whether thorax placement can ever be turned on for cameras |
| Static poses | \|Δ\| per pose | The garment term (§6.2), and the camera's best case |

Judge per swing, not by an aggregate score ((memory note `windows-reanalysis-divergence`), (memory note `reanalyze-library-workflow`)). Diff every metric against an unbound control, not just the ones being chased (the lesson in (memory note `ks-sequence-tile`)).

---

## 9. Order of work, and why

1. **The frame contract, the solve and the end-to-end test come first.** Every later number depends on the vertical being right, and a wrong axis is invisible in plausible-looking output.
2. **Hardware measurements before the estimator:** rate, BLE count, clock offset, live-versus-re-fused tilt. Each one decides whether a planned piece is needed at all. The camera calibration thread's "measure first, build later" lesson applies ((memory note `camera-calibration-thread`)).
3. **The ceremony before the capture.** The capture needs a calibrated mount, and the recorded ceremony is what makes the solve replayable.
4. **Defects 2 and 3 before the capture's analysis,** so the first worn swings do not lose `hip_stall` and their phase ladder.
5. **skeleton3d and the camera grading after the capture.** They need real worn swings.

---

## 10. The week

This needs Mark at the hardware on day 1 (about 30 minutes) and day 3 (about 90 minutes). Builds are kept to two or three per task ((memory note `build-and-test-economy`)).

| Day | Work | Gate at the end of the day |
|---|---|---|
| **Mon 5** | **Frames and measurement.** Write `imu_frame_contract.md` §5 trunk rows, `solveTrunkSegment()` and the synthetic end-to-end test. Fix the phase_segmenter axis. Role-keyed placement with migration; `SegmentRole` append-only. **Mark (30 min):** wear the vest and belt; stream 3 Witmotions at 200 Hz, then 2 Witmotions plus the wG3, and watch the counts (BLE budget); record the walk-three-steps candidate (§5.3); record a 5-min still hold (bias); the drop test (clock offset); 5 swings recorded through a Wrist session with the new placement (raw material for days 2–3). | The end-to-end test passes. Both trunk lanes appear in a swing as Pelvis/Thorax. Measured: achievable rate with 3 Witmotions and with 2 + wG3, the bias, the clock offset. |
| **Tue 6** | **Calibration and estimator.** `TrunkCalibrationFlow.qml` (new file), the `BodyVizView` trunk overrides and S1–S3 demonstrations, the IMU-mode live trunk check (§5.7), ImuInstance plumbing, ceremony recording, offline replay in swinglab. The wizard change is proposed with a probe script. Run the trunk refusion on Monday's swings and decide whether §6.1 is needed from the numbers. Fix the integrity exclusion if parity fails. | The ceremony runs end to end under `--probe-qml`. Frame grabs show the avatar demonstrating S1–S3 and the live model following a synthetic turn with the right sign. The gates reject a synthetic flipped mount. The live-versus-refused tilt error is measured. |
| **Wed 7** | **Producers, then the capture.** The σ model, signed IMU series (defect 2), SegResolve (defect 3), posture metrics, KS rate definition, the `--ignore-role` switch. **Mark (90 min): the truth session (§7).** | A worn swing re-analysed shows measured, σ-carrying pelvis/thorax rotation, placed trunk KS nodes, and `hip_stall` and `sequence_order` assessable. A trunk-only analysis keeps the vision ladder. |
| **Thu 8** | **Fusion.** The skeleton3d trunk term with a known mount, σ, relaxed priors, ψ pooling, the clavicle series, the stage-version bump. The per-swing clock cross-correlation. GOLFSIMPC sweep of the session, bound and unbound. | The 3-D hips no longer pause through impact on a worn swing. Bound-versus-unbound diffs reviewed for every metric. |
| **Fri 9** | **Grading and review.** The §8 paired table and the research note in `docs/research/data/trunk_imu/`. Feed the measured σ into the census rows and `kTriScaleFrac`. Recommendations on the thorax placement switches. User guide section for the vest and belt. Mark reviews in the app; commits at the end, then push on his word. | Every §12 decision answered or explicitly deferred. Every §8 number reproducible from a command in the note. |

**What slips first if the week runs short:** the mixed-route X-factor (D4), the posture measures in `core.json`, and the clavicle series. The ceremony, the three defects and the paired table do not slip. They are the point.

---

## 11. Results

*(Filled in during the week, with the run root named.)*

- Day 1 measurements: rate, BLE count, bias, clock offset.
- Live-versus-refused trunk tilt at impact.
- Garment term: static poses, session-start versus session-end mount.
- §8 paired table.

---

## 12. Decisions for Mark

| # | Decision | Recommendation |
|---|---|---|
| D1 | May a wG3 on the wrist be worn with Witmotions on the trunk? | **DECIDED 4 Oct: yes** (§6.8) |
| D2 | How many units? | **DECIDED 4 Oct:** users have 3 Witmotions; start with 2 on the trunk (§6.8) |
| D3 | KS trunk rate | **DECIDED 4 Oct:** the trunk's own (S-I) rate, with fusion correcting and managing camera alignment (§4.3) |
| D10 | Ceremony with no hinge | **DECIDED 4 Oct:** standing and turning only. δ comes from the prescribed mount, refined from swings (§5) |
| D11 | Try the walk-three-steps step on day 1 as a δ source? | Yes, record once; adopt only if it holds to about ±5° against the two-camera δ |
| D4 | Mixed-route X-factor (IMU pelvis + camera thorax)? | Allow, relative to address, Estimated, σ in quadrature |
| D5 | Heading reference for the ceremony: "face the camera" or the alignment stick? | The stick. It is the camera ceremony's G2 object, so the two worlds share it. |
| D6 | Apply the per-swing IMU↔camera lag from cross-correlation, or only record it? | Record and flag this week; apply once the paired table shows it is stable |
| D7 | Slip handling: toast only, or also widen σ automatically? | Both. The toast tells the golfer; the σ keeps the diagnostics honest. |
| D8 | Combined arm + trunk ceremony in one sequence (§5.6), which touches `ImuCalibrationFlow.qml`? | Yes, but after the trunk-only flow has run on hardware. Separate approval. |
| D9 | The 9-axis heading experiment on day 1? | Only if the day has room. Off the critical path. |

---

## 13. Risks

- **The garment term may be large.** A vest pocket over moving scapulae could be worse than the camera on some quantities. §8's static poses measure this. If it is large, the thorax IMU stays a *timing* instrument (rates and peaks are less sensitive to a slow pocket shift) while the level comes from the fusion. The design does not assume the IMU is truth on every axis. It measures that first.
- **BLE capacity at 200 Hz with three Witmotions (or two plus a wG3)** may force 100 Hz. KS timing at 100 Hz with peak interpolation is still about 2–3 ms. Acceptable, and stated in σ_t.
- **Witmotion parity has never passed.** If the root cause is deeper than the ESKF-capture hypothesis, the trunk lanes run on re-fused orientation only (§6.1), and the integrity check needs its own trunk rule.
- **One golfer.** Everything graded this week is Mark's swing. Corridors and norms stay where they are.
- **The prescribed mount may be poor.** If the vest pocket sits well off the midline or twisted, δ0 is wrong, and the side-bend family is weak until δ1/δ2 refine it. The KS, the peak times and the turn are unaffected (§5.2), so the core value survives.
- **Wizard regressions.** Contained by the new-file approach and per-change approval with a probe.
