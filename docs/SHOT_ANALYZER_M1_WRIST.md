# M1 — Wrist Motion

*A concrete refinement of [`SHOT_ANALYZER_DESIGN.md`](SHOT_ANALYZER_DESIGN.md) for the **Wrist Motion** session (`SessionController::Type::Wrist == 1`): **one face-on camera + three Witmotion WT901BLE67 IMUs on the lead arm** (back-of-hand, forearm, upper arm). This document fixes the algorithms, type names, file targets and exit criteria so `WristAnalyzer` can be built directly against the committed code.*

---

## 1. Overview & why this is the strongest single-camera config

With the **entire lead arm instrumented**, the headline wrist-coaching metrics — lead-wrist flexion/extension (FE), radial/ulnar deviation (RUD), forearm pronation/supination (PS), and elbow flexion — become **pure IMU orientation math**. They are *relative quaternions between adjacent rigidly-instrumented segments*, so they need **no camera intrinsics, no extrinsics, no triangulation, no monocular depth lift**. A single face-on RGB camera is sufficient — its job is placement, an elbow cross-check, phase/impact backup, and the on-screen visual, never the joint-angle source.

This is the **single best return on one camera** in the whole product. Every other session type (`Swing`, `GRF`, `Coach`) needs either multi-camera triangulation or an ill-posed mono-3D lift to reconstruct arm orientation; here the orientation is *measured*, drift-bounded by gravity, at 100 Hz. Out-of-plane and axial rotations — which a single 2D view fundamentally **cannot** observe — are exactly what the IMUs deliver.

**What ships at M1:** `WristAnalyzer` replaces `WristStubAnalyzer` (`shot_analyzer.cpp:91`) and emits four *real* metrics keyed for `ScreenWrist.qml` — `leadWristFlexExt`, `leadWristRadUln`, `forearmPronation`, `leadArmFlexion` (the last two need the **optional** upper-arm IMU — without it `forearmPronation` is suppressed/`estimated` and `leadArmFlexion` falls back to the camera 2D elbow angle; wrist FE/RUD stay full-fidelity) — sampled at **Address / Top / Impact**, scored against HackMotion-grade one-sided bands, plus a real lead-wrist-FE trace (Address→Impact) and a `SwingAnalysis` detail object. The ceiling is **HackMotion-grade**: validation target of elbow FE RMSE ~2–4° (functional) / ~6–8° (static), wrist FE / PS ~4–5° vs an optical reference, with the clubface coupling (~1° lead-wrist FE ≈ 1° face) surfaced as an *approximate* coaching cue.

---

## 2. Sensor map & data flow

### Physical → SegmentRole binding

| Wizard slot | Placement label | `SegmentRole` | Mount constant | Required |
|---|---|---|---|---|
| **A** | Forearm (wrist anchor) | `LeadForearm` | `nominalArmMount()` | yes |
| **B** | Hand (back of glove) | `LeadHand` | `nominalHandMount()` | yes |
| **C** | Upper arm | `LeadUpperArm` | `nominalArmMount()` | **optional — pronation/elbow degrade if absent** |

> **Wizard reconciliation.** `ScreenSessionWizard.qml` keeps Wrist `requiredImus:2` with slot C (upper arm) `required:false`. Forearm + hand alone fully determine **wrist FE/RUD** (the headline metrics); the upper-arm IMU is needed *only* for **forearm pronation/supination** and the IMU **elbow flexion** term, so it stays optional and those two **degrade gracefully when slot C is absent** (suppressed / flagged `estimated`, never faked — and `leadArmFlexion` falls back to the camera 2D elbow angle). The only wizard change is the slot-A *display label* (the existing `"Wrist"` string → `"Forearm"`); the authoritative binding to `SegmentRole` happens once in C++ (below).

The device→slot map already exists: **`AppSettings::imuPlacement`** (`QVariantMap deviceId → "A"|"B"|"C"`, QSettings key `imu/placement`, written from `ImusPanel.qml`). It is QML/QSettings-only today. A new resolver runs **on the UI thread** in `ShotProcessor::startAnalysis()` and packs each live IMU into the design's `ImuSegmentBinding`:

```cpp
struct ImuSegmentBinding { pinpoint::SourceId source; SegmentRole role; QQuaternion alignA, mountM; };
```

For each session-enabled `ImuInstance`: read `imuPlacement[deviceId]`, map the slot letter to `SegmentRole` via the Wrist slot table, snapshot `inst->sourceId()`, `inst->alignA()`, `inst->mountM()`. These A/M are **session-lifetime on the live QObject and never reach the ring** — the snapshot is the only way the worker gets them. Append `std::vector<ImuSegmentBinding> imuBindings` to `ShotAnalysisJob` (the design already specifies this field).

### Timeline in the frozen window

Each IMU registers a `DeviceKind::IMU_WitMotion` source, schema `imu_sample_v1`, **100 Hz**, into the `EventBuffer`. The 40-byte `pinpoint::ImuSample` stores `accel[3] (g)`, `gyro[3] (°/s)`, `quat[4] (w,x,y,z)`. **Critical:** accel/gyro are axis-remapped at write time but **the quaternion is the RAW fused sensor-body quaternion** (host-side Madgwick/ESKF, gravity-referenced, yaw-drifting — *not* anatomical, *not* remapped). The analyzer **must** re-apply `q_anat = A·q_raw·M` itself per binding.

`WristAnalyzer::analyze()` reads the frozen `SwingWindow`:
* `window.entriesFor(binding.source)` → time-ordered `IndexEntry`s;
* `window.payloadOf(entry)` → `ReadHandle`, `reinterpret_cast<const ImuSample*>(handle.data)` (null-check the handle);
* `window.entriesFor(job.markerSourceId)` → impact instant (the `IndexEntry::timestamp_us` *is* impact; no payload parse).

### Face-on camera

`PoseRunner` (design layer 1) re-runs body pose (today MoveNet COCO-17) over the exported face-on frames → 2D shoulder/elbow/wrist. `job.cameraSources` is face-on-first. **Handedness:** lead arm = **left** for a right-handed golfer (`AthleteController::currentHandedness`). The face-on webcam path uses `mirroredSource = true` (mirror-flips the x component, per `BodyPoseAdapter`); the analyzer must honor handedness when choosing which COCO joints are the *lead* shoulder/elbow/wrist and when applying ISB left-arm sign mirroring (§4).

---

## 3. Sensor-to-segment calibration

### Goal

One **constant** unit quaternion `alignA` (`A_seg`) per IMU such that `q_segment(t) = A · q_raw(t) · M`, with `M` = the fixed strap mount (`nominalArmMount` / `nominalHandMount`) and `A` chosen so the segment reads its rest orientation at the captured reference pose. Joint angles are then relative quaternions (§4) and are immune to the shared yaw datum.

### Procedure (golfer-friendly, ~8 s — extends the existing two-pose flow)

The codebase already ships a validated two-pose flow in `ImuCalibrationFlow.qml` driving `ImuInstance` calibration methods. M1 keeps it and fixes two bugs:

1. **Arm-down (static, ~2 s).** Stillness-gated (≤15 °/s, slerp-averaged). For each connected slot call `setNominalCalibration(refRaw, handMount)` — `M = nominal`, `A = conj(refRaw·M)` so `anatQuat == identity` at arm-down. Gravity flip-check sets `mountGravityErrorDeg`.
   * **Confirmed bug fix:** `ImuCalibrationFlow.qml:458` calibrates the **hand** (slot B) with `handMount=false` — the "strap convention" comment at `:447-451` wrongly lumps the glove sensor with the arm straps. The hand IMU is **dorsal (back of glove)**, so slot B must pass **`handMount=true`** to use `nominalHandMount()`, not `nominalArmMount()`; otherwise wrist FE/RUD inherit a constant offset **and** FE↔RUD cross-talk.
2. **Abduction (functional, ~3 s).** Raise the arm to ~horizontal; `refineMountAboutLongAxis(refRaw, phiDeg, handMount)` sets `M = nominal·Ry(φ)`, `|φ|≤25°`, re-anchors `A`, and sets `mountDeviationDeg`. **Two-gate accept** (both required): `mountDeviationDeg ≤ 15°` (strap rotation) **AND** `mountGravityErrorDeg ≤ 25°` (flip). One gate alone is insufficient — gravity catches flips that `φ` is blind to.

For deeper accuracy, `setFunctionalCalibration(refRaw, gravityDown, longAxis, flexAxis)` runs the full `imu_calibration::solveSegment()` (Seel-style functional axes): build `e_y = long`, `e_x = flex` (Gram-Schmidt), `e_z = e_x×e_y`, `M = fromAxes(...)`, `A = conj(refRaw·M)`; `axisAngleDeg` near 90° gates quality. This is the path to upgrade the abduction gate to a true elbow-flex / pro-sup sweep later.

### CALSW relationship & per-shot heading re-zero

The on-device `CALSW` "set angle reference"/"zero heading" (`WT9011DCL_BLE::zeroToCurrentPose`) is **vestigial** in this build — orientation is fused host-side and the ring stores `q_raw`. It is **not** a substitute for `A`: CALSW is a *global* zero and cannot encode the ISB anatomical frame; `A` does. **6-axis drift handling** is purely software and relative: (a) all three IMUs share the *same* world frame seeded from gravity, so the **tilt** part never drifts; (b) only about-vertical yaw drifts, and that **cancels exactly** in every adjacent-segment relative quaternion `q_prox⁻¹·q_dist`; (c) a golf shot is ~1–5 s so residual differential drift is a few degrees at most. No per-shot device CALSW is needed; the relative-quaternion formulation is the drift fix.

### Validity & wizard (session-only, 30-min drift timeout)

`A`/`M` live on the live `ImuInstance` and are **session-only — never persisted across app restart or device reconnect.** 6-axis fusion has no absolute heading reference, so a stored calibration goes stale as yaw drifts; rather than save a frame that will be wrong on reload, M1 stamps each calibration with a monotonic capture time and treats it as **valid for 30 minutes**. After expiry — or any reconnect — `calibrationValid` flips false and the UI prompts a re-calibrate before the next shot is accepted. **Do not wire the dead `AppSettings::imuCalibration` map** (key `imu/calibration`, zero callers): cross-restart persistence is explicitly out of scope because it would resurrect a drifted, wrong anatomical frame. The job carries a per-binding `calibAgeMs` / `calibValid` so the worker lowers confidence (or `WristAnalyzer` refuses the shot) when calibration is stale. The wizard already exposes the step: `hasCalibrateStep == (sessionType === Wrist)` enables **Calibrate (step 4) + Confirm (step 5)** in `ScreenSessionWizard.qml`; `calibFlow.calibrationDone` gates completion. M1 fixes the `handMount`/label bugs, adds the capture-time stamp + 30-min expiry, and prompts re-cal when stale.

---

## 4. Kinematic chain & angle math

All math is `QQuaternion`; **Euler appears only at the final UI label** (project hard rule). Per binding, apply `q_anat = A·q_raw·M` once. Form per-joint relative quaternions, **address-referenced in quaternion space** before any decomposition:

```cpp
QQuaternion qWrist = (qFore_anat.conjugated() * qHand_anat).normalized();   // forearm⁻¹·hand
QQuaternion qElbow = (qUpper_anat.conjugated() * qFore_anat).normalized();  // upperarm⁻¹·forearm
// reference to Address (subtract neutral in quaternion space, NOT on angles):
QQuaternion qWristRel = (qWristAddr.conjugated() * qWrist).normalized();
QQuaternion qElbowRel = (qElbowAddr.conjugated() * qElbow).normalized();
```

### Wrist FE & RUD (cross-talk-safe, ISB JCS)

Naive per-axis `atan2` on three independent axes leaks large FE into the RUD channel. Use **swing-twist to peel off hand axial**, then read the **swing** with the ISB Cardan **Z(FE)–X(RUD)–Y(axial)** order:

```cpp
// swing-twist about hand long axis (anatomical Y, post-A·M)
const QVector3D yHand(0,1,0);
QVector3D r(qWristRel.x(), qWristRel.y(), qWristRel.z());
QVector3D p = QVector3D::dotProduct(r, yHand) * yHand;
QQuaternion twist = QQuaternion(qWristRel.scalar(), p.x(), p.y(), p.z()).normalized();
QQuaternion swing = (qWristRel * twist.conjugated()).normalized();    // FE + RUD only
QMatrix3x3 R = swing.toRotationMatrix();
double feRad  = std::atan2(-R(0,1), R(1,1));            // flexion(+) / extension(−), about proximal Z
double rudRad = std::asin(std::clamp(R(2,1),-1.0,1.0)); // ulnar(+) / radial(−), floating axis
```

Verify axis indices/signs against a **known cup/bow pose** during bring-up (the X=volar, Y=long, Z=radial convention may flip a sign per build). `leadWristFlexExt = +feRad`, `leadWristRadUln = +rudRad`.

### Forearm pronation & elbow flexion (one relative quaternion, swing-twist)

```cpp
const QVector3D yFore(0,1,0);                  // forearm long axis (elbow→wrist)
QVector3D re(qElbowRel.x(), qElbowRel.y(), qElbowRel.z());
QVector3D pe = QVector3D::dotProduct(re, yFore) * yFore;
QQuaternion eTwist = QQuaternion(qElbowRel.scalar(), pe.x(), pe.y(), pe.z()).normalized();
QQuaternion eSwing = (qElbowRel * eTwist.conjugated()).normalized();
// twist about long axis = pronation(+)/supination(−); swing magnitude = elbow flexion
double pronRad  = 2.0 * std::atan2(std::copysign(pe.length(), QVector3D::dotProduct(pe,yFore)),
                                   qElbowRel.scalar());
double flexRad  = 2.0 * std::atan2(QVector3D(eSwing.x(),eSwing.y(),eSwing.z()).length(),
                                   eSwing.scalar());
```

**180° swing-twist singularity** (|re|²<eps): rotate `yFore` by `qElbowRel`, take `cross(yFore, rotated)` as the swing axis (identity if parallel), assign a 180° twist — required because the elbow can approach it; the wrist Cardan middle angle only locks near ±90°, outside human ROM.

**Left-arm (lead) sign mirroring:** for the lead = left arm, flip the X and Z anatomical-axis signs (or negate FE/RUD/pron as needed) so flexion/pronation/ulnar stay **positive**, matching the right-arm convention and the existing `mirroredSource` handling. Without it, bow↔cup inverts.

**No upper-arm IMU (slot C absent — the optional case).** `q_upperarm` is unavailable, so `forearmPronation` is **suppressed** (omitted, or emitted `estimated`) and `leadArmFlexion` falls back to the camera 2D elbow angle (§5). Wrist FE/RUD are **unaffected** — they need only the forearm + hand IMUs. `WristAnalyzer` branches on whether the `LeadUpperArm` binding is present.

### Outputs

* **Skeleton:** publish per-bone parent-local `QQuaternion`s for `SegmentRole::LeadUpperArm/LeadForearm/LeadHand` into the `SkeletonTimeline` (`segQuat[]`). For the offline/detail visual, `BodyPoseAdapter` gains an "offline-skeleton" path that consumes these quaternions directly (the live path is camera-COCO-driven and has no hand bone) so `BodyVizView`/`ArmVizView`'s lead-arm chain renders the analyzed swing.
* **Metrics:** emit a `MetricSeries` per metric (full 100 Hz history + scalar at scoring phase).

---

## 5. Camera fusion role

The face-on 2D skeleton is **complementary, never authoritative** for joint angles.

**It adds:** (1) **global arm placement** — IMUs give orientation, not position; the camera locates shoulder/elbow/wrist in frame. (2) **Lead-arm (elbow) flexion cross-check** — the *only* IMU angle a 2D view can independently estimate: `elbow2D = angle(shoulder→elbow, wrist→elbow)`. Assert `|elbow2D − leadArmFlexion| < tol` (camera noise + ~5–10° IMU); on disagreement flag a tracking error but keep the IMU value. (3) **Frontal-plane arm path/plane & tempo.** (4) **Phase/impact backup** — corroborates `job.impactUs`. (5) The visual overlay.

**It cannot add:** wrist FE/RUD or forearm pronation — these are out-of-plane/axial and **unobservable** from a single 2D view (depth is ill-posed). The IMU stays authoritative for all four metrics; the camera is placement + one redundant flexion channel + phase + visual. Per the design's reconstruction-tier table, Wrist FE/RUD/pronation are sourced **IMU full**, `leadArmFlexion` is **camera-2D with IMU cross-check**. This is `ImuVisionFuser`'s *orientation slice* only — no Kabsch world-yaw alignment or occlusion-fill is required at M1 because positions aren't reconstructed.

---

## 6. Metrics & scoring

`MetricExtractor` reads each metric at its **matched phase event** from `PhaseSegmenter` (anchored on `job.impactUs`; Top via lead-hand angular-velocity reversal; Address re-anchored at the pre-roll start). All angles are **relative to Address**.

| Metric key | Formula | Phase(s) | Ideal / Tour band (HackMotion) | Band shape |
|---|---|---|---|---|
| `leadWristFlexExt` | Cardan-1 of `q_forearm⁻¹·q_hand` (FE+) | Top, **Impact** | Impact **15–30° more flexed** than address (flat→slightly bowed); Top within ~−30°…+5° of address | **one-sided** — penalize **cupping** (added extension) at Top/Impact; bowed side clamped to 100 |
| `leadWristRadUln` | Cardan-2 (ulnar+) | Top, Impact | radial set/hold at top normal; large ulnar at top OK | two-sided, wide |
| `forearmPronation` | `twist(q_upperarm⁻¹·q_forearm, elbow→wrist)` | Top→Impact | rolls toward square through impact | two-sided |
| `leadArmFlexion` | elbow swing angle (IMU) + 2D cross-check | Top, Impact | near-straight ~165–180° through impact | one-sided (flexed = fault) |

› *`forearmPronation` requires the optional upper-arm IMU (slot C); without it it is omitted/`estimated`, and `leadArmFlexion` uses the camera 2D elbow angle. Wrist FE/RUD are always available.*

**Scoring (`SwingScorer`).** Per metric: tolerance-band **clipped-z** sub-score, `zIn≈1.0`, `zOut≈3.0` (`band ∈ green/yellow/red`); **one-sided** metrics clamp the good side to 100 (cupped lead wrist is a fault, bowed is not). Aggregate via the weighted **geometric** mean → `ScoreBreakdown`. `FaultRanker` keys `(metric, phase, sign)`: e.g. positive `leadWristFlexExt` at impact → *"Cupped lead wrist / open face — loss of compression"*. The clubface coupling (~1° FE ≈ 1° face) is surfaced as an **approximate** cue, never a measured metric.

**UI wiring (`ScreenWrist.qml`).** The carousel's `metricKeys` is `["wristAngleTop","impactConditions","trailWristExtension","transition"]` and `PpShotPanel` renders `metrics[key]={label,value}`, showing `—` for any missing key. **Reconcile the vocabulary**: update `ScreenWrist.qml:120` `metricKeys` to the four real IDs `["leadWristFlexExt","leadWristRadUln","forearmPronation","leadArmFlexion"]` and have `WristAnalyzer` fill exactly those keys with `{label, value}` (e.g. `{"Lead wrist · impact","18° flexed"}`). Keep `traceLabel: "LEAD-WRIST FLEXION · ADDRESS → IMPACT"` and supply the real `leadWristFlexExt` trace as the normalized `tracePoints`. `ShotProcessor::maybeJoin()` already forwards `metrics`/`tracePoints`/`score` to `ShotListModel::addShot()` unchanged.

---

## 7. Implementation plan

**New / changed files (reuse committed names):**

1. `src/Analysis/segment_role.h` — `enum class SegmentRole` + `ImuSegmentBinding{source, role, alignA, mountM}` (per design §110/§131).
2. `src/Analysis/shot_analyzer.h` — widen `ShotAnalysisJob` with `std::vector<ImuSegmentBinding> imuBindings`, `handedness`, `cameraFixedInPlace`.
3. `src/Gui/shot_processor.cpp` (`startAnalysis()`, UI thread) — resolver: `imuPlacement[deviceId]` + Wrist slot table → `SegmentRole`; snapshot `ImuInstance::sourceId()/alignA()/mountM()`; fill `imuBindings`.
4. `src/Analysis/biomech_analyzer.{h,cpp}` + `src/Analysis/wrist_analyzer.{h,cpp}` — `WristAnalyzer : BiomechAnalyzer`; **`makeShotAnalyzer(1)` returns `WristAnalyzer`** (replaces `WristStubAnalyzer`, `shot_analyzer.cpp:126`).
5. `src/Analysis/fusion/imu_vision_fuser.{h,cpp}` — *orientation slice*: per binding `q_anat = A·q_raw·M`, resampled per-segment quaternion stream. Reuse `imu_calibration.h` (header-only, no Qt-UI deps).
6. `src/Analysis/metrics/metric_extractor.{h,cpp}` — the §4 wrist/forearm/elbow extraction → `MetricSeries`; **guards `forearmPronation` / IMU-elbow on the `LeadUpperArm` binding being present** (else degrade per §4).
7. `src/Analysis/phase/phase_segmenter.{h,cpp}` — Address/Top/Impact from IMU dynamics + `job.impactUs`.
8. `src/Analysis/score/swing_scorer.{h,cpp}` — one-sided bands + geometric mean → `ScoreBreakdown`.
9. `src/Gui/ImuCalibrationFlow.qml` — **fix `handMount=true` for slot B**; fix `nameFor()` label.
10. `src/Gui/ScreenSessionWizard.qml` — relabel Wrist slot A `"Wrist"`→`"Forearm"`; **keep `requiredImus:2`, slot C optional** (pronation/elbow degrade when absent).
11. `src/Gui/ScreenWrist.qml` — `metricKeys` → the four real IDs.
12. `src/Gui/imu_instance.{h,cpp}` — stamp each calibration with a capture time; expose `calibrationValid` (≤ 30 min, invalidated on reconnect) + `calibAgeMs`. **Do not** persist to `AppSettings::imuCalibration` — calibration is session-only by design.
13. `src/Gui/body_pose_adapter.{h,cpp}` — offline-skeleton path consuming `SkeletonTimeline` quaternions.

**Sequencing:** (1) types + job widen + resolver (no behavior change, stub still runs) → (2) `ImuVisionFuser` orientation slice + unit tests on `q_anat` → (3) `MetricExtractor` angle math + held-angle test → (4) `PhaseSegmenter` → (5) `WristAnalyzer` wires layers, dispatch flip → (6) `SwingScorer` bands → (7) calibration bug-fixes + 30-min session-validity stamp + wizard slot-A relabel → (8) `ScreenWrist` keys + `BodyPoseAdapter` offline viz.

**Dependencies:** `imu_calibration.h`, `SwingWindow`/`ImuSample`, `AppSettings`, `ImuInstance` A/M accessors — all present. **No new ONNX** (RTMW-l hand-crop cross-check is deferred; MoveNet COCO-17 suffices for placement + elbow).

**Exit criteria / validation:**
* **Held known angle:** arm in a jig at a fixed elbow angle (e.g. 90°) → `q_upperarm⁻¹·q_forearm` decoded elbow flexion within **±5°**; report **RMSE and offset** (high correlation can hide a constant offset).
* **HackMotion-plausible bands:** Address ~10–25° extension; Impact 15–30° more flexed; cupping at Top flagged as a fault.
* **Sign conventions:** bow→negative/closing, cup→positive/opening hold for the **lead = left** arm; flip x/z mirroring verified against a deliberate cup and bow pose.
* **Join robustness:** a failed analysis returns `ok=false` with an `error` string — never crashes `maybeJoin()` (degrades to score 0 / `—` / no replay).

---

## 8. Risks & open questions (3-IMU lead-arm rig)

* **Soft-tissue artifact (forearm/upper-arm straps).** Skin/strap motion under downswing acceleration biases pronation more than any math error. Mitigate with tight straps and a static-pose recheck; warn if a re-check disagrees with stored `A`.
* **Glove / hand IMU mounting.** `nominalHandMount()` is a *dorsal* numerically-solved constant, not a long-axis rotation of the arm nominal — sensitive to where on the glove back it sits. The `handMount=true` fix is mandatory; consider a hand-specific functional check.
* **Calibration repeatability.** Two-pose static accuracy is the floor (~6–8° elbow). Non-expert golfers produce sloppy sweeps; gate on the existing stillness + two-gate (15°/25°) accept, and PCA-reject functional sweeps where one gyro axis doesn't dominate. **Resolved:** calibration is **session-only with a 30-min validity window** — a re-strap or >30 min of drift ⇒ re-calibrate; no cross-restart/reconnect persistence (it would resurrect a drifted frame).
* **Heading drift (6-axis, no magnetometer).** Tilt is gravity-bounded; about-vertical yaw drifts and **only cancels in relative quaternions if all three IMUs share one drift datum** — they do (common gravity-seeded world). the **30-min calibration timeout is exactly this mid-session re-reference**; differential bias between sensors within the window is the residual risk.
* **Elbow hyperextension / singularity.** Near-straight impact pushes the elbow swing toward the 180° swing-twist singularity — the cross-product fallback is mandatory, and `leadArmFlexion` near 180° must not wrap.
* **Camera cross-check degradation.** Face-on 2D elbow angle is valid only in the frontal plane; as the arm swings out of plane the cross-check tolerance must widen or suspend — never let a stale camera disagreement override the IMU value.
